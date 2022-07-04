/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface.
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#ifdef SHOW_LOGTRACE
#include <linux/syscalls.h>
#include <event_log.h>
#endif /* SHOW_LOGTRACE */

#if defined(PCIE_FULL_DONGLE) || defined(SHOW_LOGTRACE)
#include <bcmmsgbuf.h>
#endif /* PCIE_FULL_DONGLE */

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
#include <linux/irq.h>
#if defined(CONFIG_TIZEN)
#include <linux/net_stat_tizen.h>
#endif /* CONFIG_TIZEN */
#include <net/addrconf.h>
#ifdef ENABLE_ADAPTIVE_SCHED
#include <linux/cpufreq.h>
#endif /* ENABLE_ADAPTIVE_SCHED */
#include <linux/rtc.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <dhd_linux_priv.h>
#if defined(CUSTOMER_HW_ROCKCHIP) && defined(BCMPCIE)
#include <rk_dhd_pcie_linux.h>
#endif /* CUSTOMER_HW_ROCKCHIP && BCMPCIE */

#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <bcmdevs_legacy.h>    /* need to still support chips no longer in trunk firmware */
#include <bcmiov.h>
#include <bcmstdlib_s.h>

#include <ethernet.h>
#include <bcmevent.h>
#include <vlan.h>
#include <802.3.h>

#ifdef WL_NANHO
#include <nanho.h>
#endif /* WL_NANHO */
#include <dhd_linux_wq.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhd_linux_pktdump.h>
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
#include <dhd_dbg_ring.h>
#include <dhd_debug.h>
#if defined(WL_CFG80211)
#include <wl_cfg80211.h>
#ifdef WL_BAM
#include <wl_bam.h>
#endif	/* WL_BAM */
#endif	/* WL_CFG80211 */
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif
#ifdef DHD_TIMESYNC
#include <dhd_timesync.h>
#include <linux/ip.h>
#include <net/icmp.h>
#endif /* DHD_TIMESYNC */

#include <dhd_linux_sock_qos.h>

#ifdef CSI_SUPPORT
#include <dhd_csi.h>
#endif /* CSI_SUPPORT */

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#ifdef CONFIG_ARCH_EXYNOS
#ifndef SUPPORT_EXYNOS7420
#include <linux/exynos-pci-ctrl.h>
#endif /* SUPPORT_EXYNOS7420 */
#endif /* CONFIG_ARCH_EXYNOS */

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

#ifdef AMPDU_VO_ENABLE
/* XXX: Enabling VO AMPDU to reduce FER */
#include <802.1d.h>
#endif /* AMPDU_VO_ENABLE */

#if defined(DHDTCPACK_SUPPRESS) || defined(DHDTCPSYNC_FLOOD_BLK)
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS || DHDTCPSYNC_FLOOD_BLK */
#include <dhd_daemon.h>
#ifdef DHD_PKT_LOGGING
#include <dhd_pktlog.h>
#endif /* DHD_PKT_LOGGING */
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
#include <eapol.h>
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */
#ifdef DHD_DEBUG_PAGEALLOC
typedef void (*page_corrupt_cb_t)(void *handle, void *addr_corrupt, size_t len);
void dhd_page_corrupt_cb(void *handle, void *addr_corrupt, size_t len);
extern void register_page_corrupt_cb(page_corrupt_cb_t cb, void* handle);
#endif /* DHD_DEBUG_PAGEALLOC */

#if defined(DHD_TCP_WINSIZE_ADJUST)
#include <linux/tcp.h>
#include <net/tcp.h>
#endif /* DHD_TCP_WINSIZE_ADJUST */

#ifdef ENABLE_DHD_GRO
#include <net/sch_generic.h>
#endif /* ENABLE_DHD_GRO */

#define IP_PROT_RESERVED	0xFF

#ifdef DHD_MQ
#define MQ_MAX_QUEUES AC_COUNT
#define MQ_MAX_CPUS 16
int enable_mq = TRUE;
module_param(enable_mq, int, 0644);
int mq_select_disable = FALSE;
#endif

#ifdef BCMINTERNAL
#ifdef DHD_FWTRACE
#include <dhd_fwtrace.h>
#endif /* DHD_FWTRACE */
#endif /* BCMINTERNAL */

#if defined(DHD_LB)
#if !defined(PCIE_FULL_DONGLE)
#error "DHD Loadbalancing only supported on PCIE_FULL_DONGLE"
#endif /* !PCIE_FULL_DONGLE */
#endif /* DHD_LB */

#if defined(DHD_LB_RXP) || defined(DHD_LB_TXP) || defined(DHD_LB_STATS)
#if !defined(DHD_LB)
#error "DHD loadbalance derivatives are supported only if DHD_LB is defined"
#endif /* !DHD_LB */
#endif /* DHD_LB_RXP || DHD_LB_TXP || DHD_LB_STATS */

#ifdef DHD_4WAYM4_FAIL_DISCONNECT
static void dhd_m4_state_handler(struct work_struct * work);
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT) && defined(DHD_FW_COREDUMP)
static int dhd_wait_for_file_dump(dhd_pub_t *dhdp);
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT && DHD_FW_COREDUMP */

#ifdef FIX_CPU_MIN_CLOCK
#include <linux/pm_qos.h>
#endif /* FIX_CPU_MIN_CLOCK */

#ifdef ENABLE_ADAPTIVE_SCHED
#define DEFAULT_CPUFREQ_THRESH		1000000	/* threshold frequency : 1000000 = 1GHz */
#ifndef CUSTOM_CPUFREQ_THRESH
#define CUSTOM_CPUFREQ_THRESH	DEFAULT_CPUFREQ_THRESH
#endif /* CUSTOM_CPUFREQ_THRESH */
#endif /* ENABLE_ADAPTIVE_SCHED */

/* enable HOSTIP cache update from the host side when an eth0:N is up */
#define AOE_IP_ALIAS_SUPPORT 1

#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif

#if defined(OEM_ANDROID)
#include <wl_android.h>
#endif

/* Maximum STA per radio */
#if defined(BCM_ROUTER_DHD)
#define DHD_MAX_STA     128
#else
#define DHD_MAX_STA     32
#endif /* BCM_ROUTER_DHD */

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
#include <ctf/hndctf.h>

#ifdef CTFPOOL
#define RXBUFPOOLSZ		2048
#define	RXBUFSZ			DHD_FLOWRING_RX_BUFPOST_PKTSZ /* packet data buffer size */
#endif /* CTFPOOL */
#endif /* BCM_ROUTER_DHD && HNDCTF */

#ifdef BCMDBG
#include <dhd_macdbg.h>
#endif /* BCMDBG */

#ifdef DHD_EVENT_LOG_FILTER
#include <dhd_event_log_filter.h>
#endif /* DHD_EVENT_LOG_FILTER */

#ifdef DHDTCPSYNC_FLOOD_BLK
static void dhd_blk_tsfl_handler(struct work_struct * work);
#endif /* DHDTCPSYNC_FLOOD_BLK */

#ifdef WL_NATOE
#include <dhd_linux_nfct.h>
#endif /* WL_NATOE */

#ifdef DHD_TX_PROFILE
#include <bcmarp.h>
#include <bcmicmp.h>
#include <bcmudp.h>
#include <bcmproto.h>
#endif /* defined(DHD_TX_PROFILE) */

#if defined(DHD_TCP_WINSIZE_ADJUST)
static uint target_ports[MAX_TARGET_PORTS] = {20, 0, 0, 0, 0};
static uint dhd_use_tcp_window_size_adjust = FALSE;
static void dhd_adjust_tcp_winsize(int op_mode, struct sk_buff *skb);
#endif /* DHD_TCP_WINSIZE_ADJUST */

#ifdef SET_RANDOM_MAC_SOFTAP
#ifndef CONFIG_DHD_SET_RANDOM_MAC_VAL
#define CONFIG_DHD_SET_RANDOM_MAC_VAL	0x001A11
#endif
static u32 vendor_oui = CONFIG_DHD_SET_RANDOM_MAC_VAL;
#endif /* SET_RANDOM_MAC_SOFTAP */

#if defined(BCM_ROUTER_DHD)
/*
 * Queue budget: Minimum number of packets that a queue must be allowed to hold
 * to prevent starvation.
 */
#define DHD_QUEUE_BUDGET_DEFAULT    (256)
int dhd_queue_budget = DHD_QUEUE_BUDGET_DEFAULT;

module_param(dhd_queue_budget, int, 0);

/*
 * Per station pkt threshold: Sum total of all packets in the backup queues of
 * flowrings belonging to the station, not including packets already admitted
 * to flowrings.
 */
#define DHD_STA_THRESHOLD_DEFAULT   (2048)
int dhd_sta_threshold = DHD_STA_THRESHOLD_DEFAULT;
module_param(dhd_sta_threshold, int, 0);

/*
 * Per interface pkt threshold: Sum total of all packets in the backup queues of
 * flowrings belonging to the interface, not including packets already admitted
 * to flowrings.
 */
#define DHD_IF_THRESHOLD_DEFAULT   (2048 * 32)
int dhd_if_threshold = DHD_IF_THRESHOLD_DEFAULT;
module_param(dhd_if_threshold, int, 0);
#endif /* BCM_ROUTER_DHD */

/* XXX: where does this belong? */
/* XXX: this needs to reviewed for host OS. */
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
 * created in the kernel notifier link list (with 'next' pointing to itself)
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

#if defined (CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = FALSE;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#ifdef ENABLE_WAKEUP_PKT_DUMP
volatile bool dhd_mmc_wake = FALSE;
long long temp_raw;
#endif /* ENABLE_WAKEUP_PKT_DUMP */
#endif /* defined(CONFIG_PM_SLEEP) */

#if defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID) || defined(FORCE_WOWLAN)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif /* defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID) */
#if defined(OEM_ANDROID)
static void dhd_hang_process(struct work_struct *work_data);
#endif /* OEM_ANDROID */
MODULE_LICENSE("GPL and additional rights");

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
DEFINE_MUTEX(_dhd_mutex_lock_);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
#endif
static int dhd_suspend_resume_helper(struct dhd_info *dhd, int val, int force);

#include <dhd_bus.h>

/* XXX Set up an MTU change notifier per linux/notifier.h? */
#ifndef PROP_TXSTATUS
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen)
#else
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen + 128)
#endif

#ifdef PROP_TXSTATUS
extern bool dhd_wlfc_skip_fc(void * dhdp, uint8 idx);
extern void dhd_wlfc_plat_init(void *dhd);
extern void dhd_wlfc_plat_deinit(void *dhd);
#endif /* PROP_TXSTATUS */
#ifdef USE_DYNAMIC_F2_BLKSIZE
extern uint sd_f2_blocksize;
extern int dhdsdio_func_blocksize(dhd_pub_t *dhd, int function_num, int block_size);
#endif /* USE_DYNAMIC_F2_BLKSIZE */

/* Linux wireless extension support */
#if defined(WL_WIRELESS_EXT)
#include <wl_iw.h>
#endif /* defined(WL_WIRELESS_EXT) */

#ifdef CONFIG_PARTIALSUSPEND_SLP
/* XXX SLP use defferent earlysuspend header file and some functions
 * But most of meaning is same as Android
 */
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

#ifdef CONFIG_IRQ_HISTORY
#include <linux/power/irq_history.h>
#endif /* CONFIG_IRQ_HISTORY */

#if defined(OEM_ANDROID)
#include <linux/nl80211.h>
#endif /* OEM_ANDROID */

#if defined(PKT_FILTER_SUPPORT) && defined(APF)
static int __dhd_apf_add_filter(struct net_device *ndev, uint32 filter_id,
	u8* program, uint32 program_len);
static int __dhd_apf_config_filter(struct net_device *ndev, uint32 filter_id,
	uint32 mode, uint32 enable);
static int __dhd_apf_delete_filter(struct net_device *ndev, uint32 filter_id);
#endif /* PKT_FILTER_SUPPORT && APF */

#ifdef DHD_FW_COREDUMP
static void dhd_mem_dump(void *dhd_info, void *event_info, u8 event);
#endif /* DHD_FW_COREDUMP */

#ifdef DHD_LOG_DUMP

struct dhd_log_dump_buf g_dld_buf[DLD_BUFFER_NUM];

/* Only header for log dump buffers is stored in array
 * header for sections like 'dhd dump', 'ext trap'
 * etc, is not in the array, because they are not log
 * ring buffers
 */
dld_hdr_t dld_hdrs[DLD_BUFFER_NUM] = {
		{GENERAL_LOG_HDR, LOG_DUMP_SECTION_GENERAL},
		{PRESERVE_LOG_HDR, LOG_DUMP_SECTION_PRESERVE},
		{SPECIAL_LOG_HDR, LOG_DUMP_SECTION_SPECIAL}
};
static int dld_buf_size[DLD_BUFFER_NUM] = {
		LOG_DUMP_GENERAL_MAX_BUFSIZE,	/* DLD_BUF_TYPE_GENERAL */
		LOG_DUMP_PRESERVE_MAX_BUFSIZE,	/* DLD_BUF_TYPE_PRESERVE */
		LOG_DUMP_SPECIAL_MAX_BUFSIZE,	/* DLD_BUF_TYPE_SPECIAL */
};

static void dhd_log_dump_init(dhd_pub_t *dhd);
static void dhd_log_dump_deinit(dhd_pub_t *dhd);
static void dhd_log_dump(void *handle, void *event_info, u8 event);
static int do_dhd_log_dump(dhd_pub_t *dhdp, log_dump_type_t *type);
static void dhd_print_buf_addr(dhd_pub_t *dhdp, char *name, void *buf, unsigned int size);
static void dhd_log_dump_buf_addr(dhd_pub_t *dhdp, log_dump_type_t *type);
char *dhd_dbg_get_system_timestamp(void);
#endif /* DHD_LOG_DUMP */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

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

#ifdef OEM_ANDROID
#ifdef BCMPCIE
static int is_reboot = 0;
#endif /* BCMPCIE */
#endif /* OEM_ANDROID */

dhd_pub_t	*g_dhd_pub = NULL;

#if defined(BT_OVER_SDIO)
#include "dhd_bt_interface.h"
#endif /* defined (BT_OVER_SDIO) */

#ifdef WL_NANHO
static int dhd_nho_ioctl_process(dhd_pub_t *pub, int ifidx, dhd_ioctl_t *ioc, void *data_buf);
static int dhd_nho_ioctl_cb(void *drv_ctx, int ifidx, wl_ioctl_t *ioc, bool drv_lock);
static int dhd_nho_evt_cb(void *drv_ctx, int ifidx, bcm_event_t *evt, uint16 evt_len);
#endif /* WL_NANHO */

#ifdef WL_STATIC_IF
bool dhd_is_static_ndev(dhd_pub_t *dhdp, struct net_device *ndev);
#endif /* WL_STATIC_IF */

atomic_t exit_in_progress = ATOMIC_INIT(0);

static void dhd_process_daemon_msg(struct sk_buff *skb);
static void dhd_destroy_to_notifier_skt(void);
static int dhd_create_to_notifier_skt(void);
static struct sock *nl_to_event_sk = NULL;
int sender_pid = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
struct netlink_kernel_cfg dhd_netlink_cfg = {
	.groups = 1,
	.input = dhd_process_daemon_msg,
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) */

#ifdef DHD_PKTTS
static int dhd_create_to_notifier_ts(void);
static void dhd_destroy_to_notifier_ts(void);

static struct sock *nl_to_ts = NULL;
int sender_pid_ts = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static void dhd_recv_msg_from_ts(struct sk_buff *skb);

struct netlink_kernel_cfg dhd_netlink_ts = {
	.groups = 1,
	.input = dhd_recv_msg_from_ts,
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) */

#define GET_METADATA_VER(val)		((uint16)((val & 0xffff0000) >> 16))
#define GET_METADATA_BUFLEN(val)	((uint16)(val & 0x0000ffff))
#endif /* DHD_PKTTS */

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
#if defined(OEM_ANDROID)
extern int wl_control_wl_start(struct net_device *dev);
#if defined(BCMLXSDMMC) || defined(BCMDBUS)
struct semaphore dhd_registration_sem;
#endif /* BCMXSDMMC */
#endif /* defined(OEM_ANDROID) */
void dhd_generate_rand_mac_addr(struct ether_addr *ea_addr);

#ifdef DHD_LOG_DUMP
int logdump_max_filesize = LOG_DUMP_MAX_FILESIZE;
module_param(logdump_max_filesize, int, 0644);
int logdump_max_bufsize = LOG_DUMP_GENERAL_MAX_BUFSIZE;
module_param(logdump_max_bufsize, int, 0644);
int logdump_periodic_flush = FALSE;
module_param(logdump_periodic_flush, int, 0644);
#ifdef EWP_ECNTRS_LOGGING
int logdump_ecntr_enable = TRUE;
#else
int logdump_ecntr_enable = FALSE;
#endif /* EWP_ECNTRS_LOGGING */
module_param(logdump_ecntr_enable, int, 0644);
#ifdef EWP_RTT_LOGGING
int logdump_rtt_enable = TRUE;
#else
int logdump_rtt_enable = FALSE;
#endif /* EWP_RTT_LOGGING */
int logdump_prsrv_tailsize = DHD_LOG_DUMP_MAX_TAIL_FLUSH_SIZE;
#endif /* DHD_LOG_DUMP */

#ifdef EWP_EDL
int host_edl_support = TRUE;
module_param(host_edl_support, int, 0644);
#endif

/* deferred handlers */
static void dhd_ifadd_event_handler(void *handle, void *event_info, u8 event);
static void dhd_ifdel_event_handler(void *handle, void *event_info, u8 event);
static void dhd_set_mac_addr_handler(void *handle, void *event_info, u8 event);
static void dhd_set_mcast_list_handler(void *handle, void *event_info, u8 event);
#ifdef BCM_ROUTER_DHD
static void dhd_inform_dhd_monitor_handler(void *handle, void *event_info, u8 event);
#endif
#ifdef WL_NATOE
static void dhd_natoe_ct_event_hanlder(void *handle, void *event_info, u8 event);
static void dhd_natoe_ct_ioctl_handler(void *handle, void *event_info, uint8 event);
#endif /* WL_NATOE */

#ifdef DHD_UPDATE_INTF_MAC
static void dhd_ifupdate_event_handler(void *handle, void *event_info, u8 event);
#endif /* DHD_UPDATE_INTF_MAC */
#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
static void dhd_inet6_work_handler(void *dhd_info, void *event_data, u8 event);
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
#ifdef WL_CFG80211
extern void dhd_netdev_free(struct net_device *ndev);
#endif /* WL_CFG80211 */
static dhd_if_t * dhd_get_ifp_by_ndev(dhd_pub_t *dhdp, struct net_device *ndev);

#if defined(WLDWDS) && defined(FOURADDR_AUTO_BRG)
static void dhd_bridge_dev_set(dhd_info_t *dhd, int ifidx, struct net_device *sdev);
#endif /* WLDWDS && FOURADDR_AUTO_BRG */

#if (defined(DHD_WET) || defined(DHD_MCAST_REGEN) || defined(DHD_L2_FILTER))
/* update rx_pkt_chainable state of dhd interface */
static void dhd_update_rx_pkt_chainable_state(dhd_pub_t* dhdp, uint32 idx);
#endif /* DHD_WET || DHD_MCAST_REGEN || DHD_L2_FILTER */

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
/* ARP offload agent mode : Enable ARP Host Auto-Reply and ARP Peer Auto-Reply */
/* XXX ARP HOST Auto Reply can cause dongle trap at VSDB situation */
/* XXX ARP OL SNOOP can be used to more good quility */

#ifdef ENABLE_ARP_SNOOP_MODE
uint dhd_arp_mode = (ARP_OL_AGENT | ARP_OL_PEER_AUTO_REPLY | ARP_OL_SNOOP | ARP_OL_HOST_AUTO_REPLY |
		ARP_OL_UPDATE_HOST_CACHE);
#else
uint dhd_arp_mode = ARP_OL_AGENT | ARP_OL_PEER_AUTO_REPLY | ARP_OL_UPDATE_HOST_CACHE;
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

/* wl event forwarding */
#ifdef WL_EVENT_ENAB
uint wl_event_enable = true;
#else
uint wl_event_enable = false;
#endif /* WL_EVENT_ENAB */
module_param(wl_event_enable, uint, 0660);

/* wl event forwarding */
#ifdef LOGTRACE_PKT_SENDUP
uint logtrace_pkt_sendup = true;
#else
uint logtrace_pkt_sendup = false;
#endif /* LOGTRACE_PKT_SENDUP */
module_param(logtrace_pkt_sendup, uint, 0660);

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
#if defined(OEM_ANDROID)
uint dhd_console_ms = 0;  /* XXX andrey by default no fw msg prints */
#else
uint dhd_console_ms = 250;
#endif /* OEM_ANDROID */
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
static dhd_if_t * dhd_get_ifp_by_ndev(dhd_pub_t *dhdp, struct net_device *ndev);

#ifdef DHD_MSI_SUPPORT
uint enable_msi = TRUE;
module_param(enable_msi, uint, 0);
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_SSSR_DUMP
int dhdpcie_sssr_dump_get_before_after_len(dhd_pub_t *dhd, uint32 *arr_len);
module_param(sssr_enab, uint, 0);
module_param(fis_enab, uint, 0);
#endif /* DHD_SSSR_DUMP */

/* Keep track of number of instances */
static int dhd_found = 0;
static int instance_base = 0; /* Starting instance number */
module_param(instance_base, int, 0644);

#if defined(DHD_LB_RXP) && defined(PCIE_FULL_DONGLE)
/*
 * Rx path process budget(dhd_napi_weight) number of packets in one go and hands over
 * the packets to network stack.
 *
 * dhd_dpc tasklet is the producer(packets received from dongle) and dhd_napi_poll()
 * is the consumer. The maximum number of packets that can be received from the dongle
 * at any given point of time are D2HRING_RXCMPLT_MAX_ITEM.
 * Also DHD will always post fresh rx buffers to dongle while processing rx completions.
 *
 * The consumer must consume the packets at equal are better rate than the producer.
 * i.e if dhd_napi_poll() does not process at the same rate as the producer(dhd_dpc),
 * rx_process_queue depth increases, which can even consume the entire system memory.
 * Such situation will be tacken care by rx flow control.
 *
 * Device drivers are strongly advised to not use bigger value than NAPI_POLL_WEIGHT
 */
static int dhd_napi_weight = NAPI_POLL_WEIGHT;
module_param(dhd_napi_weight, int, 0644);
#endif /* DHD_LB_RXP && PCIE_FULL_DONGLE */

#ifdef PCIE_FULL_DONGLE
extern int h2d_max_txpost;
module_param(h2d_max_txpost, int, 0644);

#if defined(DHD_HTPUT_TUNABLES)
extern int h2d_htput_max_txpost;
module_param(h2d_htput_max_txpost, int, 0644);
#endif /* DHD_HTPUT_TUNABLES */

#ifdef AGG_H2D_DB
extern bool agg_h2d_db_enab;
module_param(agg_h2d_db_enab, bool, 0644);
extern uint agg_h2d_db_timeout;
module_param(agg_h2d_db_timeout, uint, 0644);
extern uint agg_h2d_db_inflight_thresh;
module_param(agg_h2d_db_inflight_thresh, uint, 0644);
#endif /* AGG_H2D_DB */

extern uint dma_ring_indices;
module_param(dma_ring_indices, uint, 0644);

extern bool h2d_phase;
module_param(h2d_phase, bool, 0644);
extern bool force_trap_bad_h2d_phase;
module_param(force_trap_bad_h2d_phase, bool, 0644);
#endif /* PCIE_FULL_DONGLE */

#ifdef FORCE_TPOWERON
/*
 * On Fire's reference platform, coming out of L1.2,
 * there is a constant delay of 45us between CLKREQ# and stable REFCLK
 * Due to this delay, with tPowerOn < 50
 * there is a chance of the refclk sense to trigger on noise.
 *
 * 0x29 when written to L1SSControl2 translates to 50us.
 */
#define FORCE_TPOWERON_50US 0x29
uint32 tpoweron_scale = FORCE_TPOWERON_50US; /* default 50us */
module_param(tpoweron_scale, uint, 0644);
#endif /* FORCE_TPOWERON */

#ifdef SHOW_LOGTRACE
#ifdef DHD_LINUX_STD_FW_API
static char *logstrs_path = "logstrs.bin";
char *st_str_file_path = "rtecdc.bin";
static char *map_file_path = "rtecdc.map";
static char *rom_st_str_file_path = "roml.bin";
static char *rom_map_file_path = "roml.map";
#else
static char *logstrs_path = PLATFORM_PATH"logstrs.bin";
char *st_str_file_path = PLATFORM_PATH"rtecdc.bin";
static char *map_file_path = PLATFORM_PATH"rtecdc.map";
static char *rom_st_str_file_path = PLATFORM_PATH"roml.bin";
static char *rom_map_file_path = PLATFORM_PATH"roml.map";
#endif /* DHD_LINUX_STD_FW_API */

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

#if defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL)
static void gdb_proxy_fs_try_create(dhd_info_t *dhd, const char *dev_name);
static void gdb_proxy_fs_remove(dhd_info_t *dhd);
#endif /* defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL) */

#ifdef D2H_MINIDUMP
void dhd_d2h_minidump(dhd_pub_t *dhdp);
#endif /* D2H_MINIDUMP */

#define DHD_MEMDUMP_TYPE_STR_LEN 32
#define DHD_MEMDUMP_PATH_STR_LEN 128

#ifdef DHD_TX_PROFILE
/* process layer 3 headers, to ultimately determine if a
 * dhd_tx_profile_protocol_t matches
 */
static int process_layer3_headers(uint8 **p, int plen, uint16 *type);

/* process layer 2 headers, to ultimately determine if a
 * dhd_tx_profile_protocol_t matches
 */
static int process_layer2_headers(uint8 **p, int *plen, uint16 *type, bool is_host_sfhllc);

/* whether or not a dhd_tx_profile_protocol_t matches with data in a packet */
bool dhd_protocol_matches_profile(uint8 *p, int plen, const
		dhd_tx_profile_protocol_t *proto, bool is_host_sfhllc);
#endif /* defined(DHD_TX_PROFILE) */

#define PATH_BANDLOCK_INFO PLATFORM_PATH".bandlock.info"

static void dhd_set_bandlock(dhd_pub_t * dhd);

static void
dhd_tx_stop_queues(struct net_device *net)
{
#ifdef DHD_MQ
	netif_tx_stop_all_queues(net);
#else
	netif_stop_queue(net);
#endif
}

static void
dhd_tx_start_queues(struct net_device *net)
{
#ifdef DHD_MQ
	netif_tx_wake_all_queues(net);
#else
	netif_wake_queue(net);
#endif
}

#ifdef USE_WFA_CERT_CONF
int g_frameburst = 1;
#endif /* USE_WFA_CERT_CONF */

static int dhd_get_pend_8021x_cnt(dhd_info_t *dhd);

#ifdef PCIE_FULL_DONGLE
#define DHD_IF_STA_LIST_LOCK_INIT(lock) spin_lock_init(lock)

#if defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP)
static struct list_head * dhd_sta_list_snapshot(dhd_info_t *dhd, dhd_if_t *ifp,
	struct list_head *snapshot_list);
static void dhd_sta_list_snapshot_free(dhd_info_t *dhd, struct list_head *snapshot_list);
#define DHD_IF_WMF_UCFORWARD_LOCK(dhd, ifp, slist) ({ dhd_sta_list_snapshot(dhd, ifp, slist); })
#define DHD_IF_WMF_UCFORWARD_UNLOCK(dhd, slist) ({ dhd_sta_list_snapshot_free(dhd, slist); })
#endif /* DHD_IGMP_UCQUERY || DHD_UCAST_UPNP */
#endif /* PCIE_FULL_DONGLE */

/* Control fw roaming */
#ifdef BCMCCX
uint dhd_roam_disable = 0;
#else
#ifdef OEM_ANDROID
uint dhd_roam_disable = 0;
#else
uint dhd_roam_disable = 1;
#endif
#endif /* BCMCCX */

#ifdef BCMDBGFS
extern void dhd_dbgfs_init(dhd_pub_t *dhdp);
extern void dhd_dbgfs_remove(void);
#endif

/* Enable TX status metadta report: 0=disable 1=enable 2=debug */
static uint pcie_txs_metadata_enable = 0;
module_param(pcie_txs_metadata_enable, int, 0);

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

#ifdef BCMINTERNAL
extern uint dhd_anychip;
module_param(dhd_anychip, uint, 0);
#endif /* BCMINTERNAL */
#endif /* BCMSDIO */

#ifdef BCMSLTGT
#ifdef BCMFPGA_HW
/* For FPGA use fixed htclkration as 30 */
uint htclkratio = 30;
#else
uint htclkratio = 1;
#endif /* BCMFPGA_HW */
module_param(htclkratio, uint, 0);

int dngl_xtalfreq = 0;
module_param(dngl_xtalfreq, int, 0);
#endif /* BCMSLTGT */

#ifdef SDTEST
/* Echo packet generator (pkts/s) */
uint dhd_pktgen = 0;
module_param(dhd_pktgen, uint, 0);

/* Echo packet len (0 => sawtooth, max 2040) */
uint dhd_pktgen_len = 0;
module_param(dhd_pktgen_len, uint, 0);
#endif /* SDTEST */

#ifdef CUSTOM_DSCP_TO_PRIO_MAPPING
uint dhd_dscpmap_enable = 1;
module_param(dhd_dscpmap_enable, uint, 0644);
#endif /* CUSTOM_DSCP_TO_PRIO_MAPPING */

#if defined(BCMSUP_4WAY_HANDSHAKE)
/* Use in dongle supplicant for 4-way handshake */
#if defined(WLFBT) || defined(WL_ENABLE_IDSUP)
/* Enable idsup by default (if supported in fw) */
uint dhd_use_idsup = 1;
#else
uint dhd_use_idsup = 0;
#endif /* WLFBT || WL_ENABLE_IDSUP */
module_param(dhd_use_idsup, uint, 0);
#endif /* BCMSUP_4WAY_HANDSHAKE */

#ifndef BCMDBUS
#if defined(OEM_ANDROID)
/* Allow delayed firmware download for debug purpose */
int allow_delay_fwdl = FALSE;
#elif defined(BCM_ROUTER_DHD)
/* Allow delayed firmware download for debug purpose */
int allow_delay_fwdl = FALSE;
#else
int allow_delay_fwdl = TRUE;
#endif /* OEM_ANDROID */
module_param(allow_delay_fwdl, int, 0);
#endif /* !BCMDBUS */

#ifdef GDB_PROXY
/* Adds/replaces deadman_to= in NVRAM file with deadman_to=0 */
static uint nodeadman = 0;
module_param(nodeadman, uint, 0);
#endif /* GDB_PROXY */

#ifdef ECOUNTER_PERIODIC_DISABLE
uint enable_ecounter = FALSE;
#else
uint enable_ecounter = TRUE;
#endif
module_param(enable_ecounter, uint, 0);

#ifdef BCMQT_HW
int qt_flr_reset = FALSE;
module_param(qt_flr_reset, int, 0);

int qt_dngl_timeout = 0; // dongle attach timeout in ms
module_param(qt_dngl_timeout, int, 0);
#endif /* BCMQT_HW */

/* TCM verification flag */
uint dhd_tcm_test_enable = FALSE;
module_param(dhd_tcm_test_enable, uint, 0644);

extern char dhd_version[];
extern char fw_version[];
extern char clm_version[];

int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
static void dhd_net_if_lock_local(dhd_info_t *dhd);
static void dhd_net_if_unlock_local(dhd_info_t *dhd);
static void dhd_suspend_lock(dhd_pub_t *dhdp);
static void dhd_suspend_unlock(dhd_pub_t *dhdp);

/* Monitor interface */
int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);

#ifdef DHD_PM_CONTROL_FROM_FILE
bool g_pm_control;
#ifdef DHD_EXPORT_CNTL_FILE
uint32 pmmode_val = 0xFF;
#endif /* DHD_EXPORT_CNTL_FILE */
#ifdef CUSTOMER_HW10
void dhd_control_pm(dhd_pub_t *dhd, uint *);
#else
void sec_control_pm(dhd_pub_t *dhd, uint *);
#endif /* CUSTOMER_HW10 */
#endif /* DHD_PM_CONTROL_FROM_FILE */

#if defined(WL_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif /* defined(WL_WIRELESS_EXT) */

#ifdef DHD_PM_OVERRIDE
bool g_pm_override;
#endif /* DHD_PM_OVERRIDE */

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
	dhd_info_t *dhdinfo = (dhd_info_t*)container_of(nfb, const dhd_info_t, pm_notifier);
	dhd_pub_t *dhd = &dhdinfo->pub;
	struct dhd_conf *conf = dhd->conf;
	int suspend_mode = conf->suspend_mode;

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

	if(!dhd->early_suspended && suspend_mode != PM_NOTIFIER) {
		suspend_mode = PM_NOTIFIER;
		conf->suspend_mode = PM_NOTIFIER;
		conf->insuspend |= (NO_TXDATA_IN_SUSPEND | NO_TXCTL_IN_SUSPEND);
		printf("%s: switch suspend_mode to %d\n", __FUNCTION__, suspend_mode);
	}
	printf("%s: action=%ld, suspend=%d, suspend_mode=%d\n",
		__FUNCTION__, action, suspend, suspend_mode);
	if (suspend) {
		DHD_OS_WAKE_LOCK_WAIVE(dhd);
		if (suspend_mode == PM_NOTIFIER)
			dhd_suspend_resume_helper(dhdinfo, suspend, 0);
#if defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS)
		dhd_wlfc_suspend(dhd);
#endif /* defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS) */
		if (suspend_mode == PM_NOTIFIER || suspend_mode == SUSPEND_MODE_2)
			dhd_conf_set_suspend_resume(dhd, suspend);
		DHD_OS_WAKE_LOCK_RESTORE(dhd);
	} else {
		if (suspend_mode == PM_NOTIFIER || suspend_mode == SUSPEND_MODE_2)
			dhd_conf_set_suspend_resume(dhd, suspend);
#if defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS)
		dhd_wlfc_resume(dhd);
#endif /* defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS) */
		if (suspend_mode == PM_NOTIFIER)
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

#if defined(DHD_H2D_LOG_TIME_SYNC)
static void
dhd_deferred_work_rte_log_time_sync(void *handle, void *event_info, u8 event);
#endif /* DHD_H2D_LOG_TIME_SYNC */

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

/* Return interface pointer */
struct dhd_if * dhd_get_ifp(dhd_pub_t *dhdp, uint32 ifidx)
{
	ASSERT(ifidx < DHD_MAX_IFS);

	if (!dhdp || !dhdp->info || ifidx >= DHD_MAX_IFS)
		return NULL;

	return dhdp->info->iflist[ifidx];
}

#ifdef WLEASYMESH
int
dhd_set_1905_almac(dhd_pub_t *dhdp, uint8 ifidx, uint8* ea, bool mcast)
{
	dhd_if_t *ifp;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp(dhdp, ifidx);
	if (ifp == NULL) {
		return BCME_ERROR;
	}
	if (mcast) {
		memcpy(ifp->_1905_al_mcast, ea, ETHER_ADDR_LEN);
	} else {
		memcpy(ifp->_1905_al_ucast, ea, ETHER_ADDR_LEN);
	}
	return BCME_OK;
}
int
dhd_get_1905_almac(dhd_pub_t *dhdp, uint8 ifidx, uint8* ea, bool mcast)
{
	dhd_if_t *ifp;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp(dhdp, ifidx);
	if (ifp == NULL) {
		return BCME_ERROR;
	}
	if (mcast) {
		memcpy(ea, ifp->_1905_al_mcast, ETHER_ADDR_LEN);
	} else {
		memcpy(ea, ifp->_1905_al_ucast, ETHER_ADDR_LEN);
	}
	return BCME_OK;
}
#endif /* WLEASYMESH */

#ifdef PCIE_FULL_DONGLE

/** Dummy objects are defined with state representing bad|down.
 * Performance gains from reducing branch conditionals, instruction parallelism,
 * dual issue, reducing load shadows, avail of larger pipelines.
 * Use DHD_XXX_NULL instead of (dhd_xxx_t *)NULL, whenever an object pointer
 * is accessed via the dhd_sta_t.
 */

/* Dummy dhd_info object */
dhd_info_t dhd_info_null = {
	.pub = {
	         .info = &dhd_info_null,
#ifdef DHDTCPACK_SUPPRESS
	         .tcpack_sup_mode = TCPACK_SUP_REPLACE,
#endif /* DHDTCPACK_SUPPRESS */
#if defined(BCM_ROUTER_DHD)
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
#ifdef WMF
	.wmf = { .wmf_enable = TRUE },
#endif
	.info = DHD_INFO_NULL,
	.net = DHD_NET_DEV_NULL,
	.idx = DHD_BAD_IF
};
#define DHD_IF_NULL  (&dhd_if_null)

/* XXX should we use the sta_pool[0] object as DHD_STA_NULL? */
#define DHD_STA_NULL ((dhd_sta_t *)NULL)

/** Interface STA list management. */

/** Alloc/Free a dhd_sta object from the dhd instances' sta_pool. */
static void dhd_sta_free(dhd_pub_t *pub, dhd_sta_t *sta);
static dhd_sta_t * dhd_sta_alloc(dhd_pub_t * dhdp);

/* Delete a dhd_sta or flush all dhd_sta in an interface's sta_list. */
static void dhd_if_del_sta_list(dhd_if_t * ifp);

/* Construct/Destruct a sta pool. */
static int dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta);
static void dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta);
/* Clear the pool of dhd_sta_t objects for built-in type driver */
static void dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta);

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
			flow_ring_node_t * flow_ring_node;

#ifdef DHDTCPACK_SUPPRESS
			/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
			 * when there is a newly coming packet from network stack.
			 */
			dhd_tcpack_info_tbl_clean(dhdp);
#endif /* DHDTCPACK_SUPPRESS */

			flow_ring_node = dhd_flow_ring_node(dhdp, flowid);
			if (flow_ring_node) {
				flow_queue_t *queue = &flow_ring_node->queue;

				DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
				flow_ring_node->status = FLOW_RING_STATUS_STA_FREEING;

				if (!DHD_FLOW_QUEUE_EMPTY(queue)) {
					void * pkt;
					while ((pkt = dhd_flow_queue_dequeue(dhdp, queue)) !=
						NULL) {
						PKTFREE(dhdp->osh, pkt, TRUE);
					}
				}

				DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
				ASSERT(DHD_FLOW_QUEUE_EMPTY(queue));
			}
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

	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		GCC_DIAGNOSTIC_POP();
		list_del(&sta->list);
		dhd_sta_free(&ifp->info->pub, sta);
	}

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);

	return;
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

	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry(sta, &ifp->sta_list, list) {
		GCC_DIAGNOSTIC_POP();
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
			DHD_INFO(("%s: Found STA " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG((char *)ea)));
			DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);
			return sta;
		}
	}

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);

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

	if (!memcmp(ifp->net->dev_addr, ea, ETHER_ADDR_LEN)) {
		DHD_ERROR(("%s: Serious FAILURE, receive own MAC %pM !!\n", __FUNCTION__, ea));
		return DHD_STA_NULL;
	}

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

	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);

	list_add_tail(&sta->list, &ifp->sta_list);

	DHD_ERROR(("%s: Adding  STA " MACDBG "\n",
		__FUNCTION__, MAC2STRDBG((char *)ea)));

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);

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

	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		GCC_DIAGNOSTIC_POP();
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

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);

	return;
}

/** Delete STA from the interface's STA list. */
void
dhd_del_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta, *next;
	dhd_if_t *ifp;
	unsigned long flags;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return;

	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		GCC_DIAGNOSTIC_POP();
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
			DHD_ERROR(("%s: Deleting STA " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG(sta->ea.octet)));
			list_del(&sta->list);
			dhd_sta_free(&ifp->info->pub, sta);
		}
	}

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);
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
static struct list_head *
dhd_sta_list_snapshot(dhd_info_t *dhd, dhd_if_t *ifp, struct list_head *snapshot_list)
{
	unsigned long flags;
	dhd_sta_t *sta, *snapshot;

	INIT_LIST_HEAD(snapshot_list);

	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);

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

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);

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
#endif /* DHD_IGMP_UCQUERY || DHD_UCAST_UPNP */

#else
static inline void dhd_if_del_sta_list(dhd_if_t *ifp) {}
static inline int dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta) { return BCME_OK; }
static inline void dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta) {}
static inline void dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta) {}
dhd_sta_t *dhd_findadd_sta(void *pub, int ifidx, void *ea) { return NULL; }
dhd_sta_t *dhd_find_sta(void *pub, int ifidx, void *ea) { return NULL; }
void dhd_del_sta(void *pub, int ifidx, void *ea) {}
#endif /* PCIE_FULL_DONGLE */

#ifdef BCM_ROUTER_DHD
/** Bind a flowid to the dhd_sta's flowid table. */
void
dhd_add_flowid(dhd_pub_t * dhdp, int ifidx, uint8 ac_prio, void * ea,
                uint16 flowid)
{
	int prio;
	dhd_if_t * ifp;
	dhd_sta_t * sta;
	flow_queue_t * queue;

	ASSERT((dhdp != (dhd_pub_t *)NULL) && (ea != NULL));

	/* Fetch the dhd_if object given the if index */
	ifp = dhd_get_ifp(dhdp, ifidx);
	if (ifp == (dhd_if_t *)NULL) /* ifp fetched from dhdp iflist[] */
		return;

	/* Intializing the backup queue parameters */
	if (DHD_IF_ROLE_WDS(dhdp, ifidx) ||
#ifdef DHD_WET
		WET_ENABLED(dhdp) ||
#endif /* DHD_WET */
		0) {
		queue = dhd_flow_queue(dhdp, flowid);
		dhd_flow_ring_config_thresholds(dhdp, flowid,
			dhd_queue_budget, queue->max, DHD_FLOW_QUEUE_CLEN_PTR(queue),
			dhd_if_threshold, (void *)&ifp->cumm_ctr);
		return;
	} else if ((sta = dhd_find_sta(dhdp, ifidx, ea)) == DHD_STA_NULL) {
		/* Fetch the station with a matching Mac address. */
		/* Update queue's grandparent cummulative length threshold */
		if (ETHER_ISMULTI((char *)ea)) {
			queue = dhd_flow_queue(dhdp, flowid);
			if (ifidx != 0 && DHD_IF_ROLE_STA(dhdp, ifidx)) {
				/* Use default dhdp->cumm_ctr and dhdp->l2cumm_ctr,
				 * in PSTA mode the ifp will be deleted but we don't delete
				 * the PSTA flowring.
				 */
				dhd_flow_ring_config_thresholds(dhdp, flowid,
					queue->max, queue->max, DHD_FLOW_QUEUE_CLEN_PTR(queue),
					dhd_if_threshold, DHD_FLOW_QUEUE_L2CLEN_PTR(queue));
			}
			else if (DHD_FLOW_QUEUE_L2CLEN_PTR(queue) != (void *)&ifp->cumm_ctr) {
				dhd_flow_ring_config_thresholds(dhdp, flowid,
					queue->max, queue->max, DHD_FLOW_QUEUE_CLEN_PTR(queue),
					dhd_if_threshold, (void *)&ifp->cumm_ctr);
			}
		}
		return;
	}

	/* Set queue's min budget and queue's parent cummulative length threshold */
	dhd_flow_ring_config_thresholds(dhdp, flowid, dhd_queue_budget,
	                                dhd_sta_threshold, (void *)&sta->cumm_ctr,
	                                dhd_if_threshold, (void *)&ifp->cumm_ctr);

	/* Populate the flowid into the stations flowid table, for all packet
	 * priorities that would match the given flow's ac priority.
	 */
	for (prio = 0; prio < (int)NUMPRIO; prio++) {
		if (dhdp->flow_prio_map[prio] == ac_prio) {
			/* flowring shared for all these pkt prio */
			sta->flowid[prio] = flowid;
		}
	}
}

/** Unbind a flowid to the sta's flowid table. */
void
dhd_del_flowid(dhd_pub_t * dhdp, int ifidx, uint16 flowid)
{
	int prio;
	dhd_if_t * ifp;
	dhd_sta_t * sta;
	unsigned long flags;

	/* Fetch the dhd_if object given the if index */
	ifp = dhd_get_ifp(dhdp, ifidx);
	if (ifp == (dhd_if_t *)NULL) /* ifp fetched from dhdp iflist[] */
		return;

	/* Walk all stations and delete clear any station's reference to flowid */
	DHD_IF_STA_LIST_LOCK(&ifp->sta_list_lock, flags);

	list_for_each_entry(sta, &ifp->sta_list, list) {
		for (prio = 0; prio < (int)NUMPRIO; prio++) {
			if (sta->flowid[prio] == flowid) {
				sta->flowid[prio] = FLOWID_INVALID;
			}
		}
	}

	DHD_IF_STA_LIST_UNLOCK(&ifp->sta_list_lock, flags);
}
#endif /* BCM_ROUTER_DHD */

#if defined(DNGL_AXI_ERROR_LOGGING) && defined(DHD_USE_WQ_FOR_DNGL_AXI_ERROR)
void
dhd_axi_error_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	schedule_work(&dhd->axi_error_dispatcher_work);
}

static void dhd_axi_error_dispatcher_fn(struct work_struct * work)
{
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, axi_error_dispatcher_work);
	dhd_axi_error(&dhd->pub);
}
#endif /* DNGL_AXI_ERROR_LOGGING && DHD_USE_WQ_FOR_DNGL_AXI_ERROR */

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
#if defined(BCMSDIO) || defined(BCMPCIE)
	uint chipid = dhd_bus_chip_id(dhdp);
	int ret = BCME_OK;
	if (prepost) { /* pre process */
		ret = dhd_alloc_cis(dhdp);
		if (ret != BCME_OK) {
			return ret;
		}
		switch (chipid) {
#ifndef DHD_READ_CIS_FROM_BP
			case BCM4389_CHIP_GRPID:
				/* BCM4389B0 or higher rev is used new otp iovar */
				dhd_read_otp_sw_rgn(dhdp);
				break;
#endif /* !DHD_READ_CIS_FROM_BP */
			default:
				dhd_read_cis(dhdp);
				break;
		}
		dhd_check_module_cid(dhdp);
		dhd_check_module_mac(dhdp);
		dhd_set_macaddr_from_file(dhdp);
	} else { /* post process */
		dhd_write_macaddr(&dhdp->mac);
		dhd_clear_cis(dhdp);
	}
#endif

	return BCME_OK;
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
#ifdef APSTA_BLOCK_ARP_DURING_DHCP
			if (value && (i == DHD_BROADCAST_ARP_FILTER_NUM) &&
				dhd->pktfilter[DHD_BROADCAST_ARP_FILTER_NUM]) {
				/* XXX: BROADCAST_ARP_FILTER is only for the
				 * STA/SoftAP concurrent mode (Please refer to RB:90348)
				 * Remove the filter for other cases explicitly
				 */
				DHD_ERROR(("%s: Remove the DHD_BROADCAST_ARP_FILTER\n",
					__FUNCTION__));
				dhd_packet_filter_add_remove(dhd, FALSE,
					DHD_BROADCAST_ARP_FILTER_NUM);
			}
#endif /* APSTA_BLOCK_ARP_DURING_DHCP */
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
			} /* XXX: intend omitting else case */
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
			} /* XXX: intend omitting else case */
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
		if (dhdp->pktfilter[num] != NULL) {
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
	/* wl_pkt_filter_enable_t	enable_parm; */
	int bcn_li_dtim = 0; /* Default bcn_li_dtim in resume mode is 0 */
	int ret = 0;
#ifdef DHD_USE_EARLYSUSPEND
#ifdef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
	int roam_time_thresh = 0;   /* (ms) */
#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#ifndef ENABLE_FW_ROAM_SUSPEND
	uint roamvar = 1;
#endif /* ENABLE_FW_ROAM_SUSPEND */
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
	int bcn_li_bcn = 1;
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
	uint nd_ra_filter = 0;
#ifdef ENABLE_IPMCAST_FILTER
	int ipmcast_l2filter;
#endif /* ENABLE_IPMCAST_FILTER */
#ifdef CUSTOM_EVENT_PM_WAKE
	uint32 pm_awake_thresh = CUSTOM_EVENT_PM_WAKE;
#endif /* CUSTOM_EVENT_PM_WAKE */
#endif /* DHD_USE_EARLYSUSPEND */
#ifdef PASS_ALL_MCAST_PKTS
	struct dhd_info *dhdinfo;
	uint32 allmulti;
	uint i;
#endif /* PASS_ALL_MCAST_PKTS */
#ifdef DYNAMIC_SWOOB_DURATION
#ifndef CUSTOM_INTR_WIDTH
#define CUSTOM_INTR_WIDTH 100
	int intr_width = 0;
#endif /* CUSTOM_INTR_WIDTH */
#endif /* DYNAMIC_SWOOB_DURATION */

#if defined(DHD_BCN_TIMEOUT_IN_SUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	/* CUSTOM_BCN_TIMEOUT_IN_SUSPEND in suspend, otherwise CUSTOM_BCN_TIMEOUT */
	int bcn_timeout = CUSTOM_BCN_TIMEOUT;
#endif /* DHD_BCN_TIMEOUT_IN_SUSPEND && DHD_USE_EARLYSUSPEND */
#if defined(OEM_ANDROID) && defined(BCMPCIE)
	int lpas = 0;
	int dtim_period = 0;
	int bcn_interval = 0;
	int bcn_to_dly = 0;
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
				dhd->early_suspended = 1;
				/* Kernel suspended */
				DHD_ERROR(("%s: force extra Suspend setting \n", __FUNCTION__));

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
#ifdef ARP_OFFLOAD_SUPPORT
				if (dhd->arpoe_enable) {
					dhd_arp_offload_enable(dhd, TRUE);
				}
#endif /* ARP_OFFLOAD_SUPPORT */

#ifdef PASS_ALL_MCAST_PKTS
				allmulti = 0;
				for (i = 0; i < DHD_MAX_IFS; i++) {
					if (dhdinfo->iflist[i] && dhdinfo->iflist[i]->net) {
						ret = dhd_iovar(dhd, i, "allmulti",
								(char *)&allmulti,
								sizeof(allmulti),
								NULL, 0, TRUE);
						if (ret < 0) {
							DHD_ERROR(("%s allmulti failed %d\n",
								__FUNCTION__, ret));
						}
					}
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
#if defined(OEM_ANDROID) && defined(BCMPCIE)
				bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd, &dtim_period,
						&bcn_interval);
				ret = dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
						sizeof(bcn_li_dtim), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s bcn_li_dtim failed %d\n",
							__FUNCTION__, ret));
				}
				if ((bcn_li_dtim * dtim_period * bcn_interval) >=
					MIN_DTIM_FOR_ROAM_THRES_EXTEND) {
					/*
					 * Increase max roaming threshold from 2 secs to 8 secs
					 * the real roam threshold is MIN(max_roam_threshold,
					 * bcn_timeout/2)
					 */
					lpas = 1;
					ret = dhd_iovar(dhd, 0, "lpas", (char *)&lpas, sizeof(lpas),
							NULL, 0, TRUE);
					if (ret < 0) {
						if (ret == BCME_UNSUPPORTED) {
							DHD_ERROR(("%s lpas, UNSUPPORTED\n",
								__FUNCTION__));
						} else {
							DHD_ERROR(("%s set lpas failed %d\n",
								__FUNCTION__, ret));
						}
					}
					bcn_to_dly = 1;
					/*
					 * if bcn_to_dly is 1, the real roam threshold is
					 * MIN(max_roam_threshold, bcn_timeout -1);
					 * notify link down event after roaming procedure complete
					 * if we hit bcn_timeout while we are in roaming progress.
					 */
					ret = dhd_iovar(dhd, 0, "bcn_to_dly", (char *)&bcn_to_dly,
							sizeof(bcn_to_dly), NULL, 0, TRUE);
					if (ret < 0) {
						if (ret == BCME_UNSUPPORTED) {
							DHD_ERROR(("%s bcn_to_dly, UNSUPPORTED\n",
								__FUNCTION__));
						} else {
							DHD_ERROR(("%s set bcn_to_dly failed %d\n",
								__FUNCTION__, ret));
						}
					}
				}
#else
				bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd);
				if (dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
						sizeof(bcn_li_dtim), NULL, 0, TRUE) < 0)
					DHD_ERROR(("%s: set dtim failed\n", __FUNCTION__));
#endif /* OEM_ANDROID && BCMPCIE */

#ifdef DHD_USE_EARLYSUSPEND
#ifdef DHD_BCN_TIMEOUT_IN_SUSPEND
				bcn_timeout = CUSTOM_BCN_TIMEOUT_IN_SUSPEND;
				ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
						sizeof(bcn_timeout), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s bcn_timeout failed %d\n", __FUNCTION__,
						ret));
				}
#endif /* DHD_BCN_TIMEOUT_IN_SUSPEND */
#ifdef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
				roam_time_thresh = CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND;
				ret = dhd_iovar(dhd, 0, "roam_time_thresh",
						(char *)&roam_time_thresh,
						sizeof(roam_time_thresh), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s roam_time_thresh failed %d\n",
						__FUNCTION__, ret));
				}
#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#ifndef ENABLE_FW_ROAM_SUSPEND
				/* Disable firmware roaming during suspend */
				ret = dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar,
						sizeof(roamvar), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s roam_off failed %d\n",
						__FUNCTION__, ret));
				}
#endif /* ENABLE_FW_ROAM_SUSPEND */
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
				if (bcn_li_dtim) {
					bcn_li_bcn = 0;
				}
				ret = dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn,
						sizeof(bcn_li_bcn), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s bcn_li_bcn failed %d\n", __FUNCTION__, ret));
				}
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
#if defined(WL_CFG80211) && defined(WL_BCNRECV)
				ret = wl_android_bcnrecv_suspend(dhd_linux_get_primary_netdev(dhd));
				if (ret != BCME_OK) {
					DHD_ERROR(("failed to stop beacon recv event on"
						" suspend state (%d)\n", ret));
				}
#endif /* WL_CFG80211 && WL_BCNRECV */
#ifdef NDO_CONFIG_SUPPORT
				if (dhd->ndo_enable) {
					if (!dhd->ndo_host_ip_overflow) {
						/* enable ND offload on suspend */
						ret = dhd_ndo_enable(dhd, TRUE);
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
				if (ret < 0) {
					DHD_ERROR(("failed to set ipmcast_l2filter (%d)\n", ret));
				}
#endif /* ENABLE_IPMCAST_FILTER */
#ifdef DYNAMIC_SWOOB_DURATION
				intr_width = CUSTOM_INTR_WIDTH;
				ret = dhd_iovar(dhd, 0, "bus:intr_width", (char *)&intr_width,
						sizeof(intr_width), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("failed to set intr_width (%d)\n", ret));
				}
#endif /* DYNAMIC_SWOOB_DURATION */
#ifdef CUSTOM_EVENT_PM_WAKE
				pm_awake_thresh = CUSTOM_EVENT_PM_WAKE * 4;
				ret = dhd_iovar(dhd, 0, "const_awake_thresh",
					(char *)&pm_awake_thresh,
					sizeof(pm_awake_thresh), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s set const_awake_thresh failed %d\n",
						__FUNCTION__, ret));
				}
#endif /* CUSTOM_EVENT_PM_WAKE */
#ifdef CONFIG_SILENT_ROAM
				if (!dhd->sroamed) {
					ret = dhd_sroam_set_mon(dhd, TRUE);
					if (ret < 0) {
						DHD_ERROR(("%s set sroam failed %d\n",
							__FUNCTION__, ret));
					}
				}
				dhd->sroamed = FALSE;
#endif /* CONFIG_SILENT_ROAM */
#endif /* DHD_USE_EARLYSUSPEND */
			} else {
				dhd->early_suspended = 0;
				/* Kernel resumed  */
				DHD_ERROR(("%s: Remove extra suspend setting \n", __FUNCTION__));
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
#if defined(WL_CFG80211) && defined(WL_BCNRECV)
				ret = wl_android_bcnrecv_resume(dhd_linux_get_primary_netdev(dhd));
				if (ret != BCME_OK) {
					DHD_ERROR(("failed to resume beacon recv state (%d)\n",
							ret));
				}
#endif /* WL_CF80211 && WL_BCNRECV */
#ifdef ARP_OFFLOAD_SUPPORT
				if (dhd->arpoe_enable) {
					dhd_arp_offload_enable(dhd, FALSE);
				}
#endif /* ARP_OFFLOAD_SUPPORT */
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
						ret = dhd_iovar(dhd, i, "allmulti",
								(char *)&allmulti,
								sizeof(allmulti), NULL,
								0, TRUE);
					if (ret < 0) {
						DHD_ERROR(("%s: allmulti failed:%d\n",
								__FUNCTION__, ret));
					}
				}
#endif /* PASS_ALL_MCAST_PKTS */
#if defined(OEM_ANDROID) && defined(BCMPCIE)
				/* restore pre-suspend setting */
				ret = dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
						sizeof(bcn_li_dtim), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:bcn_li_ditm failed:%d\n",
							__FUNCTION__, ret));
				}
				ret = dhd_iovar(dhd, 0, "lpas", (char *)&lpas, sizeof(lpas), NULL,
						0, TRUE);
				if (ret < 0) {
					if (ret == BCME_UNSUPPORTED) {
						DHD_ERROR(("%s lpas, UNSUPPORTED\n", __FUNCTION__));
					 } else {
						DHD_ERROR(("%s set lpas failed %d\n",
							__FUNCTION__, ret));
					 }
				}
				ret = dhd_iovar(dhd, 0, "bcn_to_dly", (char *)&bcn_to_dly,
						sizeof(bcn_to_dly), NULL, 0, TRUE);
				if (ret < 0) {
					if (ret == BCME_UNSUPPORTED) {
						DHD_ERROR(("%s bcn_to_dly UNSUPPORTED\n",
							__FUNCTION__));
					} else {
						DHD_ERROR(("%s set bcn_to_dly failed %d\n",
							__FUNCTION__, ret));
					}
				}
#else
				/* restore pre-suspend setting for dtim_skip */
				ret = dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
						sizeof(bcn_li_dtim), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:bcn_li_ditm fail:%d\n", __FUNCTION__, ret));
				}
#endif /* OEM_ANDROID && BCMPCIE */
#ifdef DHD_USE_EARLYSUSPEND
#ifdef DHD_BCN_TIMEOUT_IN_SUSPEND
				bcn_timeout = CUSTOM_BCN_TIMEOUT;
				ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
						sizeof(bcn_timeout), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:bcn_timeout failed:%d\n",
						__FUNCTION__, ret));
				}
#endif /* DHD_BCN_TIMEOUT_IN_SUSPEND */
#ifdef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
				roam_time_thresh = 2000;
				ret = dhd_iovar(dhd, 0, "roam_time_thresh",
						(char *)&roam_time_thresh,
						sizeof(roam_time_thresh), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:roam_time_thresh failed:%d\n",
							__FUNCTION__, ret));
				}

#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#ifndef ENABLE_FW_ROAM_SUSPEND
				roamvar = dhd_roam_disable;
				ret = dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar,
						sizeof(roamvar), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s: roam_off fail:%d\n", __FUNCTION__, ret));
				}
#endif /* ENABLE_FW_ROAM_SUSPEND */
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
				ret = dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn,
						sizeof(bcn_li_bcn), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s: bcn_li_bcn failed:%d\n",
						__FUNCTION__, ret));
				}
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
#ifdef NDO_CONFIG_SUPPORT
				if (dhd->ndo_enable) {
					/* Disable ND offload on resume */
					ret = dhd_ndo_enable(dhd, FALSE);
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
				if (ret < 0) {
					DHD_ERROR(("failed to clear ipmcast_l2filter ret:%d", ret));
				}
#endif /* ENABLE_IPMCAST_FILTER */
#ifdef CUSTOM_EVENT_PM_WAKE
				ret = dhd_iovar(dhd, 0, "const_awake_thresh",
					(char *)&pm_awake_thresh,
					sizeof(pm_awake_thresh), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s set const_awake_thresh failed %d\n",
						__FUNCTION__, ret));
				}
#endif /* CUSTOM_EVENT_PM_WAKE */
#ifdef CONFIG_SILENT_ROAM
				ret = dhd_sroam_set_mon(dhd, FALSE);
				if (ret < 0) {
					DHD_ERROR(("%s set sroam failed %d\n", __FUNCTION__, ret));
				}
#endif /* CONFIG_SILENT_ROAM */
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

	/* Set flag when early suspend was called */
	dhdp->in_suspend = val;
	if ((force || !dhdp->suspend_disable_flag) &&
		(dhd_support_sta_mode(dhdp) || dhd_conf_get_insuspend(dhdp, ALL_IN_SUSPEND)))
	{
		ret = dhd_set_suspend(val, dhdp);
	}

	DHD_OS_WAKE_UNLOCK(dhdp);
	return ret;
}

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
static void dhd_early_suspend(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);
	DHD_TRACE_HW4(("%s: enter\n", __FUNCTION__));

	if (dhd && (dhd->pub.conf->suspend_mode == EARLY_SUSPEND ||
			dhd->pub.conf->suspend_mode == SUSPEND_MODE_2)) {
		dhd_suspend_resume_helper(dhd, 1, 0);
		if (dhd->pub.conf->suspend_mode == EARLY_SUSPEND)
			dhd_conf_set_suspend_resume(&dhd->pub, 1);
	}
}

static void dhd_late_resume(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);
	DHD_TRACE_HW4(("%s: enter\n", __FUNCTION__));

	if (dhd && (dhd->pub.conf->suspend_mode == EARLY_SUSPEND ||
			dhd->pub.conf->suspend_mode == SUSPEND_MODE_2)) {
		dhd_conf_set_suspend_resume(&dhd->pub, 0);
		if (dhd->pub.conf->suspend_mode == EARLY_SUSPEND)
			dhd_suspend_resume_helper(dhd, 0, 0);
	}
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
#ifdef BCMQT
	tmo->limit = usec * htclkratio;
#else
	tmo->limit = usec;
#endif
	tmo->increment = 0;
	tmo->elapsed = 0;
	tmo->tick = 10 * USEC_PER_MSEC;	/* 10 msec */
}

int
dhd_timeout_expired(dhd_timeout_t *tmo)
{
	/* Does nothing the first call */
	if (tmo->increment == 0) {
		tmo->increment = USEC_PER_MSEC;	/* Start with 1 msec */
		return 0;
	}

	if (tmo->elapsed >= tmo->limit)
		return 1;

	DHD_INFO(("%s: CAN_SLEEP():%d tmo->increment=%ld msec\n",
		__FUNCTION__, CAN_SLEEP(), tmo->increment / USEC_PER_MSEC));

	CAN_SLEEP() ? OSL_SLEEP(tmo->increment / USEC_PER_MSEC) : OSL_DELAY(tmo->increment);

	/* Till tmo->tick, the delay will be in 2x, after that delay will be constant
	 * tmo->tick (10 msec), till timer elapses.
	 */
	tmo->increment = (tmo->increment >= tmo->tick) ? tmo->tick : (tmo->increment * 2);

	/* Add the delay that's about to take place */
#ifdef BCMQT
	tmo->elapsed += tmo->increment * htclkratio;
#else
	tmo->elapsed += tmo->increment;
#endif

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

#ifdef MCAST_LIST_ACCUMULATION
	int i;
	uint32 cnt_iface[DHD_MAX_IFS];
	cnt = 0;
	allmulti = 0;

	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			dev = dhd->iflist[i]->net;
			if (!dev)
				continue;
			netif_addr_lock_bh(dev);
			cnt_iface[i] = netdev_mc_count(dev);
			cnt += cnt_iface[i];
			netif_addr_unlock_bh(dev);

			/* Determine initial value of allmulti flag */
			allmulti |= (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;
		}
	}
#else /* !MCAST_LIST_ACCUMULATION */
	if (!dhd->iflist[ifidx]) {
		DHD_ERROR(("%s : dhd->iflist[%d] was NULL\n", __FUNCTION__, ifidx));
		return;
	}
	dev = dhd->iflist[ifidx]->net;
	if (!dev)
		return;
	netif_addr_lock_bh(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	cnt = netdev_mc_count(dev);
#else
	cnt = dev->mc_count;
#endif /* LINUX_VERSION_CODE */

	netif_addr_unlock_bh(dev);

	/* Determine initial value of allmulti flag */
	allmulti = (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;
#endif /* MCAST_LIST_ACCUMULATION */

#ifdef PASS_ALL_MCAST_PKTS
#ifdef PKT_FILTER_SUPPORT
	if (!dhd->pub.early_suspended)
#endif /* PKT_FILTER_SUPPORT */
		allmulti = TRUE;
#endif /* PASS_ALL_MCAST_PKTS */

	/* Send down the multicast list first. */

	/* XXX Not using MAXMULTILIST to avoid including wlc_pub.h; but
	 * maybe we should?  (Or should that be in wlioctl.h instead?)
	 */

	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETHER_ADDR_LEN);
	if (!(bufp = buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
		           dhd_ifname(&dhd->pub, ifidx), cnt));
		return;
	}

	strlcpy(bufp, "mcast_list", buflen);
	bufp += strlen("mcast_list") + 1;

	cnt = htol32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);

#ifdef MCAST_LIST_ACCUMULATION
	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			DHD_TRACE(("_dhd_set_multicast_list: ifidx %d\n", i));
			dev = dhd->iflist[i]->net;

			netif_addr_lock_bh(dev);
			GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
			netdev_for_each_mc_addr(ha, dev) {
				GCC_DIAGNOSTIC_POP();
				if (!cnt_iface[i])
					break;
				memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
				bufp += ETHER_ADDR_LEN;
				DHD_TRACE(("_dhd_set_multicast_list: cnt "
					"%d " MACDBG "\n",
					cnt_iface[i], MAC2STRDBG(ha->addr)));
				cnt_iface[i]--;
			}
			netif_addr_unlock_bh(dev);
		}
	}
#else /* !MCAST_LIST_ACCUMULATION */
	netif_addr_lock_bh(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	netdev_for_each_mc_addr(ha, dev) {
		GCC_DIAGNOSTIC_POP();
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
		cnt--;
	}
#else
	for (mclist = dev->mc_list; (mclist && (cnt > 0));
			cnt--, mclist = mclist->next) {
		memcpy(bufp, (void *)mclist->dmi_addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
	}
#endif /* LINUX_VERSION_CODE */
	netif_addr_unlock_bh(dev);
#endif /* MCAST_LIST_ACCUMULATION */

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

#ifdef MCAST_LIST_ACCUMULATION
	allmulti = 0;
	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			dev = dhd->iflist[i]->net;
			allmulti |= (dev->flags & IFF_PROMISC) ? TRUE : FALSE;
		}
	}
#else
	allmulti = (dev->flags & IFF_PROMISC) ? TRUE : FALSE;
#endif /* MCAST_LIST_ACCUMULATION */

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
_dhd_set_mac_address(dhd_info_t *dhd, int ifidx, uint8 *addr, bool skip_stop)
{
	int ret;

#ifdef DHD_NOTIFY_MAC_CHANGED
	if (skip_stop) {
		WL_MSG(dhd_ifname(&dhd->pub, ifidx), "close dev for mac changing\n");
		dhd->pub.skip_dhd_stop = TRUE;
		dev_close(dhd->iflist[ifidx]->net);
	}
#endif /* DHD_NOTIFY_MAC_CHANGED */

	ret = dhd_iovar(&dhd->pub, ifidx, "cur_etheraddr", (char *)addr,
			ETHER_ADDR_LEN, NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: set cur_etheraddr %pM failed ret=%d\n",
			dhd_ifname(&dhd->pub, ifidx), addr, ret));
		goto exit;
	} else {
		dev_addr_set(dhd->iflist[ifidx]->net, addr);
		if (ifidx == 0)
			memcpy(dhd->pub.mac.octet, addr, ETHER_ADDR_LEN);
		WL_MSG(dhd_ifname(&dhd->pub, ifidx), "MACID %pM is overwritten\n", addr);
	}

exit:
#ifdef DHD_NOTIFY_MAC_CHANGED
	if (skip_stop) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
		dev_open(dhd->iflist[ifidx]->net, NULL);
#else
		dev_open(dhd->iflist[ifidx]->net);
#endif
		dhd->pub.skip_dhd_stop = FALSE;
		WL_MSG(dhd_ifname(&dhd->pub, ifidx), "notify mac changed done\n");
	}
#endif /* DHD_NOTIFY_MAC_CHANGED */

	return ret;
}

int dhd_update_rand_mac_addr(dhd_pub_t *dhd)
{
	struct ether_addr mac_addr;
	dhd_generate_rand_mac_addr(&mac_addr);
	if (_dhd_set_mac_address(dhd->info, 0, mac_addr.octet, TRUE) != 0) {
		DHD_ERROR(("randmac setting failed\n"));
#ifdef STA_RANDMAC_ENFORCED
		return  BCME_BADADDR;
#endif /* STA_RANDMAC_ENFORCED */
	}
	return BCME_OK;
}

#ifdef BCM_ROUTER_DHD
void dhd_update_dpsta_interface_for_sta(dhd_pub_t* dhdp, int ifidx, void* event_data)
{
	struct wl_dpsta_intf_event *dpsta_prim_event = (struct wl_dpsta_intf_event *)event_data;
	dhd_if_t *ifp = dhdp->info->iflist[ifidx];

	if (dpsta_prim_event->intf_type == WL_INTF_DWDS) {
		ifp->primsta_dwds = TRUE;
	} else {
		ifp->primsta_dwds = FALSE;
	}
}
#endif /* BCM_ROUTER_DHD */

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
	int ifidx, bssidx;
	int ret = 0;
#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	struct wl_if_event_info info;
#if defined(WLDWDS) && defined(FOURADDR_AUTO_BRG)
	struct net_device *ndev = NULL;
#endif /* WLDWDS && FOURADDR_AUTO_BRG */
#else
	struct net_device *ndev;
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
#ifdef DHD_AWDL
	bool is_awdl_iface = FALSE;
#endif /* DHD_AWDL */

	BCM_REFERENCE(ret);
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

	ifidx = if_event->event.ifidx;
	bssidx = if_event->event.bssidx;
	DHD_TRACE(("%s: registering if with ifidx %d\n", __FUNCTION__, ifidx));

#ifdef DHD_AWDL
	if (if_event->event.opcode == WLC_E_IF_ADD &&
		if_event->event.role == WLC_E_IF_ROLE_AWDL) {
		dhd->pub.awdl_ifidx = ifidx;
		is_awdl_iface = TRUE;
	}
#endif /* DHD_AWDL */

#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (if_event->event.ifidx > 0) {
		u8 *mac_addr;
		bzero(&info, sizeof(info));
		info.ifidx = ifidx;
		info.bssidx = bssidx;
		info.role = if_event->event.role;
		strlcpy(info.name, if_event->name, sizeof(info.name));
		if (is_valid_ether_addr(if_event->mac)) {
			mac_addr = if_event->mac;
		} else {
			mac_addr = NULL;
		}

#if defined(WLDWDS) && defined(FOURADDR_AUTO_BRG)
		if ((ndev = wl_cfg80211_post_ifcreate(dhd->pub.info->iflist[0]->net,
			&info, mac_addr, if_event->name, true)) == NULL)
#else
		if (wl_cfg80211_post_ifcreate(dhd->pub.info->iflist[0]->net,
			&info, mac_addr, NULL, true) == NULL)
#endif /* WLDWDS && FOURADDR_AUTO_BRG */
		{
			/* Do the post interface create ops */
			DHD_ERROR(("Post ifcreate ops failed. Returning \n"));
			ret = BCME_ERROR;
			goto done;
		}
	}
#else
	/* This path is for non-android case */
	/* The interface name in host and in event msg are same */
	/* if name in event msg is used to create dongle if list on host */
	ndev = dhd_allocate_if(&dhd->pub, ifidx, if_event->name,
		if_event->mac, bssidx, TRUE, if_event->name);
	if (!ndev) {
		DHD_ERROR(("%s: net device alloc failed  \n", __FUNCTION__));
		ret = BCME_NOMEM;
		goto done;
	}

	ret = dhd_register_if(&dhd->pub, ifidx, TRUE);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: dhd_register_if failed\n", __FUNCTION__));
		dhd_remove_if(&dhd->pub, ifidx, TRUE);
		goto done;
	}

#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

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

#if defined(WLDWDS) && defined(FOURADDR_AUTO_BRG)
	dhd_bridge_dev_set(dhd, ifidx, ndev);
#endif /* WLDWDS && FOURADDR_AUTO_BRG */

done:
#ifdef DHD_AWDL
	if (ret != BCME_OK && is_awdl_iface) {
		dhd->pub.awdl_ifidx = 0;
	}
#endif /* DHD_AWDL */

	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));

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

	ifidx = if_event->event.ifidx;
	DHD_TRACE(("Removing interface with idx %d\n", ifidx));

	if (!dhd->pub.info->iflist[ifidx]) {
		/* No matching netdev found */
		DHD_ERROR(("Netdev not found! Do nothing.\n"));
		goto done;
	}
#if defined(WLDWDS) && defined(FOURADDR_AUTO_BRG)
	dhd_bridge_dev_set(dhd, ifidx, NULL);
#endif /* WLDWDS && FOURADDR_AUTO_BRG */
#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (if_event->event.ifidx > 0) {
		/* Do the post interface del ops */
		if (wl_cfg80211_post_ifdel(dhd->pub.info->iflist[ifidx]->net,
				true, if_event->event.ifidx) != 0) {
			DHD_TRACE(("Post ifdel ops failed. Returning \n"));
			goto done;
		}
	}
#else
	/* For non-cfg80211 drivers */
	dhd_remove_if(&dhd->pub, ifidx, TRUE);
#ifdef DHD_AWDL
	if (if_event->event.opcode == WLC_E_IF_DEL &&
		if_event->event.role == WLC_E_IF_ROLE_AWDL) {
		dhd->pub.awdl_ifidx = 0;
	}
#endif /* DHD_AWDL */

#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

done:
	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));
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
			dev_addr_set(dhdinfo->iflist[ifp->idx]->net, buf);
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

	// terence 20160907: fix for not able to set mac when wlan0 is down
	if (ifp == NULL || !ifp->set_macaddress) {
		goto done;
	}
	if (ifp == NULL || !dhd->pub.up) {
		DHD_ERROR(("%s: interface info not available/down \n", __FUNCTION__));
		goto done;
	}

	ifp->set_macaddress = FALSE;

#ifdef DHD_NOTIFY_MAC_CHANGED
	rtnl_lock();
#endif /* DHD_NOTIFY_MAC_CHANGED */

	if (_dhd_set_mac_address(dhd, ifp->idx, ifp->mac_addr, TRUE) == 0)
		DHD_INFO(("%s: MACID is overwritten\n",	__FUNCTION__));
	else
		DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));

#ifdef DHD_NOTIFY_MAC_CHANGED
	rtnl_unlock();
#endif /* DHD_NOTIFY_MAC_CHANGED */

done:
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

#ifdef MCAST_LIST_ACCUMULATION
	ifidx = 0;
#endif /* MCAST_LIST_ACCUMULATION */

	_dhd_set_multicast_list(dhd, ifidx);
	DHD_INFO(("%s: set multicast list for if %d\n", __FUNCTION__, ifidx));

done:
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
#ifdef WL_STATIC_IF
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif /* WL_STATIC_IF */
	dhd_pub_t *dhdp = &dhd->pub;

	BCM_REFERENCE(ifidx);

	DHD_TRACE(("%s \n", __func__));

	dhdif = dhd_get_ifp_by_ndev(dhdp, dev);
	if (!dhdif) {
		return -ENODEV;
	}
	ifidx = dhdif->idx;
	dhd_net_if_lock_local(dhd);
	memcpy(dhdif->mac_addr, sa->sa_data, ETHER_ADDR_LEN);
	dhdif->set_macaddress = TRUE;
	dhd_net_if_unlock_local(dhd);

	WL_MSG(dev->name, "macaddr = %pM\n", dhdif->mac_addr);
#ifdef WL_CFG80211
	/* Check wdev->iftype for the role */
	if (wl_cfg80211_macaddr_sync_reqd(dev)) {
		/* Supplicant and certain user layer applications expect macaddress to be
		 * set once the context returns. so set it from the same context
		 */
#ifdef WL_STATIC_IF
		if (wl_cfg80211_static_if(cfg, dev) && !(dev->flags & IFF_UP)) {
			/* In softap case, the macaddress will be applied before interface up
			 * and hence curether_addr can't be done at this stage (no fw iface
			 * available). Store the address and return. macaddr will be applied
			 * from interface create context.
			 */
			dev_addr_set(dev, dhdif->mac_addr);
#ifdef DHD_NOTIFY_MAC_CHANGED
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
			dev_open(dev, NULL);
#else
			dev_open(dev);
#endif
#endif /* DHD_NOTIFY_MAC_CHANGED */
			return ret;
		}
#endif /* WL_STATIC_IF */
		wl_cfg80211_handle_macaddr_change(dev, dhdif->mac_addr);
		return _dhd_set_mac_address(dhd, ifidx, dhdif->mac_addr, TRUE);
	}
#endif /* WL_CFG80211 */

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

#if defined(WL_MONITOR) && defined(BCMSDIO)
static void
dhd_rx_mon_pkt_sdio(dhd_pub_t *dhdp, void *pkt, int ifidx);
bool
dhd_monitor_enabled(dhd_pub_t *dhd, int ifidx);
#endif /* WL_MONITOR && BCMSDIO */

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
#if defined(WL_MONITOR) && defined(BCMSDIO)
			if (dhd_monitor_enabled(dhdp, ifidx))
				dhd_rx_mon_pkt_sdio(dhdp, skb, ifidx);
			else
#endif /* WL_MONITOR && BCMSDIO */
			netif_rx_ni(skb);
		}
	}

	if (dhdp->info->rxthread_enabled && skbhead)
		dhd_sched_rxf(dhdp, skbhead);

	return BCME_OK;
}

void
dhd_handle_pktdata(dhd_pub_t *dhdp, int ifidx, void *pkt, uint8 *pktdata, uint32 pktid,
	uint32 pktlen, uint16 *pktfate, uint8 *dhd_udr, bool tx, int pkt_wake, bool pkt_log)
{
	struct ether_header *eh;
	uint16 ether_type;
	uint32 pkthash;
	uint8 pkt_type = PKT_TYPE_DATA;

	if (!pktdata || pktlen < ETHER_HDR_LEN) {
		return;
	}

	eh = (struct ether_header *)pktdata;
	ether_type = ntoh16(eh->ether_type);

	/* Check packet type */
	if (dhd_check_ip_prot(pktdata, ether_type)) {
		if (dhd_check_dhcp(pktdata)) {
			pkt_type = PKT_TYPE_DHCP;
		} else if (dhd_check_icmp(pktdata)) {
			pkt_type = PKT_TYPE_ICMP;
		} else if (dhd_check_dns(pktdata)) {
			pkt_type = PKT_TYPE_DNS;
		}
	}
	else if (dhd_check_arp(pktdata, ether_type)) {
		pkt_type = PKT_TYPE_ARP;
	}
	else if (ether_type == ETHER_TYPE_802_1X) {
		pkt_type = PKT_TYPE_EAP;
	}

#ifdef DHD_SBN
	/* Set UDR based on packet type */
	if (dhd_udr && (pkt_type == PKT_TYPE_DHCP ||
		pkt_type == PKT_TYPE_DNS ||
		pkt_type == PKT_TYPE_ARP)) {
		*dhd_udr = TRUE;
	}
#endif /* DHD_SBN */

#ifdef DHD_PKT_LOGGING
#ifdef DHD_SKIP_PKTLOGGING_FOR_DATA_PKTS
	if (pkt_type != PKT_TYPE_DATA)
#endif
	{
		if (pkt_log) {
			if (tx) {
				if (pktfate) {
					/* Tx status */
					DHD_PKTLOG_TXS(dhdp, pkt, pktdata, pktid, *pktfate);
				} else {
					/* Tx packet */
					DHD_PKTLOG_TX(dhdp, pkt, pktdata, pktid);
				}
				pkthash = __dhd_dbg_pkt_hash((uintptr_t)pkt, pktid);
			} else {
				struct sk_buff *skb = (struct sk_buff *)pkt;
				if (pkt_wake) {
					DHD_PKTLOG_WAKERX(dhdp, skb, pktdata);
				} else {
					DHD_PKTLOG_RX(dhdp, skb, pktdata);
				}
			}
		}
	}
#endif /* DHD_PKT_LOGGING */

	/* Dump packet data */
	if (!tx) {
	switch (pkt_type) {
		case PKT_TYPE_DHCP:
			dhd_dhcp_dump(dhdp, ifidx, pktdata, tx, &pkthash, pktfate);
			break;
		case PKT_TYPE_ICMP:
			dhd_icmp_dump(dhdp, ifidx, pktdata, tx, &pkthash, pktfate);
			break;
		case PKT_TYPE_DNS:
			dhd_dns_dump(dhdp, ifidx, pktdata, tx, &pkthash, pktfate);
			break;
		case PKT_TYPE_ARP:
			dhd_arp_dump(dhdp, ifidx, pktdata, tx, &pkthash, pktfate);
			break;
		case PKT_TYPE_EAP:
			dhd_dump_eapol_message(dhdp, ifidx, pktdata, pktlen, tx, &pkthash, pktfate);
			break;
		default:
			break;
	}
	}
}

int
BCMFASTPATH(__dhd_sendpkt)(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret = BCME_OK;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh = NULL;
	uint8 pkt_flow_prio;

#if (defined(DHD_L2_FILTER) || (defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET)))
	dhd_if_t *ifp = dhd_get_ifp(dhdp, ifidx);
#endif /* DHD_L2_FILTER || (BCM_ROUTER_DHD && QOS_MAP_SET) */

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
		return NETDEV_TX_BUSY;
	}
#endif /* PCIE_FULL_DONGLE */

	/* Reject if pktlen > MAX_MTU_SZ */
	if (PKTLEN(dhdp->osh, pktbuf) > MAX_MTU_SZ) {
		/* free the packet here since the caller won't */
		dhdp->tx_big_packets++;
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return BCME_ERROR;
	}

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
#if defined(WL_CFG80211) && defined (WL_WPS_SYNC)
			wl_handle_wps_states(dhd_idx2net(dhdp, ifidx),
				pktdata, PKTLEN(dhdp->osh, pktbuf), TRUE);
#endif /* WL_CFG80211 && WL_WPS_SYNC */
#ifdef EAPOL_RESEND
			wl_ext_backup_eapol_txpkt(dhdp, ifidx, pktbuf);
#endif /* EAPOL_RESEND */
		}
		dhd_dump_pkt(dhdp, ifidx, pktdata,
			(uint32)PKTLEN(dhdp->osh, pktbuf), TRUE, NULL, NULL);
	} else {
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return BCME_ERROR;
	}

#if (defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET))
	if (ifp->qosmap_up_table_enable) {
		pktsetprio_qms(pktbuf, ifp->qosmap_up_table, FALSE);
	}
	else
#endif
	{
		/* Look into the packet and update the packet priority */
#ifndef PKTPRIO_OVERRIDE
		/* XXX RB:6270 Ignore skb->priority from TCP/IP stack */
		if (PKTPRIO(pktbuf) == 0)
#endif /* !PKTPRIO_OVERRIDE */
		{
#if (!defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET))
			pktsetprio_qms(pktbuf, wl_get_up_table(dhdp, ifidx), FALSE);
#else
			/* For LLR, pkt prio will be changed to 7(NC) here */
			pktsetprio(pktbuf, FALSE);
#endif /* QOS_MAP_SET */
		}
#ifndef PKTPRIO_OVERRIDE
		else {
			/* Some protocols like OZMO use priority values from 256..263.
			 * these are magic values to indicate a specific 802.1d priority.
			 * make sure that priority field is in range of 0..7
			 */
			PKTSETPRIO(pktbuf, PKTPRIO(pktbuf) & 0x7);
		}
#endif /* !PKTPRIO_OVERRIDE */
	}

#if defined(BCM_ROUTER_DHD)
	traffic_mgmt_pkt_set_prio(dhdp, pktbuf);

#endif /* BCM_ROUTER_DHD */

	BCM_REFERENCE(pkt_flow_prio);
	/* Intercept and create Socket level statistics */
	/*
	 * TODO: Some how moving this code block above the pktsetprio code
	 * is resetting the priority back to 0, but this does not happen for
	 * packets generated from iperf uisng -S option. Can't understand why.
	 */
	dhd_update_sock_flows(dhd, pktbuf);

#ifdef SUPPORT_SET_TID
	dhd_set_tid_based_on_uid(dhdp, pktbuf);
#endif	/* SUPPORT_SET_TID */

#ifdef PCIE_FULL_DONGLE
	/*
	 * Lkup the per interface hash table, for a matching flowring. If one is not
	 * available, allocate a unique flowid and add a flowring entry.
	 * The found or newly created flowid is placed into the pktbuf's tag.
	 */

#ifdef DHD_TX_PROFILE
	if (dhdp->tx_profile_enab && dhdp->num_profiles > 0 &&
		dhd_protocol_matches_profile(PKTDATA(dhdp->osh, pktbuf),
		PKTLEN(dhdp->osh, pktbuf), dhdp->protocol_filters,
		dhdp->host_sfhllc_supported)) {
		/* we only have support for one tx_profile at the moment */

		/* tagged packets must be put into TID 6 */
		pkt_flow_prio = PRIO_8021D_VO;
	} else
#endif /* defined(DHD_TX_PROFILE) */
	{
		pkt_flow_prio = dhdp->flow_prio_map[(PKTPRIO(pktbuf))];
	}

	ret = dhd_flowid_update(dhdp, ifidx, pkt_flow_prio, pktbuf);
	if (ret != BCME_OK) {
		PKTCFREE(dhd->pub.osh, pktbuf, TRUE);
		return ret;
	}
#endif /* PCIE_FULL_DONGLE */
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

int
BCMFASTPATH(dhd_sendpkt)(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret = 0;
	unsigned long flags;
	dhd_if_t *ifp;

	DHD_GENERAL_LOCK(dhdp, flags);
	ifp = dhd_get_ifp(dhdp, ifidx);
	if (!ifp || ifp->del_in_progress) {
		DHD_ERROR(("%s: ifp:%p del_in_progress:%d\n",
			__FUNCTION__, ifp, ifp ? ifp->del_in_progress : 0));
		DHD_GENERAL_UNLOCK(dhdp, flags);
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_ERROR(("%s: returning as busstate=%d\n",
			__FUNCTION__, dhdp->busstate));
		DHD_GENERAL_UNLOCK(dhdp, flags);
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}
	DHD_IF_SET_TX_ACTIVE(ifp, DHD_TX_SEND_PKT);
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
		DHD_IF_CLR_TX_ACTIVE(ifp, DHD_TX_SEND_PKT);
		dhd_os_tx_completion_wake(dhdp);
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
	DHD_IF_CLR_TX_ACTIVE(ifp, DHD_TX_SEND_PKT);
	dhd_os_tx_completion_wake(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
	return ret;
}

#ifdef DHD_MQ
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
static uint16
BCMFASTPATH(dhd_select_queue)(struct net_device *net, struct sk_buff *skb,
       void *accel_priv, select_queue_fallback_t fallback)
#else
static uint16
BCMFASTPATH(dhd_select_queue)(struct net_device *net, struct sk_buff *skb)
#endif /* LINUX_VERSION_CODE */
{
	dhd_info_t *dhd_info = DHD_DEV_INFO(net);
	dhd_pub_t *dhdp = &dhd_info->pub;
	uint16 prio = 0;

	BCM_REFERENCE(dhd_info);
	BCM_REFERENCE(dhdp);
	BCM_REFERENCE(prio);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	if (mq_select_disable) {
		/* if driver side queue selection is disabled via sysfs, call the kernel
		* supplied fallback function to select the queue, which is usually
		* '__netdev_pick_tx()' in net/core/dev.c
		*/
		return fallback(net, skb);
	}
#endif /* LINUX_VERSION */

	prio = dhdp->flow_prio_map[skb->priority];
	if (prio < AC_COUNT)
		return prio;
	else
		return AC_BK;
}
#endif /* DHD_MQ */

netdev_tx_t
BCMFASTPATH(dhd_start_xmit)(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	uint datalen;
	void *pktbuf;
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp = NULL;
	int ifidx;
	unsigned long flags;
#if !defined(BCM_ROUTER_DHD)
	uint8 htsfdlystat_sz = 0;
#endif /* ! BCM_ROUTER_DHD */
#ifdef DHD_WMF
	struct ether_header *eh;
	uint8 *iph;
#endif /* DHD_WMF */
#if defined(DHD_MQ) && defined(DHD_MQ_STATS)
	int qidx = 0;
	int cpuid = 0;
	int prio = 0;
#endif /* DHD_MQ && DHD_MQ_STATS */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#if defined(DHD_MQ) && defined(DHD_MQ_STATS)
	qidx = skb_get_queue_mapping(skb);
	/* if in a non pre-emptable context, smp_processor_id can be used
	* else get_cpu and put_cpu should be used
	*/
	if (!CAN_SLEEP()) {
		cpuid = smp_processor_id();
	}
	else {
		cpuid = get_cpu();
		put_cpu();
	}
	prio = dhd->pub.flow_prio_map[skb->priority];
	DHD_TRACE(("%s: Q idx = %d, CPU = %d, prio = %d \n", __FUNCTION__,
		qidx, cpuid, prio));
	dhd->pktcnt_qac_histo[qidx][prio]++;
	dhd->pktcnt_per_ac[prio]++;
	dhd->cpu_qstats[qidx][cpuid]++;
#endif /* DHD_MQ && DHD_MQ_STATS */

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
		return NETDEV_TX_BUSY;
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
		return NETDEV_TX_BUSY;
	}
#else
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(&dhd->pub)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state!!\n",
			__FUNCTION__, dhd->pub.busstate, dhd->pub.dhd_bus_busy_state));
	}
#endif

	DHD_OS_WAKE_LOCK(&dhd->pub);

#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhd->pub.req_hang_type == HANG_REASON_BUS_DOWN) {
		DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
		dhd->pub.busstate = DHD_BUS_DOWN;
	}
#endif /* DHD_HANG_SEND_UP_TEST */

	/* Reject if down */
	/* XXX kernel panic issue when first bootup time,
	 *	 rmmod without interface down make unnecessary hang event.
	 */
	if (dhd->pub.hang_was_sent || DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(&dhd->pub)) {
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d \n",
			__FUNCTION__, dhd->pub.up, dhd->pub.busstate));
		dhd_tx_stop_queues(net);
#if defined(OEM_ANDROID)
		/* Send Event when bus down detected during data session */
		if (dhd->pub.up && !dhd->pub.hang_was_sent && !DHD_BUS_CHECK_REMOVE(&dhd->pub)) {
			DHD_ERROR(("%s: Event HANG sent up\n", __FUNCTION__));
			dhd->pub.hang_reason = HANG_REASON_BUS_DOWN;
			net_os_send_hang_message(net);
		}
#endif /* OEM_ANDROID */
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return NETDEV_TX_BUSY;
	}

	ifp = DHD_DEV_IFP(net);
	ifidx = DHD_DEV_IFIDX(net);
#ifdef DHD_BUZZZ_LOG_ENABLED
	BUZZZ_LOG(START_XMIT_BGN, 2, (uint32)ifidx, (uintptr)skb);
#endif /* DHD_BUZZZ_LOG_ENABLED */
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx %d\n", __FUNCTION__, ifidx));
		dhd_tx_stop_queues(net);
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return NETDEV_TX_BUSY;
	}

	DHD_GENERAL_UNLOCK(&dhd->pub, flags);

	/* If tput test is in progress */
	if (dhd->pub.tput_data.tput_test_running) {
		return NETDEV_TX_BUSY;
	}

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

#ifdef TPUT_MONITOR
	if (dhd->pub.conf->tput_monitor_ms) {
		dhd_os_sdlock_txq(&dhd->pub);
		dhd->pub.conf->net_len += datalen;
		dhd_os_sdunlock_txq(&dhd->pub);
		if ((dhd->pub.conf->data_drop_mode == XMIT_DROP) &&
				(PKTLEN(dhd->pub.osh, skb) > 500)) {
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}
	}
#endif
	/* Make sure there's enough room for any header */
#if !defined(BCM_ROUTER_DHD)
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
#endif /* !BCM_ROUTER_DHD */

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

			GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
			/* Convert upnp/igmp query to unicast for each assoc STA */
			list_for_each_entry(sta, wmf_ucforward_list, list) {
				GCC_DIAGNOSTIC_POP();
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
	if (PSR_ENABLED(&dhd->pub) &&
#ifdef BCM_ROUTER_DHD
		!(ifp->primsta_dwds) &&
#endif /* BCM_ROUTER_DHD */
		(dhd_psta_proc(&dhd->pub, ifidx, &pktbuf, TRUE) < 0)) {

			DHD_ERROR(("%s:%s: psta send proc failed\n", __FUNCTION__,
				dhd_ifname(&dhd->pub, ifidx)));
	}
#endif /* DHD_PSTA */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) && defined(DHD_TCP_PACING_SHIFT)
#ifndef DHD_DEFAULT_TCP_PACING_SHIFT
#define DHD_DEFAULT_TCP_PACING_SHIFT 7
#endif /* DHD_DEFAULT_TCP_PACING_SHIFT */
	if (skb->sk) {
		sk_pacing_shift_update(skb->sk, DHD_DEFAULT_TCP_PACING_SHIFT);
	}
#endif /* LINUX_VERSION_CODE >= 4.19.0 && DHD_TCP_PACING_SHIFT */

#ifdef DHDTCPSYNC_FLOOD_BLK
	if (dhd_tcpdata_get_flag(&dhd->pub, pktbuf) == FLAG_SYNCACK) {
		ifp->tsyncack_txed ++;
	}
#endif /* DHDTCPSYNC_FLOOD_BLK */

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
	/* XXX Bus modules may have different "native" error spaces? */
	/* XXX USB is native linux and it'd be nice to retain errno  */
	/* XXX meaning, but SDIO is not so we'd need an OSL_ERROR.   */
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
		dhd->pub.actual_tx_pkts++;
	}

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
	DHD_IF_CLR_TX_ACTIVE(ifp, DHD_TX_START_XMIT);
	dhd_os_tx_completion_wake(&dhd->pub);
	dhd_os_busbusy_wake(&dhd->pub);
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
#ifdef DHD_BUZZZ_LOG_ENABLED
	BUZZZ_LOG(START_XMIT_END, 0);
#endif /* DHD_BUZZZ_LOG_ENABLED */
	/* Return ok: we always eat the packet */
	return NETDEV_TX_OK;
}

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
void dhd_rx_wq_wakeup(struct work_struct *ptr)
{
	struct dhd_rx_tx_work *work;
	struct dhd_pub * pub;

	work = container_of(ptr, struct dhd_rx_tx_work, work);

	pub = work->pub;

	DHD_RPM(("%s: ENTER. \n", __FUNCTION__));

	if (atomic_read(&pub->block_bus) || pub->busstate == DHD_BUS_DOWN) {
		return;
	}

	DHD_OS_WAKE_LOCK(pub);
	if (pm_runtime_get_sync(dhd_bus_to_dev(pub->bus)) >= 0) {

		// do nothing but wakeup the bus.
		pm_runtime_mark_last_busy(dhd_bus_to_dev(pub->bus));
		pm_runtime_put_autosuspend(dhd_bus_to_dev(pub->bus));
	}
	DHD_OS_WAKE_UNLOCK(pub);
	kfree(work);
}

void dhd_start_xmit_wq_adapter(struct work_struct *ptr)
{
	struct dhd_rx_tx_work *work;
	netdev_tx_t ret;
	dhd_info_t *dhd;
	struct dhd_bus * bus;

	work = container_of(ptr, struct dhd_rx_tx_work, work);

	dhd = DHD_DEV_INFO(work->net);

	bus = dhd->pub.bus;

	if (atomic_read(&dhd->pub.block_bus)) {
		kfree_skb(work->skb);
		kfree(work);
		dhd_netif_start_queue(bus);
		return;
	}

	if (pm_runtime_get_sync(dhd_bus_to_dev(bus)) >= 0) {
		ret = dhd_start_xmit(work->skb, work->net);
		pm_runtime_mark_last_busy(dhd_bus_to_dev(bus));
		pm_runtime_put_autosuspend(dhd_bus_to_dev(bus));
	}
	kfree(work);
	dhd_netif_start_queue(bus);

	if (ret)
		netdev_err(work->net,
			   "error: dhd_start_xmit():%d\n", ret);
}

netdev_tx_t
BCMFASTPATH(dhd_start_xmit_wrapper)(struct sk_buff *skb, struct net_device *net)
{
	struct dhd_rx_tx_work *start_xmit_work;
	netdev_tx_t ret;
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	if (dhd->pub.busstate == DHD_BUS_SUSPEND) {
		DHD_RPM(("%s: wakeup the bus using workqueue.\n", __FUNCTION__));

		dhd_netif_stop_queue(dhd->pub.bus);

		start_xmit_work = (struct dhd_rx_tx_work*)
			kmalloc(sizeof(*start_xmit_work), GFP_ATOMIC);

		if (!start_xmit_work) {
			netdev_err(net,
				   "error: failed to alloc start_xmit_work\n");
			ret = -ENOMEM;
			goto exit;
		}

		INIT_WORK(&start_xmit_work->work, dhd_start_xmit_wq_adapter);
		start_xmit_work->skb = skb;
		start_xmit_work->net = net;
		queue_work(dhd->tx_wq, &start_xmit_work->work);
		ret = NET_XMIT_SUCCESS;

	} else if (dhd->pub.busstate == DHD_BUS_DATA) {
		ret = dhd_start_xmit(skb, net);
	} else {
		/* when bus is down */
		ret = -ENODEV;
	}

exit:
	return ret;
}
void
dhd_bus_wakeup_work(dhd_pub_t *dhdp)
{
	struct dhd_rx_tx_work *rx_work;
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	rx_work = kmalloc(sizeof(*rx_work), GFP_ATOMIC);
	if (!rx_work) {
		DHD_ERROR(("%s: start_rx_work alloc error. \n", __FUNCTION__));
		return;
	}

	INIT_WORK(&rx_work->work, dhd_rx_wq_wakeup);
	rx_work->pub = dhdp;
	queue_work(dhd->rx_wq, &rx_work->work);

}
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

static void
__dhd_txflowcontrol(dhd_pub_t *dhdp, struct net_device *net, bool state)
{
	if (state == ON) {
		if (!netif_queue_stopped(net)) {
			DHD_INFO(("%s: Stop Netif Queue\n", __FUNCTION__));
			netif_stop_queue(net);
		} else {
			DHD_INFO(("%s: Netif Queue already stopped\n", __FUNCTION__));
		}
	}

	if (state == OFF) {
		if (netif_queue_stopped(net)) {
			DHD_INFO(("%s: Start Netif Queue\n", __FUNCTION__));
			netif_wake_queue(net);
		} else {
			DHD_INFO(("%s: Netif Queue already started\n", __FUNCTION__));
		}
	}
}

void
dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool state)
{
	struct net_device *net;
	dhd_info_t *dhd = dhdp->info;
	unsigned long flags;
	int i;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(dhd);

#ifdef DHD_LOSSLESS_ROAMING
	/* block flowcontrol during roaming */
	if ((dhdp->dequeue_prec_map == (1 << dhdp->flow_prio_map[PRIO_8021D_NC])) && (state == ON))
	{
		DHD_ERROR_RLMT(("%s: Roaming in progress, cannot stop network queue (0x%x:%d)\n",
			__FUNCTION__, dhdp->dequeue_prec_map, dhdp->flow_prio_map[PRIO_8021D_NC]));
		return;
	}
#endif

	flags = dhd_os_sdlock_txoff(&dhd->pub);
	if (ifidx == ALL_INTERFACES) {
		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				net = dhd->iflist[i]->net;
				__dhd_txflowcontrol(dhdp, net, state);
			}
		}
	} else {
		if (dhd->iflist[ifidx]) {
			net = dhd->iflist[ifidx]->net;
			__dhd_txflowcontrol(dhdp, net, state);
		}
	}
	dhdp->txoff = state;
	dhd_os_sdunlock_txoff(&dhd->pub, flags);
}

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))

/* Dump CTF stats */
void
dhd_ctf_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	dhd_info_t *dhd = dhdp->info;

	bcm_bprintf(strbuf, "CTF stats:\n");
	ctf_dump(dhd->cih, strbuf);
}

bool
BCMFASTPATH(dhd_rx_pkt_chainable)(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp = dhd->iflist[ifidx];

	return ifp->rx_pkt_chainable;
}

/* Returns FALSE if block ping is enabled */
bool
BCMFASTPATH(dhd_l2_filter_chainable)(dhd_pub_t *dhdp, uint8 *eh, int ifidx)
{
#ifdef DHD_L2_FILTER
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp = dhd->iflist[ifidx];
	ASSERT(ifp != NULL);
	return ifp->block_ping ? FALSE : TRUE;
#else
	return TRUE;
#endif /* DHD_L2_FILTER */
}
/* Returns FALSE if WET is enabled */
bool
BCMFASTPATH(dhd_wet_chainable)(dhd_pub_t *dhdp)
{
#ifdef DHD_WET
	return (!WET_ENABLED(dhdp));
#else
	return TRUE;
#endif
}

/* Returns TRUE if hot bridge entry for this da is present */
bool
BCMFASTPATH(dhd_ctf_hotbrc_check)(dhd_pub_t *dhdp, uint8 *eh, int ifidx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp = dhd->iflist[ifidx];

	ASSERT(ifp != NULL);

	if (!dhd->brc_hot)
		return FALSE;

	return CTF_HOTBRC_CMP(dhd->brc_hot, (eh), (void *)(ifp->net));
}

/*
 * Try to forward the complete packet chain through CTF.
 * If unsuccessful,
 *   - link the chain by skb->next
 *   - change the pnext to the 2nd packet of the chain
 *   - the chained packets will be sent up to the n/w stack
 */
static inline int32
BCMFASTPATH(dhd_ctf_forward)(dhd_info_t *dhd, struct sk_buff *skb, void **pnext)
{
	dhd_pub_t *dhdp = &dhd->pub;
	void *p, *n;
	void *old_pnext;

	/* try cut thru first */
	if (!CTF_ENAB(dhd->cih) || (ctf_forward(dhd->cih, skb, skb->dev) == BCME_ERROR)) {
		/* Fall back to slow path if ctf is disabled or if ctf_forward fails */

		/* clear skipct flag before sending up */
		PKTCLRSKIPCT(dhdp->osh, skb);

#ifdef CTFPOOL
		/* allocate and add a new skb to the pkt pool */
		if (PKTISFAST(dhdp->osh, skb))
			osl_ctfpool_add(dhdp->osh);

		/* clear fast buf flag before sending up */
		PKTCLRFAST(dhdp->osh, skb);

		/* re-init the hijacked field */
		CTFPOOLPTR(dhdp->osh, skb) = NULL;
#endif /* CTFPOOL */

		/* link the chained packets by skb->next */
		if (PKTISCHAINED(skb)) {
			old_pnext = *pnext;
			PKTFRMNATIVE(dhdp->osh, skb);
			p = (void *)skb;
			FOREACH_CHAINED_PKT(p, n) {
				PKTCLRCHAINED(dhdp->osh, p);
				PKTCCLRFLAGS(p);
				if (p == (void *)skb)
					PKTTONATIVE(dhdp->osh, p);
				if (n)
					PKTSETNEXT(dhdp->osh, p, n);
				else
					PKTSETNEXT(dhdp->osh, p, old_pnext);
			}
			*pnext = PKTNEXT(dhdp->osh, skb);
			PKTSETNEXT(dhdp->osh, skb, NULL);
		}
		return (BCME_ERROR);
	}

	return (BCME_OK);
}
#endif /* BCM_ROUTER_DHD && HNDCTF */

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

void
dhd_dpc_tasklet_dispatcher_work(struct work_struct * work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct dhd_info *dhd;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	dhd = container_of(dw, struct dhd_info, dhd_dpc_dispatcher_work);
	GCC_DIAGNOSTIC_POP();

	DHD_INFO(("%s:\n", __FUNCTION__));

	tasklet_schedule(&dhd->tasklet);
}

void
dhd_schedule_delayed_dpc_on_dpc_cpu(dhd_pub_t *dhdp, ulong delay)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	int dpc_cpu = atomic_read(&dhd->dpc_cpu);
	DHD_INFO(("%s:\n", __FUNCTION__));

	/* scheduler will take care of scheduling to appropriate cpu if dpc_cpu is not online */
	schedule_delayed_work_on(dpc_cpu, &dhd->dhd_dpc_dispatcher_work, delay);

	return;
}

#ifdef SHOW_LOGTRACE
static void
dhd_netif_rx_ni(struct sk_buff * skb)
{
	/* Do not call netif_recieve_skb as this workqueue scheduler is
	 * not from NAPI Also as we are not in INTR context, do not call
	 * netif_rx, instead call netif_rx_ni (for kerenl >= 2.6) which
	 * does netif_rx, disables irq, raise NET_IF_RX softirq and
	 * enables interrupts back
	 */
	netif_rx_ni(skb);
}

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

	pktdata = (void *)skb_mac_header(skb);
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

/*
 * dhd_event_logtrace_process_items processes
 * each skb from evt_trace_queue.
 * Returns TRUE if more packets to be processed
 * else returns FALSE
 */

static int
dhd_event_logtrace_process_items(dhd_info_t *dhd)
{
	dhd_pub_t *dhdp;
	struct sk_buff *skb;
	uint32 qlen;
	uint32 process_len;

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return 0;
	}

	dhdp = &dhd->pub;

	if (!dhdp) {
		DHD_ERROR(("%s: dhd pub is null \n", __FUNCTION__));
		return 0;
	}

#ifdef BCMINTERNAL
#ifdef DHD_FWTRACE
	/* Check if there is any update in the firmware trace buffer */
	process_fw_trace_data(dhdp);
#endif /* DHD_FWTRACE */
#endif /* BCMINTERNAL */
	qlen = skb_queue_len(&dhd->evt_trace_queue);
	process_len = MIN(qlen, DHD_EVENT_LOGTRACE_BOUND);

	/* Run while loop till bound is reached or skb queue is empty */
	while (process_len--) {
		int ifid = 0;
		skb = skb_dequeue(&dhd->evt_trace_queue);
		if (skb == NULL) {
			DHD_ERROR(("%s: skb is NULL, which is not valid case\n",
				__FUNCTION__));
			break;
		}
		BCM_REFERENCE(ifid);
#ifdef PCIE_FULL_DONGLE
		/* Check if pkt is from INFO ring or WLC_E_TRACE */
		ifid = DHD_PKTTAG_IFID((dhd_pkttag_fr_t *)PKTTAG(skb));
		if (ifid == DHD_DUMMY_INFO_IF) {
			/* Process logtrace from info rings */
			dhd_event_logtrace_infobuf_pkt_process(dhdp, skb, &dhd->event_data);
		} else
#endif /* PCIE_FULL_DONGLE */
		{
			/* Processing WLC_E_TRACE case OR non PCIE PCIE_FULL_DONGLE case */
			dhd_event_logtrace_pkt_process(dhdp, skb);
		}

		/* Dummy sleep so that scheduler kicks in after processing any logprints */
		OSL_SLEEP(0);

		/* Send packet up if logtrace_pkt_sendup is TRUE */
		if (dhdp->logtrace_pkt_sendup) {
#ifdef DHD_USE_STATIC_CTRLBUF
			/* If bufs are allocated via static buf pool
			 * and logtrace_pkt_sendup enabled, make a copy,
			 * free the local one and send the copy up.
			 */
			void *npkt = PKTDUP(dhdp->osh, skb);
			/* Clone event and send it up */
			PKTFREE_STATIC(dhdp->osh, skb, FALSE);
			if (npkt) {
				skb = npkt;
			} else {
				DHD_ERROR(("skb clone failed. dropping logtrace pkt.\n"));
				/* Packet is already freed, go to next packet */
				continue;
			}
#endif /* DHD_USE_STATIC_CTRLBUF */
#ifdef PCIE_FULL_DONGLE
			/* For infobuf packets as if is DHD_DUMMY_INFO_IF,
			 * to send skb to network layer, assign skb->dev with
			 * Primary interface n/w device
			 */
			if (ifid == DHD_DUMMY_INFO_IF) {
				skb = PKTTONATIVE(dhdp->osh, skb);
				skb->dev = dhd->iflist[0]->net;
			}
#endif /* PCIE_FULL_DONGLE */
			/* Send pkt UP */
			dhd_netif_rx_ni(skb);
		} else	{
			/* Don't send up. Free up the packet. */
#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhdp->osh, skb, FALSE);
#else
			PKTFREE(dhdp->osh, skb, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
		}
	}

	/* Reschedule if more packets to be processed */
	return (qlen >= DHD_EVENT_LOGTRACE_BOUND);
}

#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
static int
dhd_logtrace_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
	dhd_pub_t *dhdp = (dhd_pub_t *)&dhd->pub;
	int ret;

	while (1) {
		dhdp->logtrace_thr_ts.entry_time = OSL_LOCALTIME_NS();
		if (!binary_sema_down(tsk)) {
			dhdp->logtrace_thr_ts.sem_down_time = OSL_LOCALTIME_NS();
			SMP_RD_BARRIER_DEPENDS();
			if (dhd->pub.dongle_reset == FALSE) {
				do {
					/* Check terminated before processing the items */
					if (tsk->terminated) {
						DHD_ERROR(("%s: task terminated\n", __FUNCTION__));
						goto exit;
					}
#ifdef EWP_EDL
					/* check if EDL is being used */
					if (dhd->pub.dongle_edl_support) {
						ret = dhd_prot_process_edl_complete(&dhd->pub,
								&dhd->event_data);
					} else {
						ret = dhd_event_logtrace_process_items(dhd);
					}
#else
					ret = dhd_event_logtrace_process_items(dhd);
#endif /* EWP_EDL */
					/* if ret > 0, bound has reached so to be fair to other
					 * processes need to yield the scheduler.
					 * The comment above yield()'s definition says:
					 * If you want to use yield() to wait for something,
					 * use wait_event().
					 * If you want to use yield() to be 'nice' for others,
					 * use cond_resched().
					 * If you still want to use yield(), do not!
					 */
					if (ret > 0) {
						cond_resched();
						OSL_SLEEP(DHD_EVENT_LOGTRACE_RESCHEDULE_DELAY_MS);
					} else if (ret < 0) {
						DHD_ERROR(("%s: ERROR should not reach here\n",
							__FUNCTION__));
					}
				} while (ret > 0);
			}
			if (tsk->flush_ind) {
				DHD_ERROR(("%s: flushed\n", __FUNCTION__));
				dhdp->logtrace_thr_ts.flush_time = OSL_LOCALTIME_NS();
				tsk->flush_ind = 0;
				complete(&tsk->flushed);
			}
		} else {
			DHD_ERROR(("%s: unexpted break\n", __FUNCTION__));
			dhdp->logtrace_thr_ts.unexpected_break_time = OSL_LOCALTIME_NS();
			break;
		}
	}
exit:
	complete_and_exit(&tsk->completed, 0);
	dhdp->logtrace_thr_ts.complete_time = OSL_LOCALTIME_NS();
}
#else
static void
dhd_event_logtrace_process(struct work_struct * work)
{
/* Ignore compiler warnings due to -Werror=cast-qual */
	struct delayed_work *dw = to_delayed_work(work);
	struct dhd_info *dhd;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	dhd = container_of(dw, struct dhd_info, event_log_dispatcher_work);
	GCC_DIAGNOSTIC_POP();

#ifdef EWP_EDL
	if (dhd->pub.dongle_edl_support) {
		ret = dhd_prot_process_edl_complete(&dhd->pub, &dhd->event_data);
	} else {
		ret = dhd_event_logtrace_process_items(dhd);
	}
#else
	ret = dhd_event_logtrace_process_items(dhd);
#endif /* EWP_EDL */

	if (ret > 0) {
		schedule_delayed_work(&(dhd)->event_log_dispatcher_work,
			msecs_to_jiffies(DHD_EVENT_LOGTRACE_RESCHEDULE_DELAY_MS));
	}
	return;
}
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */

void
dhd_schedule_logtrace(void *dhd_info)
{
	dhd_info_t *dhd = (dhd_info_t *)dhd_info;

#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
	if (dhd->thr_logtrace_ctl.thr_pid >= 0) {
		binary_sema_up(&dhd->thr_logtrace_ctl);
	} else {
		DHD_ERROR(("%s: thr_logtrace_ctl(%ld) not inited\n", __FUNCTION__,
			dhd->thr_logtrace_ctl.thr_pid));
	}
#else
	schedule_delayed_work(&dhd->event_log_dispatcher_work, 0);
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */
	return;
}

void
dhd_cancel_logtrace_process_sync(dhd_info_t *dhd)
{
#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
	if (dhd->thr_logtrace_ctl.thr_pid >= 0) {
		PROC_STOP_USING_BINARY_SEMA(&dhd->thr_logtrace_ctl);
	} else {
		DHD_ERROR(("%s: thr_logtrace_ctl(%ld) not inited\n", __FUNCTION__,
			dhd->thr_logtrace_ctl.thr_pid));
	}
#else
	cancel_delayed_work_sync(&dhd->event_log_dispatcher_work);
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */
}

void
dhd_flush_logtrace_process(dhd_info_t *dhd)
{
#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
	if (dhd->thr_logtrace_ctl.thr_pid >= 0) {
		PROC_FLUSH_USING_BINARY_SEMA(&dhd->thr_logtrace_ctl);
	} else {
		DHD_ERROR(("%s: thr_logtrace_ctl(%ld) not inited\n", __FUNCTION__,
			dhd->thr_logtrace_ctl.thr_pid));
	}
#else
	flush_delayed_work(&dhd->event_log_dispatcher_work);
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */
}

int
dhd_init_logtrace_process(dhd_info_t *dhd)
{
#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
	dhd->thr_logtrace_ctl.thr_pid = DHD_PID_KT_INVALID;
	PROC_START(dhd_logtrace_thread, dhd, &dhd->thr_logtrace_ctl, 0, "dhd_logtrace_thread");
	if (dhd->thr_logtrace_ctl.thr_pid < 0) {
		DHD_ERROR(("%s: init logtrace process failed\n", __FUNCTION__));
		return BCME_ERROR;
	} else {
		DHD_ERROR(("%s: thr_logtrace_ctl(%ld) not inited\n", __FUNCTION__,
			dhd->thr_logtrace_ctl.thr_pid));
	}
#else
	INIT_DELAYED_WORK(&dhd->event_log_dispatcher_work, dhd_event_logtrace_process);
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */
	return BCME_OK;
}

int
dhd_reinit_logtrace_process(dhd_info_t *dhd)
{
#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
	/* Re-init only if PROC_STOP from dhd_stop was called
	 * which can be checked via thr_pid
	 */
	if (dhd->thr_logtrace_ctl.thr_pid < 0) {
		PROC_START(dhd_logtrace_thread, dhd, &dhd->thr_logtrace_ctl,
			0, "dhd_logtrace_thread");
		if (dhd->thr_logtrace_ctl.thr_pid < 0) {
			DHD_ERROR(("%s: reinit logtrace process failed\n", __FUNCTION__));
			return BCME_ERROR;
		} else {
		DHD_ERROR(("%s: thr_logtrace_ctl(%ld) not inited\n", __FUNCTION__,
			dhd->thr_logtrace_ctl.thr_pid));
	}
	}
#else
	/* No need to re-init for WQ as calcel_delayed_work_sync will
	 * will not delete the WQ
	 */
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */
	return BCME_OK;
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

	dhd_schedule_logtrace(dhd);
}

#ifdef BCMINTERNAL
#ifdef DHD_FWTRACE
void
dhd_event_logtrace_enqueue_fwtrace(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *) dhdp->info;

	/* Schedule a kernel thread */
	dhd_schedule_logtrace(dhd);

	return;
}
#endif	/* DHD_FWTRACE */
#endif	/* BCMINTERNAL */

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

#ifdef EWP_EDL
void
dhd_sendup_info_buf(dhd_pub_t *dhdp, uint8 *msg)
{
	struct sk_buff *skb = NULL;
	uint32 pktsize = 0;
	void *pkt = NULL;
	info_buf_payload_hdr_t *infobuf = NULL;
	dhd_info_t *dhd = dhdp->info;
	uint8 *pktdata = NULL;

	if (!msg)
		return;

	/* msg = |infobuf_ver(u32)|info_buf_payload_hdr_t|msgtrace_hdr_t|<var len data>| */
	infobuf = (info_buf_payload_hdr_t *)(msg + sizeof(uint32));
	pktsize = (uint32)(ltoh16(infobuf->length) + sizeof(info_buf_payload_hdr_t) +
			sizeof(uint32));
	pkt = PKTGET(dhdp->osh, pktsize, FALSE);
	if (!pkt) {
		DHD_ERROR(("%s: skb alloc failed ! not sending event log up.\n", __FUNCTION__));
	} else {
		PKTSETLEN(dhdp->osh, pkt, pktsize);
		pktdata = PKTDATA(dhdp->osh, pkt);
		memcpy(pktdata, msg, pktsize);
		/* For infobuf packets assign skb->dev with
		 * Primary interface n/w device
		 */
		skb = PKTTONATIVE(dhdp->osh, pkt);
		skb->dev = dhd->iflist[0]->net;
		/* Send pkt UP */
		dhd_netif_rx_ni(skb);
	}
}
#endif /* EWP_EDL */
#endif /* SHOW_LOGTRACE */

#ifdef BTLOG
static void
dhd_bt_log_process(struct work_struct *work)
{
	struct dhd_info *dhd;
	dhd_pub_t *dhdp;
	struct sk_buff *skb;

	/* Ignore compiler warnings due to -Werror=cast-qual */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	dhd = container_of(work, struct dhd_info, bt_log_dispatcher_work);
	GCC_DIAGNOSTIC_POP();

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
	while ((skb = skb_dequeue(&dhd->bt_log_queue)) != NULL) {
		dhd_bt_log_pkt_process(dhdp, skb);
#ifdef DHD_USE_STATIC_CTRLBUF
		PKTFREE_STATIC(dhdp->osh, skb, FALSE);
#else
		PKTFREE(dhdp->osh, skb, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
	}
}

void
dhd_rx_bt_log(dhd_pub_t *dhdp, void *pkt)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	skb_queue_tail(&dhd->bt_log_queue, pkt);

	/* schedule workqueue to process bt logs */
	schedule_work(&dhd->bt_log_dispatcher_work);
}
#endif	/* BTLOG */

#ifdef EWP_EDL
static void
dhd_edl_process_work(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct dhd_info *dhd_info;
	/* Ignore compiler warnings due to -Werror=cast-qual */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	dhd_info = container_of(dw, struct dhd_info, edl_dispatcher_work);
	GCC_DIAGNOSTIC_POP();

	if (dhd_info)
		dhd_prot_process_edl_complete(&dhd_info->pub, &dhd_info->event_data);
}

void
dhd_schedule_edl_work(dhd_pub_t *dhdp, uint delay_ms)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	schedule_delayed_work(&dhd->edl_dispatcher_work, msecs_to_jiffies(delay_ms));
}
#endif /* EWP_EDL */

#ifdef WL_NANHO
/* forward NAN event to NANHO host module. API returns TRUE if event is consumed by NANHO */
static bool
dhd_nho_evt_process(dhd_pub_t *pub, int ifidx, wl_event_msg_t *evt_msg,
	void *pktdata, uint16 pktlen)
{
	uint32 evt_type = ntoh32_ua(&evt_msg->event_type);
	bool consumed = FALSE;

	if ((evt_type == WLC_E_NAN_CRITICAL) || (evt_type == WLC_E_NAN_NON_CRITICAL)) {
		bcm_event_t *pvt_data = (bcm_event_t *)pktdata;
		uint32 event_len = sizeof(wl_event_msg_t) + ntoh32_ua(&evt_msg->datalen);

		bcm_nanho_evt(pub->nanhoi, &pvt_data->event, event_len, &consumed);
	}
	return consumed;
}

static int
dhd_nho_evt_cb(void *drv_ctx, int ifidx, bcm_event_t *evt, uint16 evt_len)
{
	struct sk_buff *p, *skb;
	dhd_if_t *ifp;
	dhd_pub_t *dhdp = (dhd_pub_t *)drv_ctx;

	if ((p = PKTGET(dhdp->osh, evt_len, FALSE))) {
		memcpy(PKTDATA(dhdp->osh, p), (uint8 *)evt, evt_len);
		skb = PKTTONATIVE(dhdp->osh, p);

		ifp = dhdp->info->iflist[ifidx];
		if (ifp == NULL) {
			/* default to main interface */
			ifp = dhdp->info->iflist[0];
		}
		ASSERT(ifp);

		skb->dev = ifp->net;
		skb->protocol = eth_type_trans(skb, skb->dev);

		/* strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* send the packet */
		if (in_interrupt()) {
			netif_rx(skb);
		} else {
			netif_rx_ni(skb);
		}
	} else {
		DHD_ERROR(("NHO: dhd_nho_evt_cb: unable to alloc sk_buf"));
		return BCME_NOMEM;
	}

	return BCME_OK;
}
#endif /* WL_NANHO */

#ifdef ENABLE_WAKEUP_PKT_DUMP
static void
update_wake_pkt_info(struct sk_buff *skb)
{
	struct iphdr *ip_header;
	struct ipv6hdr *ipv6_header;
	struct udphdr *udp_header;
	struct tcphdr *tcp_header;
	uint16 dport = 0;

	ip_header = (struct iphdr *)(skb->data);

	temp_raw |= ((long long)ntoh16(skb->protocol)) << 48;

	DHD_INFO(("eth_hdr(skb)->h_dest : %pM\n", eth_hdr(skb)->h_dest));
	if (eth_hdr(skb)->h_dest[0] & 0x01) {
		temp_raw |= (long long)1 << 39;
	}

	if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM) {
		wl_event_msg_t event;
		bcm_event_msg_u_t evu;
		int ret;
		uint event_type;

		ret = wl_host_event_get_data(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
			skb_mac_header(skb),
#else
			skb->mac.raw,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) */
			skb->len, &evu);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: wl_host_event_get_data err = %d\n",
				__FUNCTION__, ret));
		}

		memcpy(&event, &evu.event, sizeof(wl_event_msg_t));
		event_type = ntoh32_ua((void *)&event.event_type);

		temp_raw |= (long long)event_type << 40;
	} else if (ntoh16(skb->protocol) == ETHER_TYPE_IP ||
			ntoh16(skb->protocol) == ETHER_TYPE_IPV6) {
		if (ip_header->version == 6) {
			ipv6_header = (struct ipv6hdr *)ip_header;
			temp_raw |= ((long long)ipv6_header->nexthdr) << 40;
			dport = 0;

			if (ipv6_header->daddr.s6_addr[0] & 0xff) {
				temp_raw |= (long long)1 << 38;
			}

			DHD_INFO(("IPv6 [%x]%pI6c > %pI6c:%d\n",
				ip_header->protocol, &(ipv6_header->saddr.s6_addr),
				&(ipv6_header->daddr.s6_addr), dport));
		} else if (ip_header->version == 4) {
			temp_raw |= ((long long)ip_header->protocol) << 40;

#define IP_HDR_OFFSET	((char *)ip_header + IPV4_HLEN(ip_header))
			if (ip_header->protocol == IPPROTO_TCP) {
				tcp_header = (struct tcphdr *)IP_HDR_OFFSET;
				dport = ntohs(tcp_header->dest);
			}
			else if (ip_header->protocol == IPPROTO_UDP) {
				udp_header = (struct udphdr *)IP_HDR_OFFSET;
				dport = ntohs(udp_header->dest);
			}

			if (ipv4_is_multicast(ip_header->daddr)) {
				temp_raw |= (long long)1 << 38;
			}

			DHD_INFO(("IP [%x] %pI4 > %pI4:%d\n",
				ip_header->protocol, &(ip_header->saddr),
				&(ip_header->daddr), dport));
		}

		temp_raw |= (long long)dport << 16;
	}
}
#endif /* ENABLE_WAKEUP_PKT_DUMP */

#if defined(BCMPCIE)
int
dhd_check_shinfo_nrfrags(dhd_pub_t *dhdp, void *pktbuf,
	dmaaddr_t *pa, uint32 pktid)
{
	struct sk_buff *skb;
	struct skb_shared_info *shinfo;

	if (!pktbuf)
		return BCME_ERROR;

	skb = PKTTONATIVE(dhdp->osh, pktbuf);
	shinfo = skb_shinfo(skb);

	if (shinfo->nr_frags) {
#ifdef BCMDMA64OSL
		DHD_ERROR(("!!Invalid nr_frags: %u pa.loaddr: 0x%llx pa.hiaddr: 0x%llx "
			"skb: 0x%llx skb_data: 0x%llx skb_head: 0x%llx skb_tail: 0x%llx "
			"skb_end: 0x%llx skb_len: %u shinfo: 0x%llx pktid: %u\n",
			shinfo->nr_frags, (uint64)(pa->loaddr), (uint64)(pa->hiaddr),
			(uint64)skb, (uint64)(skb->data), (uint64)(skb->head), (uint64)(skb->tail),
			(uint64)(skb->end), skb->len, (uint64)shinfo, pktid));
#else
		DHD_ERROR(("!!Invalid nr_frags: %u "
			"skb: 0x%x skb_data: 0x%x skb_head: 0x%x skb_tail: 0x%x "
			"skb_end: 0x%x skb_len: %u shinfo: 0x%x pktid: %u\n",
			shinfo->nr_frags,
			(uint)skb, (uint)(skb->data), (uint)(skb->head), (uint)(skb->tail),
			(uint)(skb->end), skb->len, (uint)shinfo, pktid));
#endif
		prhex("shinfo", (char*)shinfo, sizeof(struct skb_shared_info));
		if (!dhd_query_bus_erros(dhdp)) {
#ifdef DHD_FW_COREDUMP
			/* Collect socram dump */
			if (dhdp->memdump_enabled) {
				/* collect core dump */
				dhdp->memdump_type = DUMP_TYPE_INVALID_SHINFO_NRFRAGS;
				dhd_bus_mem_dump(dhdp);
			} else
#endif /* DHD_FW_COREDUMP */
			{
				shinfo->nr_frags = 0;
				/* In production case, free the packet and continue
				 * if nfrags is corrupted. Whereas in non-production
				 * case collect memdump and call BUG_ON().
				 */
				PKTCFREE(dhdp->osh, pktbuf, FALSE);
			}
		}
		return BCME_ERROR;
	}
	return BCME_OK;
}
#endif /* BCMPCIE */

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
#if defined(OEM_ANDROID)
	int tout_rx = 0;
	int tout_ctrl = 0;
#endif /* OEM_ANDROID */
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
	wake_counts_t *wcp = NULL;
#endif /* DHD_WAKE_STATUS */
	int pkt_wake = 0;
#ifdef ENABLE_DHD_GRO
	bool dhd_gro_enable = TRUE;
	struct Qdisc *qdisc = NULL;
#endif /* ENABLE_DHD_GRO */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	BCM_REFERENCE(dump_data);
	BCM_REFERENCE(pkt_wake);

#ifdef DHD_TPUT_PATCH
	if (dhdp->conf->pktsetsum)
		PKTSETSUMGOOD(pktbuf, TRUE);
#endif

#ifdef ENABLE_DHD_GRO
	if (ifidx < DHD_MAX_IFS) {
		ifp = dhd->iflist[ifidx];
		if (ifp && ifp->net->qdisc) {
			if (ifp->net->qdisc->ops->cl_ops) {
				dhd_gro_enable = FALSE;
				DHD_TRACE(("%s: disable sw gro becasue of"
						" qdisc tx traffic control\n", __FUNCTION__));
			}

			if (dev_ingress_queue(ifp->net)) {
				qdisc = dev_ingress_queue(ifp->net)->qdisc_sleeping;
				if (qdisc != NULL && (qdisc->flags & TCQ_F_INGRESS)) {
					dhd_gro_enable = FALSE;
					DHD_TRACE(("%s: disable sw gro because of"
						" qdisc rx traffic control\n", __FUNCTION__));
				}
			}
		}
	}
#ifdef DHD_GRO_ENABLE_HOST_CTRL
	if (!dhdp->permitted_gro && dhd_gro_enable) {
		dhd_gro_enable = FALSE;
	}
#endif /* DHD_GRO_ENABLE_HOST_CTRL */
#endif /* ENABLE_DHD_GRO */

	for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {
		struct ether_header *eh;

		pnext = PKTNEXT(dhdp->osh, pktbuf);
		PKTSETNEXT(dhdp->osh, pktbuf, NULL);

		/* info ring "debug" data, which is not a 802.3 frame, is sent/hacked with a
		 * special ifidx of DHD_DUMMY_INFO_IF.  This is just internal to dhd to get the data
		 * from dhd_msgbuf.c:dhd_prot_infobuf_cmplt_process() to here (dhd_rx_frame).
		 */
		if (ifidx == DHD_DUMMY_INFO_IF) {
			/* Event msg printing is called from dhd_rx_frame which is in Tasklet
			 * context in case of PCIe FD, in case of other bus this will be from
			 * DPC context. If we get bunch of events from Dongle then printing all
			 * of them from Tasklet/DPC context that too in data path is costly.
			 * Also in the new Dongle SW(4359, 4355 onwards) console prints too come as
			 * events with type WLC_E_TRACE.
			 * We'll print this console logs from the WorkQueue context by enqueing SKB
			 * here and Dequeuing will be done in WorkQueue and will be freed only if
			 * logtrace_pkt_sendup is TRUE
			 */
#ifdef SHOW_LOGTRACE
			dhd_event_logtrace_enqueue(dhdp, ifidx, pktbuf);
#else /* !SHOW_LOGTRACE */
		/* If SHOW_LOGTRACE not defined and ifidx is DHD_DUMMY_INFO_IF,
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
			/* If wakeinfo count buffer is null do not  update wake count values */
			pkt_wake = 0;
		}
#endif /* DHD_WAKE_STATUS */

		eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);
#ifdef DHD_AWDL
		if (dhdp->awdl_llc_enabled &&
			dhdp->awdl_ifidx && ifidx == dhdp->awdl_ifidx) {
			if (ntoh16(eh->ether_type) < ETHER_TYPE_MIN) {
				dhd_awdl_llc_to_eth_hdr(dhdp, eh, pktbuf);
			}
		}
#endif /* DHD_AWDL */

		if (dhd->pub.tput_data.tput_test_running &&
			dhd->pub.tput_data.direction == TPUT_DIR_RX &&
			ntoh16(eh->ether_type) == ETHER_TYPE_IP) {
			dhd_tput_test_rx(dhdp, pktbuf);
			PKTFREE(dhd->pub.osh, pktbuf, FALSE);
			continue;
		}

		if (ifidx >= DHD_MAX_IFS) {
			DHD_ERROR(("%s: ifidx(%d) Out of bound. drop packet\n",
				__FUNCTION__, ifidx));
			if (ntoh16(eh->ether_type) == ETHER_TYPE_BRCM) {
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			} else {
				PKTCFREE(dhdp->osh, pktbuf, FALSE);
			}
			continue;
		}

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL) {
			DHD_ERROR_RLMT(("%s: ifp is NULL. drop packet\n",
				__FUNCTION__));
			if (ntoh16(eh->ether_type) == ETHER_TYPE_BRCM) {
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			} else {
				PKTCFREE(dhdp->osh, pktbuf, FALSE);
			}
			continue;
		}

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
		if (ifp->block_tdls) {
			if (bcm_l2_filter_block_tdls(dhdp->osh, pktbuf) == BCME_OK) {
				PKTCFREE(dhdp->osh, pktbuf, FALSE);
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

#ifdef DHDTCPSYNC_FLOOD_BLK
		if (dhd_tcpdata_get_flag(dhdp, pktbuf) == FLAG_SYNC) {
			int delta_sec;
			int delta_sync;
			int sync_per_sec;
			u64 curr_time = DIV_U64_BY_U32(OSL_LOCALTIME_NS(), NSEC_PER_SEC);
			ifp->tsync_rcvd ++;
			delta_sync = ifp->tsync_rcvd - ifp->tsyncack_txed;
			delta_sec = curr_time - ifp->last_sync;
			if (delta_sec > 1) {
				sync_per_sec = delta_sync/delta_sec;
				if (sync_per_sec > TCP_SYNC_FLOOD_LIMIT) {
					schedule_work(&ifp->blk_tsfl_work);
					DHD_ERROR(("ifx %d TCP SYNC Flood attack suspected! "
						"sync recvied %d pkt/sec \n",
						ifidx, sync_per_sec));
					ifp->tsync_per_sec = sync_per_sec;
				}
				dhd_reset_tcpsync_info_by_ifp(ifp);
			}

		}
#endif /* DHDTCPSYNC_FLOOD_BLK */

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
		if (PSR_ENABLED(dhdp) &&
#ifdef BCM_ROUTER_DHD
				!(ifp->primsta_dwds) &&
#endif /* BCM_ROUTER_DHD */
				(dhd_psta_proc(dhdp, ifidx, &pktbuf, FALSE) < 0)) {
			DHD_ERROR(("%s:%s: psta recv proc failed\n", __FUNCTION__,
				dhd_ifname(dhdp, ifidx)));
		}
#endif /* DHD_PSTA */

#if defined(BCM_ROUTER_DHD)
		/* XXX Use WOFA for both dhdap and dhdap-atlas router. */
		/* XXX dhd_sendpkt verify pkt accounting (TO/FRM NATIVE) and PKTCFREE */

		if (DHD_IF_ROLE_AP(dhdp, ifidx) && (!ifp->ap_isolate)) {
			eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);
			if (ETHER_ISUCAST(eh->ether_dhost)) {
				if (dhd_find_sta(dhdp, ifidx, (void *)eh->ether_dhost)) {
					dhd_sendpkt(dhdp, ifidx, pktbuf);
					continue;
				}
			} else {
				void *npkt;
#if defined(HNDCTF)
				if (PKTISCHAINED(pktbuf)) { /* XXX WAR */
					DHD_ERROR(("Error: %s():%d Chained non unicast pkt<%p>\n",
						__FUNCTION__, __LINE__, pktbuf));
					PKTFRMNATIVE(dhdp->osh, pktbuf);
					PKTCFREE(dhdp->osh, pktbuf, FALSE);
					continue;
				}
#endif /* HNDCTF */
				if ((ntoh16(eh->ether_type) != ETHER_TYPE_IAPP_L2_UPDATE) &&
					((npkt = PKTDUP(dhdp->osh, pktbuf)) != NULL))
					dhd_sendpkt(dhdp, ifidx, npkt);
			}
		}

#if defined(HNDCTF)
		/* try cut thru' before sending up */
		if (dhd_ctf_forward(dhd, skb, &pnext) == BCME_OK) {
			continue;
		}
#endif /* HNDCTF */

#else /* !BCM_ROUTER_DHD */
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
				if ((ntoh16(eh->ether_type) != ETHER_TYPE_IAPP_L2_UPDATE)) {
					void *npktbuf = NULL;
					/*
					* If host_sfhllc_supported enabled, do skb_copy as SFHLLC
					* header will be inserted during Tx, due to which network
					* stack will not decode the Rx packet.
					* Else PKTDUP(skb_clone) is enough.
					*/
					if (dhdp->host_sfhllc_supported) {
						npktbuf = skb_copy(skb, GFP_ATOMIC);
					} else {
						npktbuf = PKTDUP(dhdp->osh, pktbuf);
					}
					if (npktbuf != NULL) {
						dhd_sendpkt(dhdp, ifidx, npktbuf);
					}
				}
			}
		}
#endif /* PCIE_FULL_DONGLE */
#endif /* BCM_ROUTER_DHD */
#ifdef DHD_POST_EAPOL_M1_AFTER_ROAM_EVT
		if (IS_STA_IFACE(ndev_to_wdev(ifp->net)) &&
			(ifp->recv_reassoc_evt == TRUE) && (ifp->post_roam_evt == FALSE) &&
			(dhd_is_4way_msg((char *)(skb->data)) == EAPOL_4WAY_M1)) {
				DHD_ERROR(("%s: Reassoc is in progress. "
					"Drop EAPOL M1 frame\n", __FUNCTION__));
				PKTFREE(dhdp->osh, pktbuf, FALSE);
				continue;
		}
#endif /* DHD_POST_EAPOL_M1_AFTER_ROAM_EVT */
#ifdef WLEASYMESH
		if ((dhdp->conf->fw_type == FW_TYPE_EZMESH) &&
				(ntoh16(eh->ether_type) != ETHER_TYPE_BRCM)) {
			uint16 * da = (uint16 *)(eh->ether_dhost);
			ASSERT(ISALIGNED(da, 2));

			/* XXX: Special handling for 1905 messages
			 * if DA matches with configured 1905 AL MAC addresses
			 * bypass fwder and foward it to linux stack
			 */
			if (ntoh16(eh->ether_type) == ETHER_TYPE_1905_1) {
				if (!eacmp(da, ifp->_1905_al_ucast) || !eacmp(da, ifp->_1905_al_mcast)) {
					//skb->fwr_flood = 0;
				} else {
					//skb->fwr_flood = 1;
				}
			}
		}
#endif /* WLEASYMESH */
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
#if defined(WL_CFG80211) && defined(WL_WPS_SYNC)
			wl_handle_wps_states(ifp->net, dump_data, len, FALSE);
#endif /* WL_CFG80211 && WL_WPS_SYNC */
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
			if (dhd_is_4way_msg((uint8 *)(skb->data)) == EAPOL_4WAY_M3) {
				OSL_ATOMIC_SET(dhdp->osh, &ifp->m4state, M3_RXED);
			}
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */
#ifdef EAPOL_RESEND
			wl_ext_release_eapol_txpkt(dhdp, ifidx, TRUE);
#endif /* EAPOL_RESEND */
		}
		dhd_dump_pkt(dhdp, ifidx, dump_data, len, FALSE, NULL, NULL);

#if defined(DHD_WAKE_STATUS) && defined(DHD_WAKEPKT_DUMP)
		if (pkt_wake) {
			dhd_prhex("[wakepkt_dump]", (char*)dump_data, MIN(len, 64), DHD_ERROR_VAL);
			DHD_ERROR(("config check in_suspend: %d ", dhdp->in_suspend));
#ifdef ARP_OFFLOAD_SUPPORT
			DHD_ERROR(("arp hmac_update:%d \n", dhdp->hmac_updated));
#endif /* ARP_OFFLOAD_SUPPORT */
		}
#endif /* DHD_WAKE_STATUS && DHD_WAKEPKT_DUMP */

#ifdef BCMINTERNAL
		if (dhd->pub.loopback) {
			struct ether_header *local_eh = (struct ether_header *)eth;
			if (ntoh16(local_eh->ether_type) == ETHER_TYPE_IP) {
				uint8 *myp = (uint8 *)local_eh;
				struct ipv4_hdr *iph = (struct ipv4_hdr *)(myp + ETHER_HDR_LEN);
				uint16 iplen = (iph->version_ihl & 0xf) * sizeof(uint32);
				if (iph->prot == 1) {
					uint8 *icmph = (uint8 *)iph + iplen;
					if (icmph[0] == 8) {
						uint8 temp_addr[ETHER_ADDR_LEN];
						uint8 temp_ip[IPV4_ADDR_LEN];
						/* Ether header flip */
						memcpy(temp_addr, local_eh->ether_dhost,
							ETHER_ADDR_LEN);
						memcpy(local_eh->ether_dhost,
							local_eh->ether_shost, ETHER_ADDR_LEN);
						memcpy(local_eh->ether_shost, temp_addr,
							ETHER_ADDR_LEN);

						/* IP header flip */
						memcpy(temp_ip, iph->src_ip, IPV4_ADDR_LEN);
						memcpy(iph->src_ip, iph->dst_ip, IPV4_ADDR_LEN);
						memcpy(iph->dst_ip, temp_ip, IPV4_ADDR_LEN);

						/* ICMP header flip */
						icmph[0] = 0;
					}
				} else if (iph->prot == 17) {
					uint8 *udph = (uint8 *)iph + iplen;
					uint16 destport = ntoh16(*((uint16 *)udph + 1));
					if (destport == 8888) {
						uint8 temp_addr[ETHER_ADDR_LEN];
						uint8 temp_ip[IPV4_ADDR_LEN];
						/* Ether header flip */
						memcpy(temp_addr, local_eh->ether_dhost,
							ETHER_ADDR_LEN);
						memcpy(local_eh->ether_dhost,
							local_eh->ether_shost, ETHER_ADDR_LEN);
						memcpy(local_eh->ether_shost, temp_addr,
							ETHER_ADDR_LEN);

						/* IP header flip */
						memcpy(temp_ip, iph->src_ip, IPV4_ADDR_LEN);
						memcpy(iph->src_ip, iph->dst_ip, IPV4_ADDR_LEN);
						memcpy(iph->dst_ip, temp_ip, IPV4_ADDR_LEN);

						/* Reset UDP checksum to */
						*((uint16 *)udph + 3) = 0;
					}
				}
			}
		}
#endif /* BCMINTERNAL */
		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST) {
			dhd->pub.rx_multicast++;
			ifp->stats.multicast++;
		}

		skb->data = eth;
		skb->len = len;

		/* TODO: XXX: re-look into dropped packets. */
		DHD_DBG_PKT_MON_RX(dhdp, skb);
		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

#ifdef ENABLE_WAKEUP_PKT_DUMP
		if (dhd_mmc_wake) {
			DHD_INFO(("wake_pkt %s(%d)\n", __FUNCTION__, __LINE__));
			if (DHD_INFO_ON()) {
				prhex("wake_pkt", (char*) eth, MIN(len, 48));
			}
			update_wake_pkt_info(skb);
#ifdef CONFIG_IRQ_HISTORY
			add_irq_history(0, "WIFI");
#endif
			dhd_mmc_wake = FALSE;
		}
#endif /* ENABLE_WAKEUP_PKT_DUMP */

		/* Process special event packets and then discard them */
		/* XXX Decide on a better way to fit this in */
		memset(&event, 0, sizeof(event));

		if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM) {
			bcm_event_msg_u_t evu;
			int ret_event, event_type;
			void *pkt_data = skb_mac_header(skb);

			ret_event = wl_host_event_get_data(pkt_data, len, &evu);

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
			 * logtrace_pkt_sendup is true
			 */
			if (event_type == WLC_E_TRACE) {
				DHD_TRACE(("%s: WLC_E_TRACE\n", __FUNCTION__));
				dhd_event_logtrace_enqueue(dhdp, ifidx, pktbuf);
				continue;
			}
#endif /* SHOW_LOGTRACE */

#ifdef WL_NANHO
			/* Process firmware NAN event by NANHO host module */
			if (dhd_nho_evt_process(dhdp, ifidx, &event, pkt_data, len)) {
				/* NANHO host module consumed NAN event. free pkt here. */
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif
				continue;
			}
#endif /* WL_NANHO */

			ret_event = dhd_wl_host_event(dhd, ifidx, pkt_data, len, &event, &data);

			wl_event_to_host_order(&event);
#if defined(OEM_ANDROID)
			if (!tout_ctrl)
				tout_ctrl = DHD_PACKET_TIMEOUT_MS;
#endif /* OEM_ANDROID */

#if (defined(OEM_ANDROID) && defined(PNO_SUPPORT))
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

#ifdef SENDPROB
			if (dhdp->wl_event_enabled ||
				(dhdp->recv_probereq && (event.event_type == WLC_E_PROBREQ_MSG)))
#else
			if (dhdp->wl_event_enabled)
#endif
			{
#ifdef DHD_USE_STATIC_CTRLBUF
				/* If event bufs are allocated via static buf pool
				 * and wl events are enabled, make a copy, free the
				 * local one and send the copy up.
				 */
				struct sk_buff *nskb = skb_copy(skb, GFP_ATOMIC);
				/* Copy event and send it up */
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
				if (nskb) {
					skb = nskb;
				} else {
					DHD_ERROR(("skb clone failed. dropping event.\n"));
					continue;
				}
#endif /* DHD_USE_STATIC_CTRLBUF */
			} else {
				/* If event enabled not explictly set, drop events */
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
				continue;
			}
		} else {
#if defined(OEM_ANDROID)
			tout_rx = DHD_PACKET_TIMEOUT_MS;
#endif /* OEM_ANDROID */

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
		ifp->net->last_rx = jiffies;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0) */

		if (ntoh16(skb->protocol) != ETHER_TYPE_BRCM) {
			dhdp->dstats.rx_bytes += skb->len;
			dhdp->rx_packets++; /* Local count */
			ifp->stats.rx_bytes += skb->len;
			ifp->stats.rx_packets++;
		}
#if defined(DHD_TCP_WINSIZE_ADJUST)
		if (dhd_use_tcp_window_size_adjust) {
			if (ifidx == 0 && ntoh16(skb->protocol) == ETHER_TYPE_IP) {
				dhd_adjust_tcp_winsize(dhdp->op_mode, skb);
			}
		}
#endif /* DHD_TCP_WINSIZE_ADJUST */

		/* XXX WL here makes sure data is 4-byte aligned? */
		if (in_interrupt()) {
			bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
				__FUNCTION__, __LINE__);
#if defined(DHD_LB_RXP)
#ifdef ENABLE_DHD_GRO
			/* The pktlog module clones a skb using skb_clone and
			 * stores the skb point to the ring buffer of the pktlog module.
			 * Once the buffer is full,
			 * the PKTFREE is called for removing the oldest skb.
			 * The kernel panic occurred when the pktlog module free
			 * the rx frame handled by napi_gro_receive().
			 * It is a fix code that DHD don't use napi_gro_receive() for
			 * the packet used in pktlog module.
			 */
			if (dhd_gro_enable && !skb_cloned(skb) &&
				ntoh16(skb->protocol) != ETHER_TYPE_BRCM) {
				napi_gro_receive(&dhd->rx_napi_struct, skb);
			} else {
				netif_receive_skb(skb);
			}
#else
#if defined(WL_MONITOR) && defined(BCMSDIO)
			if (dhd_monitor_enabled(dhdp, ifidx))
				dhd_rx_mon_pkt_sdio(dhdp, skb, ifidx);
			else
#endif /* WL_MONITOR && BCMSDIO */
			netif_receive_skb(skb);
#endif /* ENABLE_DHD_GRO */
#else /* !defined(DHD_LB_RXP) */
			netif_rx(skb);
#endif /* !defined(DHD_LB_RXP) */
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

#if defined(BCMPCIE) && defined(DHDTCPACK_SUPPRESS)
				dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif /* BCMPCIE && DHDTCPACK_SUPPRESS */
#if defined(DHD_LB_RXP)
#ifdef ENABLE_DHD_GRO
				if (dhd_gro_enable && !skb_cloned(skb) &&
					ntoh16(skb->protocol) != ETHER_TYPE_BRCM) {
					napi_gro_receive(&dhd->rx_napi_struct, skb);
				} else {
					netif_receive_skb(skb);
				}
#else
				netif_receive_skb(skb);
#endif /* ENABLE_DHD_GRO */
#else /* !defined(DHD_LB_RXP) */
				netif_rx_ni(skb);
#endif /* !defined(DHD_LB_RXP) */
			}
		}
	}

	if (dhd->rxthread_enabled && skbhead)
		dhd_sched_rxf(dhdp, skbhead);

#if defined(OEM_ANDROID)
	DHD_OS_WAKE_LOCK_RX_TIMEOUT_ENABLE(dhdp, tout_rx);
	DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(dhdp, tout_ctrl);
#endif /* OEM_ANDROID */
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

	if (dhdp->tput_data.tput_test_running) {

		dhdp->batch_tx_pkts_cmpl++;

		/* don't count the stop pkt */
		if (success &&
			dhdp->batch_tx_pkts_cmpl <= dhdp->batch_tx_num_pkts)
			dhdp->tput_data.pkts_good++;
		else if (!success)
			dhdp->tput_data.pkts_bad++;

		/* we dont care for the stop packet in tput test */
		if (dhdp->batch_tx_pkts_cmpl == dhdp->batch_tx_num_pkts) {
			dhdp->tput_stop_ts = OSL_SYSUPTIME_US();
			dhdp->tput_data.pkts_cmpl += dhdp->batch_tx_pkts_cmpl;
			dhdp->tput_data.num_pkts += dhdp->batch_tx_num_pkts;
			dhd_os_tput_test_wake(dhdp);
		}
	}
	/* XXX where does this stuff belong to? */
	dhd_prot_hdrpull(dhdp, NULL, txp, NULL, NULL);

	/* XXX Use packet tag when it is available to identify its type */

	eh = (struct ether_header *)PKTDATA(dhdp->osh, txp);
	type  = ntoh16(eh->ether_type);

	if (type == ETHER_TYPE_802_1X) {
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
	if (success) {
		dhd->pub.tot_txcpl++;
	}
}

int dhd_os_tput_test_wait(dhd_pub_t *pub, uint *condition,
		uint timeout_ms)
{
	int timeout;

	/* Convert timeout in millsecond to jiffies */
	timeout = msecs_to_jiffies(timeout_ms);
	pub->tput_test_done = FALSE;
	condition = (uint *)&pub->tput_test_done;
	timeout = wait_event_timeout(pub->tx_tput_test_wait,
		(*condition), timeout);

	return timeout;
}

int dhd_os_tput_test_wake(dhd_pub_t * pub)
{
	OSL_SMP_WMB();
	pub->tput_test_done = TRUE;
	OSL_SMP_WMB();
	wake_up(&(pub->tx_tput_test_wait));
	return 0;
}

static struct net_device_stats *
dhd_get_stats(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		goto error;
	}

	ifp = dhd_get_ifp_by_ndev(&dhd->pub, net);
	if (!ifp) {
		/* return empty stats */
		DHD_ERROR(("%s: BAD_IF\n", __FUNCTION__));
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
#ifdef BCMPCIE
			DHD_OS_WD_WAKE_LOCK(&dhd->pub);
#endif /* BCMPCIE */

			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
#ifdef BCMPCIE
				DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
#endif /* BCMPCIE */
				break;
			}

			if (dhd->pub.dongle_reset == FALSE) {
				DHD_TIMER(("%s:\n", __FUNCTION__));
				dhd_analyze_sock_flows(dhd, dhd_watchdog_ms);
				dhd_bus_watchdog(&dhd->pub);

#ifdef DHD_TIMESYNC
				/* Call the timesync module watchdog */
				dhd_timesync_watchdog(&dhd->pub);
#endif /* DHD_TIMESYNC */
#if defined(BCM_ROUTER_DHD) && defined(CTFPOOL)
				/* allocate and add a new skb to the pkt pool */
				if (CTF_ENAB(dhd->cih))
					osl_ctfpool_replenish(dhd->pub.osh, CTFPOOL_REFILL_THRESH);
#endif /* BCM_ROUTER_DHD && CTFPOOL */

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
#ifdef BCMPCIE
			DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
#endif /* BCMPCIE */
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

#ifdef BCMPCIE
	DHD_OS_WD_WAKE_LOCK(&dhd->pub);
#endif /* BCMPCIE */
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
#ifdef BCMPCIE
	DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
#endif /* BCMPCIE */
#if defined(BCM_ROUTER_DHD) && defined(CTFPOOL)
	    /* allocate and add a new skb to the pkt pool */
	    if (CTF_ENAB(dhd->cih))
			osl_ctfpool_replenish(dhd->pub.osh, CTFPOOL_REFILL_THRESH);
#endif /* BCM_ROUTER_DHD && CTFPOOL */
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
#if defined(PCIE_OOB) || defined(PCIE_INB_DW)
					dhd_bus_dw_deassert(&dhd->pub);
#endif /* PCIE_OOB || PCIE_INB_DW */
					if (dhd_get_rpm_state(&dhd->pub)) {
						dhd_runtimepm_state(&dhd->pub);
					}
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
	dhd_set_rpm_state(dhdp, FALSE);
	dhdpcie_runtime_bus_wake(dhdp, CAN_SLEEP(), __builtin_return_address(0));
}

void dhd_runtime_pm_enable(dhd_pub_t *dhdp)
{
	/* Enable Runtime PM except for MFG Mode */
	if (!(dhdp->op_mode & DHD_FLAG_MFG_MODE)) {
		if (dhd_get_idletime(dhdp)) {
			dhd_set_rpm_state(dhdp, TRUE);
		}
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
		if (dhd->pub.conf->dhd_dpc_prio >= 0) {
			struct sched_param param;
			printf("%s: set dhd_dpc_prio %d\n", __FUNCTION__, dhd->pub.conf->dhd_dpc_prio);
			param.sched_priority = (dhd->pub.conf->dhd_dpc_prio < MAX_RT_PRIO)?
				dhd->pub.conf->dhd_dpc_prio:(MAX_RT_PRIO-1);
			setScheduler(current, SCHED_FIFO, &param);
			dhd->pub.conf->dhd_dpc_prio = -1;
		}
		if (!binary_sema_down(tsk)) {
#ifdef ENABLE_ADAPTIVE_SCHED
			dhd_sched_policy(dhd_dpc_prio);
#endif /* ENABLE_ADAPTIVE_SCHED */
			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
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
#ifdef ENABLE_ADAPTIVE_SCHED
			dhd_sched_policy(dhd_rxf_prio);
#endif /* ENABLE_ADAPTIVE_SCHED */

			SMP_RD_BARRIER_DEPENDS();

			if (tsk->terminated) {
				DHD_OS_WAKE_UNLOCK(pub);
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
#if defined(WL_MONITOR) && defined(BCMSDIO)
				if (dhd_monitor_enabled(pub, 0))
					dhd_rx_mon_pkt_sdio(pub, skb, 0);
				else
#endif /* WL_MONITOR && BCMSDIO */
				netif_rx_ni(skb);
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

	cancel_delayed_work_sync(&dhd->dhd_dpc_dispatcher_work);
#ifdef DHD_LB
#ifdef DHD_LB_RXP
	cancel_work_sync(&dhd->rx_napi_dispatcher_work);
	__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */
#ifdef DHD_LB_TXP
	cancel_work_sync(&dhd->tx_dispatcher_work);
	skb_queue_purge(&dhd->tx_pend_queue);
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
	dhd_info_t *dhd = (dhd_info_t *)data;

	int curr_cpu = get_cpu();
	put_cpu();

	/* Store current cpu as dpc_cpu */
	atomic_set(&dhd->dpc_cpu, curr_cpu);

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

	/* Store as prev_dpc_cpu, which will be used in Rx load balancing for deciding candidacy */
	atomic_set(&dhd->prev_dpc_cpu, curr_cpu);

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
	} else {
		DHD_OS_WAKE_UNLOCK(dhdp);
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
	char iovbuf[WL_EVENTING_MASK_LEN + sizeof(wl_scb_probe_t)];
	int ret;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		return;
	}

	ret = dhd_iovar(dhd, 0, "scb_probe", NULL, 0, iovbuf, sizeof(iovbuf), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: GET max_scb_probe failed\n", __FUNCTION__));
	}

	memcpy(&scb_probe, iovbuf, sizeof(wl_scb_probe_t));

	scb_probe.scb_max_probe = NUM_SCB_MAX_PROBE;

	ret = dhd_iovar(dhd, 0, "scb_probe", (char *)&scb_probe, sizeof(wl_scb_probe_t), NULL, 0,
			TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: max_scb_probe setting failed\n", __FUNCTION__));
		return;
	}
}
#endif /* WL_CFG80211 && NUM_SCB_MAX_PROBE */

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
		bzero(&info.driver, sizeof(info.driver));
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		if (info.driver[sizeof(info.driver) - 1] != '\0') {
			DHD_ERROR(("%s: Exceeds the size of info.driver"
				"truncating last byte with null\n", __FUNCTION__));
			info.driver[sizeof(info.driver) - 1] = '\0';
		}
		strlcpy(drvname, info.driver, sizeof(drvname));

		/* clear struct for return */
		memset(&info, 0, sizeof(info));
		info.cmd = cmd;

		/* if dhd requested, identify ourselves */
		if (strcmp(drvname, "?dhd") == 0) {
			snprintf(info.driver, sizeof(info.driver), "dhd");
			strlcpy(info.version, EPI_VERSION_STR, sizeof(info.version));
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

/* XXX function to detect that FW is dead and send Event up */
static bool dhd_check_hang(struct net_device *net, dhd_pub_t *dhdp, int error)
{
#if defined(OEM_ANDROID)
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return FALSE;
	}

	if (!dhdp->up)
		return FALSE;

#if (!defined(BCMDBUS) && !defined(BCMPCIE))
	if (dhdp->info->thr_dpc_ctl.thr_pid < 0) {
		DHD_ERROR(("%s : skipped due to negative pid - unloading?\n", __FUNCTION__));
		return FALSE;
	}
#endif /* BCMDBUS */

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
				dhdp->hang_reason = dhdp->is_sched_error ?
					HANG_REASON_D3_ACK_TIMEOUT_SCHED_ERROR :
					HANG_REASON_D3_ACK_TIMEOUT;
#endif /* BCMPCIE */
			} else {
				dhdp->hang_reason = dhdp->is_sched_error ?
					HANG_REASON_IOCTL_RESP_TIMEOUT_SCHED_ERROR :
					HANG_REASON_IOCTL_RESP_TIMEOUT;
			}
		}
		printf("%s\n", info_string);
		printf("MAC %pM\n", &dhdp->mac);
		net_os_send_hang_message(net);
		return TRUE;
	}
#endif /* OEM_ANDROID */
	return FALSE;
}

#ifdef WL_MONITOR
bool
dhd_monitor_enabled(dhd_pub_t *dhd, int ifidx)
{
	return (dhd->info->monitor_type != 0);
}

#ifdef BCMSDIO
static void
dhd_rx_mon_pkt_sdio(dhd_pub_t *dhdp, void *pkt, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

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

	netif_rx_ni(dhd->monitor_skb);

	dhd->monitor_skb = NULL;
}
#elif defined(BCMPCIE)

void
dhd_rx_mon_pkt(dhd_pub_t *dhdp, host_rxbuf_cmpl_t* msg, void *pkt, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
#ifdef HOST_RADIOTAP_CONV
	if (dhd->host_radiotap_conv) {
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

		dhd->monitor_skb->protocol = eth_type_trans(dhd->monitor_skb,
			dhd->monitor_skb->dev);
	}
	else
#endif /* HOST_RADIOTAP_CONV */
	{
		uint8 amsdu_flag = (msg->flags & BCMPCIE_PKT_FLAGS_MONITOR_MASK) >>
			BCMPCIE_PKT_FLAGS_MONITOR_SHIFT;
		switch (amsdu_flag) {
			case BCMPCIE_PKT_FLAGS_MONITOR_NO_AMSDU:
			default:
				if (!dhd->monitor_skb) {
					if ((dhd->monitor_skb = PKTTONATIVE(dhdp->osh, pkt))
						== NULL)
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
					if ((dhd->monitor_skb = dev_alloc_skb(MAX_MON_PKT_SIZE))
						== NULL)
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
	}

	if (skb_headroom(dhd->monitor_skb) < ETHER_HDR_LEN) {
		struct sk_buff *skb2;

		DHD_INFO(("%s: insufficient headroom\n",
		          dhd_ifname(&dhd->pub, ifidx)));

		skb2 = skb_realloc_headroom(dhd->monitor_skb, ETHER_HDR_LEN);

		dev_kfree_skb(dhd->monitor_skb);
		if ((dhd->monitor_skb = skb2) == NULL) {
			DHD_ERROR(("%s: skb_realloc_headroom failed\n",
			           dhd_ifname(&dhd->pub, ifidx)));
			return;
		}
	}
	PKTPUSH(dhd->pub.osh, dhd->monitor_skb, ETHER_HDR_LEN);

	/* XXX WL here makes sure data is 4-byte aligned? */
	if (in_interrupt()) {
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
			__FUNCTION__, __LINE__);
		netif_rx(dhd->monitor_skb);
	} else {
		/* If the receive is not processed inside an ISR,
		 * the softirqd must be woken explicitly to service
		 * the NET_RX_SOFTIRQ.	In 2.6 kernels, this is handled
		 * by netif_rx_ni(), but in earlier kernels, we need
		 * to do it manually.
		 */
		bcm_object_trace_opr(dhd->monitor_skb, BCM_OBJDBG_REMOVE,
			__FUNCTION__, __LINE__);

		netif_rx_ni(dhd->monitor_skb);
	}

	dhd->monitor_skb = NULL;
}
#endif

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

		/* XXX Some DHD modules (e.g. cfg80211) configures operation mode based on firmware
		 * name. This is indeed a hack but we have to make it work properly before we have
		 * a better solution
		 */
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

void dhd_bus_retry_hang_recovery(wlan_bt_handle_t handle)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;
	dhd_info_t *dhd = (dhd_info_t*)dhdp->info;

	dhdp->hang_was_sent = 0;

	dhd_os_send_hang_message(&dhd->pub);
}
EXPORT_SYMBOL(dhd_bus_retry_hang_recovery);

#endif /* BT_OVER_SDIO */

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
dhd_add_monitor_if(dhd_info_t *dhd)
{
	struct net_device *dev;
	char *devname;
#ifdef HOST_RADIOTAP_CONV
	dhd_pub_t *dhdp = (dhd_pub_t *)&dhd->pub;
#endif /* HOST_RADIOTAP_CONV */
	uint32 scan_suppress = FALSE;
	int ret = BCME_OK;

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (dhd->monitor_dev) {
		DHD_ERROR(("%s: monitor i/f already exists", __FUNCTION__));
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

	dev->netdev_ops = &netdev_monitor_ops;

	/* XXX: This is called from IOCTL path, in this case, rtnl_lock is already taken.
	 * So, register_netdev() shouldn't be called. It leads to deadlock.
	 * To avoid deadlock due to rtnl_lock(), register_netdevice() should be used.
	 */
	if (register_netdevice(dev)) {
		DHD_ERROR(("%s, register_netdev failed for %s\n",
			__FUNCTION__, dev->name));
		free_netdev(dev);
		return;
	}

	if (FW_SUPPORTED((&dhd->pub), monitor)) {
#ifdef DHD_PCIE_RUNTIMEPM
		/* Disable RuntimePM in monitor mode */
		DHD_DISABLE_RUNTIME_PM(&dhd->pub);
		DHD_ERROR(("%s : disable runtime PM in monitor mode\n", __FUNCTION__));
#endif /* DHD_PCIE_RUNTIME_PM */
		scan_suppress = TRUE;
		/* Set the SCAN SUPPRESS Flag in the firmware to disable scan in Monitor mode */
		ret = dhd_iovar(&dhd->pub, 0, "scansuppress", (char *)&scan_suppress,
			sizeof(scan_suppress), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: scansuppress set failed, ret=%d\n", __FUNCTION__, ret));
		}
	}

#ifdef HOST_RADIOTAP_CONV
	bcmwifi_monitor_create(&dhd->monitor_info);
	bcmwifi_set_corerev_major(dhd->monitor_info, dhdpcie_get_corerev_major(dhdp));
	bcmwifi_set_corerev_minor(dhd->monitor_info, dhdpcie_get_corerev_minor(dhdp));
#endif /* HOST_RADIOTAP_CONV */
	dhd->monitor_dev = dev;
}

static void
dhd_del_monitor_if(dhd_info_t *dhd)
{
	int ret = BCME_OK;
	uint32 scan_suppress = FALSE;

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (!dhd->monitor_dev) {
		DHD_ERROR(("%s: monitor i/f doesn't exist\n", __FUNCTION__));
		return;
	}

	if (FW_SUPPORTED((&dhd->pub), monitor)) {
#ifdef DHD_PCIE_RUNTIMEPM
		/* Enable RuntimePM */
		DHD_ENABLE_RUNTIME_PM(&dhd->pub);
		DHD_ERROR(("%s : enabled runtime PM\n", __FUNCTION__));
#endif /* DHD_PCIE_RUNTIME_PM */
		scan_suppress = FALSE;
		/* Unset the SCAN SUPPRESS Flag in the firmware to enable scan */
		ret = dhd_iovar(&dhd->pub, 0, "scansuppress", (char *)&scan_suppress,
				sizeof(scan_suppress), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: scansuppress set failed, ret=%d\n", __FUNCTION__, ret));
		}
	}

	if (dhd->monitor_dev) {
		if (dhd->monitor_dev->reg_state == NETREG_UNINITIALIZED) {
			free_netdev(dhd->monitor_dev);
		} else {
			if (rtnl_is_locked()) {
				unregister_netdevice(dhd->monitor_dev);
			} else {
				unregister_netdev(dhd->monitor_dev);
			}
		}
		dhd->monitor_dev = NULL;
	}
#ifdef HOST_RADIOTAP_CONV
	if (dhd->monitor_info) {
		bcmwifi_monitor_delete(dhd->monitor_info);
		dhd->monitor_info = NULL;
	}
#endif /* HOST_RADIOTAP_CONV */
}

void
dhd_set_monitor(dhd_pub_t *pub, int ifidx, int val)
{
	dhd_info_t *dhd = pub->info;

	DHD_TRACE(("%s: val %d\n", __FUNCTION__, val));

	dhd_net_if_lock_local(dhd);
	if (!val) {
			/* Delete monitor */
			dhd_del_monitor_if(dhd);
	} else {
			/* Add monitor */
			dhd_add_monitor_if(dhd);
	}
	dhd->monitor_type = val;
	dhd_net_if_unlock_local(dhd);
}
#endif /* WL_MONITOR */

#if defined(DHD_H2D_LOG_TIME_SYNC)
/*
 * Helper function:
 * Used for RTE console message time syncing with Host printk
 */
void dhd_h2d_log_time_sync_deferred_wq_schedule(dhd_pub_t *dhdp)
{
	dhd_info_t *info = dhdp->info;

	/* Ideally the "state" should be always TRUE */
	dhd_deferred_schedule_work(info->dhd_deferred_wq, NULL,
			DHD_WQ_WORK_H2D_CONSOLE_TIME_STAMP_MATCH,
			dhd_deferred_work_rte_log_time_sync,
			DHD_WQ_WORK_PRIORITY_LOW);
}

void
dhd_deferred_work_rte_log_time_sync(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd_info = handle;
	dhd_pub_t *dhd;

	if (event != DHD_WQ_WORK_H2D_CONSOLE_TIME_STAMP_MATCH) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd_info) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dhd = &dhd_info->pub;

	/*
	 * Function to send IOVAR for console timesyncing
	 * between Host and Dongle.
	 * If the IOVAR fails,
	 * 1. dhd_rte_time_sync_ms is set to 0 and
	 * 2. HOST Dongle console time sync will *not* happen.
	 */
	dhd_h2d_log_time_sync(dhd);
}
#endif /* DHD_H2D_LOG_TIME_SYNC */

int dhd_ioctl_process(dhd_pub_t *pub, int ifidx, dhd_ioctl_t *ioc, void *data_buf)
{
	int bcmerror = BCME_OK;
	int buflen = 0;
	struct net_device *net;

	net = dhd_idx2net(pub, ifidx);
	if (!net) {
		bcmerror = BCME_BADARG;
		/*
		 * The netdev pointer is bad means the DHD can't communicate
		 * to higher layers, so just return from here
		 */
		return bcmerror;
	}

	/* check for local dhd ioctl and handle it */
	if (ioc->driver == DHD_IOCTL_MAGIC) {
		if (data_buf) {
			/* Return error if nvram size is too big */
			if (!bcmstricmp((char *)data_buf, "vars")) {
				DHD_ERROR(("%s: nvram len(%d) MAX_NVRAMBUF_SIZE(%d)\n",
					__FUNCTION__, ioc->len, MAX_NVRAMBUF_SIZE));
				if (ioc->len > MAX_NVRAMBUF_SIZE) {
					DHD_ERROR(("%s: nvram len(%d) > MAX_NVRAMBUF_SIZE(%d)\n",
						__FUNCTION__, ioc->len, MAX_NVRAMBUF_SIZE));
					bcmerror = BCME_BUFTOOLONG;
					goto done;
				}
				buflen = ioc->len;
			} else if (!bcmstricmp((char *)data_buf, "dump")) {
				buflen = MIN(ioc->len, DHD_IOCTL_MAXLEN_32K);
			} else {
				/* This is a DHD IOVAR, truncate buflen to DHD_IOCTL_MAXLEN */
				buflen = MIN(ioc->len, DHD_IOCTL_MAXLEN);
			}
		}
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

	if ((ioc->cmd == WLC_SET_VAR || ioc->cmd == WLC_GET_VAR) &&
		data_buf != NULL && strncmp("rpc_", data_buf, 4) == 0) {
		bcmerror = BCME_UNSUPPORTED;
		goto done;
	}

	/* XXX this typecast is BAD !!! */
	bcmerror = dhd_wl_ioctl(pub, ifidx, (wl_ioctl_t *)ioc, data_buf, buflen);

#ifdef REPORT_FATAL_TIMEOUTS
	/* ensure that the timeouts/flags are started/set after the ioctl returns success */
	if (bcmerror == BCME_OK) {
		if (ioc->cmd == WLC_SET_WPA_AUTH) {
			int wpa_auth;

			wpa_auth = *((int *)ioc->buf);
			DHD_INFO(("wpa_auth:%d\n", wpa_auth));
			if (wpa_auth != WPA_AUTH_DISABLED) {
				/* If AP is with security then enable
				* WLC_E_PSK_SUP event checking
				*/
				pub->secure_join = TRUE;
			} else {
				/* If AP is with open then disable
				* WLC_E_PSK_SUP event checking
				*/
				pub->secure_join = FALSE;
			}
		}

		if (ioc->cmd == WLC_SET_AUTH) {
			int auth;
			auth = *((int *)ioc->buf);
			DHD_INFO(("Auth:%d\n", auth));

			if (auth != WL_AUTH_OPEN_SYSTEM) {
				/* If AP is with security then enable
				* WLC_E_PSK_SUP event checking
				*/
				pub->secure_join = TRUE;
			} else {
				/* If AP is with open then disable WLC_E_PSK_SUP event checking */
				pub->secure_join = FALSE;
			}
		}

		if (ioc->cmd == WLC_SET_SSID) {
			bool set_ssid_rcvd = OSL_ATOMIC_READ(pub->osh, &pub->set_ssid_rcvd);
			if ((!set_ssid_rcvd) && (!pub->secure_join)) {
				dhd_start_join_timer(pub);
			} else {
				DHD_ERROR(("%s: didnot start join timer."
					"open join, set_ssid_rcvd: %d secure_join: %d\n",
					__FUNCTION__, set_ssid_rcvd, pub->secure_join));
				OSL_ATOMIC_SET(pub->osh, &pub->set_ssid_rcvd, FALSE);
			}
		}

		if (ioc->cmd == WLC_SCAN) {
			dhd_start_scan_timer(pub, 0);
		}
	}
#endif  /* REPORT_FATAL_TIMEOUTS */

done:
#if defined(OEM_ANDROID)
	dhd_check_hang(net, pub, bcmerror);
#endif /* OEM_ANDROID */

	return bcmerror;
}

#ifdef WL_NANHO
static bool
dhd_nho_iovar_filter(dhd_ioctl_t *ioc)
{
	bool forward_to_nanho = FALSE;

	if ((ioc->cmd == WLC_SET_VAR) || (ioc->cmd == WLC_GET_VAR)) {
		if ((ioc->len >= sizeof("nan")) && !strcmp(ioc->buf, "nan")) {
			/* forward nan iovar to nanho module */
			forward_to_nanho = TRUE;
		} else if ((ioc->len >= sizeof("slot_bss")) && !strcmp(ioc->buf, "slot_bss")) {
			/* forward slot_bss iovar to nanho module */
			forward_to_nanho = TRUE;
		}
	}
	return forward_to_nanho;
}

static int
dhd_nho_ioctl_process(dhd_pub_t *pub, int ifidx, dhd_ioctl_t *ioc, void *data_buf)
{
	int err;

	if (dhd_nho_iovar_filter(ioc)) {
		/* forward iovar to nanho module */
		err = bcm_nanho_iov(pub->nanhoi, ifidx, (wl_ioctl_t *)ioc);
	} else {
		/* all other iovars bypass nanho and issued through normal path */
		err = dhd_ioctl_process(pub, ifidx, ioc, data_buf);
	}
	return err;
}

static int
dhd_nho_ioctl_cb(void *drv_ctx, int ifidx, wl_ioctl_t *ioc, bool drv_lock)
{
	int err;

	if (drv_lock) {
		DHD_OS_WAKE_LOCK((dhd_pub_t *)drv_ctx);
	}

	err = dhd_ioctl_process((dhd_pub_t *)drv_ctx, ifidx, (dhd_ioctl_t *)ioc, ioc->buf);

	if (drv_lock) {
		DHD_OS_WAKE_UNLOCK((dhd_pub_t *)drv_ctx);
	}

	return err;
}
#endif /* WL_NANHO */

/* XXX For the moment, local ioctls will return BCM errors */
/* XXX Others return linux codes, need to be changed... */
/**
 * Called by the OS (optionally via a wrapper function).
 * @param net  Linux per dongle instance
 * @param ifr  Linux request structure
 * @param cmd  e.g. SIOCETHTOOL
 */
static int
dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	void __user *data,
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */
	int cmd)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int ifidx;
	int ret;
	void *local_buf = NULL;           /**< buffer in kernel space */
	void __user *ioc_buf_user = NULL; /**< buffer in user space */
	u16 buflen = 0;

	if (atomic_read(&exit_in_progress)) {
		DHD_ERROR(("%s module exit in progress\n", __func__));
		bcmerror = BCME_DONGLE_DOWN;
		return OSL_ERROR(bcmerror);
	}

	DHD_OS_WAKE_LOCK(&dhd->pub);

#if defined(OEM_ANDROID)
	/* Interface up check for built-in type */
	if (!dhd_download_fw_on_driverload && dhd->pub.up == FALSE) {
		DHD_ERROR(("%s: Interface is down \n", __FUNCTION__));
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return OSL_ERROR(BCME_NOTUP);
	}
#endif /* (OEM_ANDROID) */

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __FUNCTION__, ifidx, cmd));

#if defined(WL_STATIC_IF)
	/* skip for static ndev when it is down */
	if (dhd_is_static_ndev(&dhd->pub, net) && !(net->flags & IFF_UP)) {
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -1;
	}
#endif /* WL_STATIC_iF */

	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD IF\n", __FUNCTION__));
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -1;
	}

#if defined(WL_WIRELESS_EXT)
	/* linux wireless extensions */
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		/* may recurse, do NOT lock */
		ret = wl_iw_ioctl(net, ifr, cmd);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif /* defined(WL_WIRELESS_EXT) */

	if (cmd == SIOCETHTOOL) {
		ret = dhd_ethtool(dhd, (void*)ifr->ifr_data);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

#if defined(OEM_ANDROID)
	if (cmd == SIOCDEVPRIVATE+1) {
		ret = wl_android_priv_cmd(net, ifr);
		dhd_check_hang(net, &dhd->pub, ret);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

#endif /* OEM_ANDROID */

	if (cmd != SIOCDEVPRIVATE) {
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -EOPNOTSUPP;
	}

	memset(&ioc, 0, sizeof(ioc));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	/* Copy the ioc control structure part of ioctl request */
	if (copy_from_user(&ioc, data, sizeof(wl_ioctl_t))) {
		bcmerror = BCME_BADADDR;
		goto done;
	}
	/* To differentiate between wl and dhd read 4 more byes */
	if ((copy_from_user(&ioc.driver, (char *)data + sizeof(wl_ioctl_t),
			sizeof(uint)) != 0)) {
		bcmerror = BCME_BADADDR;
		goto done;
	}
#else
#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
	if (in_compat_syscall())
#else
	if (is_compat_task())
#endif /* LINUX_VER >= 4.6 */
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
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */

	if (!capable(CAP_NET_ADMIN)) {
		bcmerror = BCME_EPERM;
		goto done;
	}

	/* Take backup of ioc.buf and restore later */
	ioc_buf_user = ioc.buf;

	if (ioc.len > 0) {
		/*
		* some IOVARs in DHD require 32K user memory. So allocate the
		* maximum local buffer.
		*
		* For IOVARS which donot require 32K user memory, dhd_ioctl_process()
		* takes care of trimming the length to DHD_IOCTL_MAXLEN(16K). So that DHD
		* will not overflow the buffer size while updating the buffer.
		*/
		buflen = MIN(ioc.len, DHD_IOCTL_MAXLEN_32K);
		if (!(local_buf = MALLOC(dhd->pub.osh, buflen+1))) {
			bcmerror = BCME_NOMEM;
			goto done;
		}

		if (copy_from_user(local_buf, ioc.buf, buflen)) {
			bcmerror = BCME_BADADDR;
			goto done;
		}

		*((char *)local_buf + buflen) = '\0';

		/* For some platforms accessing userspace memory
		 * of ioc.buf is causing kernel panic, so to avoid that
		 * make ioc.buf pointing to kernel space memory local_buf
		 */
		ioc.buf = local_buf;
	}

#if defined(OEM_ANDROID)
	/* Skip all the non DHD iovars (wl iovars) after f/w hang */
	if (ioc.driver != DHD_IOCTL_MAGIC && dhd->pub.hang_was_sent) {
		DHD_TRACE(("%s: HANG was sent up earlier\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(&dhd->pub, DHD_EVENT_TIMEOUT_MS);
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}
#endif /* OEM_ANDROID */

#ifdef WL_NANHO
	bcmerror = dhd_nho_ioctl_process(&dhd->pub, ifidx, &ioc, local_buf);
#else
	bcmerror = dhd_ioctl_process(&dhd->pub, ifidx, &ioc, local_buf);
#endif  /* WL_NANHO */

	/* Restore back userspace pointer to ioc.buf */
	ioc.buf = ioc_buf_user;
	if (!bcmerror && buflen && local_buf && ioc.buf) {
		if (copy_to_user(ioc.buf, local_buf, buflen))
			bcmerror = -EFAULT;
	}

done:
	if (local_buf)
		MFREE(dhd->pub.osh, local_buf, buflen+1);

	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return OSL_ERROR(bcmerror);
}

#if defined(WL_CFG80211) && defined(SUPPORT_DEEP_SLEEP)
/* Flags to indicate if we distingish power off policy when
 * user set the memu "Keep Wi-Fi on during sleep" to "Never"
 */
int trigger_deep_sleep = 0;
#endif /* WL_CFG80211 && SUPPORT_DEEP_SLEEP */

#ifdef FIX_CPU_MIN_CLOCK
static int dhd_init_cpufreq_fix(dhd_info_t *dhd)
{
	if (dhd) {
		mutex_init(&dhd->cpufreq_fix);
		dhd->cpufreq_fix_status = FALSE;
	}
	return 0;
}

static void dhd_fix_cpu_freq(dhd_info_t *dhd)
{
	mutex_lock(&dhd->cpufreq_fix);
	if (dhd && !dhd->cpufreq_fix_status) {
		pm_qos_add_request(&dhd->dhd_cpu_qos, PM_QOS_CPU_FREQ_MIN, 300000);
#ifdef FIX_BUS_MIN_CLOCK
		pm_qos_add_request(&dhd->dhd_bus_qos, PM_QOS_BUS_THROUGHPUT, 400000);
#endif /* FIX_BUS_MIN_CLOCK */
		DHD_ERROR(("pm_qos_add_requests called\n"));

		dhd->cpufreq_fix_status = TRUE;
	}
	mutex_unlock(&dhd->cpufreq_fix);
}

static void dhd_rollback_cpu_freq(dhd_info_t *dhd)
{
	mutex_lock(&dhd ->cpufreq_fix);
	if (dhd && dhd->cpufreq_fix_status != TRUE) {
		mutex_unlock(&dhd->cpufreq_fix);
		return;
	}

	pm_qos_remove_request(&dhd->dhd_cpu_qos);
#ifdef FIX_BUS_MIN_CLOCK
	pm_qos_remove_request(&dhd->dhd_bus_qos);
#endif /* FIX_BUS_MIN_CLOCK */
	DHD_ERROR(("pm_qos_add_requests called\n"));

	dhd->cpufreq_fix_status = FALSE;
	mutex_unlock(&dhd->cpufreq_fix);
}
#endif /* FIX_CPU_MIN_CLOCK */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int
dhd_ioctl_entry_wrapper(struct net_device *net, struct ifreq *ifr,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	void __user *data, 
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */
	int cmd)
{
	int error;
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	if (atomic_read(&dhd->pub.block_bus))
		return -EHOSTDOWN;

	if (pm_runtime_get_sync(dhd_bus_to_dev(dhd->pub.bus)) < 0)
		return BCME_ERROR;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	error = dhd_ioctl_entry(net, ifr, data, cmd);
#else
	error = dhd_ioctl_entry(net, ifr, cmd);
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */

	pm_runtime_mark_last_busy(dhd_bus_to_dev(dhd->pub.bus));
	pm_runtime_put_autosuspend(dhd_bus_to_dev(dhd->pub.bus));

	return error;
}
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#ifdef CONFIG_HAS_WAKELOCK
#define dhd_wake_lock_unlock_destroy(wlock) \
{ \
	if (dhd_wake_lock_active(wlock)) { \
		dhd_wake_unlock(wlock); \
	} \
	dhd_wake_lock_destroy(wlock); \
}
#endif /* CONFIG_HAS_WAKELOCK */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) && defined(DHD_TCP_LIMIT_OUTPUT)
#define DHD_TCP_LIMIT_OUTPUT_BYTES (4 * 1024 * 1024)
#ifndef TCP_DEFAULT_LIMIT_OUTPUT
#define TCP_DEFAULT_LIMIT_OUTPUT (256 * 1024)
#endif /* TSQ_DEFAULT_LIMIT_OUTPUT */
void
dhd_ctrl_tcp_limit_output_bytes(int level)
{
	if (level == 0) {
		init_net.ipv4.sysctl_tcp_limit_output_bytes = TCP_DEFAULT_LIMIT_OUTPUT;
	} else if (level == 1) {
		init_net.ipv4.sysctl_tcp_limit_output_bytes = DHD_TCP_LIMIT_OUTPUT_BYTES;
	}
}
#endif /* LINUX_VERSION_CODE > 4.19.0 && DHD_TCP_LIMIT_OUTPUT */

static int
dhd_stop(struct net_device *net)
{
	int ifidx = 0;
	bool skip_reset = false;
#ifdef WL_CFG80211
	unsigned long flags = 0;
#ifdef WL_STATIC_IF
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
#endif /* WL_STATIC_IF */
#endif /* WL_CFG80211 */
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	DHD_OS_WAKE_LOCK(&dhd->pub);
	WL_MSG(net->name, "Enter\n");
	dhd->pub.rxcnt_timeout = 0;
	dhd->pub.txcnt_timeout = 0;

#ifdef BCMPCIE
	dhd->pub.d3ackcnt_timeout = 0;
#endif /* BCMPCIE */

	mutex_lock(&dhd->pub.ndev_op_sync);
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

#if defined(WLAN_ACCEL_BOOT)
	if (!dhd->wl_accel_force_reg_on && dhd_query_bus_erros(&dhd->pub)) {
		DHD_ERROR(("%s: set force reg on\n", __FUNCTION__));
		dhd->wl_accel_force_reg_on = TRUE;
	}
#endif /* WLAN_ACCEL_BOOT */

#ifdef FIX_CPU_MIN_CLOCK
	if (dhd_get_fw_mode(dhd) == DHD_FLAG_HOSTAP_MODE)
		dhd_rollback_cpu_freq(dhd);
#endif /* FIX_CPU_MIN_CLOCK */

	ifidx = dhd_net2idx(dhd, net);
	BCM_REFERENCE(ifidx);

	DHD_ERROR(("%s: ######### called for ifidx=%d #########\n", __FUNCTION__, ifidx));

#if defined(WL_STATIC_IF) && defined(WL_CFG80211)
	/* If static if is operational, don't reset the chip */
	if (wl_cfg80211_static_if_active(cfg)) {
		WL_MSG(net->name, "static if operational. skip chip reset.\n");
		skip_reset = true;
		wl_cfg80211_sta_ifdown(net);
		goto exit;
	}
#endif /* WL_STATIC_IF && WL_CFG80211 */
#ifdef DHD_NOTIFY_MAC_CHANGED
	if (dhd->pub.skip_dhd_stop) {
		WL_MSG(net->name, "skip chip reset.\n");
		skip_reset = true;
#if defined(WL_CFG80211)
		wl_cfg80211_sta_ifdown(net);
#endif /* WL_CFG80211 */
		goto exit;
	}
#endif /* DHD_NOTIFY_MAC_CHANGED */

#ifdef WL_CFG80211
	if (ifidx == 0) {
		dhd_if_t *ifp;
		wl_cfg80211_down(net);

		DHD_ERROR(("%s: making dhdpub up FALSE\n", __FUNCTION__));
#ifdef WL_CFG80211
		/* Disable Runtime PM before interface down */
		DHD_STOP_RPM_TIMER(&dhd->pub);

		DHD_UP_LOCK(&dhd->pub.up_lock, flags);
		dhd->pub.up = 0;
		DHD_UP_UNLOCK(&dhd->pub.up_lock, flags);
#else
		dhd->pub.up = 0;
#endif /* WL_CFG80211 */
#if defined(BCMPCIE) && defined(CONFIG_ARCH_MSM)
		dhd_bus_inform_ep_loaded_to_rc(&dhd->pub, dhd->pub.up);
#endif /* BCMPCIE && CONFIG_ARCH_MSM */

		ifp = dhd->iflist[0];
		/*
		 * For CFG80211: Clean up all the left over virtual interfaces
		 * when the primary Interface is brought down. [ifconfig wlan0 down]
		 */
		if (!dhd_download_fw_on_driverload) {
			DHD_STATLOG_CTRL(&dhd->pub, ST(WLAN_POWER_OFF), ifidx, 0);
			if ((dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) &&
				(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)) {
				int i;
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
				dhd_cleanup_m4_state_work(&dhd->pub, ifidx);
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */
#ifdef DHD_PKTDUMP_ROAM
				dhd_dump_pkt_clear(&dhd->pub);
#endif /* DHD_PKTDUMP_ROAM */

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
			/* Wait till event logs work/kthread finishes */
			dhd_cancel_logtrace_process_sync(dhd);
#endif /* SHOW_LOGTRACE */

#ifdef BTLOG
			/* Wait till bt_log_dispatcher_work finishes */
			cancel_work_sync(&dhd->bt_log_dispatcher_work);
#endif /* BTLOG */

#ifdef EWP_EDL
			cancel_delayed_work_sync(&dhd->edl_dispatcher_work);
#endif

#if defined(DHD_LB_RXP)
			__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
			skb_queue_purge(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */
		}
#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif /* DHDTCPACK_SUPPRESS */
#if defined(DHD_LB_RXP)
		if (ifp && ifp->net == dhd->rx_napi_netdev) {
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
			}
			if (dhd->event_data.raw_fmts) {
				MFREE(dhd->pub.osh, dhd->event_data.raw_fmts,
					dhd->event_data.raw_fmts_size);
			}
			if (dhd->event_data.raw_sstr) {
				MFREE(dhd->pub.osh, dhd->event_data.raw_sstr,
					dhd->event_data.raw_sstr_size);
			}
			if (dhd->event_data.rom_raw_sstr) {
				MFREE(dhd->pub.osh, dhd->event_data.rom_raw_sstr,
					dhd->event_data.rom_raw_sstr_size);
			}
			dhd->dhd_state &= ~DHD_ATTACH_LOGTRACE_INIT;
		}
	}
#endif /* SHOW_LOGTRACE */
#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
	/* Stop all ring buffer */
	dhd_os_reset_logging(&dhd->pub);
#endif
#ifdef APF
	dhd_dev_apf_delete_filter(net);
#endif /* APF */

	/* Stop the protocol module */
	dhd_prot_stop(&dhd->pub);

	OLD_MOD_DEC_USE_COUNT;
exit:
	if (skip_reset == false) {
#ifdef WL_ESCAN
		if (ifidx == 0) {
			wl_escan_down(net);
		}
#endif /* WL_ESCAN */
		if (ifidx == 0 && !dhd_download_fw_on_driverload) {
#if defined(WLAN_ACCEL_BOOT)
			wl_android_wifi_accel_off(net, dhd->wl_accel_force_reg_on);
#else
#if defined (BT_OVER_SDIO)
			dhd_bus_put(&dhd->pub, WLAN_MODULE);
			wl_android_set_wifi_on_flag(FALSE);
#else
			wl_android_wifi_off(net, TRUE);
#ifdef WL_EXT_IAPSTA
			wl_ext_iapsta_dettach_netdev(net, ifidx);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
			wl_escan_event_dettach(net, ifidx);
#endif /* WL_ESCAN */
#ifdef WL_EVENT
			wl_ext_event_dettach_netdev(net, ifidx);
#endif /* WL_EVENT */
#endif /* BT_OVER_SDIO */
#endif /* WLAN_ACCEL_BOOT */
		}
#ifdef SUPPORT_DEEP_SLEEP
		else {
			/* CSP#505233: Flags to indicate if we distingish
			 * power off policy when user set the memu
			 * "Keep Wi-Fi on during sleep" to "Never"
			 */
			if (trigger_deep_sleep) {
				dhd_deepsleep(net, 1);
				trigger_deep_sleep = 0;
			}
		}
#endif /* SUPPORT_DEEP_SLEEP */
		dhd->pub.hang_was_sent = 0;
		dhd->pub.hang_was_pending = 0;

		/* Clear country spec for for built-in type driver */
		if (!dhd_download_fw_on_driverload) {
			dhd->pub.dhd_cspec.country_abbrev[0] = 0x00;
			dhd->pub.dhd_cspec.rev = 0;
			dhd->pub.dhd_cspec.ccode[0] = 0x00;
		}

#ifdef BCMDBGFS
		dhd_dbgfs_remove();
#endif
	}

	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	/* Destroy wakelock */
	if (!dhd_download_fw_on_driverload &&
		(dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) &&
		(skip_reset == false)) {
		DHD_OS_WAKE_LOCK_DESTROY(dhd);
		dhd->dhd_state &= ~DHD_ATTACH_STATE_WAKELOCKS_INIT;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) && defined(DHD_TCP_LIMIT_OUTPUT)
	dhd_ctrl_tcp_limit_output_bytes(0);
#endif /* LINUX_VERSION_CODE > 4.19.0 && DHD_TCP_LIMIT_OUTPUT */
	WL_MSG(net->name, "Exit\n");

	mutex_unlock(&dhd->pub.ndev_op_sync);
	return 0;
}

#if defined(OEM_ANDROID) && defined(WL_CFG80211) && (defined(USE_INITIAL_2G_SCAN) || \
	defined(USE_INITIAL_SHORT_DWELL_TIME))
extern bool g_first_broadcast_scan;
#endif /* OEM_ANDROID && WL_CFG80211 && (USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME) */

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

#if defined(WLAN_ACCEL_BOOT)
void
dhd_verify_firmware_mode_change(dhd_info_t *dhd)
{
	int current_mode = 0;

	/*
	 * check for the FW change
	 * previous FW mode - dhd->pub.op_mode remember the previous mode
	 * current mode - update fw/nv path, get current FW  mode from dhd->fw_path
	 */
	dhd_update_fw_nv_path(dhd);
#ifdef WL_MONITOR
	DHD_INFO(("%s : check monitor mode with fw_path : %s\n", __FUNCTION__, dhd->fw_path));

	if (strstr(dhd->fw_path, "_mon") != NULL) {
		DHD_ERROR(("%s : monitor mode is enabled, set force reg on", __FUNCTION__));
		dhd->wl_accel_force_reg_on = TRUE;
		return;
	} else if (dhd->pub.monitor_enable == TRUE) {
		DHD_ERROR(("%s : monitor was enabled, changed to other fw_mode", __FUNCTION__));
		dhd->wl_accel_force_reg_on = TRUE;
		return;
	}
#endif /* WL_MONITOR */
	current_mode = dhd_get_fw_mode(dhd);

	DHD_ERROR(("%s: current_mode 0x%x, prev_opmode 0x%x", __FUNCTION__,
		current_mode, dhd->pub.op_mode));

	if (!(dhd->pub.op_mode & current_mode)) {
		DHD_ERROR(("%s: firmware path has changed, set force reg on", __FUNCTION__));
		dhd->wl_accel_force_reg_on = TRUE;
	}
}

#ifndef DHD_FS_CHECK_RETRY_DELAY_MS
#define DHD_FS_CHECK_RETRY_DELAY_MS 3000
#endif

#ifndef DHD_FS_CHECK_RETRIES
#define DHD_FS_CHECK_RETRIES    3
#endif

static bool
dhd_check_filesystem_is_up(void)
{
	struct file *fp;
	const char *clm = VENDOR_PATH CONFIG_BCMDHD_CLM_PATH;
	fp = filp_open(clm, O_RDONLY, 0);

	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: filp_open(%s) failed(%d) schedule wl_accel_work\n",
				__FUNCTION__, clm, (int)IS_ERR(fp)));
		return FALSE;
	}
	filp_close(fp, NULL);

	return TRUE;
}

static void
dhd_wifi_accel_on_work_cb(struct work_struct *work)
{
	int ret = 0;
	struct delayed_work *dw = to_delayed_work(work);
	struct dhd_info *dhd;
	struct net_device *net;

	/* Ignore compiler warnings due to -Werror=cast-qual */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	dhd = container_of(dw, struct dhd_info, wl_accel_work);
	GCC_DIAGNOSTIC_POP();

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* Initialise force regon to TRUE and it will be made FALSE at the end */
	dhd->wl_accel_force_reg_on = TRUE;

	if (!dhd_check_filesystem_is_up()) {
		if (!dhd->fs_check_retry--) {
			DHD_ERROR(("%s: max retry reached, BACKOFF\n", __FUNCTION__));
			return;
		}
		schedule_delayed_work(&dhd->wl_accel_work,
				msecs_to_jiffies(DHD_FS_CHECK_RETRY_DELAY_MS));
		return;
	}

	net = dhd->iflist[0]->net;

	/*
	 * Keep wlan turn on and download firmware during bootup
	 * by making g_wifi_on = FALSE
	 */
	ret = wl_android_wifi_on(net);
	if (ret) {
		DHD_ERROR(("%s: wl_android_wifi_on failed(%d)\n", __FUNCTION__, ret));
		goto fail;
	}

	/* Disable host access from dongle */
	ret = dhd_wl_ioctl_set_intiovar(&dhd->pub, "bus:host_access", 0, WLC_SET_VAR, TRUE, 0);
	if (ret) {
		/* Proceed even if iovar fails for backward compatibilty */
		DHD_ERROR(("%s: bus:host_access(0) failed(%d)\n", __FUNCTION__, ret));
	}

	/* After bootup keep in suspend state */
	ret = dhd_net_bus_suspend(net);
	if (ret) {
		DHD_ERROR(("%s: dhd_net_bus_suspend failed(%d)\n", __FUNCTION__, ret));
		goto fail;
	}

	/* Set force regon to FALSE and it will be set for Big Hammer case */
	dhd->wl_accel_force_reg_on = FALSE;

fail:
	/* mark wl_accel_boot_on_done for dhd_open to proceed */
	dhd->wl_accel_boot_on_done = TRUE;
	return;

}
#endif /* WLAN_ACCEL_BOOT */

int
dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
#ifdef TOE
	uint32 toe_ol;
#endif
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
			return -1;
		}
	}
#endif /* PREVENT_REOPEN_DURING_HANG */

	mutex_lock(&dhd->pub.ndev_op_sync);

#ifdef SCAN_SUPPRESS
	wl_ext_reset_scan_busy(&dhd->pub);
#endif

	if (dhd->pub.up == 1) {
		/* already up */
		WL_MSG(net->name, "Primary net_device is already up\n");
		mutex_unlock(&dhd->pub.ndev_op_sync);
		return BCME_OK;
	}

	if (!dhd_download_fw_on_driverload) {
#if defined(WLAN_ACCEL_BOOT)
		if (dhd->wl_accel_boot_on_done == FALSE) {
#if defined(WLAN_ACCEL_SKIP_WQ_IN_ATTACH)
			dhd_wifi_accel_on_work_cb(&dhd->wl_accel_work.work);
#else
			DHD_ERROR(("%s: WLAN accel boot not done yet\n", __FUNCTION__));
			mutex_unlock(&dhd->pub.ndev_op_sync);
			return -1;
#endif /* WLAN_ACCEL_SKIP_WQ_IN_ATTACH */
		}
		if (!dhd->wl_accel_force_reg_on && dhd_query_bus_erros(&dhd->pub)) {
			DHD_ERROR(("%s: set force reg on\n", __FUNCTION__));
			dhd->wl_accel_force_reg_on = TRUE;
		}
#endif /* WLAN_ACCEL_BOOT */
		if (!dhd_driver_init_done) {
			DHD_ERROR(("%s: WLAN driver is not initialized\n", __FUNCTION__));
			mutex_unlock(&dhd->pub.ndev_op_sync);
			return -1;
		}
	}

	WL_MSG(net->name, "Enter\n");
	DHD_ERROR(("%s\n", dhd_version));
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

	DHD_OS_WAKE_LOCK(&dhd->pub);
	dhd->pub.dongle_trap_occured = 0;
#ifdef BT_OVER_PCIE
	dhd->pub.dongle_trap_due_to_bt = 0;
#endif /* BT_OVER_PCIE */
	dhd->pub.hang_was_sent = 0;
	dhd->pub.hang_was_pending = 0;
	dhd->pub.hang_reason = 0;
	dhd->pub.iovar_timeout_occured = 0;
#ifdef PCIE_FULL_DONGLE
	dhd->pub.d3ack_timeout_occured = 0;
	dhd->pub.livelock_occured = 0;
	dhd->pub.pktid_audit_failed = 0;
#endif /* PCIE_FULL_DONGLE */
	dhd->pub.iface_op_failed = 0;
	dhd->pub.scan_timeout_occurred = 0;
	dhd->pub.scan_busy_occurred = 0;
	dhd->pub.smmu_fault_occurred = 0;
#ifdef DHD_LOSSLESS_ROAMING
	dhd->pub.dequeue_prec_map = ALLPRIO;
#endif
#ifdef DHD_GRO_ENABLE_HOST_CTRL
	dhd->pub.permitted_gro = TRUE;
#endif /* DHD_GRO_ENABLE_HOST_CTRL */
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

#endif /* defined(OEM_ANDROID) && !defined(WL_CFG80211) */

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

	DHD_ERROR(("%s: ######### called for ifidx=%d #########\n", __FUNCTION__, ifidx));

#if defined(WLAN_ACCEL_BOOT)
	dhd_verify_firmware_mode_change(dhd);
#endif /* WLAN_ACCEL_BOOT */

	if (ifidx == 0) {
		atomic_set(&dhd->pend_8021x_cnt, 0);
		if (!dhd_download_fw_on_driverload) {
			DHD_STATLOG_CTRL(&dhd->pub, ST(WLAN_POWER_ON), ifidx, 0);
#ifdef WL_EVENT
			wl_ext_event_attach_netdev(net, ifidx, dhd->iflist[ifidx]->bssidx);
#endif /* WL_EVENT */
#ifdef WL_ESCAN
			wl_escan_event_attach(net, ifidx);
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
			wl_ext_iapsta_attach_netdev(net, ifidx, dhd->iflist[ifidx]->bssidx);
#endif /* WL_EXT_IAPSTA */
#if defined(USE_INITIAL_2G_SCAN) || defined(USE_INITIAL_SHORT_DWELL_TIME)
			g_first_broadcast_scan = TRUE;
#endif /* USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME */
#ifdef SHOW_LOGTRACE
			/* dhd_cancel_logtrace_process_sync is called in dhd_stop
			 * for built-in models. Need to start logtrace kthread before
			 * calling wifi on, because once wifi is on, EDL will be in action
			 * any moment, and if kthread is not active, FW event logs will
			 * not be available
			 */
			if (dhd_reinit_logtrace_process(dhd) != BCME_OK) {
				goto exit;
			}
#endif /* SHOW_LOGTRACE */
#if defined(WLAN_ACCEL_BOOT)
			ret = wl_android_wifi_accel_on(net, dhd->wl_accel_force_reg_on);
			/* Enable wl_accel_force_reg_on if ON fails, else disable it */
			if (ret) {
				dhd->wl_accel_force_reg_on = TRUE;
			} else {
				dhd->wl_accel_force_reg_on = FALSE;
			}
#else
#if defined(BT_OVER_SDIO)
			ret = dhd_bus_get(&dhd->pub, WLAN_MODULE);
			wl_android_set_wifi_on_flag(TRUE);
#else
			ret = wl_android_wifi_on(net);
#endif /* BT_OVER_SDIO */
#endif /* WLAN_ACCEL_BOOT */
			if (ret != 0) {
				DHD_ERROR(("%s : wl_android_wifi_on failed (%d)\n",
					__FUNCTION__, ret));
				ret = -1;
				goto exit;
			}
		}
#ifdef SUPPORT_DEEP_SLEEP
		else {
			/* Flags to indicate if we distingish
			 * power off policy when user set the memu
			 * "Keep Wi-Fi on during sleep" to "Never"
			 */
			if (trigger_deep_sleep) {
#if defined(USE_INITIAL_2G_SCAN) || defined(USE_INITIAL_SHORT_DWELL_TIME)
				g_first_broadcast_scan = TRUE;
#endif /* USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME */
				dhd_deepsleep(net, 0);
				trigger_deep_sleep = 0;
			}
		}
#endif /* SUPPORT_DEEP_SLEEP */
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

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
			if (pm_runtime_get_sync(dhd_bus_to_dev(dhd->pub.bus)) >= 0) {
				ret = dhd_bus_start(&dhd->pub);
				pm_runtime_mark_last_busy(dhd_bus_to_dev(dhd->pub.bus));
				pm_runtime_put_autosuspend(dhd_bus_to_dev(dhd->pub.bus));
			}
#else
			ret = dhd_bus_start(&dhd->pub);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

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

#ifdef BT_OVER_SDIO
		if (dhd->pub.is_bt_recovery_required) {
			DHD_ERROR(("%s: Send Hang Notification 2 to BT\n", __FUNCTION__));
			bcmsdh_btsdio_process_dhd_hang_notification(TRUE);
		}
		dhd->pub.is_bt_recovery_required = FALSE;
#endif

		/* dhd_sync_with_dongle has been called in dhd_bus_start or wl_android_wifi_on */
		dev_addr_set(net, dhd->pub.mac.octet);

#ifdef TOE
		/* Get current TOE mode from dongle */
		if (dhd_toe_get(dhd, ifidx, &toe_ol) >= 0 && (toe_ol & TOE_TX_CSUM_OL) != 0) {
			dhd->iflist[ifidx]->net->features |= NETIF_F_IP_CSUM;
		} else {
			dhd->iflist[ifidx]->net->features &= ~NETIF_F_IP_CSUM;
		}
#endif /* TOE */

#ifdef DHD_LB
#ifdef ENABLE_DHD_GRO
		dhd->iflist[ifidx]->net->features |= NETIF_F_GRO;
#endif /* ENABLE_DHD_GRO */

#ifdef HOST_SFH_LLC
		dhd->iflist[ifidx]->net->needed_headroom = DOT11_LLC_SNAP_HDR_LEN;
#endif

#if defined(DHD_LB_RXP)
		__skb_queue_head_init(&dhd->rx_pend_queue);
		if (dhd->rx_napi_netdev == NULL) {
			dhd->rx_napi_netdev = dhd->iflist[ifidx]->net;
			memset(&dhd->rx_napi_struct, 0, sizeof(struct napi_struct));
			netif_napi_add(dhd->rx_napi_netdev, &dhd->rx_napi_struct,
				dhd_napi_poll, dhd_napi_weight);
			DHD_INFO(("%s napi<%p> enabled ifp->net<%p,%s> dhd_napi_weight: %d\n",
				__FUNCTION__, &dhd->rx_napi_struct, net,
				net->name, dhd_napi_weight));
			napi_enable(&dhd->rx_napi_struct);
			DHD_INFO(("%s load balance init rx_napi_struct\n", __FUNCTION__));
			skb_queue_head_init(&dhd->rx_napi_queue);
			__skb_queue_head_init(&dhd->rx_process_queue);
		} /* rx_napi_netdev == NULL */
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
		/* Use the variant that uses locks */
		skb_queue_head_init(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */
		dhd->dhd_lb_candidacy_override = FALSE;
#endif /* DHD_LB */
		netdev_update_features(net);
#ifdef DHD_PM_OVERRIDE
		g_pm_override = FALSE;
#endif /* DHD_PM_OVERRIDE */
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

#if defined(DHD_CONTROL_PCIE_ASPM_WIFI_TURNON)
		dhd_bus_aspm_enable_rc_ep(dhd->pub.bus, TRUE);
#endif /* DHD_CONTROL_PCIE_ASPM_WIFI_TURNON */
#if defined(DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON)
		dhd_irq_set_affinity(&dhd->pub, cpumask_of(0));
#endif /* DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON */
#if defined(NUM_SCB_MAX_PROBE)
		dhd_set_scb_probe(&dhd->pub);
#endif /* NUM_SCB_MAX_PROBE */
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
		if (unlikely(wl_escan_up(net))) {
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

	dhd->pub.up = 1;
#if defined(BCMPCIE) && defined(CONFIG_ARCH_MSM)
	dhd_bus_inform_ep_loaded_to_rc(&dhd->pub, dhd->pub.up);
#endif /* BCMPCIE && CONFIG_ARCH_MSM */
	DHD_START_RPM_TIMER(&dhd->pub);

	if (wl_event_enable) {
		/* For wl utility to receive events */
		dhd->pub.wl_event_enabled = true;
	} else {
		dhd->pub.wl_event_enabled = false;
	}

	if (logtrace_pkt_sendup) {
		/* For any deamon to recieve logtrace */
		dhd->pub.logtrace_pkt_sendup = true;
	} else {
		dhd->pub.logtrace_pkt_sendup = false;
	}

	OLD_MOD_INC_USE_COUNT;

#ifdef BCMDBGFS
	dhd_dbgfs_init(&dhd->pub);
#endif

exit:
	mutex_unlock(&dhd->pub.ndev_op_sync);
	if (ret) {
		dhd_stop(net);
	} else {
#if defined(ENABLE_INSMOD_NO_FW_LOAD) && defined(NO_POWER_OFF_AFTER_OPEN)
		dhd_download_fw_on_driverload = TRUE;
		dhd_driver_init_done = TRUE;
#elif defined(ENABLE_INSMOD_NO_FW_LOAD) && defined(ENABLE_INSMOD_NO_POWER_OFF)
		dhd_download_fw_on_driverload = FALSE;
		dhd_driver_init_done = TRUE;
#endif
	}

	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	WL_MSG(net->name, "Exit ret=%d\n", ret);
	return ret;
}

/*
 * ndo_start handler for primary ndev
 */
static int
dhd_pri_open(struct net_device *net)
{
	s32 ret;

	DHD_MUTEX_IS_LOCK_RETURN();
	DHD_MUTEX_LOCK();
	ret = dhd_open(net);
	if (unlikely(ret)) {
		DHD_ERROR(("Failed to open primary dev ret %d\n", ret));
		DHD_MUTEX_UNLOCK();
		return ret;
	}

	/* Allow transmit calls */
	dhd_tx_start_queues(net);
	WL_MSG(net->name, "tx queue started\n");

#if defined(SET_RPS_CPUS)
	dhd_rps_cpus_enable(net, TRUE);
#endif

#if defined(SET_XPS_CPUS)
	dhd_xps_cpus_enable(net, TRUE);
#endif
	DHD_MUTEX_UNLOCK();

	return ret;
}

/*
 * ndo_stop handler for primary ndev
 */
static int
dhd_pri_stop(struct net_device *net)
{
	s32 ret;

	/* Set state and stop OS transmissions */
	dhd_tx_stop_queues(net);
	WL_MSG(net->name, "tx queue stopped\n");

	ret = dhd_stop(net);
	if (unlikely(ret)) {
		DHD_ERROR(("dhd_stop failed: %d\n", ret));
		return ret;
	}

	return ret;
}

#ifdef PCIE_INB_DW
bool
dhd_check_cfg_in_progress(dhd_pub_t *dhdp)
{
#if defined(WL_CFG80211)
	return wl_cfg80211_check_in_progress(dhd_linux_get_primary_netdev(dhdp));
#endif /* WL_CFG80211 */
	return FALSE;
}
#endif

#if defined(WL_STATIC_IF) && defined(WL_CFG80211)
/*
 * For static I/Fs, the firmware interface init
 * is done from the IFF_UP context.
 */
static int
dhd_static_if_open(struct net_device *net)
{
	s32 ret = 0;
	struct bcm_cfg80211 *cfg;
	struct net_device *primary_netdev = NULL;
#ifdef WLEASYMESH
	dhd_info_t *dhd = DHD_DEV_INFO(net);
#endif /* WLEASYMESH */

	DHD_MUTEX_LOCK();
	cfg = wl_get_cfg(net);
	primary_netdev = bcmcfg_to_prmry_ndev(cfg);

	if (!wl_cfg80211_static_if(cfg, net)) {
		WL_MSG(net->name, "non-static interface ..do nothing\n");
		ret = BCME_OK;
		goto done;
	}

	WL_MSG(net->name, "Enter\n");
	/* Ensure fw is initialized. If it is already initialized,
	 * dhd_open will return success.
	 */
#ifdef WLEASYMESH
	WL_MSG(net->name, "switch to EasyMesh fw\n");
	dhd->pub.conf->fw_type = FW_TYPE_EZMESH;
	ret = dhd_stop(primary_netdev);
	if (unlikely(ret)) {
		printf("===>%s, Failed to close primary dev ret %d\n", __FUNCTION__, ret);
		goto done;
	}
	OSL_SLEEP(1);
#endif /* WLEASYMESH */
	ret = dhd_open(primary_netdev);
	if (unlikely(ret)) {
		DHD_ERROR(("Failed to open primary dev ret %d\n", ret));
		goto done;
	}

	ret = wl_cfg80211_static_if_open(net);
	if (ret == BCME_OK) {
		/* Allow transmit calls */
		netif_start_queue(net);
	}
done:
	WL_MSG(net->name, "Exit ret=%d\n", ret);
	DHD_MUTEX_UNLOCK();
	return ret;
}

static int
dhd_static_if_stop(struct net_device *net)
{
	struct bcm_cfg80211 *cfg;
	struct net_device *primary_netdev = NULL;
	int ret = BCME_OK;
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	WL_MSG(net->name, "Enter\n");

	cfg = wl_get_cfg(net);
	if (!wl_cfg80211_static_if(cfg, net)) {
		DHD_TRACE(("non-static interface (%s)..do nothing \n", net->name));
		return BCME_OK;
	}
#ifdef DHD_NOTIFY_MAC_CHANGED
	if (dhd->pub.skip_dhd_stop) {
		WL_MSG(net->name, "Exit skip stop\n");
		return BCME_OK;
	}
#endif /* DHD_NOTIFY_MAC_CHANGED */

	/* Ensure queue is disabled */
	netif_tx_disable(net);

	dhd_net_if_lock_local(dhd);
	ret = wl_cfg80211_static_if_close(net);
	dhd_net_if_unlock_local(dhd);

	if (dhd->pub.up == 0) {
		/* If fw is down, return */
		DHD_ERROR(("fw down\n"));
		return BCME_OK;
	}
	/* If STA iface is not in operational, invoke dhd_close from this
	* context.
	*/
	primary_netdev = bcmcfg_to_prmry_ndev(cfg);
#ifdef WLEASYMESH
	if (dhd->pub.conf->fw_type == FW_TYPE_EZMESH) {
		WL_MSG(net->name, "switch to STA fw\n");
		dhd->pub.conf->fw_type = FW_TYPE_STA;
	} else
#endif /* WLEASYMESH */
	if (!(primary_netdev->flags & IFF_UP)) {
		ret = dhd_stop(primary_netdev);
	} else {
		DHD_ERROR(("Skipped dhd_stop, as sta is operational\n"));
	}
	WL_MSG(net->name, "Exit ret=%d\n", ret);

	return ret;
}
#endif /* WL_STATIC_IF && WL_CF80211 */

int dhd_do_driver_init(struct net_device *net)
{
	dhd_info_t *dhd = NULL;
	int ret = 0;

	if (!net) {
		DHD_ERROR(("Primary Interface not initialized \n"));
		return -EINVAL;
	}

	DHD_MUTEX_IS_LOCK_RETURN();
	DHD_MUTEX_LOCK();

	/*  && defined(OEM_ANDROID) && defined(BCMSDIO) */
	dhd = DHD_DEV_INFO(net);

	/* If driver is already initialized, do nothing
	 */
	if (dhd->pub.busstate == DHD_BUS_DATA) {
		DHD_TRACE(("Driver already Inititalized. Nothing to do"));
		goto exit;
	}

	if (dhd_open(net) < 0) {
		DHD_ERROR(("Driver Init Failed \n"));
		ret = -1;
		goto exit;
	}

exit:
	DHD_MUTEX_UNLOCK();
	return ret;
}

int
dhd_event_ifadd(dhd_info_t *dhdinfo, wl_event_data_if_t *ifevent, char *name, uint8 *mac)
{

#ifdef WL_CFG80211
		if (wl_cfg80211_notify_ifadd(dhd_linux_get_primary_netdev(&dhdinfo->pub),
			ifevent->ifidx, name, mac, ifevent->bssidx, ifevent->role) == BCME_OK)
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
		strlcpy(if_event->name, name, sizeof(if_event->name));
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
	strlcpy(if_event->name, name, sizeof(if_event->name));
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

#ifdef WL_NATOE
/* Handler to update natoe info and bind with new subscriptions if there is change in config */
static void
dhd_natoe_ct_event_hanlder(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	wl_event_data_natoe_t *natoe = event_info;
	dhd_nfct_info_t *nfct = dhd->pub.nfct;

	if (event != DHD_WQ_WORK_NATOE_EVENT) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}
	if (natoe->natoe_active && natoe->sta_ip && natoe->start_port && natoe->end_port &&
			(natoe->start_port < natoe->end_port)) {
		/* Rebind subscriptions to start receiving notifications from groups */
		if (dhd_ct_nl_bind(nfct, nfct->subscriptions) < 0) {
			dhd_ct_close(nfct);
		}
		dhd_ct_send_dump_req(nfct);
	} else if (!natoe->natoe_active) {
		/* Rebind subscriptions to stop receiving notifications from groups */
		if (dhd_ct_nl_bind(nfct, CT_NULL_SUBSCRIPTION) < 0) {
			dhd_ct_close(nfct);
		}
	}
}

/* As NATOE enable/disbale event is received, we have to bind with new NL subscriptions.
 * Scheduling workq to switch from tasklet context as bind call may sleep in handler
 */
int
dhd_natoe_ct_event(dhd_pub_t *dhd, char *data)
{
	wl_event_data_natoe_t *event_data = (wl_event_data_natoe_t *)data;

	if (dhd->nfct) {
		wl_event_data_natoe_t *natoe = dhd->nfct->natoe_info;
		uint8 prev_enable = natoe->natoe_active;

		spin_lock_bh(&dhd->nfct_lock);
		memcpy(natoe, event_data, sizeof(*event_data));
		spin_unlock_bh(&dhd->nfct_lock);

		if (prev_enable != event_data->natoe_active) {
			dhd_deferred_schedule_work(dhd->info->dhd_deferred_wq,
					(void *)natoe, DHD_WQ_WORK_NATOE_EVENT,
					dhd_natoe_ct_event_hanlder, DHD_WQ_WORK_PRIORITY_LOW);
		}
		return BCME_OK;
	}
	DHD_ERROR(("%s ERROR NFCT is not enabled \n", __FUNCTION__));
	return BCME_ERROR;
}

/* Handler to send natoe ioctl to dongle */
static void
dhd_natoe_ct_ioctl_handler(void *handle, void *event_info, uint8 event)
{
	dhd_info_t *dhd = handle;
	dhd_ct_ioc_t *ct_ioc = event_info;

	if (event != DHD_WQ_WORK_NATOE_IOCTL) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (dhd_natoe_prep_send_exception_port_ioctl(&dhd->pub, ct_ioc) < 0) {
		DHD_ERROR(("%s: Error in sending NATOE IOCTL \n", __FUNCTION__));
	}
}

/* When Netlink message contains port collision info, the info must be sent to dongle FW
 * For that we have to switch context from softirq/tasklet by scheduling workq for natoe_ct ioctl
 */
void
dhd_natoe_ct_ioctl_schedule_work(dhd_pub_t *dhd, dhd_ct_ioc_t *ioc)
{

	dhd_deferred_schedule_work(dhd->info->dhd_deferred_wq, (void *)ioc,
			DHD_WQ_WORK_NATOE_IOCTL, dhd_natoe_ct_ioctl_handler,
			DHD_WQ_WORK_PRIORITY_HIGH);
}
#endif /* WL_NATOE */

/* This API maps ndev to ifp inclusive of static IFs */
static dhd_if_t *
dhd_get_ifp_by_ndev(dhd_pub_t *dhdp, struct net_device *ndev)
{
	dhd_if_t *ifp = NULL;
#ifdef WL_STATIC_IF
	u32 ifidx = (DHD_MAX_IFS + DHD_MAX_STATIC_IFS - 1);
#else
	u32 ifidx = (DHD_MAX_IFS - 1);
#endif /* WL_STATIC_IF */

	dhd_info_t *dhdinfo = (dhd_info_t *)dhdp->info;
	do {
		ifp = dhdinfo->iflist[ifidx];
		if (ifp && (ifp->net == ndev)) {
			DHD_TRACE(("match found for %s. ifidx:%d\n",
				ndev->name, ifidx));
			return ifp;
		}
	} while (ifidx--);

	DHD_ERROR(("no entry found for %s\n", ndev->name));
	return NULL;
}

bool
dhd_is_static_ndev(dhd_pub_t *dhdp, struct net_device *ndev)
{
	dhd_if_t *ifp = NULL;

	if (!dhdp || !ndev) {
		DHD_ERROR(("wrong input\n"));
		ASSERT(0);
		return false;
	}

	ifp = dhd_get_ifp_by_ndev(dhdp, ndev);
	return (ifp && (ifp->static_if == true));
}

#ifdef WL_STATIC_IF
/* In some cases, while registering I/F, the actual ifidx, bssidx and dngl_name
 * are not known. For e.g: static i/f case. This function lets to update it once
 * it is known.
 */
s32
dhd_update_iflist_info(dhd_pub_t *dhdp, struct net_device *ndev, int ifidx,
	uint8 *mac, uint8 bssidx, const char *dngl_name, int if_state)
{
	dhd_info_t *dhdinfo = (dhd_info_t *)dhdp->info;
	dhd_if_t *ifp, *ifp_new;
	s32 cur_idx;
	dhd_dev_priv_t * dev_priv;

	DHD_TRACE(("[STATIC_IF] update ifinfo for state:%d ifidx:%d\n",
			if_state, ifidx));

	ASSERT(dhdinfo && (ifidx < (DHD_MAX_IFS + DHD_MAX_STATIC_IFS)));

	if ((ifp = dhd_get_ifp_by_ndev(dhdp, ndev)) == NULL) {
		return -ENODEV;
	}
	cur_idx = ifp->idx;

	if (if_state == NDEV_STATE_OS_IF_CREATED) {
		/* mark static if */
		ifp->static_if = TRUE;
		return BCME_OK;
	}

	ifp_new = dhdinfo->iflist[ifidx];
	if (ifp_new && (ifp_new != ifp)) {
		/* There should be only one entry for a given ifidx. */
		DHD_ERROR(("ifp ptr already present for ifidx:%d\n", ifidx));
		ASSERT(0);
		dhdp->hang_reason = HANG_REASON_IFACE_ADD_FAILURE;
		net_os_send_hang_message(ifp->net);
		return -EINVAL;
	}

	/* For static if delete case, cleanup the if before ifidx update */
	if ((if_state == NDEV_STATE_FW_IF_DELETED) ||
		(if_state == NDEV_STATE_FW_IF_FAILED)) {
		dhd_cleanup_if(ifp->net);
		dev_priv = DHD_DEV_PRIV(ndev);
		dev_priv->ifidx = ifidx;
	}

	/* update the iflist ifidx slot with cached info */
	dhdinfo->iflist[ifidx] = ifp;
	dhdinfo->iflist[cur_idx] = NULL;

	/* update the values */
	ifp->idx = ifidx;
	ifp->bssidx = bssidx;

	if (if_state == NDEV_STATE_FW_IF_CREATED) {
		dhd_dev_priv_save(ndev, dhdinfo, ifp, ifidx);
		/* initialize the dongle provided if name */
		if (dngl_name) {
			strncpy(ifp->dngl_name, dngl_name, IFNAMSIZ);
		} else if (ndev->name[0] != '\0') {
			strncpy(ifp->dngl_name, ndev->name, IFNAMSIZ);
		}
		if (mac != NULL && ifp->set_macaddress == FALSE) {
			/* To and fro locations have same size - ETHER_ADDR_LEN */
			(void)memcpy_s(&ifp->mac_addr, ETHER_ADDR_LEN, mac, ETHER_ADDR_LEN);
		}
#ifdef WL_EVENT
		wl_ext_event_attach_netdev(ndev, ifidx, bssidx);
#endif /* WL_EVENT */
#ifdef WL_ESCAN
		wl_escan_event_attach(ndev, ifidx);
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_ifadding(ndev, ifidx);
		wl_ext_iapsta_attach_netdev(ndev, ifidx, bssidx);
		wl_ext_iapsta_attach_name(ndev, ifidx);
#endif /* WL_EXT_IAPSTA */
	}
	else if (if_state == NDEV_STATE_FW_IF_DELETED) {
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_dettach_netdev(ndev, cur_idx);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
		wl_escan_event_dettach(ndev, cur_idx);
#endif /* WL_ESCAN */
#ifdef WL_EVENT
		wl_ext_event_dettach_netdev(ndev, cur_idx);
#endif /* WL_EVENT */
	}
	DHD_INFO(("[STATIC_IF] ifp ptr updated for ifidx:%d curidx:%d if_state:%d\n",
		ifidx, cur_idx, if_state));
	return BCME_OK;
}
#endif /* WL_STATIC_IF */

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

	ASSERT(dhdinfo && (ifidx < (DHD_MAX_IFS + DHD_MAX_STATIC_IFS)));
	if (!dhdinfo || ifidx < 0 || ifidx >= (DHD_MAX_IFS + DHD_MAX_STATIC_IFS)) {
		return NULL;
	}

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
#if defined(CONFIG_TIZEN)
			net_stat_tizen_unregister(ifp->net);
#endif /* CONFIG_TIZEN */
			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
				dhd_tx_stop_queues(ifp->net);
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
#ifdef DHD_MQ
	if (enable_mq) {
		ifp->net = alloc_etherdev_mq(DHD_DEV_PRIV_SIZE, MQ_MAX_QUEUES);
	} else {
		ifp->net = alloc_etherdev(DHD_DEV_PRIV_SIZE);
	}
#else
	ifp->net = alloc_etherdev(DHD_DEV_PRIV_SIZE);
#endif /* DHD_MQ */

	if (ifp->net == NULL) {
		DHD_ERROR(("%s: OOM - alloc_etherdev(%zu)\n", __FUNCTION__, sizeof(dhdinfo)));
		goto fail;
	}

	/* Setup the dhd interface's netdevice private structure. */
	dhd_dev_priv_save(ifp->net, dhdinfo, ifp, ifidx);

	if (name && name[0]) {
		strlcpy(ifp->net->name, name, IFNAMSIZ);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 9))
	/* as priv_destructor calls free_netdev, no need to set need_free_netdev */
	ifp->net->needs_free_netdev = 0;
#ifdef WL_CFG80211
	if (ifidx == 0)
		ifp->net->priv_destructor = free_netdev;
	else
		ifp->net->priv_destructor = dhd_netdev_free;
#else
	ifp->net->priv_destructor = free_netdev;
#endif /* WL_CFG80211 */
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 9) */
#ifdef WL_CFG80211
	if (ifidx == 0)
		ifp->net->destructor = free_netdev;
	else
		ifp->net->destructor = dhd_netdev_free;
#else
	ifp->net->destructor = free_netdev;
#endif /* WL_CFG80211 */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 9) */
	strlcpy(ifp->name, ifp->net->name, sizeof(ifp->name));
	dhdinfo->iflist[ifidx] = ifp;

/* initialize the dongle provided if name */
	if (dngl_name) {
		strlcpy(ifp->dngl_name, dngl_name, sizeof(ifp->dngl_name));
	} else if (name) {
		strlcpy(ifp->dngl_name, name, sizeof(ifp->dngl_name));
	}

#ifdef PCIE_FULL_DONGLE
	/* Initialize STA info list */
	INIT_LIST_HEAD(&ifp->sta_list);
	DHD_IF_STA_LIST_LOCK_INIT(&ifp->sta_list_lock);
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_L2_FILTER
	ifp->phnd_arp_table = init_l2_filter_arp_table(dhdpub->osh);
	ifp->parp_allnode = TRUE;
#endif /* DHD_L2_FILTER */

#if (defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET))
	ifp->qosmap_up_table = ((uint8*)MALLOCZ(dhdpub->osh, UP_TABLE_MAX));
	ifp->qosmap_up_table_enable = FALSE;
#endif /* BCM_ROUTER_DHD && QOS_MAP_SET */

	DHD_CUMM_CTR_INIT(&ifp->cumm_ctr);

#ifdef DHD_4WAYM4_FAIL_DISCONNECT
	INIT_DELAYED_WORK(&ifp->m4state_work, dhd_m4_state_handler);
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */

#ifdef DHD_POST_EAPOL_M1_AFTER_ROAM_EVT
	ifp->recv_reassoc_evt = FALSE;
	ifp->post_roam_evt = FALSE;
#endif /* DHD_POST_EAPOL_M1_AFTER_ROAM_EVT */

#ifdef DHDTCPSYNC_FLOOD_BLK
	INIT_WORK(&ifp->blk_tsfl_work, dhd_blk_tsfl_handler);
	dhd_reset_tcpsync_info_by_ifp(ifp);
#endif /* DHDTCPSYNC_FLOOD_BLK */

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
	}
	dhdinfo->iflist[ifidx] = NULL;
	return NULL;
}

static void
dhd_cleanup_ifp(dhd_pub_t *dhdp, dhd_if_t *ifp)
{
#ifdef PCIE_FULL_DONGLE
	s32 ifidx = 0;
	if_flow_lkup_t *if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;
#endif /* PCIE_FULL_DONGLE */

	if (ifp != NULL) {
		if ((ifp->idx < 0) || (ifp->idx >= DHD_MAX_IFS)) {
			DHD_ERROR(("Wrong idx:%d \n", ifp->idx));
			ASSERT(0);
			return;
		}
#ifdef DHD_L2_FILTER
		bcm_l2_filter_arp_table_update(dhdpub->osh, ifp->phnd_arp_table, TRUE,
			NULL, FALSE, dhdpub->tickcnt);
		deinit_l2_filter_arp_table(dhdpub->osh, ifp->phnd_arp_table);
		ifp->phnd_arp_table = NULL;
#endif /* DHD_L2_FILTER */

#if (defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET))
		MFREE(dhdpub->osh, ifp->qosmap_up_table, UP_TABLE_MAX);
		ifp->qosmap_up_table = NULL;
		ifp->qosmap_up_table_enable = FALSE;
#endif /* BCM_ROUTER_DHD && QOS_MAP_SET */

		dhd_if_del_sta_list(ifp);
#ifdef PCIE_FULL_DONGLE
		/* Delete flowrings of virtual interface */
		ifidx = ifp->idx;
		if ((ifidx != 0) &&
		    ((if_flow_lkup != NULL) && (if_flow_lkup[ifidx].role != WLC_E_IF_ROLE_AP))) {
			dhd_flow_rings_delete(dhdp, ifidx);
		}
#endif /* PCIE_FULL_DONGLE */
	}
}

void
dhd_cleanup_if(struct net_device *net)
{
	dhd_info_t *dhdinfo = DHD_DEV_INFO(net);
	dhd_pub_t *dhdp = &dhdinfo->pub;
	dhd_if_t *ifp;

	ifp = dhd_get_ifp_by_ndev(dhdp, net);
	if (ifp) {
		if (ifp->idx >= DHD_MAX_IFS) {
			DHD_ERROR(("Wrong ifidx: %p, %d\n", ifp, ifp->idx));
			ASSERT(0);
			return;
		}
		dhd_cleanup_ifp(dhdp, ifp);
	}
}

/* unregister and free the the net_device interface associated with the indexed
 * slot, also free the slot memory and set the slot pointer to NULL
 */
#define DHD_TX_COMPLETION_TIMEOUT 5000
int
dhd_remove_if(dhd_pub_t *dhdpub, int ifidx, bool need_rtnl_lock)
{
	dhd_info_t *dhdinfo = (dhd_info_t *)dhdpub->info;
	dhd_if_t *ifp;
	unsigned long flags;
	long timeout;

	ifp = dhdinfo->iflist[ifidx];

	if (ifp != NULL) {
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
		cancel_delayed_work_sync(&ifp->m4state_work);
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */

#ifdef DHDTCPSYNC_FLOOD_BLK
		cancel_work_sync(&ifp->blk_tsfl_work);
#endif /* DHDTCPSYNC_FLOOD_BLK */

		dhd_cleanup_ifp(dhdpub, ifp);
#ifdef WL_STATIC_IF
		if (ifp->static_if) {
			/* static IF will be handled in detach */
			DHD_TRACE(("Skip del iface for static interface\n"));
			return BCME_OK;
		}
#endif /* WL_STATIC_IF */
		if (ifp->net != NULL) {
			DHD_ERROR(("deleting interface '%s' idx %d\n", ifp->net->name, ifp->idx));

			DHD_GENERAL_LOCK(dhdpub, flags);
			ifp->del_in_progress = true;
			DHD_GENERAL_UNLOCK(dhdpub, flags);

			/* If TX is in progress, hold the if del */
			if (DHD_IF_IS_TX_ACTIVE(ifp)) {
				DHD_INFO(("TX in progress. Wait for it to be complete."));
				timeout = wait_event_timeout(dhdpub->tx_completion_wait,
					((ifp->tx_paths_active & DHD_TX_CONTEXT_MASK) == 0),
					msecs_to_jiffies(DHD_TX_COMPLETION_TIMEOUT));
				if (!timeout) {
					/* Tx completion timeout. Attempt proceeding ahead */
					DHD_ERROR(("Tx completion timed out!\n"));
					ASSERT(0);
				}
			} else {
				DHD_TRACE(("No outstanding TX!\n"));
			}
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
#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
				if (dhdinfo->cih)
					ctf_dev_unregister(dhdinfo->cih, ifp->net);
#endif /* BCM_ROUTER_DHD && HNDCTF */

#if (defined(DHDTCPACK_SUPPRESS) && defined(BCMPCIE))
				dhd_tcpack_suppress_set(dhdpub, TCPACK_SUP_OFF);
#endif /* DHDTCPACK_SUPPRESS && BCMPCIE */
				if (need_rtnl_lock)
					unregister_netdev(ifp->net);
				else
					unregister_netdevice(ifp->net);
#if defined(WLDWDS) && defined(WL_EXT_IAPSTA)
				if (ifp->dwds) {
					wl_ext_iapsta_dettach_dwds_netdev(ifp->net, ifidx, ifp->bssidx);
				} else
#endif /* WLDWDS && WL_EXT_IAPSTA */
				{
#ifdef WL_EXT_IAPSTA
					wl_ext_iapsta_dettach_netdev(ifp->net, ifidx);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
					wl_escan_event_dettach(ifp->net, ifidx);
#endif /* WL_ESCAN */
#ifdef WL_EVENT
					wl_ext_event_dettach_netdev(ifp->net, ifidx);
#endif /* WL_EVENT */
				}
			}
			ifp->net = NULL;
			DHD_GENERAL_LOCK(dhdpub, flags);
			ifp->del_in_progress = false;
			DHD_GENERAL_UNLOCK(dhdpub, flags);
		}
#ifdef DHD_WMF
		dhd_wmf_cleanup(dhdpub, ifidx);
#endif /* DHD_WMF */
		DHD_CUMM_CTR_INIT(&ifp->cumm_ctr);

		MFREE(dhdinfo->pub.osh, ifp, sizeof(*ifp));
		ifp = NULL;
	}

	return BCME_OK;
}

#if (defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET))
int
dhd_set_qosmap_up_table(dhd_pub_t *dhdp, uint32 idx, bcm_tlv_t *qos_map_ie)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];

	if (!ifp)
	    return BCME_ERROR;

	wl_set_up_table(ifp->qosmap_up_table, qos_map_ie);
	ifp->qosmap_up_table_enable = TRUE;

	return BCME_OK;
}
#endif /* BCM_ROUTER_DHD && QOS_MAP_SET */

static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_pri_open,
	.ndo_stop = dhd_pri_stop,
	.ndo_get_stats = dhd_get_stats,
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	.ndo_siocdevprivate = dhd_ioctl_entry_wrapper,
#else
	.ndo_do_ioctl = dhd_ioctl_entry_wrapper,
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */
	.ndo_start_xmit = dhd_start_xmit_wrapper,
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	.ndo_siocdevprivate = dhd_ioctl_entry,
#else
	.ndo_do_ioctl = dhd_ioctl_entry,
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */
	.ndo_start_xmit = dhd_start_xmit,
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
#ifdef DHD_MQ
	.ndo_select_queue = dhd_select_queue
#endif
};

static struct net_device_ops dhd_ops_virt = {
#if defined(WL_CFG80211) && defined(WL_STATIC_IF)
	.ndo_open = dhd_static_if_open,
	.ndo_stop = dhd_static_if_stop,
#endif
	.ndo_get_stats = dhd_get_stats,
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	.ndo_siocdevprivate = dhd_ioctl_entry_wrapper,
#else
	.ndo_do_ioctl = dhd_ioctl_entry_wrapper,
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */
	.ndo_start_xmit = dhd_start_xmit_wrapper,
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	.ndo_siocdevprivate = dhd_ioctl_entry,
#else
	.ndo_do_ioctl = dhd_ioctl_entry,
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0) */
	.ndo_start_xmit = dhd_start_xmit,
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
static void
dhd_ctf_detach(ctf_t *ci, void *arg)
{
	dhd_info_t *dhd = (dhd_info_t *)arg;
	dhd->cih = NULL;

#ifdef CTFPOOL
	/* free the buffers in fast pool */
	osl_ctfpool_cleanup(dhd->pub.osh);
#endif /* CTFPOOL */

	return;
}
#endif /* BCM_ROUTER_DHD && HNDCTF */

int
dhd_os_write_file_posn(void *fp, unsigned long *posn, void *buf,
		unsigned long buflen)
{
	loff_t wr_posn = *posn;

	if (!fp || !buf || buflen == 0)
		return -1;

	if (vfs_write((struct file *)fp, buf, buflen, &wr_posn) < 0)
		return -1;

	*posn = wr_posn;
	return 0;
}

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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	struct kstat stat;
	mm_segment_t fs;
	int error = 0;
#endif
	char *raw_fmts =  NULL;
	int logstrs_size = 0;

	if (control_logtrace != LOGTRACE_PARSED_FMT) {
		DHD_ERROR_NO_HW4(("%s : turned off logstr parsing\n", __FUNCTION__));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	filep = filp_open(logstrs_path, O_RDONLY, 0);

	if (IS_ERR(filep)) {
		DHD_ERROR_NO_HW4(("%s: Failed to open the file %s \n", __FUNCTION__, logstrs_path));
		goto fail;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	error = vfs_stat(logstrs_path, &stat);
	if (error) {
		DHD_ERROR_NO_HW4(("%s: Failed to stat file %s \n", __FUNCTION__, logstrs_path));
		goto fail;
	}
	logstrs_size = (int) stat.size;
#else
	logstrs_size = dhd_os_get_image_size(filep);
#endif

	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail1;
	}

	if (temp->raw_fmts != NULL) {
		raw_fmts = temp->raw_fmts;	/* reuse already malloced raw_fmts */
	} else {
		raw_fmts = MALLOC(osh, logstrs_size);
		if (raw_fmts == NULL) {
			DHD_ERROR(("%s: Failed to allocate memory \n", __FUNCTION__));
			goto fail;
		}
	}

	if (vfs_read(filep, raw_fmts, logstrs_size, &filep->f_pos) !=	logstrs_size) {
		DHD_ERROR_NO_HW4(("%s: Failed to read file %s\n", __FUNCTION__, logstrs_path));
		goto fail;
	}

	if (dhd_parse_logstrs_file(osh, raw_fmts, logstrs_size, temp)
				== BCME_OK) {
		filp_close(filep, NULL);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
		set_fs(fs);
#endif
		return BCME_OK;
	}

fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
	}
	if (temp->fmts != NULL) {
		MFREE(osh, temp->fmts, temp->num_fmts * sizeof(char *));
	}

fail1:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif
	temp->fmts = NULL;
	temp->raw_fmts = NULL;

	return BCME_ERROR;
}

static int
dhd_read_map(osl_t *osh, char *fname, uint32 *ramstart, uint32 *rodata_start,
		uint32 *rodata_end)
{
	struct file *filep = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t fs;
#endif
	int err = BCME_ERROR;

	if (fname == NULL) {
		DHD_ERROR(("%s: ERROR fname is NULL \n", __FUNCTION__));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	filep = filp_open(fname, O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DHD_ERROR_NO_HW4(("%s: Failed to open %s \n",  __FUNCTION__, fname));
		goto fail;
	}

	if ((err = dhd_parse_map_file(osh, filep, ramstart,
			rodata_start, rodata_end)) < 0)
		goto fail;

fail:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif

	return err;
}
#ifdef DHD_COREDUMP
#define PC_FOUND_BIT 0x01
#define LR_FOUND_BIT 0x02
#define ALL_ADDR_VAL (PC_FOUND_BIT | LR_FOUND_BIT)
#define READ_NUM_BYTES 1000
#define DHD_FUNC_STR_LEN 80
static int
dhd_lookup_map(osl_t *osh, char *fname, uint32 pc, char *pc_fn,
		uint32 lr, char *lr_fn)
{
#ifdef DHD_LINUX_STD_FW_API
	const struct firmware *fw = NULL;
	uint32 size = 0, mem_offset = 0;
#else
	struct file *filep = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t fs;
#endif
#endif /* DHD_LINUX_STD_FW_API */
	char *raw_fmts = NULL, *raw_fmts_loc = NULL, *cptr = NULL;
	uint32 read_size = READ_NUM_BYTES;
	int err = BCME_ERROR;
	uint32 addr = 0, addr1 = 0, addr2 = 0;
	char type = '?', type1 = '?', type2 = '?';
	char func[DHD_FUNC_STR_LEN] = "\0";
	char func1[DHD_FUNC_STR_LEN] = "\0";
	char func2[DHD_FUNC_STR_LEN] = "\0";
	uint8 count = 0;
	int num, len = 0, offset;

	DHD_TRACE(("%s: fname %s pc 0x%x lr 0x%x \n",
		__FUNCTION__, fname, pc, lr));
	if (fname == NULL) {
		DHD_ERROR(("%s: ERROR fname is NULL \n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Allocate 1 byte more than read_size to terminate it with NULL */
	raw_fmts = MALLOCZ(osh, read_size + 1);
	if (raw_fmts == NULL) {
		DHD_ERROR(("%s: Failed to allocate raw_fmts memory \n",
			__FUNCTION__));
		return BCME_ERROR;
	}

#ifdef DHD_LINUX_STD_FW_API
	err = dhd_os_get_img_fwreq(&fw, fname);
	if (err < 0) {
		DHD_ERROR(("dhd_os_get_img(Request Firmware API) error : %d\n",
			err));
		goto fail;
	}
	size = fw->size;
#else
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	filep = filp_open(fname, O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DHD_ERROR(("%s: Failed to open %s \n",  __FUNCTION__, fname));
		goto fail;
	}
#endif /* DHD_LINUX_STD_FW_API */

	if (pc_fn == NULL) {
		count |= PC_FOUND_BIT;
	}
	if (lr_fn == NULL) {
		count |= LR_FOUND_BIT;
	}
	while (count != ALL_ADDR_VAL)
	{
#ifdef DHD_LINUX_STD_FW_API
		/* Bound check for size before doing memcpy() */
		if ((mem_offset + read_size) > size) {
			read_size = size - mem_offset;
		}

		err = memcpy_s(raw_fmts, read_size,
			((char *)(fw->data) + mem_offset), read_size);
		if (err) {
			DHD_ERROR(("%s: failed to copy raw_fmts, err=%d\n",
				__FUNCTION__, err));
			goto fail;
		}
#else
		err = dhd_os_read_file(filep, raw_fmts, read_size);
		if (err < 0) {
			DHD_ERROR(("%s: map file read failed err:%d \n",
				__FUNCTION__, err));
			goto fail;
		}

#endif /* DHD_LINUX_STD_FW_API */
		/* End raw_fmts with NULL as strstr expects NULL terminated
		* strings
		*/
		raw_fmts[read_size] = '\0';
		raw_fmts_loc = raw_fmts;
		offset = 0;

		while ((count != ALL_ADDR_VAL) && (offset < read_size))
		{
			cptr = bcmstrtok(&raw_fmts_loc, "\n", 0);
			if (cptr == NULL) {
				DHD_TRACE(("%s: cptr is NULL, offset %d"
					" raw_fmts_loc %s \n",
					__FUNCTION__, offset, raw_fmts_loc));
				break;
			}
			DHD_TRACE(("%s: %s \n", __FUNCTION__, cptr));
			if ((type2 == 'A') ||
				(type2 == 'T') ||
				(type2 == 'W')) {
				addr1 = addr2;
				type1 = type2;
				(void)memcpy_s(func1, DHD_FUNC_STR_LEN,
					func2, DHD_FUNC_STR_LEN);
				DHD_TRACE(("%s: %x %c %s \n",
					__FUNCTION__, addr1, type1, func1));
			}
			len = strlen(cptr);
			num = sscanf(cptr, "%x %c %79s", &addr, &type, func);
			DHD_TRACE(("%s: num %d addr %x type %c func %s \n",
				__FUNCTION__, num, addr, type, func));
			if (num == 3) {
				addr2 = addr;
				type2 = type;
				(void)memcpy_s(func2, DHD_FUNC_STR_LEN,
					func, DHD_FUNC_STR_LEN);
			}

			if (!(count & PC_FOUND_BIT) &&
				(pc >= addr1 && pc < addr2)) {
				if ((cptr = strchr(func1, '$')) != NULL) {
					(void)strncpy(func, cptr + 1,
						DHD_FUNC_STR_LEN - 1);
				} else {
					(void)memcpy_s(func, DHD_FUNC_STR_LEN,
						func1, DHD_FUNC_STR_LEN);
				}
				if ((cptr = strstr(func, "__bcmromfn"))
					!= NULL) {
					*cptr = 0;
				}
				if (pc > addr1) {
					sprintf(pc_fn, "%.68s+0x%x",
						func, pc - addr1);
				} else {
					(void)memcpy_s(pc_fn, DHD_FUNC_STR_LEN,
						func, DHD_FUNC_STR_LEN);
				}
				count |= PC_FOUND_BIT;
				DHD_INFO(("%s: found addr1 %x pc %x"
					" addr2 %x \n",
					__FUNCTION__, addr1, pc, addr2));
			}
			if (!(count & LR_FOUND_BIT) &&
				(lr >= addr1 && lr < addr2)) {
				if ((cptr = strchr(func1, '$')) != NULL) {
					(void)strncpy(func, cptr + 1,
						DHD_FUNC_STR_LEN - 1);
				} else {
					(void)memcpy_s(func, DHD_FUNC_STR_LEN,
						func1, DHD_FUNC_STR_LEN);
				}
				if ((cptr = strstr(func, "__bcmromfn"))
					!= NULL) {
					*cptr = 0;
				}
				if (lr > addr1) {
					sprintf(lr_fn, "%.68s+0x%x",
						func, lr - addr1);
				} else {
					(void)memcpy_s(lr_fn, DHD_FUNC_STR_LEN,
						func, DHD_FUNC_STR_LEN);
				}
				count |= LR_FOUND_BIT;
				DHD_INFO(("%s: found addr1 %x lr %x"
					" addr2 %x \n",
					__FUNCTION__, addr1, lr, addr2));
			}
			offset += (len + 1);
		}
#ifdef DHD_LINUX_STD_FW_API
		if ((mem_offset + read_size) >= size) {
			break;
		}

		memset(raw_fmts, 0, read_size);
		mem_offset += (read_size -(len + 1));
#else
		if (err < (int)read_size) {
			/*
			* since we reset file pos back to earlier pos by
			* bytes of one line we won't reach EOF.
			* The reason for this is if string is spreaded across
			* bytes, the read function should not miss it.
			* So if ret value is less than read_size, reached EOF
			* don't read further
			*/
			break;
		}
		memset(raw_fmts, 0, read_size);
		/*
		* go back to bytes of one line so that we won't miss
		* the string and addr even if it comes as splited in next read.
		*/
		dhd_os_seek_file(filep, -(len + 1));
#endif /* DHD_LINUX_STD_FW_API */
		DHD_TRACE(("%s: seek %d \n", __FUNCTION__, -(len + 1)));
	}

fail:
#ifdef DHD_LINUX_STD_FW_API
	if (fw) {
		dhd_os_close_img_fwreq(fw);
	}
#else
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif

#endif /* DHD_LINUX_STD_FW_API */
	if (!(count & PC_FOUND_BIT)) {
		sprintf(pc_fn, "0x%08x", pc);
	}
	if (!(count & LR_FOUND_BIT)) {
		sprintf(lr_fn, "0x%08x", lr);
	}
	return err;
}
#endif /* DHD_COREDUMP */

#ifdef DHD_LINUX_STD_FW_API
static int
dhd_init_logstrs_array(osl_t *osh, dhd_event_log_t *temp)
{
	char *raw_fmts =  NULL;
	int logstrs_size = 0;
	int error = 0;
	const struct firmware *fw = NULL;

	if (control_logtrace != LOGTRACE_PARSED_FMT) {
		DHD_ERROR_NO_HW4(("%s : turned off logstr parsing\n", __FUNCTION__));
		return BCME_ERROR;
	}

	error = dhd_os_get_img_fwreq(&fw, logstrs_path);
	if (error < 0) {
		DHD_ERROR(("dhd_os_get_img(Request Firmware API) error : %d\n",
			error));
		goto fail;
	}

	logstrs_size = (int)fw->size;
	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail;
	}

	if (temp->raw_fmts != NULL) {
		raw_fmts = temp->raw_fmts;	/* reuse already malloced raw_fmts */
	} else {
		raw_fmts = MALLOC(osh, logstrs_size);
		if (raw_fmts == NULL) {
			DHD_ERROR(("%s: Failed to allocate memory \n", __FUNCTION__));
			goto fail;
		}
	}
	error = memcpy_s(raw_fmts, logstrs_size, (char *)(fw->data), logstrs_size);
	if (error) {
		DHD_ERROR(("%s: failed to copy raw_fmts, err=%d\n",
			__FUNCTION__, error));
		goto fail;
	}
	if (dhd_parse_logstrs_file(osh, raw_fmts, logstrs_size, temp) == BCME_OK) {
		dhd_os_close_img_fwreq(fw);
		DHD_ERROR(("%s: return ok\n", __FUNCTION__));
		return BCME_OK;
	}

fail:
	if (fw) {
		dhd_os_close_img_fwreq(fw);
	}
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
	}
	if (temp->fmts != NULL) {
		MFREE(osh, temp->fmts, temp->num_fmts * sizeof(char *));
	}

	temp->fmts = NULL;
	temp->raw_fmts = NULL;

	return BCME_ERROR;
}

static int
dhd_read_map(osl_t *osh, char *fname, uint32 *ramstart, uint32 *rodata_start,
		uint32 *rodata_end)
{
	int err = BCME_ERROR;
	const struct firmware *fw = NULL;

	if (fname == NULL) {
		DHD_ERROR(("%s: ERROR fname is NULL \n", __FUNCTION__));
		return BCME_ERROR;
	}

	err = dhd_os_get_img_fwreq(&fw, fname);
	if (err < 0) {
		DHD_ERROR(("dhd_os_get_img(Request Firmware API) error : %d\n",
			err));
		goto fail;
	}

	if ((err = dhd_parse_map_file(osh, (struct firmware *)fw, ramstart,
			rodata_start, rodata_end)) < 0) {
		goto fail;
	}

fail:
	if (fw) {
		dhd_os_close_img_fwreq(fw);
	}

	return err;
}

static int
dhd_init_static_strs_array(osl_t *osh, dhd_event_log_t *temp, char *str_file, char *map_file)
{
	char *raw_fmts =  NULL;
	uint32 logstrs_size = 0;
	int error = 0;
	uint32 ramstart = 0;
	uint32 rodata_start = 0;
	uint32 rodata_end = 0;
	uint32 logfilebase = 0;
	const struct firmware *fw = NULL;

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

	/* Full file size is huge. Just read required part */
	logstrs_size = rodata_end - rodata_start;
	logfilebase = rodata_start - ramstart;

	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail1;
	}

	if (strstr(str_file, ram_file_str) != NULL && temp->raw_sstr != NULL) {
		raw_fmts = temp->raw_sstr;	/* reuse already malloced raw_fmts */
	} else if (strstr(str_file, rom_file_str) != NULL && temp->rom_raw_sstr != NULL) {
		raw_fmts = temp->rom_raw_sstr;	/* reuse already malloced raw_fmts */
	} else {
		raw_fmts = MALLOC(osh, logstrs_size);

		if (raw_fmts == NULL) {
			DHD_ERROR(("%s: Failed to allocate raw_fmts memory \n", __FUNCTION__));
			goto fail;
		}
	}

	error = dhd_os_get_img_fwreq(&fw, str_file);
	if (error < 0 || (fw == NULL) || (fw->size < logfilebase)) {
		DHD_ERROR(("dhd_os_get_img(Request Firmware API) error : %d\n",
			error));
		goto fail;
	}

	error = memcpy_s(raw_fmts, logstrs_size, (char *)((fw->data) + logfilebase),
		logstrs_size);
	if (error) {
		DHD_ERROR(("%s: failed to copy raw_fmts, err=%d\n",
			__FUNCTION__, error));
		goto fail;
	}

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = raw_fmts;
		temp->raw_sstr_size = logstrs_size;
		temp->rodata_start = rodata_start;
		temp->rodata_end = rodata_end;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = raw_fmts;
		temp->rom_raw_sstr_size = logstrs_size;
		temp->rom_rodata_start = rodata_start;
		temp->rom_rodata_end = rodata_end;
	}

	if (fw) {
		dhd_os_close_img_fwreq(fw);
	}

	return BCME_OK;

fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
	}

fail1:
	if (fw) {
		dhd_os_close_img_fwreq(fw);
	}

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = NULL;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = NULL;
	}

	return error;
} /* dhd_init_static_strs_array */
#else
static int
dhd_init_logstrs_array(osl_t *osh, dhd_event_log_t *temp)
{
	struct file *filep = NULL;
	struct kstat stat;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t fs;
#endif
	char *raw_fmts =  NULL;
	int logstrs_size = 0;
	int error = 0;

	if (control_logtrace != LOGTRACE_PARSED_FMT) {
		DHD_ERROR_NO_HW4(("%s : turned off logstr parsing\n", __FUNCTION__));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	filep = dhd_filp_open(logstrs_path, O_RDONLY, 0);

	if (IS_ERR(filep) || (filep == NULL)) {
		DHD_ERROR_NO_HW4(("%s: Failed to open the file %s \n",
			__FUNCTION__, logstrs_path));
		goto fail;
	}
	error = dhd_vfs_stat(logstrs_path, &stat);
	if (error) {
		DHD_ERROR_NO_HW4(("%s: Failed to stat file %s \n", __FUNCTION__, logstrs_path));
		goto fail;
	}
	logstrs_size = (int) stat.size;

	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail1;
	}

	if (temp->raw_fmts != NULL) {
		raw_fmts = temp->raw_fmts;	   /* reuse already malloced raw_fmts */
	} else {
		raw_fmts = MALLOC(osh, logstrs_size);
		if (raw_fmts == NULL) {
			DHD_ERROR(("%s: Failed to allocate memory \n", __FUNCTION__));
			goto fail;
		}
	}

	if (dhd_vfs_read(filep, raw_fmts, logstrs_size, &filep->f_pos) != logstrs_size) {
		DHD_ERROR_NO_HW4(("%s: Failed to read file %s\n", __FUNCTION__, logstrs_path));
		goto fail;
	}

	if (dhd_parse_logstrs_file(osh, raw_fmts, logstrs_size, temp)
			== BCME_OK) {
		dhd_filp_close(filep, NULL);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
		set_fs(fs);
#endif
		return BCME_OK;
	}

	fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
	}
	if (temp->fmts != NULL) {
		MFREE(osh, temp->fmts, temp->num_fmts * sizeof(char *));
	}

	fail1:
	if (!IS_ERR(filep))
		dhd_filp_close(filep, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif
	temp->fmts = NULL;
	temp->raw_fmts = NULL;

	return BCME_ERROR;
}

static int
dhd_read_map(osl_t *osh, char *fname, uint32 *ramstart, uint32 *rodata_start,
		uint32 *rodata_end)
{
	struct file *filep = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t fs;
#endif
	int err = BCME_ERROR;

	if (fname == NULL) {
		DHD_ERROR(("%s: ERROR fname is NULL \n", __FUNCTION__));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	filep = dhd_filp_open(fname, O_RDONLY, 0);
	if (IS_ERR(filep) || (filep == NULL)) {
		DHD_ERROR_NO_HW4(("%s: Failed to open %s \n",  __FUNCTION__, fname));
		goto fail;
	}

	if ((err = dhd_parse_map_file(osh, filep, ramstart,
			rodata_start, rodata_end)) < 0)
		goto fail;

fail:
	if (!IS_ERR(filep))
		dhd_filp_close(filep, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif

	return err;
}

static int
dhd_init_static_strs_array(osl_t *osh, dhd_event_log_t *temp, char *str_file, char *map_file)
{
	struct file *filep = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t fs;
#endif
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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	filep = filp_open(str_file, O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DHD_ERROR(("%s: Failed to open the file %s \n",  __FUNCTION__, str_file));
		goto fail;
	}

	if (TRUE) {
		/* Full file size is huge. Just read required part */
		logstrs_size = rodata_end - rodata_start;
		logfilebase = rodata_start - ramstart;
	}

	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail1;
	}

	if (strstr(str_file, ram_file_str) != NULL && temp->raw_sstr != NULL) {
		raw_fmts = temp->raw_sstr;	/* reuse already malloced raw_fmts */
	} else if (strstr(str_file, rom_file_str) != NULL && temp->rom_raw_sstr != NULL) {
		raw_fmts = temp->rom_raw_sstr;	/* reuse already malloced raw_fmts */
	} else {
		raw_fmts = MALLOC(osh, logstrs_size);

		if (raw_fmts == NULL) {
			DHD_ERROR(("%s: Failed to allocate raw_fmts memory \n", __FUNCTION__));
			goto fail;
		}
	}

	if (TRUE) {
		error = generic_file_llseek(filep, logfilebase, SEEK_SET);
		if (error < 0) {
			DHD_ERROR(("%s: %s llseek failed %d \n", __FUNCTION__, str_file, error));
			goto fail;
		}
	}

	error = vfs_read(filep, raw_fmts, logstrs_size, (&filep->f_pos));
	if (error != logstrs_size) {
		DHD_ERROR(("%s: %s read failed %d \n", __FUNCTION__, str_file, error));
		goto fail;
	}

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = raw_fmts;
		temp->raw_sstr_size = logstrs_size;
		temp->rodata_start = rodata_start;
		temp->rodata_end = rodata_end;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = raw_fmts;
		temp->rom_raw_sstr_size = logstrs_size;
		temp->rom_rodata_start = rodata_start;
		temp->rom_rodata_end = rodata_end;
	}

	filp_close(filep, NULL);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif

	return BCME_OK;

fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
	}

fail1:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(fs);
#endif

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = NULL;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = NULL;
	}

	return error;
} /* dhd_init_static_strs_array */
#endif /* DHD_LINUX_STD_FW_API */
#endif /* SHOW_LOGTRACE */

#ifdef BT_OVER_PCIE
void request_bt_quiesce(bool quiesce) __attribute__ ((weak));
void response_bt_quiesce(bool quiesce);

static void (*request_bt_quiesce_ptr)(bool);
typedef void (*response_bt_quiesce_ptr)(bool);

response_bt_quiesce_ptr
register_request_bt_quiesce(void (*fnc)(bool))
{
	request_bt_quiesce_ptr = fnc;
	return response_bt_quiesce;
}
EXPORT_SYMBOL(register_request_bt_quiesce);

void
unregister_request_bt_quiesce(void)
{
	request_bt_quiesce_ptr = NULL;
	return;
}
EXPORT_SYMBOL(unregister_request_bt_quiesce);
#endif /* BT_OVER_PCIE */

#ifdef DHD_ERPOM
uint enable_erpom = 0;
module_param(enable_erpom, int, 0);

int
dhd_wlan_power_off_handler(void *handler, unsigned char reason)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handler;
	bool dongle_isolation = dhdp->dongle_isolation;

	DHD_ERROR(("%s: WLAN DHD cleanup reason: %d\n", __FUNCTION__, reason));

	if ((reason == BY_BT_DUE_TO_BT) || (reason == BY_BT_DUE_TO_WLAN)) {
#if defined(DHD_FW_COREDUMP)
		/* save core dump to a file */
		if (dhdp->memdump_enabled) {
#ifdef DHD_SSSR_DUMP
			DHD_ERROR(("%s : Set collect_sssr as TRUE\n", __FUNCTION__));
			dhdp->collect_sssr = TRUE;
#endif /* DHD_SSSR_DUMP */
			dhdp->memdump_type = DUMP_TYPE_DUE_TO_BT;
			dhd_bus_mem_dump(dhdp);
		}
#endif /* DHD_FW_COREDUMP */
	}

	/* pause data on all the interfaces */
	dhd_bus_stop_queue(dhdp->bus);

	/* Devreset function will perform FLR again, to avoid it set dongle_isolation */
	dhdp->dongle_isolation = TRUE;
	dhd_bus_devreset(dhdp, 1); /* DHD structure cleanup */
	dhdp->dongle_isolation = dongle_isolation; /* Restore the old value */
	return 0;
}

int
dhd_wlan_power_on_handler(void *handler, unsigned char reason)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handler;
	bool dongle_isolation = dhdp->dongle_isolation;

	DHD_ERROR(("%s: WLAN DHD re-init reason: %d\n", __FUNCTION__, reason));
	/* Devreset function will perform FLR again, to avoid it set dongle_isolation */
	dhdp->dongle_isolation = TRUE;
	dhd_bus_devreset(dhdp, 0); /* DHD structure re-init */
	dhdp->dongle_isolation = dongle_isolation; /* Restore the old value */
	/* resume data on all the interfaces */
	dhd_bus_start_queue(dhdp->bus);
	return 0;

}

#endif /* DHD_ERPOM */

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

/** Called once for each hardware (dongle) instance that this DHD manages */
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
#ifdef DHD_ERPOM
	pom_func_handler_t *pom_handler;
#endif /* DHD_ERPOM */
#if defined(BCMSDIO) || defined(BCMPCIE)
	uint32 bus_type = -1;
	uint32 bus_num = -1;
	uint32 slot_num = -1;
	wifi_adapter_info_t *adapter = NULL;
#elif defined(BCMDBUS)
	wifi_adapter_info_t *adapter = data;
#endif

	dhd_attach_states_t dhd_state = DHD_ATTACH_STATE_INIT;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef PCIE_FULL_DONGLE
	ASSERT(sizeof(dhd_pkttag_fd_t) <= OSL_PKTTAG_SZ);
	ASSERT(sizeof(dhd_pkttag_fr_t) <= OSL_PKTTAG_SZ);
#endif /* PCIE_FULL_DONGLE */

	/* will implement get_ids for DBUS later */
#if defined(BCMSDIO) || defined(BCMPCIE)
	dhd_bus_get_ids(bus, &bus_type, &bus_num, &slot_num);
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

	dhd->pub.dhd_console_ms = dhd_console_ms; /* assigns default value */

	dhd->adapter = adapter;
	dhd->pub.adapter = (void *)adapter;
#ifdef BT_OVER_SDIO
	dhd->pub.is_bt_recovery_required = FALSE;
	mutex_init(&dhd->bus_user_lock);
#endif /* BT_OVER_SDIO */

	g_dhd_pub = &dhd->pub;

#ifdef DHD_DEBUG
	dll_init(&(dhd->pub.mw_list_head));
#endif /* DHD_DEBUG */

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
#ifdef WL_NANHO
	/* initialize NANHO host module */
	if (bcm_nanho_init(&dhd->pub.nanhoi, &dhd->pub,
			dhd_nho_ioctl_cb, dhd_nho_evt_cb, NULL) != BCME_OK) {
		goto fail;
	}
#endif /* WL_NANHO */
	/* Initialize thread based operation and lock */
	sema_init(&dhd->sdsem, 1);
#endif /* BCMDBUS */
#if defined(WL_MONITOR) && defined(HOST_RADIOTAP_CONV)
	dhd->host_radiotap_conv = FALSE;
#endif /* WL_MONITOR */
	dhd->pub.pcie_txs_metadata_enable = pcie_txs_metadata_enable;

	/* Link to info module */
	dhd->pub.info = dhd;

	/* Link to bus module */
	dhd->pub.bus = bus;
	dhd->pub.hdrlen = bus_hdrlen;
	dhd->pub.txoff = FALSE;
#ifdef CHECK_TRAP_ROT
	dhd->pub.check_trap_rot = TRUE;
#else
	dhd->pub.check_trap_rot = FALSE;
#endif /* CHECK_TRAP_ROT */

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
		strlcpy(if_name, iface_name, sizeof(if_name));
		len = strlen(if_name);
		ch = if_name[len - 1];
		if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2)) {
			strncat(if_name, "%d", sizeof(if_name) - len - 1);
		}
	}

	/* Passing NULL to dngl_name to ensure host gets if_name in dngl_name member */
	net = dhd_allocate_if(&dhd->pub, 0, if_name, NULL, 0, TRUE, NULL);
	if (net == NULL) {
		goto fail;
	}
	mutex_init(&dhd->pub.ndev_op_sync);

	dhd_state |= DHD_ATTACH_STATE_ADD_IF;
#ifdef DHD_L2_FILTER
	/* initialize the l2_filter_cnt */
	dhd->pub.l2_filter_cnt = 0;
#endif
	net->netdev_ops = NULL;

	mutex_init(&dhd->dhd_iovar_mutex);
	sema_init(&dhd->proto_sem, 1);

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
	/* XXX Some of this goes away, leftover from USB */
	/* XXX Some could also move to bus_init()? */
	init_waitqueue_head(&dhd->ioctl_resp_wait);
	init_waitqueue_head(&dhd->pub.tx_tput_test_wait);
	init_waitqueue_head(&dhd->d3ack_wait);
#ifdef PCIE_INB_DW
	init_waitqueue_head(&dhd->ds_exit_wait);
#endif /* PCIE_INB_DW */
	init_waitqueue_head(&dhd->ctrl_wait);
	init_waitqueue_head(&dhd->dhd_bus_busy_state_wait);
	init_waitqueue_head(&dhd->dmaxfer_wait);
#ifdef BT_OVER_PCIE
	init_waitqueue_head(&dhd->quiesce_wait);
#endif /* BT_OVER_PCIE */
	init_waitqueue_head(&dhd->pub.tx_completion_wait);
	dhd->pub.dhd_bus_busy_state = 0;
	/* Initialize the spinlocks */
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);
	spin_lock_init(&dhd->dhd_lock);
	spin_lock_init(&dhd->txoff_lock);
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

#ifdef DHD_HP2P
	spin_lock_init(&dhd->hp2p_lock);
#endif
	/* Initialize Wakelock stuff */
	spin_lock_init(&dhd->wakelock_spinlock);
	spin_lock_init(&dhd->wakelock_evt_spinlock);
	DHD_OS_WAKE_LOCK_INIT(dhd);
	dhd->wakelock_counter = 0;
	/* wakelocks prevent a system from going into a low power state */
#ifdef CONFIG_HAS_WAKELOCK
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	dhd_wake_lock_init(&dhd->wl_wifi, WAKE_LOCK_SUSPEND, "wlan_wake");
	dhd_wake_lock_init(&dhd->wl_wdwake, WAKE_LOCK_SUSPEND, "wlan_wd_wake");
#endif /* CONFIG_HAS_WAKELOCK */

#if defined(OEM_ANDROID)
	mutex_init(&dhd->dhd_net_if_mutex);
	mutex_init(&dhd->dhd_suspend_mutex);
#if defined(PKT_FILTER_SUPPORT) && defined(APF)
	mutex_init(&dhd->dhd_apf_mutex);
#endif /* PKT_FILTER_SUPPORT && APF */
#endif /* defined(OEM_ANDROID) */
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

#ifdef WL_EVENT
	if (wl_ext_event_attach(net) != 0) {
		DHD_ERROR(("wl_ext_event_attach failed\n"));
		goto fail;
	}
#endif /* WL_EVENT */
#ifdef WL_ESCAN
	/* Attach and link in the escan */
	if (wl_escan_attach(net) != 0) {
		DHD_ERROR(("wl_escan_attach failed\n"));
		goto fail;
	}
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
	if (wl_ext_iapsta_attach(net) != 0) {
		DHD_ERROR(("wl_ext_iapsta_attach failed\n"));
		goto fail;
	}
#endif /* WL_EXT_IAPSTA */
#ifdef WL_EXT_GENL
	if (wl_ext_genl_init(net)) {
		DHD_ERROR(("wl_ext_genl_init failed\n"));
		goto fail;
	}
#endif
#if defined(WL_WIRELESS_EXT)
	/* Attach and link in the iw */
	if (wl_iw_attach(net) != 0) {
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

	/* attach debug if support */
	if (dhd_os_dbg_attach(&dhd->pub)) {
		DHD_ERROR(("%s debug module attach failed\n", __FUNCTION__));
		goto fail;
	}
#ifdef DEBUGABILITY
#if !defined(OEM_ANDROID) && defined(SHOW_LOGTRACE)
	/* enable verbose ring to support dump_trace_buf */
	dhd_os_start_logging(&dhd->pub, FW_VERBOSE_RING_NAME, 3, 0, 0, 0);
#endif /* !OEM_ANDROID && SHOW_LOGTRACE */

#if !defined(OEM_ANDROID) && defined(BTLOG)
	/* enable bt log ring to support dump_bt_log */
	dhd_os_start_logging(&dhd->pub, BT_LOG_RING_NAME, 3, 0, 0, 0);
#endif /* !OEM_ANDROID && BTLOG */
#ifdef DBG_PKT_MON
	dhd->pub.dbg->pkt_mon_lock = osl_spin_lock_init(dhd->pub.osh);
#ifdef DBG_PKT_MON_INIT_DEFAULT
	dhd_os_dbg_attach_pkt_monitor(&dhd->pub);
#endif /* DBG_PKT_MON_INIT_DEFAULT */
#endif /* DBG_PKT_MON */

#endif /* DEBUGABILITY */

#ifdef DHD_MEM_STATS
	dhd->pub.mem_stats_lock = osl_spin_lock_init(dhd->pub.osh);
	dhd->pub.txpath_mem = 0;
	dhd->pub.rxpath_mem = 0;
#endif /* DHD_MEM_STATS */

#if defined(DHD_AWDL) && defined(AWDL_SLOT_STATS)
	dhd->pub.awdl_stats_lock = osl_spin_lock_init(dhd->pub.osh);
#endif /* DHD_AWDL && AWDL_SLOT_STATS */

#ifdef DHD_STATUS_LOGGING
	dhd->pub.statlog = dhd_attach_statlog(&dhd->pub, MAX_STATLOG_ITEM,
		MAX_STATLOG_REQ_ITEM, STATLOG_LOGBUF_LEN);
	if (dhd->pub.statlog == NULL) {
		DHD_ERROR(("%s: alloc statlog failed\n", __FUNCTION__));
	}
#endif /* DHD_STATUS_LOGGING */

#ifdef DHD_LOG_DUMP
	dhd_log_dump_init(&dhd->pub);
#endif /* DHD_LOG_DUMP */
#ifdef DHD_PKTDUMP_ROAM
	dhd_dump_pkt_init(&dhd->pub);
#endif /* DHD_PKTDUMP_ROAM */
#ifdef DHD_PKT_LOGGING
	dhd_os_attach_pktlog(&dhd->pub);
#endif /* DHD_PKT_LOGGING */

#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
	dhd->pub.hang_info = MALLOCZ(osh, VENDOR_SEND_HANG_EXT_INFO_LEN);
	if (dhd->pub.hang_info == NULL) {
		DHD_ERROR(("%s: alloc hang_info failed\n", __FUNCTION__));
	}
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
	if (dhd_sta_pool_init(&dhd->pub, DHD_MAX_STA) != BCME_OK) {
		DHD_ERROR(("%s: Initializing %u sta\n", __FUNCTION__, DHD_MAX_STA));
		goto fail;
	}

#ifdef BCM_ROUTER_DHD
#if defined(HNDCTF)
	dhd->cih = ctf_attach(dhd->pub.osh, "dhd", &dhd_msg_level, dhd_ctf_detach, dhd);
	if (!dhd->cih) {
		DHD_ERROR(("%s: ctf_attach() failed\n", __FUNCTION__));
	}
#ifdef CTFPOOL
	{
		int poolsz = RXBUFPOOLSZ;
		if (CTF_ENAB(dhd->cih) && (osl_ctfpool_init(dhd->pub.osh,
			poolsz, RXBUFSZ + BCMEXTRAHDROOM) < 0)) {
			DHD_ERROR(("%s: osl_ctfpool_init() failed\n", __FUNCTION__));
		}
	}
#endif /* CTFPOOL */
#endif /* HNDCTF */
#endif /* BCM_ROUTER_DHD */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	dhd->tx_wq = alloc_workqueue("bcmdhd-tx-wq", WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!dhd->tx_wq) {
		DHD_ERROR(("%s: alloc_workqueue(bcmdhd-tx-wq) failed\n", __FUNCTION__));
		goto fail;
	}
	dhd->rx_wq = alloc_workqueue("bcmdhd-rx-wq", WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!dhd->rx_wq) {
		DHD_ERROR(("%s: alloc_workqueue(bcmdhd-rx-wq) failed\n", __FUNCTION__));
		destroy_workqueue(dhd->tx_wq);
		dhd->tx_wq = NULL;
		goto fail;
	}
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

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

#ifdef SHOW_LOGTRACE
	skb_queue_head_init(&dhd->evt_trace_queue);

	/* Create ring proc entries */
	dhd_dbg_ring_proc_create(&dhd->pub);
#endif /* SHOW_LOGTRACE */

#ifdef BTLOG
	skb_queue_head_init(&dhd->bt_log_queue);
#endif /* BTLOG */

#ifdef BT_OVER_PCIE
	mutex_init(&dhd->quiesce_flr_lock);
	mutex_init(&dhd->quiesce_lock);
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
#if defined (OEM_ANDROID)
	INIT_WORK(&dhd->dhd_hang_process_work, dhd_hang_process);
#endif /* OEM_ANDROID */
#ifdef DEBUG_CPU_FREQ
	dhd->new_freq = alloc_percpu(int);
	dhd->freq_trans.notifier_call = dhd_cpufreq_notifier;
	cpufreq_register_notifier(&dhd->freq_trans, CPUFREQ_TRANSITION_NOTIFIER);
#endif
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMSDIO
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_DELAYTX);
#elif defined(BCMPCIE)
	/* xxx : In case of PCIe based Samsung Android project, enable TCP ACK Suppress
	 *       when throughput is higher than threshold, following rps_cpus setting.
	 */
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_HOLD);
#else
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif /* BCMSDIO */
#endif /* DHDTCPACK_SUPPRESS */

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */

#ifdef DHD_DEBUG_PAGEALLOC
	register_page_corrupt_cb(dhd_page_corrupt_cb, &dhd->pub);
#endif /* DHD_DEBUG_PAGEALLOC */

	INIT_DELAYED_WORK(&dhd->dhd_dpc_dispatcher_work, dhd_dpc_tasklet_dispatcher_work);

#if defined(DHD_LB)
#if defined(DHD_LB_HOST_CTRL)
	dhd->permitted_primary_cpu = FALSE;
#endif /* DHD_LB_HOST_CTRL */
	dhd_lb_set_default_cpus(dhd);
	DHD_LB_STATS_INIT(&dhd->pub);

	/* Initialize the CPU Masks */
	if (dhd_cpumasks_init(dhd) == 0) {
		/* Now we have the current CPU maps, run through candidacy */
		dhd_select_cpu_candidacy(dhd);

		/* Register the call backs to CPU Hotplug sub-system */
		dhd_register_cpuhp_callback(dhd);

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

#ifdef DHD_LB_RXP
	/* Trun ON the feature by default */
	atomic_set(&dhd->lb_rxp_active, 1);
#endif /* DHD_LB_RXP */

	/* Initialize the Load Balancing Tasklets and Napi object */
#if defined(DHD_LB_RXP)
	__skb_queue_head_init(&dhd->rx_pend_queue);
	skb_queue_head_init(&dhd->rx_napi_queue);
	__skb_queue_head_init(&dhd->rx_process_queue);
	/* Initialize the work that dispatches NAPI job to a given core */
	INIT_WORK(&dhd->rx_napi_dispatcher_work, dhd_rx_napi_dispatcher_work);
	DHD_INFO(("%s load balance init rx_napi_queue\n", __FUNCTION__));
	/* Initialize the work that dispatches DPC tasklet to a given core */
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

#if defined(DNGL_AXI_ERROR_LOGGING) && defined(DHD_USE_WQ_FOR_DNGL_AXI_ERROR)
	INIT_WORK(&dhd->axi_error_dispatcher_work, dhd_axi_error_dispatcher_fn);
#endif /* DNGL_AXI_ERROR_LOGGING && DHD_USE_WQ_FOR_DNGL_AXI_ERROR */

#ifdef BCMDBG
	if (dhd_macdbg_attach(&dhd->pub) != BCME_OK) {
		DHD_ERROR(("%s: dhd_macdbg_attach fail\n", __FUNCTION__));
		goto fail;
	}
#endif /* BCMDBG */

#ifdef REPORT_FATAL_TIMEOUTS
	init_dhd_timeouts(&dhd->pub);
#endif /* REPORT_FATAL_TIMEOUTS */
#if defined(BCMPCIE)
	dhd->pub.extended_trap_data = MALLOCZ(osh, BCMPCIE_EXT_TRAP_DATA_MAXLEN);
	if (dhd->pub.extended_trap_data == NULL) {
		DHD_ERROR(("%s: Failed to alloc extended_trap_data\n", __FUNCTION__));
	}
#ifdef DNGL_AXI_ERROR_LOGGING
	dhd->pub.axi_err_dump = MALLOCZ(osh, sizeof(dhd_axi_error_dump_t));
	if (dhd->pub.axi_err_dump == NULL) {
		DHD_ERROR(("%s: Failed to alloc axi_err_dump\n", __FUNCTION__));
	}
#endif /* DNGL_AXI_ERROR_LOGGING */
#endif /* BCMPCIE */

#ifdef SHOW_LOGTRACE
	if (dhd_init_logtrace_process(dhd) != BCME_OK) {
		goto fail;
	}
#endif /* SHOW_LOGTRACE */

#ifdef BTLOG
	INIT_WORK(&dhd->bt_log_dispatcher_work, dhd_bt_log_process);
#endif /* BTLOG */

#ifdef EWP_EDL
	INIT_DELAYED_WORK(&dhd->edl_dispatcher_work, dhd_edl_process_work);
#endif

	DHD_SSSR_MEMPOOL_INIT(&dhd->pub);
	DHD_SSSR_REG_INFO_INIT(&dhd->pub);

#ifdef DHD_SDTC_ETB_DUMP
	dhd_sdtc_etb_mempool_init(&dhd->pub);
#endif /* DHD_SDTC_ETB_DUMP */

#ifdef EWP_EDL
	if (host_edl_support) {
		if (DHD_EDL_MEM_INIT(&dhd->pub) != BCME_OK) {
			host_edl_support = FALSE;
		}
	}
#endif /* EWP_EDL */

	dhd_init_sock_flows_buf(dhd, dhd_watchdog_ms);

	(void)dhd_sysfs_init(dhd);

#ifdef WL_NATOE
	/* Open Netlink socket for NF_CONNTRACK notifications */
	dhd->pub.nfct = dhd_ct_open(&dhd->pub, NFNL_SUBSYS_CTNETLINK | NFNL_SUBSYS_CTNETLINK_EXP,
			CT_ALL);
#endif /* WL_NATOE */
#ifdef GDB_PROXY
	dhd->pub.gdb_proxy_nodeadman = nodeadman != 0;
#endif /* GDB_PROXY */
	dhd_state |= DHD_ATTACH_STATE_DONE;
	dhd->dhd_state = dhd_state;

	dhd_found++;

#ifdef CSI_SUPPORT
	dhd_csi_init(&dhd->pub);
#endif /* CSI_SUPPORT */

#ifdef DHD_FW_COREDUMP
	/* Set memdump default values */
#ifdef CUSTOMER_HW4_DEBUG
	dhd->pub.memdump_enabled = DUMP_DISABLED;
#elif defined(OEM_ANDROID)
#ifdef DHD_COREDUMP
	dhd->pub.memdump_enabled = DUMP_MEMFILE;
#else
	dhd->pub.memdump_enabled = DUMP_MEMFILE_BUGON;
#endif /* DHD_COREDUMP */
#else
	dhd->pub.memdump_enabled = DUMP_MEMFILE;
#endif /* CUSTOMER_HW4_DEBUG */
	/* Check the memdump capability */
	dhd_get_memdump_info(&dhd->pub);
#endif /* DHD_FW_COREDUMP */

#ifdef DHD_ERPOM
	if (enable_erpom) {
		pom_handler = &dhd->pub.pom_wlan_handler;
		pom_handler->func_id = WLAN_FUNC_ID;
		pom_handler->handler = (void *)g_dhd_pub;
		pom_handler->power_off = dhd_wlan_power_off_handler;
		pom_handler->power_on = dhd_wlan_power_on_handler;

		dhd->pub.pom_func_register = NULL;
		dhd->pub.pom_func_deregister = NULL;
		dhd->pub.pom_toggle_reg_on = NULL;

		dhd->pub.pom_func_register = symbol_get(pom_func_register);
		dhd->pub.pom_func_deregister = symbol_get(pom_func_deregister);
		dhd->pub.pom_toggle_reg_on = symbol_get(pom_toggle_reg_on);

		symbol_put(pom_func_register);
		symbol_put(pom_func_deregister);
		symbol_put(pom_toggle_reg_on);

		if (!dhd->pub.pom_func_register ||
			!dhd->pub.pom_func_deregister ||
			!dhd->pub.pom_toggle_reg_on) {
			DHD_ERROR(("%s, enable_erpom enabled through module parameter but "
				"POM is not loaded\n", __FUNCTION__));
			ASSERT(0);
			goto fail;
		}
		dhd->pub.pom_func_register(pom_handler);
		dhd->pub.enable_erpom = TRUE;

	}
#endif /* DHD_ERPOM */

#ifdef DHD_DUMP_MNGR
	dhd->pub.dump_file_manage =
		(dhd_dump_file_manage_t *)MALLOCZ(dhd->pub.osh, sizeof(dhd_dump_file_manage_t));
	if (unlikely(!dhd->pub.dump_file_manage)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_dump_file_manage_t\n", __FUNCTION__));
	}
#endif /* DHD_DUMP_MNGR */

#ifdef BCMINTERNAL
#ifdef DHD_FWTRACE
	/* Attach the fwtrace */
	if (dhd_fwtrace_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_fwtrace_attach has failed\n"));
		goto fail;
	}
#endif /* DHD_FWTRACE */
#endif /* BCMINTERNAL */

#ifdef RTT_SUPPORT
	if (dhd_rtt_attach(&dhd->pub)) {
		DHD_ERROR(("dhd_rtt_attach has failed\n"));
		goto fail;
	}
#endif /* RTT_SUPPORT */

#ifdef DHD_TX_PROFILE
	if (dhd_tx_profile_attach(&dhd->pub) != BCME_OK) {
		DHD_ERROR(("%s:\tdhd_tx_profile_attach has failed\n", __FUNCTION__));
		goto fail;
	}
#endif /* defined(DHD_TX_PROFILE) */

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

extern char * nvram_get(const char *name);
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
#ifdef DHD_LINUX_STD_FW_API
		fw = DHD_FW_NAME;
		nv = DHD_NVRAM_NAME;
#else
#ifdef CONFIG_BCMDHD_FW_PATH
		fw = VENDOR_PATH CONFIG_BCMDHD_FW_PATH;
#endif /* CONFIG_BCMDHD_FW_PATH */
#ifdef CONFIG_BCMDHD_NVRAM_PATH
		nv = VENDOR_PATH CONFIG_BCMDHD_NVRAM_PATH;
#endif /* CONFIG_BCMDHD_NVRAM_PATH */
#endif /* DHD_LINUX_STD_FW_API */
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

#ifdef BCM_ROUTER_DHD
	if (!fw) {
		char var[32];

		snprintf(var, sizeof(var), "firmware_path%d", dhdinfo->unit);
		fw = nvram_get(var);
	}
	if (!nv) {
		char var[32];

		snprintf(var, sizeof(var), "nvram_path%d", dhdinfo->unit);
		nv = nvram_get(var);
	}
	DHD_ERROR(("dhd:%d: fw path:%s nv path:%s\n", dhdinfo->unit, fw, nv));
#endif

	if (fw && fw[0] != '\0') {
		fw_len = strlen(fw);
		if (fw_len >= fw_path_len) {
			DHD_ERROR(("fw path len exceeds max len of dhdinfo->fw_path\n"));
			return FALSE;
		}
		strlcpy(dhdinfo->fw_path, fw, fw_path_len);
	}
	if (nv && nv[0] != '\0') {
		nv_len = strlen(nv);
		if (nv_len >= nv_path_len) {
			DHD_ERROR(("nvram path len exceeds max len of dhdinfo->nv_path\n"));
			return FALSE;
		}
		memset(dhdinfo->nv_path, 0, nv_path_len);
		strlcpy(dhdinfo->nv_path, nv, nv_path_len);
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
		strlcpy(dhdinfo->uc_path, uc, sizeof(dhdinfo->uc_path));
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
		strlcpy(dhdinfo->btfw_path, fw, sizeof(dhdinfo->btfw_path));
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

#ifdef BCM4389_CHIP_DEF
	config_chipid = BCM4389_CHIP_ID;
#elif defined(BCM4375_CHIP)
	config_chipid = BCM4375_CHIP_ID;
#elif defined(BCM4361_CHIP)
	config_chipid = BCM4361_CHIP_ID;
#elif defined(BCM4359_CHIP)
	config_chipid = BCM4359_CHIP_ID;
#elif defined(BCM4358_CHIP)
	config_chipid = BCM4358_CHIP_ID;
#elif defined(BCM4354_CHIP)
	config_chipid = BCM4354_CHIP_ID;
#elif defined(BCM4339_CHIP)
	config_chipid = BCM4339_CHIP_ID;
#elif defined(BCM4335_CHIP)
	config_chipid = BCM4335_CHIP_ID;
#elif defined(BCM43430_CHIP)
	config_chipid = BCM43430_CHIP_ID;
#elif defined(BCM43018_CHIP)
	config_chipid = BCM43018_CHIP_ID;
#elif defined(BCM43455_CHIP)
	config_chipid = BCM4345_CHIP_ID;
#elif defined(BCM43454_CHIP)
	config_chipid = BCM43454_CHIP_ID;
#elif defined(BCM43012_CHIP_)
	config_chipid = BCM43012_CHIP_ID;
#elif defined(BCM43013_CHIP)
	config_chipid = BCM43012_CHIP_ID;
#else
	DHD_ERROR(("%s: Unknown chip id, if you use new chipset,"
		" please add CONFIG_BCMXXXX into the Kernel and"
		" BCMXXXX_CHIP definition into the DHD driver\n",
		__FUNCTION__));
	config_chipid = 0;

	return FALSE;
#endif /* BCM4354_CHIP */

#if defined(BCM4354_CHIP) && defined(SUPPORT_MULTIPLE_REVISION)
	if (chipid == BCM4350_CHIP_ID && config_chipid == BCM4354_CHIP_ID) {
		return TRUE;
	}
#endif /* BCM4354_CHIP && SUPPORT_MULTIPLE_REVISION */
#if defined(BCM4358_CHIP) && defined(SUPPORT_MULTIPLE_REVISION)
	if (chipid == BCM43569_CHIP_ID && config_chipid == BCM4358_CHIP_ID) {
		return TRUE;
	}
#endif /* BCM4358_CHIP && SUPPORT_MULTIPLE_REVISION */
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
	dhdp->memdump_type = 0;
	dhdp->dongle_trap_occured = 0;
#if defined(BCMPCIE)
	if (dhdp->extended_trap_data) {
		memset(dhdp->extended_trap_data, 0, BCMPCIE_EXT_TRAP_DATA_MAXLEN);
	}
#endif /* BCMPCIE */
#ifdef DHD_SSSR_DUMP
	/* Flag to indicate sssr dump is collected */
	dhdp->sssr_dump_collected = 0;
#endif /* DHD_SSSR_DUMP */
#ifdef BT_OVER_PCIE
	dhd->pub.dongle_trap_due_to_bt = 0;
#endif /* BT_OVER_PCIE */
	dhdp->iovar_timeout_occured = 0;
#ifdef PCIE_FULL_DONGLE
	dhdp->d3ack_timeout_occured = 0;
	dhdp->livelock_occured = 0;
	dhdp->pktid_audit_failed = 0;
#endif /* PCIE_FULL_DONGLE */
	dhd->pub.iface_op_failed = 0;
	dhd->pub.scan_timeout_occurred = 0;
	dhd->pub.scan_busy_occurred = 0;
	/* Retain BH induced errors and clear induced error during initialize */
	if (dhd->pub.dhd_induce_error) {
		dhd->pub.dhd_induce_bh_error = dhd->pub.dhd_induce_error;
	}
	dhd->pub.dhd_induce_error = DHD_INDUCE_ERROR_CLEAR;
#ifdef DHD_PKTTS
	dhd->latency = 0;
#endif
	dhd->pub.tput_test_done = FALSE;

#if defined(BCMINTERNAL) && defined(BCMPCIE)
	{
		/* JIRA:SW4349-436 JIRA:HW4349-302 Work around for 4349a0 PCIE-D11 DMA bug */
		uint chipid = dhd_bus_chip_id(&dhd->pub);
		uint revid = dhd_bus_chiprev_id(&dhd->pub);

		if ((chipid == BCM4349_CHIP_ID) && (revid == 1)) {
			DHD_INFO(("%s:Detected 4349 A0 enable 16MB Mem restriction Flag",
				__FUNCTION__));
			osl_flag_set(dhd->pub.osh, OSL_PHYS_MEM_LESS_THAN_16MB);
		}
	}
#endif /* BCMINTERNAL && BCMINTERNAL */
	/* try to download image and nvram to the dongle */
	if  (dhd->pub.busstate == DHD_BUS_DOWN && dhd_update_fw_nv_path(dhd)) {
		/* Indicate FW Download has not yet done */
		dhd->pub.fw_download_status = FW_DOWNLOAD_IN_PROGRESS;
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
			return ret;
		}
		/* Indicate FW Download has succeeded */
		dhd->pub.fw_download_status = FW_DOWNLOAD_DONE;
	}
	if (dhd->pub.busstate != DHD_BUS_LOAD) {
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
		return ret;
	}
#if defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID) || defined(BCMPCIE_OOB_HOST_WAKE)
	/* Host registration for OOB interrupt */
	if (dhd_bus_oob_intr_register(dhdp)) {
		/* deactivate timer and wait for the handler to finish */
#if !defined(BCMPCIE_OOB_HOST_WAKE)
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);

#endif /* !BCMPCIE_OOB_HOST_WAKE */
		DHD_STOP_RPM_TIMER(&dhd->pub);

		DHD_ERROR(("%s Host failed to register for OOB\n", __FUNCTION__));
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
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
#endif /* OOB_INTR_ONLY || BCMSPI_ANDROID || BCMPCIE_OOB_HOST_WAKE */
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
			return ret;
		}
	}
#endif /* PCIE_FULL_DONGLE */

	/* set default value for now. Will be updated again in dhd_preinit_ioctls()
	 * after querying FW
	 */
	dhdp->event_log_max_sets = NUM_EVENT_LOG_SETS;
	dhdp->event_log_max_sets_queried = FALSE;

	dhdp->smmu_fault_occurred = 0;
#ifdef DNGL_AXI_ERROR_LOGGING
	dhdp->axi_error = FALSE;
#endif /* DNGL_AXI_ERROR_LOGGING */

	/* Do protocol initialization necessary for IOCTL/IOVAR */
	ret = dhd_prot_init(&dhd->pub);
	if (unlikely(ret) != BCME_OK) {
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
		DHD_STOP_RPM_TIMER(&dhd->pub);
#ifdef BCMSDIO
		dhd_os_sdunlock(dhdp);
#endif /* BCMSDIO */
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
		return -ENODEV;
	}

#ifdef BCMSDIO
	dhd_os_sdunlock(dhdp);
#endif /* BCMSDIO */

	/* Bus is ready, query any dongle information */
	/* XXX Since dhd_sync_with_dongle can sleep, should module count surround it? */
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
		return ret;
	}

#ifdef BT_OVER_PCIE
	/* Enable L1SS of RC and EP */
	dhd_bus_l1ss_enable_rc_ep(dhdp->bus, TRUE);
#endif /* BT_OVER_PCIE */

#if defined(CUSTOMER_HW_ROCKCHIP) && defined(BCMPCIE)
	if (IS_ENABLED(CONFIG_PCIEASPM_ROCKCHIP_WIFI_EXTENSION))
		rk_dhd_bus_l1ss_enable_rc_ep(dhdp->bus, TRUE);
#endif /* CUSTOMER_HW_ROCKCHIP && BCMPCIE */

#if defined(CONFIG_ARCH_EXYNOS) && defined(BCMPCIE)
#if !defined(CONFIG_SOC_EXYNOS8890) && !defined(SUPPORT_EXYNOS7420)
	/* XXX: JIRA SWWLAN-139454: Added L1ss enable
	 * after firmware download completion due to link down issue
	 * JIRA SWWLAN-142236: Amendment - Changed L1ss enable point
	 */
	DHD_ERROR(("%s: Enable L1ss EP side\n", __FUNCTION__));
#if defined(CONFIG_SOC_GS101)
	exynos_pcie_rc_l1ss_ctrl(1, PCIE_L1SS_CTRL_WIFI, 1);
#else
	exynos_pcie_l1ss_ctrl(1, PCIE_L1SS_CTRL_WIFI);
#endif /* CONFIG_SOC_GS101 */
#endif /* !CONFIG_SOC_EXYNOS8890 && !SUPPORT_EXYNOS7420 */
#endif /* CONFIG_ARCH_EXYNOS && BCMPCIE */
#if defined(DHD_DEBUG) && defined(BCMSDIO)
	f2_sync_end = OSL_SYSUPTIME();
	DHD_ERROR(("Time taken for FW download and F2 ready is: %d msec\n",
			(fw_download_end - fw_download_start) + (f2_sync_end - f2_sync_start)));
#endif /* DHD_DEBUG && BCMSDIO */

#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd->pend_ipaddr) {
#ifdef AOE_IP_ALIAS_SUPPORT
		/* XXX Assume pending ip address is belong to primary interface */
		aoe_update_host_ipv4_table(&dhd->pub, dhd->pend_ipaddr, TRUE, 0);
#endif /* AOE_IP_ALIAS_SUPPORT */
		dhd->pend_ipaddr = 0;
	}
#endif /* ARP_OFFLOAD_SUPPORT */

#if defined(BCM_ROUTER_DHD)
	bzero(&dhd->pub.dhd_tm_dwm_tbl, sizeof(dhd_trf_mgmt_dwm_tbl_t));
#endif /* BCM_ROUTER_DHD */
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
	uint32 tdls_pktcnt_high = CUSTOM_TDLS_PCKTCNT_THRESHOLD_HIGH;
	uint32 tdls_pktcnt_low = CUSTOM_TDLS_PCKTCNT_THRESHOLD_LOW;

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
		ret = dhd_iovar(dhd, 0, "tdls_trigger_pktcnt_high", (char *)&tdls_pktcnt_high,
				sizeof(tdls_pktcnt_high), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: tdls_trigger_pktcnt_high failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
		ret = dhd_iovar(dhd, 0, "tdls_trigger_pktcnt_low", (char *)&tdls_pktcnt_low,
				sizeof(tdls_pktcnt_low), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: tdls_trigger_pktcnt_low failed %d\n", __FUNCTION__, ret));
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

	/* No handling needed for peer discovered reason */
	if (reason == WLC_E_TDLS_PEER_DISCOVERED) {
		return BCME_ERROR;
	}
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
		DHD_ERROR(("%s: Add TDLS peer, count=%d " MACDBG "\n",
			__FUNCTION__, dhd_pub->peer_tbl.tdls_peer_count,
			MAC2STRDBG((char *)da)));
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
				DHD_ERROR(("%s: Remove TDLS peer, count=%d " MACDBG "\n",
					__FUNCTION__, dhd_pub->peer_tbl.tdls_peer_count,
					MAC2STRDBG((char *)da)));
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
#endif /* BCMDBUS */

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
#if defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P)
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
#endif /* defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P) */

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
			DHD_ERROR(("Failed to enable AP power save"));
		}
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_pps", (char *)&pps, sizeof(pps), NULL, 0,
				TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to set pps"));
		}
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_quiet_time", (char *)&quiet_time,
				sizeof(quiet_time), NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to set quiet time"));
		}
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_stas_assoc_check",
				(char *)&stas_assoc_check, sizeof(stas_assoc_check), NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to set stas assoc check"));
		}
	} else {
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_enable", (char *)&enable, sizeof(enable),
				NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to disable AP power save"));
		}
	}

	return 0;
}
#endif /* SUPPORT_AP_POWERSAVE */

#if defined(READ_CONFIG_FROM_FILE)
#include <linux/fs.h>
#include <linux/ctype.h>

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
bool PM_control = TRUE;

static int dhd_preinit_proc(dhd_pub_t *dhd, int ifidx, char *name, char *value)
{
	int var_int;
	wl_country_t cspec = {{0}, -1, {0}};
	char *revstr;
	char *endptr = NULL;
#ifdef ROAM_AP_ENV_DETECTION
	int roam_env_mode = AP_ENV_INDETERMINATE;
#endif /* ROAM_AP_ENV_DETECTION */

	if (!strcmp(name, "country")) {
		revstr = strchr(value, '/');
#if defined(DHD_BLOB_EXISTENCE_CHECK)
		if (dhd->is_blob) {
			cspec.rev = 0;
			memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
			memcpy(cspec.ccode, value, WLC_CNTRY_BUF_SZ);
		} else
#endif /* DHD_BLOB_EXISTENCE_CHECK */
		{
			if (revstr) {
				cspec.rev = strtoul(revstr + 1, &endptr, 10);
				memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
				cspec.country_abbrev[2] = '\0';
				memcpy(cspec.ccode, cspec.country_abbrev, WLC_CNTRY_BUF_SZ);
			} else {
				cspec.rev = -1;
				memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
				memcpy(cspec.ccode, value, WLC_CNTRY_BUF_SZ);
				get_customized_country_code(dhd->info->adapter,
						(char *)&cspec.country_abbrev, &cspec);
			}

		}
		DHD_ERROR(("config country code is country : %s, rev : %d !!\n",
			cspec.country_abbrev, cspec.rev));
		return dhd_iovar(dhd, 0, "country", (char*)&cspec, sizeof(cspec), NULL, 0, TRUE);
	} else if (!strcmp(name, "roam_scan_period")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		return dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_SCAN_PERIOD,
			&var_int, sizeof(var_int), TRUE, 0);
	} else if (!strcmp(name, "roam_delta")) {
		struct {
			int val;
			int band;
		} x;
		x.val = (int)simple_strtol(value, NULL, 0);
		/* x.band = WLC_BAND_AUTO; */
		x.band = WLC_BAND_ALL;
		return dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, &x, sizeof(x), TRUE, 0);
	} else if (!strcmp(name, "roam_trigger")) {
		int ret = 0;
		int roam_trigger[2];

		roam_trigger[0] = (int)simple_strtol(value, NULL, 0);
		roam_trigger[1] = WLC_BAND_ALL;
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, &roam_trigger,
			sizeof(roam_trigger), TRUE, 0);

#ifdef ROAM_AP_ENV_DETECTION
		if (roam_trigger[0] == WL_AUTO_ROAM_TRIGGER) {
			if (dhd_iovar(dhd, 0, "roam_env_detection",
			    (char *)&roam_env_mode, sizeof(roam_env_mode), NULL,
			    0, TRUE) == BCME_OK) {
				dhd->roam_env_detection = TRUE;
			} else {
				dhd->roam_env_detection = FALSE;
			}
		}
#endif /* ROAM_AP_ENV_DETECTION */
		return ret;
	} else if (!strcmp(name, "PM")) {
		int ret = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		ret =  dhd_wl_ioctl_cmd(dhd, WLC_SET_PM,
			&var_int, sizeof(var_int), TRUE, 0);

#if defined(DHD_PM_CONTROL_FROM_FILE) || defined(CONFIG_PM_LOCK)
		if (var_int == 0) {
			g_pm_control = TRUE;
			printk("%s var_int=%d don't control PM\n", __func__, var_int);
		} else {
			g_pm_control = FALSE;
			printk("%s var_int=%d do control PM\n", __func__, var_int);
		}
#endif

		return ret;
	}
	else if (!strcmp(name, "band")) {
		int ret;
		if (!strcmp(value, "auto"))
			var_int = WLC_BAND_AUTO;
		else if (!strcmp(value, "a"))
			var_int = WLC_BAND_5G;
		else if (!strcmp(value, "b"))
			var_int = WLC_BAND_2G;
		else if (!strcmp(value, "all"))
			var_int = WLC_BAND_ALL;
		else {
			printk(" set band value should be one of the a or b or all\n");
			var_int = WLC_BAND_AUTO;
		}
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, &var_int,
			sizeof(var_int), TRUE, 0)) < 0)
			printk(" set band err=%d\n", ret);
		return ret;
	} else if (!strcmp(name, "cur_etheraddr")) {
		struct ether_addr ea;
		int ret;

		bcm_ether_atoe(value, &ea);

		ret = memcmp(&ea.octet, dhd->mac.octet, ETHER_ADDR_LEN);
		if (ret == 0) {
			DHD_ERROR(("%s: Same Macaddr\n", __FUNCTION__));
			return 0;
		}

		DHD_ERROR(("%s: Change Macaddr = %02X:%02X:%02X:%02X:%02X:%02X\n", __FUNCTION__,
			ea.octet[0], ea.octet[1], ea.octet[2],
			ea.octet[3], ea.octet[4], ea.octet[5]));

		ret = dhd_iovar(dhd, 0, "cur_etheraddr", (char*)&ea, ETHER_ADDR_LEN, NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			return ret;
		} else {
			memcpy(dhd->mac.octet, (void *)&ea, ETHER_ADDR_LEN);
			return ret;
		}
	} else if (!strcmp(name, "lpc")) {
		int ret = 0;
		var_int = (int)simple_strtol(value, NULL, 0);
		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "lpc", (char *)&var_int, sizeof(var_int), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set lpc failed  %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcmp(name, "vht_features")) {
		int ret = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "vht_features", (char *)&var_int, sizeof(var_int), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set vht_features failed  %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	} else {
		/* wlu_iovar_setint */
		var_int = (int)simple_strtol(value, NULL, 0);

		/* Setup timeout bcm_timeout from dhd driver 4.217.48 */

		DHD_INFO(("%s:[%s]=[%d]\n", __FUNCTION__, name, var_int));

		return dhd_iovar(dhd, 0, name, (char *)&var_int,
				sizeof(var_int), NULL, 0, TRUE);
	}

	return 0;
}

static int dhd_preinit_config(dhd_pub_t *dhd, int ifidx)
{
	mm_segment_t old_fs;
	struct kstat stat;
	struct file *fp = NULL;
	unsigned int len;
	char *buf = NULL, *p, *name, *value;
	int ret = 0;
	char *config_path;

	config_path = CONFIG_BCMDHD_CONFIG_PATH;

	if (!config_path)
	{
		printk(KERN_ERR "config_path can't read. \n");
		return 0;
	}

	old_fs = get_fs();
	set_fs(get_ds());
	if ((ret = vfs_stat(config_path, &stat))) {
		set_fs(old_fs);
		printk(KERN_ERR "%s: Failed to get information (%d)\n",
			config_path, ret);
		return ret;
	}
	set_fs(old_fs);

	if (!(buf = MALLOC(dhd->osh, stat.size + 1))) {
		printk(KERN_ERR "Failed to allocate memory %llu bytes\n", stat.size);
		return -ENOMEM;
	}
	memset(buf, 0x0, stat.size + 1);
	printk("dhd_preinit_config : config path : %s \n", config_path);

	if (!(fp = dhd_os_open_image1(dhd, config_path)) ||
		(len = dhd_os_get_image_block(buf, stat.size, fp)) < 0)
		goto err;

	if (len != stat.size) {
		printk("dhd_preinit_config : Error - read length mismatched len = %d\n", len);
		goto err;
	}

	buf[stat.size] = '\0';
	for (p = buf; *p; p++) {
		if (isspace(*p))
			continue;
		for (name = p++; *p && !isspace(*p); p++) {
			if (*p == '=') {
				*p = '\0';
				p++;
				for (value = p; *p && !isspace(*p); p++);
				*p = '\0';
				if ((ret = dhd_preinit_proc(dhd, ifidx, name, value)) < 0) {
					printk(KERN_ERR "%s: %s=%s\n",
						bcmerrorstr(ret), name, value);
				}
				break;
			}
		}
	}
	ret = 0;

out:
	if (fp)
		dhd_os_close_image1(dhd, fp);
	if (buf)
		MFREE(dhd->osh, buf, stat.size+1);
	return ret;

err:
	ret = -1;
	goto out;
}
#endif /* READ_CONFIG_FROM_FILE */

#ifdef WLAIBSS
int
dhd_preinit_aibss_ioctls(dhd_pub_t *dhd, char *iov_buf_smlen)
{
	int ret = BCME_OK;
	aibss_bcn_force_config_t bcn_config;
	uint32 aibss;
#ifdef WLAIBSS_PS
	uint32 aibss_ps;
	s32 atim;
#endif /* WLAIBSS_PS */
	int ibss_coalesce;

	aibss = 1;
	ret = dhd_iovar(dhd, 0, "aibss", (char *)&aibss, sizeof(aibss), NULL, 0, TRUE);
	if (ret < 0) {
		if (ret == BCME_UNSUPPORTED) {
			DHD_ERROR(("%s aibss , UNSUPPORTED\n", __FUNCTION__));
			return BCME_OK;
		} else {
			DHD_ERROR(("%s Set aibss to %d err(%d)\n", __FUNCTION__, aibss, ret));
			return ret;
		}
	}

#ifdef WLAIBSS_PS
	aibss_ps = 1;
	ret = dhd_iovar(dhd, 0, "aibss_ps", (char *)&aibss_ps, sizeof(aibss_ps), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set aibss PS to %d failed  %d\n",
			__FUNCTION__, aibss, ret));
		return ret;
	}

	atim = 10;
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ATIM,
		(char *)&atim, sizeof(atim), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Enable custom IBSS ATIM mode failed %d\n",
			__FUNCTION__, ret));
		return ret;
	}
#endif /* WLAIBSS_PS */

	memset(&bcn_config, 0, sizeof(bcn_config));
	bcn_config.initial_min_bcn_dur = AIBSS_INITIAL_MIN_BCN_DUR;
	bcn_config.min_bcn_dur = AIBSS_MIN_BCN_DUR;
	bcn_config.bcn_flood_dur = AIBSS_BCN_FLOOD_DUR;
	bcn_config.version = AIBSS_BCN_FORCE_CONFIG_VER_0;
	bcn_config.len = sizeof(bcn_config);

	ret = dhd_iovar(dhd, 0, "aibss_bcn_force_config", (char *)&bcn_config,
			sizeof(aibss_bcn_force_config_t), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set aibss_bcn_force_config to %d, %d, %d failed %d\n",
			__FUNCTION__, AIBSS_INITIAL_MIN_BCN_DUR, AIBSS_MIN_BCN_DUR,
			AIBSS_BCN_FLOOD_DUR, ret));
		return ret;
	}

	ibss_coalesce = IBSS_COALESCE_DEFAULT;
	ret = dhd_iovar(dhd, 0, "ibss_coalesce_allowed", (char *)&ibss_coalesce,
			sizeof(ibss_coalesce), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set ibss_coalesce_allowed failed  %d\n",
			__FUNCTION__, ret));
		return ret;
	}

	dhd->op_mode |= DHD_FLAG_IBSS_MODE;
	return BCME_OK;
}
#endif /* WLAIBSS */

#if defined(WLADPS) || defined(WLADPS_PRIVATE_CMD)
#ifdef WL_BAM
static int
dhd_check_adps_bad_ap(dhd_pub_t *dhd)
{
	struct net_device *ndev;
	struct bcm_cfg80211 *cfg;
	struct wl_profile *profile;
	struct ether_addr bssid;

	if (!dhd_is_associated(dhd, 0, NULL)) {
		DHD_ERROR(("%s - not associated\n", __FUNCTION__));
		return BCME_OK;
	}

	ndev = dhd_linux_get_primary_netdev(dhd);
	if (!ndev) {
		DHD_ERROR(("%s: Cannot find primary netdev\n", __FUNCTION__));
		return -ENODEV;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		DHD_ERROR(("%s: Cannot find cfg\n", __FUNCTION__));
		return -EINVAL;
	}

	profile = wl_get_profile_by_netdev(cfg, ndev);
	memcpy(bssid.octet, profile->bssid, ETHER_ADDR_LEN);
	if (wl_adps_bad_ap_check(cfg, &bssid)) {
		if (wl_adps_enabled(cfg, ndev)) {
			wl_adps_set_suspend(cfg, ndev, ADPS_SUSPEND);
		}
	}

	return BCME_OK;
}
#endif	/* WL_BAM */

int
dhd_enable_adps(dhd_pub_t *dhd, uint8 on)
{
	int i;
	int len;
	int ret = BCME_OK;

	bcm_iov_buf_t *iov_buf = NULL;
	wl_adps_params_v1_t *data = NULL;

	len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(*data);
	iov_buf = MALLOC(dhd->osh, len);
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
		ret = dhd_iovar(dhd, 0, "adps", (char *)iov_buf, len, NULL, 0, TRUE);
		if (ret < 0) {
			if (ret == BCME_UNSUPPORTED) {
				DHD_ERROR(("%s adps, UNSUPPORTED\n", __FUNCTION__));
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

#ifdef WL_BAM
	if (on) {
		dhd_check_adps_bad_ap(dhd);
	}
#endif	/* WL_BAM */

exit:
	if (iov_buf) {
		MFREE(dhd->osh, iov_buf, len);
	}
	return ret;
}
#endif /* WLADPS || WLADPS_PRIVATE_CMD */

int
dhd_get_preserve_log_numbers(dhd_pub_t *dhd, uint32 *logset_mask)
{
	wl_el_set_type_t logset_type, logset_op;
	wl_el_set_all_type_v1_t *logset_all_type_op = NULL;
	bool use_logset_all_type = FALSE;
	int ret = BCME_ERROR;
	int err = 0;
	uint8 i = 0;
	int el_set_all_type_len;

	if (!dhd || !logset_mask)
		return BCME_BADARG;

	el_set_all_type_len = OFFSETOF(wl_el_set_all_type_v1_t, set_type) +
		(sizeof(wl_el_set_type_v1_t) * dhd->event_log_max_sets);

	logset_all_type_op = (wl_el_set_all_type_v1_t *) MALLOC(dhd->osh, el_set_all_type_len);
	if (logset_all_type_op == NULL) {
		DHD_ERROR(("%s: failed to allocate %d bytes for logset_all_type_op\n",
			__FUNCTION__, el_set_all_type_len));
		return BCME_NOMEM;
	}

	*logset_mask = 0;
	memset(&logset_type, 0, sizeof(logset_type));
	memset(&logset_op, 0, sizeof(logset_op));
	logset_type.version = htod16(EVENT_LOG_SET_TYPE_CURRENT_VERSION);
	logset_type.len = htod16(sizeof(wl_el_set_type_t));

	/* Try with set = event_log_max_sets, if fails, use legacy event_log_set_type */
	logset_type.set = dhd->event_log_max_sets;
	err = dhd_iovar(dhd, 0, "event_log_set_type", (char *)&logset_type, sizeof(logset_type),
		(char *)logset_all_type_op, el_set_all_type_len, FALSE);
	if (err == BCME_OK) {
		DHD_ERROR(("%s: use optimised use_logset_all_type\n", __FUNCTION__));
		use_logset_all_type = TRUE;
	}

	for (i = 0; i < dhd->event_log_max_sets; i++) {
		if (use_logset_all_type) {
			logset_op.type = logset_all_type_op->set_type[i].type_val;
		} else {
			logset_type.set = i;
			err = dhd_iovar(dhd, 0, "event_log_set_type", (char *)&logset_type,
				sizeof(logset_type), (char *)&logset_op, sizeof(logset_op), FALSE);
		}
		/* the iovar may return 'unsupported' error if a log set number is not present
		* in the fw, so we should not return on error !
		*/
		if (err == BCME_OK &&
				logset_op.type == EVENT_LOG_SET_TYPE_PRSRV) {
			*logset_mask |= 0x01u << i;
			ret = BCME_OK;
			DHD_ERROR(("[INIT] logset:%d is preserve/chatty\n", i));
		}
	}

	MFREE(dhd->osh, logset_all_type_op, el_set_all_type_len);
	return ret;
}

#ifndef OEM_ANDROID
/* For non-android FC modular builds, override firmware preinited values */
void
dhd_override_fwprenit(dhd_pub_t * dhd)
{
	int ret = 0;

	{
		/* Disable bcn_li_bcn */
		uint32 bcn_li_bcn = 0;
		ret = dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn,
				sizeof(bcn_li_bcn), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: bcn_li_bcn failed:%d\n",
				__FUNCTION__, ret));
		}
	}

	{
		/* Disable apsta */
		uint32 apsta = 0;
		ret = dhd_iovar(dhd, 0, "apsta", (char *)&apsta,
				sizeof(apsta), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: apsta failed:%d\n",
				__FUNCTION__, ret));
		}
	}

	{
		int ap_mode = 0;
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_AP, (char *)&ap_mode,
			sizeof(ap_mode), TRUE, 0)) < 0) {
			DHD_ERROR(("%s: set apmode failed :%d\n", __FUNCTION__, ret));
		}
	}
}
#endif /* !OEM_ANDROID */

int
dhd_get_fw_capabilities(dhd_pub_t * dhd)
{

	int ret = 0;
	uint32 cap_buf_size = sizeof(dhd->fw_capabilities);
	memset(dhd->fw_capabilities, 0, cap_buf_size);
	ret = dhd_iovar(dhd, 0, "cap", NULL, 0, dhd->fw_capabilities, (cap_buf_size - 1),
		FALSE);

	if (ret < 0) {
		DHD_ERROR(("%s: Get Capability failed (error=%d)\n",
		__FUNCTION__, ret));
		return ret;
	}

	memmove(&dhd->fw_capabilities[1], dhd->fw_capabilities, (cap_buf_size - 1));
	dhd->fw_capabilities[0] = ' ';
	dhd->fw_capabilities[cap_buf_size - 2] = ' ';
	dhd->fw_capabilities[cap_buf_size - 1] = '\0';

	return 0;
}

int
dhd_optimised_preinit_ioctls(dhd_pub_t * dhd)
{
	int ret = 0;
	/*  Room for "event_msgs_ext" + '\0' + bitvec  */
	char iovbuf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE + 16];
#ifdef DHD_PKTTS
	uint32 val = 0;
#endif
	uint32 event_log_max_sets = 0;
	char* iov_buf = NULL;
	/* XXX: Use ret2 for return check of IOVARS that might return BCME_UNSUPPORTED,
	*	based on FW build tag.
	*/
	int ret2 = 0;
#if defined(WL_MONITOR) && defined(HOST_RADIOTAP_CONV)
	uint monitor = 0;
	dhd_info_t *dhdinfo = (dhd_info_t*)dhd->info;
#endif /* WL_MONITOR */
#if defined(BCMSUP_4WAY_HANDSHAKE)
	uint32 sup_wpa = 1;
#endif /* BCMSUP_4WAY_HANDSHAKE */

	uint32 frameburst = CUSTOM_FRAMEBURST_SET;
	uint wnm_bsstrans_resp = 0;
#ifdef DHD_BUS_MEM_ACCESS
	uint32 enable_memuse = 1;
#endif /* DHD_BUS_MEM_ACCESS */
#ifdef DHD_PM_CONTROL_FROM_FILE
	uint power_mode = PM_FAST;
#endif /* DHD_PM_CONTROL_FROM_FILE */
	char buf[WLC_IOCTL_SMLEN];
	char *ptr;
#ifdef ROAM_ENABLE
	uint roamvar = 0;
#ifdef ROAM_AP_ENV_DETECTION
	int roam_env_mode = 0;
#endif /* ROAM_AP_ENV_DETECTION */
#endif /* ROAM_ENABLE */
#if defined(SOFTAP)
	uint dtim = 1;
#endif
/* xxx andrey tmp fix for dk8000 build error  */
	struct ether_addr p2p_ea;
#ifdef GET_CUSTOM_MAC_ENABLE
	struct ether_addr ea_addr;
#endif /* GET_CUSTOM_MAC_ENABLE */
#ifdef BCMPCIE_OOB_HOST_WAKE
	uint32 hostwake_oob = 0;
#endif /* BCMPCIE_OOB_HOST_WAKE */
	wl_wlc_version_t wlc_ver;

#if defined(WBTEXT) && defined(RRM_BCNREQ_MAX_CHAN_TIME)
	uint32 rrm_bcn_req_thrtl_win = RRM_BCNREQ_MAX_CHAN_TIME * 2;
	uint32 rrm_bcn_req_max_off_chan_time = RRM_BCNREQ_MAX_CHAN_TIME;
#endif /* WBTEXT && RRM_BCNREQ_MAX_CHAN_TIME */
#ifdef PKT_FILTER_SUPPORT
	dhd_pkt_filter_enable = TRUE;
#ifdef APF
	dhd->apf_set = FALSE;
#endif /* APF */
#endif /* PKT_FILTER_SUPPORT */
	dhd->suspend_bcn_li_dtim = CUSTOM_SUSPEND_BCN_LI_DTIM;
#ifdef ENABLE_MAX_DTIM_IN_SUSPEND
	dhd->max_dtim_enable = TRUE;
#else
	dhd->max_dtim_enable = FALSE;
#endif /* ENABLE_MAX_DTIM_IN_SUSPEND */
	dhd->disable_dtim_in_suspend = FALSE;
#ifdef CUSTOM_SET_OCLOFF
	dhd->ocl_off = FALSE;
#endif /* CUSTOM_SET_OCLOFF */
#ifdef SUPPORT_SET_TID
	dhd->tid_mode = SET_TID_OFF;
	dhd->target_uid = 0;
	dhd->target_tid = 0;
#endif /* SUPPORT_SET_TID */
	DHD_TRACE(("Enter %s\n", __FUNCTION__));
	dhd->op_mode = 0;

#ifdef ARP_OFFLOAD_SUPPORT
	/* arpoe will be applied from the supsend context */
	dhd->arpoe_enable = TRUE;
	dhd->arpol_configured = FALSE;
#endif /* ARP_OFFLOAD_SUPPORT */

	/* clear AP flags */
#if defined(CUSTOM_COUNTRY_CODE)
	dhd->dhd_cflags &= ~WLAN_PLAT_AP_FLAG;
#endif /* CUSTOM_COUNTRY_CODE */

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

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	ret = dhd_iovar(dhd, 0, "ver", NULL, 0, (char *)&buf, sizeof(buf), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		bcmstrtok(&ptr, "\n", 0);
		/* Print fw version info */
		DHD_ERROR(("Firmware version = %s\n", buf));
		strncpy(fw_version, buf, FW_VER_STR_LEN);
		fw_version[FW_VER_STR_LEN-1] = '\0';
#if defined(BCMSDIO) || defined(BCMPCIE)
		dhd_set_version_info(dhd, buf);
#endif /* BCMSDIO || BCMPCIE */
	}

	/* query for 'wlc_ver' to get version info from firmware */
	/* memsetting to zero */
	memset_s(&wlc_ver, sizeof(wl_wlc_version_t), 0,
		sizeof(wl_wlc_version_t));
	ret = dhd_iovar(dhd, 0, "wlc_ver", NULL, 0, (char *)&wlc_ver,
			sizeof(wl_wlc_version_t), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		dhd->wlc_ver_major = wlc_ver.wlc_ver_major;
		dhd->wlc_ver_minor = wlc_ver.wlc_ver_minor;
	}
#ifdef BOARD_HIKEY
	/* Set op_mode as MFG_MODE if WLTEST is present in "wl ver" */
	if (strstr(fw_version, "WLTEST") != NULL) {
		DHD_ERROR(("%s: wl ver has WLTEST, setting op_mode as DHD_FLAG_MFG_MODE\n",
			__FUNCTION__));
		op_mode = DHD_FLAG_MFG_MODE;
	}
#endif /* BOARD_HIKEY */
	/* get a capabilities from firmware */
	ret = dhd_get_fw_capabilities(dhd);

	if (ret < 0) {
		DHD_ERROR(("%s: Get Capability failed (error=%d)\n",
			__FUNCTION__, ret));
		goto done;
	}

	if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
		dhd->op_mode = DHD_FLAG_MFG_MODE;
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
		/* disable runtimePM by default in MFG mode. */
		pm_runtime_disable(dhd_bus_to_dev(dhd->bus));
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
#ifdef DHD_PCIE_RUNTIMEPM
		/* Disable RuntimePM in mfg mode */
		DHD_DISABLE_RUNTIME_PM(dhd);
		DHD_ERROR(("%s : Disable RuntimePM in Manufactring Firmware\n", __FUNCTION__));
#endif /* DHD_PCIE_RUNTIME_PM */
		/* Check and adjust IOCTL response timeout for Manufactring firmware */
		dhd_os_set_ioctl_resp_timeout(MFG_IOCTL_RESP_TIMEOUT);
		DHD_ERROR(("%s : Set IOCTL response time for Manufactring Firmware\n",
			__FUNCTION__));

#if defined(ARP_OFFLOAD_SUPPORT)
		dhd->arpoe_enable = FALSE;
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef PKT_FILTER_SUPPORT
		dhd_pkt_filter_enable = FALSE;
#endif /* PKT_FILTER_SUPPORT */
#ifndef CUSTOM_SET_ANTNPM
		if (FW_SUPPORTED(dhd, rsdb)) {
			wl_config_t rsdb_mode;
			memset(&rsdb_mode, 0, sizeof(rsdb_mode));
			ret = dhd_iovar(dhd, 0, "rsdb_mode", (char *)&rsdb_mode, sizeof(rsdb_mode),
				NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s Disable rsdb_mode is failed ret= %d\n",
					__FUNCTION__, ret));
			}
		}
#endif /* !CUSTOM_SET_ANTNPM */
	} else {
		uint32 concurrent_mode = 0;
		dhd_os_set_ioctl_resp_timeout(IOCTL_RESP_TIMEOUT);
		DHD_INFO(("%s : Set IOCTL response time.\n", __FUNCTION__));

		BCM_REFERENCE(concurrent_mode);

		dhd->op_mode = DHD_FLAG_STA_MODE;

		BCM_REFERENCE(p2p_ea);
#if defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P)
		if ((concurrent_mode = dhd_get_concurrent_capabilites(dhd))) {
			dhd->op_mode |= concurrent_mode;
		}

		/* Check if we are enabling p2p */
		if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
			memcpy(&p2p_ea, &dhd->mac, ETHER_ADDR_LEN);
			ETHER_SET_LOCALADDR(&p2p_ea);
			ret = dhd_iovar(dhd, 0, "p2p_da_override", (char *)&p2p_ea, sizeof(p2p_ea),
					NULL, 0, TRUE);
			if (ret < 0)
				DHD_ERROR(("%s p2p_da_override ret= %d\n", __FUNCTION__, ret));
			else
				DHD_INFO(("dhd_preinit_ioctls: p2p_da_override succeeded\n"));
		}
#endif /* defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P) */

	}

#ifdef BCMPCIE_OOB_HOST_WAKE
	ret = dhd_iovar(dhd, 0, "bus:hostwake_oob", NULL, 0, (char *)&hostwake_oob,
		sizeof(hostwake_oob), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: hostwake_oob IOVAR not present, proceed\n", __FUNCTION__));
	} else {
		if (hostwake_oob == 0) {
			DHD_ERROR(("%s: hostwake_oob is not enabled in the NVRAM, STOP\n",
				__FUNCTION__));
			ret = BCME_UNSUPPORTED;
			goto done;
		} else {
			DHD_ERROR(("%s: hostwake_oob enabled\n", __FUNCTION__));
		}
	}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef DNGL_AXI_ERROR_LOGGING
	ret = dhd_iovar(dhd, 0, "axierror_logbuf_addr", NULL, 0, (char *)&dhd->axierror_logbuf_addr,
		sizeof(dhd->axierror_logbuf_addr), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: axierror_logbuf_addr IOVAR not present, proceed\n", __FUNCTION__));
		dhd->axierror_logbuf_addr = 0;
	} else {
		DHD_ERROR(("%s: axierror_logbuf_addr : 0x%x\n",
			__FUNCTION__, dhd->axierror_logbuf_addr));
	}
#endif /* DNGL_AXI_ERROR_LOGGING */

#ifdef GET_CUSTOM_MAC_ENABLE
	ret = wifi_platform_get_mac_addr(dhd->info->adapter, ea_addr.octet, 0);
	if (!ret) {
		ret = dhd_iovar(dhd, 0, "cur_etheraddr", (char *)&ea_addr, ETHER_ADDR_LEN, NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			ret = BCME_NOTUP;
			goto done;
		}
		memcpy(dhd->mac.octet, ea_addr.octet, ETHER_ADDR_LEN);
	} else
#endif /* GET_CUSTOM_MAC_ENABLE */
	{
		/* Get the default device MAC address directly from firmware */
		ret = dhd_iovar(dhd, 0, "cur_etheraddr", NULL, 0, (char *)&buf, sizeof(buf), FALSE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
			ret = BCME_NOTUP;
			goto done;
		}

		DHD_ERROR(("%s: use firmware generated mac_address "MACDBG"\n",
			__FUNCTION__, MAC2STRDBG(&buf)));

#ifdef MACADDR_PROVISION_ENFORCED
		if (ETHER_IS_LOCALADDR(buf)) {
			DHD_ERROR(("%s: error! not using provision mac addr!\n", __FUNCTION__));
			ret = BCME_BADADDR;
			goto done;
		}
#endif /* MACADDR_PROVISION_ENFORCED */

		/* Update public MAC address after reading from Firmware */
		memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);
	}

	if (ETHER_ISNULLADDR(dhd->mac.octet)) {
		DHD_ERROR(("%s: NULL MAC address during pre-init\n", __FUNCTION__));
		ret = BCME_BADADDR;
		goto done;
	} else {
		(void)memcpy_s(dhd_linux_get_primary_netdev(dhd)->perm_addr, ETHER_ADDR_LEN,
			dhd->mac.octet, ETHER_ADDR_LEN);
	}

	if ((ret = dhd_apply_default_clm(dhd, dhd->clm_path)) < 0) {
		DHD_ERROR(("%s: CLM set failed. Abort initialization.\n", __FUNCTION__));
		goto done;
	}

	DHD_ERROR(("Firmware up: op_mode=0x%04x, MAC="MACDBG"\n",
		dhd->op_mode, MAC2STRDBG(dhd->mac.octet)));
#if defined(DHD_BLOB_EXISTENCE_CHECK)
	if (!dhd->is_blob)
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

#if defined(ROAM_ENABLE)
	BCM_REFERENCE(roamvar);
#ifdef USE_WFA_CERT_CONF
	if (sec_get_param_wfa_cert(dhd, SET_PARAM_ROAMOFF, &roamvar) == BCME_OK) {
		DHD_ERROR(("%s: read roam_off param =%d\n", __FUNCTION__, roamvar));
	}
	/* roamvar is set to 0 by preinit fw, change only if roamvar is non-zero */
	if (roamvar != 0) {
		/* Disable built-in roaming to allowed ext supplicant to take care of roaming */
		ret = dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar), NULL, 0,
			TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s roam_off failed %d\n", __FUNCTION__, ret));
		}
	}
#endif /* USE_WFA_CERT_CONF */

#ifdef ROAM_AP_ENV_DETECTION
	/* Changed to GET iovar to read roam_env_mode */
	dhd->roam_env_detection = FALSE;
	ret = dhd_iovar(dhd, 0, "roam_env_detection", NULL, 0, (char *)&roam_env_mode,
			sizeof(roam_env_mode), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: roam_env_detection IOVAR not present\n", __FUNCTION__));
	} else {
		if (roam_env_mode == AP_ENV_INDETERMINATE) {
			dhd->roam_env_detection = TRUE;
		}
	}
#endif /* ROAM_AP_ENV_DETECTION */
#ifdef CONFIG_ROAM_RSSI_LIMIT
	ret = dhd_roam_rssi_limit_set(dhd, CUSTOM_ROAMRSSI_2G, CUSTOM_ROAMRSSI_5G);
	if (ret < 0) {
		DHD_ERROR(("%s set roam_rssi_limit failed ret %d\n", __FUNCTION__, ret));
	}
#endif /* CONFIG_ROAM_RSSI_LIMIT */
#ifdef CONFIG_ROAM_MIN_DELTA
	ret = dhd_roam_min_delta_set(dhd, CUSTOM_ROAM_MIN_DELTA, CUSTOM_ROAM_MIN_DELTA);
	if (ret < 0) {
		DHD_ERROR(("%s set roam_min_delta failed ret %d\n", __FUNCTION__, ret));
	}
#endif /* CONFIG_ROAM_MIN_DELTA */
#endif /* ROAM_ENABLE */

#ifdef WLTDLS
	dhd->tdls_enable = FALSE;
	/* query tdls_eable */
	ret = dhd_iovar(dhd, 0, "tdls_enable", NULL, 0, (char *)&dhd->tdls_enable,
		sizeof(dhd->tdls_enable), FALSE);
	DHD_ERROR(("%s: tdls_enable=%d ret=%d\n", __FUNCTION__, dhd->tdls_enable, ret));
#endif /* WLTDLS */

#ifdef DHD_PM_CONTROL_FROM_FILE
#ifdef CUSTOMER_HW10
	dhd_control_pm(dhd, &power_mode);
#else
	sec_control_pm(dhd, &power_mode);
#endif /* CUSTOMER_HW10 */
#endif /* DHD_PM_CONTROL_FROM_FILE */

#ifdef MIMO_ANT_SETTING
	dhd_sel_ant_from_file(dhd);
#endif /* MIMO_ANT_SETTING */

#if defined(OEM_ANDROID) && defined(SOFTAP)
	if (ap_fw_loaded == TRUE) {
		dhd_wl_ioctl_cmd(dhd, WLC_SET_DTIMPRD, (char *)&dtim, sizeof(dtim), TRUE, 0);
	}
#endif /* defined(OEM_ANDROID) && defined(SOFTAP) */

#if defined(KEEP_ALIVE)
	/* Set Keep Alive : be sure to use FW with -keepalive */
	if (!(dhd->op_mode &
		(DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))) {
		if ((ret = dhd_keep_alive_onoff(dhd)) < 0)
			DHD_ERROR(("%s set keeplive failed %d\n",
			__FUNCTION__, ret));
	}
#endif /* defined(KEEP_ALIVE) */

	ret = dhd_iovar(dhd, 0, "event_log_max_sets", NULL, 0, (char *)&event_log_max_sets,
		sizeof(event_log_max_sets), FALSE);
	if (ret == BCME_OK) {
		dhd->event_log_max_sets = event_log_max_sets;
	} else {
		dhd->event_log_max_sets = NUM_EVENT_LOG_SETS;
	}
	BCM_REFERENCE(iovbuf);
	/* Make sure max_sets is set first with wmb and then sets_queried,
	 * this will be used during parsing the logsets in the reverse order.
	 */
	OSL_SMP_WMB();
	dhd->event_log_max_sets_queried = TRUE;
	DHD_ERROR(("%s: event_log_max_sets: %d ret: %d\n",
		__FUNCTION__, dhd->event_log_max_sets, ret));
#ifdef DHD_BUS_MEM_ACCESS
	ret = dhd_iovar(dhd, 0, "enable_memuse", (char *)&enable_memuse,
			sizeof(enable_memuse), iovbuf, sizeof(iovbuf), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: enable_memuse is failed ret=%d\n",
			__FUNCTION__, ret));
	} else {
		DHD_ERROR(("%s: enable_memuse = %d\n",
			__FUNCTION__, enable_memuse));
	}
#endif /* DHD_BUS_MEM_ACCESS */

#ifdef USE_WFA_CERT_CONF
#ifdef USE_WL_FRAMEBURST
	 if (sec_get_param_wfa_cert(dhd, SET_PARAM_FRAMEBURST, &frameburst) == BCME_OK) {
		DHD_ERROR(("%s, read frameburst param=%d\n", __FUNCTION__, frameburst));
	 }
#endif /* USE_WL_FRAMEBURST */
	 g_frameburst = frameburst;
#endif /* USE_WFA_CERT_CONF */

#ifdef DISABLE_WL_FRAMEBURST_SOFTAP
	/* Disable Framebursting for SofAP */
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		frameburst = 0;
	}
#endif /* DISABLE_WL_FRAMEBURST_SOFTAP */

	BCM_REFERENCE(frameburst);
#if defined(USE_WL_FRAMEBURST) || defined(DISABLE_WL_FRAMEBURST_SOFTAP)
	/* frameburst is set to 1 by preinit fw, change if otherwise */
	if (frameburst != 1) {
		/* Set frameburst to value */
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_FAKEFRAG, (char *)&frameburst,
			sizeof(frameburst), TRUE, 0)) < 0) {
			DHD_INFO(("%s frameburst not supported	%d\n", __FUNCTION__, ret));
		}
	}
#endif /* USE_WL_FRAMEBURST || DISABLE_WL_FRAMEBURST_SOFTAP */

	iov_buf = (char*)MALLOC(dhd->osh, WLC_IOCTL_SMLEN);
	if (iov_buf == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for iov_buf\n", WLC_IOCTL_SMLEN));
		ret = BCME_NOMEM;
		goto done;
	}

#if defined(BCMSUP_4WAY_HANDSHAKE)
	/* Read 4-way handshake requirements */
	if (dhd_use_idsup == 1) {
		ret = dhd_iovar(dhd, 0, "sup_wpa", (char *)&sup_wpa, sizeof(sup_wpa),
				(char *)&iovbuf, sizeof(iovbuf), FALSE);
		/* sup_wpa iovar returns NOTREADY status on some platforms using modularized
		 * in-dongle supplicant.
		 */
		if (ret >= 0 || ret == BCME_NOTREADY)
			dhd->fw_4way_handshake = TRUE;
		DHD_TRACE(("4-way handshake mode is: %d\n", dhd->fw_4way_handshake));
	}
#endif /* BCMSUP_4WAY_HANDSHAKE */

#if defined(PCIE_FULL_DONGLE) && defined(DHD_LOSSLESS_ROAMING)
	dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_LLR_MAP);
#endif /* defined(PCIE_FULL_DONGLE) && defined(DHD_LOSSLESS_ROAMING) */

#if defined(BCMPCIE) && defined(EAPOL_PKT_PRIO)
	dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_LLR_MAP);
#endif /* defined(BCMPCIE) && defined(EAPOL_PKT_PRIO) */

#ifdef ARP_OFFLOAD_SUPPORT
	DHD_ERROR(("arp_enable:%d arp_ol:%d\n",
		dhd->arpoe_enable, dhd->arpol_configured));
#endif /* ARP_OFFLOAD_SUPPORT */
	/*
	 * Retaining pktfilter fotr temporary, once fw preinit includes this,
	 * this will be removed. Caution is to skip the pktfilter check during
	 * each pktfilter removal.
	 */
#ifdef PKT_FILTER_SUPPORT
	/* Setup default defintions for pktfilter , enable in suspend */
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

#ifdef BLOCK_IPV6_PACKET
	/* Setup filter to allow only IPv4 unicast frames */
	dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 "
		HEX_PREF_STR UNI_FILTER_STR ZERO_ADDR_STR ETHER_TYPE_STR IPV6_FILTER_STR
		" "
		HEX_PREF_STR ZERO_ADDR_STR ZERO_ADDR_STR ETHER_TYPE_STR ZERO_TYPE_STR;
#else
	/* Setup filter to allow only unicast */
	dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 0x01 0x00";
#endif /* BLOCK_IPV6_PACKET */

#ifdef PASS_IPV4_SUSPEND
	/* XXX customer want to get IPv4 multicast packets */
	dhd->pktfilter[DHD_MDNS_FILTER_NUM] = "104 0 0 0 0xFFFFFF 0x01005E";
#else
	/* Add filter to pass multicastDNS packet and NOT filter out as Broadcast */
	dhd->pktfilter[DHD_MDNS_FILTER_NUM] = NULL;
#endif /* PASS_IPV4_SUSPEND */
	if (FW_SUPPORTED(dhd, pf6)) {
		/* Immediately pkt filter TYPE 6 Dicard Broadcast IP packet */
		dhd->pktfilter[DHD_IP4BCAST_DROP_FILTER_NUM] = DISCARD_IPV4_BCAST;
		/* Immediately pkt filter TYPE 6 Dicard Cisco STP packet */
		dhd->pktfilter[DHD_LLC_STP_DROP_FILTER_NUM] = DISCARD_LLC_STP;
		/* Immediately pkt filter TYPE 6 Dicard Cisco XID protocol */
		dhd->pktfilter[DHD_LLC_XID_DROP_FILTER_NUM] = DISCARD_LLC_XID;
		/* Immediately pkt filter TYPE 6 Dicard NETBIOS packet(port 137) */
		dhd->pktfilter[DHD_UDPNETBIOS_DROP_FILTER_NUM] = DISCARD_UDPNETBIOS;
		dhd->pktfilter_count = 11;
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

#if defined(SOFTAP)
	if (ap_fw_loaded) {
		/* XXX Andrey: fo SOFTAP disable pkt filters (if there were any )  */
		dhd_enable_packet_filter(0, dhd);
	}
#endif /* defined(SOFTAP) */
	dhd_set_packet_filter(dhd);
#endif /* PKT_FILTER_SUPPORT */

	/* query for 'clmver' to get clm version info from firmware */
	bzero(buf, sizeof(buf));
	ret = dhd_iovar(dhd, 0, "clmver", NULL, 0, buf, sizeof(buf), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		char *ver_temp_buf = NULL;

		if ((ver_temp_buf = bcmstrstr(buf, "Data:")) == NULL) {
			DHD_ERROR(("Couldn't find \"Data:\"\n"));
		} else {
			ptr = (ver_temp_buf + strlen("Data:"));
			if ((ver_temp_buf = bcmstrtok(&ptr, "\n", 0)) == NULL) {
				DHD_ERROR(("Couldn't find New line character\n"));
			} else {
				bzero(clm_version, CLM_VER_STR_LEN);
				strlcpy(clm_version, ver_temp_buf,
					MIN(strlen(ver_temp_buf) + 1, CLM_VER_STR_LEN));
				DHD_INFO(("CLM version = %s\n", clm_version));
			}
		}

#if defined(CUSTOMER_HW4_DEBUG)
		if ((ver_temp_buf = bcmstrstr(ptr, "Customization:")) == NULL) {
			DHD_ERROR(("Couldn't find \"Customization:\"\n"));
		} else {
			char tokenlim;
			ptr = (ver_temp_buf + strlen("Customization:"));
			if ((ver_temp_buf = bcmstrtok(&ptr, "(\n", &tokenlim)) == NULL) {
				DHD_ERROR(("Couldn't find project blob version"
					"or New line character\n"));
			} else if (tokenlim == '(') {
				snprintf(clm_version,
					CLM_VER_STR_LEN - 1, "%s, Blob ver = Major : %s minor : ",
					clm_version, ver_temp_buf);
				DHD_INFO(("[INFO]CLM/Blob version = %s\n", clm_version));
				if ((ver_temp_buf = bcmstrtok(&ptr, "\n", &tokenlim)) == NULL) {
					DHD_ERROR(("Couldn't find New line character\n"));
				} else {
					snprintf(clm_version,
						strlen(clm_version) + strlen(ver_temp_buf),
						"%s%s", clm_version, ver_temp_buf);
					DHD_INFO(("[INFO]CLM/Blob/project version = %s\n",
						clm_version));

				}
			} else if (tokenlim == '\n') {
				snprintf(clm_version,
					strlen(clm_version) + strlen(", Blob ver = Major : ") + 1,
					"%s, Blob ver = Major : ", clm_version);
				snprintf(clm_version,
					strlen(clm_version) + strlen(ver_temp_buf) + 1,
					"%s%s", clm_version, ver_temp_buf);
				DHD_INFO(("[INFO]CLM/Blob/project version = %s\n", clm_version));
			}
		}
#endif /* CUSTOMER_HW4_DEBUG */
		if (strlen(clm_version)) {
			DHD_ERROR(("CLM version = %s\n", clm_version));
		} else {
			DHD_ERROR(("Couldn't find CLM version!\n"));
		}

	}

#ifdef WRITE_WLANINFO
	sec_save_wlinfo(fw_version, EPI_VERSION_STR, dhd->info->nv_path, clm_version);
#endif /* WRITE_WLANINFO */

#ifdef GEN_SOFTAP_INFO_FILE
	sec_save_softap_info();
#endif /* GEN_SOFTAP_INFO_FILE */

#ifdef PNO_SUPPORT
	if (!dhd->pno_state) {
		dhd_pno_init(dhd);
	}
#endif

#ifdef DHD_PKTTS
	/* get the pkt metadata buffer length supported by FW */
	if (dhd_wl_ioctl_get_intiovar(dhd, "bus:metadata_info", &val,
			WLC_GET_VAR, FALSE, 0) != BCME_OK) {
		DHD_ERROR(("%s: failed to get pkt metadata buflen, use IPC pkt TS.\n",
				__FUNCTION__));
		/*
		 * if iovar fails, IPC method of collecting
		 * TS should be used, hence set metadata_buflen as
		 * 0 here. This will be checked later on Tx completion
		 * to decide if IPC or metadata method of reading TS
		 * should be used
		 */
		dhd->pkt_metadata_version = 0;
		dhd->pkt_metadata_buflen = 0;
	} else {
		dhd->pkt_metadata_version  = GET_METADATA_VER(val);
		dhd->pkt_metadata_buflen  = GET_METADATA_BUFLEN(val);
	}

	/* Check FW supports pktlat, if supports enable pktts_enab iovar */
	ret = dhd_set_pktts_enab(dhd, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: Enabling pktts_enab failed, ret=%d\n", __FUNCTION__, ret));
	}
#endif /* DHD_PKTTS */

#ifdef RTT_SUPPORT
	if (dhd->rtt_state) {
		ret = dhd_rtt_init(dhd);
		if (ret < 0) {
			DHD_ERROR(("%s failed to initialize RTT\n", __FUNCTION__));
		}
	}
#endif

#ifdef FILTER_IE
	/* Failure to configure filter IE is not a fatal error, ignore it. */
	if (FW_SUPPORTED(dhd, fie) &&
		!(dhd->op_mode & (DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))) {
		dhd_read_from_file(dhd);
	}
#endif /* FILTER_IE */

#ifdef NDO_CONFIG_SUPPORT
	dhd->ndo_enable = FALSE;
	dhd->ndo_host_ip_overflow = FALSE;
	dhd->ndo_max_host_ip = NDO_MAX_HOST_IP_ENTRIES;
#endif /* NDO_CONFIG_SUPPORT */

	/* ND offload version supported */
	dhd->ndo_version = dhd_ndo_get_version(dhd);

	/* check dongle supports wbtext (product policy) or not */
	dhd->wbtext_support = FALSE;
	if (dhd_wl_ioctl_get_intiovar(dhd, "wnm_bsstrans_resp", &wnm_bsstrans_resp,
			WLC_GET_VAR, FALSE, 0) != BCME_OK) {
		DHD_ERROR(("failed to get wnm_bsstrans_resp\n"));
	}
	dhd->wbtext_policy = wnm_bsstrans_resp;
	if (dhd->wbtext_policy == WL_BSSTRANS_POLICY_PRODUCT_WBTEXT) {
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

#ifdef DHD_NON_DMA_M2M_CORRUPTION
	/* check pcie non dma loopback */
	if (dhd->op_mode == DHD_FLAG_MFG_MODE &&
		(dhd_bus_dmaxfer_lpbk(dhd, M2M_NON_DMA_LPBK) < 0)) {
			goto done;
	}
#endif /* DHD_NON_DMA_M2M_CORRUPTION */

#ifdef CUSTOM_ASSOC_TIMEOUT
	/* set recreate_bi_timeout to increase assoc timeout :
	* 20 * 100TU * 1024 / 1000 = 2 secs
	* (beacon wait time = recreate_bi_timeout * beacon_period * 1024 / 1000)
	*/
	if (dhd_wl_ioctl_set_intiovar(dhd, "recreate_bi_timeout",
			CUSTOM_ASSOC_TIMEOUT,
			WLC_SET_VAR, TRUE, 0) != BCME_OK) {
		DHD_ERROR(("failed to set assoc timeout\n"));
	}
#endif /* CUSTOM_ASSOC_TIMEOUT */

	BCM_REFERENCE(ret2);
#if defined(WBTEXT) && defined(RRM_BCNREQ_MAX_CHAN_TIME)
	if (dhd_iovar(dhd, 0, "rrm_bcn_req_thrtl_win",
		(char *)&rrm_bcn_req_thrtl_win, sizeof(rrm_bcn_req_thrtl_win),
		NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set RRM BCN request thrtl_win\n"));
	}
	if (dhd_iovar(dhd, 0, "rrm_bcn_req_max_off_chan_time",
		(char *)&rrm_bcn_req_max_off_chan_time, sizeof(rrm_bcn_req_max_off_chan_time),
		NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set RRM BCN Request max_off_chan_time\n"));
	}
#endif /* WBTEXT && RRM_BCNREQ_MAX_CHAN_TIME */
#ifdef WL_MONITOR
#ifdef HOST_RADIOTAP_CONV
	/* 'Wl monitor' IOVAR is fired to check whether the FW supports radiotap conversion or not.
	 * This is indicated through MSB(1<<31) bit, based on which host radiotap conversion
	 * will be enabled or disabled.
	 * 0 - Host supports Radiotap conversion.
	 * 1 - FW supports Radiotap conversion.
	 */
	bcm_mkiovar("monitor", (char *)&monitor, sizeof(monitor), iovbuf, sizeof(iovbuf));
	if ((ret2 = dhd_wl_ioctl_cmd(dhd, WLC_GET_MONITOR, iovbuf,
		sizeof(iovbuf), FALSE, 0)) == 0) {
		memcpy(&monitor, iovbuf, sizeof(monitor));
		dhdinfo->host_radiotap_conv = (monitor & HOST_RADIOTAP_CONV_BIT) ? TRUE : FALSE;
	} else {
		DHD_ERROR(("%s Failed to get monitor mode, err %d\n",
			__FUNCTION__, ret2));
	}
#endif /* HOST_RADIOTAP_CONV */
	if (FW_SUPPORTED(dhd, monitor)) {
		dhd->monitor_enable = TRUE;
		DHD_ERROR(("%s: Monitor mode is enabled in FW cap\n", __FUNCTION__));
	} else {
		dhd->monitor_enable = FALSE;
		DHD_ERROR(("%s: Monitor mode is not enabled in FW cap\n", __FUNCTION__));
	}
#endif /* WL_MONITOR */

	/* store the preserve log set numbers */
	if (dhd_get_preserve_log_numbers(dhd, &dhd->logset_prsrv_mask)
			!= BCME_OK) {
		DHD_ERROR(("%s: Failed to get preserve log # !\n", __FUNCTION__));
	}

#ifdef CONFIG_SILENT_ROAM
	dhd->sroam_turn_on = TRUE;
	dhd->sroamed = FALSE;
#endif /* CONFIG_SILENT_ROAM */

#ifndef OEM_ANDROID
	/* For non-android FC modular builds, override firmware preinited values */
	dhd_override_fwprenit(dhd);
#endif /* !OEM_ANDROID */
	dhd_set_bandlock(dhd);

done:
	if (iov_buf) {
		MFREE(dhd->osh, iov_buf, WLC_IOCTL_SMLEN);
	}
	return ret;
}

int
dhd_legacy_preinit_ioctls(dhd_pub_t *dhd)
{
	int ret = 0;
	/*  Room for "event_msgs_ext" + '\0' + bitvec  */
	char iovbuf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE + 16];
	char *mask;
	uint32 buf_key_b4_m4 = 1;
#ifdef DHD_PKTTS
	uint32 val = 0;
#endif
	uint8 msglen;
	eventmsgs_ext_t *eventmask_msg = NULL;
	uint32 event_log_max_sets = 0;
	char* iov_buf = NULL;
	/* XXX: Use ret2 for return check of IOVARS that might return BCME_UNSUPPORTED,
	*	based on FW build tag.
	*/
	int ret2 = 0;
	uint32 wnm_cap = 0;
#if defined(WL_MONITOR) && defined(HOST_RADIOTAP_CONV)
	uint monitor = 0;
	dhd_info_t *dhdinfo = (dhd_info_t*)dhd->info;
#endif /* WL_MONITOR */
#if defined(BCMSUP_4WAY_HANDSHAKE)
	uint32 sup_wpa = 1;
#endif /* BCMSUP_4WAY_HANDSHAKE */
#if defined(CUSTOM_AMPDU_BA_WSIZE) || (defined(WLAIBSS) && \
	defined(CUSTOM_IBSS_AMPDU_BA_WSIZE))
	uint32 ampdu_ba_wsize = 0;
#endif /* CUSTOM_AMPDU_BA_WSIZE ||(WLAIBSS && CUSTOM_IBSS_AMPDU_BA_WSIZE) */
#if defined(CUSTOM_AMPDU_MPDU)
	int32 ampdu_mpdu = 0;
#endif
#if defined(CUSTOM_AMPDU_RELEASE)
	int32 ampdu_release = 0;
#endif
#if defined(CUSTOM_AMSDU_AGGSF)
	int32 amsdu_aggsf = 0;
#endif

#if defined(BCMSDIO) || defined(BCMDBUS)
#ifdef PROP_TXSTATUS
	int wlfc_enable = TRUE;
#ifndef DISABLE_11N
	uint32 hostreorder = 1;
	uint wl_down = 1;
#endif /* DISABLE_11N */
#endif /* PROP_TXSTATUS */
#endif /* defined(BCMSDIO) || defined(BCMDBUS) */

#ifndef PCIE_FULL_DONGLE
	uint32 wl_ap_isolate;
#endif /* PCIE_FULL_DONGLE */
	uint32 frameburst = CUSTOM_FRAMEBURST_SET;
	uint wnm_bsstrans_resp = 0;
#ifdef SUPPORT_SET_CAC
	uint32 cac = 1;
#endif /* SUPPORT_SET_CAC */
#ifdef DHD_BUS_MEM_ACCESS
	uint32 enable_memuse = 1;
#endif /* DHD_BUS_MEM_ACCESS */
#if defined(SUPPORT_2G_VHT) || defined(SUPPORT_5G_1024QAM_VHT)
	uint32 vht_features = 0; /* init to 0, will be set based on each support */
#endif /* SUPPORT_2G_VHT || SUPPORT_5G_1024QAM_VHT */

#ifdef OEM_ANDROID
#ifdef DHD_ENABLE_LPC
	uint32 lpc = 1;
#endif /* DHD_ENABLE_LPC */
	uint power_mode = PM_FAST;
#if defined(BCMSDIO)
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = CUSTOM_GLOM_SETTING;
#endif /* defined(BCMSDIO) */
	uint bcn_timeout = CUSTOM_BCN_TIMEOUT;
	uint scancache_enab = TRUE;
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
	uint32 bcn_li_bcn = 1;
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
	uint retry_max = CUSTOM_ASSOC_RETRY_MAX;
	int scan_assoc_time = DHD_SCAN_ASSOC_ACTIVE_TIME;
	int scan_unassoc_time = DHD_SCAN_UNASSOC_ACTIVE_TIME;
	int scan_passive_time = DHD_SCAN_PASSIVE_TIME;
	char buf[WLC_IOCTL_SMLEN];
	char *ptr;
	uint32 listen_interval = CUSTOM_LISTEN_INTERVAL; /* Default Listen Interval in Beacons */
#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	wl_el_tag_params_t *el_tag = NULL;
#endif /* DHD_8021X_DUMP */
#ifdef DHD_RANDMAC_LOGGING
	uint privacy_mask = 0;
#endif /* DHD_RANDMAC_LOGGING */
#ifdef ROAM_ENABLE
	uint roamvar = 0;
	int roam_trigger[2] = {CUSTOM_ROAM_TRIGGER_SETTING, WLC_BAND_ALL};
	int roam_scan_period[2] = {10, WLC_BAND_ALL};
	int roam_delta[2] = {CUSTOM_ROAM_DELTA_SETTING, WLC_BAND_ALL};
#ifdef ROAM_AP_ENV_DETECTION
	int roam_env_mode = AP_ENV_INDETERMINATE;
#endif /* ROAM_AP_ENV_DETECTION */
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
/* xxx andrey tmp fix for dk8000 build error  */
#if (defined(AP) && !defined(WLP2P)) || (!defined(AP) && defined(WL_CFG80211))
	struct ether_addr p2p_ea;
#endif
#ifdef BCMCCX
	uint32 ccx = 1;
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
#ifdef OKC_SUPPORT
	uint32 okc = 1;
#endif

#ifdef DISABLE_11N
	uint32 nmode = 0;
#endif /* DISABLE_11N */

#if defined(DISABLE_11AC)
	uint32 vhtmode = 0;
#endif /* DISABLE_11AC */
#ifdef USE_WL_TXBF
	uint32 txbf = 1;
#endif /* USE_WL_TXBF */
#ifdef DISABLE_TXBFR
	uint32 txbf_bfr_cap = 0;
#endif /* DISABLE_TXBFR */
#ifdef AMPDU_VO_ENABLE
	/* XXX: Enabling VO AMPDU to reduce FER */
	struct ampdu_tid_control tid;
#endif
#if defined(PROP_TXSTATUS)
#ifdef USE_WFA_CERT_CONF
	uint32 proptx = 0;
#endif /* USE_WFA_CERT_CONF */
#endif /* PROP_TXSTATUS */
#ifdef DHD_SET_FW_HIGHSPEED
	uint32 ack_ratio = 250;
	uint32 ack_ratio_depth = 64;
#endif /* DHD_SET_FW_HIGHSPEED */
#ifdef DISABLE_11N_PROPRIETARY_RATES
	uint32 ht_features = 0;
#endif /* DISABLE_11N_PROPRIETARY_RATES */
#ifdef CUSTOM_PSPRETEND_THR
	uint32 pspretend_thr = CUSTOM_PSPRETEND_THR;
#endif
#ifdef CUSTOM_EVENT_PM_WAKE
	uint32 pm_awake_thresh = CUSTOM_EVENT_PM_WAKE;
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef DISABLE_PRUNED_SCAN
	uint32 scan_features = 0;
#endif /* DISABLE_PRUNED_SCAN */
#ifdef BCMPCIE_OOB_HOST_WAKE
	uint32 hostwake_oob = 0;
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef EVENT_LOG_RATE_HC
	/* threshold number of lines per second */
#define EVENT_LOG_RATE_HC_THRESHOLD	1000
	uint32 event_log_rate_hc = EVENT_LOG_RATE_HC_THRESHOLD;
#endif /* EVENT_LOG_RATE_HC */
#if defined(WBTEXT) && defined(WBTEXT_BTMDELTA)
	uint32 btmdelta = WBTEXT_BTMDELTA;
#endif /* WBTEXT && WBTEXT_BTMDELTA */
#if defined(WBTEXT) && defined(RRM_BCNREQ_MAX_CHAN_TIME)
	uint32 rrm_bcn_req_thrtl_win = RRM_BCNREQ_MAX_CHAN_TIME * 2;
	uint32 rrm_bcn_req_max_off_chan_time = RRM_BCNREQ_MAX_CHAN_TIME;
#endif /* WBTEXT && RRM_BCNREQ_MAX_CHAN_TIME */
#endif  /* OEM_ANDROID */

	BCM_REFERENCE(iovbuf);
	DHD_TRACE(("Enter %s\n", __FUNCTION__));

#ifdef ARP_OFFLOAD_SUPPORT
	/* arpoe will be applied from the supsend context */
	dhd->arpoe_enable = TRUE;
	dhd->arpol_configured = FALSE;
#endif /* ARP_OFFLOAD_SUPPORT */

#ifdef OEM_ANDROID
#ifdef PKT_FILTER_SUPPORT
	dhd_pkt_filter_enable = TRUE;
#ifdef APF
	dhd->apf_set = FALSE;
#endif /* APF */
#endif /* PKT_FILTER_SUPPORT */
	dhd->suspend_bcn_li_dtim = CUSTOM_SUSPEND_BCN_LI_DTIM;
#ifdef ENABLE_MAX_DTIM_IN_SUSPEND
	dhd->max_dtim_enable = TRUE;
#else
	dhd->max_dtim_enable = FALSE;
#endif /* ENABLE_MAX_DTIM_IN_SUSPEND */
	dhd->disable_dtim_in_suspend = FALSE;
#ifdef CUSTOM_SET_OCLOFF
	dhd->ocl_off = FALSE;
#endif /* CUSTOM_SET_OCLOFF */
#ifdef SUPPORT_SET_TID
	dhd->tid_mode = SET_TID_OFF;
	dhd->target_uid = 0;
	dhd->target_tid = 0;
#endif /* SUPPORT_SET_TID */
#ifdef DHDTCPACK_SUPPRESS
	dhd_tcpack_suppress_set(dhd, dhd->conf->tcpack_sup_mode);
#endif
	dhd->op_mode = 0;

	/* clear AP flags */
#if defined(CUSTOM_COUNTRY_CODE)
	dhd->dhd_cflags &= ~WLAN_PLAT_AP_FLAG;
#endif /* CUSTOM_COUNTRY_CODE */

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

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	ret = dhd_iovar(dhd, 0, "ver", NULL, 0, (char *)&buf, sizeof(buf), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		bcmstrtok(&ptr, "\n", 0);
		/* Print fw version info */
		strncpy(fw_version, buf, FW_VER_STR_LEN);
		fw_version[FW_VER_STR_LEN-1] = '\0';
	}

#ifdef BOARD_HIKEY
	/* Set op_mode as MFG_MODE if WLTEST is present in "wl ver" */
	if (strstr(fw_version, "WLTEST") != NULL) {
		DHD_ERROR(("%s: wl ver has WLTEST, setting op_mode as DHD_FLAG_MFG_MODE\n",
			__FUNCTION__));
		op_mode = DHD_FLAG_MFG_MODE;
	}
#endif /* BOARD_HIKEY */

	if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
		dhd->op_mode = DHD_FLAG_MFG_MODE;
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
		/* disable runtimePM by default in MFG mode. */
		pm_runtime_disable(dhd_bus_to_dev(dhd->bus));
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
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
#ifdef BCMPCIE_OOB_HOST_WAKE
	ret = dhd_iovar(dhd, 0, "bus:hostwake_oob", NULL, 0, (char *)&hostwake_oob,
		sizeof(hostwake_oob), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: hostwake_oob IOVAR not present, proceed\n", __FUNCTION__));
	} else {
		if (hostwake_oob == 0) {
			DHD_ERROR(("%s: hostwake_oob is not enabled in the NVRAM, STOP\n",
				__FUNCTION__));
			ret = BCME_UNSUPPORTED;
			goto done;
		} else {
			DHD_ERROR(("%s: hostwake_oob enabled\n", __FUNCTION__));
		}
	}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef DNGL_AXI_ERROR_LOGGING
	ret = dhd_iovar(dhd, 0, "axierror_logbuf_addr", NULL, 0, (char *)&dhd->axierror_logbuf_addr,
		sizeof(dhd->axierror_logbuf_addr), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: axierror_logbuf_addr IOVAR not present, proceed\n", __FUNCTION__));
		dhd->axierror_logbuf_addr = 0;
	} else {
		DHD_ERROR(("%s: axierror_logbuf_addr : 0x%x\n",
			__FUNCTION__, dhd->axierror_logbuf_addr));
	}
#endif /* DNGL_AXI_ERROR_LOGGING */

#ifdef EVENT_LOG_RATE_HC
	ret = dhd_iovar(dhd, 0, "event_log_rate_hc", (char *)&event_log_rate_hc,
		sizeof(event_log_rate_hc), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s event_log_rate_hc set failed %d\n", __FUNCTION__, ret));
	} else  {
		DHD_ERROR(("%s event_log_rate_hc set with threshold:%d\n", __FUNCTION__,
			event_log_rate_hc));
	}
#endif /* EVENT_LOG_RATE_HC */

#ifdef GET_CUSTOM_MAC_ENABLE
	memset(hw_ether, 0, sizeof(hw_ether));
	ret = wifi_platform_get_mac_addr(dhd->info->adapter, hw_ether, 0);
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
	ret = dhd_iovar(dhd, 0, "cur_etheraddr", NULL, 0, (char *)&buf, sizeof(buf), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
		ret = BCME_NOTUP;
		goto done;
	}

	DHD_ERROR(("%s: use firmware generated mac_address "MACDBG"\n",
		__FUNCTION__, MAC2STRDBG(&buf)));

#ifdef MACADDR_PROVISION_ENFORCED
	if (ETHER_IS_LOCALADDR(buf)) {
		DHD_ERROR(("%s: error! not using provision mac addr!\n", __FUNCTION__));
		ret = BCME_BADADDR;
		goto done;
	}
#endif /* MACADDR_PROVISION_ENFORCED */

	/* Update public MAC address after reading from Firmware */
	memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);

	if (ETHER_ISNULLADDR(dhd->mac.octet)) {
		DHD_ERROR(("%s: NULL MAC address during pre-init\n", __FUNCTION__));
		ret = BCME_BADADDR;
		goto done;
	} else {
		(void)memcpy_s(dhd_linux_get_primary_netdev(dhd)->perm_addr, ETHER_ADDR_LEN,
			dhd->mac.octet, ETHER_ADDR_LEN);
	}
#if defined(WL_STA_ASSOC_RAND) && defined(WL_STA_INIT_RAND)
	/* Set cur_etheraddr of primary interface to randomized address to ensure
	 * that any action frame transmission will happen using randomized macaddr
	 * primary netdev->perm_addr will hold the original factory MAC.
	 */
	{
		if ((ret = dhd_update_rand_mac_addr(dhd)) < 0) {
			DHD_ERROR(("%s: failed to set macaddress\n", __FUNCTION__));
			goto done;
		}
	}
#endif /* WL_STA_ASSOC_RAND && WL_STA_INIT_RAND */

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

		/* set AP flag for specific country code of SOFTAP */
#if defined(CUSTOM_COUNTRY_CODE)
		dhd->dhd_cflags |= WLAN_PLAT_AP_FLAG | WLAN_PLAT_NODFS_FLAG;
#endif /* CUSTOM_COUNTRY_CODE */
	} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
#if defined(ARP_OFFLOAD_SUPPORT)
		dhd->arpoe_enable = FALSE;
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef PKT_FILTER_SUPPORT
		dhd_pkt_filter_enable = FALSE;
#endif /* PKT_FILTER_SUPPORT */
		dhd->op_mode = DHD_FLAG_MFG_MODE;
#ifdef USE_DYNAMIC_F2_BLKSIZE
		/* XXX The 'wl counters' command triggers SDIO bus error
		 * if F2 block size is greater than 128 bytes using 4354A1
		 * manufacturing firmware. To avoid this problem, F2 block
		 * size is set to 128 bytes only for DHD_FLAG_MFG_MODE.
		 * There is no problem for other chipset since big data
		 * transcation through SDIO bus is not happened during
		 * manufacturing test.
		 */
		dhdsdio_func_blocksize(dhd, 2, DYNAMIC_F2_BLKSIZE_FOR_NONLEGACY);
#endif /* USE_DYNAMIC_F2_BLKSIZE */
#ifndef CUSTOM_SET_ANTNPM
		if (FW_SUPPORTED(dhd, rsdb)) {
			wl_config_t rsdb_mode;
			memset(&rsdb_mode, 0, sizeof(rsdb_mode));
			ret = dhd_iovar(dhd, 0, "rsdb_mode", (char *)&rsdb_mode, sizeof(rsdb_mode),
				NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s Disable rsdb_mode is failed ret= %d\n",
					__FUNCTION__, ret));
			}
		}
#endif /* !CUSTOM_SET_ANTNPM */
	} else {
		uint32 concurrent_mode = 0;
		if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_P2P_MODE) ||
			(op_mode == DHD_FLAG_P2P_MODE)) {
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif
			dhd->op_mode = DHD_FLAG_P2P_MODE;
		} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_IBSS_MODE) ||
			(op_mode == DHD_FLAG_IBSS_MODE)) {
			dhd->op_mode = DHD_FLAG_IBSS_MODE;
		} else
			dhd->op_mode = DHD_FLAG_STA_MODE;
#if defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P)
		if (dhd->op_mode != DHD_FLAG_IBSS_MODE &&
			(concurrent_mode = dhd_get_concurrent_capabilites(dhd))) {
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
#endif /* defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P) */
	}

#ifdef DISABLE_PRUNED_SCAN
	if (FW_SUPPORTED(dhd, rsdb)) {
		ret = dhd_iovar(dhd, 0, "scan_features", (char *)&scan_features,
				sizeof(scan_features), iovbuf, sizeof(iovbuf), FALSE);
		if (ret < 0) {
			if (ret == BCME_UNSUPPORTED) {
				DHD_ERROR(("%s get scan_features, UNSUPPORTED\n",
					__FUNCTION__));
			} else {
				DHD_ERROR(("%s get scan_features err(%d)\n",
					__FUNCTION__, ret));
			}

		} else {
			memcpy(&scan_features, iovbuf, 4);
			scan_features &= ~RSDB_SCAN_DOWNGRADED_CH_PRUNE_ROAM;
			ret = dhd_iovar(dhd, 0, "scan_features", (char *)&scan_features,
					sizeof(scan_features), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s set scan_features err(%d)\n",
					__FUNCTION__, ret));
			}
		}
	}
#endif /* DISABLE_PRUNED_SCAN */

	DHD_ERROR(("Firmware up: op_mode=0x%04x, MAC="MACDBG"\n",
		dhd->op_mode, MAC2STRDBG(dhd->mac.octet)));
#if defined(DHD_BLOB_EXISTENCE_CHECK)
	if (!dhd->is_blob)
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

#if defined(DISABLE_11AC)
	ret = dhd_iovar(dhd, 0, "vhtmode", (char *)&vhtmode, sizeof(vhtmode), NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s wl vhtmode 0 failed %d\n", __FUNCTION__, ret));
#endif /* DISABLE_11AC */

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
	ret = dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s roam_off failed %d\n", __FUNCTION__, ret));
	}
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */
#if defined(ROAM_ENABLE)
#ifdef DISABLE_BCNLOSS_ROAM
	ret = dhd_iovar(dhd, 0, "roam_bcnloss_off", (char *)&roam_bcnloss_off,
			sizeof(roam_bcnloss_off), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s roam_bcnloss_off failed %d\n", __FUNCTION__, ret));
	}
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
#ifdef ROAM_AP_ENV_DETECTION
	if (roam_trigger[0] == WL_AUTO_ROAM_TRIGGER) {
		if (dhd_iovar(dhd, 0, "roam_env_detection", (char *)&roam_env_mode,
				sizeof(roam_env_mode), NULL, 0, TRUE) == BCME_OK)
			dhd->roam_env_detection = TRUE;
		else
			dhd->roam_env_detection = FALSE;
	}
#endif /* ROAM_AP_ENV_DETECTION */
#ifdef CONFIG_ROAM_RSSI_LIMIT
	ret = dhd_roam_rssi_limit_set(dhd, CUSTOM_ROAMRSSI_2G, CUSTOM_ROAMRSSI_5G);
	if (ret < 0) {
		DHD_ERROR(("%s set roam_rssi_limit failed ret %d\n", __FUNCTION__, ret));
	}
#endif /* CONFIG_ROAM_RSSI_LIMIT */
#ifdef CONFIG_ROAM_MIN_DELTA
	ret = dhd_roam_min_delta_set(dhd, CUSTOM_ROAM_MIN_DELTA, CUSTOM_ROAM_MIN_DELTA);
	if (ret < 0) {
		DHD_ERROR(("%s set roam_min_delta failed ret %d\n", __FUNCTION__, ret));
	}
#endif /* CONFIG_ROAM_MIN_DELTA */
#endif /* ROAM_ENABLE */

#ifdef CUSTOM_EVENT_PM_WAKE
	/* XXX need to check time value */
	ret = dhd_iovar(dhd, 0, "const_awake_thresh", (char *)&pm_awake_thresh,
			sizeof(pm_awake_thresh), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set const_awake_thresh failed %d\n", __FUNCTION__, ret));
	}
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef OKC_SUPPORT
	dhd_iovar(dhd, 0, "okc_enable", (char *)&okc, sizeof(okc), NULL, 0, TRUE);
#endif
#ifdef BCMCCX
	dhd_iovar(dhd, 0, "ccx_enable", (char *)&ccx, sizeof(ccx), NULL, 0, TRUE);
#endif /* BCMCCX */

#ifdef WLTDLS
	dhd->tdls_enable = FALSE;
	dhd_tdls_set_mode(dhd, false);
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
	if (dhd->op_mode & DHD_FLAG_STA_MODE) {
		if ((ret = dhd_enable_adps(dhd, ADPS_ENABLE)) != BCME_OK &&
			(ret != BCME_UNSUPPORTED)) {
			DHD_ERROR(("%s dhd_enable_adps failed %d\n",
				__FUNCTION__, ret));
		}
	}
#endif /* WLADPS */

#ifdef DHD_PM_CONTROL_FROM_FILE
#ifdef CUSTOMER_HW10
	dhd_control_pm(dhd, &power_mode);
#else
	sec_control_pm(dhd, &power_mode);
#endif /* CUSTOMER_HW10 */
#else
	/* Set PowerSave mode */
	(void) dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode), TRUE, 0);
#endif /* DHD_PM_CONTROL_FROM_FILE */

#if defined(BCMSDIO)
	/* Match Host and Dongle rx alignment */
	ret = dhd_iovar(dhd, 0, "bus:txglomalign", (char *)&dongle_align, sizeof(dongle_align),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set bus:txglomalign failed %d\n", __FUNCTION__, ret));
	}

#ifdef USE_WFA_CERT_CONF
	if (sec_get_param_wfa_cert(dhd, SET_PARAM_BUS_TXGLOM_MODE, &glom) == BCME_OK) {
		DHD_ERROR(("%s, read txglom param =%d\n", __FUNCTION__, glom));
	}
#endif /* USE_WFA_CERT_CONF */
	if (glom != DEFAULT_GLOM_VALUE) {
		DHD_INFO(("%s set glom=0x%X\n", __FUNCTION__, glom));
		ret = dhd_iovar(dhd, 0, "bus:txglom", (char *)&glom, sizeof(glom), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s set bus:txglom failed %d\n", __FUNCTION__, ret));
		}
	}
#endif /* defined(BCMSDIO) */

	/* Setup timeout if Beacons are lost and roam is off to report link down */
	ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout, sizeof(bcn_timeout),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set bcn_timeout failed %d\n", __FUNCTION__, ret));
	}

	/* Setup assoc_retry_max count to reconnect target AP in dongle */
	ret = dhd_iovar(dhd, 0, "assoc_retry_max", (char *)&retry_max, sizeof(retry_max),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set assoc_retry_max failed %d\n", __FUNCTION__, ret));
	}

#if defined(AP) && !defined(WLP2P)
	ret = dhd_iovar(dhd, 0, "apsta", (char *)&apsta, sizeof(apsta), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set apsta failed %d\n", __FUNCTION__, ret));
	}

#endif /* defined(AP) && !defined(WLP2P) */

#ifdef MIMO_ANT_SETTING
	dhd_sel_ant_from_file(dhd);
#endif /* MIMO_ANT_SETTING */

#if defined(OEM_ANDROID) && defined(SOFTAP)
	if (ap_fw_loaded == TRUE) {
		dhd_wl_ioctl_cmd(dhd, WLC_SET_DTIMPRD, (char *)&dtim, sizeof(dtim), TRUE, 0);
	}
#endif /* defined(OEM_ANDROID) && defined(SOFTAP) */

#if defined(KEEP_ALIVE)
	{
	/* Set Keep Alive : be sure to use FW with -keepalive */
	int res;

#if defined(OEM_ANDROID) && defined(SOFTAP)
	if (ap_fw_loaded == FALSE)
#endif /* defined(OEM_ANDROID) && defined(SOFTAP) */
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

#else /* OEM_ANDROID */
	if ((ret = dhd_apply_default_clm(dhd, clm_path)) < 0) {
		DHD_ERROR(("%s: CLM set failed. Abort initialization.\n", __FUNCTION__));
		goto done;
	}

#if defined(KEEP_ALIVE)
	if (!(dhd->op_mode &
		(DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))) {
		if ((ret = dhd_keep_alive_onoff(dhd)) < 0)
			DHD_ERROR(("%s set keeplive failed %d\n",
			__FUNCTION__, ret));
	}
#endif

	/* get a capabilities from firmware */
	memset(dhd->fw_capabilities, 0, sizeof(dhd->fw_capabilities));
	ret = dhd_iovar(dhd, 0, "cap", NULL, 0, dhd->fw_capabilities, sizeof(dhd->fw_capabilities),
			FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: Get Capability failed (error=%d)\n",
			__FUNCTION__, ret));
		goto done;
	}
#endif  /* OEM_ANDROID */

	ret = dhd_iovar(dhd, 0, "event_log_max_sets", NULL, 0, (char *)&event_log_max_sets,
		sizeof(event_log_max_sets), FALSE);
	if (ret == BCME_OK) {
		dhd->event_log_max_sets = event_log_max_sets;
	} else {
		dhd->event_log_max_sets = NUM_EVENT_LOG_SETS;
	}
	/* Make sure max_sets is set first with wmb and then sets_queried,
	 * this will be used during parsing the logsets in the reverse order.
	 */
	OSL_SMP_WMB();
	dhd->event_log_max_sets_queried = TRUE;
	DHD_ERROR(("%s: event_log_max_sets: %d ret: %d\n",
		__FUNCTION__, dhd->event_log_max_sets, ret));
#ifdef DHD_BUS_MEM_ACCESS
	ret = dhd_iovar(dhd, 0, "enable_memuse", (char *)&enable_memuse,
			sizeof(enable_memuse), iovbuf, sizeof(iovbuf), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: enable_memuse is failed ret=%d\n",
			__FUNCTION__, ret));
	} else {
		DHD_ERROR(("%s: enable_memuse = %d\n",
			__FUNCTION__, enable_memuse));
	}
#endif /* DHD_BUS_MEM_ACCESS */

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
	 g_frameburst = frameburst;
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
#ifdef DHD_SET_FW_HIGHSPEED
	/* Set ack_ratio */
	ret = dhd_iovar(dhd, 0, "ack_ratio", (char *)&ack_ratio, sizeof(ack_ratio), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set ack_ratio failed  %d\n", __FUNCTION__, ret));
	}

	/* Set ack_ratio_depth */
	ret = dhd_iovar(dhd, 0, "ack_ratio_depth", (char *)&ack_ratio_depth,
			sizeof(ack_ratio_depth), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set ack_ratio_depth failed  %d\n", __FUNCTION__, ret));
	}
#endif /* DHD_SET_FW_HIGHSPEED */

	iov_buf = (char*)MALLOC(dhd->osh, WLC_IOCTL_SMLEN);
	if (iov_buf == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for iov_buf\n", WLC_IOCTL_SMLEN));
		ret = BCME_NOMEM;
		goto done;
	}

	BCM_REFERENCE(ret2);

#ifdef WLAIBSS
	/* Apply AIBSS configurations */
	if ((ret = dhd_preinit_aibss_ioctls(dhd, iov_buf)) != BCME_OK) {
		DHD_ERROR(("%s dhd_preinit_aibss_ioctls failed %d\n",
				__FUNCTION__, ret));
		goto done;
	}
#endif /* WLAIBSS */

#if defined(CUSTOM_AMPDU_BA_WSIZE) || (defined(WLAIBSS) && \
	defined(CUSTOM_IBSS_AMPDU_BA_WSIZE))
	/* Set ampdu ba wsize to 64 or 16 */
#ifdef CUSTOM_AMPDU_BA_WSIZE
	ampdu_ba_wsize = CUSTOM_AMPDU_BA_WSIZE;
#endif
#if defined(WLAIBSS) && defined(CUSTOM_IBSS_AMPDU_BA_WSIZE)
	if (dhd->op_mode == DHD_FLAG_IBSS_MODE)
		ampdu_ba_wsize = CUSTOM_IBSS_AMPDU_BA_WSIZE;
#endif /* WLAIBSS && CUSTOM_IBSS_AMPDU_BA_WSIZE */
	if (ampdu_ba_wsize != 0) {
		ret = dhd_iovar(dhd, 0, "ampdu_ba_wsize", (char *)&ampdu_ba_wsize,
				sizeof(ampdu_ba_wsize), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set ampdu_ba_wsize to %d failed  %d\n",
				__FUNCTION__, ampdu_ba_wsize, ret));
		}
	}
#endif /* CUSTOM_AMPDU_BA_WSIZE || (WLAIBSS && CUSTOM_IBSS_AMPDU_BA_WSIZE) */

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
			DHD_ERROR(("%s Set amsdu_aggsf to %d failed  %d\n",
				__FUNCTION__, CUSTOM_AMSDU_AGGSF, ret));
		}
	}
#endif /* CUSTOM_AMSDU_AGGSF */

#if defined(BCMSUP_4WAY_HANDSHAKE)
	/* Read 4-way handshake requirements */
	if (dhd_use_idsup == 1) {
		ret = dhd_iovar(dhd, 0, "sup_wpa", (char *)&sup_wpa, sizeof(sup_wpa),
				(char *)&iovbuf, sizeof(iovbuf), FALSE);
		/* sup_wpa iovar returns NOTREADY status on some platforms using modularized
		 * in-dongle supplicant.
		 */
		if (ret >= 0 || ret == BCME_NOTREADY)
			dhd->fw_4way_handshake = TRUE;
		DHD_TRACE(("4-way handshake mode is: %d\n", dhd->fw_4way_handshake));
	}
#endif /* BCMSUP_4WAY_HANDSHAKE */
#if defined(SUPPORT_2G_VHT) || defined(SUPPORT_5G_1024QAM_VHT)
	ret = dhd_iovar(dhd, 0, "vht_features", (char *)&vht_features, sizeof(vht_features),
			(char *)&vht_features, sizeof(vht_features), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s vht_features get failed %d\n", __FUNCTION__, ret));
		vht_features = 0;
	} else {
#ifdef SUPPORT_2G_VHT
		vht_features |= 0x3; /* 2G support */
#endif /* SUPPORT_2G_VHT */
#ifdef SUPPORT_5G_1024QAM_VHT
		vht_features |= 0x6; /* 5G 1024 QAM support */
#endif /* SUPPORT_5G_1024QAM_VHT */
	}
	if (vht_features) {
		ret = dhd_iovar(dhd, 0, "vht_features", (char *)&vht_features, sizeof(vht_features),
				NULL, 0, TRUE);
		if (ret < 0) {
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
			if (ret != BCME_BADOPTION) {
				DHD_ERROR(("%s vht_features set failed %d\n", __FUNCTION__, ret));
			} else {
				DHD_INFO(("%s vht_features ret(%d) - need to check BANDLOCK\n",
					__FUNCTION__, ret));
			}
		}
	}
#endif /* SUPPORT_2G_VHT || SUPPORT_5G_1024QAM_VHT */
#ifdef DISABLE_11N_PROPRIETARY_RATES
	ret = dhd_iovar(dhd, 0, "ht_features", (char *)&ht_features, sizeof(ht_features), NULL, 0,
			TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s ht_features set failed %d\n", __FUNCTION__, ret));
	}
#endif /* DISABLE_11N_PROPRIETARY_RATES */
#if defined(DISABLE_HE_ENAB) || defined(CUSTOM_CONTROL_HE_ENAB)
#if defined(DISABLE_HE_ENAB)
	/* XXX DISABLE_HE_ENAB has higher priority than CUSTOM_CONTROL_HE_ENAB */
	control_he_enab = 0;
#endif /* DISABLE_HE_ENAB */
	dhd_control_he_enab(dhd, control_he_enab);
#endif /* DISABLE_HE_ENAB || CUSTOM_CONTROL_HE_ENAB */

#ifdef CUSTOM_PSPRETEND_THR
	/* Turn off MPC in AP mode */
	ret = dhd_iovar(dhd, 0, "pspretend_threshold", (char *)&pspretend_thr,
			sizeof(pspretend_thr), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s pspretend_threshold for HostAPD failed  %d\n",
			__FUNCTION__, ret));
	}
#endif

	/* XXX Enable firmware key buffering before sent 4-way M4 */
	ret = dhd_iovar(dhd, 0, "buf_key_b4_m4", (char *)&buf_key_b4_m4, sizeof(buf_key_b4_m4),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s buf_key_b4_m4 set failed %d\n", __FUNCTION__, ret));
	}
#ifdef SUPPORT_SET_CAC
	ret = dhd_iovar(dhd, 0, "cac", (char *)&cac, sizeof(cac), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Failed to set cac to %d, %d\n", __FUNCTION__, cac, ret));
	}
#endif /* SUPPORT_SET_CAC */
	/* make up event mask ext message iovar for event larger than 128 */
	msglen = WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE;
	eventmask_msg = (eventmsgs_ext_t*)MALLOC(dhd->osh, msglen);
	if (eventmask_msg == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for event_msg_ext\n", msglen));
		ret = BCME_NOMEM;
		goto done;
	}
	bzero(eventmask_msg, msglen);
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->len = ROUNDUP(WLC_E_LAST, NBBY)/NBBY;

	/* Read event_msgs_ext mask */
	ret = dhd_iovar(dhd, 0, "event_msgs_ext", (char *)eventmask_msg, msglen, iov_buf,
			WLC_IOCTL_SMLEN, FALSE);

	/* event_msgs_ext must be supported */
	if (ret != BCME_OK) {
		DHD_ERROR(("%s read event mask ext failed %d\n", __FUNCTION__, ret));
		goto done;
	}

	bcopy(iov_buf, eventmask_msg, msglen);
	/* make up event mask ext message iovar for event larger than 128 */
	mask = eventmask_msg->mask;

	/* Setup event_msgs */
	setbit(mask, WLC_E_SET_SSID);
	setbit(mask, WLC_E_PRUNE);
	setbit(mask, WLC_E_AUTH);
	setbit(mask, WLC_E_AUTH_IND);
	setbit(mask, WLC_E_ASSOC);
	setbit(mask, WLC_E_REASSOC);
	setbit(mask, WLC_E_REASSOC_IND);
	if (!(dhd->op_mode & DHD_FLAG_IBSS_MODE))
		setbit(mask, WLC_E_DEAUTH);
	setbit(mask, WLC_E_DEAUTH_IND);
	setbit(mask, WLC_E_DISASSOC_IND);
	setbit(mask, WLC_E_DISASSOC);
	setbit(mask, WLC_E_JOIN);
	setbit(mask, WLC_E_START);
	setbit(mask, WLC_E_ASSOC_IND);
	setbit(mask, WLC_E_PSK_SUP);
	setbit(mask, WLC_E_LINK);
	setbit(mask, WLC_E_MIC_ERROR);
	setbit(mask, WLC_E_ASSOC_REQ_IE);
	setbit(mask, WLC_E_ASSOC_RESP_IE);
#ifdef LIMIT_BORROW
	setbit(mask, WLC_E_ALLOW_CREDIT_BORROW);
#endif
#ifndef WL_CFG80211
	setbit(mask, WLC_E_PMKID_CACHE);
//	setbit(mask, WLC_E_TXFAIL); // terence 20181106: remove unnecessary event
#endif
	setbit(mask, WLC_E_JOIN_START);
//	setbit(mask, WLC_E_SCAN_COMPLETE); // terence 20150628: remove redundant event
#ifdef DHD_DEBUG
	setbit(mask, WLC_E_SCAN_CONFIRM_IND);
#endif
#ifdef PNO_SUPPORT
	setbit(mask, WLC_E_PFN_NET_FOUND);
	setbit(mask, WLC_E_PFN_BEST_BATCHING);
	setbit(mask, WLC_E_PFN_BSSID_NET_FOUND);
	setbit(mask, WLC_E_PFN_BSSID_NET_LOST);
#endif /* PNO_SUPPORT */
	/* enable dongle roaming event */
#ifdef WL_CFG80211
#if !defined(ROAM_EVT_DISABLE)
	setbit(mask, WLC_E_ROAM);
#endif /* !ROAM_EVT_DISABLE */
	setbit(mask, WLC_E_BSSID);
#endif /* WL_CFG80211 */
#ifdef BCMCCX
	setbit(mask, WLC_E_ADDTS_IND);
	setbit(mask, WLC_E_DELTS_IND);
#endif /* BCMCCX */
#ifdef WLTDLS
	setbit(mask, WLC_E_TDLS_PEER_EVENT);
#endif /* WLTDLS */
#ifdef WL_ESCAN
	setbit(mask, WLC_E_ESCAN_RESULT);
#endif /* WL_ESCAN */
#ifdef CSI_SUPPORT
	setbit(mask, WLC_E_CSI);
#endif /* CSI_SUPPORT */
#ifdef RTT_SUPPORT
	setbit(mask, WLC_E_PROXD);
#endif /* RTT_SUPPORT */
#if !defined(WL_CFG80211) && !defined(OEM_ANDROID)
	setbit(mask, WLC_E_ESCAN_RESULT);
#endif
#ifdef WL_CFG80211
	setbit(mask, WLC_E_ESCAN_RESULT);
	setbit(mask, WLC_E_AP_STARTED);
	setbit(mask, WLC_E_ACTION_FRAME_RX);
	if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
		setbit(mask, WLC_E_P2P_DISC_LISTEN_COMPLETE);
	}
#endif /* WL_CFG80211 */
#ifdef WLAIBSS
	setbit(mask, WLC_E_AIBSS_TXFAIL);
#endif /* WLAIBSS */

#if defined(SHOW_LOGTRACE) && defined(LOGTRACE_FROM_FILE)
	if (dhd_logtrace_from_file(dhd)) {
		setbit(mask, WLC_E_TRACE);
	} else {
		clrbit(mask, WLC_E_TRACE);
	}
#elif defined(SHOW_LOGTRACE)
	setbit(mask, WLC_E_TRACE);
#else
	clrbit(mask, WLC_E_TRACE);
#endif /* defined(SHOW_LOGTRACE) && defined(LOGTRACE_FROM_FILE) */

	setbit(mask, WLC_E_CSA_COMPLETE_IND);
#ifdef DHD_WMF
	setbit(mask, WLC_E_PSTA_PRIMARY_INTF_IND);
#endif
#ifdef CUSTOM_EVENT_PM_WAKE
	setbit(mask, WLC_E_EXCESS_PM_WAKE_EVENT);
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef DHD_LOSSLESS_ROAMING
	setbit(mask, WLC_E_ROAM_PREP);
#endif
	/* nan events */
	setbit(mask, WLC_E_NAN);
#if defined(PCIE_FULL_DONGLE) && defined(DHD_LOSSLESS_ROAMING)
	dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_LLR_MAP);
#endif /* defined(PCIE_FULL_DONGLE) && defined(DHD_LOSSLESS_ROAMING) */

#if defined(BCMPCIE) && defined(EAPOL_PKT_PRIO)
	dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_LLR_MAP);
#endif /* defined(BCMPCIE) && defined(EAPOL_PKT_PRIO) */

#ifdef RSSI_MONITOR_SUPPORT
	setbit(mask, WLC_E_RSSI_LQM);
#endif /* RSSI_MONITOR_SUPPORT */
#ifdef GSCAN_SUPPORT
	setbit(mask, WLC_E_PFN_GSCAN_FULL_RESULT);
	setbit(mask, WLC_E_PFN_SCAN_COMPLETE);
	setbit(mask, WLC_E_PFN_SSID_EXT);
	setbit(mask, WLC_E_ROAM_EXP_EVENT);
#endif /* GSCAN_SUPPORT */
	setbit(mask, WLC_E_RSSI_LQM);
#ifdef BT_WIFI_HANDOVER
	setbit(mask, WLC_E_BT_WIFI_HANDOVER_REQ);
#endif /* BT_WIFI_HANDOVER */
#ifdef DBG_PKT_MON
	setbit(mask, WLC_E_ROAM_PREP);
#endif /* DBG_PKT_MON */
#ifdef WL_NATOE
	setbit(mask, WLC_E_NATOE_NFCT);
#endif /* WL_NATOE */
#ifdef BCM_ROUTER_DHD
	setbit(mask, WLC_E_DPSTA_INTF_IND);
#endif /* BCM_ROUTER_DHD */
	setbit(mask, WLC_E_SLOTTED_BSS_PEER_OP);
#ifdef WL_BCNRECV
	setbit(mask, WLC_E_BCNRECV_ABORTED);
#endif /* WL_BCNRECV */
#ifdef WL_MBO
	setbit(mask, WLC_E_MBO);
#endif /* WL_MBO */
#ifdef WL_CLIENT_SAE
	setbit(mask, WLC_E_JOIN_START);
#endif /* WL_CLIENT_SAE */
#ifdef WL_CAC_TS
	setbit(mask, WLC_E_ADDTS_IND);
	setbit(mask, WLC_E_DELTS_IND);
#endif /* WL_BCNRECV */
	setbit(mask, WLC_E_COUNTRY_CODE_CHANGED);

	/* Write updated Event mask */
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_SET_MASK;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;
	ret = dhd_iovar(dhd, 0, "event_msgs_ext", (char *)eventmask_msg, msglen, NULL, 0,
			TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s write event mask ext failed %d\n", __FUNCTION__, ret));
		goto done;
	}

#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	/* Enabling event log trace for EAP events */
	el_tag = (wl_el_tag_params_t *)MALLOC(dhd->osh, sizeof(wl_el_tag_params_t));
	if (el_tag == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for event_msg_ext\n",
				(int)sizeof(wl_el_tag_params_t)));
		ret = BCME_NOMEM;
		goto done;
	}
	el_tag->tag = EVENT_LOG_TAG_4WAYHANDSHAKE;
	el_tag->set = 1;
	el_tag->flags = EVENT_LOG_TAG_FLAG_LOG;
	ret = dhd_iovar(dhd, 0, "event_log_tag_control", (char *)el_tag, sizeof(*el_tag), NULL,
			0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set event_log_tag_control fail %d\n", __FUNCTION__, ret));
	}
#endif /* DHD_8021X_DUMP */
#ifdef DHD_RANDMAC_LOGGING
	if (FW_SUPPORTED((dhd), event_log)) {
		if (dhd_iovar(dhd, 0, "privacy_mask", (char *)&privacy_mask, sizeof(privacy_mask),
			NULL, 0, TRUE) < 0) {
			DHD_ERROR(("failed to set privacy mask\n"));
		}
	} else {
		/* Don't enable feature to prevent macaddr print in clr text */
		DHD_ERROR(("skip privacy_mask set. event_log not enabled\n"));
	}
#endif /* DHD_RANDMAC_LOGGING */

#ifdef OEM_ANDROID
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
			sizeof(scan_assoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
			sizeof(scan_unassoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_PASSIVE_TIME, (char *)&scan_passive_time,
			sizeof(scan_passive_time), TRUE, 0);

#ifdef ARP_OFFLOAD_SUPPORT
	DHD_ERROR(("arp_enable:%d arp_ol:%d\n",
		dhd->arpoe_enable, dhd->arpol_configured));
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

#ifdef BLOCK_IPV6_PACKET
		/* Setup filter to allow only IPv4 unicast frames */
		dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 "
			HEX_PREF_STR UNI_FILTER_STR ZERO_ADDR_STR ETHER_TYPE_STR IPV6_FILTER_STR
			" "
			HEX_PREF_STR ZERO_ADDR_STR ZERO_ADDR_STR ETHER_TYPE_STR ZERO_TYPE_STR;
#else
		/* Setup filter to allow only unicast */
		dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 0x01 0x00";
#endif /* BLOCK_IPV6_PACKET */

#ifdef PASS_IPV4_SUSPEND
		/* XXX customer want to get IPv4 multicast packets */
		dhd->pktfilter[DHD_MDNS_FILTER_NUM] = "104 0 0 0 0xFFFFFF 0x01005E";
#else
		/* Add filter to pass multicastDNS packet and NOT filter out as Broadcast */
		dhd->pktfilter[DHD_MDNS_FILTER_NUM] = NULL;
#endif /* PASS_IPV4_SUSPEND */
		if (FW_SUPPORTED(dhd, pf6)) {
			/* Immediately pkt filter TYPE 6 Dicard Broadcast IP packet */
			dhd->pktfilter[DHD_IP4BCAST_DROP_FILTER_NUM] = DISCARD_IPV4_BCAST;
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
		/* XXX Andrey: fo SOFTAP disable pkt filters (if there were any )  */
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
	ret = dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn, sizeof(bcn_li_bcn),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: set bcn_li_bcn failed %d\n", __FUNCTION__, ret));
	}
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
#ifdef AMPDU_VO_ENABLE
	/* XXX: Enabling VO AMPDU to reduce FER */
	tid.tid = PRIO_8021D_VO; /* Enable TID(6) for voice */
	tid.enable = TRUE;
	ret = dhd_iovar(dhd, 0, "ampdu_tid", (char *)&tid, sizeof(tid), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s ampdu_tid %d\n", __FUNCTION__, ret));
	}

	tid.tid = PRIO_8021D_NC; /* Enable TID(7) for voice */
	tid.enable = TRUE;
	ret = dhd_iovar(dhd, 0, "ampdu_tid", (char *)&tid, sizeof(tid), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s ampdu_tid %d\n", __FUNCTION__, ret));
	}
#endif
#if defined(SOFTAP_TPUT_ENHANCE)
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
#if defined(BCMSDIO)
		dhd_bus_setidletime(dhd, (int)100);
#endif /* BCMSDIO */
#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpack_suppress_set(dhd, TCPACK_SUP_OFF);
#endif
#if defined(DHD_TCP_WINSIZE_ADJUST)
		dhd_use_tcp_window_size_adjust = TRUE;
#endif

#if defined(BCMSDIO)
		memset(buf, 0, sizeof(buf));
		ret = dhd_iovar(dhd, 0, "bus:txglom_auto_control", NULL, 0, buf, sizeof(buf),
				FALSE);
		if (ret < 0) {
			glom = 0;
			ret = dhd_iovar(dhd, 0, "bus:txglom", (char *)&glom, sizeof(glom),
					NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s bus:txglom failed %d\n", __FUNCTION__, ret));
			}
		} else {
			if (buf[0] == 0) {
				glom = 1;
				ret = dhd_iovar(dhd, 0, "bus:txglom_auto_control", (char *)&glom,
						sizeof(glom), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s bus:txglom_auto_control failed %d\n",
						__FUNCTION__, ret));
				}
			}
		}
#endif /* BCMSDIO */
	}
#endif /* SOFTAP_TPUT_ENHANCE */
	/* query for 'clmver' to get clm version info from firmware */
	bzero(buf, sizeof(buf));
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

#if defined(CUSTOMER_HW4_DEBUG)
		if ((ver_temp_buf = bcmstrstr(ptr, "Customization:")) == NULL) {
			DHD_ERROR(("Couldn't find \"Customization:\"\n"));
		} else {
			char tokenlim;
			ptr = (ver_temp_buf + strlen("Customization:"));
			if ((ver_temp_buf = bcmstrtok(&ptr, "(\n", &tokenlim)) == NULL) {
				DHD_ERROR(("Couldn't find project blob version"
					"or New line character\n"));
			} else if (tokenlim == '(') {
				snprintf(clm_version,
					CLM_VER_STR_LEN - 1, "%s, Blob ver = Major : %s minor : ",
					clm_version, ver_temp_buf);
				DHD_INFO(("[INFO]CLM/Blob version = %s\n", clm_version));
				if ((ver_temp_buf = bcmstrtok(&ptr, "\n", &tokenlim)) == NULL) {
					DHD_ERROR(("Couldn't find New line character\n"));
				} else {
					snprintf(clm_version,
						strlen(clm_version) + strlen(ver_temp_buf),
						"%s%s",	clm_version, ver_temp_buf);
					DHD_INFO(("[INFO]CLM/Blob/project version = %s\n",
						clm_version));

				}
			} else if (tokenlim == '\n') {
				snprintf(clm_version,
					strlen(clm_version) + strlen(", Blob ver = Major : ") + 1,
					"%s, Blob ver = Major : ", clm_version);
				snprintf(clm_version,
					strlen(clm_version) + strlen(ver_temp_buf) + 1,
					"%s%s",	clm_version, ver_temp_buf);
				DHD_INFO(("[INFO]CLM/Blob/project version = %s\n", clm_version));
			}
		}
#endif /* CUSTOMER_HW4_DEBUG */
		if (strlen(clm_version)) {
			DHD_INFO(("CLM version = %s\n", clm_version));
		} else {
			DHD_ERROR(("Couldn't find CLM version!\n"));
		}
	}
	dhd_set_version_info(dhd, fw_version);

#ifdef WRITE_WLANINFO
	sec_save_wlinfo(fw_version, EPI_VERSION_STR, dhd->info->nv_path, clm_version);
#endif /* WRITE_WLANINFO */

#endif /* defined(OEM_ANDROID) */
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

#ifdef READ_CONFIG_FROM_FILE
	dhd_preinit_config(dhd, 0);
#endif /* READ_CONFIG_FROM_FILE */

	if (wlfc_enable) {
		dhd_wlfc_init(dhd);
		/* terence 20161229: enable ampdu_hostreorder if tlv enabled */
		dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "ampdu_hostreorder", 1, 0, TRUE);
	}
#ifndef DISABLE_11N
	else if (hostreorder)
		dhd_wlfc_hostreorder_init(dhd);
#endif /* DISABLE_11N */
#else
	/* terence 20161229: disable ampdu_hostreorder if PROP_TXSTATUS not defined */
	printf("%s: not define PROP_TXSTATUS\n", __FUNCTION__);
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "ampdu_hostreorder", 0, 0, TRUE);
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

#ifdef DHD_PKTTS
	/* get the pkt metadata buffer length supported by FW */
	if (dhd_wl_ioctl_get_intiovar(dhd, "bus:metadata_info", &val,
			WLC_GET_VAR, FALSE, 0) != BCME_OK) {
		DHD_ERROR(("%s: failed to get pkt metadata buflen, use IPC pkt TS.\n",
				__FUNCTION__));
		/*
		 * if iovar fails, IPC method of collecting
		 * TS should be used, hence set metadata_buflen as
		 * 0 here. This will be checked later on Tx completion
		 * to decide if IPC or metadata method of reading TS
		 * should be used
		 */
		dhd->pkt_metadata_version = 0;
		dhd->pkt_metadata_buflen = 0;
	} else {
		dhd->pkt_metadata_version  = GET_METADATA_VER(val);
		dhd->pkt_metadata_buflen  = GET_METADATA_BUFLEN(val);
	}

	/* Check FW supports pktlat, if supports enable pktts_enab iovar */
	ret = dhd_set_pktts_enab(dhd, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: Enabling pktts_enab failed, ret=%d\n", __FUNCTION__, ret));
	}
#endif /* DHD_PKTTS */

#ifdef RTT_SUPPORT
	if (dhd->rtt_state) {
		ret = dhd_rtt_init(dhd);
		if (ret < 0) {
			DHD_ERROR(("%s failed to initialize RTT\n", __FUNCTION__));
		}
	}
#endif
#ifdef FILTER_IE
	/* Failure to configure filter IE is not a fatal error, ignore it. */
	if (FW_SUPPORTED(dhd, fie) &&
		!(dhd->op_mode & (DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))) {
		dhd_read_from_file(dhd);
	}
#endif /* FILTER_IE */
#ifdef WL11U
	dhd_interworking_enable(dhd);
#endif /* WL11U */

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

	/* check dongle supports wbtext (product policy) or not */
	dhd->wbtext_support = FALSE;
	if (dhd_wl_ioctl_get_intiovar(dhd, "wnm_bsstrans_resp", &wnm_bsstrans_resp,
			WLC_GET_VAR, FALSE, 0) != BCME_OK) {
		DHD_ERROR(("failed to get wnm_bsstrans_resp\n"));
	}
	dhd->wbtext_policy = wnm_bsstrans_resp;
	if (dhd->wbtext_policy == WL_BSSTRANS_POLICY_PRODUCT_WBTEXT) {
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

#ifdef DHD_NON_DMA_M2M_CORRUPTION
	/* check pcie non dma loopback */
	if (dhd->op_mode == DHD_FLAG_MFG_MODE &&
		(dhd_bus_dmaxfer_lpbk(dhd, M2M_NON_DMA_LPBK) < 0)) {
			goto done;
	}
#endif /* DHD_NON_DMA_M2M_CORRUPTION */

	/* WNM capabilities */
	wnm_cap = 0
#ifdef WL11U
		| WL_WNM_BSSTRANS | WL_WNM_NOTIF
#endif
#ifdef WBTEXT
		| WL_WNM_BSSTRANS | WL_WNM_MAXIDLE
#endif
		;
#if defined(WL_MBO) && defined(WL_OCE)
	if (FW_SUPPORTED(dhd, estm)) {
		wnm_cap |= WL_WNM_ESTM;
	}
#endif /* WL_MBO && WL_OCE */
	if (dhd_iovar(dhd, 0, "wnm", (char *)&wnm_cap, sizeof(wnm_cap), NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set WNM capabilities\n"));
	}

#ifdef CUSTOM_ASSOC_TIMEOUT
	/* set recreate_bi_timeout to increase assoc timeout :
	* 20 * 100TU * 1024 / 1000 = 2 secs
	* (beacon wait time = recreate_bi_timeout * beacon_period * 1024 / 1000)
	*/
	if (dhd_wl_ioctl_set_intiovar(dhd, "recreate_bi_timeout",
			CUSTOM_ASSOC_TIMEOUT,
			WLC_SET_VAR, TRUE, 0) != BCME_OK) {
		DHD_ERROR(("failed to set assoc timeout\n"));
	}
#endif /* CUSTOM_ASSOC_TIMEOUT */

#if defined(WBTEXT) && defined(WBTEXT_BTMDELTA)
	if (dhd_iovar(dhd, 0, "wnm_btmdelta", (char *)&btmdelta, sizeof(btmdelta),
			NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set BTM delta\n"));
	}
#endif /* WBTEXT && WBTEXT_BTMDELTA */
#if defined(WBTEXT) && defined(RRM_BCNREQ_MAX_CHAN_TIME)
	if (dhd_iovar(dhd, 0, "rrm_bcn_req_thrtl_win",
		(char *)&rrm_bcn_req_thrtl_win, sizeof(rrm_bcn_req_thrtl_win),
		NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set RRM BCN request thrtl_win\n"));
	}
	if (dhd_iovar(dhd, 0, "rrm_bcn_req_max_off_chan_time",
		(char *)&rrm_bcn_req_max_off_chan_time, sizeof(rrm_bcn_req_max_off_chan_time),
		NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set RRM BCN Request max_off_chan_time\n"));
	}
#endif /* WBTEXT && RRM_BCNREQ_MAX_CHAN_TIME */

#ifdef WL_MONITOR
#ifdef HOST_RADIOTAP_CONV
	/* 'Wl monitor' IOVAR is fired to check whether the FW supports radiotap conversion or not.
	 * This is indicated through MSB(1<<31) bit, based on which host radiotap conversion
	 * will be enabled or disabled.
	 * 0 - Host supports Radiotap conversion.
	 * 1 - FW supports Radiotap conversion.
	 */
	bcm_mkiovar("monitor", (char *)&monitor, sizeof(monitor), iovbuf, sizeof(iovbuf));
	if ((ret2 = dhd_wl_ioctl_cmd(dhd, WLC_GET_MONITOR, iovbuf,
		sizeof(iovbuf), FALSE, 0)) == 0) {
		memcpy(&monitor, iovbuf, sizeof(monitor));
		dhdinfo->host_radiotap_conv = (monitor & HOST_RADIOTAP_CONV_BIT) ? TRUE : FALSE;
	} else {
		DHD_ERROR(("%s Failed to get monitor mode, err %d\n",
			__FUNCTION__, ret2));
	}
#endif /* HOST_RADIOTAP_CONV */
	if (FW_SUPPORTED(dhd, monitor)) {
		dhd->monitor_enable = TRUE;
		DHD_ERROR(("%s: Monitor mode is enabled in FW cap\n", __FUNCTION__));
	} else {
		dhd->monitor_enable = FALSE;
		DHD_ERROR(("%s: Monitor mode is not enabled in FW cap\n", __FUNCTION__));
	}
#endif /* WL_MONITOR */

	/* store the preserve log set numbers */
	if (dhd_get_preserve_log_numbers(dhd, &dhd->logset_prsrv_mask)
			!= BCME_OK) {
		DHD_ERROR(("%s: Failed to get preserve log # !\n", __FUNCTION__));
	}

	if (FW_SUPPORTED(dhd, ecounters) && enable_ecounter) {
		dhd_ecounter_configure(dhd, TRUE);
	}

#ifdef CONFIG_SILENT_ROAM
	dhd->sroam_turn_on = TRUE;
	dhd->sroamed = FALSE;
#endif /* CONFIG_SILENT_ROAM */
	dhd_set_bandlock(dhd);

	dhd_conf_postinit_ioctls(dhd);
done:

	if (eventmask_msg) {
		MFREE(dhd->osh, eventmask_msg, msglen);
	}
	if (iov_buf) {
		MFREE(dhd->osh, iov_buf, WLC_IOCTL_SMLEN);
	}
#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	if (el_tag) {
		MFREE(dhd->osh, el_tag, sizeof(wl_el_tag_params_t));
	}
#endif /* DHD_8021X_DUMP */
	return ret;
}

/* Deafult enable preinit optimisation */
#define DHD_PREINIT_OPTIMISATION

int
dhd_preinit_ioctls(dhd_pub_t *dhd)
{
	int ret = 0;

#ifdef DHD_PREINIT_OPTIMISATION
	int preinit_status = 0;
	ret = dhd_iovar(dhd, 0, "preinit_status", NULL, 0, (char *)&preinit_status,
		sizeof(preinit_status), FALSE);

	if (ret == BCME_OK) {
		DHD_ERROR(("%s: preinit_status IOVAR present, use optimised preinit\n",
			__FUNCTION__));
		dhd->fw_preinit = TRUE;
		ret = dhd_optimised_preinit_ioctls(dhd);
	} else if (ret == BCME_UNSUPPORTED) {
		DHD_ERROR(("%s: preinit_status IOVAR not supported, use legacy preinit\n",
			__FUNCTION__));
		dhd->fw_preinit = FALSE;
		ret = dhd_legacy_preinit_ioctls(dhd);
	} else {
		DHD_ERROR(("%s: preinit_status IOVAR returned err(%d), ABORT\n",
			__FUNCTION__, ret));
	}
#else
	dhd->fw_preinit = FALSE;
	ret = dhd_legacy_preinit_ioctls(dhd);
#endif /* DHD_PREINIT_OPTIMISATION */
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

#ifndef DHD_TPUT_PATCH
	if (netif_running(dev)) {
		DHD_ERROR(("%s: Must be down to change its MTU\n", dev->name));
		return BCME_NOTDOWN;
	}
#endif

#define DHD_MIN_MTU 1500
#define DHD_MAX_MTU 1752

	if ((new_mtu < DHD_MIN_MTU) || (new_mtu > DHD_MAX_MTU)) {
		DHD_ERROR(("%s: MTU size %d is invalid.\n", __FUNCTION__, new_mtu));
		return BCME_BADARG;
	}

	dev->mtu = new_mtu;
	return 0;
}

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT) && defined(DHD_FW_COREDUMP)
static int dhd_wait_for_file_dump(dhd_pub_t *dhdp)
{
	int ret = BCME_OK;
	struct net_device *primary_ndev;
	struct bcm_cfg80211 *cfg;
	unsigned long flags = 0;
	primary_ndev = dhd_linux_get_primary_netdev(dhdp);

	if (!primary_ndev) {
		DHD_ERROR(("%s: Cannot find primary netdev\n", __FUNCTION__));
		return BCME_ERROR;
	}
	cfg = wl_get_cfg(primary_ndev);

	if (!cfg) {
		DHD_ERROR(("%s: Cannot find cfg\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_GENERAL_LOCK(dhdp, flags);
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_BUS_BUSY_CLEAR_IN_HALDUMP(dhdp);
		dhd_os_busbusy_wake(dhdp);
		DHD_GENERAL_UNLOCK(dhdp, flags);
		DHD_ERROR(("%s: bus is down! can't collect log dump. \n", __FUNCTION__));
		return BCME_ERROR;
	}
	DHD_BUS_BUSY_SET_IN_HALDUMP(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);

	DHD_OS_WAKE_LOCK(dhdp);
	/* check for hal started and only then send event if not clear dump state here */
	if (wl_cfg80211_is_hal_started(cfg)) {
		int timeleft = 0;

		DHD_ERROR(("[DUMP] %s: HAL started. send urgent event\n", __FUNCTION__));
		dhd_dbg_send_urgent_evt(dhdp, NULL, 0);

		DHD_ERROR(("%s: wait to clear dhd_bus_busy_state: 0x%x\n",
			__FUNCTION__, dhdp->dhd_bus_busy_state));
		timeleft = dhd_os_busbusy_wait_bitmask(dhdp,
				&dhdp->dhd_bus_busy_state, DHD_BUS_BUSY_IN_HALDUMP, 0);
		if ((dhdp->dhd_bus_busy_state & DHD_BUS_BUSY_IN_HALDUMP) != 0) {
			DHD_ERROR(("%s: Timed out(%d) dhd_bus_busy_state=0x%x\n",
					__FUNCTION__, timeleft, dhdp->dhd_bus_busy_state));
			ret = BCME_ERROR;
		}
	} else {
		DHD_ERROR(("[DUMP] %s: HAL Not started. skip urgent event\n", __FUNCTION__));
		ret = BCME_ERROR;
	}

	DHD_OS_WAKE_UNLOCK(dhdp);
	/* In case of dhd_os_busbusy_wait_bitmask() timeout,
	 * hal dump bit will not be cleared. Hence clearing it here.
	 */
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_HALDUMP(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);

	return ret;
}
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT && DHD_FW_CORE_DUMP */

#ifdef ARP_OFFLOAD_SUPPORT
/* add or remove AOE host ip(s) (up to 8 IPs on the interface)  */
/* XXX add operation is more efficent */
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

/* XXX this function is only for IP address */
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

	if (!ifa || !(ifa->ifa_dev->dev))
		return NOTIFY_DONE;

	/* Filter notifications meant for non Broadcom devices */
	if ((ifa->ifa_dev->dev->netdev_ops != &dhd_ops_pri) &&
	    (ifa->ifa_dev->dev->netdev_ops != &dhd_ops_virt)) {
#if defined(WL_ENABLE_P2P_IF)
		if (!wl_cfgp2p_is_ifops(ifa->ifa_dev->dev->netdev_ops))
#endif /* WL_ENABLE_P2P_IF */
			return NOTIFY_DONE;
	}

	dhd = DHD_DEV_INFO(ifa->ifa_dev->dev);
	if (!dhd)
		return NOTIFY_DONE;

	dhd_pub = &dhd->pub;

	if (!dhd_pub->arpoe_enable) {
		DHD_ERROR(("arpoe_enable not set"));
		return NOTIFY_DONE;
	}

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

			/*
			 * Skip if Bus is not in a state to transport the IOVAR
			 * (or) the Dongle is not ready.
			 */
			if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(&dhd->pub) ||
				dhd->pub.busstate ==  DHD_BUS_LOAD) {
				DHD_ERROR(("%s: bus not ready, exit NETDEV_UP : %d\n",
					__FUNCTION__, dhd->pub.busstate));
				if (dhd->pend_ipaddr) {
					DHD_ERROR(("%s: overwrite pending ipaddr: 0x%x\n",
						__FUNCTION__, dhd->pend_ipaddr));
				}
				dhd->pend_ipaddr = ifa->ifa_address;
				break;
			}

#ifdef AOE_IP_ALIAS_SUPPORT
			/* XXX HOSTAPD will be rerturned at first */
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
			/* XXX HOSTAPD will be rerturned at first */
			DHD_ARPOE(("%s:interface is down, AOE clr all for this if\n",
				__FUNCTION__));
			if ((dhd_pub->op_mode & DHD_FLAG_HOSTAP_MODE) ||
				(ifa->ifa_dev->dev != dhd_linux_get_primary_netdev(dhd_pub))) {
				aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, FALSE, idx);
			} else
#endif /* AOE_IP_ALIAS_SUPPORT */
			{
				/* XXX clear ALL arp and hostip tables */
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
} /* dhd_init_logstrs_array */

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

	/* Filter notifications meant for non Broadcom devices */
	if (inet6_ifa->idev->dev->netdev_ops != &dhd_ops_pri) {
			return NOTIFY_DONE;
	}

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

/* Network attach to be invoked from the bus probe handlers */
int
dhd_attach_net(dhd_pub_t *dhdp, bool need_rtnl_lock)
{
	struct net_device *primary_ndev;
#ifdef GET_CUSTOM_MAC_ENABLE
	char hw_ether[62];
#endif /* GET_CUSTOM_MAC_ENABLE */
#if defined(GET_CUSTOM_MAC_ENABLE) || defined(GET_OTP_MAC_ENABLE)
	int ret = BCME_ERROR;
#endif /* GET_CUSTOM_MAC_ENABLE || GET_OTP_MAC_ENABLE */

	BCM_REFERENCE(primary_ndev);

#ifdef GET_CUSTOM_MAC_ENABLE
	ret = wifi_platform_get_mac_addr(dhdp->adapter, hw_ether, 0);
	if (!ret)
		bcopy(hw_ether, dhdp->mac.octet, ETHER_ADDR_LEN);
#endif /* GET_CUSTOM_MAC_ENABLE */

#ifdef GET_OTP_MAC_ENABLE
	if (ret && memcmp(&ether_null, &dhdp->conf->otp_mac, ETHER_ADDR_LEN))
		bcopy(&dhdp->conf->otp_mac, &dhdp->mac, ETHER_ADDR_LEN);
#endif /* GET_OTP_MAC_ENABLE */

	/* Register primary net device */
	if (dhd_register_if(dhdp, 0, need_rtnl_lock) != 0) {
		return BCME_ERROR;
	}

#if defined(WL_CFG80211)
	primary_ndev =  dhd_linux_get_primary_netdev(dhdp);
	if (wl_cfg80211_net_attach(primary_ndev) < 0) {
		/* fail the init */
		dhd_remove_if(dhdp, 0, TRUE);
		return BCME_ERROR;
	}
#endif /* WL_CFG80211 */
	return BCME_OK;
}

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

	ASSERT(!net->netdev_ops);
	net->netdev_ops = &dhd_ops_virt;

	/* Ok, link into the network layer... */
	if (ifidx == 0) {
		/*
		 * device functions for the primary interface only
		 */
		net->netdev_ops = &dhd_ops_pri;
		if (!ETHER_ISNULLADDR(dhd->pub.mac.octet))
			memcpy(temp_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
	} else {
		/*
		 * We have to use the primary MAC for virtual interfaces
		 */
		memcpy(temp_addr, ifp->mac_addr, ETHER_ADDR_LEN);
#if defined(OEM_ANDROID)
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
#endif /* defined(OEM_ANDROID) */
	}

	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
#ifdef HOST_SFH_LLC
	net->needed_headroom += DOT11_LLC_SNAP_HDR_LEN;
#endif

#ifdef DHD_AWDL
	if (dhdp->awdl_ifidx &&
		ifidx == dhdp->awdl_ifidx) {
		/* A total of 30 bytes are required for the
		 * ethernet + AWDL LLC header. Out of this 14
		 * bytes in the form of ethernet header is already
		 * present in the skb handed over by the stack.
		 * So we need to reserve an additonal 16 bytes as
		 * headroom. Out of these 16 bytes, if the host
		 * sfh llc feature is being used, then additonal
		 * 8 bytes are already being reserved
		 * during dhd_register_if (below), hence reserving
		 * only an additional 8 bytes is enough. If the host
		 * sfh llc feature is not used, then all of the 16
		 * bytes need to be reserved from here
		 */
		net->needed_headroom += DOT11_LLC_SNAP_HDR_LEN;
#ifndef HOST_SFH_LLC
		net->needed_headroom += DOT11_LLC_SNAP_HDR_LEN;
#endif /* HOST_SFH_LLC */
	}
#endif /* DHD_AWDL */

	net->ethtool_ops = &dhd_ethtool_ops;

#if defined(WL_WIRELESS_EXT)
#if WIRELESS_EXT < 19
	net->get_wireless_stats = dhd_get_wireless_stats;
#endif /* WIRELESS_EXT < 19 */
#if WIRELESS_EXT > 12
	net->wireless_handlers = &wl_iw_handler_def;
#endif /* WIRELESS_EXT > 12 */
#endif /* defined(WL_WIRELESS_EXT) */

	/* XXX Set up an MTU change notifier as per linux/notifier.h? */
	dhd->pub.rxsz = DBUS_RX_BUFFER_SIZE_DHD(net);

#ifdef WLMESH
	if (ifidx >= 2 && dhdp->conf->fw_type == FW_TYPE_MESH) {
		temp_addr[4] ^= 0x80;
		temp_addr[4] += ifidx;
		temp_addr[5] += ifidx;
	}
#endif
	/*
	 * XXX Linux 2.6.25 does not like a blank MAC address, so use a
	 * dummy address until the interface is brought up.
	 */
	dev_addr_set(net, temp_addr);

	if (ifidx == 0)
		printf("%s\n", dhd_version);
	else {
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_update_net_device(net, ifidx);
#endif /* WL_EXT_IAPSTA */
		if (dhd->pub.up == 1) {
			if (_dhd_set_mac_address(dhd, ifidx, net->dev_addr, FALSE) == 0)
				DHD_INFO(("%s: MACID is overwritten\n", __FUNCTION__));
			else
				DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));
		}
	}

	if (need_rtnl_lock)
		err = register_netdev(net);
	else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)) && defined(WL_CFG80211)
		err = cfg80211_register_netdevice(net);
#else
		err = register_netdevice(net);
#endif
	}

	if (err != 0) {
		DHD_ERROR(("couldn't register the net device [%s], err %d\n", net->name, err));
		goto fail;
	}

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
	if ((ctf_dev_register(dhd->cih, net, FALSE) != BCME_OK) ||
	    (ctf_enable(dhd->cih, net, TRUE, &dhd->brc_hot) != BCME_OK)) {
		DHD_ERROR(("%s:%d: ctf_dev_register/ctf_enable failed for interface %d\n",
			__FUNCTION__, __LINE__, ifidx));
		goto fail;
	}
#endif /* BCM_ROUTER_DHD && HNDCTF */

#if defined(WLDWDS) && defined(WL_EXT_IAPSTA)
	if (ifp->dwds) {
		wl_ext_iapsta_attach_dwds_netdev(net, ifidx, ifp->bssidx);
	} else
#endif /* WLDWDS && WL_EXT_IAPSTA */
	{
#ifdef WL_EVENT
		wl_ext_event_attach_netdev(net, ifidx, ifp->bssidx);
#endif /* WL_EVENT */
#ifdef WL_ESCAN
		wl_escan_event_attach(net, ifidx);
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_attach_netdev(net, ifidx, ifp->bssidx);
		wl_ext_iapsta_attach_name(net, ifidx);
#endif /* WL_EXT_IAPSTA */
	}

#if defined(CONFIG_TIZEN)
	net_stat_tizen_register(net);
#endif /* CONFIG_TIZEN */

	printf("Register interface [%s]  MAC: "MACDBG"\n\n", net->name,
#if defined(CUSTOMER_HW4_DEBUG)
		MAC2STRDBG(dhd->pub.mac.octet));
#else
		MAC2STRDBG(net->dev_addr));
#endif /* CUSTOMER_HW4_DEBUG */

#if defined(OEM_ANDROID) && (defined(BCMPCIE) || defined(BCMLXSDMMC) || defined(BCMDBUS))
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

#if defined(BCMPCIE) && defined(DHDTCPACK_SUPPRESS)
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
#endif /* BCMPCIE && DHDTCPACK_SUPPRESS */

#if defined(WLAN_ACCEL_BOOT)
			dhd->fs_check_retry = DHD_FS_CHECK_RETRIES;
			dhd->wl_accel_boot_on_done = FALSE;
			INIT_DELAYED_WORK(&dhd->wl_accel_work, dhd_wifi_accel_on_work_cb);
#if !defined(WLAN_ACCEL_SKIP_WQ_IN_ATTACH)
			/* If the WLAN_ACCEL_SKIP_WQ_IN_ATTACH feature is enabled,
			* the dhd_wifi_accel_on_work_cb() is called in dhd_open()
			* to skip dongle firmware downloading during insmod and dhd_attach.
			*/
			schedule_delayed_work(&dhd->wl_accel_work,
					msecs_to_jiffies(DHD_FS_CHECK_RETRY_DELAY_MS));
#endif /* !defined(WLAN_ACCEL_SKIP_WQ_IN_ATTACH) */
#else
			/* Turn off Wifi after boot up */
#if defined (BT_OVER_SDIO)
			dhd_bus_put(&dhd->pub, WLAN_MODULE);
			wl_android_set_wifi_on_flag(FALSE);
#else
			wl_android_wifi_off(net, TRUE);
#endif /* BT_OVER_SDIO */
#endif /* WLAN_ACCEL_BOOT */

		}
	}
#endif /* OEM_ANDROID && (BCMPCIE || (BCMLXSDMMC) */
#if defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL)
	gdb_proxy_fs_try_create(ifp->info, net->name);
#endif /* defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL) */
	return 0;

fail:
	net->netdev_ops = NULL;
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
				DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
				dhd->pub.busstate = DHD_BUS_DOWN;
#else
				dhd_bus_stop(dhd->pub.bus, TRUE);
#endif /* BCMDBUS */
			}

#if defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID) || defined(BCMPCIE_OOB_HOST_WAKE)
			dhd_bus_oob_intr_unregister(dhdp);
#endif /* OOB_INTR_ONLY || BCMSPI_ANDROID || BCMPCIE_OOB_HOST_WAKE */
		}
	}
}

void dhd_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	unsigned long flags;
	int timer_valid = FALSE;
	struct net_device *dev = NULL;
	dhd_if_t *ifp;
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = NULL;
#endif
	if (!dhdp)
		return;

	dhd = (dhd_info_t *)dhdp->info;
	if (!dhd)
		return;

#if defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL)
	gdb_proxy_fs_remove(dhd);
#endif /* defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL) */

	/* primary interface 0 */
	ifp = dhd->iflist[0];
	if (ifp && ifp->net) {
		dev = ifp->net;
	}

	if (dev) {
		rtnl_lock();
#if defined(WL_CFG80211) && defined(WL_STATIC_IF)
		if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
			wl_cfg80211_static_if_dev_close(dev);
		}
#endif /* WL_CFG80211 && WL_STATIC_IF */
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

	/* XXX	kernel panic issue when first bootup time,
	 *	 rmmod without interface down make unnecessary hang event.
	 */
	DHD_ERROR(("%s: making dhdpub up FALSE\n", __FUNCTION__));
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
#ifdef WL_NANHO
	/* deinit NANHO host module */
	bcm_nanho_deinit(dhd->pub.nanhoi);
#endif /* WL_NANHO */
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

	if (dhd->dhd_state & DHD_ATTACH_STATE_PROT_ATTACH) {

#if defined(OEM_ANDROID) || !defined(BCMSDIO)
		dhd_bus_detach(dhdp);
#endif /* OEM_ANDROID || !BCMSDIO */
#ifdef OEM_ANDROID
#ifdef BCMPCIE
		if (is_reboot == SYS_RESTART) {
			extern bcmdhd_wifi_platdata_t *dhd_wifi_platdata;
			if (dhd_wifi_platdata && !dhdp->dongle_reset) {
				dhdpcie_bus_stop_host_dev(dhdp->bus);
				wifi_platform_set_power(dhd_wifi_platdata->adapters,
					FALSE, WIFI_TURNOFF_DELAY);
			}
		}
#endif /* BCMPCIE */
#endif /* OEM_ANDROID */
#ifndef PCIE_FULL_DONGLE
#if defined(OEM_ANDROID) || !defined(BCMSDIO)
		if (dhdp->prot)
			dhd_prot_detach(dhdp);
#endif /* OEM_ANDROID || !BCMSDIO */
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
		wl_iw_detach(dev);
	}
#endif /* defined(WL_WIRELESS_EXT) */
#ifdef WL_EXT_GENL
	wl_ext_genl_deinit(dev);
#endif
#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_dettach(dev);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
	wl_escan_detach(dev);
#endif /* WL_ESCAN */
#ifdef WL_EVENT
	wl_ext_event_dettach(dhdp);
#endif /* WL_EVENT */

	/* delete all interfaces, start with virtual  */
	if (dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) {
		int i = 1;

		/* Cleanup virtual interfaces */
		dhd_net_if_lock_local(dhd);
		for (i = 1; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				dhd_remove_if(&dhd->pub, i, TRUE);
			}
		}
		dhd_net_if_unlock_local(dhd);

		/* 'ifp' indicates primary interface 0, clean it up. */
		if (ifp && ifp->net) {
#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
			if (dhd->cih)
				ctf_dev_unregister(dhd->cih, ifp->net);
#endif /* BCM_ROUTER_DHD && HNDCTF */

#ifdef WL_CFG80211
			cfg = wl_get_cfg(ifp->net);
#endif
			/* in unregister_netdev case, the interface gets freed by net->destructor
			 * (which is set to free_netdev)
			 */
			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
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
#if defined(BCMSDIO) && !defined(OEM_ANDROID)
			dhd_bus_detach(dhdp);

			if (dhdp->prot)
				dhd_prot_detach(dhdp);
#endif /* BCMSDIO && !OEM_ANDROID */

#ifdef DHD_WMF
			dhd_wmf_cleanup(dhdp, 0);
#endif /* DHD_WMF */
#ifdef DHD_L2_FILTER
			bcm_l2_filter_arp_table_update(dhdp->osh, ifp->phnd_arp_table, TRUE,
				NULL, FALSE, dhdp->tickcnt);
			deinit_l2_filter_arp_table(dhdp->osh, ifp->phnd_arp_table);
			ifp->phnd_arp_table = NULL;
#endif /* DHD_L2_FILTER */

#if (defined(BCM_ROUTER_DHD) && defined(QOS_MAP_SET))
			MFREE(dhdp->osh, ifp->qosmap_up_table, UP_TABLE_MAX);
			ifp->qosmap_up_table_enable = FALSE;
#endif /* BCM_ROUTER_DHD && QOS_MAP_SET */

			dhd_if_del_sta_list(ifp);

			MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
			ifp = NULL;
#ifdef WL_CFG80211
			if (cfg && cfg->wdev)
				cfg->wdev->netdev = NULL;
#endif
		}
	}

	/* Clear the watchdog timer */
	DHD_GENERAL_LOCK(&dhd->pub, flags);
	timer_valid = dhd->wd_timer_valid;
	dhd->wd_timer_valid = FALSE;
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	if (timer_valid)
		del_timer_sync(&dhd->timer);
	DHD_STOP_RPM_TIMER(&dhd->pub);

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

#ifdef WL_NATOE
	if (dhd->pub.nfct) {
		dhd_ct_close(dhd->pub.nfct);
	}
#endif /* WL_NATOE */

	cancel_delayed_work_sync(&dhd->dhd_dpc_dispatcher_work);
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

		/* Unregister from CPU Hotplug framework */
		dhd_unregister_cpuhp_callback(dhd);

		dhd_cpumasks_deinit(dhd);
		DHD_LB_STATS_DEINIT(&dhd->pub);
	}
#endif /* DHD_LB */

#ifdef CSI_SUPPORT
	dhd_csi_deinit(dhdp);
#endif /* CSI_SUPPORT */

#if defined(DNGL_AXI_ERROR_LOGGING) && defined(DHD_USE_WQ_FOR_DNGL_AXI_ERROR)
	cancel_work_sync(&dhd->axi_error_dispatcher_work);
#endif /* DNGL_AXI_ERROR_LOGGING && DHD_USE_WQ_FOR_DNGL_AXI_ERROR */

	DHD_SSSR_REG_INFO_DEINIT(&dhd->pub);
	DHD_SSSR_MEMPOOL_DEINIT(&dhd->pub);

#ifdef DHD_SDTC_ETB_DUMP
	dhd_sdtc_etb_mempool_deinit(&dhd->pub);
#endif /* DHD_SDTC_ETB_DUMP */

#ifdef EWP_EDL
	if (host_edl_support) {
		DHD_EDL_MEM_DEINIT(dhdp);
		host_edl_support = FALSE;
	}
#endif /* EWP_EDL */

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

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	destroy_workqueue(dhd->tx_wq);
	dhd->tx_wq = NULL;
	destroy_workqueue(dhd->rx_wq);
	dhd->rx_wq = NULL;
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
#ifdef DEBUGABILITY
	if (dhdp->dbg) {
#ifdef DBG_PKT_MON
		dhd_os_dbg_detach_pkt_monitor(dhdp);
		osl_spin_lock_deinit(dhd->pub.osh, dhd->pub.dbg->pkt_mon_lock);
#endif /* DBG_PKT_MON */
	}
#endif /* DEBUGABILITY */
	if (dhdp->dbg) {
		dhd_os_dbg_detach(dhdp);
	}
#ifdef DHD_MEM_STATS
	osl_spin_lock_deinit(dhd->pub.osh, dhd->pub.mem_stats_lock);
#endif /* DHD_MEM_STATS */

#if defined(DHD_AWDL) && defined(AWDL_SLOT_STATS)
	osl_spin_lock_deinit(dhd->pub.osh, dhd->pub.awdl_stats_lock);
#endif /* DHD_AWDL && AWDL_SLOT_STATS */
#ifdef DHD_PKT_LOGGING
	dhd_os_detach_pktlog(dhdp);
#endif /* DHD_PKT_LOGGING */
#ifdef DHD_STATUS_LOGGING
	dhd_detach_statlog(dhdp);
#endif /* DHD_STATUS_LOGGING */
#ifdef DHD_PKTDUMP_ROAM
	dhd_dump_pkt_deinit(dhdp);
#endif /* DHD_PKTDUMP_ROAM */
#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
	if (dhd->pub.hang_info) {
		MFREE(dhd->pub.osh, dhd->pub.hang_info, VENDOR_SEND_HANG_EXT_INFO_LEN);
	}
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
#ifdef SHOW_LOGTRACE
	/* Release the skbs from queue for WLC_E_TRACE event */
	dhd_event_logtrace_flush_queue(dhdp);

	/* Wait till event logtrace context finishes */
	dhd_cancel_logtrace_process_sync(dhd);

	/* Remove ring proc entries */
	dhd_dbg_ring_proc_destroy(&dhd->pub);

	if (dhd->dhd_state & DHD_ATTACH_LOGTRACE_INIT) {
		if (dhd->event_data.fmts) {
			MFREE(dhd->pub.osh, dhd->event_data.fmts,
					dhd->event_data.fmts_size);
		}
		if (dhd->event_data.raw_fmts) {
			MFREE(dhd->pub.osh, dhd->event_data.raw_fmts,
					dhd->event_data.raw_fmts_size);
		}
		if (dhd->event_data.raw_sstr) {
			MFREE(dhd->pub.osh, dhd->event_data.raw_sstr,
					dhd->event_data.raw_sstr_size);
		}
		if (dhd->event_data.rom_raw_sstr) {
			MFREE(dhd->pub.osh, dhd->event_data.rom_raw_sstr,
					dhd->event_data.rom_raw_sstr_size);
		}
		dhd->dhd_state &= ~DHD_ATTACH_LOGTRACE_INIT;
	}
#endif /* SHOW_LOGTRACE */
#ifdef BTLOG
	skb_queue_purge(&dhd->bt_log_queue);
#endif	/* BTLOG */
#ifdef PNO_SUPPORT
	if (dhdp->pno_state)
		dhd_pno_deinit(dhdp);
#endif
#ifdef RTT_SUPPORT
	if (dhdp->rtt_state) {
		dhd_rtt_detach(dhdp);
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
	dhd_wake_lock_unlock_destroy(&dhd->wl_wdwake);
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	dhd_wake_lock_unlock_destroy(&dhd->wl_wifi);
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

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
	/* Release CTF pool ONLY after the prot layer is dettached and
	 * pkts, possibly from fast ctfpool are freed into ctfpool/kernel
	 */
#ifdef CTFPOOL
	/* free the buffers in fast pool */
	osl_ctfpool_cleanup(dhd->pub.osh);
#endif /* CTFPOOL */

	/* free ctf resources */
	if (dhd->cih)
		ctf_detach(dhd->cih);
#endif /* BCM_ROUTER_DHD && HNDCTF */
#ifdef BCMDBG
	dhd_macdbg_detach(dhdp);
#endif /* BCMDBG */

#ifdef DUMP_IOCTL_IOV_LIST
	dhd_iov_li_delete(dhdp, &(dhdp->dump_iovlist_head));
#endif /* DUMP_IOCTL_IOV_LIST */
#ifdef DHD_DEBUG
	/* memory waste feature list initilization */
	dhd_mw_list_delete(dhdp, &(dhdp->mw_list_head));
#endif /* DHD_DEBUG */
#ifdef WL_MONITOR
	dhd_del_monitor_if(dhd);
#endif /* WL_MONITOR */

#ifdef DHD_ERPOM
	if (dhdp->enable_erpom) {
		dhdp->pom_func_deregister(&dhdp->pom_wlan_handler);
	}
#endif /* DHD_ERPOM */

	cancel_work_sync(&dhd->dhd_hang_process_work);

	/* Prefer adding de-init code above this comment unless necessary.
	 * The idea is to cancel work queue, sysfs and flags at the end.
	 */
	dhd_deferred_work_deinit(dhd->dhd_deferred_wq);
	dhd->dhd_deferred_wq = NULL;

	/* log dump related buffers should be freed after wq is purged */
#ifdef DHD_LOG_DUMP
	dhd_log_dump_deinit(&dhd->pub);
#endif /* DHD_LOG_DUMP */
#if defined(BCMPCIE)
	if (dhdp->extended_trap_data)
	{
		MFREE(dhdp->osh, dhdp->extended_trap_data, BCMPCIE_EXT_TRAP_DATA_MAXLEN);
		dhdp->extended_trap_data = NULL;
	}
#ifdef DNGL_AXI_ERROR_LOGGING
	if (dhdp->axi_err_dump)
	{
		MFREE(dhdp->osh, dhdp->axi_err_dump, sizeof(dhd_axi_error_dump_t));
		dhdp->axi_err_dump = NULL;
	}
#endif /* DNGL_AXI_ERROR_LOGGING */
#endif /* BCMPCIE */

#ifdef BTLOG
	/* Wait till bt_log_dispatcher_work finishes */
	cancel_work_sync(&dhd->bt_log_dispatcher_work);
#endif /* BTLOG */

#ifdef EWP_EDL
	cancel_delayed_work_sync(&dhd->edl_dispatcher_work);
#endif

	(void)dhd_deinit_sock_flows_buf(dhd);

#ifdef DHD_DUMP_MNGR
	if (dhd->pub.dump_file_manage) {
		MFREE(dhd->pub.osh, dhd->pub.dump_file_manage,
				sizeof(dhd_dump_file_manage_t));
	}
#endif /* DHD_DUMP_MNGR */

	dhd_sysfs_exit(dhd);
	dhd->pub.fw_download_status = FW_UNLOADED;

#if defined(BT_OVER_SDIO)
	mutex_destroy(&dhd->bus_user_lock);
#endif /* BT_OVER_SDIO */

#ifdef BCMINTERNAL
#ifdef DHD_FWTRACE
	(void) dhd_fwtrace_detach(dhdp);
#endif /* DHD_FWTRACE */
#endif /* BCMINTERNAL */

#ifdef DHD_TX_PROFILE
	(void)dhd_tx_profile_detach(dhdp);
#endif /* defined(DHD_TX_PROFILE) */
	dhd_conf_detach(dhdp);

} /* dhd_detach */

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
			}
		}

		dhd_sta_pool_fini(dhdp, DHD_MAX_STA);

		dhd = (dhd_info_t *)dhdp->info;
		if (dhdp->soc_ram) {
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
			DHD_OS_PREFREE(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
#else
			if (is_vmalloc_addr(dhdp->soc_ram)) {
				VMFREE(dhdp->osh, dhdp->soc_ram, dhdp->soc_ram_length);
			}
			else {
				MFREE(dhdp->osh, dhdp->soc_ram, dhdp->soc_ram_length);
			}
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
			dhdp->soc_ram = NULL;
		}
#ifdef CACHE_FW_IMAGES
		if (dhdp->cached_fw) {
			MFREE(dhdp->osh, dhdp->cached_fw, dhdp->bus->ramsize);
		}

		if (dhdp->cached_nvram) {
			MFREE(dhdp->osh, dhdp->cached_nvram, MAX_NVRAMBUF_SIZE);
		}
#endif
		if (dhd != NULL) {
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
			}
		}

		dhd_sta_pool_clear(dhdp, DHD_MAX_STA);

		if (dhdp->soc_ram) {
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
			DHD_OS_PREFREE(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
#else
			if (is_vmalloc_addr(dhdp->soc_ram)) {
				VMFREE(dhdp->osh, dhdp->soc_ram, dhdp->soc_ram_length);
			}
			else {
				MFREE(dhdp->osh, dhdp->soc_ram, dhdp->soc_ram_length);
			}
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

#if defined(OEM_ANDROID)
	wl_android_exit();
#endif /* OEM_ANDROID */

	dhd_wifi_platform_unregister_drv();
	printf("%s: Exit\n", __FUNCTION__);
}

static void
dhd_module_exit(void)
{
	atomic_set(&exit_in_progress, 1);
#ifdef DHD_BUZZZ_LOG_ENABLED
	dhd_buzzz_detach();
#endif /* DHD_BUZZZ_LOG_ENABLED */
	dhd_module_cleanup();
	unregister_reboot_notifier(&dhd_reboot_notifier);
	dhd_destroy_to_notifier_skt();
#ifdef DHD_PKTTS
	dhd_destroy_to_notifier_ts();
#endif /* DHD_PKTTS */
}

static int
_dhd_module_init(void)
{
	int err;
	int retry = POWERUP_MAX_RETRY;

	printk(KERN_ERR PERCENT_S DHD_LOG_PREFIXS "%s: in %s\n",
		PRINTF_SYSTEM_TIME, __FUNCTION__, dhd_version);
	if (ANDROID_VERSION > 0)
		printf("ANDROID_VERSION = %d\n", ANDROID_VERSION);

#ifdef DHD_BUZZZ_LOG_ENABLED
	dhd_buzzz_attach();
#endif /* DHD_BUZZZ_LOG_ENABLED */

#if defined(BCM_ROUTER_DHD)
	{	/* XXX Should we maintain nvram budget/thresholds per 5G|2G radio? */
		char * var;
		if ((var = getvar(NULL, "dhd_queue_budget")) != NULL) {
			dhd_queue_budget = bcm_strtoul(var, NULL, 0);
		}
		DHD_ERROR(("dhd_queue_budget = %d\n", dhd_queue_budget));

		if ((var = getvar(NULL, "dhd_sta_threshold")) != NULL) {
			dhd_sta_threshold = bcm_strtoul(var, NULL, 0);
		}
		DHD_ERROR(("dhd_sta_threshold = %d\n", dhd_sta_threshold));

		if ((var = getvar(NULL, "dhd_if_threshold")) != NULL) {
			dhd_if_threshold = bcm_strtoul(var, NULL, 0);
		}
		DHD_ERROR(("dhd_if_threshold = %d\n", dhd_if_threshold));
	}
#endif /* BCM_ROUTER_DHD */

	if (firmware_path[0] != '\0') {
		strlcpy(fw_bak_path, firmware_path, sizeof(fw_bak_path));
	}

	if (nvram_path[0] != '\0') {
		strlcpy(nv_bak_path, nvram_path, sizeof(nv_bak_path));
	}

	do {
		err = dhd_wifi_platform_register_drv();
		if (!err) {
			register_reboot_notifier(&dhd_reboot_notifier);
			break;
		} else {
			DHD_ERROR(("%s: Failed to load the driver, try cnt %d\n",
				__FUNCTION__, retry));
			strlcpy(firmware_path, fw_bak_path, sizeof(firmware_path));
			strlcpy(nvram_path, nv_bak_path, sizeof(nvram_path));
		}
	} while (retry--);

	dhd_create_to_notifier_skt();

#ifdef DHD_PKTTS
	dhd_create_to_notifier_ts();
#endif /* DHD_PKTTS */

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
dhd_module_init(void)
{
	int err;

	err = _dhd_module_init();
#ifdef DHD_SUPPORT_HDM
	if (err && !dhd_download_fw_on_driverload) {
		dhd_hdm_wlan_sysfs_init();
	}
#endif /* DHD_SUPPORT_HDM */
	return err;

}

#ifdef DHD_SUPPORT_HDM
bool hdm_trigger_init = FALSE;
struct delayed_work hdm_sysfs_wq;

int
dhd_module_init_hdm(void)
{
	int err = 0;

	hdm_trigger_init = TRUE;

	if (dhd_driver_init_done) {
		DHD_INFO(("%s : Module is already inited\n", __FUNCTION__));
		return err;
	}

	err = _dhd_module_init();

	/* remove sysfs file after module load properly */
	if (!err && !dhd_download_fw_on_driverload) {
		INIT_DELAYED_WORK(&hdm_sysfs_wq, dhd_hdm_wlan_sysfs_deinit);
		schedule_delayed_work(&hdm_sysfs_wq, msecs_to_jiffies(SYSFS_DEINIT_MS));
	}

	hdm_trigger_init = FALSE;
	return err;
}
#endif /* DHD_SUPPORT_HDM */

static int
dhd_reboot_callback(struct notifier_block *this, unsigned long code, void *unused)
{
	DHD_TRACE(("%s: code = %ld\n", __FUNCTION__, code));
	if (code == SYS_RESTART) {
#ifdef OEM_ANDROID
#ifdef BCMPCIE
		is_reboot = code;
#endif /* BCMPCIE */
#else
		dhd_module_cleanup();
#endif /* OEM_ANDROID */
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
#if defined(CONFIG_DEFERRED_INITCALLS) && !defined(EXYNOS_PCIE_MODULE_PATCH)
/* XXX To decrease the device boot time, deferred_module_init() macro can be
 * used. The detailed principle and implemenation of deferred_module_init()
 * is found at http://elinux.org/Deferred_Initcalls
 * To enable this feature for module build, it needs to add another
 * deferred_module_init() definition to include/linux/init.h in Linux Kernel.
 * #define deferred_module_init(fn)	module_init(fn)
 */
#if defined(CONFIG_ARCH_MSM) || defined(CONFIG_ARCH_EXYNOS)
deferred_module_init_sync(dhd_module_init);
#else
deferred_module_init(dhd_module_init);
#endif /* CONFIG_ARCH_MSM || CONFIG_ARCH_EXYNOS */
#elif defined(USE_LATE_INITCALL_SYNC)
late_initcall_sync(dhd_module_init);
#else
late_initcall(dhd_module_init);
#endif /* USE_LATE_INITCALL_SYNC */

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
		down(&dhd->proto_sem);

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

void
dhd_os_logdump_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = NULL;

	if (!pub)
		return;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		mutex_lock(&dhd->logdump_lock);
	}
}

void
dhd_os_logdump_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = NULL;

	if (!pub)
		return;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		mutex_unlock(&dhd->logdump_lock);
	}
}

unsigned long
dhd_os_dbgring_lock(void *lock)
{
	if (!lock)
		return 0;

	mutex_lock((struct mutex *)lock);

	return 0;
}

void
dhd_os_dbgring_unlock(void *lock, unsigned long flags)
{
	BCM_REFERENCE(flags);

	if (!lock)
		return;

	mutex_unlock((struct mutex *)lock);
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
	timeout = msecs_to_jiffies(dhd_ioctl_timeout_msec);

#ifdef BCMQT_HW
	DHD_ERROR(("%s, Timeout wait until %d mins (%d ms) in QT mode\n",
		__FUNCTION__, (dhd_ioctl_timeout_msec / (60 * 1000)), dhd_ioctl_timeout_msec));
#endif /* BCMQT_HW */

	timeout = wait_event_timeout(dhd->ioctl_resp_wait, (*condition), timeout);

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
	timeout = msecs_to_jiffies(D3_ACK_RESP_TIMEOUT);
#ifdef BCMSLTGT
	timeout *= htclkratio;
#endif /* BCMSLTGT */

	timeout = wait_event_timeout(dhd->d3ack_wait, (*condition), timeout);

	return timeout;
}

#ifdef PCIE_INB_DW
int
dhd_os_ds_exit_wait(dhd_pub_t *pub, uint *condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Convert timeout in millsecond to jiffies */
	timeout = msecs_to_jiffies(ds_exit_timeout_msec);
#ifdef BCMSLTGT
	timeout *= htclkratio;
#endif /* BCMSLTGT */

	timeout = wait_event_timeout(dhd->ds_exit_wait, (*condition), timeout);

	return timeout;
}

int
dhd_os_ds_exit_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up_all(&dhd->ds_exit_wait);
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
	timeout = msecs_to_jiffies(DHD_BUS_BUSY_TIMEOUT);

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
	timeout = msecs_to_jiffies(DHD_BUS_BUSY_TIMEOUT);

	timeout = wait_event_timeout(dhd->dhd_bus_busy_state_wait, (*var == condition), timeout);

	return timeout;
}

/*
 * Wait until the '(*var & bitmask) == condition' is met.
 * Returns 0 if the @condition evaluated to false after the timeout elapsed
 * Returns 1 if the @condition evaluated to true
 */
int
dhd_os_busbusy_wait_bitmask(dhd_pub_t *pub, uint *var,
		uint bitmask, uint condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Convert timeout in millsecond to jiffies */
	timeout = msecs_to_jiffies(DHD_BUS_BUSY_TIMEOUT);

	timeout = wait_event_timeout(dhd->dhd_bus_busy_state_wait,
			((*var & bitmask) == condition), timeout);

	return timeout;
}

int
dhd_os_dmaxfer_wait(dhd_pub_t *pub, uint *condition)
{
	int ret = 0;
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	timeout = msecs_to_jiffies(IOCTL_DMAXFER_TIMEOUT);

	ret = wait_event_timeout(dhd->dmaxfer_wait, (*condition), timeout);

	return ret;

}

int
dhd_os_dmaxfer_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up(&dhd->dmaxfer_wait);
	return 0;
}

void
dhd_os_tx_completion_wake(dhd_pub_t *dhd)
{
	/* Call wmb() to make sure before waking up the other event value gets updated */
	OSL_SMP_WMB();
	wake_up(&dhd->tx_completion_wait);
}

/* Fix compilation error for FC11 */
INLINE int
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
#ifdef BCMSDIO
		if (!wdtick) {
			DHD_OS_WD_WAKE_UNLOCK(pub);
		}
#endif /* BCMSDIO */
		return;
	}

	/* Totally stop the timer */
	if (!wdtick && dhd->wd_timer_valid == TRUE) {
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(pub, flags);
		del_timer_sync(&dhd->timer);
#ifdef BCMSDIO
		DHD_OS_WD_WAKE_UNLOCK(pub);
#endif /* BCMSDIO */
		return;
	}

	 if (wdtick) {
#ifdef BCMSDIO
		DHD_OS_WD_WAKE_LOCK(pub);
		dhd_watchdog_ms = (uint)wdtick;
#endif /* BCMSDIO */
		/* Re arm the timer, at last watchdog period */
		mod_timer(&dhd->timer, jiffies + msecs_to_jiffies(dhd_watchdog_ms));
		dhd->wd_timer_valid = TRUE;
	}
	DHD_GENERAL_UNLOCK(pub, flags);
#endif /* BCMDBUS */
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
			DHD_ERROR(("DHD Runtime PM Timer ON\n"));
		}
	} else {
		/* tick is zero, we have to stop the timer */
		/* Stop the timer only if its running, otherwise we don't have to do anything */
		if (dhd->rpm_timer_valid == TRUE) {
			dhd->rpm_timer_valid = FALSE;
			DHD_GENERAL_UNLOCK(pub, flags);
			del_timer_sync(&dhd->rpm_timer);
			DHD_ERROR(("DHD Runtime PM Timer OFF \n"));
			/* we have already released the lock, so just go to exit */
			goto exit;
		}
	}

	DHD_GENERAL_UNLOCK(pub, flags);
exit:
	return;

}

#endif /* DHD_PCIE_RUNTIMEPM */

#ifdef DHD_LINUX_STD_FW_API
int
dhd_os_get_img_fwreq(const struct firmware **fw, char *file_path)
{
	int ret = BCME_ERROR;

	ret = request_firmware(fw, file_path, dhd_bus_to_dev(g_dhd_pub->bus));
	if (ret < 0) {
		DHD_ERROR(("%s: request_firmware %s err: %d\n", __FUNCTION__, file_path, ret));
		/* convert to BCME_NOTFOUND error for error handling */
		ret = BCME_NOTFOUND;
	} else
		 DHD_ERROR(("%s: %s (%zu bytes) open success\n", __FUNCTION__, file_path, (*fw)->size));

	return ret;
}

void
dhd_os_close_img_fwreq(const struct firmware *fw)
{
	release_firmware(fw);
}
#endif /* DHD_LINUX_STD_FW_API */

void *
dhd_os_open_image1(dhd_pub_t *pub, char *filename)
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
	rdlen = kernel_read_compat(fp, fp->f_pos, buf, MIN(len, size));

	if (len >= size && size != rdlen) {
		return -EIO;
	}

	if (rdlen > 0) {
		fp->f_pos += rdlen;
	}

	return rdlen;
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

	rd_len = kernel_read_compat(fp, fp->f_pos, str, len);
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

void
dhd_os_close_image1(dhd_pub_t *pub, void *image)
{
	if (image) {
		filp_close((struct file *)image, NULL);
	}
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

unsigned long
dhd_os_sdlock_txoff(dhd_pub_t *pub)
{
	dhd_info_t *dhd;
	unsigned long flags = 0;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_irqsave(&dhd->txoff_lock, flags);

	return flags;
}

void
dhd_os_sdunlock_txoff(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_irqrestore(&dhd->txoff_lock, flags);
}

void
dhd_os_sdlock_rxq(dhd_pub_t *pub)
{
}

void
dhd_os_sdunlock_rxq(dhd_pub_t *pub)
{
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
		flags = osl_spin_lock(&dhd->tcpack_lock);
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
		osl_spin_unlock(&dhd->tcpack_lock, flags);
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

	if (!(dev->flags & IFF_UP)) {
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
	if (unlikely(bcmerror != BCME_OK)) {
		return bcmerror;
	}

	if (ntoh32(event->event_type) == WLC_E_IF) {
		/* WLC_E_IF event types are consumed by wl_process_host_event.
		 * For ifadd/del ops, the netdev ptr may not be valid at this
		 * point. so return before invoking cfg80211/wext handlers.
		 */
		return BCME_OK;
	}

#ifdef WL_EVENT
	wl_ext_event_send(dhd->pub.event_params, event, *data);
#endif

#ifdef WL_CFG80211
	if (dhd->iflist[ifidx]->net) {
		DHD_UP_LOCK(&dhd->pub.up_lock, flags);
		if (dhd->pub.up) {
			wl_cfg80211_event(dhd->iflist[ifidx]->net, event, *data);
		}
		DHD_UP_UNLOCK(&dhd->pub.up_lock, flags);
	}
#endif /* defined(WL_CFG80211) */

	return (bcmerror);
}

/* send up locally generated event */
void
dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data)
{
	switch (ntoh32(event->event_type)) {
	/* Handle error case or further events here */
	default:
		break;
	}
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
#if defined(BCMSDIO)
	struct dhd_info *dhdinfo =  dhd->info;

	int timeout = msecs_to_jiffies(IOCTL_RESP_TIMEOUT);

	dhd_os_sdunlock(dhd);
	wait_event_timeout(dhdinfo->ctrl_wait, (*lockvar == FALSE), timeout);
	dhd_os_sdlock(dhd);
#endif /* defined(BCMSDIO) */
	return;
} /* dhd_init_static_strs_array */

void dhd_wait_event_wakeup(dhd_pub_t *dhd)
{
#if defined(BCMSDIO)
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

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	if (pm_runtime_get_sync(dhd_bus_to_dev(dhd->pub.bus)) < 0)
		return BCME_ERROR;
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	if (flag == TRUE) {
#ifndef WL_CFG80211
		/* Issue wl down command for non-cfg before resetting the chip */
		if (dhd_wl_ioctl_cmd(&dhd->pub, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_TRACE(("%s: wl down failed\n", __FUNCTION__));
		}
#endif /* !WL_CFG80211 */
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

		DHD_SSSR_DUMP_DEINIT(&dhd->pub);
#ifdef DHD_SDTC_ETB_DUMP
		if (dhd->pub.sdtc_etb_inited) {
			dhd_sdtc_etb_deinit(&dhd->pub);
		}
#endif /* DHD_SDTC_ETB_DUMP */
/*
 * XXX Detach only if the module is not attached by default at dhd_attach.
 * If attached by default, we need to keep it till dhd_detach, so that
 * module is not detached at wifi on/off
 */
#if defined(DBG_PKT_MON) && !defined(DBG_PKT_MON_INIT_DEFAULT)
		dhd_os_dbg_detach_pkt_monitor(&dhd->pub);
#endif /* DBG_PKT_MON */
	}

#ifdef BCMSDIO
	/* XXX Some DHD modules (e.g. cfg80211) configures operation mode based on firmware name.
	 * This is indeed a hack but we have to make it work properly before we have a better
	 * solution
	 */
	if (!flag) {
		dhd_update_fw_nv_path(dhd);
		/* update firmware and nvram path to sdio bus */
		dhd_bus_update_fw_nv_path(dhd->pub.bus,
			dhd->fw_path, dhd->nv_path, dhd->clm_path, dhd->conf_path);
	}
#endif /* BCMSDIO */
#if defined(CONFIG_ARCH_EXYNOS) && defined(BCMPCIE)
#if !defined(CONFIG_SOC_EXYNOS8890) && !defined(SUPPORT_EXYNOS7420)
	/* XXX: JIRA SWWLAN-139454: Added L1ss enable
	 * after firmware download completion due to link down issue
	 * JIRA SWWLAN-142236: Amendment - Changed L1ss enable point
	 */
	DHD_ERROR(("%s Disable L1ss EP side\n", __FUNCTION__));
	if (flag == FALSE && dhd->pub.busstate == DHD_BUS_DOWN) {
#if defined(CONFIG_SOC_GS101)
		exynos_pcie_rc_l1ss_ctrl(0, PCIE_L1SS_CTRL_WIFI, 1);
#else
		exynos_pcie_l1ss_ctrl(0, PCIE_L1SS_CTRL_WIFI);
#endif /* CONFIG_SOC_GS101  */
	}
#endif /* !CONFIG_SOC_EXYNOS8890 && !defined(SUPPORT_EXYNOS7420)  */
#endif /* CONFIG_ARCH_EXYNOS && BCMPCIE */

	ret = dhd_bus_devreset(&dhd->pub, flag);

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	pm_runtime_mark_last_busy(dhd_bus_to_dev(dhd->pub.bus));
	pm_runtime_put_autosuspend(dhd_bus_to_dev(dhd->pub.bus));
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	if (flag) {
		/* Clear some flags for recovery logic */
		dhd->pub.dongle_trap_occured = 0;
#ifdef BT_OVER_PCIE
		dhd->pub.dongle_trap_due_to_bt = 0;
#endif /* BT_OVER_PCIE */
		dhd->pub.iovar_timeout_occured = 0;
#ifdef PCIE_FULL_DONGLE
		dhd->pub.d3ack_timeout_occured = 0;
		dhd->pub.livelock_occured = 0;
		dhd->pub.pktid_audit_failed = 0;
#endif /* PCIE_FULL_DONGLE */
		dhd->pub.smmu_fault_occurred = 0;
		dhd->pub.iface_op_failed = 0;
		dhd->pub.scan_timeout_occurred = 0;
		dhd->pub.scan_busy_occurred = 0;
	}

	if (ret) {
		DHD_ERROR(("%s: dhd_bus_devreset: %d\n", __FUNCTION__, ret));
	}

	return ret;
}

#if defined(BCMSDIO) || defined(BCMPCIE)
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

#endif /* BCMSDIO || BCMPCIE */
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

	if (dhd && (dhd->pub.conf->suspend_mode == EARLY_SUSPEND ||
			dhd->pub.conf->suspend_mode == SUSPEND_MODE_2)) {
		if (dhd->pub.conf->suspend_mode == EARLY_SUSPEND && !val)
			dhd_conf_set_suspend_resume(&dhd->pub, val);
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
		ret = dhd_set_suspend(val, &dhd->pub);
#else
		ret = dhd_suspend_resume_helper(dhd, val, force);
#endif
#ifdef WL_CFG80211
		wl_cfg80211_update_power_mode(dev);
#endif
		if (dhd->pub.conf->suspend_mode == EARLY_SUSPEND && val)
			dhd_conf_set_suspend_resume(&dhd->pub, val);
	}
	return ret;
}

int net_os_set_suspend_bcn_li_dtim(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd) {
		DHD_ERROR(("%s: Set bcn_li_dtim in suspend %d\n",
			__FUNCTION__, val));
		dhd->pub.suspend_bcn_li_dtim = val;
	}

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

#ifdef DISABLE_DTIM_IN_SUSPEND
int net_os_set_disable_dtim_in_suspend(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd) {
		DHD_ERROR(("%s: Disable bcn_li_dtim in suspend %s\n",
			__FUNCTION__, (val ? "Enable" : "Disable")));
		if (val) {
			dhd->pub.disable_dtim_in_suspend = TRUE;
		} else {
			dhd->pub.disable_dtim_in_suspend = FALSE;
		}
	} else {
		return BCME_ERROR;
	}

	return BCME_OK;
}
#endif /* DISABLE_DTIM_IN_SUSPEND */

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

#ifdef BLOCK_IPV6_PACKET
	/* customer want to use NO IPV6 packets only */
	if (num == DHD_MULTICAST6_FILTER_NUM) {
		return 0;
	}
#endif /* BLOCK_IPV6_PACKET */

	if (num >= dhd->pub.pktfilter_count) {
		return -EINVAL;
	}

	ret = dhd_packet_filter_add_remove(&dhd->pub, add_remove, num);
#endif /* !GAN_LITE_NAT_KEEPALIVE_FILTER */

	return ret;
}

/* XXX RB:4238 Change net_os_set_packet_filter() function name to net_os_enable_packet_filter()
 * previous code do 'set' & 'enable' in one fucntion.
 * but from now on, we are going to separate 'set' and 'enable' feature.
 *  - set : net_os_rxfilter_add_remove() -> dhd_set_packet_filter() -> dhd_pktfilter_offload_set()
 *  - enable : net_os_enable_packet_filter() -> dhd_enable_packet_filter()
 *                                                              -> dhd_pktfilter_offload_enable()
 */
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

	/* tdls capability or othters can be missed because of initialization */
	if (dhd_get_fw_capabilities(dhd) < 0) {
		DHD_ERROR(("Capabilities rechecking fail\n"));
	}

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

#if defined(PNO_SUPPORT) && !defined(DISABLE_ANDROID_PNO)
	if (dhd_is_pno_supported(dhd)) {
		feature_set |= WIFI_FEATURE_PNO;
#ifdef BATCH_SCAN
		/* Deprecated */
		feature_set |= WIFI_FEATURE_BATCH_SCAN;
#endif /* BATCH_SCAN */
#ifdef GSCAN_SUPPORT
		/* terence 20171115: remove to get GTS PASS
		 * com.google.android.gts.wifi.WifiHostTest#testWifiScannerBatchTimestamp
		 */
//		feature_set |= WIFI_FEATURE_GSCAN;
//		feature_set |= WIFI_FEATURE_HAL_EPNO;
#endif /* GSCAN_SUPPORT */
	}
#endif /* PNO_SUPPORT && !DISABLE_ANDROID_PNO */
#ifdef RSSI_MONITOR_SUPPORT
	if (FW_SUPPORTED(dhd, rssi_mon)) {
		feature_set |= WIFI_FEATURE_RSSI_MONITOR;
	}
#endif /* RSSI_MONITOR_SUPPORT */
#ifdef WL11U
	feature_set |= WIFI_FEATURE_HOTSPOT;
#endif /* WL11U */
#ifdef KEEP_ALIVE
	feature_set |= WIFI_FEATURE_MKEEP_ALIVE;
#endif /* KEEP_ALIVE */
#ifdef NDO_CONFIG_SUPPORT
	feature_set |= WIFI_FEATURE_CONFIG_NDO;
#endif /* NDO_CONFIG_SUPPORT */
#ifdef SUPPORT_RANDOM_MAC_SCAN
	feature_set |= WIFI_FEATURE_SCAN_RAND;
#endif /* SUPPORT_RANDOM_MAC_SCAN */
#ifdef FILTER_IE
	if (FW_SUPPORTED(dhd, fie)) {
		feature_set |= WIFI_FEATURE_FILTER_IE;
	}
#endif /* FILTER_IE */
#ifdef ROAMEXP_SUPPORT
	 feature_set |= WIFI_FEATURE_CONTROL_ROAMING;
#endif /* ROAMEXP_SUPPORT */
#ifdef WL_LATENCY_MODE
	feature_set |= WIFI_FEATURE_SET_LATENCY_MODE;
#endif /* WL_LATENCY_MODE */
#ifdef WL_P2P_RAND
	feature_set |= WIFI_FEATURE_P2P_RAND_MAC;
#endif /* WL_P2P_RAND */
#ifdef WL_SAR_TX_POWER
	feature_set |= WIFI_FEATURE_SET_TX_POWER_LIMIT;
	feature_set |= WIFI_FEATURE_USE_BODY_HEAD_SAR;
#endif /* WL_SAR_TX_POWER */
#ifdef WL_STATIC_IF
	feature_set |= WIFI_FEATURE_AP_STA;
#endif /* WL_STATIC_IF */
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
		ret = dhd_ndo_enable(dhdp, FALSE);
		if (ret < 0) {
			DHD_ERROR(("%s: failed to disable NDO: %d\n", __FUNCTION__, ret));
		}
	}
	return ret;
}

static int
dhd_dev_ndo_get_valid_inet6addr_count(struct inet6_dev *inet6)
{
	struct inet6_ifaddr *ifa;
	struct ifacaddr6 *acaddr = NULL;
	int addr_count = 0;

	/* lock */
	read_lock_bh(&inet6->lock);

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	/* Count valid unicast address */
	list_for_each_entry(ifa, &inet6->addr_list, if_list) {
		GCC_DIAGNOSTIC_POP();
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
			ret = dhd_ndo_enable(dhdp, FALSE);
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
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry(ifa, &inet6->addr_list, if_list) {
		GCC_DIAGNOSTIC_POP();
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
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		list_for_each_entry(ifa, &inet6->addr_list, if_list) {
			GCC_DIAGNOSTIC_POP();
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
			ret = dhd_ndo_enable(dhdp, TRUE);
		}
	}

done:
	if (ipv6_addr) {
		MFREE(dhdp->osh, ipv6_addr, sizeof(struct in6_addr) * dhdp->ndo_max_host_ip);
	}

	return ret;
}

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

#if defined(OEM_ANDROID) && defined(PNO_SUPPORT)
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

/* Linux wrapper to call common dhd_pno_get_gscan */
void *
dhd_dev_pno_get_gscan(struct net_device *dev, dhd_pno_gscan_cmd_cfg_t type,
                      void *info, uint32 *len)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_get_gscan(&dhd->pub, type, info, len));
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
      const void  *data, int *send_evt_bytes, hotlist_type_t type, u32 *buf_len)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_handle_hotlist_scan_evt(&dhd->pub, data, send_evt_bytes, type, buf_len));
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

	DHD_INFO(("a_band_boost_thr %d a_band_penalty_thr %d\n",
	      roam_param->a_band_boost_threshold, roam_param->a_band_penalty_threshold));
	DHD_INFO(("a_band_boost_factor %d a_band_penalty_factor %d cur_bssid_boost %d\n",
	      roam_param->a_band_boost_factor, roam_param->a_band_penalty_factor,
	      roam_param->cur_bssid_boost));
	DHD_INFO(("alert_roam_trigger_thr %d a_band_max_boost %d\n",
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
	uint len;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	bssid_pref->version = BSSID_PREF_LIST_VERSION;
	/* By default programming bssid pref flushes out old values */
	bssid_pref->flags = (flush && !bssid_pref->count) ? ROAM_EXP_CLEAR_BSSID_PREF: 0;
	len = sizeof(wl_bssid_pref_cfg_t);
	if (bssid_pref->count) {
		len += (bssid_pref->count - 1) * sizeof(wl_bssid_pref_list_t);
	}
	err = dhd_iovar(&dhd->pub, 0, "roam_exp_bssid_pref",
			(char *)bssid_pref, len, NULL, 0, TRUE);
	if (err != BCME_OK) {
		DHD_ERROR(("%s : Failed to execute roam_exp_bssid_pref %d\n", __FUNCTION__, err));
	}
	return err;
}
#endif /* GSCAN_SUPPORT */

#if defined(GSCAN_SUPPORT) || defined(ROAMEXP_SUPPORT)
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
	err = dhd_iovar(&dhd->pub, 0, "roam_exp_ssid_whitelist", (char *)ssid_whitelist, len, NULL,
			0, TRUE);
	if (err != BCME_OK) {
		if (err == BCME_UNSUPPORTED) {
			DHD_ERROR(("%s : roam_exp_bssid_pref, UNSUPPORTED \n", __FUNCTION__));
		} else {
			DHD_ERROR(("%s : Failed to execute roam_exp_bssid_pref %d\n",
				__FUNCTION__, err));
		}
	}
	return err;
}
#endif /* GSCAN_SUPPORT || ROAMEXP_SUPPORT */
#endif /* defined(OEM_ANDROID) && defined(PNO_SUPPORT) */

#ifdef RSSI_MONITOR_SUPPORT
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
	err = dhd_iovar(&dhd->pub, 0, "rssi_monitor", (char *)&rssi_monitor, sizeof(rssi_monitor),
			NULL, 0, TRUE);
	if (err < 0 && err != BCME_UNSUPPORTED) {
		DHD_ERROR(("%s : Failed to execute rssi_monitor %d\n", __FUNCTION__, err));
	}
	return err;
}
#endif /* RSSI_MONITOR_SUPPORT */

#ifdef DHDTCPACK_SUPPRESS
int
dhd_dev_set_tcpack_sup_mode_cfg(struct net_device *dev, uint8 enable)
{
	int err;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	err = dhd_tcpack_suppress_set(&dhd->pub, enable);
	if (err != BCME_OK) {
		DHD_ERROR(("%s : Failed to set tcpack_suppress mode: %d\n", __FUNCTION__, err));
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
		DHD_ERROR(("Random MAC OUI to be used - "MACOUIDBG"\n",
			MACOUI2STRDBG(rand_mac_oui)));
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

	DHD_ERROR(("Setting rand mac oui to FW - "MACOUIDBG"\n",
		MACOUI2STRDBG(rand_mac_oui)));

	err = dhd_iovar(dhd, 0, "pfn_macaddr", (char *)&wl_cfg, sizeof(wl_cfg), NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_macaddr %d\n", __FUNCTION__, err));
	}
	return err;
}

#if defined(RTT_SUPPORT) && defined(WL_CFG80211)
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

#endif /* RTT_SUPPORT */
#if defined(PKT_FILTER_SUPPORT) && defined(APF)
static void _dhd_apf_lock_local(dhd_info_t *dhd)
{
	if (dhd) {
		mutex_lock(&dhd->dhd_apf_mutex);
	}
}

static void _dhd_apf_unlock_local(dhd_info_t *dhd)
{
	if (dhd) {
		mutex_unlock(&dhd->dhd_apf_mutex);
	}
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

	buf = MALLOCZ(dhdp->osh, buf_len);
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
		MFREE(dhdp->osh, buf, buf_len);
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
	char cmd[] = "pkt_filter_enable";

	ifidx = dhd_net2idx(dhd, ndev);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	cmd_len = sizeof(cmd);
	buf_len = cmd_len + sizeof(*pkt_filterp);

	buf = MALLOCZ(dhdp->osh, buf_len);
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
		MFREE(dhdp->osh, buf, buf_len);
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
	bool nan_dp_active = false;

	DHD_APF_LOCK(ndev);
#ifdef WL_NAN
	nan_dp_active = wl_cfgnan_is_dp_active(ndev);
#endif /* WL_NAN */
	if (dhdp->apf_set && (!(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE) &&
		!nan_dp_active)) {
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

#if defined(OEM_ANDROID)
static void dhd_hang_process(struct work_struct *work_data)
{
	struct net_device *dev;
#ifdef IFACE_HANG_FORCE_DEV_CLOSE
	struct net_device *ndev;
	uint8 i = 0;
#endif /* IFACE_HANG_FORCE_DEV_CLOSE */
	struct dhd_info *dhd;
	/* Ignore compiler warnings due to -Werror=cast-qual */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	dhd = container_of(work_data, dhd_info_t, dhd_hang_process_work);
	GCC_DIAGNOSTIC_POP();

	if (!dhd || !dhd->iflist[0])
		return;
	dev = dhd->iflist[0]->net;

	if (dev) {
#if defined(WL_WIRELESS_EXT)
		wl_iw_send_priv_event(dev, "HANG");
#endif
#if defined(WL_CFG80211)
		wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif
	}
#ifdef IFACE_HANG_FORCE_DEV_CLOSE
	/*
	 * For HW2, dev_close need to be done to recover
	 * from upper layer after hang. For Interposer skip
	 * dev_close so that dhd iovars can be used to take
	 * socramdump after crash, also skip for HW4 as
	 * handling of hang event is different
	 */

	rtnl_lock();
	for (i = 0; i < DHD_MAX_IFS; i++) {
		ndev = dhd->iflist[i] ? dhd->iflist[i]->net : NULL;
		if (ndev && (ndev->flags & IFF_UP)) {
			DHD_ERROR(("ndev->name : %s dev close\n",
					ndev->name));
#ifdef ENABLE_INSMOD_NO_FW_LOAD
			dhd_download_fw_on_driverload = FALSE;
#endif
			dev_close(ndev);
		}
	}
	rtnl_unlock();
#endif /* IFACE_HANG_FORCE_DEV_CLOSE */
}

#if defined(CONFIG_ARCH_EXYNOS) && defined(BCMPCIE)
extern dhd_pub_t *link_recovery;
void dhd_host_recover_link(void)
{
	DHD_ERROR(("****** %s ******\n", __FUNCTION__));
	link_recovery->hang_reason = HANG_REASON_PCIE_LINK_DOWN_RC_DETECT;
	dhd_bus_set_linkdown(link_recovery, TRUE);
	dhd_os_send_hang_message(link_recovery);
}
EXPORT_SYMBOL(dhd_host_recover_link);
#endif /* CONFIG_ARCH_EXYNOS && BCMPCIE */

#ifdef DHD_DETECT_CONSECUTIVE_MFG_HANG
#define MAX_CONSECUTIVE_MFG_HANG_COUNT 2
#endif /* DHD_DETECT_CONSECUTIVE_MFG_HANG */
int dhd_os_send_hang_message(dhd_pub_t *dhdp)
{
	int ret = 0;
	dhd_info_t *dhd_info = NULL;
#ifdef WL_CFG80211
	struct net_device *primary_ndev;
	struct bcm_cfg80211 *cfg;
#endif /* WL_CFG80211 */

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is null\n", __FUNCTION__));
		return -EINVAL;
	}

	dhd_info = (dhd_info_t *)dhdp->info;
	BCM_REFERENCE(dhd_info);

#if defined(WLAN_ACCEL_BOOT)
	if (!dhd_info->wl_accel_force_reg_on) {
		DHD_ERROR(("%s: set force reg on\n", __FUNCTION__));
		dhd_info->wl_accel_force_reg_on = TRUE;
	}
#endif /* WLAN_ACCEL_BOOT */

	if (!dhdp->hang_report) {
		DHD_ERROR(("%s: hang_report is disabled\n", __FUNCTION__));
		return BCME_ERROR;
	}

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT)
	if (dhd_info->scheduled_memdump) {
		DHD_ERROR_RLMT(("[DUMP]:%s, memdump in progress. return\n", __FUNCTION__));
		dhdp->hang_was_pending = 1;
		return BCME_OK;
	}
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT */

#ifdef WL_CFG80211
	primary_ndev = dhd_linux_get_primary_netdev(dhdp);
	if (!primary_ndev) {
		DHD_ERROR(("%s: Cannot find primary netdev\n", __FUNCTION__));
		return -ENODEV;
	}
	cfg = wl_get_cfg(primary_ndev);
	if (!cfg) {
		DHD_ERROR(("%s: Cannot find cfg\n", __FUNCTION__));
		return -EINVAL;
	}

	/* Skip sending HANG event to framework if driver is not ready */
	if (!wl_get_drv_status(cfg, READY, primary_ndev)) {
		DHD_ERROR(("%s: device is not ready\n", __FUNCTION__));
		return -ENODEV;
	}
#endif /* WL_CFG80211 */

#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhdp->req_hang_type) {
		DHD_ERROR(("%s, Clear HANG test request 0x%x\n",
			__FUNCTION__, dhdp->req_hang_type));
		dhdp->req_hang_type = 0;
	}
#endif /* DHD_HANG_SEND_UP_TEST */

	if (!dhdp->hang_was_sent) {
#ifdef DHD_DETECT_CONSECUTIVE_MFG_HANG
		if (dhdp->op_mode & DHD_FLAG_MFG_MODE) {
			dhdp->hang_count++;
			if (dhdp->hang_count >= MAX_CONSECUTIVE_MFG_HANG_COUNT) {
				DHD_ERROR(("%s, Consecutive hang from Dongle :%u\n",
					__FUNCTION__, dhdp->hang_count));
				BUG_ON(1);
			}
		}
#endif /* DHD_DETECT_CONSECUTIVE_MFG_HANG */
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
		schedule_work(&dhdp->info->dhd_hang_process_work);
		DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d s=%d\n", __FUNCTION__,
			dhdp->rxcnt_timeout, dhdp->txcnt_timeout, dhdp->busstate));
		printf("%s\n", info_string);
		printf("MAC %pM\n", &dhdp->mac);
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
				ret = dhd_os_send_hang_message(&dhd->pub);
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
#endif /* OEM_ANDROID */

int dhd_net_wifi_platform_set_power(struct net_device *dev, bool on, unsigned long delay_msec)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return wifi_platform_set_power(dhd->adapter, on, delay_msec);
}

int dhd_wifi_platform_set_power(dhd_pub_t *pub, bool on)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long delay_msec = on ? WIFI_TURNON_DELAY : WIFI_TURNOFF_DELAY;
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
#if defined(DHD_BLOB_EXISTENCE_CHECK) && !defined(CUSTOM_COUNTRY_CODE)
	else {
		/* Replace the ccode to XZ if ccode is undefined country */
		if (strncmp(country_iso_code, "", WLC_CNTRY_BUF_SZ) == 0) {
			strlcpy(country_iso_code, "XZ", WLC_CNTRY_BUF_SZ);
			strlcpy(cspec->country_abbrev, country_iso_code, WLC_CNTRY_BUF_SZ);
			strlcpy(cspec->ccode, country_iso_code, WLC_CNTRY_BUF_SZ);
			DHD_ERROR(("%s: ccode change to %s\n", __FUNCTION__, country_iso_code));
		}
	}
#endif /* DHD_BLOB_EXISTENCE_CHECK && !CUSTOM_COUNTRY_CODE */

#ifdef KEEP_JP_REGREV
/* XXX Needed by customer's request */
	if (strncmp(country_iso_code, "JP", 3) == 0) {
#if defined(DHD_BLOB_EXISTENCE_CHECK)
		if (dhd->pub.is_blob) {
			if (strncmp(dhd->pub.vars_ccode, "J1", 3) == 0) {
				memcpy(cspec->ccode, dhd->pub.vars_ccode,
					sizeof(dhd->pub.vars_ccode));
			}
		} else
#endif /* DHD_BLOB_EXISTENCE_CHECK */
		{
			if (strncmp(dhd->pub.vars_ccode, "JP", 3) == 0) {
				cspec->rev = dhd->pub.vars_regrev;
			}
		}
	}
#endif /* KEEP_JP_REGREV */
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

	strlcpy(dhd->fw_path, fw, sizeof(dhd->fw_path));

#if defined(OEM_ANDROID) && defined(SOFTAP)
	if (strstr(fw, "apsta") != NULL) {
		DHD_INFO(("GOT APSTA FIRMWARE\n"));
		ap_fw_loaded = TRUE;
	} else {
		DHD_INFO(("GOT STA FIRMWARE\n"));
		ap_fw_loaded = FALSE;
	}
#endif /* defined(OEM_ANDROID) && defined(SOFTAP) */
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
#if defined(OEM_ANDROID)
	if (dhd)
		mutex_lock(&dhd->dhd_net_if_mutex);
#endif
}

static void dhd_net_if_unlock_local(dhd_info_t *dhd)
{
#if defined(OEM_ANDROID)
	if (dhd)
		mutex_unlock(&dhd->dhd_net_if_mutex);
#endif
}

static void dhd_suspend_lock(dhd_pub_t *pub)
{
#if defined(OEM_ANDROID)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_lock(&dhd->dhd_suspend_mutex);
#endif
}

static void dhd_suspend_unlock(dhd_pub_t *pub)
{
#if defined(OEM_ANDROID)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_unlock(&dhd->dhd_suspend_mutex);
#endif
}

unsigned long dhd_os_general_spin_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags = 0;

	if (dhd) {
		flags = osl_spin_lock(&dhd->dhd_lock);
	}

	return flags;
}

void dhd_os_general_spin_unlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		osl_spin_unlock(&dhd->dhd_lock, flags);
	}
}

void *
dhd_os_dbgring_lock_init(osl_t *osh)
{
	struct mutex *mtx = NULL;

	mtx = MALLOCZ(osh, sizeof(*mtx));
	if (mtx)
		mutex_init(mtx);

	return mtx;
}

void
dhd_os_dbgring_lock_deinit(osl_t *osh, void *mutex)
{
	struct mutex *mtx = mutex;

	if (mtx) {
		mutex_destroy(mtx);
		MFREE(osh, mtx, sizeof(struct mutex));
	}
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
			schedule_timeout(timeout);
			set_current_state(TASK_RUNNING);
			ntimes--;
		}
		pend = dhd_get_pend_8021x_cnt(dhd);
	}
	if (ntimes == 0)
	{
		atomic_set(&dhd->pend_8021x_cnt, 0);
		WL_MSG(dev->name, "TIMEOUT\n");
	}
	return pend;
}

#if defined(BCM_ROUTER_DHD) || defined(DHD_DEBUG)
int write_file(const char * file_name, uint32 flags, uint8 *buf, int size)
{
	int ret = 0;
	struct file *fp = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t old_fs;
#endif
	loff_t pos = 0;

	/* change to KERNEL_DS address limit */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	/* open file to write */
	fp = filp_open(file_name, flags, 0664);
	if (IS_ERR(fp)) {
		DHD_ERROR(("open file error, err = %ld\n", PTR_ERR(fp)));
		goto exit;
	}

	/* Write buf to file */
	ret = vfs_write(fp, buf, size, &pos);
	if (ret < 0) {
		DHD_ERROR(("write file error, err = %d\n", ret));
		goto exit;
	}

	/* Sync file from filesystem to physical media */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	ret = vfs_fsync(fp, 0);
#else
	ret = vfs_fsync(fp, fp->f_path.dentry, 0);
#endif
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(old_fs);
#endif

	return ret;
}
#endif /* BCM_ROUTER_DHD || DHD_DEBUG */

#ifdef DHD_DEBUG
static void
dhd_convert_memdump_type_to_str(uint32 type, char *buf, size_t buf_len, int substr_type)
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
		case DUMP_TYPE_SCAN_BUSY:
			type_str = "SCAN_Busy";
			break;
		case DUMP_TYPE_BY_SYSDUMP:
			if (substr_type == CMD_UNWANTED) {
				type_str = "BY_SYSDUMP_FORUSER_unwanted";
			} else if (substr_type == CMD_DISCONNECTED) {
				type_str = "BY_SYSDUMP_FORUSER_disconnected";
			} else {
				type_str = "BY_SYSDUMP_FORUSER";
			}
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
		case DUMP_TYPE_RESUMED_ON_TIMEOUT_RX:
			type_str = "ERROR_RX_TIMED_OUT";
			break;
		case DUMP_TYPE_RESUMED_ON_TIMEOUT_TX:
			type_str = "ERROR_TX_TIMED_OUT";
			break;
		case DUMP_TYPE_CFG_VENDOR_TRIGGERED:
			type_str = "CFG_VENDOR_TRIGGERED";
			break;
		case DUMP_TYPE_RESUMED_ON_INVALID_RING_RDWR:
			type_str = "BY_INVALID_RING_RDWR";
			break;
		case DUMP_TYPE_IFACE_OP_FAILURE:
			type_str = "BY_IFACE_OP_FAILURE";
			break;
		case DUMP_TYPE_TRANS_ID_MISMATCH:
			type_str = "BY_TRANS_ID_MISMATCH";
			break;
#ifdef DEBUG_DNGL_INIT_FAIL
		case DUMP_TYPE_DONGLE_INIT_FAILURE:
			type_str = "DONGLE_INIT_FAIL";
			break;
#endif /* DEBUG_DNGL_INIT_FAIL */
#ifdef SUPPORT_LINKDOWN_RECOVERY
		case DUMP_TYPE_READ_SHM_FAIL:
			type_str = "READ_SHM_FAIL";
			break;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
		case DUMP_TYPE_DONGLE_HOST_EVENT:
			type_str = "BY_DONGLE_HOST_EVENT";
			break;
		case DUMP_TYPE_SMMU_FAULT:
			type_str = "SMMU_FAULT";
			break;
#ifdef DHD_ERPOM
		case DUMP_TYPE_DUE_TO_BT:
			type_str = "DUE_TO_BT";
			break;
#endif /* DHD_ERPOM */
		case DUMP_TYPE_BY_USER:
			type_str = "BY_USER";
			break;
		case DUMP_TYPE_LOGSET_BEYOND_RANGE:
			type_str = "LOGSET_BEYOND_RANGE";
			break;
		case DUMP_TYPE_CTO_RECOVERY:
			type_str = "CTO_RECOVERY";
			break;
		case DUMP_TYPE_SEQUENTIAL_PRIVCMD_ERROR:
			type_str = "SEQUENTIAL_PRIVCMD_ERROR";
			break;
		case DUMP_TYPE_PROXD_TIMEOUT:
			type_str = "PROXD_TIMEOUT";
			break;
		case DUMP_TYPE_INBAND_DEVICE_WAKE_FAILURE:
			type_str = "INBAND_DEVICE_WAKE_FAILURE";
			break;
		case DUMP_TYPE_PKTID_POOL_DEPLETED:
			type_str = "PKTID_POOL_DEPLETED";
			break;
		case DUMP_TYPE_ESCAN_SYNCID_MISMATCH:
			type_str = "ESCAN_SYNCID_MISMATCH";
			break;
		case DUMP_TYPE_INVALID_SHINFO_NRFRAGS:
			type_str = "INVALID_SHINFO_NRFRAGS";
			break;
		default:
			type_str = "Unknown_type";
			break;
	}

	strlcpy(buf, type_str, buf_len);
}

void
dhd_get_memdump_filename(struct net_device *ndev, char *memdump_path, int len, char *fname)
{
	char memdump_type[DHD_MEMDUMP_TYPE_STR_LEN];
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(ndev);
	dhd_pub_t *dhdp = &dhd->pub;

	/* Init file name */
	memset(memdump_path, 0, len);
	memset(memdump_type, 0, DHD_MEMDUMP_TYPE_STR_LEN);
	dhd_convert_memdump_type_to_str(dhdp->memdump_type, memdump_type, DHD_MEMDUMP_TYPE_STR_LEN,
		dhdp->debug_dump_subcmd);
	clear_debug_dump_time(dhdp->debug_dump_time_str);
	get_debug_dump_time(dhdp->debug_dump_time_str);
	snprintf(memdump_path, len, "%s%s_%s_" "%s",
		DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhdp->debug_dump_time_str);

	if (strstr(fname, "sssr_dump")) {
		DHD_SSSR_PRINT_FILEPATH(dhdp, memdump_path);
	} else {
		DHD_ERROR(("%s: file_path = %s%s\n", __FUNCTION__,
			memdump_path, FILE_NAME_HAL_TAG));
	}
}

int
write_dump_to_file(dhd_pub_t *dhd, uint8 *buf, int size, char *fname)
{
	int ret = 0;
	char memdump_path[DHD_MEMDUMP_PATH_STR_LEN];
	char memdump_type[DHD_MEMDUMP_TYPE_STR_LEN];
	uint32 file_mode;

	/* Init file name */
	memset(memdump_path, 0, DHD_MEMDUMP_PATH_STR_LEN);
	memset(memdump_type, 0, DHD_MEMDUMP_TYPE_STR_LEN);
	dhd_convert_memdump_type_to_str(dhd->memdump_type, memdump_type, DHD_MEMDUMP_TYPE_STR_LEN,
			dhd->debug_dump_subcmd);
	clear_debug_dump_time(dhd->debug_dump_time_str);
	get_debug_dump_time(dhd->debug_dump_time_str);

	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
		DHD_COMMON_DUMP_PATH, fname, memdump_type, dhd->debug_dump_time_str);
#ifdef CUSTOMER_HW4_DEBUG
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
#elif defined(OEM_ANDROID) && defined(__ARM_ARCH_7A__)
	file_mode = O_CREAT | O_WRONLY;
#elif defined(OEM_ANDROID)
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
			snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
				"/data/", fname, memdump_type,  dhd->debug_dump_time_str);
		} else {
			filp_close(fp, NULL);
		}
	}
#else
	file_mode = O_CREAT | O_WRONLY;
#endif /* CUSTOMER_HW4_DEBUG */

	/* print SOCRAM dump file path */
	DHD_ERROR(("%s: file_path = %s\n", __FUNCTION__, memdump_path));

#ifdef DHD_LOG_DUMP
	dhd_print_buf_addr(dhd, "write_dump_to_file", buf, size);
#endif /* DHD_LOG_DUMP */

	/* Write file */
	ret = write_file(memdump_path, file_mode, buf, size);

#ifdef DHD_DUMP_MNGR
	if (ret == BCME_OK) {
		dhd_dump_file_manage_enqueue(dhd, memdump_path, fname);
	}
#endif /* DHD_DUMP_MNGR */

	return ret;
}
#endif /* DHD_DEBUG */

int dhd_os_wake_lock_timeout(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		ret = dhd->wakelock_rx_timeout_enable > dhd->wakelock_ctrl_timeout_enable ?
			dhd->wakelock_rx_timeout_enable : dhd->wakelock_ctrl_timeout_enable;
#ifdef CONFIG_HAS_WAKELOCK
		if (dhd->wakelock_rx_timeout_enable)
			dhd_wake_lock_timeout(&dhd->wl_rxwake,
				msecs_to_jiffies(dhd->wakelock_rx_timeout_enable));
		if (dhd->wakelock_ctrl_timeout_enable)
			dhd_wake_lock_timeout(&dhd->wl_ctrlwake,
				msecs_to_jiffies(dhd->wakelock_ctrl_timeout_enable));
#endif
		dhd->wakelock_rx_timeout_enable = 0;
		dhd->wakelock_ctrl_timeout_enable = 0;
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
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
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_rx_timeout_enable)
			dhd->wakelock_rx_timeout_enable = val;
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_ctrl_timeout_enable)
			dhd->wakelock_ctrl_timeout_enable = val;
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_cancel(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		dhd->wakelock_ctrl_timeout_enable = 0;
#ifdef CONFIG_HAS_WAKELOCK
		if (dhd_wake_lock_active(&dhd->wl_ctrlwake))
			dhd_wake_unlock(&dhd->wl_ctrlwake);
#endif
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
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

atomic_t trace_wklock_onoff;
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
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	hash_for_each_possible(wklock_history, wklock_info, wklock_node, addr)
#else
	struct hlist_node *entry;
	int index = hash_long(addr, ilog2(ARRAY_SIZE(wklock_history)));
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	hlist_for_each_entry(wklock_info, entry, &wklock_history[index], wklock_node)
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */
	{
		GCC_DIAGNOSTIC_POP();
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
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	hash_for_each(wklock_history, bkt, wklock_info, wklock_node)
#else
	struct hlist_node *entry = NULL;
	int max_index = ARRAY_SIZE(wklock_history);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for (bkt = 0; bkt < max_index; bkt++)
		hlist_for_each_entry(wklock_info, entry, &wklock_history[bkt], wklock_node)
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */
		{
			GCC_DIAGNOSTIC_POP();
			switch (wklock_info->lock_type) {
				case DHD_WAKE_LOCK:
					DHD_ERROR(("wakelock lock : %pS  lock_counter : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter));
					break;
				case DHD_WAKE_UNLOCK:
					DHD_ERROR(("wakelock unlock : %pS,"
						" unlock_counter : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter));
					break;
				case DHD_WAIVE_LOCK:
					DHD_ERROR(("wakelock waive : %pS  before_waive : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter));
					break;
				case DHD_RESTORE_LOCK:
					DHD_ERROR(("wakelock restore : %pS, after_waive : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter));
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

	DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	hash_init(wklock_history);
#else
	for (i = 0; i < ARRAY_SIZE(wklock_history); i++)
		INIT_HLIST_HEAD(&wklock_history[i]);
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */
	DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	atomic_set(&trace_wklock_onoff, 1);
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

	DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	hash_for_each_safe(wklock_history, bkt, tmp, wklock_info, wklock_node)
#else
	for (bkt = 0; bkt < max_index; bkt++)
		hlist_for_each_entry_safe(wklock_info, entry, tmp,
			&wklock_history[bkt], wklock_node)
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0)) */
		{
			GCC_DIAGNOSTIC_POP();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
			hash_del(&wklock_info->wklock_node);
#else
			hlist_del_init(&wklock_info->wklock_node);
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0)) */
			kfree(wklock_info);
		}
	DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
}

void dhd_wk_lock_stats_dump(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	unsigned long flags;

	DHD_ERROR(("DHD Printing wl_wake Lock/Unlock Record \r\n"));
	DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
	dhd_wk_lock_rec_dump();
	DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);

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
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
			dhd_wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
			dhd_bus_dev_pm_stay_awake(pub);
#endif
		}
#ifdef DHD_TRACE_WAKE_LOCK
		if (atomic_read(&trace_wklock_onoff)) {
			STORE_WKLOCK_RECORD(DHD_WAKE_LOCK);
		}
#endif /* DHD_TRACE_WAKE_LOCK */
		dhd->wakelock_counter++;
		ret = dhd->wakelock_counter;
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	}

	return ret;
}

void dhd_event_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		dhd_wake_lock(&dhd->wl_evtwake);
#elif defined(BCMSDIO)
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
		dhd_wake_lock_timeout(&dhd->wl_pmwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_HAS_WAKE_LOCK */
}

void
dhd_txfl_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		dhd_wake_lock_timeout(&dhd->wl_txflwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_HAS_WAKE_LOCK */
}

void
dhd_nan_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		dhd_wake_lock_timeout(&dhd->wl_nanwake, msecs_to_jiffies(val));
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
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);

		if (dhd->wakelock_counter > 0) {
			dhd->wakelock_counter--;
#ifdef DHD_TRACE_WAKE_LOCK
			if (atomic_read(&trace_wklock_onoff)) {
				STORE_WKLOCK_RECORD(DHD_WAKE_UNLOCK);
			}
#endif /* DHD_TRACE_WAKE_LOCK */
			if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
				dhd_wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
				dhd_bus_dev_pm_relax(pub);
#endif
			}
			ret = dhd->wakelock_counter;
		}
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

void dhd_event_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		dhd_wake_unlock(&dhd->wl_evtwake);
#elif defined(BCMSDIO)
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
		if (dhd_wake_lock_active(&dhd->wl_pmwake)) {
			dhd_wake_unlock(&dhd->wl_pmwake);
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
		if (dhd_wake_lock_active(&dhd->wl_txflwake)) {
			dhd_wake_unlock(&dhd->wl_txflwake);
		}
	}
#endif /* CONFIG_HAS_WAKELOCK */
}

void dhd_nan_wake_unlock(dhd_pub_t *pub)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_nanwake is active, unlock it */
		if (dhd_wake_lock_active(&dhd->wl_nanwake)) {
			dhd_wake_unlock(&dhd->wl_nanwake);
		}
	}
#endif /* CONFIG_HAS_WAKELOCK */
}

int dhd_os_check_wakelock(dhd_pub_t *pub)
{
#if defined(CONFIG_HAS_WAKELOCK) || defined(BCMSDIO)
#if defined(CONFIG_HAS_WAKELOCK)
	int l1, l2;
	int c, lock_active;
#endif /* CONFIG_HAS_WAKELOCK */
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);
	if (!dhd) {
		return 0;
	}
#endif /* CONFIG_HAS_WAKELOCK || BCMSDIO */

#ifdef CONFIG_HAS_WAKELOCK
	c = dhd->wakelock_counter;
	l1 = dhd_wake_lock_active(&dhd->wl_wifi);
	l2 = dhd_wake_lock_active(&dhd->wl_wdwake);
	lock_active = (l1 || l2);
	/* Indicate to the SD Host to avoid going to suspend if internal locks are up */
	if (lock_active) {
		DHD_ERROR(("%s wakelock c-%d wl-%d wd-%d\n",
			__FUNCTION__, c, l1, l2));
		return 1;
	}
#elif defined(BCMSDIO)
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub)) {
		DHD_ERROR(("%s wakelock c-%d\n", __FUNCTION__, dhd->wakelock_counter));
		return 1;
	}
#endif
	return 0;
}

int
dhd_os_check_wakelock_all(dhd_pub_t *pub)
{
#if defined(CONFIG_HAS_WAKELOCK) || defined(BCMSDIO)
#if defined(CONFIG_HAS_WAKELOCK)
	int l1, l2, l3, l4, l7, l8, l9, l10;
	int l5 = 0, l6 = 0;
	int c, lock_active;
#endif /* CONFIG_HAS_WAKELOCK */
	dhd_info_t *dhd;

	if (!pub) {
		return 0;
	}
	if (pub->up == 0) {
		DHD_ERROR(("%s: skip as down in progress\n", __FUNCTION__));
		return 0;
	}
	dhd = (dhd_info_t *)(pub->info);
	if (!dhd) {
		return 0;
	}
#endif /* CONFIG_HAS_WAKELOCK || BCMSDIO */

#ifdef CONFIG_HAS_WAKELOCK
	c = dhd->wakelock_counter;
	l1 = dhd_wake_lock_active(&dhd->wl_wifi);
	l2 = dhd_wake_lock_active(&dhd->wl_wdwake);
	l3 = dhd_wake_lock_active(&dhd->wl_rxwake);
	l4 = dhd_wake_lock_active(&dhd->wl_ctrlwake);
	l7 = dhd_wake_lock_active(&dhd->wl_evtwake);
#ifdef BCMPCIE_OOB_HOST_WAKE
	l5 = dhd_wake_lock_active(&dhd->wl_intrwake);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	l6 = dhd_wake_lock_active(&dhd->wl_scanwake);
#endif /* DHD_USE_SCAN_WAKELOCK */
	l8 = dhd_wake_lock_active(&dhd->wl_pmwake);
	l9 = dhd_wake_lock_active(&dhd->wl_txflwake);
	l10 = dhd_wake_lock_active(&dhd->wl_nanwake);
	lock_active = (l1 || l2 || l3 || l4 || l5 || l6 || l7 || l8 || l9 || l10);

	/* Indicate to the Host to avoid going to suspend if internal locks are up */
	if (lock_active) {
		DHD_ERROR(("%s wakelock c-%d wl-%d wd-%d rx-%d "
			"ctl-%d intr-%d scan-%d evt-%d, pm-%d, txfl-%d nan-%d\n",
			__FUNCTION__, c, l1, l2, l3, l4, l5, l6, l7, l8, l9, l10));
		return 1;
	}
#elif defined(BCMSDIO)
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub)) {
		DHD_ERROR(("%s wakelock c-%d\n", __FUNCTION__, dhd->wakelock_counter));
		return 1;
	}
#endif /* defined(BCMSDIO) */
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
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_wd_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
			/* if wakelock_wd_counter was never used : lock it at once */
			dhd_wake_lock(&dhd->wl_wdwake);
#endif
		}
		dhd->wakelock_wd_counter++;
		ret = dhd->wakelock_wd_counter;
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int dhd_os_wd_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_wd_counter > 0) {
			dhd->wakelock_wd_counter = 0;
			if (!dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
				dhd_wake_unlock(&dhd->wl_wdwake);
#endif
			}
		}
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

#ifdef BCMPCIE_OOB_HOST_WAKE
void
dhd_os_oob_irq_wake_lock_timeout(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		dhd_wake_lock_timeout(&dhd->wl_intrwake, msecs_to_jiffies(val));
#else
		printk("%s: =========\n",__FUNCTION__);
		wake_lock_timeout(&dhd->rx_wakelock, 5*HZ);
#endif /* CONFIG_HAS_WAKELOCK */
	}
}

void
dhd_os_oob_irq_wake_unlock(dhd_pub_t *pub)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_intrwake is active, unlock it */
		if (dhd_wake_lock_active(&dhd->wl_intrwake)) {
			dhd_wake_unlock(&dhd->wl_intrwake);
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
		dhd_wake_lock_timeout(&dhd->wl_scanwake, msecs_to_jiffies(val));
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
		if (dhd_wake_lock_active(&dhd->wl_scanwake)) {
			dhd_wake_unlock(&dhd->wl_scanwake);
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
		DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);

		/* dhd_wakelock_waive/dhd_wakelock_restore must be paired */
		if (dhd->waive_wakelock == FALSE) {
#ifdef DHD_TRACE_WAKE_LOCK
			if (atomic_read(&trace_wklock_onoff)) {
				STORE_WKLOCK_RECORD(DHD_WAIVE_LOCK);
			}
#endif /* DHD_TRACE_WAKE_LOCK */
			/* record current lock status */
			dhd->wakelock_before_waive = dhd->wakelock_counter;
			dhd->waive_wakelock = TRUE;
		}
		ret = dhd->wakelock_wd_counter;
		DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
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

	DHD_WAKE_SPIN_LOCK(&dhd->wakelock_spinlock, flags);

	/* dhd_wakelock_waive/dhd_wakelock_restore must be paired */
	if (!dhd->waive_wakelock)
		goto exit;

	dhd->waive_wakelock = FALSE;
	/* if somebody else acquires wakelock between dhd_wakelock_waive/dhd_wakelock_restore,
	 * we need to make it up by calling dhd_wake_lock or pm_stay_awake. or if somebody releases
	 * the lock in between, do the same by calling dhd_wake_unlock or pm_relax
	 */
#ifdef DHD_TRACE_WAKE_LOCK
	if (atomic_read(&trace_wklock_onoff)) {
		STORE_WKLOCK_RECORD(DHD_RESTORE_LOCK);
	}
#endif /* DHD_TRACE_WAKE_LOCK */

	if (dhd->wakelock_before_waive == 0 && dhd->wakelock_counter > 0) {
#ifdef CONFIG_HAS_WAKELOCK
		dhd_wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
		dhd_bus_dev_pm_stay_awake(&dhd->pub);
#endif
	} else if (dhd->wakelock_before_waive > 0 && dhd->wakelock_counter == 0) {
#ifdef CONFIG_HAS_WAKELOCK
		dhd_wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
		dhd_bus_dev_pm_relax(&dhd->pub);
#endif
	}
	dhd->wakelock_before_waive = 0;
exit:
	ret = dhd->wakelock_wd_counter;
	DHD_WAKE_SPIN_UNLOCK(&dhd->wakelock_spinlock, flags);
	return ret;
}

void dhd_os_wake_lock_init(struct dhd_info *dhd)
{
	DHD_TRACE(("%s: initialize wake_lock_counters\n", __FUNCTION__));
	dhd->wakelock_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
	/* wakelocks prevent a system from going into a low power state */
#ifdef CONFIG_HAS_WAKELOCK
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	dhd_wake_lock_init(&dhd->wl_rxwake, WAKE_LOCK_SUSPEND, "wlan_rx_wake");
	dhd_wake_lock_init(&dhd->wl_ctrlwake, WAKE_LOCK_SUSPEND, "wlan_ctrl_wake");
	dhd_wake_lock_init(&dhd->wl_evtwake, WAKE_LOCK_SUSPEND, "wlan_evt_wake");
	dhd_wake_lock_init(&dhd->wl_pmwake, WAKE_LOCK_SUSPEND, "wlan_pm_wake");
	dhd_wake_lock_init(&dhd->wl_txflwake, WAKE_LOCK_SUSPEND, "wlan_txfl_wake");
#ifdef BCMPCIE_OOB_HOST_WAKE
	dhd_wake_lock_init(&dhd->wl_intrwake, WAKE_LOCK_SUSPEND, "wlan_oob_irq_wake");
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	dhd_wake_lock_init(&dhd->wl_scanwake, WAKE_LOCK_SUSPEND, "wlan_scan_wake");
#endif /* DHD_USE_SCAN_WAKELOCK */
	dhd_wake_lock_init(&dhd->wl_nanwake, WAKE_LOCK_SUSPEND, "wlan_nan_wake");
#endif /* CONFIG_HAS_WAKELOCK */
#ifdef DHD_TRACE_WAKE_LOCK
	dhd_wk_lock_trace_init(dhd);
#endif /* DHD_TRACE_WAKE_LOCK */
	wake_lock_init(&dhd->rx_wakelock, WAKE_LOCK_SUSPEND, "wlan_rx_wakelock");
}

void dhd_os_wake_lock_destroy(struct dhd_info *dhd)
{
	DHD_TRACE(("%s: deinit wake_lock_counters\n", __FUNCTION__));
#ifdef CONFIG_HAS_WAKELOCK
	dhd->wakelock_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	dhd_wake_lock_unlock_destroy(&dhd->wl_rxwake);
	dhd_wake_lock_unlock_destroy(&dhd->wl_ctrlwake);
	dhd_wake_lock_unlock_destroy(&dhd->wl_evtwake);
	dhd_wake_lock_unlock_destroy(&dhd->wl_pmwake);
	dhd_wake_lock_unlock_destroy(&dhd->wl_txflwake);
#ifdef BCMPCIE_OOB_HOST_WAKE
	dhd_wake_lock_unlock_destroy(&dhd->wl_intrwake);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	dhd_wake_lock_unlock_destroy(&dhd->wl_scanwake);
#endif /* DHD_USE_SCAN_WAKELOCK */
	dhd_wake_lock_unlock_destroy(&dhd->wl_nanwake);
#ifdef DHD_TRACE_WAKE_LOCK
	dhd_wk_lock_trace_deinit(dhd);
#endif /* DHD_TRACE_WAKE_LOCK */
#else /* !CONFIG_HAS_WAKELOCK */
	if (dhd->wakelock_counter > 0) {
		DHD_ERROR(("%s: wake lock count=%d\n",
			__FUNCTION__, dhd->wakelock_counter));
		while (dhd_os_wake_unlock(&dhd->pub));
	}
#endif /* CONFIG_HAS_WAKELOCK */
	wake_lock_destroy(&dhd->rx_wakelock);
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
		"  Driver: %s\n%s  Firmware: %s\n%s  CLM: %s ",
		EPI_VERSION_STR,
		DHD_LOG_PREFIXS, fw,
		DHD_LOG_PREFIXS, clm_version);
	printf("%s\n", info_string);

	if (!dhdp)
		return;

	i = snprintf(&info_string[i], sizeof(info_string) - i,
		"\n%s  Chip: %x Rev %x", DHD_LOG_PREFIXS, dhd_conf_get_chip(dhdp),
		dhd_conf_get_chiprev(dhdp));
}

int dhd_ioctl_entry_local(struct net_device *net, wl_ioctl_t *ioc, int cmd)
{
	int ifidx;
	int ret = 0;
	dhd_info_t *dhd = NULL;

	if (!net || !DEV_PRIV(net)) {
		DHD_ERROR(("%s invalid parameter net %p dev_priv %p\n",
			__FUNCTION__, net, DEV_PRIV(net)));
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

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, ioc, ioc->buf, ioc->len);
	dhd_check_hang(net, &dhd->pub, ret);

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

#if defined(WL_CFG80211) && defined(SUPPORT_DEEP_SLEEP)
#define MAX_TRY_CNT             5 /* Number of tries to disable deepsleep */
int dhd_deepsleep(struct net_device *dev, int flag)
{
	char iovbuf[20];
	uint powervar = 0;
	dhd_info_t *dhd;
	dhd_pub_t *dhdp;
	int cnt = 0;
	int ret = 0;

	dhd = DHD_DEV_INFO(dev);
	dhdp = &dhd->pub;

	switch (flag) {
		case 1 :  /* Deepsleep on */
			DHD_ERROR(("[WiFi] Deepsleep On\n"));
			/* give some time to sysioc_work before deepsleep */
			OSL_SLEEP(200);
#ifdef PKT_FILTER_SUPPORT
		/* disable pkt filter */
		dhd_enable_packet_filter(0, dhdp);
#endif /* PKT_FILTER_SUPPORT */
			/* Disable MPC */
			powervar = 0;
			ret = dhd_iovar(dhdp, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
					0, TRUE);
			if (ret) {
				DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
			}
			/* Enable Deepsleep */
			powervar = 1;
			ret = dhd_iovar(dhdp, 0, "deepsleep", (char *)&powervar, sizeof(powervar),
					NULL, 0, TRUE);
			if (ret) {
				DHD_ERROR(("%s: deepsleep failed:%d\n", __FUNCTION__, ret));
			}
			break;

		case 0: /* Deepsleep Off */
			DHD_ERROR(("[WiFi] Deepsleep Off\n"));

			/* Disable Deepsleep */
			for (cnt = 0; cnt < MAX_TRY_CNT; cnt++) {
				powervar = 0;
				ret = dhd_iovar(dhdp, 0, "deepsleep", (char *)&powervar,
						sizeof(powervar), NULL, 0, TRUE);
				if (ret) {
					DHD_ERROR(("%s: deepsleep failed:%d\n", __FUNCTION__, ret));
				}

				ret = dhd_iovar(dhdp, 0, "deepsleep", (char *)&powervar,
						sizeof(powervar), iovbuf, sizeof(iovbuf), FALSE);
				if (ret < 0) {
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
			ret = dhd_iovar(dhdp, 0, "mpc", (char *)&powervar, sizeof(powervar),
					NULL, 0, TRUE);
			if (ret) {
				DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
			}
			break;
	}

	return 0;
}
#endif /* WL_CFG80211 && SUPPORT_DEEP_SLEEP */

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

	/* XXX: The user can request any length they want, but they are getting 4 bytes */
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

	/* XXX: The user can request any length they want, but they are getting 4 bytes */
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
	DHD_TRACE(("%s: set(%d) cpucore success!\n", __FUNCTION__, set));

	return;
}
#endif /* CUSTOM_SET_CPUCORE */
#if defined(DHD_TCP_WINSIZE_ADJUST)
static int dhd_port_list_match(int port)
{
	int i;
	for (i = 0; i < MAX_TARGET_PORTS; i++) {
		if (target_ports[i] == port)
			return 1;
	}
	return 0;
}
static void dhd_adjust_tcp_winsize(int op_mode, struct sk_buff *skb)
{
	struct iphdr *ipheader;
	struct tcphdr *tcpheader;
	uint16 win_size;
	int32 incremental_checksum;

	if (!(op_mode & DHD_FLAG_HOSTAP_MODE))
		return;
	if (skb == NULL || skb->data == NULL)
		return;

	ipheader = (struct iphdr*)(skb->data);

	if (ipheader->protocol == IPPROTO_TCP) {
		tcpheader = (struct tcphdr*) skb_pull(skb, (ipheader->ihl)<<2);
		if (tcpheader) {
			win_size = ntoh16(tcpheader->window);
			if (win_size < MIN_TCP_WIN_SIZE &&
				dhd_port_list_match(ntoh16(tcpheader->dest))) {
				incremental_checksum = ntoh16(tcpheader->check);
				incremental_checksum += win_size - win_size*WIN_SIZE_SCALE_FACTOR;
				if (incremental_checksum < 0)
					--incremental_checksum;
				tcpheader->window = hton16(win_size*WIN_SIZE_SCALE_FACTOR);
				tcpheader->check = hton16((unsigned short)incremental_checksum);
			}
		}
		skb_push(skb, (ipheader->ihl)<<2);
	}
}
#endif /* DHD_TCP_WINSIZE_ADJUST */

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

#ifdef DHD_RND_DEBUG
/*
 * XXX The filename to store .rnd.(in/out) is defined for each platform.
 * - The default path of CUSTOMER_HW4 device is "PLATFORM_PATH/.memdump.info"
 * - Brix platform will take default path "/installmedia/.memdump.info"
 * New platforms can add their ifdefs accordingly below.
 */

#ifdef CUSTOMER_HW4_DEBUG
#define RNDINFO PLATFORM_PATH".rnd"
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
#define RNDINFO "/data/misc/wifi/.rnd"
#elif defined(OEM_ANDROID) && defined(__ARM_ARCH_7A__)
#define RNDINFO "/data/misc/wifi/.rnd"
#elif defined(OEM_ANDROID)
#define RNDINFO_LIVE "/installmedia/.rnd"
#define RNDINFO_INST "/data/.rnd"
#define RNDINFO RNDINFO_LIVE
#else /* FC19 and Others */
#define RNDINFO "/root/.rnd"
#endif /* CUSTOMER_HW4_DEBUG */

#define RND_IN RNDINFO".in"
#define RND_OUT RNDINFO".out"

int
dhd_get_rnd_info(dhd_pub_t *dhd)
{
	struct file *fp = NULL;
	int ret = BCME_ERROR;
	char *filepath = RND_IN;
	uint32 file_mode =  O_RDONLY;
	mm_segment_t old_fs;
	loff_t pos = 0;

	/* Read memdump info from the file */
	fp = filp_open(filepath, file_mode, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
#if defined(CONFIG_X86) && defined(OEM_ANDROID)
		/* Check if it is Live Brix Image */
		if (bcmstrstr(filepath, RNDINFO_LIVE)) {
			goto err1;
		}
		/* Try if it is Installed Brix Image */
		filepath = RNDINFO_INST".in";
		DHD_ERROR(("%s: Try File [%s]\n", __FUNCTION__, filepath));
		fp = filp_open(filepath, file_mode, 0);
		if (IS_ERR(fp)) {
			DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
			goto err1;
		}
#else /* Non Brix Android platform */
		goto err1;
#endif /* CONFIG_X86 && OEM_ANDROID */
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* Handle success case */
	ret = vfs_read(fp, (char *)&dhd->rnd_len, sizeof(dhd->rnd_len), &pos);
	if (ret < 0) {
		DHD_ERROR(("%s: rnd_len read error, ret=%d\n", __FUNCTION__, ret));
		goto err2;
	}

	dhd->rnd_buf = MALLOCZ(dhd->osh, dhd->rnd_len);
	if (!dhd->rnd_buf) {
		DHD_ERROR(("%s: MALLOC failed\n", __FUNCTION__));
		goto err2;
	}

	ret = vfs_read(fp, (char *)dhd->rnd_buf, dhd->rnd_len, &pos);
	if (ret < 0) {
		DHD_ERROR(("%s: rnd_buf read error, ret=%d\n", __FUNCTION__, ret));
		goto err3;
	}

	set_fs(old_fs);
	filp_close(fp, NULL);

	DHD_ERROR(("%s: RND read from %s\n", __FUNCTION__, filepath));
	return BCME_OK;

err3:
	MFREE(dhd->osh, dhd->rnd_buf, dhd->rnd_len);
	dhd->rnd_buf = NULL;
err2:
	set_fs(old_fs);
	filp_close(fp, NULL);
err1:
	return BCME_ERROR;
}

int
dhd_dump_rnd_info(dhd_pub_t *dhd, uint8 *rnd_buf, uint32 rnd_len)
{
	struct file *fp = NULL;
	int ret = BCME_OK;
	char *filepath = RND_OUT;
	uint32 file_mode = O_CREAT | O_WRONLY | O_SYNC;
	mm_segment_t old_fs;
	loff_t pos = 0;

	/* Read memdump info from the file */
	fp = filp_open(filepath, file_mode, 0664);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
#if defined(CONFIG_X86) && defined(OEM_ANDROID)
		/* Check if it is Live Brix Image */
		if (bcmstrstr(filepath, RNDINFO_LIVE)) {
			goto err1;
		}
		/* Try if it is Installed Brix Image */
		filepath = RNDINFO_INST".out";
		DHD_ERROR(("%s: Try File [%s]\n", __FUNCTION__, filepath));
		fp = filp_open(filepath, file_mode, 0664);
		if (IS_ERR(fp)) {
			DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
			goto err1;
		}
#else /* Non Brix Android platform */
		goto err1;
#endif /* CONFIG_X86 && OEM_ANDROID */
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* Handle success case */
	ret = vfs_write(fp, (char *)&rnd_len, sizeof(rnd_len), &pos);
	if (ret < 0) {
		DHD_ERROR(("%s: rnd_len write error, ret=%d\n", __FUNCTION__, ret));
		goto err2;
	}

	ret = vfs_write(fp, (char *)rnd_buf, rnd_len, &pos);
	if (ret < 0) {
		DHD_ERROR(("%s: rnd_buf write error, ret=%d\n", __FUNCTION__, ret));
		goto err2;
	}

	set_fs(old_fs);
	filp_close(fp, NULL);
	DHD_ERROR(("%s: RND written to %s\n", __FUNCTION__, filepath));
	return BCME_OK;

err2:
	set_fs(old_fs);
	filp_close(fp, NULL);
err1:
	return BCME_ERROR;

}
#endif /* DHD_RND_DEBUG */

#ifdef DHD_FW_COREDUMP
void dhd_schedule_memdump(dhd_pub_t *dhdp, uint8 *buf, uint32 size)
{
	dhd_dump_t *dump = NULL;
	unsigned long flags = 0;
	dhd_info_t *dhd_info = NULL;
#if defined(DHD_LOG_DUMP) && !defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	log_dump_type_t type = DLD_BUF_TYPE_ALL;
#endif /* DHD_LOG_DUMP && !DHD_DUMP_FILE_WRITE_FROM_KERNEL */

	dhd_info = (dhd_info_t *)dhdp->info;
	dump = (dhd_dump_t *)MALLOC(dhdp->osh, sizeof(dhd_dump_t));
	if (dump == NULL) {
		DHD_ERROR(("%s: dhd dump memory allocation failed\n", __FUNCTION__));
		return;
	}
	dump->buf = buf;
	dump->bufsize = size;
#ifdef BCMPCIE
	dhd_get_hscb_info(dhdp, (void*)(&dump->hscb_buf),
			(uint32 *)(&dump->hscb_bufsize));
#else
	dump->hscb_bufsize = 0;
#endif /* BCMPCIE */

#ifdef DHD_LOG_DUMP
	dhd_print_buf_addr(dhdp, "memdump", buf, size);
#if !defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	/* Print out buffer infomation */
	dhd_log_dump_buf_addr(dhdp, &type);
#endif /* !DHD_DUMP_FILE_WRITE_FROM_KERNEL */
#endif /* DHD_LOG_DUMP */

	if (dhdp->memdump_enabled == DUMP_MEMONLY) {
		BUG_ON(1);
	}

	if ((dhdp->memdump_type == DUMP_TYPE_DONGLE_INIT_FAILURE) ||
		(dhdp->memdump_type == DUMP_TYPE_DUE_TO_BT) ||
		(dhdp->memdump_type == DUMP_TYPE_SMMU_FAULT))
	{
		dhd_info->scheduled_memdump = FALSE;
		dhd_mem_dump((void *)dhdp->info, (void *)dump, 0);
		/* No need to collect debug dump for init failure */
		if (dhdp->memdump_type == DUMP_TYPE_DONGLE_INIT_FAILURE) {
			return;
		}
#ifdef DHD_LOG_DUMP
		{
			log_dump_type_t *flush_type = NULL;
			/* for dongle init fail cases, 'dhd_mem_dump' does
			 * not call 'dhd_log_dump', so call it here.
			*/
			flush_type = MALLOCZ(dhdp->osh,
				sizeof(log_dump_type_t));
			if (flush_type) {
				*flush_type = DLD_BUF_TYPE_ALL;
				DHD_ERROR(("%s: calling log dump.. \n", __FUNCTION__));
				dhd_log_dump(dhdp->info, flush_type, 0);
			}
		}
#endif /* DHD_LOG_DUMP */
		return;
	}

	dhd_info->scheduled_memdump = TRUE;

	/* bus busy bit for mem dump will be cleared in mem dump
	* work item context, after mem dump file is written
	*/
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_SET_IN_MEMDUMP(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
	DHD_ERROR(("%s: scheduling mem dump.. \n", __FUNCTION__));
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)dump,
		DHD_WQ_WORK_SOC_RAM_DUMP, dhd_mem_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}

static void
dhd_mem_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_pub_t *dhdp = NULL;
	unsigned long flags = 0;

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT)
	int ret = 0;
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT */
	dhd_dump_t *dump = NULL;
#ifdef DHD_COREDUMP
	char pc_fn[DHD_FUNC_STR_LEN] = "\0";
	char lr_fn[DHD_FUNC_STR_LEN] = "\0";
	char *map_path = VENDOR_PATH CONFIG_BCMDHD_MAP_PATH;
	trap_t *tr;
#endif /* DHD_COREDUMP */

	DHD_ERROR(("%s: ENTER \n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	dhdp = &dhd->pub;
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return;
	}

	DHD_GENERAL_LOCK(dhdp, flags);
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_GENERAL_UNLOCK(dhdp, flags);
		DHD_ERROR(("%s: bus is down! can't collect mem dump. \n", __FUNCTION__));
		goto exit;
	}
	DHD_GENERAL_UNLOCK(dhdp, flags);

	dump = (dhd_dump_t *)event_info;
	if (!dump) {
		DHD_ERROR(("%s: dump is NULL\n", __FUNCTION__));
		goto exit;
	}

#ifdef DHD_SDTC_ETB_DUMP
	if (dhdp->collect_sdtc) {
		dhd_sdtc_etb_dump(dhdp);
		dhdp->collect_sdtc = FALSE;
	}
#endif /* DHD_SDTC_ETB_DUMP */

#ifdef DHD_SSSR_DUMP
	DHD_ERROR(("%s: sssr_enab=%d dhdp->sssr_inited=%d dhdp->collect_sssr=%d\n",
		__FUNCTION__, sssr_enab, dhdp->sssr_inited, dhdp->collect_sssr));
	if (sssr_enab && dhdp->sssr_inited && dhdp->collect_sssr) {
		if (fis_enab && dhdp->sssr_reg_info->rev3.fis_enab) {
			int bcmerror = dhd_bus_fis_trigger(dhdp);

			if (bcmerror == BCME_OK) {
				dhd_bus_fis_dump(dhdp);
			} else {
				DHD_ERROR(("%s: FIS trigger failed: %d\n",
					__FUNCTION__, bcmerror));
			}
		} else	{
			DHD_ERROR(("%s: FIS not enabled (%d:%d), collect legacy sssr\n",
				__FUNCTION__, fis_enab, dhdp->sssr_reg_info->rev3.fis_enab));
			dhdpcie_sssr_dump(dhdp);
		}
	}
	dhdp->collect_sssr = FALSE;
#endif /* DHD_SSSR_DUMP */

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT)
	ret = dhd_wait_for_file_dump(dhdp);
#ifdef BOARD_HIKEY
	/* For Hikey do force kernel write of socram if HAL dump fails */
	if (ret) {
		if (write_dump_to_file(&dhd->pub, dump->buf, dump->bufsize, "mem_dump")) {
			DHD_ERROR(("%s: writing SoC_RAM dump to the file failed\n", __FUNCTION__));
		}
	}
#endif /* BOARD_HIKEY */
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT */

#ifdef DHD_COREDUMP
	memset_s(dhdp->memdump_str, DHD_MEMDUMP_LONGSTR_LEN, 0, DHD_MEMDUMP_LONGSTR_LEN);
	dhd_convert_memdump_type_to_str(dhdp->memdump_type, dhdp->memdump_str,
		DHD_MEMDUMP_LONGSTR_LEN, dhdp->debug_dump_subcmd);
	if (dhdp->memdump_type == DUMP_TYPE_DONGLE_TRAP &&
		dhdp->dongle_trap_occured == TRUE) {
		tr = &dhdp->last_trap_info;
		dhd_lookup_map(dhdp->osh, map_path,
			ltoh32(tr->epc), pc_fn, ltoh32(tr->r14), lr_fn);
		sprintf(&dhdp->memdump_str[strlen(dhdp->memdump_str)], "_%.79s_%.79s",
				pc_fn, lr_fn);
	}
	DHD_ERROR(("%s: dump reason: %s\n", __FUNCTION__, dhdp->memdump_str));
	if (wifi_platform_set_coredump(dhd->adapter, dump->buf, dump->bufsize, dhdp->memdump_str)) {
		DHD_ERROR(("%s: writing SoC_RAM dump failed\n", __FUNCTION__));
#ifdef DHD_DEBUG_UART
		dhd->pub.memdump_success = FALSE;
#endif  /* DHD_DEBUG_UART */
	}
#endif /* DHD_COREDUMP */

	/*
	 * If kernel does not have file write access enabled
	 * then skip writing dumps to files.
	 * The dumps will be pushed to HAL layer which will
	 * write into files
	 */
#ifdef DHD_DUMP_FILE_WRITE_FROM_KERNEL

#ifdef D2H_MINIDUMP
	/* dump minidump */
	if (dhd_bus_is_minidump_enabled(dhdp)) {
		dhd_d2h_minidump(&dhd->pub);
	} else {
		DHD_ERROR(("minidump is not enabled\n"));
	}
#endif /* D2H_MINIDUMP */

	if (write_dump_to_file(&dhd->pub, dump->buf, dump->bufsize, "mem_dump")) {
		DHD_ERROR(("%s: writing SoC_RAM dump to the file failed\n", __FUNCTION__));
#ifdef DHD_DEBUG_UART
		dhd->pub.memdump_success = FALSE;
#endif	/* DHD_DEBUG_UART */
	}

	if (dump->hscb_buf && dump->hscb_bufsize) {
		if (write_dump_to_file(&dhd->pub, dump->hscb_buf,
			dump->hscb_bufsize, "mem_dump_hscb")) {
			DHD_ERROR(("%s: writing HSCB dump to the file failed\n", __FUNCTION__));
#ifdef DHD_DEBUG_UART
			dhd->pub.memdump_success = FALSE;
#endif	/* DHD_DEBUG_UART */
		}
	}

#ifndef DHD_PKT_LOGGING
	clear_debug_dump_time(dhdp->debug_dump_time_str);
#endif /* !DHD_PKT_LOGGING */

	/* directly call dhd_log_dump for debug_dump collection from the mem_dump work queue
	* context, no need to schedule another work queue for log dump. In case of
	* user initiated DEBUG_DUMP wpa_cli command (DUMP_TYPE_BY_SYSDUMP),
	* cfg layer is itself scheduling the log_dump work queue.
	* that path is not disturbed. If 'dhd_mem_dump' is called directly then we will not
	* collect debug_dump as it may be called from non-sleepable context.
	*/
#ifdef DHD_LOG_DUMP
	if (dhd->scheduled_memdump &&
		dhdp->memdump_type != DUMP_TYPE_BY_SYSDUMP) {
		log_dump_type_t *flush_type = MALLOCZ(dhdp->osh,
				sizeof(log_dump_type_t));
		if (flush_type) {
			*flush_type = DLD_BUF_TYPE_ALL;
			DHD_ERROR(("%s: calling log dump.. \n", __FUNCTION__));
			dhd_log_dump(dhd, flush_type, 0);
		}
	}
#endif /* DHD_LOG_DUMP */

	/* before calling bug on, wait for other logs to be dumped.
	* we cannot wait in case dhd_mem_dump is called directly
	* as it may not be from a sleepable context
	*/
	if (dhd->scheduled_memdump) {
		uint bitmask = 0;
		int timeleft = 0;
#ifdef DHD_SSSR_DUMP
		bitmask |= DHD_BUS_BUSY_IN_SSSRDUMP;
#endif
		if (bitmask != 0) {
			DHD_ERROR(("%s: wait to clear dhd_bus_busy_state: 0x%x\n",
				__FUNCTION__, dhdp->dhd_bus_busy_state));
			timeleft = dhd_os_busbusy_wait_bitmask(dhdp,
					&dhdp->dhd_bus_busy_state, bitmask, 0);
			if ((timeleft == 0) || (timeleft == 1)) {
				DHD_ERROR(("%s: Timed out dhd_bus_busy_state=0x%x\n",
					__FUNCTION__, dhdp->dhd_bus_busy_state));
			}
		}
	}
#endif /* DHD_DUMP_FILE_WRITE_FROM_KERNEL */

	if (dhd->pub.memdump_enabled == DUMP_MEMFILE_BUGON &&
#ifdef WLAN_ACCEL_BOOT
		/* BUG_ON only if wlan accel boot up is done */
		dhd->wl_accel_boot_on_done == TRUE &&
#endif /* WLAN_ACCEL_BOOT */
#ifdef DHD_LOG_DUMP
		dhd->pub.memdump_type != DUMP_TYPE_BY_SYSDUMP &&
#endif /* DHD_LOG_DUMP */
		dhd->pub.memdump_type != DUMP_TYPE_BY_USER &&
#ifdef DHD_DEBUG_UART
		dhd->pub.memdump_success == TRUE &&
#endif	/* DHD_DEBUG_UART */
#ifdef DNGL_EVENT_SUPPORT
		dhd->pub.memdump_type != DUMP_TYPE_DONGLE_HOST_EVENT &&
#endif /* DNGL_EVENT_SUPPORT */
		dhd->pub.memdump_type != DUMP_TYPE_CFG_VENDOR_TRIGGERED) {
#ifdef SHOW_LOGTRACE
		/* Wait till logtrace context is flushed */
		dhd_flush_logtrace_process(dhd);
#endif /* SHOW_LOGTRACE */

#ifdef BTLOG
		/* Wait till bt_log_dispatcher_work finishes */
		cancel_work_sync(&dhd->bt_log_dispatcher_work);
#endif /* BTLOG */

#ifdef EWP_EDL
		cancel_delayed_work_sync(&dhd->edl_dispatcher_work);
#endif

		printf("%s\n", info_string);
		printf("MAC %pM\n", &dhdp->mac);
		DHD_ERROR(("%s: call BUG_ON \n", __FUNCTION__));
//		BUG_ON(1);
	}

exit:
	if (dump) {
		MFREE(dhd->pub.osh, dump, sizeof(dhd_dump_t));
	}
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_MEMDUMP(&dhd->pub);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
	dhd->scheduled_memdump = FALSE;

#ifdef OEM_ANDROID
	if (dhdp->hang_was_pending) {
		DHD_ERROR(("%s: Send pending HANG event...\n", __FUNCTION__));
		dhd_os_send_hang_message(dhdp);
		dhdp->hang_was_pending = 0;
	}
#endif /* OEM_ANDROID */
	DHD_ERROR(("%s: EXIT \n", __FUNCTION__));

	return;
}
#endif /* DHD_FW_COREDUMP */

#ifdef D2H_MINIDUMP
void
dhd_d2h_minidump(dhd_pub_t *dhdp)
{
	char d2h_minidump[128];
	dhd_dma_buf_t *minidump_buf;

	minidump_buf = dhd_prot_get_minidump_buf(dhdp);
	if (minidump_buf->va == NULL) {
		DHD_ERROR(("%s: minidump_buf is NULL\n", __FUNCTION__));
		return;
	}

	/* Init file name */
	memset(d2h_minidump, 0, sizeof(d2h_minidump));
	snprintf(d2h_minidump, sizeof(d2h_minidump), "%s", "d2h_minidump");

	if (write_dump_to_file(dhdp, (uint8 *)minidump_buf->va, minidump_buf->len, d2h_minidump)) {
		DHD_ERROR(("%s: failed to dump d2h_minidump to file\n", __FUNCTION__));
	}
}
#endif /* D2H_MINIDUMP */

#ifdef DHD_SSSR_DUMP
uint
dhd_sssr_dig_buf_size(dhd_pub_t *dhdp)
{
	uint dig_buf_size = 0;

	/* SSSR register information structure v0 and v1 shares most except dig_mem */
	switch (dhdp->sssr_reg_info->rev2.version) {
		case SSSR_REG_INFO_VER_3:
			/* intentional fall through */
		case SSSR_REG_INFO_VER_2 :
			if ((dhdp->sssr_reg_info->rev2.length >
			 OFFSETOF(sssr_reg_info_v2_t, dig_mem_info)) &&
			 dhdp->sssr_reg_info->rev2.dig_mem_info.dig_sr_size) {
				dig_buf_size = dhdp->sssr_reg_info->rev2.dig_mem_info.dig_sr_size;
			}
			break;
		case SSSR_REG_INFO_VER_1 :
			if (dhdp->sssr_reg_info->rev1.vasip_regs.vasip_sr_size) {
				dig_buf_size = dhdp->sssr_reg_info->rev1.vasip_regs.vasip_sr_size;
			} else if ((dhdp->sssr_reg_info->rev1.length >
			 OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
			 dhdp->sssr_reg_info->rev1.dig_mem_info.dig_sr_size) {
				dig_buf_size = dhdp->sssr_reg_info->rev1.dig_mem_info.dig_sr_size;
			}
			break;
		case SSSR_REG_INFO_VER_0 :
			if (dhdp->sssr_reg_info->rev0.vasip_regs.vasip_sr_size) {
				dig_buf_size = dhdp->sssr_reg_info->rev0.vasip_regs.vasip_sr_size;
			}
			break;
		default :
			DHD_ERROR(("invalid sssr_reg_ver"));
			return BCME_UNSUPPORTED;
	}

	return dig_buf_size;
}

uint
dhd_sssr_dig_buf_addr(dhd_pub_t *dhdp)
{
	uint dig_buf_addr = 0;

	/* SSSR register information structure v0 and v1 shares most except dig_mem */
	switch (dhdp->sssr_reg_info->rev2.version) {
		case SSSR_REG_INFO_VER_3 :
			/* intentional fall through */
		case SSSR_REG_INFO_VER_2 :
			if ((dhdp->sssr_reg_info->rev2.length >
			 OFFSETOF(sssr_reg_info_v2_t, dig_mem_info)) &&
			 dhdp->sssr_reg_info->rev2.dig_mem_info.dig_sr_size) {
				dig_buf_addr = dhdp->sssr_reg_info->rev2.dig_mem_info.dig_sr_addr;
			}
			break;
		case SSSR_REG_INFO_VER_1 :
			if (dhdp->sssr_reg_info->rev1.vasip_regs.vasip_sr_size) {
				dig_buf_addr = dhdp->sssr_reg_info->rev1.vasip_regs.vasip_sr_addr;
			} else if ((dhdp->sssr_reg_info->rev1.length >
			 OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
			 dhdp->sssr_reg_info->rev1.dig_mem_info.dig_sr_size) {
				dig_buf_addr = dhdp->sssr_reg_info->rev1.dig_mem_info.dig_sr_addr;
			}
			break;
		case SSSR_REG_INFO_VER_0 :
			if (dhdp->sssr_reg_info->rev0.vasip_regs.vasip_sr_size) {
				dig_buf_addr = dhdp->sssr_reg_info->rev0.vasip_regs.vasip_sr_addr;
			}
			break;
		default :
			DHD_ERROR(("invalid sssr_reg_ver"));
			return BCME_UNSUPPORTED;
	}

	return dig_buf_addr;
}

uint
dhd_sssr_mac_buf_size(dhd_pub_t *dhdp, uint8 core_idx)
{
	uint mac_buf_size = 0;
	uint8 num_d11cores;

	num_d11cores = dhd_d11_slices_num_get(dhdp);

	/* SSSR register information structure v0 and v1 shares most except dig_mem */
	if (core_idx < num_d11cores) {
		switch (dhdp->sssr_reg_info->rev2.version) {
			case SSSR_REG_INFO_VER_3 :
				/* intentional fall through */
			case SSSR_REG_INFO_VER_2 :
				mac_buf_size = dhdp->sssr_reg_info->rev2.mac_regs[core_idx].sr_size;
				break;
			case SSSR_REG_INFO_VER_1 :
				mac_buf_size = dhdp->sssr_reg_info->rev1.mac_regs[core_idx].sr_size;
				break;
			case SSSR_REG_INFO_VER_0 :
				mac_buf_size = dhdp->sssr_reg_info->rev0.mac_regs[core_idx].sr_size;
				break;
			default :
				DHD_ERROR(("invalid sssr_reg_ver"));
				return BCME_UNSUPPORTED;
		}
	}

	return mac_buf_size;
}

uint
dhd_sssr_mac_xmtaddress(dhd_pub_t *dhdp, uint8 core_idx)
{
	uint xmtaddress = 0;
	uint8 num_d11cores;

	num_d11cores = dhd_d11_slices_num_get(dhdp);

	/* SSSR register information structure v0 and v1 shares most except dig_mem */
	if (core_idx < num_d11cores) {
		switch (dhdp->sssr_reg_info->rev2.version) {
			case SSSR_REG_INFO_VER_3 :
				/* intentional fall through */
			case SSSR_REG_INFO_VER_2 :
				xmtaddress = dhdp->sssr_reg_info->rev2.
					mac_regs[core_idx].base_regs.xmtaddress;
				break;
			case SSSR_REG_INFO_VER_1 :
				xmtaddress = dhdp->sssr_reg_info->rev1.
					mac_regs[core_idx].base_regs.xmtaddress;
				break;
			case SSSR_REG_INFO_VER_0 :
				xmtaddress = dhdp->sssr_reg_info->rev0.
					mac_regs[core_idx].base_regs.xmtaddress;
				break;
			default :
				DHD_ERROR(("invalid sssr_reg_ver"));
				return BCME_UNSUPPORTED;
		}
	}

	return xmtaddress;
}

uint
dhd_sssr_mac_xmtdata(dhd_pub_t *dhdp, uint8 core_idx)
{
	uint xmtdata = 0;
	uint8 num_d11cores;

	num_d11cores = dhd_d11_slices_num_get(dhdp);

	/* SSSR register information structure v0 and v1 shares most except dig_mem */
	if (core_idx < num_d11cores) {
		switch (dhdp->sssr_reg_info->rev2.version) {
			case SSSR_REG_INFO_VER_3 :
				/* intentional fall through */
			case SSSR_REG_INFO_VER_2 :
				xmtdata = dhdp->sssr_reg_info->rev2.
					mac_regs[core_idx].base_regs.xmtdata;
				break;
			case SSSR_REG_INFO_VER_1 :
				xmtdata = dhdp->sssr_reg_info->rev1.
					mac_regs[core_idx].base_regs.xmtdata;
				break;
			case SSSR_REG_INFO_VER_0 :
				xmtdata = dhdp->sssr_reg_info->rev0.
					mac_regs[core_idx].base_regs.xmtdata;
				break;
			default :
				DHD_ERROR(("invalid sssr_reg_ver"));
				return BCME_UNSUPPORTED;
		}
	}

	return xmtdata;
}

#ifdef DHD_SSSR_DUMP_BEFORE_SR
int
dhd_sssr_dump_dig_buf_before(void *dev, const void *user_buf, uint32 len)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
	dhd_pub_t *dhdp = &dhd_info->pub;
	int pos = 0, ret = BCME_ERROR;
	uint dig_buf_size = 0;

	dig_buf_size = dhd_sssr_dig_buf_size(dhdp);

	if (dhdp->sssr_dig_buf_before && (dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		ret = dhd_export_debug_data((char *)dhdp->sssr_dig_buf_before,
			NULL, user_buf, dig_buf_size, &pos);
	}
	return ret;
}

int
dhd_sssr_dump_d11_buf_before(void *dev, const void *user_buf, uint32 len, int core)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
	dhd_pub_t *dhdp = &dhd_info->pub;
	int pos = 0, ret = BCME_ERROR;

	if (dhdp->sssr_d11_before[core] &&
		dhdp->sssr_d11_outofreset[core] &&
		(dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		ret = dhd_export_debug_data((char *)dhdp->sssr_d11_before[core],
			NULL, user_buf, len, &pos);
	}
	return ret;
}
#endif /* DHD_SSSR_DUMP_BEFORE_SR */

int
dhd_sssr_dump_dig_buf_after(void *dev, const void *user_buf, uint32 len)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
	dhd_pub_t *dhdp = &dhd_info->pub;
	int pos = 0, ret = BCME_ERROR;
	uint dig_buf_size = 0;

	dig_buf_size = dhd_sssr_dig_buf_size(dhdp);

	if (dhdp->sssr_dig_buf_after) {
		ret = dhd_export_debug_data((char *)dhdp->sssr_dig_buf_after,
			NULL, user_buf, dig_buf_size, &pos);
	}
	return ret;
}

int
dhd_sssr_dump_d11_buf_after(void *dev, const void *user_buf, uint32 len, int core)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
	dhd_pub_t *dhdp = &dhd_info->pub;
	int pos = 0, ret = BCME_ERROR;

	if (dhdp->sssr_d11_after[core] &&
		dhdp->sssr_d11_outofreset[core]) {
		ret = dhd_export_debug_data((char *)dhdp->sssr_d11_after[core],
			NULL, user_buf, len, &pos);
	}
	return ret;
}

void
dhd_sssr_dump_to_file(dhd_info_t* dhdinfo)
{
	dhd_info_t *dhd = dhdinfo;
	dhd_pub_t *dhdp;
	int i;
#ifdef DHD_SSSR_DUMP_BEFORE_SR
	char before_sr_dump[128];
#endif /* DHD_SSSR_DUMP_BEFORE_SR */
	char after_sr_dump[128];
	unsigned long flags = 0;
	uint dig_buf_size = 0;
	uint8 num_d11cores = 0;
	uint d11_buf_size = 0;

	DHD_ERROR(("%s: ENTER \n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	dhdp = &dhd->pub;

	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_SET_IN_SSSRDUMP(dhdp);
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_GENERAL_UNLOCK(dhdp, flags);
		DHD_ERROR(("%s: bus is down! can't collect sssr dump. \n", __FUNCTION__));
		goto exit;
	}
	DHD_GENERAL_UNLOCK(dhdp, flags);

	num_d11cores = dhd_d11_slices_num_get(dhdp);

	for (i = 0; i < num_d11cores; i++) {
		/* Init file name */
#ifdef DHD_SSSR_DUMP_BEFORE_SR
		memset(before_sr_dump, 0, sizeof(before_sr_dump));
#endif /* DHD_SSSR_DUMP_BEFORE_SR */
		memset(after_sr_dump, 0, sizeof(after_sr_dump));

#ifdef DHD_SSSR_DUMP_BEFORE_SR
		snprintf(before_sr_dump, sizeof(before_sr_dump), "%s_%d_%s",
			"sssr_dump_core", i, "before_SR");
#endif /* DHD_SSSR_DUMP_BEFORE_SR */
		snprintf(after_sr_dump, sizeof(after_sr_dump), "%s_%d_%s",
			"sssr_dump_core", i, "after_SR");

		d11_buf_size = dhd_sssr_mac_buf_size(dhdp, i);

#ifdef DHD_SSSR_DUMP_BEFORE_SR
		if (dhdp->sssr_d11_before[i] && dhdp->sssr_d11_outofreset[i] &&
			(dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
			if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_d11_before[i],
				d11_buf_size, before_sr_dump)) {
				DHD_ERROR(("%s: writing SSSR MAIN dump before to the file failed\n",
					__FUNCTION__));
			}
		}
#endif /* DHD_SSSR_DUMP_BEFORE_SR */

		if (dhdp->sssr_d11_after[i] && dhdp->sssr_d11_outofreset[i]) {
			if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_d11_after[i],
				d11_buf_size, after_sr_dump)) {
				DHD_ERROR(("%s: writing SSSR AUX dump after to the file failed\n",
					__FUNCTION__));
			}
		}
	}

	dig_buf_size = dhd_sssr_dig_buf_size(dhdp);

#ifdef DHD_SSSR_DUMP_BEFORE_SR
	if (dhdp->sssr_dig_buf_before && (dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_dig_buf_before,
			dig_buf_size, "sssr_dump_dig_before_SR")) {
			DHD_ERROR(("%s: writing SSSR Dig dump before to the file failed\n",
				__FUNCTION__));
		}
	}
#endif /* DHD_SSSR_DUMP_BEFORE_SR */

	if (dhdp->sssr_dig_buf_after) {
		if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_dig_buf_after,
			dig_buf_size, "sssr_dump_dig_after_SR")) {
			DHD_ERROR(("%s: writing SSSR Dig VASIP dump after to the file failed\n",
			 __FUNCTION__));
		}
	}

exit:
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_SSSRDUMP(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
}

void
dhd_write_sssr_dump(dhd_pub_t *dhdp, uint32 dump_mode)
{
#if defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	dhdp->sssr_dump_mode = dump_mode;
#endif /* DHD_DUMP_FILE_WRITE_FROM_KERNEL */

	/*
	 * If kernel does not have file write access enabled
	 * then skip writing dumps to files.
	 * The dumps will be pushed to HAL layer which will
	 * write into files
	 */
#if !defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	return;
#else
	/*
	 * dhd_mem_dump -> dhd_sssr_dump -> dhd_write_sssr_dump
	 * Without workqueue -
	 * DUMP_TYPE_DONGLE_INIT_FAILURE/DUMP_TYPE_DUE_TO_BT/DUMP_TYPE_SMMU_FAULT
	 * : These are called in own handler, not in the interrupt context
	 * With workqueue - all other DUMP_TYPEs : dhd_mem_dump is called in workqueue
	 * Thus, it doesn't neeed to dump SSSR in workqueue
	 */
	DHD_ERROR(("%s: writing sssr dump to file... \n", __FUNCTION__));
	dhd_sssr_dump_to_file(dhdp->info);
#endif /* !DHD_DUMP_FILE_WRITE_FROM_KERNEL */
}
#endif /* DHD_SSSR_DUMP */

#ifdef DHD_SDTC_ETB_DUMP
void
dhd_sdtc_etb_dump(dhd_pub_t *dhd)
{
	etb_info_t etb_info;
	uint8 *sdtc_etb_dump;
	uint8 *sdtc_etb_mempool;
	uint etb_dump_len;
	int ret = 0;

	if (!dhd->sdtc_etb_inited) {
		DHD_ERROR(("%s, SDTC ETB dump not supported\n", __FUNCTION__));
		return;
	}

	bzero(&etb_info, sizeof(etb_info));

	if ((ret = dhd_bus_get_etb_info(dhd, dhd->etb_addr_info.etbinfo_addr, &etb_info))) {
		DHD_ERROR(("%s: failed to get etb info %d\n", __FUNCTION__, ret));
		return;
	}

	if (etb_info.read_bytes == 0) {
		DHD_ERROR(("%s ETB is of zero size. Hence donot collect SDTC ETB\n", __FUNCTION__));
		return;
	}

	DHD_ERROR(("%s etb_info ver:%d len:%d rwp:%d etb_full:%d etb:addr:0x%x, len:%d\n",
		__FUNCTION__, etb_info.version, etb_info.len,
		etb_info.read_write_p, etb_info.etb_full,
		etb_info.addr, etb_info.read_bytes));

	/*
	 * etb mempool format = etb_info + etb
	 */
	etb_dump_len = etb_info.read_bytes + sizeof(etb_info);
	if (etb_dump_len > DHD_SDTC_ETB_MEMPOOL_SIZE) {
		DHD_ERROR(("%s etb_dump_len: %d is more than the alloced %d.Hence cannot collect\n",
			__FUNCTION__, etb_dump_len, DHD_SDTC_ETB_MEMPOOL_SIZE));
		return;
	}
	sdtc_etb_mempool = dhd->sdtc_etb_mempool;
	memcpy(sdtc_etb_mempool, &etb_info, sizeof(etb_info));
	sdtc_etb_dump = sdtc_etb_mempool + sizeof(etb_info);
	if ((ret = dhd_bus_get_sdtc_etb(dhd, sdtc_etb_dump, etb_info.addr, etb_info.read_bytes))) {
		DHD_ERROR(("%s: error to get SDTC ETB ret: %d\n", __FUNCTION__, ret));
		return;
	}

	if (write_dump_to_file(dhd, (uint8 *)sdtc_etb_mempool,
		etb_dump_len, "sdtc_etb_dump")) {
		DHD_ERROR(("%s: failed to dump sdtc_etb to file\n",
			__FUNCTION__));
	}
}
#endif /* DHD_SDTC_ETB_DUMP */

#ifdef DHD_LOG_DUMP
static void
dhd_log_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	log_dump_type_t *type = (log_dump_type_t *)event_info;

	if (!dhd || !type) {
		DHD_ERROR(("%s: dhd/type is NULL\n", __FUNCTION__));
		return;
	}

#ifdef WL_CFG80211
	/* flush the fw preserve logs */
	wl_flush_fw_log_buffer(dhd_linux_get_primary_netdev(&dhd->pub),
		FW_LOGSET_MASK_ALL);
#endif

	/* there are currently 3 possible contexts from which
	 * log dump can be scheduled -
	 * 1.TRAP 2.supplicant DEBUG_DUMP pvt driver command
	 * 3.HEALTH CHECK event
	 * The concise debug info buffer is a shared resource
	 * and in case a trap is one of the contexts then both the
	 * scheduled work queues need to run because trap data is
	 * essential for debugging. Hence a mutex lock is acquired
	 * before calling do_dhd_log_dump().
	 */
	DHD_ERROR(("%s: calling log dump.. \n", __FUNCTION__));
	dhd_os_logdump_lock(&dhd->pub);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	if (do_dhd_log_dump(&dhd->pub, type) != BCME_OK) {
		DHD_ERROR(("%s: writing debug dump to the file failed\n", __FUNCTION__));
	}
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_os_logdump_unlock(&dhd->pub);
}

void dhd_schedule_log_dump(dhd_pub_t *dhdp, void *type)
{
	DHD_ERROR(("%s: scheduling log dump.. \n", __FUNCTION__));

	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
		type, DHD_WQ_WORK_DHD_LOG_DUMP,
		dhd_log_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}

static void
dhd_print_buf_addr(dhd_pub_t *dhdp, char *name, void *buf, unsigned int size)
{
#ifdef DHD_FW_COREDUMP
	if ((dhdp->memdump_enabled == DUMP_MEMONLY) ||
		(dhdp->memdump_enabled == DUMP_MEMFILE_BUGON) ||
		(dhdp->memdump_type == DUMP_TYPE_SMMU_FAULT) ||
#ifdef DHD_DETECT_CONSECUTIVE_MFG_HANG
		(dhdp->op_mode & DHD_FLAG_MFG_MODE &&
			(dhdp->hang_count >= MAX_CONSECUTIVE_MFG_HANG_COUNT-1)) ||
#endif /* DHD_DETECT_CONSECUTIVE_MFG_HANG */
		FALSE)
#else
	if (dhdp->memdump_type == DUMP_TYPE_SMMU_FAULT)
#endif
	{
#if defined(CONFIG_ARM64)
		DHD_ERROR(("-------- %s: buf(va)=%llx, buf(pa)=%llx, bufsize=%d\n",
			name, (uint64)buf, (uint64)__virt_to_phys((ulong)buf), size));
#elif defined(__ARM_ARCH_7A__)
		DHD_ERROR(("-------- %s: buf(va)=%x, buf(pa)=%x, bufsize=%d\n",
			name, (uint32)buf, (uint32)__virt_to_phys((ulong)buf), size));
#endif /* __ARM_ARCH_7A__ */
	}
}

static void
dhd_log_dump_buf_addr(dhd_pub_t *dhdp, log_dump_type_t *type)
{
	int i;
	unsigned long wr_size = 0;
	struct dhd_log_dump_buf *dld_buf = &g_dld_buf[0];
	size_t log_size = 0;
	char buf_name[DHD_PRINT_BUF_NAME_LEN];
	dhd_dbg_ring_t *ring = NULL;

	BCM_REFERENCE(ring);

	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		dld_buf = &g_dld_buf[i];
		log_size = (unsigned long)dld_buf->max -
			(unsigned long)dld_buf->buffer;
		if (dld_buf->wraparound) {
			wr_size = log_size;
		} else {
			wr_size = (unsigned long)dld_buf->present -
				(unsigned long)dld_buf->front;
		}
		scnprintf(buf_name, sizeof(buf_name), "dlb_buf[%d]", i);
		dhd_print_buf_addr(dhdp, buf_name, dld_buf, dld_buf_size[i]);
		scnprintf(buf_name, sizeof(buf_name), "dlb_buf[%d] buffer", i);
		dhd_print_buf_addr(dhdp, buf_name, dld_buf->buffer, wr_size);
		scnprintf(buf_name, sizeof(buf_name), "dlb_buf[%d] present", i);
		dhd_print_buf_addr(dhdp, buf_name, dld_buf->present, wr_size);
		scnprintf(buf_name, sizeof(buf_name), "dlb_buf[%d] front", i);
		dhd_print_buf_addr(dhdp, buf_name, dld_buf->front, wr_size);
	}

#ifdef DEBUGABILITY_ECNTRS_LOGGING
	/* periodic flushing of ecounters is NOT supported */
	if (*type == DLD_BUF_TYPE_ALL &&
			logdump_ecntr_enable &&
			dhdp->ecntr_dbg_ring) {

		ring = (dhd_dbg_ring_t *)dhdp->ecntr_dbg_ring;
		dhd_print_buf_addr(dhdp, "ecntr_dbg_ring", ring, LOG_DUMP_ECNTRS_MAX_BUFSIZE);
		dhd_print_buf_addr(dhdp, "ecntr_dbg_ring ring_buf", ring->ring_buf,
				LOG_DUMP_ECNTRS_MAX_BUFSIZE);
	}
#endif /* DEBUGABILITY_ECNTRS_LOGGING */

#if defined(BCMPCIE)
	if (dhdp->dongle_trap_occured && dhdp->extended_trap_data) {
		dhd_print_buf_addr(dhdp, "extended_trap_data", dhdp->extended_trap_data,
				BCMPCIE_EXT_TRAP_DATA_MAXLEN);
	}
#endif /* BCMPCIE */

#if defined(DHD_FW_COREDUMP) && defined(DNGL_EVENT_SUPPORT)
	/* if health check event was received */
	if (dhdp->memdump_type == DUMP_TYPE_DONGLE_HOST_EVENT) {
		dhd_print_buf_addr(dhdp, "health_chk_event_data", dhdp->health_chk_event_data,
				HEALTH_CHK_BUF_SIZE);
	}
#endif /* DHD_FW_COREDUMP && DNGL_EVENT_SUPPORT */

	/* append the concise debug information */
	if (dhdp->concise_dbg_buf) {
		dhd_print_buf_addr(dhdp, "concise_dbg_buf", dhdp->concise_dbg_buf,
				CONCISE_DUMP_BUFLEN);
	}
}

#ifdef CUSTOMER_HW4_DEBUG
static void
dhd_log_dump_print_to_kmsg(char *bufptr, unsigned long len)
{
	char tmp_buf[DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE + 1];
	char *end = NULL;
	unsigned long plen = 0;

	if (!bufptr || !len)
		return;

	memset(tmp_buf, 0, DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE);
	end = bufptr + len;
	while (bufptr < end) {
		if ((bufptr + DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE) < end) {
			memcpy(tmp_buf, bufptr, DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE);
			tmp_buf[DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE] = '\0';
			printf("%s", tmp_buf);
			bufptr += DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE;
		} else {
			plen = (unsigned long)end - (unsigned long)bufptr;
			memcpy(tmp_buf, bufptr, plen);
			tmp_buf[plen] = '\0';
			printf("%s", tmp_buf);
			bufptr += plen;
		}
	}
}

static void
dhd_log_dump_print_tail(dhd_pub_t *dhdp,
		struct dhd_log_dump_buf *dld_buf,
		uint tail_len)
{
	char *flush_ptr1 = NULL, *flush_ptr2 = NULL;
	unsigned long len_flush1 = 0, len_flush2 = 0;
	unsigned long flags = 0;

	/* need to hold the lock before accessing 'present' and 'remain' ptrs */
	DHD_LOG_DUMP_BUF_LOCK(&dld_buf->lock, flags);
	flush_ptr1 = dld_buf->present - tail_len;
	if (flush_ptr1 >= dld_buf->front) {
		/* tail content is within the buffer */
		flush_ptr2 = NULL;
		len_flush1 = tail_len;
	} else if (dld_buf->wraparound) {
		/* tail content spans the buffer length i.e, wrap around */
		flush_ptr1 = dld_buf->front;
		len_flush1 = (unsigned long)dld_buf->present - (unsigned long)flush_ptr1;
		len_flush2 = (unsigned long)tail_len - len_flush1;
		flush_ptr2 = (char *)((unsigned long)dld_buf->max -
			(unsigned long)len_flush2);
	} else {
		/* amt of logs in buffer is less than tail size */
		flush_ptr1 = dld_buf->front;
		flush_ptr2 = NULL;
		len_flush1 = (unsigned long)dld_buf->present - (unsigned long)dld_buf->front;
	}
	DHD_LOG_DUMP_BUF_UNLOCK(&dld_buf->lock, flags);

	printf("\n================= LOG_DUMP tail =================\n");
	if (flush_ptr2) {
		dhd_log_dump_print_to_kmsg(flush_ptr2, len_flush2);
	}
	dhd_log_dump_print_to_kmsg(flush_ptr1, len_flush1);
	printf("\n===================================================\n");
}
#endif /* CUSTOMER_HW4_DEBUG */

#ifdef DHD_SSSR_DUMP
int
dhdpcie_sssr_dump_get_before_after_len(dhd_pub_t *dhd, uint32 *arr_len)
{
	int i = 0;
	uint dig_buf_size = 0;

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* core 0 */
	i = 0;
#ifdef DHD_SSSR_DUMP_BEFORE_SR
	if (dhd->sssr_d11_before[i] && dhd->sssr_d11_outofreset[i] &&
		(dhd->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {

		arr_len[SSSR_C0_D11_BEFORE]  = dhd_sssr_mac_buf_size(dhd, i);
		DHD_ERROR(("%s: arr_len[SSSR_C0_D11_BEFORE] : %d\n", __FUNCTION__,
			arr_len[SSSR_C0_D11_BEFORE]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C0_D11_BEFORE",
			dhd->sssr_d11_before[i], arr_len[SSSR_C0_D11_BEFORE]);
#endif /* DHD_LOG_DUMP */
	}
#endif /* DHD_SSSR_DUMP_BEFORE_SR */
	if (dhd->sssr_d11_after[i] && dhd->sssr_d11_outofreset[i]) {
		arr_len[SSSR_C0_D11_AFTER]  = dhd_sssr_mac_buf_size(dhd, i);
		DHD_ERROR(("%s: arr_len[SSSR_C0_D11_AFTER] : %d\n", __FUNCTION__,
			arr_len[SSSR_C0_D11_AFTER]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C0_D11_AFTER",
			dhd->sssr_d11_after[i], arr_len[SSSR_C0_D11_AFTER]);
#endif /* DHD_LOG_DUMP */
	}

	/* core 1 */
	i = 1;
#ifdef DHD_SSSR_DUMP_BEFORE_SR
	if (dhd->sssr_d11_before[i] && dhd->sssr_d11_outofreset[i] &&
		(dhd->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		arr_len[SSSR_C1_D11_BEFORE]  = dhd_sssr_mac_buf_size(dhd, i);
		DHD_ERROR(("%s: arr_len[SSSR_C1_D11_BEFORE] : %d\n", __FUNCTION__,
			arr_len[SSSR_C1_D11_BEFORE]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C1_D11_BEFORE",
			dhd->sssr_d11_before[i], arr_len[SSSR_C1_D11_BEFORE]);
#endif /* DHD_LOG_DUMP */
	}
#endif /* DHD_SSSR_DUMP_BEFORE_SR */
	if (dhd->sssr_d11_after[i] && dhd->sssr_d11_outofreset[i]) {
		arr_len[SSSR_C1_D11_AFTER]  = dhd_sssr_mac_buf_size(dhd, i);
		DHD_ERROR(("%s: arr_len[SSSR_C1_D11_AFTER] : %d\n", __FUNCTION__,
			arr_len[SSSR_C1_D11_AFTER]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C1_D11_AFTER",
			dhd->sssr_d11_after[i], arr_len[SSSR_C1_D11_AFTER]);
#endif /* DHD_LOG_DUMP */
	}

	/* core 2 scan core */
	if (dhd->sssr_reg_info->rev2.version >= SSSR_REG_INFO_VER_2) {
		i = 2;
#ifdef DHD_SSSR_DUMP_BEFORE_SR
		if (dhd->sssr_d11_before[i] && dhd->sssr_d11_outofreset[i] &&
			(dhd->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
			arr_len[SSSR_C2_D11_BEFORE]  = dhd_sssr_mac_buf_size(dhd, i);
			DHD_ERROR(("%s: arr_len[SSSR_C2_D11_BEFORE] : %d\n", __FUNCTION__,
				arr_len[SSSR_C2_D11_BEFORE]));
#ifdef DHD_LOG_DUMP
			dhd_print_buf_addr(dhd, "SSSR_C2_D11_BEFORE",
				dhd->sssr_d11_before[i], arr_len[SSSR_C2_D11_BEFORE]);
#endif /* DHD_LOG_DUMP */
		}
#endif /* DHD_SSSR_DUMP_BEFORE_SR */
		if (dhd->sssr_d11_after[i] && dhd->sssr_d11_outofreset[i]) {
			arr_len[SSSR_C2_D11_AFTER]  = dhd_sssr_mac_buf_size(dhd, i);
			DHD_ERROR(("%s: arr_len[SSSR_C2_D11_AFTER] : %d\n", __FUNCTION__,
				arr_len[SSSR_C2_D11_AFTER]));
#ifdef DHD_LOG_DUMP
			dhd_print_buf_addr(dhd, "SSSR_C2_D11_AFTER",
				dhd->sssr_d11_after[i], arr_len[SSSR_C2_D11_AFTER]);
#endif /* DHD_LOG_DUMP */
		}
	}

	/* DIG core or VASIP */
	dig_buf_size = dhd_sssr_dig_buf_size(dhd);
#ifdef DHD_SSSR_DUMP_BEFORE_SR
	arr_len[SSSR_DIG_BEFORE] = (dhd->sssr_dig_buf_before) ? dig_buf_size : 0;
	DHD_ERROR(("%s: arr_len[SSSR_DIG_BEFORE] : %d\n", __FUNCTION__,
		arr_len[SSSR_DIG_BEFORE]));
#ifdef DHD_LOG_DUMP
		if (dhd->sssr_dig_buf_before) {
			dhd_print_buf_addr(dhd, "SSSR_DIG_BEFORE",
				dhd->sssr_dig_buf_before, arr_len[SSSR_DIG_BEFORE]);
		}
#endif /* DHD_LOG_DUMP */
#endif /* DHD_SSSR_DUMP_BEFORE_SR */

	arr_len[SSSR_DIG_AFTER] = (dhd->sssr_dig_buf_after) ? dig_buf_size : 0;
	DHD_ERROR(("%s: arr_len[SSSR_DIG_AFTER] : %d\n", __FUNCTION__,
		arr_len[SSSR_DIG_AFTER]));
#ifdef DHD_LOG_DUMP
	if (dhd->sssr_dig_buf_after) {
		dhd_print_buf_addr(dhd, "SSSR_DIG_AFTER",
			dhd->sssr_dig_buf_after, arr_len[SSSR_DIG_AFTER]);
	}
#endif /* DHD_LOG_DUMP */

	return BCME_OK;
}

void
dhd_nla_put_sssr_dump_len(void *ndev, uint32 *arr_len)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
	dhd_pub_t *dhdp = &dhd_info->pub;

	if (dhdp->sssr_dump_collected) {
		dhdpcie_sssr_dump_get_before_after_len(dhdp, arr_len);
	}
}
#endif /* DHD_SSSR_DUMP */

uint32
dhd_get_time_str_len()
{
	char *ts = NULL, time_str[128];

	ts = dhd_log_dump_get_timestamp();
	snprintf(time_str, sizeof(time_str),
			"\n\n ========== LOG DUMP TAKEN AT : %s =========\n", ts);
	return strlen(time_str);
}

#if defined(BCMPCIE)
uint32
dhd_get_ext_trap_len(void *ndev, dhd_pub_t *dhdp)
{
	int length = 0;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->extended_trap_data) {
		length = (strlen(EXT_TRAP_LOG_HDR)
					+ sizeof(sec_hdr) + BCMPCIE_EXT_TRAP_DATA_MAXLEN);
	}
	return length;
}
#endif /* BCMPCIE */

#if defined(DHD_FW_COREDUMP) && defined(DNGL_EVENT_SUPPORT)
uint32
dhd_get_health_chk_len(void *ndev, dhd_pub_t *dhdp)
{
	int length = 0;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->memdump_type == DUMP_TYPE_DONGLE_HOST_EVENT) {
		length = (strlen(HEALTH_CHK_LOG_HDR)
			+ sizeof(sec_hdr) + HEALTH_CHK_BUF_SIZE);
	}
	return length;
}
#endif /* DHD_FW_COREDUMP && DNGL_EVENT_SUPPORT */

uint32
dhd_get_dhd_dump_len(void *ndev, dhd_pub_t *dhdp)
{
	uint32 length = 0;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;
	int remain_len = 0;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->concise_dbg_buf) {
		remain_len = dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
		 if (remain_len <= 0 || remain_len >= CONCISE_DUMP_BUFLEN) {
			DHD_ERROR(("%s: error getting concise debug info ! remain_len: %d\n",
				__FUNCTION__, remain_len));
			return length;
		}

		length += (uint32)(CONCISE_DUMP_BUFLEN - remain_len);
	}

	length += (uint32)(strlen(DHD_DUMP_LOG_HDR) + sizeof(sec_hdr));
	return length;
}

uint32
dhd_get_cookie_log_len(void *ndev, dhd_pub_t *dhdp)
{
	int length = 0;
	dhd_info_t *dhd_info;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->logdump_cookie && dhd_logdump_cookie_count(dhdp) > 0) {
		length = dhd_log_dump_cookie_len(dhdp);
	}
	return length;

}

#ifdef DHD_DUMP_PCIE_RINGS
uint32
dhd_get_flowring_len(void *ndev, dhd_pub_t *dhdp)
{
	uint32 length = 0;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;
	uint16 h2d_flowrings_total;
	int remain_len = 0;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->concise_dbg_buf) {
		remain_len = dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
		if (remain_len <= 0 || remain_len >= CONCISE_DUMP_BUFLEN) {
			DHD_ERROR(("%s: error getting concise debug info ! remain_len: %d\n",
				__FUNCTION__, remain_len));
		   return length;
		}

		length += (uint32)(CONCISE_DUMP_BUFLEN - remain_len);
	}

	length += (uint32) strlen(FLOWRING_DUMP_HDR);
	length += (uint32) sizeof(sec_hdr);
	h2d_flowrings_total = dhd_get_max_flow_rings(dhdp);
	length += ((D2HRING_TXCMPLT_ITEMSIZE * D2HRING_TXCMPLT_MAX_ITEM)
				+ (H2DRING_RXPOST_ITEMSIZE * H2DRING_RXPOST_MAX_ITEM)
				+ (D2HRING_RXCMPLT_ITEMSIZE * D2HRING_RXCMPLT_MAX_ITEM)
				+ (H2DRING_CTRL_SUB_ITEMSIZE * H2DRING_CTRL_SUB_MAX_ITEM)
				+ (D2HRING_CTRL_CMPLT_ITEMSIZE * D2HRING_CTRL_CMPLT_MAX_ITEM)
#ifdef EWP_EDL
				+ (D2HRING_EDL_HDR_SIZE * D2HRING_EDL_MAX_ITEM));
#else
				+ (H2DRING_INFO_BUFPOST_ITEMSIZE * H2DRING_DYNAMIC_INFO_MAX_ITEM)
				+ (D2HRING_INFO_BUFCMPLT_ITEMSIZE * D2HRING_DYNAMIC_INFO_MAX_ITEM));
#endif /* EWP_EDL */

#if defined(DHD_HTPUT_TUNABLES)
	/* flowring lengths are different for HTPUT rings, handle accordingly */
	length += ((H2DRING_TXPOST_ITEMSIZE * dhd_prot_get_h2d_htput_max_txpost(dhdp) *
		HTPUT_TOTAL_FLOW_RINGS) +
		(H2DRING_TXPOST_ITEMSIZE * dhd_prot_get_h2d_max_txpost(dhdp) *
		(h2d_flowrings_total - HTPUT_TOTAL_FLOW_RINGS)));
#else
	length += (H2DRING_TXPOST_ITEMSIZE * dhd_prot_get_h2d_max_txpost(dhdp) *
		h2d_flowrings_total);
#endif /* DHD_HTPUT_TUNABLES */

	return length;
}
#endif /* DHD_DUMP_PCIE_RINGS */

#ifdef EWP_ECNTRS_LOGGING
uint32
dhd_get_ecntrs_len(void *ndev, dhd_pub_t *dhdp)
{
	dhd_info_t *dhd_info;
	log_dump_section_hdr_t sec_hdr;
	int length = 0;
	dhd_dbg_ring_t *ring;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (logdump_ecntr_enable && dhdp->ecntr_dbg_ring) {
		ring = (dhd_dbg_ring_t *)dhdp->ecntr_dbg_ring;
		length = ring->ring_size + strlen(ECNTRS_LOG_HDR) + sizeof(sec_hdr);
	}
	return length;
}
#endif /* EWP_ECNTRS_LOGGING */

int
dhd_get_dld_log_dump(void *dev, dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, int type, void *pos)
{
	int ret = BCME_OK;
	struct dhd_log_dump_buf *dld_buf;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;

	dld_buf = &g_dld_buf[type];

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	} else if (!dhdp) {
		return BCME_ERROR;
	}

	DHD_ERROR(("%s: ENTER \n", __FUNCTION__));

	dhd_init_sec_hdr(&sec_hdr);

	/* write the section header first */
	ret = dhd_export_debug_data(dld_hdrs[type].hdr_str, fp, user_buf,
		strlen(dld_hdrs[type].hdr_str), pos);
	if (ret < 0)
		goto exit;
	len -= (uint32)strlen(dld_hdrs[type].hdr_str);
	len -= (uint32)sizeof(sec_hdr);
	sec_hdr.type = dld_hdrs[type].sec_type;
	sec_hdr.length = len;
	ret = dhd_export_debug_data((char *)&sec_hdr, fp, user_buf, sizeof(sec_hdr), pos);
	if (ret < 0)
		goto exit;
	ret = dhd_export_debug_data(dld_buf->buffer, fp, user_buf, len, pos);
	if (ret < 0)
		goto exit;

exit:
	return ret;
}

static int
dhd_log_flush(dhd_pub_t *dhdp, log_dump_type_t *type)
{
	unsigned long flags = 0;
#ifdef EWP_EDL
	int i = 0;
#endif /* EWP_EDL */
	dhd_info_t *dhd_info = NULL;

	BCM_REFERENCE(dhd_info);

	/* if dhdp is null, its extremely unlikely that log dump will be scheduled
	 * so not freeing 'type' here is ok, even if we want to free 'type'
	 * we cannot do so, since 'dhdp->osh' is unavailable
	 * as dhdp is null
	 */
	if (!dhdp || !type) {
		if (dhdp) {
			DHD_GENERAL_LOCK(dhdp, flags);
			DHD_BUS_BUSY_CLEAR_IN_LOGDUMP(dhdp);
			dhd_os_busbusy_wake(dhdp);
			DHD_GENERAL_UNLOCK(dhdp, flags);
		}
		return BCME_ERROR;
	}

#if defined(BCMPCIE)
	if (dhd_bus_get_linkdown(dhdp)) {
		/* As link is down donot collect any data over PCIe.
		 * Also return BCME_OK to caller, so that caller can
		 * dump all the outstanding data to file
		 */
		return BCME_OK;
	}
#endif /* BCMPCIE */

	dhd_info = (dhd_info_t *)dhdp->info;
	/* in case of trap get preserve logs from ETD */
#if defined(BCMPCIE) && defined(EWP_ETD_PRSRV_LOGS)
	if (dhdp->dongle_trap_occured &&
			dhdp->extended_trap_data) {
		dhdpcie_get_etd_preserve_logs(dhdp, (uint8 *)dhdp->extended_trap_data,
				&dhd_info->event_data);
	}
#endif /* BCMPCIE */

	/* flush the event work items to get any fw events/logs
	 * flush_work is a blocking call
	 */
#ifdef SHOW_LOGTRACE
#ifdef EWP_EDL
	if (dhd_info->pub.dongle_edl_support) {
		/* wait till existing edl items are processed */
		dhd_flush_logtrace_process(dhd_info);
		/* dhd_flush_logtrace_process will ensure the work items in the ring
		* (EDL ring) from rd to wr are processed. But if wr had
		* wrapped around, only the work items from rd to ring-end are processed.
		* So to ensure that the work items at the
		* beginning of ring are also processed in the wrap around case, call
		* it twice
		*/
		for (i = 0; i < 2; i++) {
			/* blocks till the edl items are processed */
			dhd_flush_logtrace_process(dhd_info);
		}
	} else {
		dhd_flush_logtrace_process(dhd_info);
	}
#else
	dhd_flush_logtrace_process(dhd_info);
#endif /* EWP_EDL */
#endif /* SHOW_LOGTRACE */

#ifdef CUSTOMER_HW4_DEBUG
	/* print last 'x' KB of preserve buffer data to kmsg console
	* this is to address cases where debug_dump is not
	* available for debugging
	*/
	dhd_log_dump_print_tail(dhdp,
		&g_dld_buf[DLD_BUF_TYPE_PRESERVE], logdump_prsrv_tailsize);
#endif /* CUSTOMER_HW4_DEBUG */
	return BCME_OK;
}

int
dhd_get_debug_dump_file_name(void *dev, dhd_pub_t *dhdp, char *dump_path, int size)
{
	int ret;
	int len = 0;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	memset(dump_path, 0, size);

	ret = snprintf(dump_path, size, "%s",
			DHD_COMMON_DUMP_PATH DHD_DEBUG_DUMP_TYPE);
	len += ret;

	/* Keep the same timestamp across different dump logs */
	if (!dhdp->logdump_periodic_flush) {
		struct rtc_time tm;
		clear_debug_dump_time(dhdp->debug_dump_time_str);
		get_debug_dump_time(dhdp->debug_dump_time_str);
		sscanf(dhdp->debug_dump_time_str, DHD_LOG_DUMP_TS_FMT_YYMMDDHHMMSS,
			&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
			&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
		ret = snprintf(dump_path + len, size - len, "_" DHD_LOG_DUMP_TS_FMT_YYMMDDHHMMSS,
				tm.tm_year, tm.tm_mon, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
		len += ret;
	}

	ret = 0;
	switch (dhdp->debug_dump_subcmd) {
	case CMD_UNWANTED:
		ret = snprintf(dump_path + len, size - len, "%s", DHD_DUMP_SUBSTR_UNWANTED);
		break;
	case CMD_DISCONNECTED:
		ret = snprintf(dump_path + len, size - len, "%s", DHD_DUMP_SUBSTR_DISCONNECTED);
		break;
	default:
		break;
	}
	len += ret;

	return BCME_OK;
}

uint32
dhd_get_dld_len(int log_type)
{
	unsigned long wr_size = 0;
	unsigned long buf_size = 0;
	unsigned long flags = 0;
	struct dhd_log_dump_buf *dld_buf;
	log_dump_section_hdr_t sec_hdr;

	/* calculate the length of the log */
	dld_buf = &g_dld_buf[log_type];
	buf_size = (unsigned long)dld_buf->max -
			(unsigned long)dld_buf->buffer;

	if (dld_buf->wraparound) {
		wr_size = buf_size;
	} else {
		/* need to hold the lock before accessing 'present' and 'remain' ptrs */
		DHD_LOG_DUMP_BUF_LOCK(&dld_buf->lock, flags);
		wr_size = (unsigned long)dld_buf->present -
				(unsigned long)dld_buf->front;
		DHD_LOG_DUMP_BUF_UNLOCK(&dld_buf->lock, flags);
	}
	return (wr_size + sizeof(sec_hdr) + strlen(dld_hdrs[log_type].hdr_str));
}

static void
dhd_get_time_str(dhd_pub_t *dhdp, char *time_str, int size)
{
	char *ts = NULL;
	memset(time_str, 0, size);
	ts = dhd_log_dump_get_timestamp();
	snprintf(time_str, size,
			"\n\n ========== LOG DUMP TAKEN AT : %s =========\n", ts);
}

int
dhd_print_time_str(const void *user_buf, void *fp, uint32 len, void *pos)
{
	char *ts = NULL;
	int ret = 0;
	char time_str[128];

	memset_s(time_str, sizeof(time_str), 0, sizeof(time_str));
	ts = dhd_log_dump_get_timestamp();
	snprintf(time_str, sizeof(time_str),
			"\n\n ========== LOG DUMP TAKEN AT : %s =========\n", ts);

	/* write the timestamp hdr to the file first */
	ret = dhd_export_debug_data(time_str, fp, user_buf, strlen(time_str), pos);
	if (ret < 0) {
		DHD_ERROR(("write file error, err = %d\n", ret));
	}
	return ret;
}

#if defined(DHD_FW_COREDUMP) && defined(DNGL_EVENT_SUPPORT)
int
dhd_print_health_chk_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, void *pos)
{
	int ret = BCME_OK;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	if (dhdp->memdump_type == DUMP_TYPE_DONGLE_HOST_EVENT) {
		/* write the section header first */
		ret = dhd_export_debug_data(HEALTH_CHK_LOG_HDR, fp, user_buf,
			strlen(HEALTH_CHK_LOG_HDR), pos);
		if (ret < 0)
			goto exit;

		len -= (uint32)strlen(HEALTH_CHK_LOG_HDR);
		sec_hdr.type = LOG_DUMP_SECTION_HEALTH_CHK;
		sec_hdr.length = HEALTH_CHK_BUF_SIZE;
		ret = dhd_export_debug_data((char *)&sec_hdr, fp, user_buf, sizeof(sec_hdr), pos);
		if (ret < 0)
			goto exit;

		len -= (uint32)sizeof(sec_hdr);
		/* write the log */
		ret = dhd_export_debug_data((char *)dhdp->health_chk_event_data, fp,
			user_buf, len, pos);
		if (ret < 0)
			goto exit;
	}
exit:
	return ret;
}
#endif /* DHD_FW_COREDUMP && DNGL_EVENT_SUPPORT */

#if defined(BCMPCIE)
int
dhd_print_ext_trap_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, void *pos)
{
	int ret = BCME_OK;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	/* append extended trap data to the file in case of traps */
	if (dhdp->dongle_trap_occured &&
			dhdp->extended_trap_data) {
		/* write the section header first */
		ret = dhd_export_debug_data(EXT_TRAP_LOG_HDR, fp, user_buf,
			strlen(EXT_TRAP_LOG_HDR), pos);
		if (ret < 0)
			goto exit;

		len -= (uint32)strlen(EXT_TRAP_LOG_HDR);
		sec_hdr.type = LOG_DUMP_SECTION_EXT_TRAP;
		sec_hdr.length = BCMPCIE_EXT_TRAP_DATA_MAXLEN;
		ret = dhd_export_debug_data((uint8 *)&sec_hdr, fp, user_buf, sizeof(sec_hdr), pos);
		if (ret < 0)
			goto exit;

		len -= (uint32)sizeof(sec_hdr);
		/* write the log */
		ret = dhd_export_debug_data((uint8 *)dhdp->extended_trap_data, fp,
			user_buf, len, pos);
		if (ret < 0)
			goto exit;
	}
exit:
	return ret;
}
#endif /* BCMPCIE */

int
dhd_print_dump_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, void *pos)
{
	int ret = BCME_OK;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	ret = dhd_export_debug_data(DHD_DUMP_LOG_HDR, fp, user_buf, strlen(DHD_DUMP_LOG_HDR), pos);
	if (ret < 0)
		goto exit;

	len -= (uint32)strlen(DHD_DUMP_LOG_HDR);
	sec_hdr.type = LOG_DUMP_SECTION_DHD_DUMP;
	sec_hdr.length = len;
	ret = dhd_export_debug_data((char *)&sec_hdr, fp, user_buf, sizeof(sec_hdr), pos);
	if (ret < 0)
		goto exit;

	len -= (uint32)sizeof(sec_hdr);

	if (dhdp->concise_dbg_buf) {
		dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
		ret = dhd_export_debug_data(dhdp->concise_dbg_buf, fp, user_buf, len, pos);
		if (ret < 0)
			goto exit;
	}

exit:
	return ret;
}

int
dhd_print_cookie_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, void *pos)
{
	int ret = BCME_OK;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	if (dhdp->logdump_cookie && dhd_logdump_cookie_count(dhdp) > 0) {
		ret = dhd_log_dump_cookie_to_file(dhdp, fp, user_buf, (unsigned long *)pos);
	}
	return ret;
}

#ifdef DHD_DUMP_PCIE_RINGS
int
dhd_print_flowring_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
		void *fp, uint32 len, void *pos)
{
	log_dump_section_hdr_t sec_hdr;
	int ret = BCME_OK;
	int remain_len = 0;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	remain_len = dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
	if (remain_len <= 0 || remain_len >= CONCISE_DUMP_BUFLEN) {
		DHD_ERROR(("%s: error getting concise debug info !\n",
			__FUNCTION__));
	   return BCME_ERROR;
	}
	memset(dhdp->concise_dbg_buf, 0, CONCISE_DUMP_BUFLEN);

	/* write the section header first */
	ret = dhd_export_debug_data(FLOWRING_DUMP_HDR, fp, user_buf,
		strlen(FLOWRING_DUMP_HDR), pos);
	if (ret < 0)
		goto exit;

	/* Write the ring summary */
	ret = dhd_export_debug_data(dhdp->concise_dbg_buf, fp, user_buf,
		(CONCISE_DUMP_BUFLEN - remain_len), pos);
	if (ret < 0)
		goto exit;

	sec_hdr.type = LOG_DUMP_SECTION_FLOWRING;
	sec_hdr.length = len;
	ret = dhd_export_debug_data((char *)&sec_hdr, fp, user_buf, sizeof(sec_hdr), pos);
	if (ret < 0)
		goto exit;

	/* write the log */
	ret = dhd_d2h_h2d_ring_dump(dhdp, fp, user_buf, (unsigned long *)pos, TRUE);
	if (ret < 0)
		goto exit;

exit:
	return ret;
}
#endif /* DHD_DUMP_PCIE_RINGS */

#ifdef EWP_ECNTRS_LOGGING
int
dhd_print_ecntrs_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
		void *fp, uint32 len, void *pos)
{
	log_dump_section_hdr_t sec_hdr;
	int ret = BCME_OK;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	if (logdump_ecntr_enable &&
			dhdp->ecntr_dbg_ring) {
		sec_hdr.type = LOG_DUMP_SECTION_ECNTRS;
		ret = dhd_dump_debug_ring(dhdp, dhdp->ecntr_dbg_ring,
				user_buf, &sec_hdr, ECNTRS_LOG_HDR, len, LOG_DUMP_SECTION_ECNTRS);
	}
	return ret;

}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
int
dhd_print_rtt_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
		void *fp, uint32 len, void *pos)
{
	log_dump_section_hdr_t sec_hdr;
	int ret = BCME_OK;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	if (logdump_rtt_enable && dhdp->rtt_dbg_ring) {
		ret = dhd_dump_debug_ring(dhdp, dhdp->rtt_dbg_ring,
				user_buf, &sec_hdr, RTT_LOG_HDR, len, LOG_DUMP_SECTION_RTT);
	}
	return ret;

}
#endif /* EWP_RTT_LOGGING */

#ifdef DHD_STATUS_LOGGING
int
dhd_print_status_log_data(void *dev, dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, void *pos)
{
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp) {
		return BCME_ERROR;
	}

	return dhd_statlog_write_logdump(dhdp, user_buf, fp, len, pos);
}

uint32
dhd_get_status_log_len(void *ndev, dhd_pub_t *dhdp)
{
	dhd_info_t *dhd_info;
	uint32 length = 0;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (dhdp) {
		length = dhd_statlog_get_logbuf_len(dhdp);
	}

	return length;
}
#endif /* DHD_STATUS_LOGGING */

void
dhd_init_sec_hdr(log_dump_section_hdr_t *sec_hdr)
{
	/* prep the section header */
	memset(sec_hdr, 0, sizeof(*sec_hdr));
	sec_hdr->magic = LOG_DUMP_MAGIC;
	sec_hdr->timestamp = local_clock();
}

/* Must hold 'dhd_os_logdump_lock' before calling this function ! */
static int
do_dhd_log_dump(dhd_pub_t *dhdp, log_dump_type_t *type)
{
	int ret = 0, i = 0;
	struct file *fp = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t old_fs;
	struct kstat stat;
#endif
	loff_t pos = 0;
	char dump_path[128];
	uint32 file_mode;
	unsigned long flags = 0;
	size_t log_size = 0;
	size_t fspace_remain = 0;
	char time_str[128];
	unsigned int len = 0;
	log_dump_section_hdr_t sec_hdr;
	uint32 file_size = 0;

	DHD_ERROR(("%s: ENTER \n", __FUNCTION__));

	DHD_GENERAL_LOCK(dhdp, flags);
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_GENERAL_UNLOCK(dhdp, flags);
		DHD_ERROR(("%s: bus is down! can't collect log dump. \n", __FUNCTION__));
		goto exit1;
	}
	DHD_BUS_BUSY_SET_IN_LOGDUMP(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);

	if ((ret = dhd_log_flush(dhdp, type)) < 0) {
		goto exit1;
	}
	/* change to KERNEL_DS address limit */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	dhd_get_debug_dump_file_name(NULL, dhdp, dump_path, sizeof(dump_path));

	DHD_ERROR(("debug_dump_path = %s\n", dump_path));
	DHD_ERROR(("DHD version: %s\n", dhd_version));
	DHD_ERROR(("F/W version: %s\n", fw_version));

	dhd_log_dump_buf_addr(dhdp, type);

	dhd_get_time_str(dhdp, time_str, 128);

	/* if this is the first time after dhd is loaded,
	 * or, if periodic flush is disabled, clear the log file
	 */
	if (!dhdp->logdump_periodic_flush || dhdp->last_file_posn == 0)
		file_mode = O_CREAT | O_WRONLY | O_SYNC | O_TRUNC;
	else
		file_mode = O_CREAT | O_RDWR | O_SYNC;

	fp = filp_open(dump_path, file_mode, 0664);
	if (IS_ERR(fp)) {
		/* If android installed image, try '/data' directory */
#if defined(CONFIG_X86) && defined(OEM_ANDROID)
		DHD_ERROR(("%s: File open error on Installed android image, trying /data...\n",
			__FUNCTION__));
		snprintf(dump_path, sizeof(dump_path), "/data/" DHD_DEBUG_DUMP_TYPE);
		if (!dhdp->logdump_periodic_flush) {
			snprintf(dump_path + strlen(dump_path),
				sizeof(dump_path) - strlen(dump_path),
				"_%s", dhdp->debug_dump_time_str);
		}
		fp = filp_open(dump_path, file_mode, 0664);
		if (IS_ERR(fp)) {
			ret = PTR_ERR(fp);
			DHD_ERROR(("open file error, err = %d\n", ret));
			goto exit2;
		}
		DHD_ERROR(("debug_dump_path = %s\n", dump_path));
#else
		ret = PTR_ERR(fp);
		DHD_ERROR(("open file error, err = %d\n", ret));
		goto exit2;
#endif /* CONFIG_X86 && OEM_ANDROID */
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	ret = vfs_stat(dump_path, &stat);
	if (ret < 0) {
		DHD_ERROR(("file stat error, err = %d\n", ret));
		goto exit2;
	}
	file_size = stat.size;
#else
	file_size = dhd_os_get_image_size(fp);
	if (file_size <= 0) {
		DHD_ERROR(("%s: get file size fails ! %d\n", __FUNCTION__, file_size));
		goto exit2;
	}
#endif

	/* if some one else has changed the file */
	if (dhdp->last_file_posn != 0 &&
			file_size < dhdp->last_file_posn) {
		dhdp->last_file_posn = 0;
	}

	/* XXX: periodic flush is disabled by default, if enabled
	 * only periodic flushing of 'GENERAL' log dump buffer
	 * is supported, its not recommended to turn on periodic
	 * flushing, except for developer unit test.
	 */
	if (dhdp->logdump_periodic_flush) {
		log_size = strlen(time_str) + strlen(DHD_DUMP_LOG_HDR) + sizeof(sec_hdr);
		/* calculate the amount of space required to dump all logs */
		for (i = 0; i < DLD_BUFFER_NUM; ++i) {
			if (*type != DLD_BUF_TYPE_ALL && i != *type)
				continue;

			if (g_dld_buf[i].wraparound) {
				log_size += (unsigned long)g_dld_buf[i].max
						- (unsigned long)g_dld_buf[i].buffer;
			} else {
				DHD_LOG_DUMP_BUF_LOCK(&g_dld_buf[i].lock, flags);
				log_size += (unsigned long)g_dld_buf[i].present -
						(unsigned long)g_dld_buf[i].front;
				DHD_LOG_DUMP_BUF_UNLOCK(&g_dld_buf[i].lock, flags);
			}
			log_size += strlen(dld_hdrs[i].hdr_str) + sizeof(sec_hdr);

			if (*type != DLD_BUF_TYPE_ALL && i == *type)
				break;
		}

		ret = generic_file_llseek(fp, dhdp->last_file_posn, SEEK_CUR);
		if (ret < 0) {
			DHD_ERROR(("file seek last posn error ! err = %d \n", ret));
			goto exit2;
		}
		pos = fp->f_pos;

		/* if the max file size is reached, wrap around to beginning of the file
		 * we're treating the file as a large ring buffer
		 */
		fspace_remain = logdump_max_filesize - pos;
		if (log_size > fspace_remain) {
			fp->f_pos -= pos;
			pos = fp->f_pos;
		}
	}

	dhd_print_time_str(0, fp, len, &pos);

	for (i = 0; i < DLD_BUFFER_NUM; ++i) {

		if (*type != DLD_BUF_TYPE_ALL && i != *type)
			continue;

		len = dhd_get_dld_len(i);
		dhd_get_dld_log_dump(NULL, dhdp, 0, fp, len, i, &pos);
		if (*type != DLD_BUF_TYPE_ALL)
			break;
	}

#ifdef EWP_ECNTRS_LOGGING
	if (*type == DLD_BUF_TYPE_ALL &&
			logdump_ecntr_enable &&
			dhdp->ecntr_dbg_ring) {
		dhd_log_dump_ring_to_file(dhdp, dhdp->ecntr_dbg_ring,
				fp, (unsigned long *)&pos,
				&sec_hdr, ECNTRS_LOG_HDR, LOG_DUMP_SECTION_ECNTRS);
	}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef DHD_STATUS_LOGGING
	if (dhdp->statlog) {
		/* write the statlog */
		len = dhd_get_status_log_len(NULL, dhdp);
		if (len) {
			if (dhd_print_status_log_data(NULL, dhdp, 0, fp,
				len, &pos) < 0) {
				goto exit2;
			}
		}
	}
#endif /* DHD_STATUS_LOGGING */

#ifdef DHD_STATUS_LOGGING
	if (dhdp->statlog) {
		dhd_print_buf_addr(dhdp, "statlog_logbuf", dhd_statlog_get_logbuf(dhdp),
			dhd_statlog_get_logbuf_len(dhdp));
	}
#endif /* DHD_STATUS_LOGGING */

#ifdef EWP_RTT_LOGGING
	if (*type == DLD_BUF_TYPE_ALL &&
			logdump_rtt_enable &&
			dhdp->rtt_dbg_ring) {
		dhd_log_dump_ring_to_file(dhdp, dhdp->rtt_dbg_ring,
				fp, (unsigned long *)&pos,
				&sec_hdr, RTT_LOG_HDR, LOG_DUMP_SECTION_RTT);
	}
#endif /* EWP_RTT_LOGGING */

#ifdef EWP_BCM_TRACE
	if (*type == DLD_BUF_TYPE_ALL &&
		dhdp->bcm_trace_dbg_ring) {
		dhd_log_dump_ring_to_file(dhdp, dhdp->bcm_trace_dbg_ring,
				fp, (unsigned long *)&pos,
				&sec_hdr, BCM_TRACE_LOG_HDR, LOG_DUMP_SECTION_BCM_TRACE);
	}
#endif /* EWP_BCM_TRACE */

#ifdef BCMPCIE
	len = dhd_get_ext_trap_len(NULL, dhdp);
	if (len) {
		if (dhd_print_ext_trap_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}
#endif /* BCMPCIE */

#if defined(DHD_FW_COREDUMP) && defined (DNGL_EVENT_SUPPORT)
	len = dhd_get_health_chk_len(NULL, dhdp);
	if (len) {
		if (dhd_print_health_chk_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}
#endif /* DHD_FW_COREDUMP && DNGL_EVENT_SUPPORT */

	len = dhd_get_dhd_dump_len(NULL, dhdp);
	if (len) {
		if (dhd_print_dump_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}

	len = dhd_get_cookie_log_len(NULL, dhdp);
	if (len) {
		if (dhd_print_cookie_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}

#ifdef DHD_DUMP_PCIE_RINGS
	len = dhd_get_flowring_len(NULL, dhdp);
	if (len) {
		if (dhd_print_flowring_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}
#endif

	if (dhdp->logdump_periodic_flush) {
		/* store the last position written to in the file for future use */
		dhdp->last_file_posn = pos;
	}

exit2:
	if (!IS_ERR(fp) && fp != NULL) {
		filp_close(fp, NULL);
		DHD_ERROR(("%s: Finished writing log dump to file - '%s' \n",
				__FUNCTION__, dump_path));
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(old_fs);
#endif
exit1:
	if (type) {
		MFREE(dhdp->osh, type, sizeof(*type));
	}
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_LOGDUMP(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);

#ifdef DHD_DUMP_MNGR
	if (ret >= 0) {
		dhd_dump_file_manage_enqueue(dhdp, dump_path, DHD_DEBUG_DUMP_TYPE);
	}
#endif /* DHD_DUMP_MNGR */

	return (ret < 0) ? BCME_ERROR : BCME_OK;
}
#endif /* DHD_LOG_DUMP */

/* This function writes data to the file pointed by fp, OR
 * copies data to the user buffer sent by upper layer(HAL).
 */
int
dhd_export_debug_data(void *mem_buf, void *fp, const void *user_buf, uint32 buf_len, void *pos)
{
	int ret = BCME_OK;

	if (fp) {
		ret = vfs_write(fp, mem_buf, buf_len, (loff_t *)pos);
		if (ret < 0) {
			DHD_ERROR(("write file error, err = %d\n", ret));
			goto exit;
		}
	} else {
#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
		if (in_compat_syscall())
#else
		if (is_compat_task())
#endif /* LINUX_VER >= 4.6 */
		{
			void * usr_ptr =  compat_ptr((uintptr_t) user_buf);
			ret = copy_to_user((void *)((uintptr_t)usr_ptr + (*(int *)pos)),
				mem_buf, buf_len);
			if (ret) {
				DHD_ERROR(("failed to copy into user buffer : %d\n", ret));
				goto exit;
			}
		}
		else
#endif /* CONFIG_COMPAT */
		{
			ret = copy_to_user((void *)((uintptr_t)user_buf + (*(int *)pos)),
				mem_buf, buf_len);
			if (ret) {
				DHD_ERROR(("failed to copy into user buffer : %d\n", ret));
				goto exit;
			}
		}
		(*(int *)pos) += buf_len;
	}
exit:
	return ret;
}

#ifdef BCM_ROUTER_DHD
void dhd_schedule_trap_log_dump(dhd_pub_t *dhdp,
	uint8 *buf, uint32 size)
{
	dhd_write_file_t *wf = NULL;
	wf = (dhd_write_file_t *)MALLOC(dhdp->osh, sizeof(dhd_write_file_t));
	if (wf == NULL) {
		DHD_ERROR(("%s: dhd write file memory allocation failed\n", __FUNCTION__));
		return;
	}
	snprintf(wf->file_path, sizeof(wf->file_path), "%s", "/tmp/failed_if.txt");
	wf->file_flags = O_CREAT | O_WRONLY | O_SYNC;
	wf->buf = buf;
	wf->bufsize = size;
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)wf,
		DHD_WQ_WORK_INFORM_DHD_MON, dhd_inform_dhd_monitor_handler,
		DHD_WQ_WORK_PRIORITY_HIGH);
}

/* Returns the pid of a the userspace process running with the given name */
static struct task_struct *
_get_task_info(const char *pname)
{
	struct task_struct *task;
	if (!pname)
		return NULL;

	for_each_process(task) {
		if (strcmp(pname, task->comm) == 0)
			return task;
	}

	return NULL;
}

#define DHD_MONITOR_NS	"dhd_monitor"
extern void emergency_restart(void);

static void
dhd_inform_dhd_monitor_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_write_file_t *wf = event_info;
	struct task_struct *monitor_task;
	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}
	if (!event_info) {
		DHD_ERROR(("%s: File info is NULL\n", __FUNCTION__));
		return;
	}
	if (!wf->buf) {
		DHD_ERROR(("%s: Unable to get failed interface name\n", __FUNCTION__));
		goto exit;
	}
	if (write_file(wf->file_path, wf->file_flags, wf->buf, wf->bufsize)) {
		DHD_ERROR(("%s: writing to the file failed\n", __FUNCTION__));
	}
exit:
	MFREE(dhd->pub.osh, wf, sizeof(dhd_write_file_t));

	/* check if dhd_monitor is running */
	monitor_task = _get_task_info(DHD_MONITOR_NS);
	if (monitor_task == NULL) {
		/* If dhd_monitor is not running, handle recovery from here */

		char *val = nvram_get("watchdog");
		if (val && bcm_atoi(val)) {
			/* watchdog enabled, so reboot */
			DHD_ERROR(("%s: Dongle(wl%d) trap detected. Restarting the system\n",
				__FUNCTION__, dhd->unit));

			mdelay(1000);
			emergency_restart();
			while (1)
				cpu_relax();
		} else {
			DHD_ERROR(("%s: Dongle(wl%d) trap detected. No watchdog.\n",
			   __FUNCTION__, dhd->unit));
		}

		return;
	}

	/* If monitor daemon is running, let's signal the monitor for recovery */
	DHD_ERROR(("%s: Dongle(wl%d) trap detected. Send signal to dhd_monitor.\n",
		__FUNCTION__, dhd->unit));

	send_sig_info(SIGUSR1, (void *)1L, monitor_task);
}
#endif /* BCM_ROUTER_DHD */

#ifdef BCMDBG
#define DUMPMAC_BUF_SZ (128 * 1024)
#define DUMPMAC_FILENAME_SZ 32

static void
_dhd_schedule_macdbg_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_pub_t *dhdp = &dhd->pub;
#ifndef BCM_ROUTER_DHD
	char *dumpbuf = NULL;
	int dumpbuf_len = 0;
	uint16 dump_signature;
	char dumpfilename[DUMPMAC_FILENAME_SZ] = {0, };
#endif /* BCM_ROUTER_DHD */

	ASSERT(event == DHD_WQ_WORK_MACDBG);
	BCM_REFERENCE(event_info);

	DHD_ERROR(("%s: Dongle(wl%d) macreg dump scheduled\n",
		__FUNCTION__, dhd->unit));

	DHD_OS_WAKE_LOCK(dhdp);

	/* Make sure dongle stops running to avoid race condition in reading mac registers */
	(void) dhd_wl_ioctl_set_intiovar(dhdp, "bus:disconnect", 99, WLC_SET_VAR, TRUE, 0);

	/* In router, skip macregs dump as dhd_monitor will dump them */
#ifndef BCM_ROUTER_DHD
	dumpbuf = (char *)MALLOCZ(dhdp->osh, DUMPMAC_BUF_SZ);
	if (dumpbuf) {
		/* Write macdump to a file */

		/* Get dump file signature */
		dump_signature = (uint16)OSL_RAND();

		/* PSMr */
		if (dhd_macdbg_dumpmac(dhdp, dumpbuf, DUMPMAC_BUF_SZ,
			&dumpbuf_len, FALSE) == BCME_OK) {
			snprintf(dumpfilename, DUMPMAC_FILENAME_SZ,
				"/tmp/d11reg_dump_%04X.txt", dump_signature);
			DHD_ERROR(("%s: PSMr macreg dump to %s\n", __FUNCTION__, dumpfilename));
			/* Write to a file */
			if (write_file(dumpfilename, (O_CREAT | O_WRONLY | O_SYNC),
				dumpbuf, dumpbuf_len)) {
				DHD_ERROR(("%s: writing mac dump to the file failed\n",
					__FUNCTION__));
			}
			memset(dumpbuf, 0, DUMPMAC_BUF_SZ);
			memset(dumpfilename, 0, DUMPMAC_FILENAME_SZ);
			dumpbuf_len = 0;
		}

		/* PSMx */
		if (dhd_macdbg_dumpmac(dhdp, dumpbuf, DUMPMAC_BUF_SZ,
			&dumpbuf_len, TRUE) == BCME_OK) {
			snprintf(dumpfilename, DUMPMAC_FILENAME_SZ,
				"/tmp/d11regx_dump_%04X.txt", dump_signature);
			DHD_ERROR(("%s: PSMx macreg dump to %s\n", __FUNCTION__, dumpfilename));
			/* Write to a file */
			if (write_file(dumpfilename, (O_CREAT | O_WRONLY | O_SYNC),
				dumpbuf, dumpbuf_len)) {
				DHD_ERROR(("%s: writing mac dump to the file failed\n",
					__FUNCTION__));
			}
			memset(dumpbuf, 0, DUMPMAC_BUF_SZ);
			memset(dumpfilename, 0, DUMPMAC_FILENAME_SZ);
			dumpbuf_len = 0;
		}

		/* SVMP */
		if (dhd_macdbg_dumpsvmp(dhdp, dumpbuf, DUMPMAC_BUF_SZ,
			&dumpbuf_len) == BCME_OK) {
			snprintf(dumpfilename, DUMPMAC_FILENAME_SZ,
				"/tmp/svmp_dump_%04X.txt", dump_signature);
			DHD_ERROR(("%s: SVMP mems dump to %s\n", __FUNCTION__, dumpfilename));
			/* Write to a file */
			if (write_file(dumpfilename, (O_CREAT | O_WRONLY | O_SYNC),
				dumpbuf, dumpbuf_len)) {
				DHD_ERROR(("%s: writing svmp dump to the file failed\n",
					__FUNCTION__));
			}
			memset(dumpbuf, 0, DUMPMAC_BUF_SZ);
			memset(dumpfilename, 0, DUMPMAC_FILENAME_SZ);
			dumpbuf_len = 0;
		}

		MFREE(dhdp->osh, dumpbuf, DUMPMAC_BUF_SZ);
	} else {
		DHD_ERROR(("%s: print macdump\n", __FUNCTION__));
		/* Just printf the dumps */
		(void) dhd_macdbg_dumpmac(dhdp, NULL, 0, NULL, FALSE); /* PSMr */
		(void) dhd_macdbg_dumpmac(dhdp, NULL, 0, NULL, TRUE); /* PSMx */
		(void) dhd_macdbg_dumpsvmp(dhdp, NULL, 0, NULL);
	}
#endif /* BCM_ROUTER_DHD */

	DHD_OS_WAKE_UNLOCK(dhdp);
	dhd_deferred_work_set_skip(dhd->dhd_deferred_wq,
		DHD_WQ_WORK_MACDBG, FALSE);
}

void
dhd_schedule_macdbg_dump(dhd_pub_t *dhdp)
{
	DHD_ERROR(("%s: Dongle(wl%d) schedule macreg dump\n",
		__FUNCTION__, dhdp->info->unit));

	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, NULL,
		DHD_WQ_WORK_MACDBG, _dhd_schedule_macdbg_dump, DHD_WQ_WORK_PRIORITY_LOW);
	dhd_deferred_work_set_skip(dhdp->info->dhd_deferred_wq,
		DHD_WQ_WORK_MACDBG, TRUE);
}
#endif /* BCMDBG */

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
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(dhdp, TRUE, __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */
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
			*buf = dhdp->soc_ram;
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

#ifdef EWP_RTT_LOGGING
uint32
dhd_get_rtt_len(void *ndev, dhd_pub_t *dhdp)
{
	dhd_info_t *dhd_info;
	log_dump_section_hdr_t sec_hdr;
	int length = 0;
	dhd_dbg_ring_t *ring;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (logdump_rtt_enable && dhdp->rtt_dbg_ring) {
		ring = (dhd_dbg_ring_t *)dhdp->rtt_dbg_ring;
		length = ring->ring_size + strlen(RTT_LOG_HDR) + sizeof(sec_hdr);
	}
	return length;
}
#endif /* EWP_RTT_LOGGING */

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

	bzero(*buf, size);
	if (dhd_ver) {
		strlcpy(*buf, dhd_version, size);
	} else {
		strlcpy(*buf, fw_str, size);
	}
	return BCME_OK;
}

#ifdef DHD_PKT_LOGGING
int
dhd_os_get_pktlog_dump(void *dev, const void *user_buf, uint32 len)
{
	int ret = BCME_OK;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;
	if (user_buf == NULL) {
		DHD_ERROR(("%s(): user buffer is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	ret = dhd_pktlog_dump_write_memory(dhdp, user_buf, len);
	if (ret < 0) {
		DHD_ERROR(("%s(): fail to dump pktlog, err = %d\n", __FUNCTION__, ret));
		return ret;
	}
	return ret;
}

uint32
dhd_os_get_pktlog_dump_size(struct net_device *dev)
{
	uint32 size = 0;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;

	size = dhd_pktlog_get_dump_length(dhdp);
	if (size == 0) {
		DHD_ERROR(("%s(): fail to get pktlog size, err = %d\n", __FUNCTION__, size));
	}
	return size;
}

void
dhd_os_get_pktlogdump_filename(struct net_device *dev, char *dump_path, int len)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;

	dhd_pktlog_get_filename(dhdp, dump_path, len);
}
#endif /* DHD_PKT_LOGGING */
#ifdef DNGL_AXI_ERROR_LOGGING
int
dhd_os_get_axi_error_dump(void *dev, const void *user_buf, uint32 len)
{
	int ret = BCME_OK;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;
	loff_t pos = 0;
	if (user_buf == NULL) {
		DHD_ERROR(("%s(): user buffer is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	ret = dhd_export_debug_data((char *)dhdp->axi_err_dump,
			NULL, user_buf, sizeof(dhd_axi_error_dump_t), &pos);

	if (ret < 0) {
		DHD_ERROR(("%s(): fail to dump pktlog, err = %d\n", __FUNCTION__, ret));
		return ret;
	}
	return ret;
}

int
dhd_os_get_axi_error_dump_size(struct net_device *dev)
{
	int size = -1;

	size = sizeof(dhd_axi_error_dump_t);
	if (size < 0) {
		DHD_ERROR(("%s(): fail to get axi error size, err = %d\n", __FUNCTION__, size));
	}
	return size;
}

void
dhd_os_get_axi_error_filename(struct net_device *dev, char *dump_path, int len)
{
	snprintf(dump_path, len, "%s",
		DHD_COMMON_DUMP_PATH DHD_DUMP_AXI_ERROR_FILENAME);
}
#endif /* DNGL_AXI_ERROR_LOGGING */
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

#if defined(BCM_ROUTER_DHD)
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
#endif /* BCM_ROUTER_DHD */

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

int dhd_get_block_tdls_status(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	return ifp->block_tdls;
}

int dhd_set_block_tdls_status(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	ifp->block_tdls = val;

	return BCME_OK;
}
#endif /* DHD_L2_FILTER */

#if defined(SET_XPS_CPUS)
int dhd_xps_cpus_enable(struct net_device *net, int enable)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp;
	int ifidx;
	char * XPS_CPU_SETBUF;

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	if (!dhd->pub.conf->xps_cpus)
		return -ENODEV;

	if (ifidx == PRIMARY_INF) {
		if (dhd->pub.op_mode == DHD_FLAG_IBSS_MODE) {
			DHD_INFO(("%s : set for IBSS.\n", __FUNCTION__));
			XPS_CPU_SETBUF = RPS_CPUS_MASK_IBSS;
		} else {
			DHD_INFO(("%s : set for BSS.\n", __FUNCTION__));
			XPS_CPU_SETBUF = RPS_CPUS_MASK;
		}
	} else if (ifidx == VIRTUAL_INF) {
		DHD_INFO(("%s : set for P2P.\n", __FUNCTION__));
		XPS_CPU_SETBUF = RPS_CPUS_MASK_P2P;
	} else {
		DHD_ERROR(("%s : Invalid index : %d.\n", __FUNCTION__, ifidx));
		return -EINVAL;
	}

	ifp = dhd->iflist[ifidx];
	if (ifp) {
		if (enable) {
			DHD_INFO(("%s : set xps_cpus as [%s]\n", __FUNCTION__, XPS_CPU_SETBUF));
			custom_xps_map_set(ifp->net, XPS_CPU_SETBUF, strlen(XPS_CPU_SETBUF));
		} else {
			custom_xps_map_clear(ifp->net);
		}
	} else {
		DHD_ERROR(("%s : ifp is NULL!!\n", __FUNCTION__));
		return -ENODEV;
	}
	return BCME_OK;
}

int custom_xps_map_set(struct net_device *net, char *buf, size_t len)
{
	cpumask_var_t mask;
	int err;

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

	err = netif_set_xps_queue(net, mask, 0);

	free_cpumask_var(mask);

	if (0 == err)
		WL_MSG(net->name, "Done. mapping cpu\n");

	return err;
}

void custom_xps_map_clear(struct net_device *net)
{
    struct xps_dev_maps *dev_maps;

	DHD_INFO(("%s : Entered.\n", __FUNCTION__));

    rcu_read_lock();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	dev_maps = rcu_dereference(net->xps_cpus_map);
#else
    dev_maps = rcu_dereference(net->xps_maps);
#endif
    rcu_read_unlock();

	if (dev_maps) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
		RCU_INIT_POINTER(net->xps_cpus_map, NULL);
#else
		RCU_INIT_POINTER(net->xps_maps, NULL);
#endif
		kfree_rcu(dev_maps, rcu);
		DHD_INFO(("%s : xps_cpus map clear.\n", __FUNCTION__));
	}
}
#endif // endif

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

	if (!dhd->pub.conf->rps_cpus)
		return -ENODEV;

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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0))
		static_key_slow_inc(&rps_needed.key);
#else
		static_key_slow_inc(&rps_needed);
#endif
	}
	if (old_map) {
		kfree_rcu(old_map, rcu);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0))
		static_key_slow_dec(&rps_needed.key);
#else
		static_key_slow_dec(&rps_needed);
#endif
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
#endif // endif

#ifdef DHD_BUZZZ_LOG_ENABLED

static int
dhd_buzzz_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;

	DAEMONIZE("dhd_buzzz");

	/*  signal: thread has started */
	complete(&tsk->completed);

	/* Run until signal received */
	while (1) {
		if (down_interruptible(&tsk->sema) == 0) {
			if (tsk->terminated) {
				break;
			}
			printk("%s: start to dump...\n", __FUNCTION__);
			dhd_buzzz_dump();
		} else {
			break;
		}
	}
	complete_and_exit(&tsk->completed, 0);
}

void* dhd_os_create_buzzz_thread(void)
{
	tsk_ctl_t *thr_buzzz_ctl = NULL;

	thr_buzzz_ctl = kmalloc(sizeof(tsk_ctl_t), GFP_KERNEL);
	if (!thr_buzzz_ctl) {
		return NULL;
	}

	PROC_START(dhd_buzzz_thread, NULL, thr_buzzz_ctl, 0, "dhd_buzzz");

	return (void *)thr_buzzz_ctl;
}

void dhd_os_destroy_buzzz_thread(void *thr_hdl)
{
	tsk_ctl_t *thr_buzzz_ctl = (tsk_ctl_t *)thr_hdl;

	if (!thr_buzzz_ctl) {
		return;
	}

	PROC_STOP(thr_buzzz_ctl);
	kfree(thr_buzzz_ctl);
}

void dhd_os_sched_buzzz_thread(void *thr_hdl)
{
	tsk_ctl_t *thr_buzzz_ctl = (tsk_ctl_t *)thr_hdl;

	if (!thr_buzzz_ctl) {
		return;
	}

	if (thr_buzzz_ctl->thr_pid >= 0) {
		up(&thr_buzzz_ctl->sema);
	}
}
#endif /* DHD_BUZZZ_LOG_ENABLED */

#ifdef DHD_DEBUG_PAGEALLOC
/* XXX Additional Kernel implemenation is needed to use this function at
 * the top of the check_poison_mem() function in mm/debug-pagealloc.c file.
 * Please check if below codes are implemenated your Linux Kernel first.
 *
 * - mm/debug-pagealloc.c
 *
 * // for DHD_DEBUG_PAGEALLOC
 * typedef void (*page_corrupt_cb_t)(void *handle, void *addr_corrupt, uint addr_len);
 * page_corrupt_cb_t corrupt_cb = NULL;
 * void *corrupt_cb_handle = NULL;
 *
 * void register_page_corrupt_cb(page_corrupt_cb_t cb, void *handle)
 * {
 *      corrupt_cb = cb;
 *      corrupt_cb_handle = handle;
 * }
 * EXPORT_SYMBOL(register_page_corrupt_cb);
 *
 * extern void dhd_page_corrupt_cb(void *handle, void *addr_corrupt, size_t len);
 *
 * static void check_poison_mem(unsigned char *mem, size_t bytes)
 * {
 * ......
 *
 *      if (!__ratelimit(&ratelimit))
 *               return;
 *      else if (start == end && single_bit_flip(*start, PAGE_POISON))
 *              printk(KERN_ERR "pagealloc: single bit error\n");
 *      else
 *              printk(KERN_ERR "pagealloc: memory corruption\n");
 *
 *      print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1, start,
 *              end - start + 1, 1);
 *
 *      // for DHD_DEBUG_PAGEALLOC
 *      dhd_page_corrupt_cb(corrupt_cb_handle, start, end - start + 1);
 *
 *      dump_stack();
 * }
 *
 */

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
#ifdef OEM_ANDROID
	/* XXX Send HANG event to Android Framework for recovery */
	dhdp->hang_reason = HANG_REASON_PCIE_PKTID_ERROR;
	dhd_os_check_hang(dhdp, 0, -EREMOTEIO);
#endif /* OEM_ANDROID */
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

#ifdef DHD_PKTTS
/**
 * dhd_msgbuf_get_ipv6_id - return ipv6 identification number
 * return 0 in case of error
 *
 * @pkt: packet pointer
 */
uint
dhd_msgbuf_get_ipv6_id(void *pkt)
{
	struct frag_hdr _frag;
	const struct sk_buff *skb;
	const struct frag_hdr *fh;
	unsigned int offset = 0;
	int err;

	skb = (struct sk_buff *)pkt;
	err = ipv6_find_hdr(skb, &offset, NEXTHDR_FRAGMENT, NULL, NULL);
	if (err < 0) {
		return 0;
	}

	fh = skb_header_pointer(skb, offset, sizeof(_frag), &_frag);
	if (fh == NULL) {
		return 0;
	}

	return ntohl(fh->identification);
}

/**
 * dhd_create_to_notifier_ts - create BCM_NL_TS netlink socket
 *
 * @void:
 */
int
dhd_create_to_notifier_ts(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	/* Kernel 3.6 onwards this API accepts only 3 arguments. */
	nl_to_ts = netlink_kernel_create(&init_net, BCM_NL_TS, &dhd_netlink_ts);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) */
	if (!nl_to_ts)	{
		DHD_ERROR(("Error creating ts socket.\n"));
		return -1;
	}
	DHD_INFO(("nl_to socket created successfully...\n"));
	return 0;
}

/**
 * dhd_destroy_to_notifier_ts - destroy BCM_NL_TS netlink socket
 *
 * @void:
 */
void
dhd_destroy_to_notifier_ts(void)
{
	DHD_INFO(("Destroying nl_to_ts socket\n"));
	if (nl_to_ts) {
		netlink_kernel_release(nl_to_ts);
		nl_to_ts = NULL;
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
/**
 * dhd_recv_msg_from_ts - this is called on BCM_NL_TS netlink recv message
 * this api updates app pid of app which is currenty using this netlink socket
 *
 * @skb: rx packet socket buffer
 */
static void
dhd_recv_msg_from_ts(struct sk_buff *skb)
{
	sender_pid_ts = ((struct nlmsghdr *)(skb->data))->nlmsg_pid;
	DHD_INFO(("DHD Daemon Started, PID:%d\n", sender_pid_ts));
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) */

/**
 * dhd_send_msg_to_ts - send data to BCM_NL_TS netlink socket
 *
 * @skb: socket buffer (unused)
 * @data: output data
 * @size: size of output data
 */
int
dhd_send_msg_to_ts(struct sk_buff *skb, void *data, int size)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out = NULL;
	int ret = BCME_ERROR;

	BCM_REFERENCE(skb);
	if (sender_pid_ts == 0) {
		goto err;
	}

	if ((skb_out = nlmsg_new(size, GFP_ATOMIC)) == NULL) {
		DHD_ERROR(("%s: skb alloc failed\n", __FUNCTION__));
		goto err;
	}

	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, size, 0);
	if (nlh == NULL) {
		DHD_ERROR(("%s: nlmsg_put failed\n", __FUNCTION__));
		goto err;
	}

	NETLINK_CB(skb_out).dst_group = 0; /* Unicast */
	(void)memcpy_s(nlmsg_data(nlh), size, (char *)data, size);

	if ((ret = nlmsg_unicast(nl_to_ts, skb_out, sender_pid_ts)) < 0) {
		DHD_ERROR(("Error sending message, ret:%d\n", ret));
		/* skb is already freed inside nlmsg_unicast() on error case */
		/* explicitly making skb_out to NULL to avoid double free */
		skb_out = NULL;
		goto err;
	}
	return BCME_OK;

err:
	if (skb_out) {
		nlmsg_free(skb_out);
	}
	return ret;
}
#endif /* DHD_PKTTS */

static int
dhd_create_to_notifier_skt(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	/* Kernel 3.7 onwards this API accepts only 3 arguments. */
	/* Kernel version 3.6 is a special case which accepts 4 arguments */
	nl_to_event_sk = netlink_kernel_create(&init_net, BCM_NL_USER, &dhd_netlink_cfg);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	/* Kernel version 3.5 and below use this old API format */
	nl_to_event_sk = netlink_kernel_create(&init_net, BCM_NL_USER, 0,
			dhd_process_daemon_msg, NULL, THIS_MODULE);
#else
	nl_to_event_sk = netlink_kernel_create(&init_net, BCM_NL_USER, THIS_MODULE,
			&dhd_netlink_cfg);
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
	netlink_kernel_release(nl_to_event_sk);
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
	int ret = BCME_ERROR;

	BCM_REFERENCE(skb);
	if (sender_pid == 0) {
		DHD_INFO(("Invalid PID 0\n"));
		skb_out = NULL;
		goto err;
	}

	if ((skb_out = nlmsg_new(size, 0)) == NULL) {
		DHD_ERROR(("%s: skb alloc failed\n", __FUNCTION__));
		ret = BCME_NOMEM;
		goto err;
	}
	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, size, 0);
	if (nlh == NULL) {
		DHD_ERROR(("%s: nlmsg_put failed\n", __FUNCTION__));
		goto err;
	}
	NETLINK_CB(skb_out).dst_group = 0; /* Unicast */
	(void)memcpy_s(nlmsg_data(nlh), size, (char *)data, size);

	if ((ret = nlmsg_unicast(nl_to_event_sk, skb_out, sender_pid)) < 0) {
		DHD_ERROR(("Error sending message, ret:%d\n", ret));
		/* skb is already freed inside nlmsg_unicast() on error case */
		/* explicitly making skb_out to NULL to avoid double free */
		skb_out = NULL;
		goto err;
	}
	return BCME_OK;
err:
	if (skb_out) {
		nlmsg_free(skb_out);
	}
	return ret;
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
			trap = DO_TRAP;
			break;
		case DHD_REASON_SCAN_TO:
			to_reason = REASON_SCAN_TO;
			trap = DO_TRAP;
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

char*
dhd_dbg_get_system_timestamp(void)
{
	static char timebuf[DEBUG_DUMP_TIME_BUF_LEN];
	struct osl_timespec tv;
	unsigned long local_time;
	struct rtc_time tm;

	memset_s(timebuf, DEBUG_DUMP_TIME_BUF_LEN, 0, DEBUG_DUMP_TIME_BUF_LEN);
	osl_do_gettimeofday(&tv);
	local_time = (u32)(tv.tv_sec - (sys_tz.tz_minuteswest * 60));
	rtc_time_to_tm(local_time, &tm);
	scnprintf(timebuf, DEBUG_DUMP_TIME_BUF_LEN,
			"%02d:%02d:%02d.%06lu",
			tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
	return timebuf;
}

char*
dhd_log_dump_get_timestamp(void)
{
	static char buf[32];
	u64 ts_nsec;
	unsigned long rem_nsec;

	ts_nsec = local_clock();
	rem_nsec = DIV_AND_MOD_U64_BY_U32(ts_nsec, NSEC_PER_SEC);
	snprintf(buf, sizeof(buf), "%5lu.%06lu",
		(unsigned long)ts_nsec, rem_nsec / NSEC_PER_USEC);

	return buf;
}

#ifdef DHD_LOG_DUMP
bool
dhd_log_dump_ecntr_enabled(void)
{
	return (bool)logdump_ecntr_enable;
}

bool
dhd_log_dump_rtt_enabled(void)
{
	return (bool)logdump_rtt_enable;
}

void
dhd_log_dump_init(dhd_pub_t *dhd)
{
	struct dhd_log_dump_buf *dld_buf, *dld_buf_special;
	int i = 0;
	uint8 *prealloc_buf = NULL, *bufptr = NULL;
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
	int prealloc_idx = DHD_PREALLOC_DHD_LOG_DUMP_BUF;
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
	int ret;
	dhd_dbg_ring_t *ring = NULL;
	unsigned long flags = 0;
	dhd_info_t *dhd_info = dhd->info;
#if defined(EWP_ECNTRS_LOGGING)
	void *cookie_buf = NULL;
#endif

	BCM_REFERENCE(ret);
	BCM_REFERENCE(ring);
	BCM_REFERENCE(flags);

	/* sanity check */
	if (logdump_prsrv_tailsize <= 0 ||
		logdump_prsrv_tailsize > DHD_LOG_DUMP_MAX_TAIL_FLUSH_SIZE) {
		logdump_prsrv_tailsize = DHD_LOG_DUMP_MAX_TAIL_FLUSH_SIZE;
	}
	/* now adjust the preserve log flush size based on the
	* kernel printk log buffer size
	*/
#ifdef CONFIG_LOG_BUF_SHIFT
	DHD_ERROR(("%s: kernel log buf size = %uKB; logdump_prsrv_tailsize = %uKB;"
		" limit prsrv tail size to = %uKB\n",
		__FUNCTION__, (1 << CONFIG_LOG_BUF_SHIFT)/1024,
		logdump_prsrv_tailsize/1024, LOG_DUMP_KERNEL_TAIL_FLUSH_SIZE/1024));

	if (logdump_prsrv_tailsize > LOG_DUMP_KERNEL_TAIL_FLUSH_SIZE) {
		logdump_prsrv_tailsize = LOG_DUMP_KERNEL_TAIL_FLUSH_SIZE;
	}
#else
	DHD_ERROR(("%s: logdump_prsrv_tailsize = %uKB \n",
		__FUNCTION__, logdump_prsrv_tailsize/1024);
#endif /* CONFIG_LOG_BUF_SHIFT */

	mutex_init(&dhd_info->logdump_lock);
	/* initialize log dump buf structures */
	memset(g_dld_buf, 0, sizeof(struct dhd_log_dump_buf) * DLD_BUFFER_NUM);

	/* set the log dump buffer size based on the module_param */
	if (logdump_max_bufsize > LOG_DUMP_GENERAL_MAX_BUFSIZE ||
			logdump_max_bufsize <= 0)
		dld_buf_size[DLD_BUF_TYPE_GENERAL] = LOG_DUMP_GENERAL_MAX_BUFSIZE;
	else
		dld_buf_size[DLD_BUF_TYPE_GENERAL] = logdump_max_bufsize;

	/* pre-alloc the memory for the log buffers & 'special' buffer */
	dld_buf_special = &g_dld_buf[DLD_BUF_TYPE_SPECIAL];
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
	prealloc_buf = DHD_OS_PREALLOC(dhd, prealloc_idx++, LOG_DUMP_TOTAL_BUFSIZE);
	dld_buf_special->buffer = DHD_OS_PREALLOC(dhd, prealloc_idx++,
			dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
#else
	prealloc_buf = MALLOCZ(dhd->osh, LOG_DUMP_TOTAL_BUFSIZE);
	dld_buf_special->buffer = MALLOCZ(dhd->osh, dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */

	if (!prealloc_buf) {
		DHD_ERROR(("Failed to allocate memory for log buffers\n"));
		goto fail;
	}
	if (!dld_buf_special->buffer) {
		DHD_ERROR(("Failed to allocate memory for special buffer\n"));
		goto fail;
	}
#ifdef BCMINTERNAL
	DHD_ERROR(("prealloc_buf:%p dld_buf_special->buffer:%p\n",
		prealloc_buf, dld_buf_special->buffer));
#endif /* BCMINTERNAL */

	bufptr = prealloc_buf;
	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		dld_buf = &g_dld_buf[i];
		dld_buf->dhd_pub = dhd;
		spin_lock_init(&dld_buf->lock);
		dld_buf->wraparound = 0;
		if (i != DLD_BUF_TYPE_SPECIAL) {
			dld_buf->buffer = bufptr;
			dld_buf->max = (unsigned long)dld_buf->buffer + dld_buf_size[i];
			bufptr = (uint8 *)dld_buf->max;
		} else {
			dld_buf->max = (unsigned long)dld_buf->buffer + dld_buf_size[i];
		}
		dld_buf->present = dld_buf->front = dld_buf->buffer;
		dld_buf->remain = dld_buf_size[i];
		dld_buf->enable = 1;
	}

	/* now use the rest of the pre-alloc'd memory for other rings */
#ifdef EWP_ECNTRS_LOGGING
	dhd->ecntr_dbg_ring = dhd_dbg_ring_alloc_init(dhd,
			ECNTR_RING_ID, ECNTR_RING_NAME,
			LOG_DUMP_ECNTRS_MAX_BUFSIZE,
			bufptr, TRUE);
	if (!dhd->ecntr_dbg_ring) {
		DHD_ERROR(("%s: unable to init ecounters dbg ring !\n",
				__FUNCTION__));
		goto fail;
	}
	bufptr += LOG_DUMP_ECNTRS_MAX_BUFSIZE;
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	dhd->rtt_dbg_ring = dhd_dbg_ring_alloc_init(dhd,
			RTT_RING_ID, RTT_RING_NAME,
			LOG_DUMP_RTT_MAX_BUFSIZE,
			bufptr, TRUE);
	if (!dhd->rtt_dbg_ring) {
		DHD_ERROR(("%s: unable to init rtt dbg ring !\n",
				__FUNCTION__));
		goto fail;
	}
	bufptr += LOG_DUMP_RTT_MAX_BUFSIZE;
#endif /* EWP_RTT_LOGGING */

#ifdef EWP_BCM_TRACE
	dhd->bcm_trace_dbg_ring = dhd_dbg_ring_alloc_init(dhd,
			BCM_TRACE_RING_ID, BCM_TRACE_RING_NAME,
			LOG_DUMP_BCM_TRACE_MAX_BUFSIZE,
			bufptr, TRUE);
	if (!dhd->bcm_trace_dbg_ring) {
		DHD_ERROR(("%s: unable to init bcm trace dbg ring !\n",
				__FUNCTION__));
		goto fail;
	}
	bufptr += LOG_DUMP_BCM_TRACE_MAX_BUFSIZE;
#endif /* EWP_BCM_TRACE */

	/* Concise buffer is used as intermediate buffer for following purposes
	* a) pull ecounters records temporarily before
	*  writing it to file
	* b) to store dhd dump data before putting it to file
	* It should have a size equal to
	* MAX(largest possible ecntr record, 'dhd dump' data size)
	*/
	dhd->concise_dbg_buf = MALLOC(dhd->osh, CONCISE_DUMP_BUFLEN);
	if (!dhd->concise_dbg_buf) {
		DHD_ERROR(("%s: unable to alloc mem for concise debug info !\n",
				__FUNCTION__));
		goto fail;
	}

#if defined(DHD_EVENT_LOG_FILTER)
	/* XXX init filter last, because filter use buffer which alloced by log dump */
	ret = dhd_event_log_filter_init(dhd,
		bufptr,
		LOG_DUMP_FILTER_MAX_BUFSIZE);
	if (ret != BCME_OK) {
		goto fail;
	}
#endif /* DHD_EVENT_LOG_FILTER */

#if defined(EWP_ECNTRS_LOGGING)
	cookie_buf = MALLOC(dhd->osh, LOG_DUMP_COOKIE_BUFSIZE);
	if (!cookie_buf) {
		DHD_ERROR(("%s: unable to alloc mem for logdump cookie buffer\n",
			__FUNCTION__));
		goto fail;
	}

	ret = dhd_logdump_cookie_init(dhd, cookie_buf, LOG_DUMP_COOKIE_BUFSIZE);
	if (ret != BCME_OK) {
		MFREE(dhd->osh, cookie_buf, LOG_DUMP_COOKIE_BUFSIZE);
		goto fail;
	}
#endif /* EWP_ECNTRS_LOGGING */
	return;

fail:

#if defined(DHD_EVENT_LOG_FILTER)
	/* XXX deinit filter first, because filter use buffer which alloced by log dump */
	if (dhd->event_log_filter) {
		dhd_event_log_filter_deinit(dhd);
	}
#endif /* DHD_EVENT_LOG_FILTER */

	if (dhd->concise_dbg_buf) {
		MFREE(dhd->osh, dhd->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
	}

#ifdef EWP_ECNTRS_LOGGING
	if (dhd->logdump_cookie) {
		dhd_logdump_cookie_deinit(dhd);
		MFREE(dhd->osh, dhd->logdump_cookie, LOG_DUMP_COOKIE_BUFSIZE);
		dhd->logdump_cookie = NULL;
	}
#endif /* EWP_ECNTRS_LOGGING */

#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
	if (prealloc_buf) {
		DHD_OS_PREFREE(dhd, prealloc_buf, LOG_DUMP_TOTAL_BUFSIZE);
	}
	if (dld_buf_special->buffer) {
		DHD_OS_PREFREE(dhd, dld_buf_special->buffer,
				dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
	}
#else
	if (prealloc_buf) {
		MFREE(dhd->osh, prealloc_buf, LOG_DUMP_TOTAL_BUFSIZE);
	}
	if (dld_buf_special->buffer) {
		MFREE(dhd->osh, dld_buf_special->buffer,
				dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
	}
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		dld_buf = &g_dld_buf[i];
		dld_buf->enable = 0;
		dld_buf->buffer = NULL;
	}
	mutex_destroy(&dhd_info->logdump_lock);
}

void
dhd_log_dump_deinit(dhd_pub_t *dhd)
{
	struct dhd_log_dump_buf *dld_buf = NULL, *dld_buf_special = NULL;
	int i = 0;
	dhd_info_t *dhd_info = dhd->info;
	dhd_dbg_ring_t *ring = NULL;

	BCM_REFERENCE(ring);

	if (dhd->concise_dbg_buf) {
		MFREE(dhd->osh, dhd->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
		dhd->concise_dbg_buf = NULL;
	}

#ifdef EWP_ECNTRS_LOGGING
	if (dhd->logdump_cookie) {
		dhd_logdump_cookie_deinit(dhd);
		MFREE(dhd->osh, dhd->logdump_cookie, LOG_DUMP_COOKIE_BUFSIZE);
		dhd->logdump_cookie = NULL;
	}

	if (dhd->ecntr_dbg_ring) {
		dhd_dbg_ring_dealloc_deinit(&dhd->ecntr_dbg_ring, dhd);
	}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	if (dhd->rtt_dbg_ring) {
		dhd_dbg_ring_dealloc_deinit(&dhd->rtt_dbg_ring, dhd);
	}
#endif /* EWP_RTT_LOGGING */

#ifdef EWP_BCM_TRACE
	if (dhd->bcm_trace_dbg_ring) {
		dhd_dbg_ring_dealloc_deinit(&dhd->bcm_trace_dbg_ring, dhd);
	}
#endif /* EWP_BCM_TRACE */

	/* 'general' buffer points to start of the pre-alloc'd memory */
	dld_buf = &g_dld_buf[DLD_BUF_TYPE_GENERAL];
	dld_buf_special = &g_dld_buf[DLD_BUF_TYPE_SPECIAL];
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
	if (dld_buf->buffer) {
		DHD_OS_PREFREE(dhd, dld_buf->buffer, LOG_DUMP_TOTAL_BUFSIZE);
	}
	if (dld_buf_special->buffer) {
		DHD_OS_PREFREE(dhd, dld_buf_special->buffer,
				dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
	}
#else
	if (dld_buf->buffer) {
		MFREE(dhd->osh, dld_buf->buffer, LOG_DUMP_TOTAL_BUFSIZE);
	}
	if (dld_buf_special->buffer) {
		MFREE(dhd->osh, dld_buf_special->buffer,
				dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
	}
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		dld_buf = &g_dld_buf[i];
		dld_buf->enable = 0;
		dld_buf->buffer = NULL;
	}
	mutex_destroy(&dhd_info->logdump_lock);
}

void
dhd_log_dump_write(int type, char *binary_data,
		int binary_len, const char *fmt, ...)
{
	int len = 0;
	char tmp_buf[DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE] = {0, };
	va_list args;
	unsigned long flags = 0;
	struct dhd_log_dump_buf *dld_buf = NULL;
	bool flush_log = FALSE;

	if (type < 0 || type >= DLD_BUFFER_NUM) {
		DHD_INFO(("%s: Unsupported DHD_LOG_DUMP_BUF_TYPE(%d).\n",
			__FUNCTION__, type));
		return;
	}

	dld_buf = &g_dld_buf[type];
	if (dld_buf->enable != 1) {
		return;
	}

	va_start(args, fmt);
	len = vsnprintf(tmp_buf, DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE, fmt, args);
	/* Non ANSI C99 compliant returns -1,
	 * ANSI compliant return len >= DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE
	 */
	va_end(args);
	if (len < 0) {
		return;
	}

	if (len >= DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE) {
		len = DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE - 1;
		tmp_buf[len] = '\0';
	}

	/* make a critical section to eliminate race conditions */
	DHD_LOG_DUMP_BUF_LOCK(&dld_buf->lock, flags);
	if (dld_buf->remain < len) {
		dld_buf->wraparound = 1;
		dld_buf->present = dld_buf->front;
		dld_buf->remain = dld_buf_size[type];
		/* if wrap around happens, flush the ring buffer to the file */
		flush_log = TRUE;
	}

	memcpy(dld_buf->present, tmp_buf, len);
	dld_buf->remain -= len;
	dld_buf->present += len;
	DHD_LOG_DUMP_BUF_UNLOCK(&dld_buf->lock, flags);

	/* double check invalid memory operation */
	ASSERT((unsigned long)dld_buf->present <= dld_buf->max);

	if (dld_buf->dhd_pub) {
		dhd_pub_t *dhdp = (dhd_pub_t *)dld_buf->dhd_pub;
		dhdp->logdump_periodic_flush =
			logdump_periodic_flush;
		if (logdump_periodic_flush && flush_log) {
			log_dump_type_t *flush_type = MALLOCZ(dhdp->osh,
					sizeof(log_dump_type_t));
			if (flush_type) {
				*flush_type = type;
				dhd_schedule_log_dump(dld_buf->dhd_pub, flush_type);
			}
		}
	}
}

#ifdef DHD_DEBUGABILITY_LOG_DUMP_RING
extern struct dhd_dbg_ring_buf g_ring_buf;
void
dhd_dbg_ring_write(int type, char *binary_data,
		int binary_len, const char *fmt, ...)
{
	int len = 0;
	va_list args;
	struct dhd_dbg_ring_buf *ring_buf = NULL;
	char tmp_buf[DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE] = {0, };

	ring_buf = &g_ring_buf;

	va_start(args, fmt);
	len = vsnprintf(tmp_buf, DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE, fmt, args);
	/* Non ANSI C99 compliant returns -1,
	 * ANSI compliant return len >= DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE
	 */
	va_end(args);
	if (len < 0) {
		return;
	}

	if (len >= DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE) {
		len = DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE - 1;
		tmp_buf[len] = '\0';
	}

	if (ring_buf->dhd_pub) {
		dhd_pub_t *dhdp = (dhd_pub_t *)ring_buf->dhd_pub;
		if (type == DRIVER_LOG_RING_ID || type == FW_VERBOSE_RING_ID ||
				type == ROAM_STATS_RING_ID) {
			if (DBG_RING_ACTIVE(dhdp, type)) {
				dhd_os_push_push_ring_data(dhdp, type,
						tmp_buf, strlen(tmp_buf));
				return;
			}
		}
	}
	return;
}
#endif /* DHD_DEBUGABILITY_LOG_DUMP_RING */
#endif /* DHD_LOG_DUMP */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
void
dhd_flush_rx_tx_wq(dhd_pub_t *dhdp)
{
	dhd_info_t * dhd;

	if (dhdp) {
		dhd = dhdp->info;
		if (dhd) {
			flush_workqueue(dhd->tx_wq);
			flush_workqueue(dhd->rx_wq);
		}
	}

	return;
}
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

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
		if (dhdp->hang_reason == HANG_REASON_PCIE_LINK_DOWN_RC_DETECT ||
			dhdp->hang_reason == HANG_REASON_PCIE_LINK_DOWN_EP_DETECT ||
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
	char *filepath = VENDOR_PATH CONFIG_BCMDHD_CLM_PATH;

	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: ----- blob file doesn't exist (%s) -----\n", __FUNCTION__,
			filepath));
		dhdp->is_blob = FALSE;
	} else {
		DHD_ERROR(("%s: ----- blob file exists (%s) -----\n", __FUNCTION__, filepath));
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

	if (event != DHD_WQ_WORK_DMA_LB_MEM_REL) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}
	if (dhd_info == NULL) {
		DHD_ERROR(("%s: invalid dhd_info\n", __FUNCTION__));
		return;
	}
	if (dmmap == NULL) {
		DHD_ERROR(("%s: dmmap is null\n", __FUNCTION__));
		return;
	}
	dmaxfer_free_prev_dmaaddr(&dhd_info->pub, dmmap);
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
#ifdef SET_PCIE_IRQ_CPU_CORE
void
dhd_set_irq_cpucore(dhd_pub_t *dhdp, int affinity_cmd)
{
	unsigned int pcie_irq = 0;
#if defined(DHD_LB) && defined(DHD_LB_HOST_CTRL)
	struct dhd_info  *dhd = NULL;
#endif /* DHD_LB && DHD_LB_HOST_CTRL */

	if (!dhdp) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (!dhdp->bus) {
		DHD_ERROR(("%s : dhd->bus is NULL\n", __FUNCTION__));
		return;
	}

	if (affinity_cmd < DHD_AFFINITY_OFF || affinity_cmd > DHD_AFFINITY_LAST) {
		DHD_ERROR(("Wrong Affinity cmds:%d, %s\n", affinity_cmd, __FUNCTION__));
		return;
	}

	DHD_ERROR(("Enter %s, PCIe affinity cmd=0x%x\n", __FUNCTION__, affinity_cmd));

	if (dhdpcie_get_pcieirq(dhdp->bus, &pcie_irq)) {
		DHD_ERROR(("%s : Can't get interrupt number\n", __FUNCTION__));
		return;
	}

#if defined(DHD_LB) && defined(DHD_LB_HOST_CTRL)
	dhd = dhdp->info;

	if (affinity_cmd == DHD_AFFINITY_OFF) {
		dhd->permitted_primary_cpu = FALSE;
	} else if (affinity_cmd == DHD_AFFINITY_TPUT_150MBPS ||
		affinity_cmd == DHD_AFFINITY_TPUT_300MBPS) {
		dhd->permitted_primary_cpu = TRUE;
	}
	dhd_select_cpu_candidacy(dhd);
	/*
	* It needs to NAPI disable -> enable to raise NET_RX napi CPU core
	* during Rx traffic
	* NET_RX does not move to NAPI CPU core if continusly calling napi polling
	* function
	*/
	napi_disable(&dhd->rx_napi_struct);
	napi_enable(&dhd->rx_napi_struct);
#endif /* DHD_LB && DHD_LB_HOST_CTRL */

	/*
		irq_set_affinity() assign dedicated CPU core PCIe interrupt
		If dedicated CPU core is not on-line,
		PCIe interrupt scheduled on CPU core 0
	*/
#if defined(CONFIG_ARCH_SM8150) || defined(CONFIG_ARCH_KONA)
	/* For SDM platform */
	switch (affinity_cmd) {
		case DHD_AFFINITY_OFF:
#if defined(DHD_LB) && defined(DHD_LB_HOST_CTRL)
			irq_set_affinity_hint(pcie_irq, dhdp->info->cpumask_secondary);
			irq_set_affinity(pcie_irq, dhdp->info->cpumask_secondary);
#endif /* DHD_LB && DHD_LB_HOST_CTRL */
			break;
		case DHD_AFFINITY_TPUT_150MBPS:
		case DHD_AFFINITY_TPUT_300MBPS:
			irq_set_affinity_hint(pcie_irq, dhdp->info->cpumask_primary);
			irq_set_affinity(pcie_irq, dhdp->info->cpumask_primary);
			break;
		default:
			DHD_ERROR(("%s, Unknown PCIe affinity cmd=0x%x\n",
				__FUNCTION__, affinity_cmd));
	}
#elif defined(CONFIG_SOC_EXYNOS9810) || defined(CONFIG_SOC_EXYNOS9820) || \
	defined(CONFIG_SOC_EXYNOS9830)
	/* For Exynos platform */
	switch (affinity_cmd) {
		case DHD_AFFINITY_OFF:
#if defined(DHD_LB) && defined(DHD_LB_HOST_CTRL)
			irq_set_affinity(pcie_irq, dhdp->info->cpumask_secondary);
#endif /* DHD_LB && DHD_LB_HOST_CTRL */
			break;
		case DHD_AFFINITY_TPUT_150MBPS:
			irq_set_affinity(pcie_irq, dhdp->info->cpumask_primary);
			break;
		case DHD_AFFINITY_TPUT_300MBPS:
			DHD_ERROR(("%s, PCIe IRQ:%u set Core %d\n",
				__FUNCTION__, pcie_irq, PCIE_IRQ_CPU_CORE));
			irq_set_affinity(pcie_irq, cpumask_of(PCIE_IRQ_CPU_CORE));
			break;
		default:
			DHD_ERROR(("%s, Unknown PCIe affinity cmd=0x%x\n",
				__FUNCTION__, affinity_cmd));
	}
#else /* For Undefined platform */
	DHD_ERROR(("%s, Unknown PCIe affinity cmd=0x%x\n",
		__FUNCTION__, affinity_cmd));
#endif /* End of Platfrom define */

}
#endif /* SET_PCIE_IRQ_CPU_CORE */

int
dhd_write_file(const char *filepath, char *buf, int buf_len)
{
	struct file *fp = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t old_fs;
#endif
	int ret = 0;

	/* change to KERNEL_DS address limit */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	/* File is always created. */
	fp = filp_open(filepath, O_RDWR | O_CREAT, 0664);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: Couldn't open file '%s' err %ld\n",
			__FUNCTION__, filepath, PTR_ERR(fp)));
		ret = BCME_ERROR;
	} else {
		if (fp->f_mode & FMODE_WRITE) {
			ret = vfs_write(fp, buf, buf_len, &fp->f_pos);
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(old_fs);
#endif

	return ret;
}

int
dhd_read_file(const char *filepath, char *buf, int buf_len)
{
	struct file *fp = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mm_segment_t old_fs;
#endif
	int ret;

	/* change to KERNEL_DS address limit */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
		set_fs(old_fs);
#endif
		DHD_ERROR(("%s: File %s doesn't exist\n", __FUNCTION__, filepath));
		return BCME_ERROR;
	}

	ret = kernel_read_compat(fp, 0, buf, buf_len);
	filp_close(fp, NULL);

	/* restore previous address limit */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	set_fs(old_fs);
#endif

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

#ifdef FILTER_IE
int dhd_read_from_file(dhd_pub_t *dhd)
{
	int ret = 0, nread = 0;
	void *fd;
	uint8 *buf;
	NULL_CHECK(dhd, "dhd is NULL", ret);

	buf = MALLOCZ(dhd->osh, FILE_BLOCK_READ_SIZE);
	if (!buf) {
		DHD_ERROR(("error: failed to alllocate buf.\n"));
		return BCME_NOMEM;
	}

	/* open file to read */
	fd = dhd_os_open_image1(dhd, FILTER_IE_PATH);
	if (!fd) {
		DHD_ERROR(("No filter file(not an error), filter path%s\n", FILTER_IE_PATH));
		ret = BCME_EPERM;
		goto exit;
	}
	nread = dhd_os_get_image_block(buf, (FILE_BLOCK_READ_SIZE - 1), fd);
	if (nread > 0) {
		buf[nread] = '\0';
		if ((ret = dhd_parse_filter_ie(dhd, buf)) < 0) {
			DHD_ERROR(("error: failed to parse filter ie\n"));
		}
	} else {
		DHD_ERROR(("error: zero length file.failed to read\n"));
		ret = BCME_ERROR;
	}
	dhd_os_close_image1(dhd, fd);
exit:
	if (buf) {
		MFREE(dhd->osh, buf, FILE_BLOCK_READ_SIZE);
	}
	return ret;
}

int dhd_get_filter_ie_count(dhd_pub_t *dhdp, uint8* buf)
{
	uint8* pstr = buf;
	int element_count = 0;

	if (buf == NULL) {
		return BCME_ERROR;
	}

	while (*pstr != '\0') {
		if (*pstr == '\n') {
			element_count++;
		}
		pstr++;
	}
	/*
	 * New line character must not be present after last line.
	 * To count last line
	 */
	element_count++;

	return element_count;
}

int dhd_parse_oui(dhd_pub_t *dhd, uint8 *inbuf, uint8 *oui, int len)
{
	uint8 i, j, msb, lsb, oui_len = 0;
	/*
	 * OUI can vary from 3 bytes to 5 bytes.
	 * While reading from file as ascii input it can
	 * take maximum size of 14 bytes and minumum size of
	 * 8 bytes including ":"
	 * Example 5byte OUI <AB:DE:BE:CD:FA>
	 * Example 3byte OUI <AB:DC:EF>
	 */

	if ((inbuf == NULL) || (len < 8) || (len > 14)) {
		DHD_ERROR(("error: failed to parse OUI \n"));
		return BCME_ERROR;
	}

	for (j = 0, i = 0; i < len; i += 3, ++j) {
		if (!bcm_isxdigit(inbuf[i]) || !bcm_isxdigit(inbuf[i + 1])) {
			DHD_ERROR(("error: invalid OUI format \n"));
			return BCME_ERROR;
		}
		msb = inbuf[i] > '9' ? bcm_toupper(inbuf[i]) - 'A' + 10 : inbuf[i] - '0';
		lsb = inbuf[i + 1] > '9' ? bcm_toupper(inbuf[i + 1]) -
			'A' + 10 : inbuf[i + 1] - '0';
		oui[j] = (msb << 4) | lsb;
	}
	/* Size of oui.It can vary from 3/4/5 */
	oui_len = j;

	return oui_len;
}

int dhd_check_valid_ie(dhd_pub_t *dhdp, uint8* buf, int len)
{
	int i = 0;

	while (i < len) {
		if (!bcm_isdigit(buf[i])) {
			DHD_ERROR(("error: non digit value found in filter_ie \n"));
			return BCME_ERROR;
		}
		i++;
	}
	if (bcm_atoi((char*)buf) > 255) {
		DHD_ERROR(("error: element id cannot be greater than 255 \n"));
		return BCME_ERROR;
	}

	return BCME_OK;
}

int dhd_parse_filter_ie(dhd_pub_t *dhd, uint8 *buf)
{
	int element_count = 0, i = 0, oui_size = 0, ret = 0;
	uint16 bufsize, buf_space_left, id = 0, len = 0;
	uint16 filter_iovsize, all_tlvsize;
	wl_filter_ie_tlv_t *p_ie_tlv = NULL;
	wl_filter_ie_iov_v1_t *p_filter_iov = (wl_filter_ie_iov_v1_t *) NULL;
	char *token = NULL, *ele_token = NULL, *oui_token = NULL, *type = NULL;
	uint8 data[20];

	element_count = dhd_get_filter_ie_count(dhd, buf);
	DHD_INFO(("total element count %d \n", element_count));
	/* Calculate the whole buffer size */
	filter_iovsize = sizeof(wl_filter_ie_iov_v1_t) + FILTER_IE_BUFSZ;
	p_filter_iov = MALLOCZ(dhd->osh, filter_iovsize);

	if (p_filter_iov == NULL) {
		DHD_ERROR(("error: failed to allocate %d bytes of memory\n", filter_iovsize));
		return BCME_ERROR;
	}

	/* setup filter iovar header */
	p_filter_iov->version = WL_FILTER_IE_VERSION;
	p_filter_iov->len = filter_iovsize;
	p_filter_iov->fixed_length = p_filter_iov->len - FILTER_IE_BUFSZ;
	p_filter_iov->pktflag = FC_PROBE_REQ;
	p_filter_iov->option = WL_FILTER_IE_CHECK_SUB_OPTION;
	/* setup TLVs */
	bufsize = filter_iovsize - WL_FILTER_IE_IOV_HDR_SIZE; /* adjust available size for TLVs */
	p_ie_tlv = (wl_filter_ie_tlv_t *)&p_filter_iov->tlvs[0];
	buf_space_left = bufsize;

	while ((i < element_count) && (buf != NULL)) {
		len = 0;
		/* token contains one line of input data */
		token = bcmstrtok((char**)&buf, "\n", NULL);
		if (token == NULL) {
			break;
		}
		if ((ele_token = bcmstrstr(token, ",")) == NULL) {
		/* only element id is present */
			if (dhd_check_valid_ie(dhd, token, strlen(token)) == BCME_ERROR) {
				DHD_ERROR(("error: Invalid element id \n"));
				ret = BCME_ERROR;
				goto exit;
			}
			id = bcm_atoi((char*)token);
			data[len++] = WL_FILTER_IE_SET;
		} else {
			/* oui is present */
			ele_token = bcmstrtok(&token, ",", NULL);
			if ((ele_token == NULL) || (dhd_check_valid_ie(dhd, ele_token,
				strlen(ele_token)) == BCME_ERROR)) {
				DHD_ERROR(("error: Invalid element id \n"));
				ret = BCME_ERROR;
				goto exit;
			}
			id =  bcm_atoi((char*)ele_token);
			data[len++] = WL_FILTER_IE_SET;
			if ((oui_token = bcmstrstr(token, ",")) == NULL) {
				oui_size = dhd_parse_oui(dhd, token, &(data[len]), strlen(token));
				if (oui_size == BCME_ERROR) {
					DHD_ERROR(("error: Invalid OUI \n"));
					ret = BCME_ERROR;
					goto exit;
				}
				len += oui_size;
			} else {
				/* type is present */
				oui_token = bcmstrtok(&token, ",", NULL);
				if ((oui_token == NULL) || ((oui_size =
					dhd_parse_oui(dhd, oui_token,
					&(data[len]), strlen(oui_token))) == BCME_ERROR)) {
					DHD_ERROR(("error: Invalid OUI \n"));
					ret = BCME_ERROR;
					goto exit;
				}
				len += oui_size;
				if ((type = bcmstrstr(token, ",")) == NULL) {
					if (dhd_check_valid_ie(dhd, token,
						strlen(token)) == BCME_ERROR) {
						DHD_ERROR(("error: Invalid type \n"));
						ret = BCME_ERROR;
						goto exit;
					}
					data[len++] = bcm_atoi((char*)token);
				} else {
					/* subtype is present */
					type = bcmstrtok(&token, ",", NULL);
					if ((type == NULL) || (dhd_check_valid_ie(dhd, type,
						strlen(type)) == BCME_ERROR)) {
						DHD_ERROR(("error: Invalid type \n"));
						ret = BCME_ERROR;
						goto exit;
					}
					data[len++] = bcm_atoi((char*)type);
					/* subtype is last element */
					if ((token == NULL) || (*token == '\0') ||
						(dhd_check_valid_ie(dhd, token,
						strlen(token)) == BCME_ERROR)) {
						DHD_ERROR(("error: Invalid subtype \n"));
						ret = BCME_ERROR;
						goto exit;
					}
					data[len++] = bcm_atoi((char*)token);
				}
			}
		}
		ret = bcm_pack_xtlv_entry((uint8 **)&p_ie_tlv,
			&buf_space_left, id, len, data, BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s : bcm_pack_xtlv_entry() failed ,"
				"status=%d\n", __FUNCTION__, ret));
			goto exit;
		}
		i++;
	}
	if (i == 0) {
		/* file is empty or first line is blank */
		DHD_ERROR(("error: filter_ie file is empty or first line is blank \n"));
		ret = BCME_ERROR;
		goto exit;
	}
	/* update the iov header, set len to include all TLVs + header */
	all_tlvsize = (bufsize - buf_space_left);
	p_filter_iov->len = htol16(all_tlvsize + WL_FILTER_IE_IOV_HDR_SIZE);
	ret = dhd_iovar(dhd, 0, "filter_ie", (void *)p_filter_iov,
			p_filter_iov->len, NULL, 0, TRUE);
	if (ret != BCME_OK) {
		DHD_ERROR(("error: IOVAR failed, status=%d\n", ret));
	}
exit:
	/* clean up */
	if (p_filter_iov) {
		MFREE(dhd->osh, p_filter_iov, filter_iovsize);
	}
	return ret;
}
#endif /* FILTER_IE */
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

int
dhd_get_random_bytes(uint8 *buf, uint len)
{
#ifdef BCMPCIE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	int rndlen = get_random_bytes_arch(buf, len);
	if (rndlen != len) {
		bzero(buf, len);
		get_random_bytes(buf, len);
	}
#else
	get_random_bytes_arch(buf, len);
#endif
#endif /* BCMPCIE */
	return BCME_OK;
}

#if defined(DHD_HANG_SEND_UP_TEST)
void
dhd_make_hang_with_reason(struct net_device *dev, const char *string_num)
{
	dhd_info_t *dhd = NULL;
	dhd_pub_t *dhdp = NULL;
	uint reason = HANG_REASON_MAX;
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
			(void) dhd_wl_ioctl_set_intiovar(dhdp, "bus:disconnect", fw_test_code,
				WLC_SET_VAR, TRUE, 0);
			break;
		case HANG_REASON_DONGLE_TRAP:
			DHD_ERROR(("Make HANG!!!: Dongle trap (0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			fw_test_code = 99; /* dongle trap */
			(void) dhd_wl_ioctl_set_intiovar(dhdp, "bus:disconnect", fw_test_code,
				WLC_SET_VAR, TRUE, 0);
			break;
		case HANG_REASON_D3_ACK_TIMEOUT:
			DHD_ERROR(("Make HANG!!!: D3 ACK timeout (0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			break;
		case HANG_REASON_BUS_DOWN:
			DHD_ERROR(("Make HANG!!!: BUS down(0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			break;
		case HANG_REASON_PCIE_LINK_DOWN_RC_DETECT:
		case HANG_REASON_PCIE_LINK_DOWN_EP_DETECT:
		case HANG_REASON_MSGBUF_LIVELOCK:
			dhdp->req_hang_type = 0;
			DHD_ERROR(("Does not support requested HANG(0x%x)\n", reason));
			break;
		case HANG_REASON_IFACE_DEL_FAILURE:
			dhdp->req_hang_type = 0;
			DHD_ERROR(("Does not support requested HANG(0x%x)\n", reason));
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

#ifdef BT_OVER_PCIE
#define BT_QUIESCE TRUE
#define BT_RESUME FALSE
#define BT_QUIESCE_RESPONSE_TIMEOUT 4000

int
dhd_request_bt_quiesce(dhd_pub_t *dhdp)
{
	dhd_info_t * dhd = (dhd_info_t *)(dhdp->info);
	long timeout = BT_QUIESCE_RESPONSE_TIMEOUT;

	if (request_bt_quiesce_ptr == NULL) {
		DHD_ERROR(("%s: BT not loaded\n", __FUNCTION__));
		return BCME_OK;
	}

	mutex_lock(&dhd->quiesce_lock);
	DHD_ERROR(("%s: start quiesce_state = %d\n", __FUNCTION__, dhd->dhd_quiesce_state));
	if (dhd->dhd_quiesce_state != DHD_QUIESCE_INIT) {
		DHD_ERROR(("%s: start quiesce_state = %d\n", __FUNCTION__, dhd->dhd_quiesce_state));
		mutex_unlock(&dhd->quiesce_lock);
		return BCME_ERROR;
	}
	dhd->dhd_quiesce_state = REQUEST_BT_QUIESCE;
	request_bt_quiesce_ptr(BT_QUIESCE);

	timeout = wait_event_timeout(dhd->quiesce_wait,
		(dhd->dhd_quiesce_state == RESPONSE_BT_QUIESCE), timeout);

	DHD_ERROR(("%s: after wait quiesce_state = %d\n", __FUNCTION__, dhd->dhd_quiesce_state));

	mutex_unlock(&dhd->quiesce_lock);
	if (!timeout) {
		DHD_ERROR(("%s: timeout quiesce_state = %d\n",
			__FUNCTION__, dhd->dhd_quiesce_state));
		return BCME_BUSY;
	}
	return BCME_OK;
}

int
dhd_request_bt_resume(dhd_pub_t *dhdp)
{
	dhd_info_t * dhd = (dhd_info_t *)(dhdp->info);
	long timeout = BT_QUIESCE_RESPONSE_TIMEOUT;

	if (request_bt_quiesce_ptr == NULL) {
		DHD_ERROR(("%s: BT not loaded\n", __FUNCTION__));
		return BCME_OK;
	}

	DHD_ERROR(("%s: start quiesce_state = %d\n", __FUNCTION__, dhd->dhd_quiesce_state));
	mutex_lock(&dhd->quiesce_lock);
	if (dhd->dhd_quiesce_state != RESPONSE_BT_QUIESCE) {
		mutex_unlock(&dhd->quiesce_lock);
		return BCME_ERROR;
	}
	dhd->dhd_quiesce_state = REQUEST_BT_RESUME;
	request_bt_quiesce_ptr(BT_RESUME);

	timeout = wait_event_timeout(dhd->quiesce_wait,
		(dhd->dhd_quiesce_state == RESPONSE_BT_RESUME), timeout);

	DHD_ERROR(("%s: after wait quiesce_state = %d\n", __FUNCTION__, dhd->dhd_quiesce_state));

	dhd->dhd_quiesce_state = DHD_QUIESCE_INIT;
	mutex_unlock(&dhd->quiesce_lock);
	if (!timeout) {
		DHD_ERROR(("%s: timeout quiesce_state = %d\n",
			__FUNCTION__, dhd->dhd_quiesce_state));
		return BCME_BUSY;
	}
	return BCME_OK;
}

void
response_bt_quiesce(bool quiesce)
{
	dhd_pub_t *dhdp = g_dhd_pub;
	dhd_info_t * dhd = (dhd_info_t *)(dhdp->info);
	if (quiesce == BT_QUIESCE) {
		if (dhd->dhd_quiesce_state == REQUEST_BT_QUIESCE) {
			dhd->dhd_quiesce_state = RESPONSE_BT_QUIESCE;
			wake_up(&dhd->quiesce_wait);
			return;
		}
	} else if (quiesce == BT_RESUME) {
		if (dhd->dhd_quiesce_state == REQUEST_BT_RESUME) {
			dhd->dhd_quiesce_state = RESPONSE_BT_RESUME;
			wake_up(&dhd->quiesce_wait);
			return;
		}
	}
	DHD_ERROR(("%s: Wrong Queisce Response=%d in State=%d\n",
		__FUNCTION__, quiesce, dhd->dhd_quiesce_state));
	return;
}

int
dhd_bus_perform_flr_with_quiesce(dhd_pub_t *dhdp, struct dhd_bus *bus,
		bool init_deinit_path)
{
	int ret;
	dhd_info_t * dhd = (dhd_info_t *)(dhdp->info);
	bool dongle_isolation = dhdp->dongle_isolation;
	mutex_lock(&dhd->quiesce_flr_lock);
	dhd->dhd_quiesce_state = DHD_QUIESCE_INIT;

	/* pause data on all the interfaces */
	dhd_bus_stop_queue(dhdp->bus);

	/* Since we are about to do FLR advertise that bus down is in progress
	 * to other bus user contexts like Tx, Rx, IOVAR, WD etc
	 */
	dhdpcie_advertise_bus_cleanup(dhdp);

#ifdef BT_OVER_PCIE
	/* Disable L1SS of RC and EP
	 * L1SS is enabled again in dhd_bus_start if dhd_sync_with_dongle succeed
	 */
	dhd_bus_l1ss_enable_rc_ep(dhdp->bus, FALSE);
#endif /* BT_OVER_PCIE */

	if (dhd_bus_force_bt_quiesce_enabled(dhdp->bus)) {
		DHD_ERROR(("%s: Request Quiesce\n", __FUNCTION__));
		/* Request BT quiesce right before F0 FLR to minimise latency */
		ret = dhd_request_bt_quiesce(dhdp); /* Handle return value */
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: Error(%d) in Request Quiesce\n", __FUNCTION__, ret));
			/* TODO: plugin API for Toggle REGON Here */
			mutex_unlock(&dhd->quiesce_flr_lock);
			return ret;
		}
	}

	dhd_bus_pcie_pwr_req_reload_war(dhdp->bus);

	DHD_ERROR(("%s: Perform FLR\n", __FUNCTION__));

	ret = dhd_bus_perform_flr(dhdp->bus, dhd_bus_get_flr_force_fail(dhdp->bus));
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: Error(%d) in Performing FLR\n", __FUNCTION__, ret));
		/* TODO: Ensure that BT Host Driver is out of Quiesce state before REGON
		 * Either by sending an unquiesce message Here OR as a part of ON/OFF API.
		 */
		/* TODO: plugin API for Toggle REGON Here */
		mutex_unlock(&dhd->quiesce_flr_lock);
		return ret;
	}

	if (dhd_bus_force_bt_quiesce_enabled(dhdp->bus)) {
		DHD_ERROR(("%s: Request Resume\n", __FUNCTION__));
		/* Resume BT right after F0 FLR to minimise latency */
		ret = dhd_request_bt_resume(dhdp); /* Handle return value */
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: Error(%d) in Request Resume\n", __FUNCTION__, ret));
			/* TODO: plugin API for Toggle REGON Here */
			mutex_unlock(&dhd->quiesce_flr_lock);
			return ret;
		}
	}

	/* Devreset function will perform FLR again, to avoid it set dongle_isolation */
	dhdp->dongle_isolation = TRUE;

	DHD_ERROR(("%s: Devreset ON\n", __FUNCTION__));
	dhd_bus_devreset(dhdp, 1); /* DHD structure cleanup */

	DHD_ERROR(("%s: Devreset OFF\n", __FUNCTION__));
	dhd_bus_devreset(dhdp, 0); /* DHD structure re-init */

	dhdp->dongle_isolation = dongle_isolation; /* Restore the old value */

	/* resume data on all the interfaces */
	dhd_bus_start_queue(dhdp->bus);
	mutex_unlock(&dhd->quiesce_flr_lock);

	DHD_ERROR(("%s: done\n", __FUNCTION__));
	return BCME_DNGL_DEVRESET;
}
#endif /* BT_OVER_PCIE */

#ifdef DHD_TX_PROFILE
static int
process_layer2_headers(uint8 **p, int *plen, uint16 *type, bool is_host_sfhllc)
{
	int err = BCME_OK;

	if (*type < ETHER_TYPE_MIN) {
		struct dot3_mac_llc_snap_header *sh = (struct dot3_mac_llc_snap_header *)*p;

		if (bcmp(&sh->dsap, llc_snap_hdr, SNAP_HDR_LEN) == 0) {
			*type = ntoh16(sh->type);
			if (*type == ETHER_TYPE_8021Q ||
				(is_host_sfhllc && *type != ETHER_TYPE_8021Q)) {
				*p += sizeof(struct dot3_mac_llc_snap_header);
				if ((*plen -= sizeof(struct dot3_mac_llc_snap_header)) <= 0) {
					err = BCME_ERROR;
				}
			}
			else {
				struct dot3_mac_llc_snapvlan_header *svh = (struct
					dot3_mac_llc_snapvlan_header *)*p;

				*type = ntoh16(svh->ether_type);
				*p += sizeof(struct dot3_mac_llc_snapvlan_header);
				if ((*plen -= sizeof(struct dot3_mac_llc_snapvlan_header)) <= 0) {
					err = BCME_ERROR;
				}
			}
		}
		else {
			err = BCME_ERROR;
		}
	}
	else {
		if (*type == ETHER_TYPE_8021Q) {
			struct ethervlan_header *evh = (struct ethervlan_header *)*p;

			*type = ntoh16(evh->ether_type);
			*p += ETHERVLAN_HDR_LEN;
			if ((*plen -= ETHERVLAN_HDR_LEN) <= 0) {
				err = BCME_ERROR;
			}
		}
		else {
			*p += ETHER_HDR_LEN;
			if ((*plen -= ETHER_HDR_LEN) <= 0) {
				err = BCME_ERROR;
			}
		}
	}

	return err;
}

static int
process_layer3_headers(uint8 **p, int plen, uint16 *type)
{
	int err = BCME_OK;

	if (*type == ETHER_TYPE_IP) {
		struct ipv4_hdr *iph = (struct ipv4_hdr *)*p;
		uint16 len = IPV4_HLEN(iph);
		if ((plen -= len) <= 0) {
			err = BCME_ERROR;
		} else if (IP_VER(iph) == IP_VER_4 && len >= IPV4_MIN_HEADER_LEN) {
			*type = IPV4_PROT(iph);
			*p += len;
		} else {
			err = BCME_ERROR;
		}
	} else if (*type == ETHER_TYPE_IPV6) {
		struct ipv6_hdr *ip6h = (struct ipv6_hdr *)*p;
		if ((plen -= IPV6_MIN_HLEN) <= 0) {
			err = BCME_ERROR;
		} else if (IP_VER(ip6h) == IP_VER_6) {
			*type = IPV6_PROT(ip6h);
			*p += IPV6_MIN_HLEN;
			if (IPV6_EXTHDR(*type)) {
				uint8 proto_6 = 0;
				int32 exth_len = ipv6_exthdr_len(*p, &proto_6);
				if (exth_len < 0 || ((plen -= exth_len) <= 0)) {
					err = BCME_ERROR;
				} else {
					*type = proto_6;
					*p += exth_len;
				}
			}
		} else {
			err = BCME_ERROR;
		}
	}

	return err;
}

bool
dhd_protocol_matches_profile(uint8 *p, int plen, const dhd_tx_profile_protocol_t
		*proto, bool is_host_sfhllc)
{
	struct ether_header *eh = NULL;
	bool result = FALSE;
	uint16 type = 0, ether_type = 0;

	ASSERT(proto != NULL);
	ASSERT(p != NULL);

	if (plen <= 0) {
		result = FALSE;
	} else {
		eh = (struct ether_header *)p;
		type = ntoh16(eh->ether_type);
		if (type < ETHER_TYPE_MIN && is_host_sfhllc) {
			struct dot3_mac_llc_snap_header *dot3 =
				(struct dot3_mac_llc_snap_header *)p;
			ether_type = ntoh16(dot3->type);
		} else {
			ether_type = type;
		}

		if (proto->layer == DHD_TX_PROFILE_DATA_LINK_LAYER &&
				proto->protocol_number == ether_type) {
			result = TRUE;
		} else if (process_layer2_headers(&p, &plen, &type, is_host_sfhllc) != BCME_OK) {
			/* pass 'type' instead of 'ether_type' to process_layer2_headers
			 * because process_layer2_headers will take care of extraction
			 * of protocol types if llc snap header is present, based on
			 * the condition (type < ETHER_TYPE_MIN)
			 */
			result = FALSE;
		} else if (proto->layer == DHD_TX_PROFILE_DATA_LINK_LAYER) {
			result = proto->protocol_number == type;
		} else if (proto->layer != DHD_TX_PROFILE_NETWORK_LAYER) {
			result = FALSE;
		} else if (process_layer3_headers(&p, plen, &type) != BCME_OK) {
			result = FALSE;
		} else if (proto->protocol_number == type) {
			/* L4, only check TCP/UDP case */
			if ((type == IP_PROT_TCP) || (type == IP_PROT_UDP)) {
				/* src/dst port are the first two uint16 fields in both tcp/udp
				 * hdr
				 */
				struct bcmudp_hdr *hdr = (struct bcmudp_hdr *)p;

				/* note that a src_port or dest_port of zero counts as a match
				 */
				result = ((proto->src_port == 0) || (proto->src_port ==
					ntoh16(hdr->src_port))) && ((proto->dest_port == 0) ||
					(proto->dest_port == ntoh16(hdr->dst_port)));
			} else {
				/* at this point we know we are dealing with layer 3, and we
				 * know we are not dealing with TCP or UDP; this is considered a
				 * match
				 */
				result = TRUE;
			}
		}
	}

	return result;
}
#endif /* defined(DHD_TX_PROFILE) */

#ifdef DHD_TIMESYNC
void
BCMFASTPATH(dhd_parse_proto)(uint8 *pktdata, dhd_pkt_parse_t *parse)
{
	uint8 *pkt = NULL;
	struct iphdr *iph = NULL;
	struct ether_header *eh = (struct ether_header *)pktdata;

	if (ntoh16(eh->ether_type) < ETHER_TYPE_MIN) {
		pkt = (uint8 *)&pktdata[ETHER_HDR_LEN + DOT11_LLC_SNAP_HDR_LEN];
	} else {
		pkt = (uint8 *)&pktdata[ETHER_HDR_LEN];
	}

	iph = (struct iphdr *)pkt;

	parse->proto = IP_PROT_RESERVED;
	parse->t1 = 0;
	parse->t2 = 0;

	/* check IP header */
	if ((IPV4_HLEN(iph) != IPV4_HLEN_MIN) || (IP_VER(iph) != IP_VER_4)) {
		return;
	}

	if (iph->protocol == IP_PROT_ICMP) {
		struct icmphdr *icmph;

		parse->proto = iph->protocol;
		icmph = (struct icmphdr *)((uint8 *)pkt + sizeof(struct iphdr));

		if ((icmph->type == ICMP_ECHO) || (icmph->type == ICMP_ECHOREPLY)) {
			parse->t1 = icmph->type;
			parse->t2 = ntoh16(icmph->un.echo.sequence);
		} else {
			parse->t1 = icmph->type;
			parse->t2 = icmph->code;
		}
	} else {
		parse->proto = iph->protocol;
	}

	return;
}
#endif /* DHD_TIMESYNC */

#ifdef BCMPCIE
#define KIRQ_PRINT_BUF_LEN 256

void
dhd_print_kirqstats(dhd_pub_t *dhd, unsigned int irq_num)
{
	unsigned long flags = 0;
	struct irq_desc *desc;
	int i;          /* cpu iterator */
	struct bcmstrbuf strbuf;
	char tmp_buf[KIRQ_PRINT_BUF_LEN];

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
	desc = irq_data_to_desc(irq_get_irq_data(irq_num));
#else
	desc = irq_to_desc(irq_num);
#endif // (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))
	if (!desc) {
		DHD_ERROR(("%s : irqdesc is not found \n", __FUNCTION__));
		return;
	}
	bcm_binit(&strbuf, tmp_buf, KIRQ_PRINT_BUF_LEN);
	raw_spin_lock_irqsave(&desc->lock, flags);
	bcm_bprintf(&strbuf, "dhd irq %u:", irq_num);
	for_each_online_cpu(i)
		bcm_bprintf(&strbuf, "%10u ",
			desc->kstat_irqs ? *per_cpu_ptr(desc->kstat_irqs, i) : 0);
	if (desc->irq_data.chip) {
		if (desc->irq_data.chip->name)
			bcm_bprintf(&strbuf, " %8s", desc->irq_data.chip->name);
		else
			bcm_bprintf(&strbuf, " %8s", "-");
	} else {
		bcm_bprintf(&strbuf, " %8s", "None");
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
	if (desc->irq_data.domain)
		bcm_bprintf(&strbuf, " %d", (int)desc->irq_data.hwirq);
#ifdef CONFIG_GENERIC_IRQ_SHOW_LEVEL
	bcm_bprintf(&strbuf, " %-8s", irqd_is_level_type(&desc->irq_data) ? "Level" : "Edge");
#endif
#endif /* LINUX VERSION > 3.1.0 */

	if (desc->name)
		bcm_bprintf(&strbuf, "-%-8s", desc->name);

	DHD_ERROR(("%s\n", strbuf.origbuf));
	raw_spin_unlock_irqrestore(&desc->lock, flags);
#endif /* LINUX VERSION > 2.6.28 */
}
#endif /* BCMPCIE */

void
dhd_show_kirqstats(dhd_pub_t *dhd)
{
	unsigned int irq = -1;
#ifdef BCMPCIE
	dhdpcie_get_pcieirq(dhd->bus, &irq);
#endif /* BCMPCIE */
#ifdef BCMSDIO
	irq = ((wifi_adapter_info_t *)dhd->info->adapter)->irq_num;
#endif /* BCMSDIO */
	if (irq != -1) {
#ifdef BCMPCIE
		DHD_ERROR(("DUMP data kernel irq stats : \n"));
		dhd_print_kirqstats(dhd, irq);
#endif /* BCMPCIE */
#ifdef BCMSDIO
		DHD_ERROR(("DUMP data/host wakeup kernel irq stats : \n"));
#endif /* BCMSDIO */
	}
#ifdef BCMPCIE_OOB_HOST_WAKE
	irq = dhd_bus_get_oob_irq_num(dhd);
	if (irq) {
		DHD_ERROR(("DUMP PCIE host wakeup kernel irq stats : \n"));
		dhd_print_kirqstats(dhd, irq);
	}
#endif /* BCMPCIE_OOB_HOST_WAKE */
}

void
dhd_print_tasklet_status(dhd_pub_t *dhd)
{
	dhd_info_t *dhdinfo;

	if (!dhd) {
		DHD_ERROR(("%s : DHD is null\n", __FUNCTION__));
		return;
	}

	dhdinfo = dhd->info;

	if (!dhdinfo) {
		DHD_ERROR(("%s : DHD INFO is null \n", __FUNCTION__));
		return;
	}

	DHD_ERROR(("DHD Tasklet status : 0x%lx\n", dhdinfo->tasklet.state));
}

#if defined(DHD_MQ) && defined(DHD_MQ_STATS)
void
dhd_mqstats_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	dhd_info_t *dhd = NULL;
	int i = 0, j = 0;

	if (!dhdp || !strbuf)
		return;

	dhd = dhdp->info;
	bcm_bprintf(strbuf, "\nMQ STATS:\n=========\n");

	bcm_bprintf(strbuf, "\nTx packet arrival AC histogram:\n");
	bcm_bprintf(strbuf, "AC_BE    \tAC_BK    \tAC_VI    \tAC_VO\n");
	bcm_bprintf(strbuf, "-----    \t-----    \t-----    \t-----\n");
	for (i = 0; i < AC_COUNT; i++)
		bcm_bprintf(strbuf, "%-10d\t", dhd->pktcnt_per_ac[i]);

	bcm_bprintf(strbuf, "\n\nTx packet arrival Q-AC histogram:\n");
	bcm_bprintf(strbuf, "\tAC_BE    \tAC_BK    \tAC_VI    \tAC_VO\n");
	bcm_bprintf(strbuf, "\t-----    \t-----    \t-----    \t-----");
	for (i = 0; i < MQ_MAX_QUEUES; i++) {
		bcm_bprintf(strbuf, "\nQ%d\t", i);
		for (j = 0; j < AC_COUNT; j++)
			bcm_bprintf(strbuf, "%-8d\t", dhd->pktcnt_qac_histo[i][j]);
	}

	bcm_bprintf(strbuf, "\n\nTx Q-CPU scheduling histogram:\n");
	bcm_bprintf(strbuf, "\t");
	for (i = 0; i < nr_cpu_ids; i++)
		bcm_bprintf(strbuf, "CPU%d    \t", i);
	for (i = 0; i < MQ_MAX_QUEUES; i++) {
		bcm_bprintf(strbuf, "\nQ%d\t", i);
		for (j = 0; j < nr_cpu_ids; j++)
			bcm_bprintf(strbuf, "%-8d\t", dhd->cpu_qstats[i][j]);
	}
	bcm_bprintf(strbuf, "\n");
}
#endif /* DHD_MQ && DHD_MQ_STATS */

#if defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL)
/* Procfs that provides to GDB Proxy asynchronous access to "sbreg", "membytes",
 * "gdb_proxy_probe", "gdb_proxy_stop_count" iovars.
 * Procfs is comprised of the root directory,
 * /proc/dhd_gdb_proxy_<dev_name> (here <dev_name> is like 'eth0',
 * etc.) that contains files: "sbreg", "membytes", "gdb_proxy_probe",
 * "gdb_proxy_stop_count". These files are to be used to access respective
 * iovars. Difference from iovar is that access to these files is not blocked
 * by current iovar processing (i.e. file might be accessed while wl iovar is
 * stuck on breakpoint inside firmware)
 * Setting address for "membytes" and "sbreg" files is performed by means of
 * seek position
 * For now "membytes" and "sbreg" may only be used to read/write 1, 2 or 4
 * bytes - this may be expanded later.
 * For now "gdb_proxy_probe" only returns current Proxy ID, but does not set
 * a new one (unlike iovar that may do both things)
 */

/* Size of firmware address space */
#define GDB_PROXY_FS_MEM_SIZE ((loff_t)1 << 32)

/* Common part of 'llseek' routine for all files */
static loff_t
gdb_proxy_fs_llseek(struct file *fp, loff_t off, int whence, loff_t file_len)
{
	loff_t pos = -1;

	switch (whence) {
	case SEEK_SET:
		pos = off;
		break;
	case SEEK_CUR:
		pos = fp->f_pos + off;
		break;
	case SEEK_END:
		pos = file_len - off;
		break;
	}
	if ((pos < 0) || (pos > file_len)) {
		return -EINVAL;
	}
	fp->f_pos = pos;
	return pos;
}

/* Common read/write procedure for "gdb_proxy_probe" and "gdb_proxy_stop_count"
 * procfs files
 * fp: file descriptor
 * user_buffer_in: userspace buffer address for write operation, NULL for read
 *	operation
 * user_buffer_out: userspace buffer address for read operation, NULL for write
 *	operation
 * count: maximum number of bytes to read/write
 * position: seek position incremented by length of data read/written
 * iovar: name of iovar being accessed
 * iovar_data_buf: intermediate buffer to store iovar data
 * iovar_data_len: length of data, corresponded to iovar
 * read_params: NULL or address of input parameter for iovar read
 * read_plen: 0 or length of input parameter for iovar read
 * Returns number of bytes read/written or error code
 */
static ssize_t
gdb_proxy_fs_iovar_data_op(struct file *fp, const char __user *user_buffer_in,
	char __user *user_buffer_out, size_t count, loff_t *position,
	const char *iovar, void *iovar_data_buf, size_t iovar_data_len,
	void *read_params, size_t read_plen)
{
	dhd_info_t *dhd = (dhd_info_t *)PDE_DATA(file_inode(fp));
	int err;
	if (count == 0) {
		return 0;
	}
	/* If position out of data length - read nothing */
	if ((*position < 0) || (*position >= (loff_t)iovar_data_len)) {
		return 0;
	}
	/* If buffer end is past structure lenght - truncate it */
	if ((*position + count) > (loff_t)iovar_data_len) {
		count = (size_t)((loff_t)iovar_data_len - *position);
	}
	if (user_buffer_in) {
		/* SET operation */
		/* Read/modify/write if not whole-buffer-operation */
		if ((*position != 0) || (count < iovar_data_len)) {
			err = dhd_bus_iovar_op(&(dhd->pub), iovar,
				(char *)read_params, (uint)read_plen,
				(char *)iovar_data_buf, (uint)iovar_data_len, IOV_GET);
			if (err) {
				return -EPERM;
			}
		}
		if (copy_from_user((char *)iovar_data_buf + (uint)*position, user_buffer_in, count))
		{
			return -EPERM;
		}
		/* This params/plen of NULL/0 is a 'legal fiction', imposed by
		 * strange assert in dhd_bus_iovar_op(). After this strange
		 * assert, arg/arglen is copied to params/plen - and even used
		 * inside iovar handler!
		 */
		err = dhd_bus_iovar_op(&(dhd->pub), iovar, NULL, 0,
			(char *)iovar_data_buf, (uint)iovar_data_len, IOV_SET);
	} else {
		/* GET operation */
		err = dhd_bus_iovar_op(&(dhd->pub), iovar, (char *)read_params, (uint)read_plen,
			(char *)iovar_data_buf, (uint)iovar_data_len, IOV_GET);
	}
	if (err) {
		return -EPERM;
	}
	if (user_buffer_out) {
		if (copy_to_user(user_buffer_out, (char *)iovar_data_buf + (uint)*position, count))
		{
			return -EPERM;
		}
	}
	*position += count;
	return count;
}

/* Read for "gdb_proxy_probe" procfs file */
static ssize_t
gdb_proxy_fs_probe_read(struct file *fp, char __user *user_buffer, size_t count,
	loff_t *position)
{
	uint32 proxy_id = 0;
	dhd_gdb_proxy_probe_data_t probe_data;
	return gdb_proxy_fs_iovar_data_op(fp, NULL, user_buffer, count, position, "gdb_proxy_probe",
		&probe_data, sizeof(probe_data), &proxy_id, sizeof(proxy_id));
}

/* Seek for "gdb_proxy_probe" file */
static loff_t
gdb_proxy_fs_probe_llseek(struct file *fp, loff_t off, int whence)
{
	return gdb_proxy_fs_llseek(fp, off, whence, sizeof(dhd_gdb_proxy_probe_data_t));
}

/* File operations for "gdb_proxy_probe" procfs file */
static const struct file_operations
gdb_proxy_fs_probe_file_ops = {
	.read = gdb_proxy_fs_probe_read,
	.llseek = gdb_proxy_fs_probe_llseek,
};

/* Read for "gdb_proxy_stop_count" procfs file */
static ssize_t
gdb_proxy_fs_stop_count_read(struct file *fp, char __user *user_buffer, size_t count,
	loff_t *position)
{
	uint32 stop_count;
	return gdb_proxy_fs_iovar_data_op(fp, NULL, user_buffer, count, position,
		"gdb_proxy_stop_count", &stop_count, sizeof(stop_count), NULL, 0);
}

/* Write for "gdb_proxy_stop_count" procfs file */
static ssize_t
gdb_proxy_fs_stop_count_write(struct file *fp, const char __user *user_buffer, size_t count,
	loff_t *position)
{
	uint32 stop_count;
	return gdb_proxy_fs_iovar_data_op(fp, user_buffer, NULL, count, position,
		"gdb_proxy_stop_count", &stop_count, sizeof(stop_count), NULL, 0);
}

/* Seek for "gdb_proxy_stop_count" file */
static loff_t
gdb_proxy_fs_stop_count_llseek(struct file *fp, loff_t off, int whence)
{
	return gdb_proxy_fs_llseek(fp, off, whence, sizeof(uint32));
}

/* File operations for "gdb_proxy_stop_count" procfs file */
static const struct file_operations
gdb_proxy_fs_stop_count_file_ops = {
	.read = gdb_proxy_fs_stop_count_read,
	.write = gdb_proxy_fs_stop_count_write,
	.llseek = gdb_proxy_fs_stop_count_llseek,
};

/* Common read/write procedure for "membytes" and "sbreg" procfs files
 * fp: file descriptor
 * buffer_in: userspace buffer address for write operation, NULL for read
 *	operation
 * buffer_out: userspace buffer address for read operation, NULL for write
 *	operation
 * count: maximum number of bytes to read/write
 * position: seek position (interpreted as memory address in firmware address
 *	space),
 *	incremented by length of data read/written
 * iovar: name of iovar being accessed
 * address_first: TRUE if address shall be packed first, FALSE if width
 * Returns number of bytes read/written or error code
 */
static ssize_t
gdb_proxy_fs_iovar_mem_op(struct file *fp, const char __user *user_buffer_in,
	char __user *user_buffer_out, size_t count, loff_t *position,
	const char *iovar, bool address_first)
{
	dhd_info_t *dhd = (dhd_info_t *)PDE_DATA(file_inode(fp));
	uint32 buf[3];
	int err;
	if (count == 0) {
		return 0;
	}
	if ((count > sizeof(uint32)) || (count & (count - 1))) {
		return -EINVAL;
	}
	buf[address_first ? 0 : 1] = (uint32)(*position);
	buf[address_first ? 1 : 0] = (uint32)count;
	if (user_buffer_in) {
		/* SET operation */
		if (copy_from_user(&buf[2], user_buffer_in, count)) {
			return -EPERM;
		}
		/* This params/plen of NULL/0 is a 'legal fiction', imposed by
		 * strange assert in dhd_bus_iovar_op(). After this strange
		 * assert, arg/arglen is copied to params/plen - and even used
		 * inside iovar handler!
		 */
		err = dhd_bus_iovar_op(&(dhd->pub), iovar, NULL, 0, (char *)buf, sizeof(*buf) * 3,
			IOV_SET);
	} else {
		/* GET operation */
		/* This arglen of 8 bytes (where 4 would suffice) is due to
		 * strange requirement of minimum arglen to be 8, hardcoded into
		 * "membytes" iovar definition
		 */
		err = dhd_bus_iovar_op(&(dhd->pub), iovar, (char *)buf, sizeof(*buf) * 2,
			(char *)buf, sizeof(*buf) * 2, IOV_GET);
	}
	if (err) {
		return -EPERM;
	}
	*position += count;
	if (user_buffer_out) {
		if (copy_to_user(user_buffer_out, &buf[0], count)) {
			return -EPERM;
		}
	}
	return count;
}

/* Common seek procedure for "membytes" and "sbreg" procfs files */
static loff_t
gdb_proxy_fs_memory_llseek(struct file *fp, loff_t off, int whence)
{
	return gdb_proxy_fs_llseek(fp, off, whence, GDB_PROXY_FS_MEM_SIZE);
}

/* Read for "membytes" procfs file */
static ssize_t
gdb_proxy_fs_membytes_read(struct file *fp, char __user *user_buffer, size_t count,
	loff_t *position)
{
	return gdb_proxy_fs_iovar_mem_op(fp, NULL, user_buffer, count, position, "membytes", TRUE);
}

/* Write for "membytes" procfs file */
static ssize_t
gdb_proxy_fs_membytes_write(struct file *fp, const char __user *user_buffer, size_t count,
	loff_t *position)
{
	return gdb_proxy_fs_iovar_mem_op(fp, user_buffer, NULL, count, position, "membytes", TRUE);
}

/* File operations for "membytes" procfs file */
static const struct file_operations
gdb_proxy_fs_membytes_file_ops = {
	.read = gdb_proxy_fs_membytes_read,
	.write = gdb_proxy_fs_membytes_write,
	.llseek = gdb_proxy_fs_memory_llseek,
};

/* Read for "sbreg" procfs file */
static ssize_t
gdb_proxy_fs_sbreg_read(struct file *fp, char __user *user_buffer, size_t count, loff_t *position)
{
	return gdb_proxy_fs_iovar_mem_op(fp, NULL, user_buffer, count, position, "sbreg", FALSE);
}

/* Write for "sbreg" procfs file */
static ssize_t
gdb_proxy_fs_sbreg_write(struct file *fp, const char __user *user_buffer, size_t count,
	loff_t *position)
{
	return gdb_proxy_fs_iovar_mem_op(fp, user_buffer, NULL, count, position, "sbreg", FALSE);
}

/* File operations for "sbreg" procfs file */
static const struct file_operations
gdb_proxy_fs_sbreg_file_ops = {
	.read = gdb_proxy_fs_sbreg_read,
	.write = gdb_proxy_fs_sbreg_write,
	.llseek = gdb_proxy_fs_memory_llseek,
};

/* If GDB Proxy procfs files set not yet created for given dhd instance - creates it */
static void
gdb_proxy_fs_try_create(dhd_info_t *dhd, const char *dev_name)
{
	char dir_name[sizeof(dhd->gdb_proxy_fs_root_name)] = "dhd_gdb_proxy_";
	struct proc_dir_entry *root_dentry;
	int i;
	static const struct {
		const char *file_name;
		const struct file_operations *fops;
	} fileinfos[] = {
		{"gdb_proxy_probe", &gdb_proxy_fs_probe_file_ops},
		{"gdb_proxy_stop_count", &gdb_proxy_fs_stop_count_file_ops},
		{"membytes", &gdb_proxy_fs_membytes_file_ops},
		{"sbreg", &gdb_proxy_fs_sbreg_file_ops},
	};
	if (!dev_name || !*dev_name || dhd->gdb_proxy_fs_root) {
		return;
	}
	strlcat_s(dir_name, dev_name, sizeof(dir_name));
	dir_name[sizeof(dir_name) - 1] = 0;
	root_dentry = proc_mkdir(dir_name, NULL);
	if ((root_dentry == NULL) || IS_ERR(root_dentry)) {
		return;
	}
	for (i = 0; i < ARRAYSIZE(fileinfos); ++i) {
		struct proc_dir_entry *file_dentry = proc_create_data(fileinfos[i].file_name,
			S_IRUGO | (fileinfos[i].fops->write ? S_IWUGO : 0), root_dentry,
			fileinfos[i].fops,  dhd);
		if ((file_dentry == NULL) || IS_ERR(file_dentry)) {
			goto fail;
		}
	}
	dhd->gdb_proxy_fs_root = root_dentry;
	memcpy_s(dhd->gdb_proxy_fs_root_name, sizeof(dhd->gdb_proxy_fs_root_name),
		dir_name, sizeof(dhd->gdb_proxy_fs_root_name));
	return;
fail:
	if (root_dentry) {
		remove_proc_subtree(dir_name, NULL);
	}
}

/* If GDB Proxy procfs files set created for given dhd instance - removes it */
static void
gdb_proxy_fs_remove(dhd_info_t *dhd)
{
	if (dhd->gdb_proxy_fs_root) {
		remove_proc_subtree(dhd->gdb_proxy_fs_root_name, NULL);
		dhd->gdb_proxy_fs_root = NULL;
		bzero(dhd->gdb_proxy_fs_root_name, sizeof(dhd->gdb_proxy_fs_root_name));
	}
}
#endif /* defined(GDB_PROXY) && defined(PCIE_FULL_DONGLE) && defined(BCMINTERNAL) */

#ifdef DHD_MAP_LOGGING
/* Will be called from SMMU fault handler */
void
dhd_smmu_fault_handler(uint32 axid, ulong fault_addr)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)g_dhd_pub;
	uint32 irq = (uint32)-1;

	DHD_ERROR(("%s: Trigger SMMU Fault\n", __FUNCTION__));
	DHD_ERROR(("%s: axid:0x%x, fault_addr:0x%lx", __FUNCTION__, axid, fault_addr));
	dhdp->smmu_fault_occurred = TRUE;
#ifdef DNGL_AXI_ERROR_LOGGING
	dhdp->axi_error = TRUE;
	dhdp->axi_err_dump->axid = axid;
	dhdp->axi_err_dump->fault_address = fault_addr;
#endif /* DNGL_AXI_ERROR_LOGGING */

	/* Disable PCIe IRQ */
	dhdpcie_get_pcieirq(dhdp->bus, &irq);
	if (irq != (uint32)-1) {
		disable_irq_nosync(irq);
	}

	/* Take debug information first */
	DHD_OS_WAKE_LOCK(dhdp);
	dhd_prot_smmu_fault_dump(dhdp);
	DHD_OS_WAKE_UNLOCK(dhdp);

	/* Take AXI information if possible */
#ifdef DNGL_AXI_ERROR_LOGGING
#ifdef DHD_USE_WQ_FOR_DNGL_AXI_ERROR
	dhd_axi_error_dispatch(dhdp);
#else
	dhd_axi_error(dhdp);
#endif /* DHD_USE_WQ_FOR_DNGL_AXI_ERROR */
#endif /* DNGL_AXI_ERROR_LOGGING */
}
EXPORT_SYMBOL(dhd_smmu_fault_handler);
#endif /* DHD_MAP_LOGGING */

#ifdef DHD_PKTTS
/* Get pktts flow configuration */
int
dhd_get_pktts_flow(dhd_pub_t *dhdp, void *arg, int len)
{
	dhd_info_t *dhd = dhdp->info;

	if (!arg || len <= (sizeof(pktts_flow_t) * PKTTS_CONFIG_MAX)) {
		return BCME_BADARG;
	}

	return memcpy_s(arg, len, &dhd->config[0], sizeof(dhd->config));
}

/* Set pktts flow configuration */
int
dhd_set_pktts_flow(dhd_pub_t *dhdp, void *params, int plen)
{
	dhd_info_t *dhd = dhdp->info;
	pktts_flow_t *config;
	uint32 checksum = 0;
	int ret = BCME_OK;
	uint32 temp;
	uint32 idx = PKTTS_CONFIG_MAX;
	uint32 num_config = 0;

	if (plen < sizeof(*config)) {
		DHD_ERROR(("dhd_set_pktts_flow: invalid buffer length (%d)\n", plen));
		return BCME_BADLEN;
	}

	config = (pktts_flow_t *)params;

	temp = htonl(config->src_ip);
	checksum ^= bcm_compute_xor32((volatile uint32 *)&temp,
			sizeof(temp) / sizeof(uint32));
	temp = htonl(config->dst_ip);
	checksum ^= bcm_compute_xor32((volatile uint32 *)&temp,
			sizeof(temp) / sizeof(uint32));

	temp = (hton16(config->dst_port) << 16) | hton16(config->src_port);
	checksum ^= bcm_compute_xor32((volatile uint32 *)&temp,
			sizeof(temp) / sizeof(uint32));
	temp = config->proto;
	checksum ^= bcm_compute_xor32((volatile uint32 *)&temp,
			sizeof(temp) / sizeof(uint32));

	/* Look for checksum match: for delete or update */
	dhd_match_pktts_flow(dhdp, checksum, &idx, &num_config);

	/* no matching config */
	if (idx == PKTTS_CONFIG_MAX) {
		if (config->pkt_offset == PKTTS_OFFSET_INVALID) {
			/* no matching config found for deletion */
			return BCME_NOTFOUND;
		}

		/* look for free config space */
		for (idx = 0; idx < PKTTS_CONFIG_MAX; idx++) {
			if (dhd->config[idx].chksum == 0) {
				break;
			}
		}

		if (idx == PKTTS_CONFIG_MAX) {
			/* no config space left */
			return BCME_NORESOURCE;
		}
	}

	if (config->pkt_offset == PKTTS_OFFSET_INVALID) {
		/* reset if pkt_offset is zero */
		memset(&dhd->config[idx], 0, sizeof(dhd->config[idx]));
	} else {
		ret = memcpy_s(&dhd->config[idx], sizeof(dhd->config[idx]),
			config, sizeof(*config));
		if (ret == BCME_OK) {
			dhd->config[idx].chksum = checksum;
		}
	}

	return ret;
}

/**
 * dhd_match_pktts_flow - this api returns matching pktts config against checksum
 *
 * @dhdp: pointer to dhd_pub object
 * @checksum: five tuple checksum
 * @idx: returns index of matching pktts config
 * @num_config: returns number of valid pktts config
 */
pktts_flow_t *
dhd_match_pktts_flow(dhd_pub_t *dhdp, uint32 checksum, uint32 *idx, uint32 *num_config)
{
	dhd_info_t *dhd = dhdp->info;
	pktts_flow_t *flow = NULL;
	uint8 i;

	for (i = 0; i < PKTTS_CONFIG_MAX; i++) {
		if (dhd->config[i].chksum) {
			(*num_config)++;
		}

		if (checksum && (dhd->config[i].chksum == checksum)) {
			flow = &dhd->config[i];
			break;
		}
	}

	/* update matching config index */
	if (idx) {
		*idx = i;
	}

	/* countinue with valid config count */
	for (; i < PKTTS_CONFIG_MAX; i++) {
		if (dhd->config[i].chksum) {
			(*num_config)++;
		}
	}

	return flow;
}

/* Get pktts enab configuration */
int dhd_get_pktts_enab(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;

	return dhd->latency;
}

/* Set pktts enable configuration */
int dhd_set_pktts_enab(dhd_pub_t *dhdp, bool val)
{
	dhd_info_t *dhd = dhdp->info;
	uint32 var_int =  val;
	int ret = BCME_OK;
	uint power_val;

	/* check FW supports pktlat_ipc or pktlat_meta */
	if (!FW_SUPPORTED(dhdp, pktlat_ipc) && !FW_SUPPORTED(dhdp, pktlat_meta)) {
		BCM_REFERENCE(power_val);
		DHD_INFO(("Chip does not support pktlat\n"));
		return ret;
	}
	power_val = 0;
	/* Disabling mpc and PM mode for pktlat */
	ret = dhd_iovar(dhdp, 0, "mpc", (char *)&power_val, sizeof(power_val), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: Unable to set mpc 0, ret=%d\n", __FUNCTION__, ret));
		return ret;
	}
	power_val = PM_OFF;
	ret = dhd_wl_ioctl_cmd(dhdp, WLC_SET_PM, (char *)&power_val, sizeof(power_val),
			TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s: Unable to set PM 0, ret=%d\n", __FUNCTION__, ret));
		return ret;
	}

	ret = dhd_iovar(dhdp, 0, "pktts_enab", (char *)&var_int, sizeof(var_int), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: enabling pktts_enab failed, ret=%d\n", __FUNCTION__, ret));
		return ret;
	}

	dhd->latency = val;

	return 0;
}
#endif /* DHD_PKTTS */

#ifdef DHD_ERPOM
static void
dhd_error_recovery(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_pub_t *dhdp;
	int ret = 0;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	dhdp = &dhd->pub;

	if (!(dhd->dhd_state & DHD_ATTACH_STATE_DONE)) {
		DHD_ERROR(("%s: init not completed, cannot initiate recovery\n",
			__FUNCTION__));
		return;
	}

#ifdef BT_OVER_PCIE
	if (dhdp->dongle_trap_due_to_bt) {
		DHD_ERROR(("WLAN trapped due to BT, toggle REG_ON\n"));
		/* toggle REG_ON */
		dhdp->pom_toggle_reg_on(WLAN_FUNC_ID, BY_WLAN_DUE_TO_BT);
		return;
	}
#endif /* BT_OVER_PCIE */

	ret = dhd_bus_perform_flr_with_quiesce(dhdp, dhdp->bus, FALSE);
	if (ret != BCME_DNGL_DEVRESET) {
		DHD_ERROR(("%s: dhd_bus_perform_flr_with_quiesce failed with ret: %d,"
			"toggle REG_ON\n", __FUNCTION__, ret));
		/* toggle REG_ON */
		dhdp->pom_toggle_reg_on(WLAN_FUNC_ID, BY_WLAN_DUE_TO_WLAN);
		return;
	}
}

void
dhd_schedule_reset(dhd_pub_t *dhdp)
{
	if (dhdp->enable_erpom) {
		dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, NULL,
			DHD_WQ_WORK_ERROR_RECOVERY, dhd_error_recovery, DHD_WQ_WORK_PRIORITY_HIGH);
	}
}
#endif /* DHD_ERPOM */

#ifdef DHD_PKT_LOGGING
int
dhd_pktlog_debug_dump(dhd_pub_t *dhdp)
{
	struct net_device *primary_ndev;
	struct bcm_cfg80211 *cfg;
	unsigned long flags = 0;

	primary_ndev = dhd_linux_get_primary_netdev(dhdp);
	if (!primary_ndev) {
		DHD_ERROR(("%s: Cannot find primary netdev\n", __FUNCTION__));
		return BCME_ERROR;
	}

	cfg = wl_get_cfg(primary_ndev);
	if (!cfg) {
		DHD_ERROR(("%s: Cannot find cfg\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_GENERAL_LOCK(dhdp, flags);
	if (DHD_BUS_BUSY_CHECK_IN_HALDUMP(dhdp)) {
		DHD_GENERAL_UNLOCK(dhdp, flags);
		DHD_ERROR(("%s: HAL dump is already triggered \n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_BUS_BUSY_SET_IN_HALDUMP(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
	DHD_OS_WAKE_LOCK(dhdp);

	if (wl_cfg80211_is_hal_started(cfg)) {
		dhdp->pktlog_debug = TRUE;
		dhd_dbg_send_urgent_evt(dhdp, NULL, 0);
	} else {
		DHD_ERROR(("[DUMP] %s: HAL Not started. skip urgent event\n", __FUNCTION__));
	}
	DHD_OS_WAKE_UNLOCK(dhdp);
	/* In case of dhd_os_busbusy_wait_bitmask() timeout,
	 * hal dump bit will not be cleared. Hence clearing it here.
	 */
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_HALDUMP(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);

	return BCME_OK;
}

void
dhd_pktlog_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (dhd_pktlog_dump_write_file(&dhd->pub)) {
		DHD_ERROR(("%s: writing pktlog dump file failed\n", __FUNCTION__));
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

#ifdef DHDTCPSYNC_FLOOD_BLK
static void dhd_blk_tsfl_handler(struct work_struct * work)
{
	dhd_if_t *ifp = NULL;
	dhd_pub_t *dhdp = NULL;
	/* Ignore compiler warnings due to -Werror=cast-qual */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	ifp = container_of(work, dhd_if_t, blk_tsfl_work);
	GCC_DIAGNOSTIC_POP();

	if (ifp) {
		dhdp = &ifp->info->pub;
		if (dhdp) {
			if ((dhdp->op_mode & DHD_FLAG_P2P_GO_MODE)||
				(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
				DHD_ERROR(("Disassoc due to TCP SYNC FLOOD ATTACK\n"));
				wl_cfg80211_del_all_sta(ifp->net, WLAN_REASON_UNSPECIFIED);
			} else if ((dhdp->op_mode & DHD_FLAG_P2P_GC_MODE)||
				(dhdp->op_mode & DHD_FLAG_STA_MODE)) {
				DHD_ERROR(("Diconnect due to TCP SYNC FLOOD ATTACK\n"));
				wl_cfg80211_disassoc(ifp->net, WLAN_REASON_UNSPECIFIED);
			}
			ifp->disconnect_tsync_flood = TRUE;
		}
	}
}
void dhd_reset_tcpsync_info_by_ifp(dhd_if_t *ifp)
{
	ifp->tsync_rcvd = 0;
	ifp->tsyncack_txed = 0;
	ifp->last_sync = DIV_U64_BY_U32(OSL_LOCALTIME_NS(), NSEC_PER_SEC);
}
void dhd_reset_tcpsync_info_by_dev(struct net_device *dev)
{
	dhd_if_t *ifp = NULL;
	if (dev) {
		ifp = DHD_DEV_IFP(dev);
	}
	if (ifp) {
		ifp->tsync_rcvd = 0;
		ifp->tsyncack_txed = 0;
		ifp->last_sync = DIV_U64_BY_U32(OSL_LOCALTIME_NS(), NSEC_PER_SEC);
		ifp->tsync_per_sec = 0;
		ifp->disconnect_tsync_flood = FALSE;
	}
}
#endif /* DHDTCPSYNC_FLOOD_BLK */

#ifdef DHD_4WAYM4_FAIL_DISCONNECT
static void dhd_m4_state_handler(struct work_struct *work)
{
	dhd_if_t *ifp = NULL;
	/* Ignore compiler warnings due to -Werror=cast-qual */
	struct delayed_work *dw = to_delayed_work(work);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	ifp = container_of(dw, dhd_if_t, m4state_work);
	GCC_DIAGNOSTIC_POP();

	if (ifp && ifp->net &&
		(OSL_ATOMIC_READ(ifp->info->pub->osh, &ifp->m4state) == M4_TXFAILED)) {
		DHD_ERROR(("Disassoc for 4WAY_HANDSHAKE_TIMEOUT at %s\n",
				ifp->net->name));
		wl_cfg80211_disassoc(ifp->net, WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT);
	}
}

void
dhd_eap_txcomplete(dhd_pub_t *dhdp, void *txp, bool success, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh;
	uint16 type;

	if (!success) {
		/* XXX where does this stuff belong to? */
		dhd_prot_hdrpull(dhdp, NULL, txp, NULL, NULL);

		/* XXX Use packet tag when it is available to identify its type */
		eh = (struct ether_header *)PKTDATA(dhdp->osh, txp);
		type  = ntoh16(eh->ether_type);
		if (type == ETHER_TYPE_802_1X) {
			if (dhd_is_4way_msg((uint8 *)eh) == EAPOL_4WAY_M4) {
				dhd_if_t *ifp = NULL;
				ifp = dhd->iflist[ifidx];
				if (!ifp || !ifp->net) {
					return;
				}

				DHD_INFO(("%s: M4 TX failed on %d.\n",
					__FUNCTION__, ifidx));

				OSL_ATOMIC_SET(dhdp->osh, &ifp->m4state, M4_TXFAILED);
				schedule_delayed_work(&ifp->m4state_work,
					msecs_to_jiffies(MAX_4WAY_TIMEOUT_MS));
			}
		}
	}
}

void
dhd_cleanup_m4_state_work(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhdinfo;
	dhd_if_t *ifp;

	if ((ifidx < 0) || (ifidx >= DHD_MAX_IFS)) {
		DHD_ERROR(("%s: invalid ifidx %d\n", __FUNCTION__, ifidx));
		return;
	}

	dhdinfo = (dhd_info_t *)(dhdp->info);
	if (!dhdinfo) {
		DHD_ERROR(("%s: dhdinfo is NULL\n", __FUNCTION__));
		return;
	}

	ifp = dhdinfo->iflist[ifidx];
	if (ifp) {
		cancel_delayed_work_sync(&ifp->m4state_work);
	}
}
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */

#ifdef BIGDATA_SOFTAP
void dhd_schedule_gather_ap_stadata(void *bcm_cfg, void *ndev, const wl_event_msg_t *e)
{
	struct bcm_cfg80211 *cfg;
	dhd_pub_t *dhdp;
	ap_sta_wq_data_t *p_wq_data;

	if  (!bcm_cfg || !ndev || !e) {
		WL_ERR(("bcm_cfg=%p ndev=%p e=%p\n", bcm_cfg, ndev, e));
		return;
	}

	cfg = (struct bcm_cfg80211 *)bcm_cfg;
	dhdp = (dhd_pub_t *)cfg->pub;

	if (!dhdp || !cfg->ap_sta_info) {
		WL_ERR(("dhdp=%p ap_sta_info=%p\n", dhdp, cfg->ap_sta_info));
		return;
	}

	p_wq_data = (ap_sta_wq_data_t *)MALLOCZ(dhdp->osh, sizeof(ap_sta_wq_data_t));
	if (unlikely(!p_wq_data)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"ap_sta_wq_data_t\n", __FUNCTION__));
		return;
	}

	mutex_lock(&cfg->ap_sta_info->wq_data_sync);

	memcpy(&p_wq_data->e, e, sizeof(wl_event_msg_t));
	p_wq_data->dhdp = dhdp;
	p_wq_data->bcm_cfg = cfg;
	p_wq_data->ndev = (struct net_device *)ndev;

	mutex_unlock(&cfg->ap_sta_info->wq_data_sync);

	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
			p_wq_data, DHD_WQ_WORK_GET_BIGDATA_AP,
			wl_gather_ap_stadata, DHD_WQ_WORK_PRIORITY_HIGH);

}
#endif /* BIGDATA_SOFTAP */

void
get_debug_dump_time(char *str)
{
	struct osl_timespec curtime;
	unsigned long local_time;
	struct rtc_time tm;

	if (!strlen(str)) {
		osl_do_gettimeofday(&curtime);
		local_time = (u32)(curtime.tv_sec -
				(sys_tz.tz_minuteswest * DHD_LOG_DUMP_TS_MULTIPLIER_VALUE));
		rtc_time_to_tm(local_time, &tm);
		snprintf(str, DEBUG_DUMP_TIME_BUF_LEN, DHD_LOG_DUMP_TS_FMT_YYMMDDHHMMSSMSMS,
				tm.tm_year - 100, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
				tm.tm_sec, (int)(curtime.tv_usec/NSEC_PER_USEC));
	}
}

void
clear_debug_dump_time(char *str)
{
	memset(str, 0, DEBUG_DUMP_TIME_BUF_LEN);
}
#if defined(WL_CFGVENDOR_SEND_HANG_EVENT) || defined(DHD_PKT_LOGGING)
void
copy_debug_dump_time(char *dest, char *src)
{
	memcpy(dest, src, DEBUG_DUMP_TIME_BUF_LEN);
}
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT || DHD_PKT_LOGGING */

/*
 * DHD RING
 */
#define DHD_RING_ERR_INTERNAL(fmt, ...) DHD_ERROR(("EWPF-" fmt, ##__VA_ARGS__))
#define DHD_RING_TRACE_INTERNAL(fmt, ...) DHD_INFO(("EWPF-" fmt, ##__VA_ARGS__))

#define DHD_RING_ERR(x) DHD_RING_ERR_INTERNAL x
#define DHD_RING_TRACE(x) DHD_RING_TRACE_INTERNAL x

#define DHD_RING_MAGIC 0x20170910
#define DHD_RING_IDX_INVALID	0xffffffff

#define DHD_RING_SYNC_LOCK_INIT(osh)		osl_spin_lock_init(osh)
#define DHD_RING_SYNC_LOCK_DEINIT(osh, lock)	osl_spin_lock_deinit(osh, lock)
#define DHD_RING_SYNC_LOCK(lock, flags)		(flags) = osl_spin_lock(lock)
#define DHD_RING_SYNC_UNLOCK(lock, flags)	osl_spin_unlock(lock, flags)

typedef struct {
	uint32 elem_size;
	uint32 elem_cnt;
	uint32 write_idx;	/* next write index, -1 : not started */
	uint32 read_idx;	/* next read index, -1 : not start */

	/* protected elements during serialization */
	int lock_idx;	/* start index of locked, element will not be overried */
	int lock_count; /* number of locked, from lock idx */

	/* saved data elements */
	void *elem;
} dhd_fixed_ring_info_t;

typedef struct {
	uint32 elem_size;
	uint32 elem_cnt;
	uint32 idx;		/* -1 : not started */
	uint32 rsvd;		/* reserved for future use */

	/* protected elements during serialization */
	atomic_t ring_locked;
	/* check the overwriting */
	uint32 ring_overwrited;

	/* saved data elements */
	void *elem;
} dhd_singleidx_ring_info_t;

typedef struct {
	uint32 magic;
	uint32 type;
	void *ring_sync; /* spinlock for sync */
	union {
		dhd_fixed_ring_info_t fixed;
		dhd_singleidx_ring_info_t single;
	};
} dhd_ring_info_t;

uint32
dhd_ring_get_hdr_size(void)
{
	return sizeof(dhd_ring_info_t);
}

void *
dhd_ring_init(dhd_pub_t *dhdp, uint8 *buf, uint32 buf_size, uint32 elem_size,
	uint32 elem_cnt, uint32 type)
{
	dhd_ring_info_t *ret_ring;

	if (!buf) {
		DHD_RING_ERR(("NO RING BUFFER\n"));
		return NULL;
	}

	if (buf_size < dhd_ring_get_hdr_size() + elem_size * elem_cnt) {
		DHD_RING_ERR(("RING SIZE IS TOO SMALL\n"));
		return NULL;
	}

	if (type != DHD_RING_TYPE_FIXED && type != DHD_RING_TYPE_SINGLE_IDX) {
		DHD_RING_ERR(("UNSUPPORTED RING TYPE\n"));
		return NULL;
	}

	ret_ring = (dhd_ring_info_t *)buf;
	ret_ring->type = type;
	ret_ring->ring_sync = (void *)DHD_RING_SYNC_LOCK_INIT(dhdp->osh);
	ret_ring->magic = DHD_RING_MAGIC;

	if (type == DHD_RING_TYPE_FIXED) {
		ret_ring->fixed.read_idx = DHD_RING_IDX_INVALID;
		ret_ring->fixed.write_idx = DHD_RING_IDX_INVALID;
		ret_ring->fixed.lock_idx = DHD_RING_IDX_INVALID;
		ret_ring->fixed.elem = buf + sizeof(dhd_ring_info_t);
		ret_ring->fixed.elem_size = elem_size;
		ret_ring->fixed.elem_cnt = elem_cnt;
	} else {
		ret_ring->single.idx = DHD_RING_IDX_INVALID;
		atomic_set(&ret_ring->single.ring_locked, 0);
		ret_ring->single.ring_overwrited = 0;
		ret_ring->single.rsvd = 0;
		ret_ring->single.elem = buf + sizeof(dhd_ring_info_t);
		ret_ring->single.elem_size = elem_size;
		ret_ring->single.elem_cnt = elem_cnt;
	}

	return ret_ring;
}

void
dhd_ring_deinit(dhd_pub_t *dhdp, void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	if (!ring) {
		return;
	}

	if (ring->magic != DHD_RING_MAGIC) {
		return;
	}

	if (ring->type != DHD_RING_TYPE_FIXED &&
		ring->type != DHD_RING_TYPE_SINGLE_IDX) {
		return;
	}

	DHD_RING_SYNC_LOCK_DEINIT(dhdp->osh, ring->ring_sync);
	ring->ring_sync = NULL;
	if (ring->type == DHD_RING_TYPE_FIXED) {
		dhd_fixed_ring_info_t *fixed = &ring->fixed;
		memset(fixed->elem, 0, fixed->elem_size * fixed->elem_cnt);
		fixed->elem_size = fixed->elem_cnt = 0;
	} else {
		dhd_singleidx_ring_info_t *single = &ring->single;
		memset(single->elem, 0, single->elem_size * single->elem_cnt);
		single->elem_size = single->elem_cnt = 0;
	}
	ring->type = 0;
	ring->magic = 0;
}

static inline uint32
__dhd_ring_ptr2idx(void *ring, void *ptr, char *sig, uint32 type)
{
	uint32 diff;
	uint32 ret_idx = (uint32)DHD_RING_IDX_INVALID;
	uint32 elem_size, elem_cnt;
	void *elem;

	if (type == DHD_RING_TYPE_FIXED) {
		dhd_fixed_ring_info_t *fixed = (dhd_fixed_ring_info_t *)ring;
		elem_size = fixed->elem_size;
		elem_cnt = fixed->elem_cnt;
		elem = fixed->elem;
	} else if (type == DHD_RING_TYPE_SINGLE_IDX) {
		dhd_singleidx_ring_info_t *single = (dhd_singleidx_ring_info_t *)ring;
		elem_size = single->elem_size;
		elem_cnt = single->elem_cnt;
		elem = single->elem;
	} else {
		DHD_RING_ERR(("UNSUPPORTED RING TYPE %d\n", type));
		return ret_idx;
	}

	if (ptr < elem) {
		DHD_RING_ERR(("INVALID POINTER %s:%p, ring->elem:%p\n", sig, ptr, elem));
		return ret_idx;
	}
	diff = (uint32)((uint8 *)ptr - (uint8 *)elem);
	if (diff % elem_size != 0) {
		DHD_RING_ERR(("INVALID POINTER %s:%p, ring->elem:%p\n", sig, ptr, elem));
		return ret_idx;
	}
	ret_idx = diff / elem_size;
	if (ret_idx >= elem_cnt) {
		DHD_RING_ERR(("INVALID POINTER max:%d cur:%d\n", elem_cnt, ret_idx));
	}
	return ret_idx;
}

/* Sub functions for fixed ring */
/* get counts between two indexes of ring buffer (internal only) */
static inline int
__dhd_fixed_ring_get_count(dhd_fixed_ring_info_t *ring, int start, int end)
{
	if (start == DHD_RING_IDX_INVALID || end == DHD_RING_IDX_INVALID) {
		return 0;
	}

	return (ring->elem_cnt + end - start) % ring->elem_cnt + 1;
}

static inline int
__dhd_fixed_ring_get_cur_size(dhd_fixed_ring_info_t *ring)
{
	return __dhd_fixed_ring_get_count(ring, ring->read_idx, ring->write_idx);
}

static inline void *
__dhd_fixed_ring_get_first(dhd_fixed_ring_info_t *ring)
{
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		return NULL;
	}
	return (uint8 *)ring->elem + (ring->elem_size * ring->read_idx);
}

static inline void
__dhd_fixed_ring_free_first(dhd_fixed_ring_info_t *ring)
{
	uint32 next_idx;

	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return;
	}

	next_idx = (ring->read_idx + 1) % ring->elem_cnt;
	if (ring->read_idx == ring->write_idx) {
		/* Become empty */
		ring->read_idx = ring->write_idx = DHD_RING_IDX_INVALID;
		return;
	}

	ring->read_idx = next_idx;
	return;
}

static inline void *
__dhd_fixed_ring_get_last(dhd_fixed_ring_info_t *ring)
{
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		return NULL;
	}
	return (uint8 *)ring->elem + (ring->elem_size * ring->write_idx);
}

static inline void *
__dhd_fixed_ring_get_empty(dhd_fixed_ring_info_t *ring)
{
	uint32 tmp_idx;

	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		ring->read_idx = ring->write_idx = 0;
		return (uint8 *)ring->elem;
	}

	/* check next index is not locked */
	tmp_idx = (ring->write_idx + 1) % ring->elem_cnt;
	if (ring->lock_idx == tmp_idx) {
		return NULL;
	}

	ring->write_idx = tmp_idx;
	if (ring->write_idx == ring->read_idx) {
		/* record is full, drop oldest one */
		ring->read_idx = (ring->read_idx + 1) % ring->elem_cnt;

	}
	return (uint8 *)ring->elem + (ring->elem_size * ring->write_idx);
}

static inline void *
__dhd_fixed_ring_get_next(dhd_fixed_ring_info_t *ring, void *prev, uint32 type)
{
	uint32 cur_idx;

	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return NULL;
	}

	cur_idx = __dhd_ring_ptr2idx(ring, prev, "NEXT", type);
	if (cur_idx >= ring->elem_cnt) {
		return NULL;
	}

	if (cur_idx == ring->write_idx) {
		/* no more new record */
		return NULL;
	}

	cur_idx = (cur_idx + 1) % ring->elem_cnt;
	return (uint8 *)ring->elem + ring->elem_size * cur_idx;
}

static inline void *
__dhd_fixed_ring_get_prev(dhd_fixed_ring_info_t *ring, void *prev, uint32 type)
{
	uint32 cur_idx;

	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return NULL;
	}
	cur_idx = __dhd_ring_ptr2idx(ring, prev, "PREV", type);
	if (cur_idx >= ring->elem_cnt) {
		return NULL;
	}
	if (cur_idx == ring->read_idx) {
		/* no more new record */
		return NULL;
	}

	cur_idx = (cur_idx + ring->elem_cnt - 1) % ring->elem_cnt;
	return (uint8 *)ring->elem + ring->elem_size * cur_idx;
}

static inline void
__dhd_fixed_ring_lock(dhd_fixed_ring_info_t *ring, void *first_ptr, void *last_ptr, uint32 type)
{
	uint32 first_idx;
	uint32 last_idx;
	uint32 ring_filled_cnt;
	uint32 tmp_cnt;

	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return;
	}

	if (first_ptr) {
		first_idx = __dhd_ring_ptr2idx(ring, first_ptr, "LCK FIRST", type);
		if (first_idx >= ring->elem_cnt) {
			return;
		}
	} else {
		first_idx = ring->read_idx;
	}

	if (last_ptr) {
		last_idx = __dhd_ring_ptr2idx(ring, last_ptr, "LCK LAST", type);
		if (last_idx >= ring->elem_cnt) {
			return;
		}
	} else {
		last_idx = ring->write_idx;
	}

	ring_filled_cnt = __dhd_fixed_ring_get_count(ring, ring->read_idx, ring->write_idx);
	tmp_cnt = __dhd_fixed_ring_get_count(ring, ring->read_idx, first_idx);
	if (tmp_cnt > ring_filled_cnt) {
		DHD_RING_ERR(("LOCK FIRST IS TO EMPTY ELEM: write: %d read: %d cur:%d\n",
			ring->write_idx, ring->read_idx, first_idx));
		return;
	}

	tmp_cnt = __dhd_fixed_ring_get_count(ring, ring->read_idx, last_idx);
	if (tmp_cnt > ring_filled_cnt) {
		DHD_RING_ERR(("LOCK LAST IS TO EMPTY ELEM: write: %d read: %d cur:%d\n",
			ring->write_idx, ring->read_idx, last_idx));
		return;
	}

	ring->lock_idx = first_idx;
	ring->lock_count = __dhd_fixed_ring_get_count(ring, first_idx, last_idx);
	return;
}

static inline void
__dhd_fixed_ring_lock_free(dhd_fixed_ring_info_t *ring)
{
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return;
	}

	ring->lock_idx = DHD_RING_IDX_INVALID;
	ring->lock_count = 0;
	return;
}
static inline void *
__dhd_fixed_ring_lock_get_first(dhd_fixed_ring_info_t *ring)
{
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return NULL;
	}
	if (ring->lock_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("NO LOCK POINT\n"));
		return NULL;
	}
	return (uint8 *)ring->elem + ring->elem_size * ring->lock_idx;
}

static inline void *
__dhd_fixed_ring_lock_get_last(dhd_fixed_ring_info_t *ring)
{
	int lock_last_idx;
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return NULL;
	}
	if (ring->lock_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("NO LOCK POINT\n"));
		return NULL;
	}

	lock_last_idx = (ring->lock_idx + ring->lock_count - 1) % ring->elem_cnt;
	return (uint8 *)ring->elem + ring->elem_size * lock_last_idx;
}

static inline int
__dhd_fixed_ring_lock_get_count(dhd_fixed_ring_info_t *ring)
{
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return BCME_ERROR;
	}
	if (ring->lock_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("NO LOCK POINT\n"));
		return BCME_ERROR;
	}
	return ring->lock_count;
}

static inline void
__dhd_fixed_ring_lock_free_first(dhd_fixed_ring_info_t *ring)
{
	if (ring->read_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return;
	}
	if (ring->lock_idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("NO LOCK POINT\n"));
		return;
	}

	ring->lock_count--;
	if (ring->lock_count <= 0) {
		ring->lock_idx = DHD_RING_IDX_INVALID;
	} else {
		ring->lock_idx = (ring->lock_idx + 1) % ring->elem_cnt;
	}
	return;
}

static inline void
__dhd_fixed_ring_set_read_idx(dhd_fixed_ring_info_t *ring, uint32 idx)
{
	ring->read_idx = idx;
}

static inline void
__dhd_fixed_ring_set_write_idx(dhd_fixed_ring_info_t *ring, uint32 idx)
{
	ring->write_idx = idx;
}

static inline uint32
__dhd_fixed_ring_get_read_idx(dhd_fixed_ring_info_t *ring)
{
	return ring->read_idx;
}

static inline uint32
__dhd_fixed_ring_get_write_idx(dhd_fixed_ring_info_t *ring)
{
	return ring->write_idx;
}

/* Sub functions for single index ring */
static inline void *
__dhd_singleidx_ring_get_first(dhd_singleidx_ring_info_t *ring)
{
	uint32 tmp_idx = 0;

	if (ring->idx == DHD_RING_IDX_INVALID) {
		return NULL;
	}

	if (ring->ring_overwrited) {
		tmp_idx = (ring->idx + 1) % ring->elem_cnt;
	}

	return (uint8 *)ring->elem + (ring->elem_size * tmp_idx);
}

static inline void *
__dhd_singleidx_ring_get_last(dhd_singleidx_ring_info_t *ring)
{
	if (ring->idx == DHD_RING_IDX_INVALID) {
		return NULL;
	}

	return (uint8 *)ring->elem + (ring->elem_size * ring->idx);
}

static inline void *
__dhd_singleidx_ring_get_empty(dhd_singleidx_ring_info_t *ring)
{
	if (ring->idx == DHD_RING_IDX_INVALID) {
		ring->idx = 0;
		return (uint8 *)ring->elem;
	}

	/* check the lock is held */
	if (atomic_read(&ring->ring_locked)) {
		return NULL;
	}

	/* check the index rollover */
	if (!ring->ring_overwrited && ring->idx == (ring->elem_cnt - 1)) {
		ring->ring_overwrited = 1;
	}

	ring->idx = (ring->idx + 1) % ring->elem_cnt;

	return (uint8 *)ring->elem + (ring->elem_size * ring->idx);
}

static inline void *
__dhd_singleidx_ring_get_next(dhd_singleidx_ring_info_t *ring, void *prev, uint32 type)
{
	uint32 cur_idx;

	if (ring->idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return NULL;
	}

	cur_idx = __dhd_ring_ptr2idx(ring, prev, "NEXT", type);
	if (cur_idx >= ring->elem_cnt) {
		return NULL;
	}

	if (cur_idx == ring->idx) {
		/* no more new record */
		return NULL;
	}

	cur_idx = (cur_idx + 1) % ring->elem_cnt;

	return (uint8 *)ring->elem + ring->elem_size * cur_idx;
}

static inline void *
__dhd_singleidx_ring_get_prev(dhd_singleidx_ring_info_t *ring, void *prev, uint32 type)
{
	uint32 cur_idx;

	if (ring->idx == DHD_RING_IDX_INVALID) {
		DHD_RING_ERR(("EMPTY RING\n"));
		return NULL;
	}
	cur_idx = __dhd_ring_ptr2idx(ring, prev, "PREV", type);
	if (cur_idx >= ring->elem_cnt) {
		return NULL;
	}

	if (!ring->ring_overwrited && cur_idx == 0) {
		/* no more new record */
		return NULL;
	}

	cur_idx = (cur_idx + ring->elem_cnt - 1) % ring->elem_cnt;
	if (ring->ring_overwrited && cur_idx == ring->idx) {
		/* no more new record */
		return NULL;
	}

	return (uint8 *)ring->elem + ring->elem_size * cur_idx;
}

static inline void
__dhd_singleidx_ring_whole_lock(dhd_singleidx_ring_info_t *ring)
{
	if (!atomic_read(&ring->ring_locked)) {
		atomic_set(&ring->ring_locked, 1);
	}
}

static inline void
__dhd_singleidx_ring_whole_unlock(dhd_singleidx_ring_info_t *ring)
{
	if (atomic_read(&ring->ring_locked)) {
		atomic_set(&ring->ring_locked, 0);
	}
}

/* Get first element : oldest element */
void *
dhd_ring_get_first(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_get_first(&ring->fixed);
	}
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		ret = __dhd_singleidx_ring_get_first(&ring->single);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

/* Free first element : oldest element */
void
dhd_ring_free_first(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		__dhd_fixed_ring_free_first(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

void
dhd_ring_set_read_idx(void *_ring, uint32 read_idx)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		__dhd_fixed_ring_set_read_idx(&ring->fixed, read_idx);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

void
dhd_ring_set_write_idx(void *_ring, uint32 write_idx)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		__dhd_fixed_ring_set_write_idx(&ring->fixed, write_idx);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

uint32
dhd_ring_get_read_idx(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	uint32 read_idx = DHD_RING_IDX_INVALID;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return read_idx;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		read_idx = __dhd_fixed_ring_get_read_idx(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);

	return read_idx;
}

uint32
dhd_ring_get_write_idx(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	uint32 write_idx = DHD_RING_IDX_INVALID;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return write_idx;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		write_idx = __dhd_fixed_ring_get_write_idx(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);

	return write_idx;
}

/* Get latest element */
void *
dhd_ring_get_last(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_get_last(&ring->fixed);
	}
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		ret = __dhd_singleidx_ring_get_last(&ring->single);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

/* Get next point can be written
 * will overwrite which doesn't read
 * will return NULL if next pointer is locked.
 */
void *
dhd_ring_get_empty(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_get_empty(&ring->fixed);
	}
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		ret = __dhd_singleidx_ring_get_empty(&ring->single);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

void *
dhd_ring_get_next(void *_ring, void *cur)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_get_next(&ring->fixed, cur, ring->type);
	}
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		ret = __dhd_singleidx_ring_get_next(&ring->single, cur, ring->type);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

void *
dhd_ring_get_prev(void *_ring, void *cur)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_get_prev(&ring->fixed, cur, ring->type);
	}
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		ret = __dhd_singleidx_ring_get_prev(&ring->single, cur, ring->type);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

int
dhd_ring_get_cur_size(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	int cnt = 0;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return cnt;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		cnt = __dhd_fixed_ring_get_cur_size(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return cnt;
}

/* protect element between lock_ptr and write_idx */
void
dhd_ring_lock(void *_ring, void *first_ptr, void *last_ptr)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		__dhd_fixed_ring_lock(&ring->fixed, first_ptr, last_ptr, ring->type);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

/* free all lock */
void
dhd_ring_lock_free(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		__dhd_fixed_ring_lock_free(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

void *
dhd_ring_lock_get_first(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_lock_get_first(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

void *
dhd_ring_lock_get_last(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	void *ret = NULL;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return NULL;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_lock_get_last(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

int
dhd_ring_lock_get_count(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	int ret = BCME_ERROR;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return ret;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		ret = __dhd_fixed_ring_lock_get_count(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
	return ret;
}

/* free first locked element */
void
dhd_ring_lock_free_first(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_FIXED) {
		__dhd_fixed_ring_lock_free_first(&ring->fixed);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

void
dhd_ring_whole_lock(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		__dhd_singleidx_ring_whole_lock(&ring->single);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}

void
dhd_ring_whole_unlock(void *_ring)
{
	dhd_ring_info_t *ring = (dhd_ring_info_t *)_ring;
	unsigned long flags;

	if (!ring || ring->magic != DHD_RING_MAGIC) {
		DHD_RING_ERR(("%s :INVALID RING INFO\n", __FUNCTION__));
		return;
	}

	DHD_RING_SYNC_LOCK(ring->ring_sync, flags);
	if (ring->type == DHD_RING_TYPE_SINGLE_IDX) {
		__dhd_singleidx_ring_whole_unlock(&ring->single);
	}
	DHD_RING_SYNC_UNLOCK(ring->ring_sync, flags);
}
/* END of DHD RING */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0))
#define DHD_VFS_INODE(dir) (dir->d_inode)
#else
#define DHD_VFS_INODE(dir) d_inode(dir)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
#define DHD_VFS_UNLINK(dir, b, c) vfs_unlink(DHD_VFS_INODE(dir), b)
#else
#define DHD_VFS_UNLINK(dir, b, c) vfs_unlink(DHD_VFS_INODE(dir), b, c)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) */

#if ((defined DHD_DUMP_MNGR) || (defined DNGL_AXI_ERROR_LOGGING))
int
dhd_file_delete(char *path)
{
	struct path file_path;
	int err;
	struct dentry *dir;

	err = kern_path(path, 0, &file_path);

	if (err < 0) {
		DHD_ERROR(("Failed to get kern-path delete file: %s error: %d\n", path, err));
		return err;
	}
	if (
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
		!d_is_file(file_path.dentry) ||
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 1, 0))
		d_really_is_negative(file_path.dentry) ||
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(4, 1, 0) */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0) */
		FALSE)
	{
		err = -EINVAL;
	} else {
		dir = dget_parent(file_path.dentry);

		if (!IS_ERR(dir)) {
			err = DHD_VFS_UNLINK(dir, file_path.dentry, NULL);
			dput(dir);
		} else {
			err = PTR_ERR(dir);
		}
	}

	path_put(&file_path);

	if (err < 0) {
		DHD_ERROR(("Failed to delete file: %s error: %d\n", path, err));
	}

	return err;
}
#endif

#ifdef DHD_DUMP_MNGR
static int
dhd_dump_file_manage_idx(dhd_dump_file_manage_t *fm_ptr, char *fname)
{
	int i;
	int fm_idx = -1;

	for (i = 0; i < DHD_DUMP_TYPE_COUNT_MAX; i++) {
		/* XXX dump file manager enqueues the type name to empty slot,
		 * so it's impossible that empty slot is in the middle.
		 */
		if (strlen(fm_ptr->elems[i].type_name) == 0) {
			fm_idx = i;
			break;
		}
		if (!(strncmp(fname, fm_ptr->elems[i].type_name, strlen(fname)))) {
			fm_idx = i;
			break;
		}
	}

	if (fm_idx == -1) {
		return fm_idx;
	}

	if (strlen(fm_ptr->elems[fm_idx].type_name) == 0) {
		strncpy(fm_ptr->elems[fm_idx].type_name, fname, DHD_DUMP_TYPE_NAME_SIZE);
		fm_ptr->elems[fm_idx].type_name[DHD_DUMP_TYPE_NAME_SIZE - 1] = '\0';
		fm_ptr->elems[fm_idx].file_idx = 0;
	}

	return fm_idx;
}

/*
 * dhd_dump_file_manage_enqueue - enqueue dump file path
 * and delete odest file if file count is max.
 */
void
dhd_dump_file_manage_enqueue(dhd_pub_t *dhd, char *dump_path, char *fname)
{
	int fm_idx;
	int fp_idx;
	dhd_dump_file_manage_t *fm_ptr;
	DFM_elem_t *elem;

	if (!dhd || !dhd->dump_file_manage) {
		DHD_ERROR(("%s(): dhdp=%p dump_file_manage=%p\n",
			__FUNCTION__, dhd, (dhd ? dhd->dump_file_manage : NULL)));
		return;
	}

	fm_ptr = dhd->dump_file_manage;

	/* find file_manage idx */
	DHD_INFO(("%s(): fname: %s dump_path: %s\n", __FUNCTION__, fname, dump_path));
	if ((fm_idx = dhd_dump_file_manage_idx(fm_ptr, fname)) < 0) {
		DHD_ERROR(("%s(): Out of file manager entries, fname: %s\n",
			__FUNCTION__, fname));
		return;
	}

	elem = &fm_ptr->elems[fm_idx];
	fp_idx = elem->file_idx;
	DHD_INFO(("%s(): fm_idx: %d fp_idx: %d path: %s\n",
		__FUNCTION__, fm_idx, fp_idx, elem->file_path[fp_idx]));

	/* delete oldest file */
	if (strlen(elem->file_path[fp_idx]) != 0) {
		if (dhd_file_delete(elem->file_path[fp_idx]) < 0) {
			DHD_ERROR(("%s(): Failed to delete file: %s\n",
				__FUNCTION__, elem->file_path[fp_idx]));
		} else {
			DHD_ERROR(("%s(): Successed to delete file: %s\n",
				__FUNCTION__, elem->file_path[fp_idx]));
		}
	}

	/* save dump file path */
	strncpy(elem->file_path[fp_idx], dump_path, DHD_DUMP_FILE_PATH_SIZE);
	elem->file_path[fp_idx][DHD_DUMP_FILE_PATH_SIZE - 1] = '\0';

	/* change file index to next file index */
	elem->file_idx = (elem->file_idx + 1) % DHD_DUMP_FILE_COUNT_MAX;
}
#endif /* DHD_DUMP_MNGR */

#ifdef DHD_HP2P
unsigned long
dhd_os_hp2plock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;
	unsigned long flags = 0;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		flags = osl_spin_lock(&dhd->hp2p_lock);
	}

	return flags;
}

void
dhd_os_hp2punlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		osl_spin_unlock(&dhd->hp2p_lock, flags);
	}
}
#endif /* DHD_HP2P */
#ifdef DNGL_AXI_ERROR_LOGGING
static void
dhd_axi_error_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = (dhd_info_t *)handle;
	dhd_pub_t *dhdp = NULL;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		goto exit;
	}

	dhdp = &dhd->pub;
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		goto exit;
	}

	/**
	 * First save axi error information to a file
	 * because panic should happen right after this.
	 * After dhd reset, dhd reads the file, and do hang event process
	 * to send axi error stored on the file to Bigdata server
	 */
	if (dhdp->axi_err_dump->etd_axi_error_v1.version != HND_EXT_TRAP_AXIERROR_VERSION_1) {
		DHD_ERROR(("%s: Invalid AXI version: 0x%x\n",
			__FUNCTION__, dhdp->axi_err_dump->etd_axi_error_v1.version));
	}

	DHD_OS_WAKE_LOCK(dhdp);
#ifdef DHD_FW_COREDUMP
#ifdef DHD_SSSR_DUMP
	DHD_ERROR(("%s : Set collect_sssr as TRUE\n", __FUNCTION__));
	dhdp->collect_sssr = TRUE;
#endif /* DHD_SSSR_DUMP */
	DHD_ERROR(("%s: scheduling mem dump.. \n", __FUNCTION__));
	dhd_schedule_memdump(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
#endif /* DHD_FW_COREDUMP */
	DHD_OS_WAKE_UNLOCK(dhdp);

exit:
	/* Trigger kernel panic after taking necessary dumps */
	BUG_ON(1);
}

void dhd_schedule_axi_error_dump(dhd_pub_t *dhdp, void *type)
{
	DHD_ERROR(("%s: scheduling axi_error_dump.. \n", __FUNCTION__));
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
		type, DHD_WQ_WORK_AXI_ERROR_DUMP,
		dhd_axi_error_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}
#endif /* DNGL_AXI_ERROR_LOGGING */

#ifdef SUPPORT_SET_TID
/*
 * Set custom TID value for UDP frame based on UID value.
 * This will be triggered by android private command below.
 * DRIVER SET_TID <Mode:uint8> <Target UID:uint32> <Custom TID:uint8>
 * Mode 0(SET_TID_OFF) : Disable changing TID
 * Mode 1(SET_TID_ALL_UDP) : Change TID for all UDP frames
 * Mode 2(SET_TID_BASED_ON_UID) : Change TID for UDP frames based on target UID
*/
void
dhd_set_tid_based_on_uid(dhd_pub_t *dhdp, void *pkt)
{
	struct ether_header *eh = NULL;
	struct sock *sk = NULL;
	uint8 *pktdata = NULL;
	uint8 *ip_hdr = NULL;
	uint8 cur_prio;
	uint8 prio;
	uint32 uid;

	if (dhdp->tid_mode == SET_TID_OFF) {
		return;
	}

	pktdata = (uint8 *)PKTDATA(dhdp->osh, pkt);
	eh = (struct ether_header *) pktdata;
	ip_hdr = (uint8 *)eh + ETHER_HDR_LEN;

	if (IPV4_PROT(ip_hdr) != IP_PROT_UDP) {
		return;
	}

	cur_prio = PKTPRIO(pkt);
	prio = dhdp->target_tid;
	uid = dhdp->target_uid;

	if ((cur_prio == prio) ||
		(cur_prio != PRIO_8021D_BE)) {
			return;
	}

	sk = ((struct sk_buff*)(pkt))->sk;

	if ((dhdp->tid_mode == SET_TID_ALL_UDP) ||
		(sk && (uid == __kuid_val(sock_i_uid(sk))))) {
		PKTSETPRIO(pkt, prio);
	}
}
#endif /* SUPPORT_SET_TID */

#ifdef BCMPCIE
static void
dhd_cto_recovery_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_pub_t *dhdp = NULL;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		BUG_ON(1);
		return;
	}

	dhdp = &dhd->pub;
	if (dhdp->dhd_induce_error == DHD_INDUCE_BH_CBP_HANG) {
		DHD_ERROR(("%s: skip cto recovery for DHD_INDUCE_BH_CBP_HANG\n",
			__FUNCTION__));
		return;
	}
	dhdpcie_cto_recovery_handler(dhdp);
}

void
dhd_schedule_cto_recovery(dhd_pub_t *dhdp)
{
	DHD_ERROR(("%s: scheduling cto recovery.. \n", __FUNCTION__));
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
		NULL, DHD_WQ_WORK_CTO_RECOVERY,
		dhd_cto_recovery_handler, DHD_WQ_WORK_PRIORITY_HIGH);
}
#endif /* BCMPCIE */

#ifdef DHD_WIFI_SHUTDOWN
void wifi_plat_dev_drv_shutdown(struct platform_device *pdev)
{
	dhd_pub_t *dhd_pub = NULL;
	dhd_info_t *dhd_info = NULL;
	dhd_if_t *dhd_if = NULL;

	DHD_ERROR(("%s enter\n", __FUNCTION__));
	dhd_pub = g_dhd_pub;

	if (dhd_os_check_if_up(dhd_pub)) {
		dhd_info = (dhd_info_t *)dhd_pub->info;
		dhd_if = dhd_info->iflist[0];
		ASSERT(dhd_if);
		ASSERT(dhd_if->net);
		if (dhd_if && dhd_if->net) {
			dhd_stop(dhd_if->net);
		}
	}
}
#endif /* DHD_WIFI_SHUTDOWN */
#ifdef WL_AUTO_QOS
void
dhd_wl_sock_qos_set_status(dhd_pub_t *dhdp, unsigned long on_off)
{
	dhd_sock_qos_set_status(dhdp->info, on_off);
}
#endif /* WL_AUTO_QOS */

#ifdef DHD_CFG80211_SUSPEND_RESUME
void
dhd_cfg80211_suspend(dhd_pub_t *dhdp)
{
	struct net_device *net = dhd_idx2net((dhd_pub_t *)dhdp, 0);
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
	wl_cfg80211_suspend(cfg);
}

void
dhd_cfg80211_resume(dhd_pub_t *dhdp)
{
	struct net_device * net = dhd_idx2net((dhd_pub_t *)dhdp, 0);
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
	wl_cfg80211_resume(cfg);
}
#endif /* DHD_CFG80211_SUSPEND_RESUME */

void
dhd_generate_rand_mac_addr(struct ether_addr *ea_addr)
{
	RANDOM_BYTES(ea_addr->octet, ETHER_ADDR_LEN);
	/* restore mcast and local admin bits to 0 and 1 */
	ETHER_SET_UNICAST(ea_addr->octet);
	ETHER_SET_LOCALADDR(ea_addr->octet);
	DHD_ERROR(("%s:generated new MAC="MACDBG" \n",
		__FUNCTION__, MAC2STRDBG(ea_addr->octet)));
	return;
}

void *
dhd_get_roam_evt(dhd_pub_t *dhdp)
{
#if defined(DHD_PUB_ROAM_EVT)
	return (void *)&(dhdp->roam_evt);
#else
	return NULL;
#endif /* DHD_PUB_ROAM_EVT */
}

/* BANDLOCK_FILE is for Hikey only and BANDLOCK has a priority than BANDLOCK_FILE */
static void dhd_set_bandlock(dhd_pub_t * dhd)
{
#if defined(BANDLOCK)
	int band = BANDLOCK;
	if (dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, &band, sizeof(band), TRUE, 0) < 0) {
		DHD_ERROR(("%s: set band(%d) error\n", __FUNCTION__, band));
	}
#elif defined(BANDLOCK_FILE)
	int band;
	char val[2] = {0, 0};
	if (dhd_read_file(PATH_BANDLOCK_INFO, (char *)val, sizeof(char)) == BCME_OK) {
		band = bcm_atoi(val);
		if (dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, &band, sizeof(band), TRUE, 0) < 0) {
			DHD_ERROR(("%s: set band(%d) error\n", __FUNCTION__, band));
		}
	}
#endif /* BANDLOCK */
}

#ifdef PCIE_FULL_DONGLE
/* API to delete flowings and Stations
* corresponding to the interface(ndev)
*/
void
dhd_net_del_flowrings_sta(dhd_pub_t *dhd, struct net_device *ndev)
{
	dhd_if_t *ifp = NULL;

	ifp = dhd_get_ifp_by_ndev(dhd, ndev);
	if (ifp == NULL) {
		DHD_ERROR(("DHD Iface Info corresponding to %s not found\n", ndev->name));
		return;
	}

	/* For now called only in iface delete path..
	* Add reason codes if this API need to be reused in any other paths.
	*/
	DHD_ERROR(("%s:Clean up IFACE idx %d due to interface delete\n",
		__FUNCTION__, ifp->idx));

	dhd_del_all_sta(dhd, ifp->idx);
	dhd_flow_rings_delete(dhd, ifp->idx);
}
#endif /* PCIE_FULL_DONGLE */

#ifndef BCMDBUS
static void
dhd_deferred_socram_dump(void *handle, void *event_info, u8 event)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)event_info;
	DHD_ERROR(("%s ... scheduled to collect memdump over bus\n", __FUNCTION__));
	dhd_socram_dump(dhdp->bus);
}

int
dhd_schedule_socram_dump(dhd_pub_t *dhdp)
{
	int ret = 0;
	ret = dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)dhdp,
		DHD_WQ_WORK_SOC_RAM_COLLECT, dhd_deferred_socram_dump, DHD_WQ_WORK_PRIORITY_HIGH);
	return ret;
}
#endif

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

void *dhd_get_conf(struct net_device *dev)
{
	dhd_info_t *dhdinfo = *(dhd_info_t **)netdev_priv(dev);
	if (dhdinfo)
		return (void *)dhdinfo->pub.conf;
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

#if defined(WLDWDS) && defined(FOURADDR_AUTO_BRG)
/* This function is to automatically add/del interface to the bridged dev that priamy dev is in */
static void
dhd_bridge_dev_set(dhd_info_t *dhd, int ifidx, struct net_device *sdev)
{
	struct net_device *pdev = NULL, *br_dev = NULL;
	int i, err = 0;

	if (sdev) {
		/* search the primary interface wlan1(wl0.1) with same bssidx */
		for (i = 0; i < ifidx; i++) {
			if (dhd->iflist[i]->bssidx == dhd->iflist[ifidx]->bssidx) {
				pdev = dhd->pub.info->iflist[i]->net;
				WL_MSG(sdev->name, "found primary dev %s\n", pdev->name);
				break;
			}
		}
		if (!pdev) {
			WL_MSG(sdev->name, "can not find primary dev\n");
			return;
		}
	} else {
		pdev = dhd->iflist[ifidx]->net;
	}

	/* if primary net device is bridged */
	if (pdev->priv_flags & IFF_BRIDGE_PORT) {
		rtnl_lock();
		/* get bridge device */
		br_dev = netdev_master_upper_dev_get(pdev);
		if (br_dev) {
			const struct net_device_ops *ops = br_dev->netdev_ops;
			if (ops) {
				if (sdev) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0))
					err = ops->ndo_add_slave(br_dev, sdev, NULL);
#else
					err = ops->ndo_add_slave(br_dev, sdev);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) */
					if (err)
						WL_MSG(sdev->name, "add to %s failed %d\n", br_dev->name, err);
					else
						WL_MSG(sdev->name, "slave added to %s\n", br_dev->name);
					/* Also bring wds0.x interface up automatically */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
					dev_change_flags(sdev, sdev->flags | IFF_UP, NULL);
#else
					dev_change_flags(sdev, sdev->flags | IFF_UP);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0) */
				} else {
					err = ops->ndo_del_slave(br_dev, pdev);
					if (err)
						WL_MSG(pdev->name, "del from %s failed %d\n", br_dev->name, err);
					else
						WL_MSG(pdev->name, "slave deleted from %s\n", br_dev->name);
				}
			}
		}
		else {
			WL_MSG(pdev->name, "not bridged\n");
		}
		rtnl_unlock();
	}
	else {
		WL_MSG(pdev->name, "not bridged\n");
	}
}
#endif /* WLDWDS && FOURADDR_AUTO_BRG */
