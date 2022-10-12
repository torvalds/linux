/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
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
 * $Id: dhd_linux.c 702611 2017-06-02 06:40:15Z $
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmstdlib_s.h>
#ifdef SHOW_LOGTRACE
#include <linux/syscalls.h>
#include <event_log.h>
#endif /* SHOW_LOGTRACE */

#ifdef PCIE_FULL_DONGLE
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
#include <net/addrconf.h>
#ifdef ENABLE_ADAPTIVE_SCHED
#include <linux/cpufreq.h>
#endif /* ENABLE_ADAPTIVE_SCHED */
#include <linux/rtc.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <dhd_linux_priv.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
#include <uapi/linux/sched/types.h>
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */

#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <bcmiov.h>

#include <ethernet.h>
#include <bcmevent.h>
#include <vlan.h>
#include <802.3.h>

#include <dhd_linux_wq.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhd_linux_pktdump.h>
#ifdef DHD_WET
#include <dhd_wet.h>
#endif /* DHD_WET */
#ifdef PCIE_FULL_DONGLE
#include <dhd_flowring.h>
#endif // endif
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhd_dbg_ring.h>
#include <dhd_debug.h>
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
#include <linux/wakelock.h>
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
#if defined(WL_CFG80211)
#include <wl_cfg80211.h>
#ifdef WL_BAM
#include <wl_bam.h>
#endif	/* WL_BAM */
#endif	/* WL_CFG80211 */
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif // endif
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif // endif

#if defined(CONFIG_SOC_EXYNOS8895) || defined(CONFIG_SOC_EXYNOS9810) || \
	defined(CONFIG_SOC_EXYNOS9820)
#include <linux/exynos-pci-ctrl.h>
#endif /* CONFIG_SOC_EXYNOS8895 || CONFIG_SOC_EXYNOS9810 || CONFIG_SOC_EXYNOS9820 */

#ifdef DHD_L2_FILTER
#include <bcmicmp.h>
#include <bcm_l2_filter.h>
#include <dhd_l2_filter.h>
#endif /* DHD_L2_FILTER */

#ifdef DHD_PSTA
#include <dhd_psta.h>
#endif /* DHD_PSTA */

#ifdef AMPDU_VO_ENABLE
#include <802.1d.h>
#endif /* AMPDU_VO_ENABLE */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
#include <uapi/linux/sched/types.h>
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */

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

#ifdef DHD_BANDSTEER
#include <dhd_bandsteer.h>
#endif /* DHD_BANDSTEER */
#ifdef DHD_DEBUG_PAGEALLOC
typedef void (*page_corrupt_cb_t)(void *handle, void *addr_corrupt, size_t len);
void dhd_page_corrupt_cb(void *handle, void *addr_corrupt, size_t len);
extern void register_page_corrupt_cb(page_corrupt_cb_t cb, void* handle);
#endif /* DHD_DEBUG_PAGEALLOC */

#define IP_PROT_RESERVED	0xFF

#ifdef DHD_4WAYM4_FAIL_DISCONNECT
static void dhd_m4_state_handler(struct work_struct * work);
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */

#ifdef DHDTCPSYNC_FLOOD_BLK
static void dhd_blk_tsfl_handler(struct work_struct * work);
#endif /* DHDTCPSYNC_FLOOD_BLK */

#ifdef WL_NATOE
#include <dhd_linux_nfct.h>
#endif /* WL_NATOE */

#if defined(OEM_ANDROID) && defined(SOFTAP)
extern bool ap_cfg_running;
extern bool ap_fw_loaded;
#endif // endif

#ifdef FIX_CPU_MIN_CLOCK
#include <linux/pm_qos.h>
#endif /* FIX_CPU_MIN_CLOCK */

#ifdef SET_RANDOM_MAC_SOFTAP
#ifndef CONFIG_DHD_SET_RANDOM_MAC_VAL
#define CONFIG_DHD_SET_RANDOM_MAC_VAL	0x001A11
#endif // endif
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

#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif // endif

#if defined(OEM_ANDROID)
#include <wl_android.h>
#endif // endif

/* Maximum STA per radio */
#define DHD_MAX_STA     32

#ifdef DHD_EVENT_LOG_FILTER
#include <dhd_event_log_filter.h>
#endif /* DHD_EVENT_LOG_FILTER */

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

#if defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = FALSE;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#endif /* defined(CONFIG_PM_SLEEP) */

#if defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif /* defined(OOB_INTR_ONLY) || defined(BCMSPI_ANDROID) */
#if defined(OEM_ANDROID)
static void dhd_hang_process(struct work_struct *work_data);
#endif /* #OEM_ANDROID */
MODULE_LICENSE("GPL and additional rights");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

#ifdef CONFIG_BCM_DETECT_CONSECUTIVE_HANG
#define MAX_CONSECUTIVE_HANG_COUNTS 5
#endif /* CONFIG_BCM_DETECT_CONSECUTIVE_HANG */

#include <dhd_bus.h>

#ifdef DHD_ULP
#include <dhd_ulp.h>
#endif /* DHD_ULP */

#ifndef PROP_TXSTATUS
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen)
#else
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen + 128)
#endif // endif

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

#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
#include <linux/nl80211.h>
#endif /* OEM_ANDROID && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) */

#if defined(PKT_FILTER_SUPPORT) && defined(APF)
static int __dhd_apf_add_filter(struct net_device *ndev, uint32 filter_id,
	u8* program, uint32 program_len);
static int __dhd_apf_config_filter(struct net_device *ndev, uint32 filter_id,
	uint32 mode, uint32 enable);
static int __dhd_apf_delete_filter(struct net_device *ndev, uint32 filter_id);
#endif /* PKT_FILTER_SUPPORT && APF */

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT) && defined(DHD_FW_COREDUMP)
static int dhd_wait_for_file_dump(dhd_pub_t *dhdp);
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT && DHD_FW_COREDUMP */

#if defined(ARGOS_NOTIFY_CB)
/* ARGOS notifer data */
static struct notifier_block argos_wifi; /* STA */
static struct notifier_block argos_p2p; /* P2P */
argos_rps_ctrl argos_rps_ctrl_data;
#endif // endif

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
static int dhd_log_flush(dhd_pub_t *dhdp, log_dump_type_t *type);
static void dhd_get_time_str(dhd_pub_t *dhdp, char *time_str, int size);
void dhd_get_debug_dump_len(void *handle, struct sk_buff *skb, void *event_info, u8 event);
void cfgvendor_log_dump_len(dhd_pub_t *dhdp, log_dump_type_t *type, struct sk_buff *skb);
static void dhd_print_buf_addr(dhd_pub_t *dhdp, char *name, void *buf, unsigned int size);
static void dhd_log_dump_buf_addr(dhd_pub_t *dhdp, log_dump_type_t *type);
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

#if defined(BT_OVER_SDIO)
/* Flag to indicate if driver is initialized */
uint dhd_driver_init_done = TRUE;
#else
/* Flag to indicate if driver is initialized */
uint dhd_driver_init_done = FALSE;
#endif // endif
/* Flag to indicate if we should download firmware on driver load */
uint dhd_download_fw_on_driverload = TRUE;

/* Definitions to provide path to the firmware and nvram
 * example nvram_path[MOD_PARAM_PATHLEN]="/projects/wlan/nvram.txt"
 */
char firmware_path[MOD_PARAM_PATHLEN];
char nvram_path[MOD_PARAM_PATHLEN];
char clm_path[MOD_PARAM_PATHLEN];
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
#if defined(BCMLXSDMMC)
struct semaphore dhd_registration_sem;
#endif /* BCMXSDMMC */
#endif /* defined(OEM_ANDROID) */

#ifdef DHD_LOG_DUMP
int logdump_max_filesize = LOG_DUMP_MAX_FILESIZE;
module_param(logdump_max_filesize, int, 0644);
int logdump_max_bufsize = LOG_DUMP_GENERAL_MAX_BUFSIZE;
module_param(logdump_max_bufsize, int, 0644);
int logdump_prsrv_tailsize = DHD_LOG_DUMP_MAX_TAIL_FLUSH_SIZE;
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
module_param(logdump_rtt_enable, int, 0644);
#endif /* DHD_LOG_DUMP */
#ifdef EWP_EDL
int host_edl_support = TRUE;
module_param(host_edl_support, int, 0644);
#endif // endif

/* deferred handlers */
static void dhd_ifadd_event_handler(void *handle, void *event_info, u8 event);
static void dhd_ifdel_event_handler(void *handle, void *event_info, u8 event);
#ifndef DHD_DIRECT_SET_MAC
static void dhd_set_mac_addr_handler(void *handle, void *event_info, u8 event);
#endif // endif
static void dhd_set_mcast_list_handler(void *handle, void *event_info, u8 event);
#ifdef WL_NATOE
static void dhd_natoe_ct_event_hanlder(void *handle, void *event_info, u8 event);
static void dhd_natoe_ct_ioctl_handler(void *handle, void *event_info, uint8 event);
#endif /* WL_NATOE */

#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
static void dhd_inet6_work_handler(void *dhd_info, void *event_data, u8 event);
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
#ifdef WL_CFG80211
extern void dhd_netdev_free(struct net_device *ndev);
#endif /* WL_CFG80211 */
static dhd_if_t * dhd_get_ifp_by_ndev(dhd_pub_t *dhdp, struct net_device *ndev);

#if (defined(DHD_WET) || defined(DHD_MCAST_REGEN) || defined(DHD_L2_FILTER))
/* update rx_pkt_chainable state of dhd interface */
static void dhd_update_rx_pkt_chainable_state(dhd_pub_t* dhdp, uint32 idx);
#endif /* DHD_WET || DHD_MCAST_REGEN || DHD_L2_FILTER */

/* Error bits */
module_param(dhd_msg_level, int, 0);

#ifdef ARP_OFFLOAD_SUPPORT
/* ARP offload enable */
uint dhd_arp_enable = TRUE;
module_param(dhd_arp_enable, uint, 0);

/* ARP offload agent mode : Enable ARP Host Auto-Reply and ARP Peer Auto-Reply */

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
uint dhd_console_ms = 0;
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
#endif // endif

/* Pkt filter init setup */
uint dhd_pkt_filter_init = 0;
module_param(dhd_pkt_filter_init, uint, 0);

/* Pkt filter mode control */
#ifdef GAN_LITE_NAT_KEEPALIVE_FILTER
uint dhd_master_mode = FALSE;
#else
uint dhd_master_mode = TRUE;
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

#if !defined(BCMDHDUSB)
extern int dhd_dongle_ramsize;
module_param(dhd_dongle_ramsize, int, 0);
#endif /* BCMDHDUSB */

#ifdef WL_CFG80211
int passive_channel_skip = 0;
module_param(passive_channel_skip, int, (S_IRUSR|S_IWUSR));
#endif /* WL_CFG80211 */

#ifdef DHD_MSI_SUPPORT
uint enable_msi = TRUE;
module_param(enable_msi, uint, 0);
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_SSSR_DUMP
int dhdpcie_sssr_dump_get_before_after_len(dhd_pub_t *dhd, uint32 *arr_len);
extern uint support_sssr_dump;
module_param(support_sssr_dump, uint, 0);
#endif /* DHD_SSSR_DUMP */

/* Keep track of number of instances */
static int dhd_found = 0;
static int instance_base = 0; /* Starting instance number */
module_param(instance_base, int, 0644);

/* Takes value of LL of OTP param customvar2=0xKKLLMMNN.
 * LL is module variant
 */
uint32 hw_module_variant = 0;
module_param(hw_module_variant, uint, 0644);

#if defined(DHD_LB_RXP)
static int dhd_napi_weight = 32;
module_param(dhd_napi_weight, int, 0644);
#endif /* DHD_LB_RXP */

#ifdef PCIE_FULL_DONGLE
extern int h2d_max_txpost;
module_param(h2d_max_txpost, int, 0644);

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
#if defined(CUSTOMER_HW4_DEBUG)
static char *logstrs_path = PLATFORM_PATH"logstrs.bin";
char *st_str_file_path = PLATFORM_PATH"rtecdc.bin";
static char *map_file_path = PLATFORM_PATH"rtecdc.map";
static char *rom_st_str_file_path = PLATFORM_PATH"roml.bin";
static char *rom_map_file_path = PLATFORM_PATH"roml.map";
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
static char *logstrs_path = "/data/misc/wifi/logstrs.bin";
char *st_str_file_path = "/data/misc/wifi/rtecdc.bin";
static char *map_file_path = "/data/misc/wifi/rtecdc.map";
static char *rom_st_str_file_path = "/data/misc/wifi/roml.bin";
static char *rom_map_file_path = "/data/misc/wifi/roml.map";
#elif defined(OEM_ANDROID) /* For Brix KK Live Image */
static char *logstrs_path = "/installmedia/logstrs.bin";
char *st_str_file_path = "/installmedia/rtecdc.bin";
static char *map_file_path = "/installmedia/rtecdc.map";
static char *rom_st_str_file_path = "/installmedia/roml.bin";
static char *rom_map_file_path = "/installmedia/roml.map";
#else /* For Linux platforms */
static char *logstrs_path = "/root/logstrs.bin";
char *st_str_file_path = "/root/rtecdc.bin";
static char *map_file_path = "/root/rtecdc.map";
static char *rom_st_str_file_path = "/root/roml.bin";
static char *rom_map_file_path = "/root/roml.map";
#endif /* CUSTOMER_HW4_DEBUG || CUSTOMER_HW2 || BOARD_HIKEY */
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

#ifdef BCMSDIO
#define DHD_IF_ROLE(pub, idx)           ((pub)->info->iflist[idx]->role)
#define DHD_IF_ROLE_AP(pub, idx)        (DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_AP)
#define DHD_IF_ROLE_STA(pub, idx)       (DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_STA)
#define DHD_IF_ROLE_P2PGO(pub, idx)     (DHD_IF_ROLE(pub, idx) == WLC_E_IF_ROLE_P2P_GO)

void dhd_set_role(dhd_pub_t *dhdp, int role, int bssidx)
{
	int ifidx = dhd_bssidx2idx(dhdp, bssidx);
	DHD_TRACE(("dhd_set_role ifidx %d role %d\n", ifidx, role));
	dhdp->info->iflist[ifidx]->role = role;
}
#endif /* BCMSDIO */

#ifdef USE_WFA_CERT_CONF
int g_frameburst = 1;
#endif /* USE_WFA_CERT_CONF */

static int dhd_get_pend_8021x_cnt(dhd_info_t *dhd);

/* DHD Perimiter lock only used in router with bypass forwarding. */
#define DHD_PERIM_RADIO_INIT()              do { /* noop */ } while (0)
#define DHD_PERIM_LOCK_TRY(unit, flag)      do { /* noop */ } while (0)
#define DHD_PERIM_UNLOCK_TRY(unit, flag)    do { /* noop */ } while (0)

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

/* Control fw roaming */
#ifdef BCMCCX
uint dhd_roam_disable = 0;
#else
#ifdef OEM_ANDROID
uint dhd_roam_disable = 0;
#else
uint dhd_roam_disable = 1;
#endif // endif
#endif /* BCMCCX */

#ifdef BCMDBGFS
extern void dhd_dbgfs_init(dhd_pub_t *dhdp);
extern void dhd_dbgfs_remove(void);
#endif // endif

static uint pcie_txs_metadata_enable = 0;	/* Enable TX status metadta report */
module_param(pcie_txs_metadata_enable, int, 0);

/* Control radio state */
uint dhd_radio_up = 1;

/* Network inteface name */
char iface_name[IFNAMSIZ] = {'\0'};
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);

#ifdef WL_VIF_SUPPORT
/* Virtual inteface name */
char vif_name[IFNAMSIZ] = "wlan";
module_param_string(vif_name, vif_name, IFNAMSIZ, 0);

int vif_num = 0;
module_param(vif_num, int, 0);
#endif /* WL_VIF_SUPPORT */

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

#if (defined(OEM_ANDROID) && !defined(BCMQT))
/* Allow delayed firmware download for debug purpose */
int allow_delay_fwdl = FALSE;
#else
int allow_delay_fwdl = TRUE;
#endif // endif
module_param(allow_delay_fwdl, int, 0);

#ifdef ECOUNTER_PERIODIC_DISABLE
uint enable_ecounter = FALSE;
#else
uint enable_ecounter = TRUE;
#endif // endif
module_param(enable_ecounter, uint, 0);

/* TCM verification flag */
uint dhd_tcm_test_enable = FALSE;
module_param(dhd_tcm_test_enable, uint, 0644);

/* WAR to avoid system hang during FW trap */
#ifdef DHD_FW_COREDUMP
uint disable_bug_on = FALSE;
module_param(disable_bug_on, uint, 0);
#endif /* DHD_FW_COREDUMP */

extern char dhd_version[];
extern char fw_version[];
extern char clm_version[];

int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
static void dhd_net_if_lock_local(dhd_info_t *dhd);
static void dhd_net_if_unlock_local(dhd_info_t *dhd);
static void dhd_suspend_lock(dhd_pub_t *dhdp);
static void dhd_suspend_unlock(dhd_pub_t *dhdp);

#ifdef DHD_MONITOR_INTERFACE
/* Monitor interface */
int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);
#endif /* DHD_MONITOR_INTERFACE */

#ifdef DHD_PM_CONTROL_FROM_FILE
bool g_pm_control;
#ifdef DHD_EXPORT_CNTL_FILE
int pmmode_val;
#endif /* DHD_EXPORT_CNTL_FILE */
void sec_control_pm(dhd_pub_t *dhd, uint *);
#endif /* DHD_PM_CONTROL_FROM_FILE */

#if defined(WL_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif /* defined(WL_WIRELESS_EXT) */

static void dhd_dpc(ulong data);
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
#endif // endif
	dhd_info_t *dhdinfo = (dhd_info_t*)container_of(nfb, struct dhd_info, pm_notifier);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

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

#if defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS)
	if (suspend) {
		DHD_OS_WAKE_LOCK_WAIVE(&dhdinfo->pub);
		dhd_wlfc_suspend(&dhdinfo->pub);
		DHD_OS_WAKE_LOCK_RESTORE(&dhdinfo->pub);
	} else {
		dhd_wlfc_resume(&dhdinfo->pub);
	}
#endif /* defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS) */

	dhd_mmc_suspend = suspend;
	smp_mb();

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

/* Return interface pointer */
struct dhd_if * dhd_get_ifp(dhd_pub_t *dhdp, uint32 ifidx)
{
	ASSERT(ifidx < DHD_MAX_IFS);

	if (!dhdp || !dhdp->info || ifidx >= DHD_MAX_IFS)
		return NULL;

	return dhdp->info->iflist[ifidx];
}

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
#endif // endif
	.info = DHD_INFO_NULL,
	.net = DHD_NET_DEV_NULL,
	.idx = DHD_BAD_IF
};
#define DHD_IF_NULL  (&dhd_if_null)

#define DHD_STA_NULL ((dhd_sta_t *)NULL)

/** Interface STA list management. */

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

/** Reset a dhd_sta object and free into the dhd pool. */
static void
dhd_sta_free(dhd_pub_t * dhdp, dhd_sta_t * sta)
{
#ifdef PCIE_FULL_DONGLE
	int prio;
#endif // endif

	ASSERT((sta != DHD_STA_NULL) && (sta->idx != ID16_INVALID));

	ASSERT((dhdp->staid_allocator != NULL) && (dhdp->sta_pool != NULL));

#ifdef PCIE_FULL_DONGLE
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
#endif /* PCIE_FULL_DONGLE */

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
#endif // endif
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		list_del(&sta->list);
		dhd_sta_free(&ifp->info->pub, sta);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return;
}

/** Router/GMAC3: Flush all station entries in the forwarder's WOFA database. */
static void
dhd_if_flush_sta(dhd_if_t * ifp)
{
}

/** Construct a pool of dhd_sta_t objects to be used by interfaces. */
static int
dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta)
{
	int idx, sta_pool_memsz;
#ifdef PCIE_FULL_DONGLE
	int prio;
#endif /* PCIE_FULL_DONGLE */
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
#ifdef PCIE_FULL_DONGLE
		for (prio = 0; prio < (int)NUMPRIO; prio++) {
			sta->flowid[prio] = FLOWID_INVALID; /* Flow rings do not exist */
		}
#endif /* PCIE_FULL_DONGLE */
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
	int idx, sta_pool_memsz;
#ifdef PCIE_FULL_DONGLE
	int prio;
#endif /* PCIE_FULL_DONGLE */
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
#ifdef PCIE_FULL_DONGLE
		for (prio = 0; prio < (int)NUMPRIO; prio++) {
			sta->flowid[prio] = FLOWID_INVALID; /* Flow rings do not exist */
		}
#endif /* PCIE_FULL_DONGLE */
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
#endif // endif
	list_for_each_entry(sta, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
			DHD_INFO(("%s: Found STA " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG((char *)ea)));
			DHD_IF_STA_LIST_UNLOCK(ifp, flags);
			return sta;
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
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
	INIT_LIST_HEAD(&sta->list);

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	list_add_tail(&sta->list, &ifp->sta_list);

	DHD_ERROR(("%s: Adding  STA " MACDBG "\n",
		__FUNCTION__, MAC2STRDBG((char *)ea)));

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
#endif // endif
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {

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
#endif // endif
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

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return;

	DHD_IF_STA_LIST_LOCK(ifp, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
			DHD_ERROR(("%s: Deleting STA " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG(sta->ea.octet)));
			list_del(&sta->list);
			dhd_sta_free(&ifp->info->pub, sta);
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
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
#endif /* DHD_IGMP_UCQUERY || DHD_UCAST_UPNP */

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
		DHD_ERROR(("dhd_rxf_enqueue: pktbuf not consumed %p, store idx %d sent idx %d\n",
			skb, store_idx, sent_idx));
		/* removed msleep here, should use wait_event_timeout if we
		 * want to give rx frame thread a chance to run
		 */
#if defined(WAIT_DEQUEUE)
		OSL_SLEEP(1);
#endif // endif
		return BCME_ERROR;
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

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT) && defined(DHD_FW_COREDUMP)
static int dhd_wait_for_file_dump(dhd_pub_t *dhdp)
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
			DHD_ERROR(("%s: Timed out dhd_bus_busy_state=0x%x\n",
					__FUNCTION__, dhdp->dhd_bus_busy_state));
		}
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
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT && DHD_FW_CORE_DUMP */

#ifdef PKT_FILTER_SUPPORT
#ifndef GAN_LITE_NAT_KEEPALIVE_FILTER
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
	if ((dhd->op_mode & DHD_FLAG_HOSTAP_MODE) && value) {
		DHD_ERROR(("%s: DHD_FLAG_HOSTAP_MODE\n", __FUNCTION__));
		return;
	}
	/* 1 - Enable packet filter, only allow unicast packet to send up */
	/* 0 - Disable packet filter */
	if (dhd_pkt_filter_enable && (!value ||
	    (dhd_support_sta_mode(dhd) && !dhd->dhcp_in_progress)))
	    {
		for (i = 0; i < dhd->pktfilter_count; i++) {
#ifndef GAN_LITE_NAT_KEEPALIVE_FILTER
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

#if defined(OEM_ANDROID) && defined(BCMPCIE)
	int lpas = 0;
	int dtim_period = 0;
	int bcn_interval = 0;
	int bcn_to_dly = 0;
#if defined(CUSTOM_BCN_TIMEOUT_IN_SUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	bcn_timeout = CUSTOM_BCN_TIMEOUT_SETTING;
#else
	int bcn_timeout = CUSTOM_BCN_TIMEOUT_SETTING;
#endif /* CUSTOM_BCN_TIMEOUT_IN_SUSPEND && DHD_USE_EARLYSUSPEND */
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
#endif // endif
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
				dhd_arp_offload_enable(dhd, TRUE);
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
						DHD_ERROR(("%s lpas failed %d\n", __FUNCTION__,
								ret));
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
						DHD_ERROR(("%s bcn_to_dly failed %d\n",
								__FUNCTION__, ret));
					}
					/* Increase beacon timeout to 6 secs or use bigger one */
					bcn_timeout = max(bcn_timeout, BCN_TIMEOUT_IN_SUSPEND);
					ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
							sizeof(bcn_timeout), NULL, 0, TRUE);
					if (ret < 0) {
						DHD_ERROR(("%s bcn_timeout failed %d\n",
								__FUNCTION__, ret));
					}
				}
#else
				bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd);
				if (dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
						sizeof(bcn_li_dtim), NULL, 0, TRUE) < 0)
					DHD_ERROR(("%s: set dtim failed\n", __FUNCTION__));
#endif /* OEM_ANDROID && BCMPCIE */
#ifdef WL_CFG80211
				/* Disable cfg80211 feature events during suspend */
				ret = wl_cfg80211_config_suspend_events(
					dhd_linux_get_primary_netdev(dhd), FALSE);
				if (ret < 0) {
					DHD_ERROR(("failed to disable events (%d)\n", ret));
				}
#endif /* WL_CFG80211 */
#ifdef DHD_USE_EARLYSUSPEND
#ifdef CUSTOM_BCN_TIMEOUT_IN_SUSPEND
				bcn_timeout = CUSTOM_BCN_TIMEOUT_IN_SUSPEND;
				ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
						sizeof(bcn_timeout), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s bcn_timeout failed %d\n", __FUNCTION__,
						ret));
				}
#endif /* CUSTOM_BCN_TIMEOUT_IN_SUSPEND */
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
				if (FW_SUPPORTED(dhd, ndoe)) {
#else
				if (FW_SUPPORTED(dhd, ndoe) && !FW_SUPPORTED(dhd, apf)) {
#endif /* APF */
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
#ifdef PKT_FILTER_SUPPORT
				dhd->early_suspended = 0;
#endif // endif
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
				dhd_arp_offload_enable(dhd, FALSE);
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
					DHD_ERROR(("%s:lpas failed:%d\n", __FUNCTION__, ret));
				}
				ret = dhd_iovar(dhd, 0, "bcn_to_dly", (char *)&bcn_to_dly,
						sizeof(bcn_to_dly), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:bcn_to_dly failed:%d\n", __FUNCTION__, ret));
				}
				ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
						sizeof(bcn_timeout), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:bcn_timeout failed:%d\n",
							__FUNCTION__, ret));
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
#ifdef CUSTOM_BCN_TIMEOUT_IN_SUSPEND
				bcn_timeout = CUSTOM_BCN_TIMEOUT;
				ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
						sizeof(bcn_timeout), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s:bcn_timeout failed:%d\n",
						__FUNCTION__, ret));
				}
#endif /* CUSTOM_BCN_TIMEOUT_IN_SUSPEND */
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
				if (FW_SUPPORTED(dhd, ndoe)) {
#else
				if (FW_SUPPORTED(dhd, ndoe) && !FW_SUPPORTED(dhd, apf)) {
#endif /* APF */
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
#ifdef WL_CFG80211
				/* Enable cfg80211 feature events during resume */
				ret = wl_cfg80211_config_suspend_events(
					dhd_linux_get_primary_netdev(dhd), TRUE);
				if (ret < 0) {
					DHD_ERROR(("failed to enable events (%d)\n", ret));
				}
#endif /* WL_CFG80211 */
#ifdef DHD_LB_IRQSET
				dhd_irq_set_affinity(dhd, dhd->info->cpumask_primary);
#endif /* DHD_LB_IRQSET */
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
		dhd_support_sta_mode(dhdp))
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
		/*
		 * OSL_SLEEP() is corresponding to usleep_range(). In non-atomic
		 * context where the exact wakeup time is flexible, it would be good
		 * to use usleep_range() instead of udelay(). It takes a few advantages
		 * such as improving responsiveness and reducing power.
		 */
		OSL_SLEEP(jiffies_to_msecs(1));
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
	struct netdev_hw_addr *ha;
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
	cnt = netdev_mc_count(dev);
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

#ifdef MCAST_LIST_ACCUMULATION
	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (dhd->iflist[i]) {
			DHD_TRACE(("_dhd_set_multicast_list: ifidx %d\n", i));
			dev = dhd->iflist[i]->net;

			netif_addr_lock_bh(dev);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
			netdev_for_each_mc_addr(ha, dev) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
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
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	netdev_for_each_mc_addr(ha, dev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
		cnt--;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
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

#ifdef SOFTAP
extern struct net_device *ap_net_dev;
extern tsk_ctl_t ap_eth_ctl; /* ap netdev heper thread ctl */
#endif // endif

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
#endif // endif
#ifdef DHD_WET
		(dhd->wet_mode) ||
#endif // endif
#ifdef DHD_MCAST_REGEN
		(ifp->mcast_regen_bss_enable) ||
#endif // endif
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
	int ret;
#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	struct wl_if_event_info info;
#else
	struct net_device *ndev;
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

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
	DHD_PERIM_LOCK(&dhd->pub);

	ifidx = if_event->event.ifidx;
	bssidx = if_event->event.bssidx;
	DHD_TRACE(("%s: registering if with ifidx %d\n", __FUNCTION__, ifidx));

#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (if_event->event.ifidx > 0) {
		u8 *mac_addr;
		bzero(&info, sizeof(info));
		info.ifidx = ifidx;
		info.bssidx = bssidx;
		info.role = if_event->event.role;
		strncpy(info.name, if_event->name, IFNAMSIZ);
		if (is_valid_ether_addr(if_event->mac)) {
			mac_addr = if_event->mac;
		} else {
			mac_addr = NULL;
		}

		if (wl_cfg80211_post_ifcreate(dhd->pub.info->iflist[0]->net,
			&info, mac_addr, NULL, true) == NULL) {
			/* Do the post interface create ops */
			DHD_ERROR(("Post ifcreate ops failed. Returning \n"));
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
	if (!dhd->pub.info->iflist[ifidx]) {
		/* No matching netdev found */
		DHD_ERROR(("Netdev not found! Do nothing.\n"));
		goto done;
	}
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
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

done:
	DHD_PERIM_LOCK(&dhd->pub);
	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));
	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

#ifndef DHD_DIRECT_SET_MAC
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

#ifdef SOFTAP
	{
		unsigned long flags;
		bool in_ap = FALSE;
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		in_ap = (ap_net_dev != NULL);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);

		if (in_ap)  {
			DHD_ERROR(("attempt to set MAC for %s in AP Mode, blocked. \n",
			           ifp->net->name));
			goto done;
		}
	}
#endif /* SOFTAP */

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
#endif /* DHD_DIRECT_SET_MAC */

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

#ifdef SOFTAP
	{
		bool in_ap = FALSE;
		unsigned long flags;
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		in_ap = (ap_net_dev != NULL);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);

		if (in_ap)  {
			DHD_ERROR(("set MULTICAST list for %s in AP Mode, blocked. \n",
			           ifp->net->name));
			ifp->set_multicast = FALSE;
			goto done;
		}
	}
#endif /* SOFTAP */

	ifidx = ifp->idx;

#ifdef MCAST_LIST_ACCUMULATION
	ifidx = 0;
#endif /* MCAST_LIST_ACCUMULATION */

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
#ifdef DHD_DIRECT_SET_MAC
	/* It needs to update new mac address on this context */
	ret = _dhd_set_mac_address(dhd, ifidx, dhdif->mac_addr);
	dhdif->set_macaddress = FALSE;
#else
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)dhdif, DHD_WQ_WORK_SET_MAC,
		dhd_set_mac_addr_handler, DHD_WQ_WORK_PRIORITY_LOW);
#endif // endif
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
	spin_lock_bh(&di->wlfc_spinlock);
	return 1;
}

int
dhd_os_wlfc_unblock(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);

	ASSERT(di != NULL);
	spin_unlock_bh(&di->wlfc_spinlock);
	return 1;
}

#endif /* PROP_TXSTATUS */

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
			netif_rx_ni(skb);
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
	bool pkt_ether_type_802_1x = FALSE;
	uint8 pkt_flow_prio;

#if defined(DHD_L2_FILTER)
	dhd_if_t *ifp = dhd_get_ifp(dhdp, ifidx);
#endif // endif

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
			pkt_ether_type_802_1x = TRUE;
			DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_EAPOL_FRAME_TRANSMIT_REQUESTED);
			atomic_inc(&dhd->pend_8021x_cnt);
#if defined(WL_CFG80211) && defined(WL_WPS_SYNC)
			wl_handle_wps_states(dhd_idx2net(dhdp, ifidx),
				pktdata, PKTLEN(dhdp->osh, pktbuf), TRUE);
#endif /* WL_CFG80211 && WL_WPS_SYNC */
		}
		dhd_dump_pkt(dhdp, ifidx, pktdata,
			(uint32)PKTLEN(dhdp->osh, pktbuf), TRUE, NULL, NULL);
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

	BCM_REFERENCE(pkt_ether_type_802_1x);
	BCM_REFERENCE(pkt_flow_prio);

#ifdef SUPPORT_SET_TID
	dhd_set_tid_based_on_uid(dhdp, pktbuf);
#endif	/* SUPPORT_SET_TID */

#ifdef PCIE_FULL_DONGLE
	/*
	 * Lkup the per interface hash table, for a matching flowring. If one is not
	 * available, allocate a unique flowid and add a flowring entry.
	 * The found or newly created flowid is placed into the pktbuf's tag.
	 */

#ifdef DHD_LOSSLESS_ROAMING
	/* For LLR override and use flowring with prio 7 for 802.1x packets */
	if (pkt_ether_type_802_1x) {
		pkt_flow_prio = PRIO_8021D_NC;
	} else
#endif /* DHD_LOSSLESS_ROAMING */
	{
		pkt_flow_prio = dhdp->flow_prio_map[(PKTPRIO(pktbuf))];
	}

	ret = dhd_flowid_update(dhdp, ifidx, pkt_flow_prio, pktbuf);
	if (ret != BCME_OK) {
		if (ntoh16(eh->ether_type) == ETHER_TYPE_802_1X) {
			atomic_dec(&dhd->pend_8021x_cnt);
		}
		PKTCFREE(dhd->pub.osh, pktbuf, TRUE);
		return ret;
	}
#endif /* PCIE_FULL_DONGLE */

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

	return ret;
}

int BCMFASTPATH
dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
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
#endif // endif
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_SEND_PKT(dhdp);
	DHD_IF_CLR_TX_ACTIVE(ifp, DHD_TX_SEND_PKT);
	dhd_os_tx_completion_wake(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
	return ret;
}

#ifndef DHD_MONITOR_INTERFACE
static
#endif /* DHD_MONITOR_INTERFACE */
#ifdef CFI_CHECK
netdev_tx_t BCMFASTPATH
#else /* CFI_CHECK */
int BCMFASTPATH
#endif /* CFI_CHECK */
dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	uint datalen;
	void *pktbuf;
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp = NULL;
	int ifidx;
	unsigned long flags;
	uint8 htsfdlystat_sz = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd_query_bus_erros(&dhd->pub)) {
#ifdef CFI_CHECK
		return NETDEV_TX_BUSY;
#else
		return -ENODEV;
#endif /* CFI_CHECK */
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

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);

#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhd->pub.req_hang_type == HANG_REASON_BUS_DOWN) {
		DHD_ERROR(("%s: making DHD_BUS_DOWN\n", __FUNCTION__));
		dhd->pub.busstate = DHD_BUS_DOWN;
	}
#endif /* DHD_HANG_SEND_UP_TEST */

	/* Reject if down */
	if (dhd->pub.hang_was_sent || DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(&dhd->pub)) {
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d \n",
			__FUNCTION__, dhd->pub.up, dhd->pub.busstate));
		netif_stop_queue(net);
#if defined(OEM_ANDROID)
		/* Send Event when bus down detected during data session */
		if (dhd->pub.up && !dhd->pub.hang_was_sent) {
			DHD_ERROR(("%s: Event HANG sent up\n", __FUNCTION__));
			dhd->pub.hang_reason = HANG_REASON_BUS_DOWN;
			net_os_send_hang_message(net);
		}
#endif /* OEM_ANDROID */
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return NETDEV_TX_BUSY;
	}

	ifp = DHD_DEV_IFP(net);
	ifidx = DHD_DEV_IFIDX(net);
	if (!ifp || (ifidx == DHD_BAD_IF) ||
		ifp->del_in_progress) {
		DHD_ERROR(("%s: ifidx %d ifp:%p del_in_progress:%d\n",
		__FUNCTION__, ifidx, ifp, (ifp ? ifp->del_in_progress : 0)));
		netif_stop_queue(net);
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return NETDEV_TX_BUSY;
	}

	DHD_IF_SET_TX_ACTIVE(ifp, DHD_TX_START_XMIT);
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

#ifdef DHD_PSTA
	/* PSR related packet proto manipulation should be done in DHD
	 * since dongle doesn't have complete payload
	 */
	if (PSR_ENABLED(&dhd->pub) &&
		(dhd_psta_proc(&dhd->pub, ifidx, &pktbuf, TRUE) < 0)) {

			DHD_ERROR(("%s:%s: psta send proc failed\n", __FUNCTION__,
				dhd_ifname(&dhd->pub, ifidx)));
	}
#endif /* DHD_PSTA */
#ifdef CONFIG_ARCH_MSM
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
	if (skb->sk) {
		sk_pacing_shift_update(skb->sk, 8);
	}
#endif /* LINUX_VERSION_CODE >= 4.16.0 */
#endif /* CONFIG_ARCH_MSM */
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
#endif // endif

done:
	if (ret) {
		ifp->stats.tx_dropped++;
		dhd->pub.tx_dropped++;
	} else {
#ifdef PROP_TXSTATUS
		/* tx_packets counter can counted only when wlfc is disabled */
		if (!dhd_wlfc_is_supported(&dhd->pub))
#endif // endif
		{
			dhd->pub.tx_packets++;
			ifp->stats.tx_packets++;
			ifp->stats.tx_bytes += datalen;
		}
	}

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
	DHD_IF_CLR_TX_ACTIVE(ifp, DHD_TX_START_XMIT);
	dhd_os_tx_completion_wake(&dhd->pub);
	dhd_os_busbusy_wake(&dhd->pub);
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
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
	int ret;
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

#ifdef CFI_CHECK
netdev_tx_t BCMFASTPATH
#else
int BCMFASTPATH
#endif /* CFI_CHECK */
dhd_start_xmit_wrapper(struct sk_buff *skb, struct net_device *net)
{
	struct dhd_rx_tx_work *start_xmit_work;
	int ret;
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	if (dhd->pub.busstate == DHD_BUS_SUSPEND) {
		DHD_RPM(("%s: wakeup the bus using workqueue.\n", __FUNCTION__));

		dhd_netif_stop_queue(dhd->pub.bus);

		start_xmit_work = (struct dhd_rx_tx_work*)
			kmalloc(sizeof(*start_xmit_work), GFP_ATOMIC);

		if (!start_xmit_work) {
			netdev_err(net,
				   "error: failed to alloc start_xmit_work\n");
#ifdef CFI_CHECK
			ret = NETDEV_TX_BUSY;
#else
			ret = -ENOMEM;
#endif /* CFI_CHECK */
			goto exit;
		}

		INIT_WORK(&start_xmit_work->work, dhd_start_xmit_wq_adapter);
		start_xmit_work->skb = skb;
		start_xmit_work->net = net;
		queue_work(dhd->tx_wq, &start_xmit_work->work);
#ifdef CFI_CHECK
		ret = NETDEV_TX_OK;
#else
		ret = NET_XMIT_SUCCESS;
#endif /* CFI_CHECK */

	} else if (dhd->pub.busstate == DHD_BUS_DATA) {
		ret = dhd_start_xmit(skb, net);
	} else {
		/* when bus is down */
#ifdef CFI_CHECK
		ret = NETDEV_TX_BUSY;
#else
		ret = -ENODEV;
#endif /* CFI_CHECK */
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

	if ((state == ON) && (dhdp->txoff == FALSE)) {
		netif_stop_queue(net);
		dhd_prot_update_pktid_txq_stop_cnt(dhdp);
	} else if (state == ON) {
		DHD_ERROR(("%s: Netif Queue has already stopped\n", __FUNCTION__));
	}
	if ((state == OFF) && (dhdp->txoff == TRUE)) {
		netif_wake_queue(net);
		dhd_prot_update_pktid_txq_start_cnt(dhdp);
	} else if (state == OFF) {
		DHD_ERROR(("%s: Netif Queue has already started\n", __FUNCTION__));
	}
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
#endif // endif

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
}

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
	int ret = 0;
/* Ignore compiler warnings due to -Werror=cast-qual */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	struct delayed_work *dw = to_delayed_work(work);
	struct dhd_info *dhd =
		container_of(dw, struct dhd_info, event_log_dispatcher_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
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
		DHD_ERROR(("%s: thr_logtrace_ctl(%ld) succedded\n", __FUNCTION__,
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
			DHD_ERROR(("%s: thr_logtrace_ctl(%ld) succedded\n", __FUNCTION__,
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

#ifdef BCMPCIE
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
#endif /* BCMPCIE */
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
#if (defined(OEM_ANDROID) || defined(OEM_EMBEDDED_LINUX))
	int tout_rx = 0;
	int tout_ctrl = 0;
#endif /* OEM_ANDROID || OEM_EMBEDDED_LINUX */
	void *skbhead = NULL;
	void *skbprev = NULL;
	uint16 protocol;
	unsigned char *dump_data;
#ifdef DHD_MCAST_REGEN
	uint8 interface_role;
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;
#endif // endif
#ifdef DHD_WAKE_STATUS
	int pkt_wake = 0;
	wake_counts_t *wcp = NULL;
#endif /* DHD_WAKE_STATUS */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	BCM_REFERENCE(dump_data);

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
		pkt_wake = dhd_bus_get_bus_wake(dhdp);
		wcp = dhd_bus_get_wakecount(dhdp);
		if (wcp == NULL) {
			/* If wakeinfo count buffer is null do not  update wake count values */
			pkt_wake = 0;
		}
#endif /* DHD_WAKE_STATUS */

		eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);

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
			DHD_ERROR(("%s: ifp is NULL. drop packet\n",
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
#endif // endif
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
				}
				dhd_reset_tcpsync_info_by_ifp(ifp);
			}

		}
#endif /* DHDTCPSYNC_FLOOD_BLK */

#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpdata_info_get(dhdp, pktbuf);
#endif // endif
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
				(dhd_psta_proc(dhdp, ifidx, &pktbuf, FALSE) < 0)) {
			DHD_ERROR(("%s:%s: psta recv proc failed\n", __FUNCTION__,
				dhd_ifname(dhdp, ifidx)));
		}
#endif /* DHD_PSTA */

		DHD_TRACE(("\nAp isolate in dhd is %d\n", ifp->ap_isolate));
		if (ifidx >= 0 && dhdp != NULL && dhdp->info != NULL &&
				dhdp->info->iflist[ifidx] != NULL) {
			if ((DHD_IF_ROLE_AP(dhdp, ifidx) || DHD_IF_ROLE_P2PGO(dhdp, ifidx)) &&
				(!ifp->ap_isolate)) {
				DHD_TRACE(("%s: MACADDR: " MACDBG " ifidx %d\n",
						__FUNCTION__,
						MAC2STRDBG(dhdp->info->iflist[ifidx]->mac_addr),
						ifidx));
				DHD_TRACE(("%s: DEST: " MACDBG " ifidx %d\n",
						__FUNCTION__, MAC2STRDBG(eh->ether_dhost), ifidx));
				eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);
				if (ETHER_ISUCAST(eh->ether_dhost)) {
					if (dhd_find_sta(dhdp, ifidx, (void *)eh->ether_dhost)) {
						DHD_TRACE(("\nPacket not for us send down\n"));
						dhd_sendpkt(dhdp, ifidx, pktbuf);
						continue;
					}
				} else {
					void *npktbuf = PKTDUP(dhdp->osh, pktbuf);
					if (npktbuf) {
						DHD_TRACE(("\ncalling bcmc dhd_sendpkt"
									"and send dup up\n"));
						dhd_sendpkt(dhdp, ifidx, npktbuf);
					}
				}
			}
		}

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
		}
		dhd_rx_pkt_dump(dhdp, ifidx, dump_data, len);
		dhd_dump_pkt(dhdp, ifidx, dump_data, len, FALSE, NULL, NULL);

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

		DHD_DBG_PKT_MON_RX(dhdp, skb);
#ifdef DHD_PKT_LOGGING
		DHD_PKTLOG_RX(dhdp, skb);
#endif /* DHD_PKT_LOGGING */
		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* Process special event packets and then discard them */
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
#endif // endif
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

			ret_event = dhd_wl_host_event(dhd, ifidx, pkt_data, len, &event, &data);

			wl_event_to_host_order(&event);
#if (defined(OEM_ANDROID) || defined(OEM_EMBEDDED_LINUX))
			if (!tout_ctrl)
				tout_ctrl = DHD_PACKET_TIMEOUT_MS;
#endif /* (defined(OEM_ANDROID) || defined(OEM_EMBEDDED_LINUX)) */

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
#endif // endif
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
#endif // endif
				continue;
			}

			if (dhdp->wl_event_enabled) {
#ifdef DHD_USE_STATIC_CTRLBUF
				/* If event bufs are allocated via static buf pool
				 * and wl events are enabled, make a copy, free the
				 * local one and send the copy up.
				 */
				void *npkt = PKTDUP(dhdp->osh, skb);
				/* Clone event and send it up */
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
				if (npkt) {
					skb = npkt;
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
#if (defined(OEM_ANDROID) || defined(OEM_EMBEDDED_LINUX))
			tout_rx = DHD_PACKET_TIMEOUT_MS;
#endif /* OEM_ANDROID || OEM_EMBEDDED_LINUX */

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

#if defined(ARGOS_NOTIFY_CB)
		argos_register_notifier_deinit();
#endif // endif
#if defined(BCMPCIE) && defined(DHDTCPACK_SUPPRESS)
		dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif /* BCMPCIE && DHDTCPACK_SUPPRESS */
				DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
#if defined(DHD_LB_RXP)
				netif_receive_skb(skb);
#else /* !defined(DHD_LB_RXP) */
				netif_rx_ni(skb);
#endif /* defined(DHD_LB_RXP) */
				DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
			}
		}
	}

	if (dhd->rxthread_enabled && skbhead)
		dhd_sched_rxf(dhdp, skbhead);

#if (defined(OEM_ANDROID) || defined(OEM_EMBEDDED_LINUX))
	DHD_OS_WAKE_LOCK_RX_TIMEOUT_ENABLE(dhdp, tout_rx);
	DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(dhdp, tout_ctrl);
#endif /* OEM_ANDROID || OEM_EMBEDDED_LINUX */
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
#endif // endif
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
				dhd_bus_watchdog(&dhd->pub);

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
	dhdpcie_runtime_bus_wake(dhdp, CAN_SLEEP(), __builtin_return_address(0));
}

void dhd_runtime_pm_enable(dhd_pub_t *dhdp)
{
	/* Enable Runtime PM except for MFG Mode */
	if (!(dhdp->op_mode & DHD_FLAG_MFG_MODE)) {
		if (dhd_get_idletime(dhdp)) {
			dhd_os_runtimepm_timer(dhdp, dhd_runtimepm_ms);
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
#endif // endif
#ifdef CUSTOM_SET_CPUCORE
	dhd->pub.current_dpc = current;
#endif /* CUSTOM_SET_CPUCORE */
	/* Run until signal received */
	while (1) {
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
#endif // endif
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
		if (down_interruptible(&tsk->sema) == 0) {
			void *skb;
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
				netif_rx_ni(skb);
				skb = skbnext;
			}
#if defined(WAIT_DEQUEUE)
			if (OSL_SYSUPTIME() - watchdogTime > RXF_WATCHDOG_TIME) {
				OSL_SLEEP(1);
				watchdogTime = OSL_SYSUPTIME();
			}
#endif // endif

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
		dhd_bus_set_dpc_sched_time(dhdp);
		tasklet_schedule(&dhd->tasklet);
	}
}

static void
dhd_sched_rxf(dhd_pub_t *dhdp, void *skb)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	DHD_OS_WAKE_LOCK(dhdp);

	DHD_TRACE(("dhd_sched_rxf: Enter\n"));
	do {
		if (dhd_rxf_enqueue(dhdp, skb) == BCME_OK)
			break;
	} while (1);
	if (dhd->thr_rxf_ctl.thr_pid >= 0) {
		up(&dhd->thr_rxf_ctl.sema);
	}
	return;
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
#endif // endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* all ethtool calls start with a cmd word */
	if (copy_from_user(&cmd, uaddr, sizeof (uint32)))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:
		/* Copy out any request driver name */
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		strncpy(drvname, info.driver, sizeof(info.driver));
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

static bool dhd_check_hang(struct net_device *net, dhd_pub_t *dhdp, int error)
{
#if defined(OEM_ANDROID)
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return FALSE;
	}

	if (!dhdp->up)
		return FALSE;

#if !defined(BCMPCIE)
	if (dhdp->info->thr_dpc_ctl.thr_pid < 0) {
		DHD_ERROR(("%s : skipped due to negative pid - unloading?\n", __FUNCTION__));
		return FALSE;
	}
#endif // endif

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

void
dhd_rx_mon_pkt(dhd_pub_t *dhdp, host_rxbuf_cmpl_t* msg, void *pkt, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
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

		DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
		netif_rx_ni(dhd->monitor_skb);
		DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
	}

	dhd->monitor_skb = NULL;
}

typedef struct dhd_mon_dev_priv {
	struct net_device_stats stats;
} dhd_mon_dev_priv_t;

#define DHD_MON_DEV_PRIV_SIZE		(sizeof(dhd_mon_dev_priv_t))
#define DHD_MON_DEV_PRIV(dev)		((dhd_mon_dev_priv_t *)DEV_PRIV(dev))
#define DHD_MON_DEV_STATS(dev)		(((dhd_mon_dev_priv_t *)DEV_PRIV(dev))->stats)

#ifdef CFI_CHECK
static netdev_tx_t
#else
static int
#endif /* CFI_CHECK */
dhd_monitor_start(struct sk_buff *skb, struct net_device *dev)
{
	PKTFREE(NULL, skb, FALSE);
#ifdef CFI_CHECK
	return NETDEV_TX_OK;
#else
	return 0;
#endif /* CFI_CHECK */
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
dhd_add_monitor_if(dhd_info_t *dhd)
{
	struct net_device *dev;
	char *devname;
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
#endif // endif

#ifndef ARPHRD_IEEE80211_RADIOTAP
#define ARPHRD_IEEE80211_RADIOTAP	803 /* IEEE 802.11 + radiotap header */
#endif /* ARPHRD_IEEE80211_RADIOTAP */

	dev->type = ARPHRD_IEEE80211_RADIOTAP;

	dev->netdev_ops = &netdev_monitor_ops;

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
		DHD_ERROR(("%s: monitor i/f doesn't exist", __FUNCTION__));
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
			unregister_netdevice(dhd->monitor_dev);
		}
		dhd->monitor_dev = NULL;
	}
}

static void
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

	bcmerror = dhd_wl_ioctl(pub, ifidx, (wl_ioctl_t *)ioc, data_buf, buflen);

#ifdef WL_MONITOR
	/* Intercept monitor ioctl here, add/del monitor if */
	if (bcmerror == BCME_OK && ioc->cmd == WLC_SET_MONITOR) {
		int val = 0;
		if (data_buf != NULL && buflen != 0) {
			if (buflen >= 4) {
				val = *(int*)data_buf;
			} else if (buflen >= 2) {
				val = *(short*)data_buf;
			} else {
				val = *(char*)data_buf;
			}
		}
		dhd_set_monitor(pub, ifidx, val);
	}
#endif /* WL_MONITOR */

done:
#if defined(OEM_ANDROID)
	dhd_check_hang(net, pub, bcmerror);
#endif /* OEM_ANDROID */

	return bcmerror;
}

/**
 * Called by the OS (optionally via a wrapper function).
 * @param net  Linux per dongle instance
 * @param ifr  Linux request structure
 * @param cmd  e.g. SIOCETHTOOL
 */
static int
dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr, int cmd)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int ifidx;
	int ret;
	void *local_buf = NULL;           /**< buffer in kernel space */
	void __user *ioc_buf_user = NULL; /**< buffer in user space */
	u16 buflen = 0;

#ifdef ENABLE_INSMOD_NO_FW_LOAD
	allow_delay_fwdl = 1;
#endif /* ENABLE_INSMOD_NO_FW_LOAD */
	if (atomic_read(&exit_in_progress)) {
		DHD_ERROR(("%s module exit in progress\n", __func__));
		bcmerror = BCME_DONGLE_DOWN;
		return OSL_ERROR(bcmerror);
	}

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

#if defined(OEM_ANDROID)
#ifndef ENABLE_INSMOD_NO_FW_LOAD
	/* Interface up check for built-in type */
	if (!dhd_download_fw_on_driverload && dhd->pub.up == FALSE) {
		DHD_TRACE(("%s: Interface is down \n", __FUNCTION__));
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return OSL_ERROR(BCME_NOTUP);
	}
#endif /* ENABLE_INSMOD_NO_FW_LOAD */
#endif /* (OEM_ANDROID) */

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __FUNCTION__, ifidx, cmd));

#if defined(WL_STATIC_IF)
	/* skip for static ndev when it is down */
	if (dhd_is_static_ndev(&dhd->pub, net) && !(net->flags & IFF_UP)) {
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -1;
	}
#endif /* WL_STATIC_iF */

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

	if (cmd == SIOCETHTOOL) {
		ret = dhd_ethtool(dhd, (void*)ifr->ifr_data);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

#if defined(OEM_ANDROID)
	if (cmd == SIOCDEVPRIVATE+1) {
		ret = wl_android_priv_cmd(net, ifr);
		dhd_check_hang(net, &dhd->pub, ret);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

#endif /* OEM_ANDROID */

	if (cmd != SIOCDEVPRIVATE) {
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -EOPNOTSUPP;
	}

	memset(&ioc, 0, sizeof(ioc));

	{
		/* Copy the ioc control structure part of ioctl request */
		if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
			bcmerror = BCME_BADADDR;
			goto done;
		}

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

#if defined(OEM_ANDROID)
	/* Skip all the non DHD iovars (wl iovars) after f/w hang */
	if (ioc.driver != DHD_IOCTL_MAGIC && dhd->pub.hang_was_sent) {
		DHD_TRACE(("%s: HANG was sent up earlier\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(&dhd->pub, DHD_EVENT_TIMEOUT_MS);
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}
#endif /* OEM_ANDROID */

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
#if defined(OEM_ANDROID)
		mutex_init(&dhd->cpufreq_fix);
#endif // endif
		dhd->cpufreq_fix_status = FALSE;
	}
	return 0;
}

static void dhd_fix_cpu_freq(dhd_info_t *dhd)
{
#if defined(OEM_ANDROID)
	mutex_lock(&dhd->cpufreq_fix);
#endif // endif
	if (dhd && !dhd->cpufreq_fix_status) {
		pm_qos_add_request(&dhd->dhd_cpu_qos, PM_QOS_CPU_FREQ_MIN, 300000);
#ifdef FIX_BUS_MIN_CLOCK
		pm_qos_add_request(&dhd->dhd_bus_qos, PM_QOS_BUS_THROUGHPUT, 400000);
#endif /* FIX_BUS_MIN_CLOCK */
		DHD_ERROR(("pm_qos_add_requests called\n"));

		dhd->cpufreq_fix_status = TRUE;
	}
#if defined(OEM_ANDROID)
	mutex_unlock(&dhd->cpufreq_fix);
#endif // endif
}

static void dhd_rollback_cpu_freq(dhd_info_t *dhd)
{
#if defined(OEM_ANDROID)
	mutex_lock(&dhd ->cpufreq_fix);
#endif // endif
	if (dhd && dhd->cpufreq_fix_status != TRUE) {
#if defined(OEM_ANDROID)
		mutex_unlock(&dhd->cpufreq_fix);
#endif // endif
		return;
	}

	pm_qos_remove_request(&dhd->dhd_cpu_qos);
#ifdef FIX_BUS_MIN_CLOCK
	pm_qos_remove_request(&dhd->dhd_bus_qos);
#endif /* FIX_BUS_MIN_CLOCK */
	DHD_ERROR(("pm_qos_add_requests called\n"));

	dhd->cpufreq_fix_status = FALSE;
#if defined(OEM_ANDROID)
	mutex_unlock(&dhd->cpufreq_fix);
#endif // endif
}
#endif /* FIX_CPU_MIN_CLOCK */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int
dhd_ioctl_entry_wrapper(struct net_device *net, struct ifreq *ifr, int cmd)
{
	int error;
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	if (atomic_read(&dhd->pub.block_bus))
		return -EHOSTDOWN;

	if (pm_runtime_get_sync(dhd_bus_to_dev(dhd->pub.bus)) < 0)
		return BCME_ERROR;

	error = dhd_ioctl_entry(net, ifr, cmd);

	pm_runtime_mark_last_busy(dhd_bus_to_dev(dhd->pub.bus));
	pm_runtime_put_autosuspend(dhd_bus_to_dev(dhd->pub.bus));

	return error;
}
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

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
dhd_stop(struct net_device *net)
{
	int ifidx = 0;
	bool skip_reset = false;
#if defined(WL_CFG80211)
	unsigned long flags = 0;
#ifdef WL_STATIC_IF
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
#endif /* WL_STATIC_IF */
#endif /* WL_CFG80211 */
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);
	DHD_TRACE(("%s: Enter %p\n", __FUNCTION__, net));
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

	dhd_if_flush_sta(DHD_DEV_IFP(net));

#ifdef FIX_CPU_MIN_CLOCK
	if (dhd_get_fw_mode(dhd) == DHD_FLAG_HOSTAP_MODE)
		dhd_rollback_cpu_freq(dhd);
#endif /* FIX_CPU_MIN_CLOCK */

	ifidx = dhd_net2idx(dhd, net);
	BCM_REFERENCE(ifidx);

	DHD_ERROR(("%s: ######### dhd_stop called for ifidx=%d #########\n", __FUNCTION__, ifidx));

#if defined(WL_STATIC_IF) && defined(WL_CFG80211)
	/* If static if is operational, don't reset the chip */
	if (static_if_ndev_get_state(cfg, net) == NDEV_STATE_FW_IF_CREATED) {
		DHD_ERROR(("static if operational. skip chip reset.\n"));
		skip_reset = true;
		wl_cfg80211_sta_ifdown(net);
		goto exit;
	}
#endif /* WL_STATIC_IF && WL_CFG80211 */

#if defined(WL_VIF_SUPPORT)
	if (vif_num > 0) {
		DHD_ERROR(("virtual if operational. skip chip reset.\n"));
		skip_reset = true;
		wl_cfg80211_sta_ifdown(net);
		goto exit;
	}
#endif /* WL_VIF_SUPPORT */

	DHD_ERROR(("%s: making dhdpub up FALSE\n", __FUNCTION__));
#ifdef WL_CFG80211

	/* Disable Runtime PM before interface down */
	DHD_DISABLE_RUNTIME_PM(&dhd->pub);

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
		/*
		 * For CFG80211: Clean up all the left over virtual interfaces
		 * when the primary Interface is brought down. [ifconfig wlan0 down]
		 */
		if (!dhd_download_fw_on_driverload) {
			DHD_STATLOG_CTRL(&dhd->pub, ST(WLAN_POWER_OFF), ifidx, 0);
			if ((dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) &&
				(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)) {
				int i;
#ifdef WL_CFG80211_P2P_DEV_IF
				wl_cfg80211_del_p2p_wdev(net);
#endif /* WL_CFG80211_P2P_DEV_IF */
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
			cancel_work_sync(dhd->dhd_deferred_wq);

#ifdef SHOW_LOGTRACE
			/* Wait till event logs work/kthread finishes */
			dhd_cancel_logtrace_process_sync(dhd);
#endif /* SHOW_LOGTRACE */

#if defined(DHD_LB_RXP)
			__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
			skb_queue_purge(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */
		}

#if defined(ARGOS_NOTIFY_CB)
		argos_register_notifier_deinit();
#endif // endif
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

	DHD_SSSR_DUMP_DEINIT(&dhd->pub);

#ifdef PROP_TXSTATUS
	dhd_wlfc_cleanup(&dhd->pub, NULL, 0);
#endif // endif
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
	if (skip_reset == false) {
#if defined(WL_CFG80211) && defined(OEM_ANDROID)
		if (ifidx == 0 && !dhd_download_fw_on_driverload) {
#if defined(BT_OVER_SDIO)
			dhd_bus_put(&dhd->pub, WLAN_MODULE);
			wl_android_set_wifi_on_flag(FALSE);
#else
			wl_android_wifi_off(net, TRUE);
#endif /* BT_OVER_SDIO */
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
#endif /* defined(WL_CFG80211) && defined(OEM_ANDROID) */
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
#endif // endif
	}

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	/* Destroy wakelock */
	if (!dhd_download_fw_on_driverload &&
		(dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) &&
		(skip_reset == false)) {
		DHD_OS_WAKE_LOCK_DESTROY(dhd);
		dhd->dhd_state &= ~DHD_ATTACH_STATE_WAKELOCKS_INIT;
	}

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

static int
dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
#ifdef TOE
	uint32 toe_ol;
#endif // endif
	int ifidx;
	int32 ret = 0;

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

	if (dhd->pub.up == 1) {
		/* already up */
		DHD_ERROR(("Primary net_device is already up \n"));
		mutex_unlock(&dhd->pub.ndev_op_sync);
		return BCME_OK;
	}

	if (!dhd_download_fw_on_driverload) {
		if (!dhd_driver_init_done) {
			DHD_ERROR(("%s: WLAN driver is not initialized\n", __FUNCTION__));
			mutex_unlock(&dhd->pub.ndev_op_sync);
			return -1;
		}
		/* Init wakelock */
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

#if defined(MULTIPLE_SUPPLICANT)
#if defined(OEM_ANDROID) && defined(BCMSDIO)
	if (mutex_is_locked(&_dhd_sdio_mutex_lock_) != 0) {
		DHD_ERROR(("%s : dhd_open: call dev open before insmod complete!\n", __FUNCTION__));
	}
	mutex_lock(&_dhd_sdio_mutex_lock_);
#endif // endif
#endif /* MULTIPLE_SUPPLICANT */

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);
	dhd->pub.dongle_trap_occured = 0;
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
#endif // endif

#if defined(OEM_ANDROID) && !defined(WL_CFG80211)
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

	if (ifidx == 0) {
		atomic_set(&dhd->pend_8021x_cnt, 0);
#if defined(WL_CFG80211) && defined(OEM_ANDROID)
		if (!dhd_download_fw_on_driverload) {
			DHD_ERROR(("\n%s\n", dhd_version));
			DHD_STATLOG_CTRL(&dhd->pub, ST(WLAN_POWER_ON), ifidx, 0);
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
#endif /* defined(WL_CFG80211) && defined(OEM_ANDROID) */

		if (dhd->pub.busstate != DHD_BUS_DATA) {

			/* try to bring up bus */
			DHD_PERIM_UNLOCK(&dhd->pub);

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
			if (pm_runtime_get_sync(dhd_bus_to_dev(dhd->pub.bus)) >= 0) {
				ret = dhd_bus_start(&dhd->pub);
				pm_runtime_mark_last_busy(dhd_bus_to_dev(dhd->pub.bus));
				pm_runtime_put_autosuspend(dhd_bus_to_dev(dhd->pub.bus));
			}
#else
			ret = dhd_bus_start(&dhd->pub);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

			DHD_PERIM_LOCK(&dhd->pub);
			if (ret) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				ret = -1;
				goto exit;
			}

		}

#ifdef BT_OVER_SDIO
		if (dhd->pub.is_bt_recovery_required) {
			DHD_ERROR(("%s: Send Hang Notification 2 to BT\n", __FUNCTION__));
			bcmsdh_btsdio_process_dhd_hang_notification(TRUE);
		}
		dhd->pub.is_bt_recovery_required = FALSE;
#endif // endif

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

#if defined(DHD_CONTROL_PCIE_ASPM_WIFI_TURNON)
		dhd_bus_aspm_enable_rc_ep(dhd->pub.bus, TRUE);
#endif /* DHD_CONTROL_PCIE_ASPM_WIFI_TURNON */
#if defined(DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON)
		dhd_irq_set_affinity(&dhd->pub, cpumask_of(0));
#endif /* DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON */
#ifdef DHD_LB_IRQSET
		dhd_irq_set_affinity(&dhd->pub, dhd->cpumask_primary);
#endif /* DHD_LB_IRQSET */
#if defined(ARGOS_NOTIFY_CB)
		argos_register_notifier_init(net);
#endif // endif
#if defined(BCMPCIE) && defined(DHDTCPACK_SUPPRESS)
#if defined(SET_RPS_CPUS)
		dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#else
		dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif // endif
#endif /* BCMPCIE && DHDTCPACK_SUPPRESS */
#if defined(NUM_SCB_MAX_PROBE)
		dhd_set_scb_probe(&dhd->pub);
#endif /* NUM_SCB_MAX_PROBE */
#endif /* WL_CFG80211 */
	}

	dhd->pub.up = 1;

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
#endif // endif

exit:
	mutex_unlock(&dhd->pub.ndev_op_sync);
	if (ret) {
		dhd_stop(net);
	}

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

#if defined(MULTIPLE_SUPPLICANT)
#if defined(OEM_ANDROID) && defined(BCMSDIO)
	mutex_unlock(&_dhd_sdio_mutex_lock_);
#endif // endif
#endif /* MULTIPLE_SUPPLICANT */

	return ret;
}

/*
 * ndo_start handler for primary ndev
 */
static int
dhd_pri_open(struct net_device *net)
{
	s32 ret;

	ret = dhd_open(net);
	if (unlikely(ret)) {
		DHD_ERROR(("Failed to open primary dev ret %d\n", ret));
		return ret;
	}

	/* Allow transmit calls */
	netif_start_queue(net);
	DHD_ERROR(("[%s] tx queue started\n", net->name));
	return ret;
}

/*
 * ndo_stop handler for primary ndev
 */
static int
dhd_pri_stop(struct net_device *net)
{
	s32 ret;

	/* stop tx queue */
	netif_stop_queue(net);
	DHD_ERROR(("[%s] tx queue stopped\n", net->name));

	ret = dhd_stop(net);
	if (unlikely(ret)) {
		DHD_ERROR(("dhd_stop failed: %d\n", ret));
		return ret;
	}

	return ret;
}

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

	cfg = wl_get_cfg(net);
	primary_netdev = bcmcfg_to_prmry_ndev(cfg);

	if (!is_static_iface(cfg, net)) {
		DHD_TRACE(("non-static interface (%s)..do nothing \n", net->name));
		ret = BCME_OK;
		goto done;
	}

	DHD_INFO(("[%s][STATIC_IF] Enter \n", net->name));
	/* Ensure fw is initialized. If it is already initialized,
	 * dhd_open will return success.
	 */
	ret = dhd_open(primary_netdev);
	if (unlikely(ret)) {
		DHD_ERROR(("Failed to open primary dev ret %d\n", ret));
		goto done;
	}

	ret = wl_cfg80211_static_if_open(net);
	if (!ret) {
		/* Allow transmit calls */
		netif_start_queue(net);
	}
done:
	return ret;
}

static int
dhd_static_if_stop(struct net_device *net)
{
	struct bcm_cfg80211 *cfg;
	struct net_device *primary_netdev = NULL;
	int ret = BCME_OK;
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	DHD_INFO(("[%s][STATIC_IF] Enter \n", net->name));

	/* Ensure queue is disabled */
	netif_tx_disable(net);

	cfg = wl_get_cfg(net);
	if (!is_static_iface(cfg, net)) {
		DHD_TRACE(("non-static interface (%s)..do nothing \n", net->name));
		return BCME_OK;
	}

	ret = wl_cfg80211_static_if_close(net);

	if (dhd->pub.up == 0) {
		/* If fw is down, return */
		DHD_ERROR(("fw down\n"));
		return BCME_OK;
	}
	/* If STA iface is not in operational, invoke dhd_close from this
	* context.
	*/
	primary_netdev = bcmcfg_to_prmry_ndev(cfg);
	if (!(primary_netdev->flags & IFF_UP)) {
		ret = dhd_stop(primary_netdev);
	} else {
		DHD_ERROR(("Skipped dhd_stop, as sta is operational\n"));
	}

	return ret;
}
#endif /* WL_STATIC_IF && WL_CF80211 */

int dhd_do_driver_init(struct net_device *net)
{
	dhd_info_t *dhd = NULL;

	if (!net) {
		DHD_ERROR(("Primary Interface not initialized \n"));
		return -EINVAL;
	}

#ifdef MULTIPLE_SUPPLICANT
#if defined(OEM_ANDROID) && defined(BCMSDIO)
	if (mutex_is_locked(&_dhd_sdio_mutex_lock_) != 0) {
		DHD_ERROR(("%s : dhdsdio_probe is already running!\n", __FUNCTION__));
		return 0;
	}
#endif /* OEM_ANDROID & BCMSDIO */
#endif /* MULTIPLE_SUPPLICANT */

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
			ifevent->ifidx, name, mac, ifevent->bssidx, ifevent->role) == BCME_OK)
		return BCME_OK;
#endif // endif

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
#ifdef WL_CFG80211
	wl_cfg80211_notify_ifchange(dhd_linux_get_primary_netdev(&dhdinfo->pub),
		ifevent->ifidx, name, mac, ifevent->bssidx);
#endif /* WL_CFG80211 */
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
			strlcpy(ifp->dngl_name, dngl_name, IFNAMSIZ);
		} else if (ndev->name[0] != '\0') {
			strlcpy(ifp->dngl_name, ndev->name, IFNAMSIZ);
		}
		if (mac != NULL) {
			(void)memcpy_s(&ifp->mac_addr, ETHER_ADDR_LEN, mac, ETHER_ADDR_LEN);
		}
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
#endif // endif
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
	if (dngl_name) {
		strncpy(ifp->dngl_name, dngl_name, IFNAMSIZ);
	} else if (name) {
		strncpy(ifp->dngl_name, name, IFNAMSIZ);
	}

	/* Initialize STA info list */
	INIT_LIST_HEAD(&ifp->sta_list);
	DHD_IF_STA_LIST_LOCK_INIT(ifp);

#ifdef DHD_L2_FILTER
	ifp->phnd_arp_table = init_l2_filter_arp_table(dhdpub->osh);
	ifp->parp_allnode = TRUE;
#endif /* DHD_L2_FILTER */

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
		ifp = NULL;
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

		dhd_if_del_sta_list(ifp);
#ifdef PCIE_FULL_DONGLE
		/* Delete flowrings of virtual interface */
		ifidx = ifp->idx;
		if ((ifidx != 0) && (if_flow_lkup[ifidx].role != WLC_E_IF_ROLE_AP)) {
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

	if (!(ifp = dhd_get_ifp_by_ndev(dhdp, net)) ||
			(ifp->idx >= DHD_MAX_IFS)) {
		DHD_ERROR(("Wrong ifidx: %p, %d\n", ifp, ifp ? ifp->idx : -1));
		ASSERT(0);
		return;
	}

	dhd_cleanup_ifp(dhdp, ifp);
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

#ifdef WL_STATIC_IF
		/* static IF will be handled in detach */
		if (ifp->static_if) {
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
#if defined(SET_RPS_CPUS)
#if (defined(DHDTCPACK_SUPPRESS) && defined(BCMPCIE))
				dhd_tcpack_suppress_set(dhdpub, TCPACK_SUP_OFF);
#endif /* DHDTCPACK_SUPPRESS && BCMPCIE */
#endif // endif
				if (need_rtnl_lock)
					unregister_netdev(ifp->net);
				else
					unregister_netdevice(ifp->net);
			}
			ifp->net = NULL;
			DHD_GENERAL_LOCK(dhdpub, flags);
			ifp->del_in_progress = false;
			DHD_GENERAL_UNLOCK(dhdpub, flags);
		}
		dhd_cleanup_ifp(dhdpub, ifp);
		DHD_CUMM_CTR_INIT(&ifp->cumm_ctr);

		MFREE(dhdinfo->pub.osh, ifp, sizeof(*ifp));
		ifp = NULL;
	}

	return BCME_OK;
}

static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_pri_open,
	.ndo_stop = dhd_pri_stop,
	.ndo_get_stats = dhd_get_stats,
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	.ndo_do_ioctl = dhd_ioctl_entry_wrapper,
	.ndo_start_xmit = dhd_start_xmit_wrapper,
#else
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif // endif
};

static struct net_device_ops dhd_ops_virt = {
#if defined(WL_CFG80211) && defined(WL_STATIC_IF)
	.ndo_open = dhd_static_if_open,
	.ndo_stop = dhd_static_if_stop,
#endif // endif
	.ndo_get_stats = dhd_get_stats,
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	.ndo_do_ioctl = dhd_ioctl_entry_wrapper,
	.ndo_start_xmit = dhd_start_xmit_wrapper,
#else
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif // endif
};

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
	int error = 0;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0) */
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t fs;
#endif /* KERNEL_DS && USER_DS */
	char *raw_fmts =  NULL;
	int logstrs_size = 0;

#if defined(KERNEL_DS) && defined(USER_DS)
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */

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
	logstrs_size = i_size_read(file_inode(filep));
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0) */
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
		DHD_ERROR_NO_HW4(("%s: Failed to read file %s\n", __FUNCTION__, logstrs_path));
		goto fail;
	}

	if (dhd_parse_logstrs_file(osh, raw_fmts, logstrs_size, temp)
				== BCME_OK) {
		filp_close(filep, NULL);
#if defined(KERNEL_DS) && defined(USER_DS)
		set_fs(fs);
#endif /* KERNEL_DS && USER_DS */
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

#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(fs);
#endif /* KERNEL_DS && USER_DS */

	temp->fmts = NULL;
	return BCME_ERROR;
}

static int
dhd_read_map(osl_t *osh, char *fname, uint32 *ramstart, uint32 *rodata_start,
		uint32 *rodata_end)
{
	struct file *filep = NULL;
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t fs;
#endif /* KERNEL_DS && USER_DS */

	int err = BCME_ERROR;

	if (fname == NULL) {
		DHD_ERROR(("%s: ERROR fname is NULL \n", __FUNCTION__));
		return BCME_ERROR;
	}

#if defined(KERNEL_DS) && defined(USER_DS)
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */

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

#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(fs);
#endif /* KERNEL_DS && USER_DS */

	return err;
}

static int
dhd_init_static_strs_array(osl_t *osh, dhd_event_log_t *temp, char *str_file, char *map_file)
{
	struct file *filep = NULL;
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t fs;
#endif /* KERNEL_DS && USER_DS */
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

#if defined(KERNEL_DS) && defined(USER_DS)
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */

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

	raw_fmts = MALLOC(osh, logstrs_size);
	if (raw_fmts == NULL) {
		DHD_ERROR(("%s: Failed to allocate raw_fmts memory \n", __FUNCTION__));
		goto fail;
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
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(fs);
#endif /* KERNEL_DS && USER_DS */

	return BCME_OK;

fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
		raw_fmts = NULL;
	}

fail1:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(fs);
#endif /* KERNEL_DS && USER_DS */

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = NULL;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = NULL;
	}

	return error;
} /* dhd_init_static_strs_array */

#endif /* SHOW_LOGTRACE */

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

/** Called once for each hardware (dongle) instance that this DHD manages */
dhd_pub_t *
dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen)
{
	dhd_info_t *dhd = NULL;
	struct net_device *net = NULL;
	char if_name[IFNAMSIZ] = {'\0'};
	uint32 bus_type = -1;
	uint32 bus_num = -1;
	uint32 slot_num = -1;
#ifdef SHOW_LOGTRACE
	int ret;
#endif /* SHOW_LOGTRACE */
#ifdef DHD_ERPOM
	pom_func_handler_t *pom_handler;
#endif /* DHD_ERPOM */
	wifi_adapter_info_t *adapter = NULL;

	dhd_attach_states_t dhd_state = DHD_ATTACH_STATE_INIT;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef PCIE_FULL_DONGLE
	ASSERT(sizeof(dhd_pkttag_fd_t) <= OSL_PKTTAG_SZ);
	ASSERT(sizeof(dhd_pkttag_fr_t) <= OSL_PKTTAG_SZ);
#endif /* PCIE_FULL_DONGLE */

	/* will implement get_ids for DBUS later */
#if defined(BCMSDIO)
	dhd_bus_get_ids(bus, &bus_type, &bus_num, &slot_num);
#endif // endif
	adapter = dhd_wifi_platform_get_adapter(bus_type, bus_num, slot_num);

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

#ifdef SHOW_LOGTRACE
	/* Create ring proc entries */
	dhd_dbg_ring_proc_create(&dhd->pub);

	if (dhd_init_logtrace_process(dhd) != BCME_OK) {
		goto fail;
	}
#endif /* SHOW_LOGTRACE */

	dhd->unit = dhd_found + instance_base; /* do not increment dhd_found, yet */

	dhd->pub.osh = osh;
#ifdef DUMP_IOCTL_IOV_LIST
	dll_init(&(dhd->pub.dump_iovlist_head));
#endif /* DUMP_IOCTL_IOV_LIST */
	dhd->pub.dhd_console_ms = dhd_console_ms; /* assigns default value */
	dhd->adapter = adapter;
#ifdef BT_OVER_SDIO
	dhd->pub.is_bt_recovery_required = FALSE;
	mutex_init(&dhd->bus_user_lock);
#endif /* BT_OVER_SDIO */

	g_dhd_pub = &dhd->pub;

#ifdef DHD_DEBUG
	dll_init(&(dhd->pub.mw_list_head));
#endif /* DHD_DEBUG */

#ifdef GET_CUSTOM_MAC_ENABLE
	wifi_platform_get_mac_addr(dhd->adapter, dhd->pub.mac.octet);
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
	dhd->thr_dpc_ctl.thr_pid = DHD_PID_KT_TL_INVALID;
	dhd->thr_wdt_ctl.thr_pid = DHD_PID_KT_INVALID;
#ifdef DHD_WET
	dhd->pub.wet_info = dhd_get_wet_info(&dhd->pub);
#endif /* DHD_WET */
	/* Initialize thread based operation and lock */
	sema_init(&dhd->sdsem, 1);

	/* Some DHD modules (e.g. cfg80211) configures operation mode based on firmware name.
	 * This is indeed a hack but we have to make it work properly before we have a better
	 * solution
	 */
	dhd_update_fw_nv_path(dhd);
	dhd->pub.pcie_txs_metadata_enable = pcie_txs_metadata_enable;

	/* Link to info module */
	dhd->pub.info = dhd;

	/* Link to bus module */
	dhd->pub.bus = bus;
	dhd->pub.hdrlen = bus_hdrlen;
	dhd->pub.txoff = FALSE;

	/* Set network interface name if it was provided as module parameter */
	if (iface_name[0]) {
		int len;
		char ch;
		strncpy(if_name, iface_name, IFNAMSIZ);
		if_name[IFNAMSIZ - 1] = 0;
		len = strlen(if_name);
		ch = if_name[len - 1];
		if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
			strncat(if_name, "%d", 2);
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
#endif // endif
	net->netdev_ops = NULL;

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
	init_waitqueue_head(&dhd->ctrl_wait);
	init_waitqueue_head(&dhd->dhd_bus_busy_state_wait);
	init_waitqueue_head(&dhd->dmaxfer_wait);
	init_waitqueue_head(&dhd->pub.tx_completion_wait);
	dhd->pub.dhd_bus_busy_state = 0;
	/* Initialize the spinlocks */
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);
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
	/* wakelocks prevent a system from going into a low power state */
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	wake_lock_init(&dhd->wl_wdwake, WAKE_LOCK_SUSPEND, "wlan_wd_wake");
#endif /* CONFIG_PM_WAKELOCKS  || CONFIG_HAS_WAKELOCK */

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

#ifdef WL_CFG80211
	spin_lock_init(&dhd->pub.up_lock);
	/* Attach and link in the cfg80211 */
	if (unlikely(wl_cfg80211_attach(net, &dhd->pub))) {
		DHD_ERROR(("wl_cfg80211_attach failed\n"));
		goto fail;
	}

#ifdef DHD_MONITOR_INTERFACE
	dhd_monitor_init(&dhd->pub);
#endif /* DHD_MONITOR_INTERFACE */
	dhd_state |= DHD_ATTACH_STATE_CFG80211;
#endif /* WL_CFG80211 */

#if defined(WL_WIRELESS_EXT)
	/* Attach and link in the iw */
	if (!(dhd_state &  DHD_ATTACH_STATE_CFG80211)) {
		if (wl_iw_attach(net, (void *)&dhd->pub) != 0) {
		DHD_ERROR(("wl_iw_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_WL_ATTACH;
	}
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
#if defined(SHOW_LOGTRACE) && defined(DBG_RING_LOG_INIT_DEFAULT)
	/* enable verbose ring to support dump_trace_buf */
	dhd_os_start_logging(&dhd->pub, FW_VERBOSE_RING_NAME, 3, 0, 0, 0);
#endif /* SHOW_LOGTRACE */

#ifdef DBG_PKT_MON
	dhd->pub.dbg->pkt_mon_lock = dhd_os_spin_lock_init(dhd->pub.osh);
#ifdef DBG_PKT_MON_INIT_DEFAULT
	dhd_os_dbg_attach_pkt_monitor(&dhd->pub);
#endif /* DBG_PKT_MON_INIT_DEFAULT */
#endif /* DBG_PKT_MON */
#endif /* DEBUGABILITY */

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
#endif /* SHOW_LOGTRACE */

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
#if defined(OEM_ANDROID)
	INIT_WORK(&dhd->dhd_hang_process_work, dhd_hang_process);
#endif /* #if OEM_ANDROID */
#ifdef DEBUG_CPU_FREQ
	dhd->new_freq = alloc_percpu(int);
	dhd->freq_trans.notifier_call = dhd_cpufreq_notifier;
	cpufreq_register_notifier(&dhd->freq_trans, CPUFREQ_TRANSITION_NOTIFIER);
#endif // endif
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMSDIO
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_DELAYTX);
#elif defined(BCMPCIE)
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

#if defined(DHD_LB)

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
#if defined(DHD_LB_TXC)
	tasklet_init(&dhd->tx_compl_tasklet,
		dhd_lb_tx_compl_handler, (ulong)(&dhd->pub));
	INIT_WORK(&dhd->tx_compl_dispatcher_work, dhd_tx_compl_dispatcher_fn);
	DHD_INFO(("%s load balance init tx_compl_tasklet\n", __FUNCTION__));
#endif /* DHD_LB_TXC */
#if defined(DHD_LB_RXC)
	tasklet_init(&dhd->rx_compl_tasklet,
		dhd_lb_rx_compl_handler, (ulong)(&dhd->pub));
	INIT_WORK(&dhd->rx_compl_dispatcher_work, dhd_rx_compl_dispatcher_fn);
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

#if defined(DNGL_AXI_ERROR_LOGGING) && defined(DHD_USE_WQ_FOR_DNGL_AXI_ERROR)
	INIT_WORK(&dhd->axi_error_dispatcher_work, dhd_axi_error_dispatcher_fn);
#endif /* DNGL_AXI_ERROR_LOGGING && DHD_USE_WQ_FOR_DNGL_AXI_ERROR */

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
#endif /* BCMPCIE && ETD */

	DHD_SSSR_MEMPOOL_INIT(&dhd->pub);

#ifdef EWP_EDL
	if (host_edl_support) {
		if (DHD_EDL_MEM_INIT(&dhd->pub) != BCME_OK) {
			host_edl_support = FALSE;
		}
	}
#endif /* EWP_EDL */

	(void)dhd_sysfs_init(dhd);

#ifdef WL_NATOE
	/* Open Netlink socket for NF_CONNTRACK notifications */
	dhd->pub.nfct = dhd_ct_open(&dhd->pub, NFNL_SUBSYS_CTNETLINK | NFNL_SUBSYS_CTNETLINK_EXP,
			CT_ALL);
#endif /* WL_NATOE */

	dhd_state |= DHD_ATTACH_STATE_DONE;
	dhd->dhd_state = dhd_state;

	dhd_found++;

#ifdef DHD_DUMP_MNGR
	dhd->pub.dump_file_manage =
		(dhd_dump_file_manage_t *)MALLOCZ(dhd->pub.osh, sizeof(dhd_dump_file_manage_t));
	if (unlikely(!dhd->pub.dump_file_manage)) {
		DHD_ERROR(("%s(): could not allocate memory for - "
					"dhd_dump_file_manage_t\n", __FUNCTION__));
	}
#endif /* DHD_DUMP_MNGR */
#ifdef DHD_FW_COREDUMP
	/* Set memdump default values */
#ifdef CUSTOMER_HW4_DEBUG
	dhd->pub.memdump_enabled = DUMP_DISABLED;
#elif defined(OEM_ANDROID)
	dhd->pub.memdump_enabled = DUMP_MEMFILE_BUGON;
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
	const char *fw = NULL;
	const char *nv = NULL;
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
	if (!dhd_download_fw_on_driverload) {
#ifdef CONFIG_BCMDHD_FW_PATH
		fw = VENDOR_PATH CONFIG_BCMDHD_FW_PATH;
#endif /* CONFIG_BCMDHD_FW_PATH */
#ifdef CONFIG_BCMDHD_NVRAM_PATH
		nv = VENDOR_PATH CONFIG_BCMDHD_NVRAM_PATH;
#endif /* CONFIG_BCMDHD_NVRAM_PATH */
		fw = "/vendor/etc/firmware/fw_firmware_pcie.bin";
		nv = "/vendor/etc/firmware/nvram.txt";
	}

	/* check if we need to initialize the path */
	if (dhdinfo->fw_path[0] == '\0') {
		if (adapter && adapter->fw_path && adapter->fw_path[0] != '\0')
			fw = adapter->fw_path;
	}
	if (dhdinfo->nv_path[0] == '\0') {
		if (adapter && adapter->nv_path && adapter->nv_path[0] != '\0')
			nv = adapter->nv_path;
	}

	/* Use module parameter if it is valid, EVEN IF the path has not been initialized
	 *
	 * TODO: need a solution for multi-chip, can't use the same firmware for all chips
	 */
	if (firmware_path[0] != '\0')
		fw = firmware_path;

	if (nvram_path[0] != '\0')
		nv = nvram_path;

	fw = "/vendor/etc/firmware/fw_bcm88459_pcie.bin";
	nv = "/vendor/etc/firmware/nvram_cyw88459.txt";
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
		dhdinfo->nv_path[nv_len] = '\0';
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

	/* clear the path in module parameter */
	if (dhd_download_fw_on_driverload) {
		firmware_path[0] = '\0';
		nvram_path[0] = '\0';
	}
#ifdef DHD_UCODE_DOWNLOAD
	ucode_path[0] = '\0';
	DHD_ERROR(("ucode path: %s\n", dhdinfo->uc_path));
#endif /* DHD_UCODE_DOWNLOAD */

	/* fw_path and nv_path are not mandatory for BCMEMBEDIMAGE */
	if (dhdinfo->fw_path[0] == '\0') {
		DHD_ERROR(("firmware path not found\n"));
		return FALSE;
	}
	if (dhdinfo->nv_path[0] == '\0') {
		DHD_ERROR(("nvram path not found\n"));
		return FALSE;
	}

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

#ifdef BCM4375_CHIP
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
	dhdp->dongle_trap_occured = 0;
#ifdef DHD_SSSR_DUMP
	/* Flag to indicate sssr dump is collected */
	dhdp->sssr_dump_collected = 0;
#endif /* DHD_SSSR_DUMP */
	dhdp->iovar_timeout_occured = 0;
#ifdef PCIE_FULL_DONGLE
	dhdp->d3ack_timeout_occured = 0;
	dhdp->livelock_occured = 0;
	dhdp->pktid_audit_failed = 0;
#endif /* PCIE_FULL_DONGLE */
	dhd->pub.iface_op_failed = 0;
	dhd->pub.scan_timeout_occurred = 0;
	dhd->pub.scan_busy_occurred = 0;
	/* Clear induced error during initialize */
	dhd->pub.dhd_induce_error = DHD_INDUCE_ERROR_CLEAR;

	/* set default value for now. Will be updated again in dhd_preinit_ioctls()
	 * after querying FW
	 */
	dhdp->event_log_max_sets = NUM_EVENT_LOG_SETS;
	dhdp->event_log_max_sets_queried = FALSE;
	dhdp->smmu_fault_occurred = 0;
#ifdef DNGL_AXI_ERROR_LOGGING
	dhdp->axi_error = FALSE;
#endif /* DNGL_AXI_ERROR_LOGGING */

	DHD_PERIM_LOCK(dhdp);
	/* try to download image and nvram to the dongle */
	if  (dhd->pub.busstate == DHD_BUS_DOWN && dhd_update_fw_nv_path(dhd)) {
		/* Indicate FW Download has not yet done */
		dhd->pub.fw_download_status = FW_DOWNLOAD_IN_PROGRESS;
		DHD_INFO(("%s download fw %s, nv %s\n", __FUNCTION__, dhd->fw_path, dhd->nv_path));
#if defined(DHD_DEBUG) && defined(BCMSDIO)
		fw_download_start = OSL_SYSUPTIME();
#endif /* DHD_DEBUG && BCMSDIO */
		ret = dhd_bus_download_firmware(dhd->pub.bus, dhd->pub.osh,
		                                dhd->fw_path, dhd->nv_path);
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
		dhd->pub.fw_download_status = FW_DOWNLOAD_DONE;
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
		DHD_DISABLE_RUNTIME_PM(&dhd->pub);
		DHD_PERIM_UNLOCK(dhdp);
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
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
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

#if defined(CONFIG_SOC_EXYNOS8895) || defined(CONFIG_SOC_EXYNOS9810) || \
	defined(CONFIG_SOC_EXYNOS9820)
	DHD_ERROR(("%s: Enable L1ss EP side\n", __FUNCTION__));
	exynos_pcie_l1ss_ctrl(1, PCIE_L1SS_CTRL_WIFI);
#endif /* CONFIG_SOC_EXYNOS8895 || CONFIG_SOC_EXYNOS9810 || CONFIG_SOC_EXYNOS9820 */

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

	DHD_PERIM_UNLOCK(dhdp);

	return 0;
}
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
#endif // endif

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
			DHD_ERROR(("%s aibss is not supported\n",
				__FUNCTION__));
			return BCME_OK;
		} else {
			DHD_ERROR(("%s Set aibss to %d failed  %d\n",
				__FUNCTION__, aibss, ret));
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

#ifdef WL_BAM
	if (on) {
		dhd_check_adps_bad_ap(dhd);
	}
#endif	/* WL_BAM */

exit:
	if (iov_buf) {
		MFREE(dhd->osh, iov_buf, len);
		iov_buf = NULL;
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
	uint32 event_log_max_sets = 0;
	char* iov_buf = NULL;
	int ret2 = 0;
	uint32 wnm_cap = 0;
#if defined(BCMSUP_4WAY_HANDSHAKE)
	uint32 sup_wpa = 1;
#endif /* BCMSUP_4WAY_HANDSHAKE */
#if defined(CUSTOM_AMPDU_BA_WSIZE) || (defined(WLAIBSS) && \
	defined(CUSTOM_IBSS_AMPDU_BA_WSIZE))
	uint32 ampdu_ba_wsize = 0;
#endif /* CUSTOM_AMPDU_BA_WSIZE ||(WLAIBSS && CUSTOM_IBSS_AMPDU_BA_WSIZE) */
#if defined(CUSTOM_AMPDU_MPDU)
	int32 ampdu_mpdu = 0;
#endif // endif
#if defined(CUSTOM_AMPDU_RELEASE)
	int32 ampdu_release = 0;
#endif // endif
#if defined(CUSTOM_AMSDU_AGGSF)
	int32 amsdu_aggsf = 0;
#endif // endif

#if defined(BCMSDIO)
#ifdef PROP_TXSTATUS
	int wlfc_enable = TRUE;
#ifndef DISABLE_11N
	uint32 hostreorder = 1;
	uint chipid = 0;
#endif /* DISABLE_11N */
#endif /* PROP_TXSTATUS */
#endif // endif
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

#ifdef OEM_ANDROID
#ifdef DHD_ENABLE_LPC
	uint32 lpc = 1;
#endif /* DHD_ENABLE_LPC */
	uint power_mode = PM_FAST;
#if defined(BCMSDIO)
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = CUSTOM_GLOM_SETTING;
#endif /* defined(BCMSDIO) */
#if (defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)) && defined(USE_WL_CREDALL)
	uint32 credall = 1;
#endif // endif
	uint bcn_timeout = CUSTOM_BCN_TIMEOUT;
	uint scancache_enab = TRUE;
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
	uint32 bcn_li_bcn = 1;
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
	uint retry_max = CUSTOM_ASSOC_RETRY_MAX;
#if defined(ARP_OFFLOAD_SUPPORT)
	int arpoe = 0;
#endif // endif
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
#endif // endif
#if (defined(AP) && !defined(WLP2P)) || (!defined(AP) && defined(WL_CFG80211))
	struct ether_addr p2p_ea;
#endif // endif
#ifdef BCMCCX
	uint32 ccx = 1;
#endif // endif
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
#endif /* GET_CUSTOM_MAC_ENABLE */
#ifdef OKC_SUPPORT
	uint32 okc = 1;
#endif // endif

#ifdef DISABLE_11N
	uint32 nmode = 0;
#endif /* DISABLE_11N */

#ifdef USE_WL_TXBF
	uint32 txbf = 1;
#endif /* USE_WL_TXBF */
#ifdef DISABLE_TXBFR
	uint32 txbf_bfr_cap = 0;
#endif /* DISABLE_TXBFR */
#ifdef AMPDU_VO_ENABLE
	struct ampdu_tid_control tid;
#endif // endif
#if defined(PROP_TXSTATUS)
#ifdef USE_WFA_CERT_CONF
	uint32 proptx = 0;
#endif /* USE_WFA_CERT_CONF */
#endif /* PROP_TXSTATUS */
#ifdef DHD_SET_FW_HIGHSPEED
	uint32 ack_ratio = 250;
	uint32 ack_ratio_depth = 64;
#endif /* DHD_SET_FW_HIGHSPEED */
#if defined(SUPPORT_2G_VHT) || defined(SUPPORT_5G_1024QAM_VHT)
	uint32 vht_features = 0; /* init to 0, will be set based on each support */
#endif /* SUPPORT_2G_VHT || SUPPORT_5G_1024QAM_VHT */
#ifdef DISABLE_11N_PROPRIETARY_RATES
	uint32 ht_features = 0;
#endif /* DISABLE_11N_PROPRIETARY_RATES */
#ifdef CUSTOM_PSPRETEND_THR
	uint32 pspretend_thr = CUSTOM_PSPRETEND_THR;
#endif // endif
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
	wl_wlc_version_t wlc_ver;

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

#if defined(CUSTOM_COUNTRY_CODE) && (defined(CUSTOMER_HW2) || defined(BOARD_HIKEY))
	/* clear AP flags */
	dhd->dhd_cflags &= ~WLAN_PLAT_AP_FLAG;
#endif /* CUSTOM_COUNTRY_CODE && (CUSTOMER_HW2 || BOARD_HIKEY) */

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
#if defined(BCMSDIO) || defined(BCMPCIE) || defined(BCMSPI)
		dhd_set_version_info(dhd, buf);
#endif /* BCMSDIO || BCMPCIE */
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
		DHD_ERROR(("%s: axierror_logbuf_addr : 0x%x\n", __FUNCTION__,
			dhd->axierror_logbuf_addr));
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
	ret = wifi_platform_get_mac_addr(dhd->info->adapter, ea_addr.octet);
	if (!ret) {
		ret = dhd_iovar(dhd, 0, "cur_etheraddr", (char *)&ea_addr, ETHER_ADDR_LEN, NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			ret = BCME_NOTUP;
			goto done;
		}
		memcpy(dhd->mac.octet, ea_addr.octet, ETHER_ADDR_LEN);
	} else {
#endif /* GET_CUSTOM_MAC_ENABLE */
		/* Get the default device MAC address directly from firmware */
		ret = dhd_iovar(dhd, 0, "cur_etheraddr", NULL, 0, (char *)&buf, sizeof(buf), FALSE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
			ret = BCME_NOTUP;
			goto done;
		}
		/* Update public MAC address after reading from Firmware */
		memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);

#ifdef GET_CUSTOM_MAC_ENABLE
	}
#endif /* GET_CUSTOM_MAC_ENABLE */

	if ((ret = dhd_apply_default_clm(dhd, clm_path)) < 0) {
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
#endif // endif
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif // endif
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
		dhdsdio_func_blocksize(dhd, 2, sd_f2_blocksize);
#endif /* USE_DYNAMIC_F2_BLKSIZE */
#ifdef SOFTAP_UAPSD_OFF
		ret = dhd_iovar(dhd, 0, "wme_apsd", (char *)&wme_apsd, sizeof(wme_apsd), NULL, 0,
				TRUE);
		if (ret < 0) {
				DHD_ERROR(("%s: set wme_apsd 0 fail (error=%d)\n",
				__FUNCTION__, ret));
		}
#endif /* SOFTAP_UAPSD_OFF */
#if defined(CUSTOM_COUNTRY_CODE) && (defined(CUSTOMER_HW2) || defined(BOARD_HIKEY))
		/* set AP flag for specific country code of SOFTAP */
		dhd->dhd_cflags |= WLAN_PLAT_AP_FLAG | WLAN_PLAT_NODFS_FLAG;
#endif /* CUSTOM_COUNTRY_CODE && (CUSTOMER_HW2 || BOARD_HIKEY) */
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
#ifndef CUSTOM_SET_ANTNPM
#ifndef IGUANA_LEGACY_CHIPS
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
#endif /* IGUANA_LEGACY_CHIPS */
#endif /* !CUSTOM_SET_ANTNPM */
	} else {
		uint32 concurrent_mode = 0;
		if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_P2P_MODE) ||
			(op_mode == DHD_FLAG_P2P_MODE)) {
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 0;
#endif // endif
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif // endif
			dhd->op_mode = DHD_FLAG_P2P_MODE;
		} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_IBSS_MODE) ||
			(op_mode == DHD_FLAG_IBSS_MODE)) {
			dhd->op_mode = DHD_FLAG_IBSS_MODE;
		} else
			dhd->op_mode = DHD_FLAG_STA_MODE;
#if defined(OEM_ANDROID) && !defined(AP) && defined(WLP2P)
		if (dhd->op_mode != DHD_FLAG_IBSS_MODE &&
			(concurrent_mode = dhd_get_concurrent_capabilites(dhd))) {
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 1;
#endif // endif
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
#endif // endif
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
#if defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
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
#endif /* CUSTOMER_HW2 || BOARD_HIKEY */

#if defined(RXFRAME_THREAD) && defined(RXTHREAD_ONLYSTA)
	if (dhd->op_mode == DHD_FLAG_HOSTAP_MODE)
		dhd->info->rxthread_enabled = FALSE;
	else
		dhd->info->rxthread_enabled = TRUE;
#endif // endif
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
	ret = dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar), NULL, 0, TRUE);
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */
#if defined(ROAM_ENABLE)
#ifdef DISABLE_BCNLOSS_ROAM
	ret = dhd_iovar(dhd, 0, "roam_bcnloss_off", (char *)&roam_bcnloss_off,
			sizeof(roam_bcnloss_off), NULL, 0, TRUE);
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
#endif /* ROAM_ENABLE */

#ifdef CUSTOM_EVENT_PM_WAKE
	ret = dhd_iovar(dhd, 0, "const_awake_thresh", (char *)&pm_awake_thresh,
			sizeof(pm_awake_thresh), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set const_awake_thresh failed %d\n", __FUNCTION__, ret));
	}
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef OKC_SUPPORT
	ret = dhd_iovar(dhd, 0, "okc_enable", (char *)&okc, sizeof(okc), NULL, 0, TRUE);
#endif // endif
#ifdef BCMCCX
	ret = dhd_iovar(dhd, 0, "ccx_enable", (char *)&ccx, sizeof(ccx), NULL, 0, TRUE);
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
		if ((ret = dhd_enable_adps(dhd, ADPS_ENABLE)) != BCME_OK) {
			DHD_ERROR(("%s dhd_enable_adps failed %d\n",
					__FUNCTION__, ret));
		}
	}
#endif /* WLADPS */

#ifdef DHD_PM_CONTROL_FROM_FILE
	sec_control_pm(dhd, &power_mode);
#else
#ifndef H2_BRING_UP
	/* Set PowerSave mode */
	(void) dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode), TRUE, 0);
#endif // endif
#endif /* DHD_PM_CONTROL_FROM_FILE */

#if defined(BCMSDIO)
	/* Match Host and Dongle rx alignment */
	ret = dhd_iovar(dhd, 0, "bus:txglomalign", (char *)&dongle_align, sizeof(dongle_align),
			NULL, 0, TRUE);

#if (defined(CUSTOMER_HW2) || defined(BOARD_HIKEY))&& defined(USE_WL_CREDALL)
	/* enable credall to reduce the chance of no bus credit happened. */
	ret = dhd_iovar(dhd, 0, "bus:credall", (char *)&credall, sizeof(credall), NULL, 0, TRUE);
#endif // endif

#ifdef USE_WFA_CERT_CONF
	if (sec_get_param_wfa_cert(dhd, SET_PARAM_BUS_TXGLOM_MODE, &glom) == BCME_OK) {
		DHD_ERROR(("%s, read txglom param =%d\n", __FUNCTION__, glom));
	}
#endif /* USE_WFA_CERT_CONF */
	if (glom != DEFAULT_GLOM_VALUE) {
		DHD_INFO(("%s set glom=0x%X\n", __FUNCTION__, glom));
		ret = dhd_iovar(dhd, 0, "bus:txglom", (char *)&glom, sizeof(glom), NULL, 0, TRUE);
	}
#endif /* defined(BCMSDIO) */

	/* Setup timeout if Beacons are lost and roam is off to report link down */
	ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout, sizeof(bcn_timeout), NULL, 0,
			TRUE);

	/* Setup assoc_retry_max count to reconnect target AP in dongle */
	ret = dhd_iovar(dhd, 0, "assoc_retry_max", (char *)&retry_max, sizeof(retry_max), NULL, 0,
			TRUE);

#if defined(AP) && !defined(WLP2P)
	ret = dhd_iovar(dhd, 0, "apsta", (char *)&apsta, sizeof(apsta), NULL, 0, TRUE);

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
#endif // endif

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
#endif // endif
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
			NULL, 0, FALSE);
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
#endif // endif

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
	setbit(eventmask, WLC_E_START);
	setbit(eventmask, WLC_E_ASSOC_IND);
	setbit(eventmask, WLC_E_PSK_SUP);
	setbit(eventmask, WLC_E_LINK);
	setbit(eventmask, WLC_E_MIC_ERROR);
	setbit(eventmask, WLC_E_ASSOC_REQ_IE);
	setbit(eventmask, WLC_E_ASSOC_RESP_IE);
#ifdef LIMIT_BORROW
	setbit(eventmask, WLC_E_ALLOW_CREDIT_BORROW);
#endif // endif
#ifndef WL_CFG80211
	setbit(eventmask, WLC_E_PMKID_CACHE);
	setbit(eventmask, WLC_E_TXFAIL);
#endif // endif
	setbit(eventmask, WLC_E_JOIN_START);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
#ifdef DHD_DEBUG
	setbit(eventmask, WLC_E_SCAN_CONFIRM_IND);
#endif // endif
#ifdef PNO_SUPPORT
	setbit(eventmask, WLC_E_PFN_NET_FOUND);
	setbit(eventmask, WLC_E_PFN_BEST_BATCHING);
	setbit(eventmask, WLC_E_PFN_BSSID_NET_FOUND);
	setbit(eventmask, WLC_E_PFN_BSSID_NET_LOST);
#endif /* PNO_SUPPORT */
	/* enable dongle roaming event */
#ifdef WL_CFG80211
#if !defined(ROAM_EVT_DISABLE)
	setbit(eventmask, WLC_E_ROAM);
#endif /* !ROAM_EVT_DISABLE */
	setbit(eventmask, WLC_E_BSSID);
#endif /* WL_CFG80211 */
#ifdef BCMCCX
	setbit(eventmask, WLC_E_ADDTS_IND);
	setbit(eventmask, WLC_E_DELTS_IND);
#endif /* BCMCCX */
#ifdef WLTDLS
	setbit(eventmask, WLC_E_TDLS_PEER_EVENT);
#endif /* WLTDLS */
#ifdef RTT_SUPPORT
	setbit(eventmask, WLC_E_PROXD);
#endif /* RTT_SUPPORT */
#if !defined(WL_CFG80211) && !defined(OEM_ANDROID)
	setbit(eventmask, WLC_E_ESCAN_RESULT);
#endif // endif
#ifdef WL_CFG80211
	setbit(eventmask, WLC_E_ESCAN_RESULT);
	setbit(eventmask, WLC_E_AP_STARTED);
	setbit(eventmask, WLC_E_ACTION_FRAME_RX);
	if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
		setbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);
	}
#endif /* WL_CFG80211 */
#ifdef WLAIBSS
	setbit(eventmask, WLC_E_AIBSS_TXFAIL);
#endif /* WLAIBSS */

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
#ifdef CUSTOM_EVENT_PM_WAKE
	setbit(eventmask, WLC_E_EXCESS_PM_WAKE_EVENT);
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef DHD_LOSSLESS_ROAMING
	setbit(eventmask, WLC_E_ROAM_PREP);
#endif // endif
	/* nan events */
	setbit(eventmask, WLC_E_NAN);
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
#endif // endif
#ifdef WL_NATOE
		setbit(eventmask_msg->mask, WLC_E_NATOE_NFCT);
#endif /* WL_NATOE */
#ifdef WL_NAN
		setbit(eventmask_msg->mask, WLC_E_SLOTTED_BSS_PEER_OP);
#endif /* WL_NAN */
#ifdef WL_MBO
		setbit(eventmask_msg->mask, WLC_E_MBO);
#endif /* WL_MBO */
#ifdef WL_BCNRECV
		setbit(eventmask_msg->mask, WLC_E_BCNRECV_ABORTED);
#endif /* WL_BCNRECV */
#ifdef WL_CAC_TS
		setbit(eventmask_msg->mask, WLC_E_ADDTS_IND);
		setbit(eventmask_msg->mask, WLC_E_DELTS_IND);
#endif /* WL_CAC_TS */
#ifdef WL_CHAN_UTIL
		setbit(eventmask_msg->mask, WLC_E_BSS_LOAD);
#endif /* WL_CHAN_UTIL */
#ifdef WL_SAE
		setbit(eventmask_msg->mask, WLC_E_EXT_AUTH_REQ);
		setbit(eventmask_msg->mask, WLC_E_EXT_AUTH_FRAME_RX);
		setbit(eventmask_msg->mask, WLC_E_MGMT_FRAME_TXSTATUS);
		setbit(eventmask_msg->mask, WLC_E_MGMT_FRAME_OFF_CHAN_COMPLETE);
#endif /* WL_SAE */
#ifndef CONFIG_SOC_S5E5515
		setbit(eventmask_msg->mask, WLC_E_IND_DOS_STATUS);
#endif // endif
#ifdef ENABLE_HOGSQS
		setbit(eventmask_msg->mask, WLC_E_LDF_HOGGER);
#endif /* ENABLE_HOGSQS */

		/* over temp event */
		setbit(eventmask_msg->mask, WLC_E_OVERTEMP);

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
	ret = dhd_iovar(dhd, 0, "event_log_tag_control", (char *)el_tag, sizeof(*el_tag), NULL, 0,
			TRUE);
#endif /* DHD_8021X_DUMP */

#ifdef OEM_ANDROID
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
			sizeof(scan_assoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
			sizeof(scan_unassoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_PASSIVE_TIME, (char *)&scan_passive_time,
			sizeof(scan_passive_time), TRUE, 0);

#ifdef ARP_OFFLOAD_SUPPORT
	/* Set and enable ARP offload feature for STA only  */
#if defined(OEM_ANDROID) && defined(SOFTAP)
	if (arpoe && !ap_fw_loaded) {
#else
	if (arpoe) {
#endif /* defined(OEM_ANDROID) && defined(SOFTAP) */
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
		dhd->pktfilter_count = 10;
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
	ret = dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn, sizeof(bcn_li_bcn), NULL, 0,
			TRUE);
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
#ifdef AMPDU_VO_ENABLE
	tid.tid = PRIO_8021D_VO; /* Enable TID(6) for voice */
	tid.enable = TRUE;
	ret = dhd_iovar(dhd, 0, "ampdu_tid", (char *)&tid, sizeof(tid), NULL, 0, TRUE);

	tid.tid = PRIO_8021D_NC; /* Enable TID(7) for voice */
	tid.enable = TRUE;
	ret = dhd_iovar(dhd, 0, "ampdu_tid", (char *)&tid, sizeof(tid), NULL, 0, TRUE);
#endif // endif
	/* query for 'clmver' to get clm version info from firmware */
	memset(buf, 0, sizeof(buf));
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
				memset(clm_version, 0, CLM_VER_STR_LEN);
				strncpy(clm_version, ver_temp_buf,
					MIN(strlen(ver_temp_buf) + 1, CLM_VER_STR_LEN - 1));
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
			DHD_ERROR(("CLM version = %s\n", clm_version));
		} else {
			DHD_ERROR(("Couldn't find CLM version!\n"));
		}
	}

#ifdef WRITE_WLANINFO
	sec_save_wlinfo(fw_version, EPI_VERSION_STR, dhd->info->nv_path, clm_version);
#endif /* WRITE_WLANINFO */

	/* query for 'wlc_ver' to get version info from firmware */
	memset(&wlc_ver, 0, sizeof(wl_wlc_version_t));
	ret = dhd_iovar(dhd, 0, "wlc_ver", NULL, 0, (char *)&wlc_ver,
		sizeof(wl_wlc_version_t), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		dhd->wlc_ver_major = wlc_ver.wlc_ver_major;
		dhd->wlc_ver_minor = wlc_ver.wlc_ver_minor;
	}
#endif /* defined(OEM_ANDROID) */
#ifdef GEN_SOFTAP_INFO_FILE
	sec_save_softap_info();
#endif /* GEN_SOFTAP_INFO_FILE */

#if defined(BCMSDIO) && !defined(BCMSPI)
	dhd_txglom_enable(dhd, TRUE);
#endif /* BCMSDIO && !BCMSPI */

#if defined(BCMSDIO)
#ifdef PROP_TXSTATUS
	if (disable_proptx ||
#ifdef PROP_TXSTATUS_VSDB
		/* enable WLFC only if the firmware is VSDB when it is in STA mode */
		(!FW_SUPPORTED(dhd, ap)) ||
#endif /* PROP_TXSTATUS_VSDB */
		FALSE) {
		wlfc_enable = FALSE;
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
	ret2 = dhd_iovar(dhd, 0, "ampdu_hostreorder", (char *)&hostreorder, sizeof(hostreorder),
			NULL, 0, TRUE);
	chipid = dhd_bus_chip_id(dhd);
	if (ret2 < 0) {
		DHD_ERROR(("%s wl ampdu_hostreorder failed %d\n", __FUNCTION__, ret2));
		if (ret2 != BCME_UNSUPPORTED && chipid != BCM4373_CHIP_ID)
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
			if (ret2 != BCME_UNSUPPORTED && chipid != BCM4373_CHIP_ID)
				ret = ret2;
		}
		if (ret2 != BCME_OK)
			hostreorder = 0;
	}
#endif /* DISABLE_11N */

	if (wlfc_enable)
		dhd_wlfc_init(dhd);
#ifndef DISABLE_11N
	else if (hostreorder)
		dhd_wlfc_hostreorder_init(dhd);
#endif /* DISABLE_11N */

#endif /* PROP_TXSTATUS */
#endif /* BCMSDIO || BCMBUS */
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
#endif // endif
#ifdef RTT_SUPPORT
	if (!dhd->rtt_state) {
		ret = dhd_rtt_init(dhd);
		if (ret < 0) {
			DHD_ERROR(("%s failed to initialize RTT\n", __FUNCTION__));
		}
	}
#endif // endif
#ifdef FILTER_IE
	/* Failure to configure filter IE is not a fatal error, ignore it. */
	if (!(dhd->op_mode & (DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE)))
		dhd_read_from_file(dhd);
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
#endif // endif
#ifdef WBTEXT
		| WL_WNM_BSSTRANS | WL_WNM_MAXIDLE
#endif // endif
		;
#if defined(WL_MBO) && defined(WL_OCE)
	if (FW_SUPPORTED(dhd, estm)) {
		wnm_cap |= WL_WNM_ESTM;
	}
#endif /* WL_MBO && WL_OCE */
	if (dhd_iovar(dhd, 0, "wnm", (char *)&wnm_cap, sizeof(wnm_cap), NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set WNM capabilities\n"));
	}

	if (FW_SUPPORTED(dhd, ecounters) && enable_ecounter) {
		dhd_ecounter_configure(dhd, TRUE);
	}

	/* store the preserve log set numbers */
	if (dhd_get_preserve_log_numbers(dhd, &dhd->logset_prsrv_mask)
			!= BCME_OK) {
		DHD_ERROR(("%s: Failed to get preserve log # !\n", __FUNCTION__));
	}

#if defined(WBTEXT) && defined(WBTEXT_BTMDELTA)
	if (dhd_iovar(dhd, 0, "wnm_btmdelta", (char *)&btmdelta, sizeof(btmdelta),
			NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set BTM delta\n"));
	}
#endif /* WBTEXT && WBTEXT_BTMDELTA */

#ifdef WL_MONITOR
	if (FW_SUPPORTED(dhd, monitor)) {
		dhd->monitor_enable = TRUE;
		DHD_ERROR(("%s: Monitor mode is enabled in FW cap\n", __FUNCTION__));
	} else {
		dhd->monitor_enable = FALSE;
		DHD_ERROR(("%s: Monitor mode is not enabled in FW cap\n", __FUNCTION__));
	}
#endif /* WL_MONITOR */

#ifdef CONFIG_SILENT_ROAM
	dhd->sroam_turn_on = TRUE;
	dhd->sroamed = FALSE;
#endif /* CONFIG_SILENT_ROAM */

done:

	if (eventmask_msg) {
		MFREE(dhd->osh, eventmask_msg, msglen);
		eventmask_msg = NULL;
	}
	if (iov_buf) {
		MFREE(dhd->osh, iov_buf, WLC_IOCTL_SMLEN);
		iov_buf = NULL;
	}
#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	if (el_tag) {
		MFREE(dhd->osh, el_tag, sizeof(wl_el_tag_params_t));
		el_tag = NULL;
	}
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
		buf = MALLOCZ(pub->osh, input_len);
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
			buf = MALLOCZ(pub->osh, input_len);
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
	if (buf) {
		MFREE(pub->osh, buf, input_len);
		buf = NULL;
	}
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
		DHD_ERROR(("%s: Must be down to change its MTU", dev->name));
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
#endif // endif
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
#endif // endif
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
			DHD_ARPOE(("%s:add aliased IP to AOE hostip cache\n",
				__FUNCTION__));
			aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, TRUE, idx);
#endif /* AOE_IP_ALIAS_SUPPORT */
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
	BCM_REFERENCE(primary_ndev);

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
		memcpy(dhd->iflist[0]->mac_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
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
			memcpy(dhd->iflist[ifidx]->mac_addr, temp_addr, ETHER_ADDR_LEN);
		}
#endif /* defined(OEM_ANDROID) */
	}

	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
	net->ethtool_ops = &dhd_ethtool_ops;

#if defined(WL_WIRELESS_EXT)
#if WIRELESS_EXT < 19
	net->get_wireless_stats = dhd_get_wireless_stats;
#endif /* WIRELESS_EXT < 19 */
#if WIRELESS_EXT > 12
	net->wireless_handlers = &wl_iw_handler_def;
#endif /* WIRELESS_EXT > 12 */
#endif /* defined(WL_WIRELESS_EXT) */

	dhd->pub.rxsz = DBUS_RX_BUFFER_SIZE_DHD(net);

	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	if (ifidx == 0)
		printf("%s\n", dhd_version);

	if (need_rtnl_lock)
		err = register_netdev(net);
	else
		err = register_netdevice(net);

	if (err != 0) {
		DHD_ERROR(("couldn't register the net device [%s], err %d\n", net->name, err));
		goto fail;
	}

	printf("Register interface [%s]  MAC: "MACDBG"\n\n", net->name,
#if defined(CUSTOMER_HW4_DEBUG)
		MAC2STRDBG(dhd->pub.mac.octet));
#else
		MAC2STRDBG(net->dev_addr));
#endif /* CUSTOMER_HW4_DEBUG */

#if defined(OEM_ANDROID) && defined(SOFTAP) && defined(WL_WIRELESS_EXT) && \
	!defined(WL_CFG80211)
		wl_iw_iscan_set_scan_broadcast_prep(net, 1);
#endif // endif

#if defined(OEM_ANDROID) && (defined(BCMPCIE) || defined(BCMLXSDMMC))
	if (ifidx == 0) {
#ifdef BCMLXSDMMC
		up(&dhd_registration_sem);
#endif /* BCMLXSDMMC */
#ifndef ENABLE_INSMOD_NO_FW_LOAD
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
			dhd_net_bus_devreset(net, TRUE);
#ifdef BCMLXSDMMC
			dhd_net_bus_suspend(net);
#endif /* BCMLXSDMMC */
			wifi_platform_set_power(dhdp->info->adapter, FALSE, WIFI_TURNOFF_DELAY);
#if defined(BT_OVER_SDIO)
			dhd->bus_user_count--;
#endif /* BT_OVER_SDIO */
		}
#endif /* ENABLE_INSMOD_NO_FW_LOAD */
	}
#endif /* OEM_ANDROID && (BCMPCIE || BCMLXSDMMC) */
	return 0;

fail:
	net->netdev_ops = NULL;
	return err;
}

#ifdef WL_VIF_SUPPORT
#define MAX_VIF_NUM 8
int
dhd_register_vif(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	dhd_if_t *ifp;
	struct net_device *net;
	int err = BCME_OK, i;
	char viface_name[IFNAMSIZ] = {'\0'};
	ifp = dhd->iflist[0];
	net = ifp->net;
	if (vif_num && vif_num > MAX_VIF_NUM)
		vif_num = MAX_VIF_NUM;
	/* Set virtual interface name if it was provided as module parameter */
	if (vif_name[0]) {
		int len;
		char ch;
		strncpy(viface_name, vif_name, IFNAMSIZ);
		viface_name[IFNAMSIZ - 1] = 0;
		len = strlen(viface_name);
		ch = viface_name[len - 1];
		if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
			strcat(viface_name, "%d");
	} else {
		DHD_ERROR(("%s check vif_name\n", __FUNCTION__));
		return BCME_BADOPTION;
	}

	DHD_INFO(("%s Virtual interface [%s]:\n", __FUNCTION__, viface_name));
	rtnl_lock();
	for (i = 0; i < vif_num; i++) {
		if (wl_cfg80211_add_if(wl_get_cfg(net), net, WL_IF_TYPE_STA, viface_name, NULL)
			== NULL) {
			DHD_ERROR(("%s error Virtual interface [%s], i:%d\n", __FUNCTION__,
				viface_name, i));
			break;
		}
	}
	rtnl_unlock();
	return err;
}
#endif /* WL_VIF_SUPPORT */
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
			if (dhd->pub.busstate != DHD_BUS_DOWN) {
				/* Stop the protocol module */
				dhd_prot_stop(&dhd->pub);

				/* Stop the bus module */
				dhd_bus_stop(dhd->pub.bus, TRUE);
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
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = NULL;
#endif // endif
	if (!dhdp)
		return;

	dhd = (dhd_info_t *)dhdp->info;
	if (!dhd)
		return;

	if (dhd->iflist[0])
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

#ifdef WL_CFG80211
	if (dev)
		wl_cfg80211_down(dev);
#endif /* WL_CFG80211 */

	if (dhd->dhd_state & DHD_ATTACH_STATE_PROT_ATTACH) {

#if defined(OEM_ANDROID) || !defined(BCMSDIO)
		dhd_bus_detach(dhdp);
#endif /* OEM_ANDROID || !BCMSDIO */
#ifdef OEM_ANDROID
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
		wl_iw_detach();
	}
#endif /* defined(WL_WIRELESS_EXT) */

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
		if (ifp && ifp->net) {

#ifdef WL_CFG80211
			cfg = wl_get_cfg(ifp->net);
#endif // endif
			/* in unregister_netdev case, the interface gets freed by net->destructor
			 * (which is set to free_netdev)
			 */
			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
#if defined(ARGOS_NOTIFY_CB)
				argos_register_notifier_deinit();
#endif // endif
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

#ifdef WL_NATOE
	if (dhd->pub.nfct) {
		dhd_ct_close(dhd->pub.nfct);
	}
#endif /* WL_NATOE */

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

		/* Unregister from CPU Hotplug framework */
		dhd_unregister_cpuhp_callback(dhd);

		dhd_cpumasks_deinit(dhd);
		DHD_LB_STATS_DEINIT(&dhd->pub);
	}
#endif /* DHD_LB */

#if defined(DNGL_AXI_ERROR_LOGGING) && defined(DHD_USE_WQ_FOR_DNGL_AXI_ERROR)
	cancel_work_sync(&dhd->axi_error_dispatcher_work);
#endif /* DNGL_AXI_ERROR_LOGGING && DHD_USE_WQ_FOR_DNGL_AXI_ERROR */

	DHD_SSSR_MEMPOOL_DEINIT(&dhd->pub);

#ifdef WL_CFG80211
	if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
		if (!cfg) {
			DHD_ERROR(("cfg NULL!\n"));
			ASSERT(0);
		} else {
			wl_cfg80211_detach(cfg);
#ifdef DHD_MONITOR_INTERFACE
			dhd_monitor_uninit();
#endif /* DHD_MONITOR_INTERFACE */
		}
	}
#endif /* WL_CFG80211 */

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
		dhd_os_spin_lock_deinit(dhd->pub.osh, dhd->pub.dbg->pkt_mon_lock);
#endif /* DBG_PKT_MON */
	}
#endif /* DEBUGABILITY */
	if (dhdp->dbg) {
		dhd_os_dbg_detach(dhdp);
	}
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
#ifdef PNO_SUPPORT
	if (dhdp->pno_state)
		dhd_pno_deinit(dhdp);
#endif // endif
#ifdef RTT_SUPPORT
	if (dhdp->rtt_state) {
		dhd_rtt_deinit(dhdp);
	}
#endif // endif
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
#endif // endif
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd->wakelock_wd_counter = 0;
	wake_lock_destroy(&dhd->wl_wdwake);
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
	if (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) {
		DHD_TRACE(("wd wakelock count:%d\n", dhd->wakelock_wd_counter));
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
#endif // endif

#if defined(WLTDLS) && defined(PCIE_FULL_DONGLE)
		dhd_free_tdls_peer_list(dhdp);
#endif // endif

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
		if (dhd != NULL) {

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
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_bus_unregister();

#if defined(OEM_ANDROID)
	wl_android_exit();
#endif /* OEM_ANDROID */

	dhd_wifi_platform_unregister_drv();
}

static void __exit
dhd_module_exit(void)
{
	atomic_set(&exit_in_progress, 1);
	dhd_module_cleanup();
	unregister_reboot_notifier(&dhd_reboot_notifier);
	dhd_destroy_to_notifier_skt();
}

static int __init
dhd_module_init(void)
{
	int err;
	int retry = POWERUP_MAX_RETRY;

	DHD_ERROR(("%s in\n", __FUNCTION__));

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

	DHD_ERROR(("%s out\n", __FUNCTION__));

	return err;
}

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

#if defined(CONFIG_DEFERRED_INITCALLS) && !defined(EXYNOS_PCIE_MODULE_PATCH)
#if defined(CONFIG_MACH_UNIVERSAL7420) || defined(CONFIG_SOC_EXYNOS8890) || \
	defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MSM8998) || \
	defined(CONFIG_SOC_EXYNOS8895) || defined(CONFIG_SOC_EXYNOS9810) || \
	defined(CONFIG_ARCH_SDM845) || defined(CONFIG_SOC_EXYNOS9820) || \
	defined(CONFIG_ARCH_SM8150)
deferred_module_init_sync(dhd_module_init);
#else
deferred_module_init(dhd_module_init);
#endif /* CONFIG_MACH_UNIVERSAL7420 || CONFIG_SOC_EXYNOS8890 ||
	* CONFIG_ARCH_MSM8996 || CONFIG_ARCH_MSM8998 || CONFIG_SOC_EXYNOS8895
	* CONFIG_SOC_EXYNOS9810 || CONFIG_ARCH_SDM845 || CONFIG_SOC_EXYNOS9820
	* CONFIG_ARCH_SM8150
	*/
#elif defined(USE_LATE_INITCALL_SYNC)
late_initcall_sync(dhd_module_init);
#else
late_initcall(dhd_module_init);
#endif /* USE_LATE_INITCALL_SYNC */

module_exit(dhd_module_exit);

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
	timeout = msecs_to_jiffies(D3_ACK_RESP_TIMEOUT);

	DHD_PERIM_UNLOCK(pub);

	timeout = wait_event_timeout(dhd->d3ack_wait, (*condition), timeout);

	DHD_PERIM_LOCK(pub);

	return timeout;
}

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

	DHD_PERIM_UNLOCK(pub);
	ret = wait_event_timeout(dhd->dmaxfer_wait, (*condition), timeout);
	DHD_PERIM_LOCK(pub);

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
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;

	if (extend)
		dhd_os_wd_timer(bus, WATCHDOG_EXTEND_INTERVAL);
	else
		dhd_os_wd_timer(bus, dhd->default_wd_interval);
}

void
dhd_os_wd_timer(void *bus, uint wdtick)
{
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
			DHD_ERROR(("DHD Runtime PM Enabled \n"));
		}
	} else {
		/* tick is zero, we have to stop the timer */
		/* Stop the timer only if its running, otherwise we don't have to do anything */
		if (dhd->rpm_timer_valid == TRUE) {
			dhd->rpm_timer_valid = FALSE;
			DHD_GENERAL_UNLOCK(pub, flags);
			del_timer_sync(&dhd->rpm_timer);
			DHD_ERROR(("DHD Runtime PM Disabled \n"));
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
	rdlen = compat_kernel_read(fp, fp->f_pos, buf, MIN(len, size));

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

	rd_len = compat_kernel_read(fp, fp->f_pos, str, len);
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

	if (dhd_dpc_prio >= 0)
		down(&dhd->sdsem);
	else
		spin_lock_bh(&dhd->sdlock);
}

void
dhd_os_sdunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd_dpc_prio >= 0)
		up(&dhd->sdsem);
	else
		spin_unlock_bh(&dhd->sdlock);
}

void
dhd_os_sdlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_bh(&dhd->txqlock);
}

void
dhd_os_sdunlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_bh(&dhd->txqlock);
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

#if defined(WL_WIRELESS_EXT)
	if (event->bsscfgidx == 0) {
		/*
		 * Wireless ext is on primary interface only
		 */
		ASSERT(dhd->iflist[ifidx] != NULL);
		ASSERT(dhd->iflist[ifidx]->net != NULL);

		if (dhd->iflist[ifidx]->net) {
			wl_iw_event(dhd->iflist[ifidx]->net, event, *data);
		}
	}
#endif /* defined(WL_WIRELESS_EXT)  */

#ifdef WL_CFG80211
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
		DHD_ERROR(("%s: unable to alloc sk_buf", __FUNCTION__));
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
#endif // endif
	return;
}

#if defined(BCMSDIO) || defined(BCMPCIE)
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
#endif // endif
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
			dhd->fw_path, dhd->nv_path);
	}
#endif /* BCMSDIO */

	ret = dhd_bus_devreset(&dhd->pub, flag);

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	pm_runtime_mark_last_busy(dhd_bus_to_dev(dhd->pub.bus));
	pm_runtime_put_autosuspend(dhd_bus_to_dev(dhd->pub.bus));
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	if (flag) {
		/* Clear some flags for recovery logic */
		dhd->pub.dongle_trap_occured = 0;
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
	}

	if (ret) {
		DHD_ERROR(("%s: dhd_bus_devreset: %d\n", __FUNCTION__, ret));
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
#endif /* BCMSDIO || BCMPCIE */

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
#endif // endif
#ifdef WL_CFG80211
		wl_cfg80211_update_power_mode(dev);
#endif // endif
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
		return -1;
	}

	return 0;
}
#endif /* DISABLE_DTIM_IN_SUSPEND */

#ifdef PKT_FILTER_SUPPORT
int net_os_rxfilter_add_remove(struct net_device *dev, int add_remove, int num)
{
	int ret = 0;

#ifndef GAN_LITE_NAT_KEEPALIVE_FILTER
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

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

	if (FW_SUPPORTED(dhd, sta)) {
#if defined(OEM_ANDROID)
		feature_set |= WIFI_FEATURE_SET_LATENCY_MODE;
		feature_set |= WIFI_FEATURE_SET_TX_POWER_LIMIT;
#endif /*   OEM_ANDROID  */
		feature_set |= WIFI_FEATURE_INFRA;
	}
	if (FW_SUPPORTED(dhd, dualband))
		feature_set |= WIFI_FEATURE_INFRA_5G;
	if (FW_SUPPORTED(dhd, p2p)) {
		feature_set |= WIFI_FEATURE_P2P;
#if defined(OEM_ANDROID)
		feature_set |= WIFI_FEATURE_P2P_RAND_MAC;
#endif /* OEM_ANDROID */
	}
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
	feature_set |= WIFI_FEATURE_D2D_RTT;
	feature_set |= WIFI_FEATURE_D2AP_RTT;
#endif /* RTT_SUPPORT */
#ifdef LINKSTAT_SUPPORT
	feature_set |= WIFI_FEATURE_LINKSTAT;
#endif /* LINKSTAT_SUPPORT */

#if defined(PNO_SUPPORT) && !defined(DISABLE_ANDROID_PNO)
	if (dhd_is_pno_supported(dhd)) {
		feature_set |= WIFI_FEATURE_PNO;
#ifdef GSCAN_SUPPORT
		feature_set |= WIFI_FEATURE_GSCAN;
		feature_set |= WIFI_FEATURE_HAL_EPNO;
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
#ifdef NDO_CONFIG_SUPPORT
	if (FW_SUPPORTED(dhd, ndoe))
		feature_set |= WIFI_FEATURE_CONFIG_NDO;
#endif /* NDO_CONFIG_SUPPORT */
#ifdef KEEP_ALIVE
	feature_set |= WIFI_FEATURE_MKEEP_ALIVE;
#endif /* KEEP_ALIVE */
#ifdef SUPPORT_RANDOM_MAC_SCAN
	feature_set |= WIFI_FEATURE_SCAN_RAND;
#endif /* SUPPORT_RANDOM_MAC_SCAN */
#ifdef FILTER_IE
	if (FW_SUPPORTED(dhd, fie)) {
		feature_set |= WIFI_FEATURE_FILTER_IE;
	}
#endif /* FILTER_IE */
#ifdef ROAMEXP_SUPPORT
	/* Check if the Android O roam feature is supported by FW */
	if (!(BCME_UNSUPPORTED == dhd_dev_set_whitelist_ssid(dev, NULL, 0, true))) {
		feature_set |= WIFI_FEATURE_CONTROL_ROAMING;
	}
#endif /* ROAMEXP_SUPPORT */
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

	if (nodfs) {
		if (dhd->pub.dhd_cflags & WLAN_PLAT_NODFS_FLAG) {
			return 0;
		}
		dhd->pub.dhd_cflags |= WLAN_PLAT_NODFS_FLAG;
	} else {
		if (!(dhd->pub.dhd_cflags & WLAN_PLAT_NODFS_FLAG)) {
			return 0;
		}
		dhd->pub.dhd_cflags &= ~WLAN_PLAT_NODFS_FLAG;
	}
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

/* #pragma used as a WAR to fix build failure,
* ignore dropping of 'const' qualifier in 'list_entry' macro
* this pragma disables the warning only for the following function
*/
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif /* __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6 */
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
			ret = dhd_ndo_enable(dhdp, TRUE);
		}
	}

done:
	if (ipv6_addr) {
		MFREE(dhdp->osh, ipv6_addr, sizeof(struct in6_addr) * dhdp->ndo_max_host_ip);
	}

	return ret;
}
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic pop
#endif /* __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) */
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
		DHD_ERROR(("%s : Failed to execute roam_exp_bssid_pref %d\n", __FUNCTION__, err));
	}
	return err;
}
#endif /* GSCAN_SUPPORT || ROAMEXP_SUPPORT */

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

	if ((pbuf = MALLOCZ(dhd_pub->osh, KA_TEMP_BUF_SIZE)) == NULL) {
		DHD_ERROR(("failed to allocate buf with size %d\n", KA_TEMP_BUF_SIZE));
		res = BCME_NOMEM;
		return res;
	}

	if ((pmac_frame = MALLOCZ(dhd_pub->osh, KA_FRAME_SIZE)) == NULL) {
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
	if (pmac_frame_begin) {
		MFREE(dhd_pub->osh, pmac_frame_begin, KA_FRAME_SIZE);
		pmac_frame_begin = NULL;
	}
	if (pbuf) {
		MFREE(dhd_pub->osh, pbuf, KA_TEMP_BUF_SIZE);
		pbuf = NULL;
	}
	return res;
}

int
dhd_dev_stop_mkeep_alive(dhd_pub_t *dhd_pub, uint8 mkeep_alive_id)
{
	char			*pbuf = NULL;
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	wl_mkeep_alive_pkt_t	*mkeep_alive_pktp = NULL;
	int			res = BCME_ERROR;
	int			i = 0;

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
	if ((pbuf = MALLOC(dhd_pub->osh, KA_TEMP_BUF_SIZE)) == NULL) {
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
	if (pbuf) {
		MFREE(dhd_pub->osh, pbuf, KA_TEMP_BUF_SIZE);
		pbuf = NULL;
	}
	return res;
}
#endif /* KEEP_ALIVE */

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
/* Ignore compiler warnings due to -Werror=cast-qual */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	struct dhd_info *dhd =
		container_of(work_data, dhd_info_t, dhd_hang_process_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	dev = dhd->iflist[0]->net;

	if (dev) {
#if defined(WL_WIRELESS_EXT)
		wl_iw_send_priv_event(dev, "HANG");
#endif // endif
#if defined(WL_CFG80211)
		wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif // endif
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
			dev_close(ndev);
		}
	}
	rtnl_unlock();
#endif /* IFACE_HANG_FORCE_DEV_CLOSE */
}

#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
extern dhd_pub_t *link_recovery;
void dhd_host_recover_link(void)
{
	DHD_ERROR(("****** %s ******\n", __FUNCTION__));
	link_recovery->hang_reason = HANG_REASON_PCIE_LINK_DOWN_RC_DETECT;
	dhd_bus_set_linkdown(link_recovery, TRUE);
	dhd_os_send_hang_message(link_recovery);
}
EXPORT_SYMBOL(dhd_host_recover_link);
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */

int dhd_os_send_hang_message(dhd_pub_t *dhdp)
{
	int ret = 0;
#ifdef WL_CFG80211
	struct net_device *primary_ndev;
	struct bcm_cfg80211 *cfg;
#ifdef DHD_FILE_DUMP_EVENT
	dhd_info_t *dhd_info = NULL;
#endif /* DHD_FILE_DUMP_EVENT */
#endif /* WL_CFG80211 */

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is null\n", __FUNCTION__));
		return -EINVAL;
	}

#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT)
	dhd_info = (dhd_info_t *)dhdp->info;

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
#endif // endif
		schedule_work(&dhdp->info->dhd_hang_process_work);

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

	BCM_REFERENCE(dhd);
}
void dhd_bus_country_set(struct net_device *dev, wl_country_t *cspec, bool notify)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif // endif

	if (dhd && dhd->pub.up) {
		dhd->pub.force_country_change = FALSE;
		memcpy(&dhd->pub.dhd_cspec, cspec, sizeof(wl_country_t));
#ifdef WL_CFG80211
		wl_update_wiphybands(cfg, notify);
#endif // endif
	}
}

void dhd_bus_band_set(struct net_device *dev, uint band)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif // endif
	if (dhd && dhd->pub.up) {
#ifdef WL_CFG80211
		wl_update_wiphybands(cfg, true);
#endif // endif
	}
}

int dhd_net_set_fw_path(struct net_device *dev, char *fw)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (!fw || fw[0] == '\0')
		return -EINVAL;

	strncpy(dhd->fw_path, fw, sizeof(dhd->fw_path) - 1);
	dhd->fw_path[sizeof(dhd->fw_path)-1] = '\0';

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
#endif // endif
}

static void dhd_net_if_unlock_local(dhd_info_t *dhd)
{
#if defined(OEM_ANDROID)
	if (dhd)
		mutex_unlock(&dhd->dhd_net_if_mutex);
#endif // endif
}

static void dhd_suspend_lock(dhd_pub_t *pub)
{
#if defined(OEM_ANDROID)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_lock(&dhd->dhd_suspend_mutex);
#endif // endif
}

static void dhd_suspend_unlock(dhd_pub_t *pub)
{
#if defined(OEM_ANDROID)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_unlock(&dhd->dhd_suspend_mutex);
#endif // endif
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
dhd_os_dbgring_lock_deinit(osl_t *osh, void *mtx)
{
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
	loff_t pos = 0;
	/* change to KERNEL_DS address limit */
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t old_fs;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */
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
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
	return ret;
}
#endif // endif

#ifdef DHD_DEBUG
static void
dhd_convert_memdump_type_to_str(uint32 type, char *buf, int substr_type)
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
		case DUMP_TYPE_BY_USER:
			type_str = "BY_USER";
			break;
#ifdef DHD_ERPOM
		case DUMP_TYPE_DUE_TO_BT:
			type_str = "DUE_TO_BT";
			break;
#endif /* DHD_ERPOM */
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
		case DUMP_TYPE_PKTID_POOL_DEPLETED:
			type_str = "PKTID_POOL_DEPLETED";
			break;
		default:
			type_str = "Unknown_type";
			break;
	}

	strncpy(buf, type_str, strlen(type_str));
	buf[strlen(type_str)] = 0;
}

void
dhd_get_memdump_filename(struct net_device *ndev, char *memdump_path, int len, char *fname)
{
	char memdump_type[32];
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(ndev);
	dhd_pub_t *dhdp = &dhd->pub;

	/* Init file name */
	memset(memdump_path, 0, len);
	memset(memdump_type, 0, sizeof(memdump_type));
	dhd_convert_memdump_type_to_str(dhdp->memdump_type, memdump_type, dhdp->debug_dump_subcmd);
	clear_debug_dump_time(dhdp->debug_dump_time_str);
	get_debug_dump_time(dhdp->debug_dump_time_str);
#ifdef CUSTOMER_HW4_DEBUG
	snprintf(memdump_path, len, "%s%s_%s_" "%s",
			DHD_COMMON_DUMP_PATH, fname, memdump_type, dhdp->debug_dump_time_str);
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
	snprintf(memdump_path, len, "%s%s_%s_" "%s",
			DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhdp->debug_dump_time_str);
#elif defined(OEM_ANDROID) && (defined(BOARD_PANDA) || defined(__ARM_ARCH_7A__))
	snprintf(memdump_path, len, "%s%s_%s_" "%s",
			DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhdp->debug_dump_time_str);
#elif defined(OEM_ANDROID)
	snprintf(memdump_path, len, "%s%s_%s_" "%s",
			DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhdp->debug_dump_time_str);
#else
	snprintf(memdump_path, len, "%s%s_%s_" "%s",
			DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhdp->debug_dump_time_str);
#endif /* CUSTOMER_HW4_DEBUG */
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
	char memdump_path[128];
	char memdump_type[32];
	uint32 file_mode;

	/* Init file name */
	memset(memdump_path, 0, sizeof(memdump_path));
	memset(memdump_type, 0, sizeof(memdump_type));
	dhd_convert_memdump_type_to_str(dhd->memdump_type, memdump_type, dhd->debug_dump_subcmd);
	clear_debug_dump_time(dhd->debug_dump_time_str);
	get_debug_dump_time(dhd->debug_dump_time_str);
#ifdef CUSTOMER_HW4_DEBUG
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
		DHD_COMMON_DUMP_PATH, fname, memdump_type, dhd->debug_dump_time_str);
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
		DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhd->debug_dump_time_str);
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
#elif defined(OEM_ANDROID) && (defined(BOARD_PANDA) || defined(__ARM_ARCH_7A__))
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
		DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhd->debug_dump_time_str);
	file_mode = O_CREAT | O_WRONLY;
#elif defined(OEM_ANDROID)
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
		"/root/", fname, memdump_type,  dhd->debug_dump_time_str);
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
			DHD_ERROR(("open file %s, try /tmp/\n", memdump_path));
			snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
				"/tmp/", fname, memdump_type,  dhd->debug_dump_time_str);
		} else {
			filp_close(fp, NULL);
		}
	}
#else
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_" "%s",
		DHD_COMMON_DUMP_PATH, fname, memdump_type,  dhd->debug_dump_time_str);
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
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		ret = dhd->wakelock_rx_timeout_enable > dhd->wakelock_ctrl_timeout_enable ?
			dhd->wakelock_rx_timeout_enable : dhd->wakelock_ctrl_timeout_enable;
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
		if (dhd->wakelock_rx_timeout_enable)
			wake_lock_timeout(&dhd->wl_rxwake,
				msecs_to_jiffies(dhd->wakelock_rx_timeout_enable));
		if (dhd->wakelock_ctrl_timeout_enable)
			wake_lock_timeout(&dhd->wl_ctrlwake,
				msecs_to_jiffies(dhd->wakelock_ctrl_timeout_enable));
#endif // endif
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
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
		if (wake_lock_active(&dhd->wl_ctrlwake))
			wake_unlock(&dhd->wl_ctrlwake);
#endif // endif
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
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
			wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
			dhd_bus_dev_pm_stay_awake(pub);
#endif // endif
		}
#ifdef DHD_TRACE_WAKE_LOCK
		if (atomic_read(&trace_wklock_onoff)) {
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
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
		wake_lock(&dhd->wl_evtwake);
#elif defined(BCMSDIO)
		dhd_bus_dev_pm_stay_awake(pub);
#endif // endif
	}
}

void
dhd_pm_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_pmwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}

void
dhd_txfl_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_txflwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
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
			if (atomic_read(&trace_wklock_onoff)) {
				STORE_WKLOCK_RECORD(DHD_WAKE_UNLOCK);
			}
#endif /* DHD_TRACE_WAKE_LOCK */
			if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
				wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
				dhd_bus_dev_pm_relax(pub);
#endif // endif
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
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
		wake_unlock(&dhd->wl_evtwake);
#elif defined(BCMSDIO)
		dhd_bus_dev_pm_relax(pub);
#endif // endif
	}
}

void dhd_pm_wake_unlock(dhd_pub_t *pub)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_pmwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_pmwake)) {
			wake_unlock(&dhd->wl_pmwake);
		}
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}

void dhd_txfl_wake_unlock(dhd_pub_t *pub)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_txflwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_txflwake)) {
			wake_unlock(&dhd->wl_txflwake);
		}
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}

int dhd_os_check_wakelock(dhd_pub_t *pub)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK) || defined(BCMSDIO)
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK || BCMSDIO */

#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	/* Indicate to the SD Host to avoid going to suspend if internal locks are up */
	if (dhd && (wake_lock_active(&dhd->wl_wifi) ||
		(wake_lock_active(&dhd->wl_wdwake))))
		return 1;
#elif defined(BCMSDIO)
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub))
		return 1;
#endif // endif
	return 0;
}

int
dhd_os_check_wakelock_all(dhd_pub_t *pub)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK) || defined(BCMSDIO)
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	int l1, l2, l3, l4, l7, l8, l9;
	int l5 = 0, l6 = 0;
	int c, lock_active;
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
	dhd_info_t *dhd;

	if (!pub) {
		return 0;
	}
	dhd = (dhd_info_t *)(pub->info);
	if (!dhd) {
		return 0;
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK || BCMSDIO */

#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
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
#elif defined(BCMSDIO)
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub)) {
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
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_wd_counter == 0 && !dhd->waive_wakelock) {
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
			/* if wakelock_wd_counter was never used : lock it at once */
			wake_lock(&dhd->wl_wdwake);
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
		}
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
		if (dhd->wakelock_wd_counter > 0) {
			dhd->wakelock_wd_counter = 0;
			if (!dhd->waive_wakelock) {
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
				wake_unlock(&dhd->wl_wdwake);
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
			}
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

#ifdef BCMPCIE_OOB_HOST_WAKE
void
dhd_os_oob_irq_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_intrwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}

void
dhd_os_oob_irq_wake_unlock(dhd_pub_t *pub)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_intrwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_intrwake)) {
			wake_unlock(&dhd->wl_intrwake);
		}
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef DHD_USE_SCAN_WAKELOCK
void
dhd_os_scan_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_scanwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}

void
dhd_os_scan_wake_unlock(dhd_pub_t *pub)
{
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_scanwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_scanwake)) {
			wake_unlock(&dhd->wl_scanwake);
		}
	}
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
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
			if (atomic_read(&trace_wklock_onoff)) {
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
	if (atomic_read(&trace_wklock_onoff)) {
		STORE_WKLOCK_RECORD(DHD_RESTORE_LOCK);
	}
#endif /* DHD_TRACE_WAKE_LOCK */

	if (dhd->wakelock_before_waive == 0 && dhd->wakelock_counter > 0) {
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
		wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
		dhd_bus_dev_pm_stay_awake(&dhd->pub);
#endif // endif
	} else if (dhd->wakelock_before_waive > 0 && dhd->wakelock_counter == 0) {
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
		wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO)
		dhd_bus_dev_pm_relax(&dhd->pub);
#endif // endif
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
	/* wakelocks prevent a system from going into a low power state */
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	wake_lock_init(&dhd->wl_wifi, WAKE_LOCK_SUSPEND, "wlan_wake");
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
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
#ifdef DHD_TRACE_WAKE_LOCK
	dhd_wk_lock_trace_init(dhd);
#endif /* DHD_TRACE_WAKE_LOCK */
}

void dhd_os_wake_lock_destroy(struct dhd_info *dhd)
{
	DHD_TRACE(("%s: deinit wake_lock_counters\n", __FUNCTION__));
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
	dhd->wakelock_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
	wake_lock_destroy(&dhd->wl_wifi);
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
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */
}

bool dhd_os_check_if_up(dhd_pub_t *pub)
{
	if (!pub)
		return FALSE;
	return pub->up;
}

#if defined(BCMSDIO) || defined(BCMPCIE)
/* function to collect firmware, chip id and chip version info */
void dhd_set_version_info(dhd_pub_t *dhdp, char *fw)
{
	int i;

	i = snprintf(info_string, sizeof(info_string),
		"  Driver: %s\n  Firmware: %s ", EPI_VERSION_STR, fw);

	if (!dhdp)
		return;

	i = snprintf(&info_string[i], sizeof(info_string) - i,
		"\n  Chip: %x Rev %x Pkg %x", dhd_bus_chip_id(dhdp),
		dhd_bus_chiprev_id(dhdp), dhd_bus_chippkg_id(dhdp));
}
#endif /* BCMSDIO || BCMPCIE */
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

			/* Enable Deepsleep */
			powervar = 1;
			ret = dhd_iovar(dhdp, 0, "deepsleep", (char *)&powervar, sizeof(powervar),
					NULL, 0, TRUE);
			break;

		case 0: /* Deepsleep Off */
			DHD_ERROR(("[WiFi] Deepsleep Off\n"));

			/* Disable Deepsleep */
			for (cnt = 0; cnt < MAX_TRY_CNT; cnt++) {
				powervar = 0;
				ret = dhd_iovar(dhdp, 0, "deepsleep", (char *)&powervar,
						sizeof(powervar), NULL, 0, TRUE);

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
			ret = dhd_iovar(dhdp, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
					0, TRUE);
			break;
	}

	return 0;
}
#endif /* WL_CFG80211 && SUPPORT_DEEP_SLEEP */

#ifdef PROP_TXSTATUS

void dhd_wlfc_plat_init(void *dhd)
{
#ifdef USE_DYNAMIC_F2_BLKSIZE
	dhdsdio_func_blocksize((dhd_pub_t *)dhd, 2, sd_f2_blocksize);
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

#ifdef DHD_RND_DEBUG

#ifdef CUSTOMER_HW4_DEBUG
#define RNDINFO PLATFORM_PATH".rnd"
#elif defined(CUSTOMER_HW2) || defined(BOARD_HIKEY)
#define RNDINFO "/data/misc/wifi/.rnd"
#elif defined(OEM_ANDROID) && (defined(BOARD_PANDA) || defined(__ARM_ARCH_7A__))
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
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t old_fs;
#endif /* KERNEL_DS && USER_DS */
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

#if defined(KERNEL_DS) && defined(USER_DS)
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */

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

#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
	filp_close(fp, NULL);

	DHD_ERROR(("%s: RND read from %s\n", __FUNCTION__, filepath));
	return BCME_OK;

err3:
	MFREE(dhd->osh, dhd->rnd_buf, dhd->rnd_len);
	dhd->rnd_buf = NULL;
err2:
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
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
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t old_fs;
#endif /* KERNEL_DS && USER_DS */
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

#if defined(KERNEL_DS) && defined(USER_DS)
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */

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

#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
	filp_close(fp, NULL);
	DHD_ERROR(("%s: RND written to %s\n", __FUNCTION__, filepath));
	return BCME_OK;

err2:
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
	filp_close(fp, NULL);
err1:
	return BCME_ERROR;

}
#endif /* DHD_RND_DEBUG */

#ifdef DHD_FW_COREDUMP
void dhd_schedule_memdump(dhd_pub_t *dhdp, uint8 *buf, uint32 size)
{
	unsigned long flags = 0;
	dhd_dump_t *dump = NULL;
	dhd_info_t *dhd_info = NULL;
#if !defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	log_dump_type_t type = DLD_BUF_TYPE_ALL;
#endif /* !DHD_DUMP_FILE_WRITE_FROM_KERNEL */

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
#else /* BCMPCIE */
	dump->hscb_bufsize = 0;
#endif /* BCMPCIE */

#ifdef DHD_LOG_DUMP
	dhd_print_buf_addr(dhdp, "memdump", buf, size);
#if !defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	/* Print out buffer infomation */
	dhd_log_dump_buf_addr(dhdp, &type);
#endif /* !DHD_DUMP_FILE_WRITE_FROM_KERNEL */
#endif /* DHD_LOG_DUMP */

	if (dhdp->memdump_enabled == DUMP_MEMONLY && (!disable_bug_on)) {
		BUG_ON(1);
	}

#if defined(DEBUG_DNGL_INIT_FAIL) || defined(DHD_ERPOM) || \
	defined(DNGL_AXI_ERROR_LOGGING)
	if (
#if defined(DEBUG_DNGL_INIT_FAIL)
		(dhdp->memdump_type == DUMP_TYPE_DONGLE_INIT_FAILURE) ||
#endif /* DEBUG_DNGL_INIT_FAIL */
#ifdef DHD_ERPOM
		(dhdp->memdump_type == DUMP_TYPE_DUE_TO_BT) ||
#endif /* DHD_ERPOM */
#ifdef DNGL_AXI_ERROR_LOGGING
		(dhdp->memdump_type == DUMP_TYPE_SMMU_FAULT) ||
#endif /* DNGL_AXI_ERROR_LOGGING */
		FALSE)
	{
#if defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL) && defined(DHD_LOG_DUMP)
		log_dump_type_t *flush_type = NULL;
#endif /* DHD_DUMP_FILE_WRITE_FROM_KERNEL && DHD_LOG_DUMP */
		dhd_info->scheduled_memdump = FALSE;
		dhd_mem_dump((void *)dhdp->info, (void *)dump, 0);
#if defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL) && defined(DHD_LOG_DUMP)
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
#endif /* DHD_DUMP_FILE_WRITE_FROM_KERNEL && DHD_LOG_DUMP */
		return;
	}
#endif /* DEBUG_DNGL_INIT_FAIL || DHD_ERPOM || DNGL_AXI_ERROR_LOGGING */

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
	int ret = 0;
	dhd_dump_t *dump = NULL;

	DHD_ERROR(("%s: ENTER, memdump type %u\n", __FUNCTION__, dhd->pub.memdump_type));

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
		ret = -ENODEV;
		goto exit;
	}
	DHD_GENERAL_UNLOCK(dhdp, flags);

#ifdef DHD_SSSR_DUMP
	if (dhdp->sssr_inited && dhdp->collect_sssr) {
		dhdpcie_sssr_dump(dhdp);
	}
	dhdp->collect_sssr = FALSE;
#endif /* DHD_SSSR_DUMP */
#if defined(WL_CFG80211) && defined(DHD_FILE_DUMP_EVENT)
	dhd_wait_for_file_dump(dhdp);
#endif /* WL_CFG80211 && DHD_FILE_DUMP_EVENT */

	dump = (dhd_dump_t *)event_info;
	if (!dump) {
		DHD_ERROR(("%s: dump is NULL\n", __FUNCTION__));
		ret = -EINVAL;
		goto exit;
	}

	/*
	 * If kernel does not have file write access enabled
	 * then skip writing dumps to files.
	 * The dumps will be pushed to HAL layer which will
	 * write into files
	 */
#ifdef DHD_DUMP_FILE_WRITE_FROM_KERNEL

	if (write_dump_to_file(&dhd->pub, dump->buf, dump->bufsize, "mem_dump")) {
		DHD_ERROR(("%s: writing SoC_RAM dump to the file failed\n", __FUNCTION__));
#ifdef DHD_DEBUG_UART
		dhd->pub.memdump_success = FALSE;
#endif	/* DHD_DEBUG_UART */
	}

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

#ifdef DHD_PKT_LOGGING
	copy_debug_dump_time(dhdp->debug_dump_time_pktlog_str, dhdp->debug_dump_time_str);
#endif /* DHD_PKT_LOGGING */
	clear_debug_dump_time(dhdp->debug_dump_time_str);

	/* before calling bug on, wait for other logs to be dumped.
	* we cannot wait in case dhd_mem_dump is called directly
	* as it may not be in a sleepable context
	*/
	if (dhd->scheduled_memdump) {
		uint bitmask = 0;
		int timeleft = 0;
#ifdef DHD_SSSR_DUMP
		bitmask |= DHD_BUS_BUSY_IN_SSSRDUMP;
#endif // endif
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

	if (dump->hscb_buf && dump->hscb_bufsize) {
		DHD_ERROR(("%s: write HSCB dump... \n", __FUNCTION__));
		if (write_dump_to_file(&dhd->pub, dump->hscb_buf,
			dump->hscb_bufsize, "mem_dump_hscb")) {
			DHD_ERROR(("%s: writing HSCB dump to the file failed\n", __FUNCTION__));
#ifdef DHD_DEBUG_UART
			dhd->pub.memdump_success = FALSE;
#endif	/* DHD_DEBUG_UART */
		}
	}
#endif /* DHD_DUMP_FILE_WRITE_FROM_KERNEL */

	DHD_ERROR(("%s: memdump type %u\n", __FUNCTION__, dhd->pub.memdump_type));
	if (dhd->pub.memdump_enabled == DUMP_MEMFILE_BUGON &&
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

		DHD_ERROR(("%s: call BUG_ON \n", __FUNCTION__));
		if (!disable_bug_on) {
			BUG_ON(1);
		}
	}
	DHD_ERROR(("%s: No BUG ON, memdump type %u \n", __FUNCTION__, dhd->pub.memdump_type));

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
	DHD_ERROR(("%s: EXIT %d\n", __FUNCTION__, ret));
	return;
}
#endif /* DHD_FW_COREDUMP */

#ifdef DHD_SSSR_DUMP
int
dhd_sssr_dump_dig_buf_before(void *dev, const void *user_buf, uint32 len)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
	dhd_pub_t *dhdp = &dhd_info->pub;
	int pos = 0, ret = BCME_ERROR;
	uint dig_buf_size = 0;

	if (dhdp->sssr_reg_info.vasip_regs.vasip_sr_size) {
		dig_buf_size = dhdp->sssr_reg_info.vasip_regs.vasip_sr_size;
	} else if ((dhdp->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
		dhdp->sssr_reg_info.dig_mem_info.dig_sr_size) {
		dig_buf_size = dhdp->sssr_reg_info.dig_mem_info.dig_sr_size;
	}

	if (dhdp->sssr_dig_buf_before && (dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		ret = dhd_export_debug_data((char *)dhdp->sssr_dig_buf_before,
			NULL, user_buf, dig_buf_size, &pos);
	}
	return ret;
}

int
dhd_sssr_dump_dig_buf_after(void *dev, const void *user_buf, uint32 len)
{
	dhd_info_t *dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
	dhd_pub_t *dhdp = &dhd_info->pub;
	int pos = 0, ret = BCME_ERROR;
	uint dig_buf_size = 0;

	if (dhdp->sssr_reg_info.vasip_regs.vasip_sr_size) {
		dig_buf_size = dhdp->sssr_reg_info.vasip_regs.vasip_sr_size;
	} else if ((dhdp->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
		dhdp->sssr_reg_info.dig_mem_info.dig_sr_size) {
		dig_buf_size = dhdp->sssr_reg_info.dig_mem_info.dig_sr_size;
	}

	if (dhdp->sssr_dig_buf_after) {
		ret = dhd_export_debug_data((char *)dhdp->sssr_dig_buf_after,
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

static void
dhd_sssr_dump_to_file(dhd_info_t* dhdinfo)
{
	dhd_info_t *dhd = dhdinfo;
	dhd_pub_t *dhdp;
	int i;
	char before_sr_dump[128];
	char after_sr_dump[128];
	unsigned long flags = 0;
	uint dig_buf_size = 0;

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

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		/* Init file name */
		memset(before_sr_dump, 0, sizeof(before_sr_dump));
		memset(after_sr_dump, 0, sizeof(after_sr_dump));

		snprintf(before_sr_dump, sizeof(before_sr_dump), "%s_%d_%s",
			"sssr_dump_core", i, "before_SR");
		snprintf(after_sr_dump, sizeof(after_sr_dump), "%s_%d_%s",
			"sssr_dump_core", i, "after_SR");

		if (dhdp->sssr_d11_before[i] && dhdp->sssr_d11_outofreset[i] &&
			(dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
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

	if (dhdp->sssr_reg_info.vasip_regs.vasip_sr_size) {
		dig_buf_size = dhdp->sssr_reg_info.vasip_regs.vasip_sr_size;
	} else if ((dhdp->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
		dhdp->sssr_reg_info.dig_mem_info.dig_sr_size) {
		dig_buf_size = dhdp->sssr_reg_info.dig_mem_info.dig_sr_size;
	}

	if (dhdp->sssr_dig_buf_before && (dhdp->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_dig_buf_before,
			dig_buf_size, "sssr_dump_dig_before_SR")) {
			DHD_ERROR(("%s: writing SSSR Dig dump before to the file failed\n",
				__FUNCTION__));
		}
	}

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
	dhdp->sssr_dump_mode = dump_mode;

	/*
	 * If kernel does not have file write access enabled
	 * then skip writing dumps to files.
	 * The dumps will be pushed to HAL layer which will
	 * write into files
	 */
#if !defined(DHD_DUMP_FILE_WRITE_FROM_KERNEL)
	return;
#endif /* !DHD_DUMP_FILE_WRITE_FROM_KERNEL */

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

}
#endif /* DHD_SSSR_DUMP */

#ifdef DHD_LOG_DUMP
static void
dhd_log_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	log_dump_type_t *type = (log_dump_type_t *)event_info;

	if (!dhd || !type) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

#ifdef WL_CFG80211
	/* flush the fw side logs */
	wl_flush_fw_log_buffer(dhd_linux_get_primary_netdev(&dhd->pub),
		FW_LOGSET_MASK_ALL);
#endif // endif
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
	if ((dhdp->memdump_enabled == DUMP_MEMONLY) ||
		(dhdp->memdump_enabled == DUMP_MEMFILE_BUGON) ||
		(dhdp->memdump_type == DUMP_TYPE_SMMU_FAULT)) {
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

#ifdef EWP_ECNTRS_LOGGING
	/* periodic flushing of ecounters is NOT supported */
	if (*type == DLD_BUF_TYPE_ALL &&
			logdump_ecntr_enable &&
			dhdp->ecntr_dbg_ring) {

		ring = (dhd_dbg_ring_t *)dhdp->ecntr_dbg_ring;
		dhd_print_buf_addr(dhdp, "ecntr_dbg_ring", ring, LOG_DUMP_ECNTRS_MAX_BUFSIZE);
		dhd_print_buf_addr(dhdp, "ecntr_dbg_ring ring_buf", ring->ring_buf,
				LOG_DUMP_ECNTRS_MAX_BUFSIZE);
	}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef DHD_STATUS_LOGGING
	if (dhdp->statlog) {
		dhd_print_buf_addr(dhdp, "statlog_logbuf", dhd_statlog_get_logbuf(dhdp),
			dhd_statlog_get_logbuf_len(dhdp));
	}
#endif /* DHD_STATUS_LOGGING */

#ifdef EWP_RTT_LOGGING
	/* periodic flushing of ecounters is NOT supported */
	if (*type == DLD_BUF_TYPE_ALL &&
			logdump_rtt_enable &&
			dhdp->rtt_dbg_ring) {

		ring = (dhd_dbg_ring_t *)dhdp->rtt_dbg_ring;
		dhd_print_buf_addr(dhdp, "rtt_dbg_ring", ring, LOG_DUMP_RTT_MAX_BUFSIZE);
		dhd_print_buf_addr(dhdp, "rtt_dbg_ring ring_buf", ring->ring_buf,
				LOG_DUMP_RTT_MAX_BUFSIZE);
	}
#endif /* EWP_RTT_LOGGING */

#ifdef BCMPCIE
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
	spin_lock_irqsave(&dld_buf->lock, flags);
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
	spin_unlock_irqrestore(&dld_buf->lock, flags);

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

	DHD_ERROR(("%s\n", __FUNCTION__));

	/* core 0 */
	i = 0;
	if (dhd->sssr_d11_before[i] && dhd->sssr_d11_outofreset[i] &&
		(dhd->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		arr_len[SSSR_C0_D11_BEFORE]  = (dhd->sssr_reg_info.mac_regs[i].sr_size);
		DHD_ERROR(("%s: arr_len[SSSR_C0_D11_BEFORE] : %d\n", __FUNCTION__,
			arr_len[SSSR_C0_D11_BEFORE]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C0_D11_BEFORE",
			dhd->sssr_d11_before[i], arr_len[SSSR_C0_D11_BEFORE]);
#endif /* DHD_LOG_DUMP */
	}
	if (dhd->sssr_d11_after[i] && dhd->sssr_d11_outofreset[i]) {
		arr_len[SSSR_C0_D11_AFTER]  = (dhd->sssr_reg_info.mac_regs[i].sr_size);
		DHD_ERROR(("%s: arr_len[SSSR_C0_D11_AFTER] : %d\n", __FUNCTION__,
			arr_len[SSSR_C0_D11_AFTER]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C0_D11_AFTER",
			dhd->sssr_d11_after[i], arr_len[SSSR_C0_D11_AFTER]);
#endif /* DHD_LOG_DUMP */
	}

	/* core 1 */
	i = 1;
	if (dhd->sssr_d11_before[i] && dhd->sssr_d11_outofreset[i] &&
		(dhd->sssr_dump_mode == SSSR_DUMP_MODE_SSSR)) {
		arr_len[SSSR_C1_D11_BEFORE]  = (dhd->sssr_reg_info.mac_regs[i].sr_size);
		DHD_ERROR(("%s: arr_len[SSSR_C1_D11_BEFORE] : %d\n", __FUNCTION__,
			arr_len[SSSR_C1_D11_BEFORE]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C1_D11_BEFORE",
			dhd->sssr_d11_before[i], arr_len[SSSR_C1_D11_BEFORE]);
#endif /* DHD_LOG_DUMP */
	}
	if (dhd->sssr_d11_after[i] && dhd->sssr_d11_outofreset[i]) {
		arr_len[SSSR_C1_D11_AFTER]  = (dhd->sssr_reg_info.mac_regs[i].sr_size);
		DHD_ERROR(("%s: arr_len[SSSR_C1_D11_AFTER] : %d\n", __FUNCTION__,
			arr_len[SSSR_C1_D11_AFTER]));
#ifdef DHD_LOG_DUMP
		dhd_print_buf_addr(dhd, "SSSR_C1_D11_AFTER",
			dhd->sssr_d11_after[i], arr_len[SSSR_C1_D11_AFTER]);
#endif /* DHD_LOG_DUMP */
	}

	if (dhd->sssr_reg_info.vasip_regs.vasip_sr_size) {
		arr_len[SSSR_DIG_BEFORE] = (dhd->sssr_reg_info.vasip_regs.vasip_sr_size);
		arr_len[SSSR_DIG_AFTER] = (dhd->sssr_reg_info.vasip_regs.vasip_sr_size);
		DHD_ERROR(("%s: arr_len[SSSR_DIG_BEFORE] : %d\n", __FUNCTION__,
			arr_len[SSSR_DIG_BEFORE]));
		DHD_ERROR(("%s: arr_len[SSSR_DIG_AFTER] : %d\n", __FUNCTION__,
			arr_len[SSSR_DIG_AFTER]));
#ifdef DHD_LOG_DUMP
		if (dhd->sssr_dig_buf_before) {
			dhd_print_buf_addr(dhd, "SSSR_DIG_BEFORE",
				dhd->sssr_dig_buf_before, arr_len[SSSR_DIG_BEFORE]);
		}
		if (dhd->sssr_dig_buf_after) {
			dhd_print_buf_addr(dhd, "SSSR_DIG_AFTER",
				dhd->sssr_dig_buf_after, arr_len[SSSR_DIG_AFTER]);
		}
#endif /* DHD_LOG_DUMP */
	} else if ((dhd->sssr_reg_info.length > OFFSETOF(sssr_reg_info_v1_t, dig_mem_info)) &&
		dhd->sssr_reg_info.dig_mem_info.dig_sr_addr) {
		arr_len[SSSR_DIG_BEFORE] = (dhd->sssr_reg_info.dig_mem_info.dig_sr_size);
		arr_len[SSSR_DIG_AFTER] = (dhd->sssr_reg_info.dig_mem_info.dig_sr_size);
		DHD_ERROR(("%s: arr_len[SSSR_DIG_BEFORE] : %d\n", __FUNCTION__,
			arr_len[SSSR_DIG_BEFORE]));
		DHD_ERROR(("%s: arr_len[SSSR_DIG_AFTER] : %d\n", __FUNCTION__,
			arr_len[SSSR_DIG_AFTER]));
#ifdef DHD_LOG_DUMP
		if (dhd->sssr_dig_buf_before) {
			dhd_print_buf_addr(dhd, "SSSR_DIG_BEFORE",
				dhd->sssr_dig_buf_before, arr_len[SSSR_DIG_BEFORE]);
		}
		if (dhd->sssr_dig_buf_after) {
			dhd_print_buf_addr(dhd, "SSSR_DIG_AFTER",
				dhd->sssr_dig_buf_after, arr_len[SSSR_DIG_AFTER]);
		}
#endif /* DHD_LOG_DUMP */
	}
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

#ifdef BCMPCIE
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
	int length = 0;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;
	uint32 remain_len = 0;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->concise_dbg_buf) {
		remain_len = dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
		 if (remain_len <= 0) {
			DHD_ERROR(("%s: error getting concise debug info !\n",
					__FUNCTION__));
			return length;
		}
		length = (strlen(DHD_DUMP_LOG_HDR) + sizeof(sec_hdr) +
			(CONCISE_DUMP_BUFLEN - remain_len));
	}
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
	int length = 0;
	log_dump_section_hdr_t sec_hdr;
	dhd_info_t *dhd_info;
	uint16 h2d_flowrings_total;
	uint32 remain_len = 0;

	if (ndev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)ndev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return length;

	if (dhdp->concise_dbg_buf) {
		remain_len = dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
		if (remain_len <= 0) {
			DHD_ERROR(("%s: error getting concise debug info !\n",
				__FUNCTION__));
		   return length;
		}
	}

	length += strlen(FLOWRING_DUMP_HDR);
	length += CONCISE_DUMP_BUFLEN - remain_len;
	length += sizeof(sec_hdr);
	h2d_flowrings_total = dhd_get_max_flow_rings(dhdp);
	length += ((H2DRING_TXPOST_ITEMSIZE
				* H2DRING_TXPOST_MAX_ITEM * h2d_flowrings_total)
				+ (D2HRING_TXCMPLT_ITEMSIZE * D2HRING_TXCMPLT_MAX_ITEM)
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
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	memset(dump_path, 0, size);

	switch (dhdp->debug_dump_subcmd) {
	case CMD_UNWANTED:
		snprintf(dump_path, size, "%s",
			DHD_COMMON_DUMP_PATH DHD_DEBUG_DUMP_TYPE
			DHD_DUMP_SUBSTR_UNWANTED);
		break;
	case CMD_DISCONNECTED:
		snprintf(dump_path, size, "%s",
			DHD_COMMON_DUMP_PATH DHD_DEBUG_DUMP_TYPE
			DHD_DUMP_SUBSTR_DISCONNECTED);
		break;
	default:
		snprintf(dump_path, size, "%s",
			DHD_COMMON_DUMP_PATH DHD_DEBUG_DUMP_TYPE);
	}

	if (!dhdp->logdump_periodic_flush) {
		get_debug_dump_time(dhdp->debug_dump_time_str);
		snprintf(dump_path + strlen(dump_path),
			size - strlen(dump_path),
			"_%s", dhdp->debug_dump_time_str);
	}
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
		spin_lock_irqsave(&dld_buf->lock, flags);
		wr_size = (unsigned long)dld_buf->present -
				(unsigned long)dld_buf->front;
		spin_unlock_irqrestore(&dld_buf->lock, flags);
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

#ifdef BCMPCIE
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
	uint32 remain_len = 0;
	dhd_info_t *dhd_info;

	if (dev) {
		dhd_info = *(dhd_info_t **)netdev_priv((struct net_device *)dev);
		dhdp = &dhd_info->pub;
	}

	if (!dhdp)
		return BCME_ERROR;

	dhd_init_sec_hdr(&sec_hdr);

	remain_len = dhd_dump(dhdp, (char *)dhdp->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
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
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t old_fs;
#endif /* KERNEL_DS && USER_DS */
	loff_t pos = 0;
	char dump_path[128];
	uint32 file_mode;
	unsigned long flags = 0;
	size_t log_size = 0;
	size_t fspace_remain = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	int isize = 0;
#else
	struct kstat stat;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0) */
	char time_str[128];
	unsigned int len = 0;
	log_dump_section_hdr_t sec_hdr;

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
#if defined(KERNEL_DS) && defined(USER_DS)
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */
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
		snprintf(dump_path, sizeof(dump_path), "/root/" DHD_DEBUG_DUMP_TYPE);
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	isize = i_size_read(file_inode(fp));

	/* if some one else has changed the file */
	if (dhdp->last_file_posn != 0 &&
			isize < dhdp->last_file_posn) {
		dhdp->last_file_posn = 0;
	}
#else
	ret = vfs_stat(dump_path, &stat);
	if (ret < 0) {
		DHD_ERROR(("file stat error, err = %d\n", ret));
		goto exit2;
	}

	/* if some one else has changed the file */
	if (dhdp->last_file_posn != 0 &&
			stat.size < dhdp->last_file_posn) {
		dhdp->last_file_posn = 0;
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0) */
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
				spin_lock_irqsave(&g_dld_buf[i].lock, flags);
				log_size += (unsigned long)g_dld_buf[i].present -
						(unsigned long)g_dld_buf[i].front;
				spin_unlock_irqrestore(&g_dld_buf[i].lock, flags);
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
	/* periodic flushing of ecounters is NOT supported */
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

#ifdef EWP_RTT_LOGGING
	/* periodic flushing of ecounters is NOT supported */
	if (*type == DLD_BUF_TYPE_ALL &&
			logdump_rtt_enable &&
			dhdp->rtt_dbg_ring) {
		dhd_log_dump_ring_to_file(dhdp, dhdp->rtt_dbg_ring,
				fp, (unsigned long *)&pos,
				&sec_hdr, RTT_LOG_HDR, LOG_DUMP_SECTION_RTT);
	}
#endif /* EWP_RTT_LOGGING */

#ifdef BCMPCIE
	len = dhd_get_ext_trap_len(NULL, dhdp);
	if (len) {
		if (dhd_print_ext_trap_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}
#endif /* BCMPCIE */

#if defined(DHD_FW_COREDUMP) && defined(DNGL_EVENT_SUPPORT) && defined(BCMPCIE)
	len = dhd_get_health_chk_len(NULL, dhdp);
	if (len) {
		if (dhd_print_ext_trap_data(NULL, dhdp, 0, fp, len, &pos) < 0)
			goto exit2;
	}
#endif /* DHD_FW_COREDUMP && DNGL_EVENT_SUPPORT && BCMPCIE */

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
#endif // endif

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
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
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
dhd_export_debug_data(void *mem_buf, void *fp, const void *user_buf, int buf_len, void *pos)
{
	int ret = BCME_OK;

	if (fp) {
		ret = vfs_write(fp, mem_buf, buf_len, (loff_t *)pos);
		if (ret < 0) {
			DHD_ERROR(("write file error, err = %d\n", ret));
			goto exit;
		}
	} else {
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
#endif // endif

#if defined(ARGOS_NOTIFY_CB)

static int argos_status_notifier_wifi_cb(struct notifier_block *notifier,
	unsigned long speed, void *v);
static int argos_status_notifier_p2p_cb(struct notifier_block *notifier,
	unsigned long speed, void *v);

int
argos_register_notifier_init(struct net_device *net)
{
	int ret = 0;

	DHD_INFO(("DHD: %s: \n", __FUNCTION__));
	argos_rps_ctrl_data.wlan_primary_netdev = net;
	argos_rps_ctrl_data.argos_rps_cpus_enabled = 0;

	if (argos_wifi.notifier_call == NULL) {
		argos_wifi.notifier_call = argos_status_notifier_wifi_cb;
		ret = sec_argos_register_notifier(&argos_wifi, ARGOS_WIFI_TABLE_LABEL);
		if (ret < 0) {
			DHD_ERROR(("DHD:Failed to register WIFI notifier, ret=%d\n", ret));
			goto exit;
		}
	}

	if (argos_p2p.notifier_call == NULL) {
		argos_p2p.notifier_call = argos_status_notifier_p2p_cb;
		ret = sec_argos_register_notifier(&argos_p2p, ARGOS_P2P_TABLE_LABEL);
		if (ret < 0) {
			DHD_ERROR(("DHD:Failed to register P2P notifier, ret=%d\n", ret));
			sec_argos_unregister_notifier(&argos_wifi, ARGOS_WIFI_TABLE_LABEL);
			goto exit;
		}
	}

	return 0;

exit:
	if (argos_wifi.notifier_call) {
		argos_wifi.notifier_call = NULL;
	}

	if (argos_p2p.notifier_call) {
		argos_p2p.notifier_call = NULL;
	}

	return ret;
}

int
argos_register_notifier_deinit(void)
{
	DHD_INFO(("DHD: %s: \n", __FUNCTION__));

	if (argos_rps_ctrl_data.wlan_primary_netdev == NULL) {
		DHD_ERROR(("DHD: primary_net_dev is null %s: \n", __FUNCTION__));
		return -1;
	}
#ifndef DHD_LB
	custom_rps_map_clear(argos_rps_ctrl_data.wlan_primary_netdev->_rx);
#endif /* !DHD_LB */

	if (argos_p2p.notifier_call) {
		sec_argos_unregister_notifier(&argos_p2p, ARGOS_P2P_TABLE_LABEL);
		argos_p2p.notifier_call = NULL;
	}

	if (argos_wifi.notifier_call) {
		sec_argos_unregister_notifier(&argos_wifi, ARGOS_WIFI_TABLE_LABEL);
		argos_wifi.notifier_call = NULL;
	}

	argos_rps_ctrl_data.wlan_primary_netdev = NULL;
	argos_rps_ctrl_data.argos_rps_cpus_enabled = 0;

	return 0;
}

int
argos_status_notifier_wifi_cb(struct notifier_block *notifier,
	unsigned long speed, void *v)
{
	dhd_info_t *dhd;
	dhd_pub_t *dhdp;
#if defined(ARGOS_NOTIFY_CB)
	unsigned int  pcie_irq = 0;
#endif /* ARGOS_NOTIFY_CB */
	DHD_INFO(("DHD: %s: speed=%ld\n", __FUNCTION__, speed));

	if (argos_rps_ctrl_data.wlan_primary_netdev == NULL) {
		goto exit;
	}

	dhd = DHD_DEV_INFO(argos_rps_ctrl_data.wlan_primary_netdev);
	if (dhd == NULL) {
		goto exit;
	}

	dhdp = &dhd->pub;
	if (dhdp == NULL || !dhdp->up) {
		goto exit;
	}
	/* Check if reported TPut value is more than threshold value */
	if (speed > RPS_TPUT_THRESHOLD) {
		if (argos_rps_ctrl_data.argos_rps_cpus_enabled == 0) {
			/* It does not need to configre rps_cpus
			 * if Load Balance is enabled
			 */
#ifndef DHD_LB
			int err = 0;

			if (cpu_online(RPS_CPUS_WLAN_CORE_ID)) {
				err = custom_rps_map_set(
					argos_rps_ctrl_data.wlan_primary_netdev->_rx,
					RPS_CPUS_MASK, strlen(RPS_CPUS_MASK));
			} else {
				DHD_ERROR(("DHD: %s: RPS_Set fail,"
					" Core=%d Offline\n", __FUNCTION__,
					RPS_CPUS_WLAN_CORE_ID));
				err = -1;
			}

			if (err < 0) {
				DHD_ERROR(("DHD: %s: Failed to RPS_CPUs. "
					"speed=%ld, error=%d\n",
					__FUNCTION__, speed, err));
			} else {
#endif /* !DHD_LB */
#if (defined(DHDTCPACK_SUPPRESS) && defined(BCMPCIE))
				if (dhdp->tcpack_sup_mode != TCPACK_SUP_HOLD) {
					DHD_ERROR(("%s : set ack suppress. TCPACK_SUP_ON(%d)\n",
						__FUNCTION__, TCPACK_SUP_HOLD));
					dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_HOLD);
				}
#endif /* DHDTCPACK_SUPPRESS && BCMPCIE */
				argos_rps_ctrl_data.argos_rps_cpus_enabled = 1;
#ifndef DHD_LB
				DHD_ERROR(("DHD: %s: Set RPS_CPUs, speed=%ld\n",
					__FUNCTION__, speed));
			}
#endif /* !DHD_LB */
		}
	} else {
		if (argos_rps_ctrl_data.argos_rps_cpus_enabled == 1) {
#if (defined(DHDTCPACK_SUPPRESS) && defined(BCMPCIE))
			if (dhdp->tcpack_sup_mode != TCPACK_SUP_OFF) {
				DHD_ERROR(("%s : set ack suppress. TCPACK_SUP_OFF\n",
					__FUNCTION__));
				dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
			}
#endif /* DHDTCPACK_SUPPRESS && BCMPCIE */
#ifndef DHD_LB
			/* It does not need to configre rps_cpus
			 * if Load Balance is enabled
			 */
			custom_rps_map_clear(argos_rps_ctrl_data.wlan_primary_netdev->_rx);
			DHD_ERROR(("DHD: %s: Clear RPS_CPUs, speed=%ld\n", __FUNCTION__, speed));
			OSL_SLEEP(DELAY_TO_CLEAR_RPS_CPUS);
#endif /* !DHD_LB */
			argos_rps_ctrl_data.argos_rps_cpus_enabled = 0;
		}
	}

exit:
	return NOTIFY_OK;
}

int
argos_status_notifier_p2p_cb(struct notifier_block *notifier,
	unsigned long speed, void *v)
{
	DHD_INFO(("DHD: %s: speed=%ld\n", __FUNCTION__, speed));
	return argos_status_notifier_wifi_cb(notifier, speed, v);
}
#endif // endif

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
#ifdef OEM_ANDROID
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

fw_download_status_t
dhd_fw_download_status(dhd_pub_t * dhd_pub)
{
	return dhd_pub->fw_download_status;
}

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
	void *cookie_buf = NULL;

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
	DHD_ERROR(("%s : Try to allocate memory total(%d) special(%d)\n",
		__FUNCTION__, LOG_DUMP_TOTAL_BUFSIZE, LOG_DUMP_SPECIAL_MAX_BUFSIZE));
	prealloc_buf = DHD_OS_PREALLOC(dhd, prealloc_idx++, LOG_DUMP_TOTAL_BUFSIZE);
	dld_buf_special->buffer = DHD_OS_PREALLOC(dhd, prealloc_idx++,
			dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
#else
	prealloc_buf = VMALLOCZ(dhd->osh, LOG_DUMP_TOTAL_BUFSIZE);
	dld_buf_special->buffer = VMALLOCZ(dhd->osh, dld_buf_size[DLD_BUF_TYPE_SPECIAL]);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
	if (!prealloc_buf) {
		DHD_ERROR(("Failed to pre-allocate memory for log buffers !\n"));
		goto fail;
	}
	if (!dld_buf_special->buffer) {
		DHD_ERROR(("Failed to pre-allocate memory for special buffer !\n"));
		goto fail;
	}

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

#ifdef EWP_ECNTRS_LOGGING
	/* now use the rest of the pre-alloc'd memory for filter and ecounter log */
	dhd->ecntr_dbg_ring = MALLOCZ(dhd->osh, sizeof(dhd_dbg_ring_t));
	if (!dhd->ecntr_dbg_ring)
		goto fail;

	ring = (dhd_dbg_ring_t *)dhd->ecntr_dbg_ring;
	ret = dhd_dbg_ring_init(dhd, ring, ECNTR_RING_ID,
			ECNTR_RING_NAME, LOG_DUMP_ECNTRS_MAX_BUFSIZE,
			bufptr, TRUE);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: unable to init ecntr ring !\n",
				__FUNCTION__));
		goto fail;
	}
	DHD_DBG_RING_LOCK(ring->lock, flags);
	ring->state = RING_ACTIVE;
	ring->threshold = 0;
	DHD_DBG_RING_UNLOCK(ring->lock, flags);

	bufptr += LOG_DUMP_ECNTRS_MAX_BUFSIZE;
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	/* now use the rest of the pre-alloc'd memory for filter and ecounter log */
	dhd->rtt_dbg_ring = MALLOCZ(dhd->osh, sizeof(dhd_dbg_ring_t));
	if (!dhd->rtt_dbg_ring)
		goto fail;

	ring = (dhd_dbg_ring_t *)dhd->rtt_dbg_ring;
	ret = dhd_dbg_ring_init(dhd, ring, RTT_RING_ID,
			RTT_RING_NAME, LOG_DUMP_RTT_MAX_BUFSIZE,
			bufptr, TRUE);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: unable to init ecntr ring !\n",
				__FUNCTION__));
		goto fail;
	}
	DHD_DBG_RING_LOCK(ring->lock, flags);
	ring->state = RING_ACTIVE;
	ring->threshold = 0;
	DHD_DBG_RING_UNLOCK(ring->lock, flags);

	bufptr += LOG_DUMP_RTT_MAX_BUFSIZE;
#endif /* EWP_RTT_LOGGING */

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
	ret = dhd_event_log_filter_init(dhd,
		bufptr,
		LOG_DUMP_FILTER_MAX_BUFSIZE);
	if (ret != BCME_OK) {
		goto fail;
	}
#endif /* DHD_EVENT_LOG_FILTER */

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
	return;

fail:

	if (dhd->logdump_cookie) {
		dhd_logdump_cookie_deinit(dhd);
		MFREE(dhd->osh, dhd->logdump_cookie, LOG_DUMP_COOKIE_BUFSIZE);
		dhd->logdump_cookie = NULL;
	}
#if defined(DHD_EVENT_LOG_FILTER)
	if (dhd->event_log_filter) {
		dhd_event_log_filter_deinit(dhd);
	}
#endif /* DHD_EVENT_LOG_FILTER */

	if (dhd->concise_dbg_buf) {
		MFREE(dhd->osh, dhd->concise_dbg_buf, CONCISE_DUMP_BUFLEN);
	}

#ifdef EWP_ECNTRS_LOGGING
	if (dhd->ecntr_dbg_ring) {
		ring = (dhd_dbg_ring_t *)dhd->ecntr_dbg_ring;
		dhd_dbg_ring_deinit(dhd, ring);
		ring->ring_buf = NULL;
		ring->ring_size = 0;
		MFREE(dhd->osh, ring, sizeof(dhd_dbg_ring_t));
		dhd->ecntr_dbg_ring = NULL;
	}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	if (dhd->rtt_dbg_ring) {
		ring = (dhd_dbg_ring_t *)dhd->rtt_dbg_ring;
		dhd_dbg_ring_deinit(dhd, ring);
		ring->ring_buf = NULL;
		ring->ring_size = 0;
		MFREE(dhd->osh, ring, sizeof(dhd_dbg_ring_t));
		dhd->rtt_dbg_ring = NULL;
	}
#endif /* EWP_RTT_LOGGING */

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
		VMFREE(dhd->osh, prealloc_buf, LOG_DUMP_TOTAL_BUFSIZE);
	}
	if (dld_buf_special->buffer) {
		VMFREE(dhd->osh, dld_buf_special->buffer,
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

	if (dhd->logdump_cookie) {
		dhd_logdump_cookie_deinit(dhd);
		MFREE(dhd->osh, dhd->logdump_cookie, LOG_DUMP_COOKIE_BUFSIZE);
		dhd->logdump_cookie = NULL;
	}

#if defined(DHD_EVENT_LOG_FILTER)
	if (dhd->event_log_filter) {
		dhd_event_log_filter_deinit(dhd);
	}
#endif /* DHD_EVENT_LOG_FILTER */

#ifdef EWP_ECNTRS_LOGGING
	if (dhd->ecntr_dbg_ring) {
		ring = (dhd_dbg_ring_t *)dhd->ecntr_dbg_ring;
		dhd_dbg_ring_deinit(dhd, ring);
		ring->ring_buf = NULL;
		ring->ring_size = 0;
		MFREE(dhd->osh, ring, sizeof(dhd_dbg_ring_t));
		dhd->ecntr_dbg_ring = NULL;
	}
#endif /* EWP_ECNTRS_LOGGING */

#ifdef EWP_RTT_LOGGING
	if (dhd->rtt_dbg_ring) {
		ring = (dhd_dbg_ring_t *)dhd->rtt_dbg_ring;
		dhd_dbg_ring_deinit(dhd, ring);
		ring->ring_buf = NULL;
		ring->ring_size = 0;
		MFREE(dhd->osh, ring, sizeof(dhd_dbg_ring_t));
		dhd->rtt_dbg_ring = NULL;
	}
#endif /* EWP_RTT_LOGGING */

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
		VMFREE(dhd->osh, dld_buf->buffer, LOG_DUMP_TOTAL_BUFSIZE);
	}
	if (dld_buf_special->buffer) {
		VMFREE(dhd->osh, dld_buf_special->buffer,
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
		DHD_INFO(("%s: Unknown DHD_LOG_DUMP_BUF_TYPE(%d).\n",
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
	spin_lock_irqsave(&dld_buf->lock, flags);
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
	spin_unlock_irqrestore(&dld_buf->lock, flags);

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

char*
dhd_log_dump_get_timestamp(void)
{
	static char buf[16];
	u64 ts_nsec;
	unsigned long rem_nsec;

	ts_nsec = local_clock();
	rem_nsec = DIV_AND_MOD_U64_BY_U32(ts_nsec, NSEC_PER_SEC);
	snprintf(buf, sizeof(buf), "%5lu.%06lu",
		(unsigned long)ts_nsec, rem_nsec / NSEC_PER_USEC);

	return buf;
}
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
#endif // endif
	{
		if (dhdp->hang_reason == HANG_REASON_PCIE_LINK_DOWN_RC_DETECT ||
			dhdp->hang_reason == HANG_REASON_PCIE_LINK_DOWN_EP_DETECT ||
#ifdef DHD_FW_COREDUMP
			dhdp->memdump_success == FALSE ||
#endif // endif
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
#endif // endif
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
		DHD_ERROR(("%s: ----- blob file exists (%s)-----\n", __FUNCTION__, filepath));
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
		DHD_ERROR(("%s: Unexpected event \n", __FUNCTION__));
		return;
	}
	if (dhd_info == NULL) {
		DHD_ERROR(("%s: Invalid dhd_info\n", __FUNCTION__));
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

	if (!dhdp) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (!dhdp->bus) {
		DHD_ERROR(("%s : dhd->bus is NULL\n", __FUNCTION__));
		return;
	}

	DHD_ERROR(("Enter %s, PCIe affinity cmd=0x%x\n", __FUNCTION__, affinity_cmd));

	if (dhdpcie_get_pcieirq(dhdp->bus, &pcie_irq)) {
		DHD_ERROR(("%s : Can't get interrupt number\n", __FUNCTION__));
		return;
	}

	/*
		irq_set_affinity() assign dedicated CPU core PCIe interrupt
		If dedicated CPU core is not on-line,
		PCIe interrupt scheduled on CPU core 0
	*/
	switch (affinity_cmd) {
		case PCIE_IRQ_AFFINITY_OFF:
			break;
		case PCIE_IRQ_AFFINITY_BIG_CORE_ANY:
#if defined(CONFIG_ARCH_SM8150)
			irq_set_affinity_hint(pcie_irq, dhdp->info->cpumask_primary);
			irq_set_affinity(pcie_irq, dhdp->info->cpumask_primary);
#else /* Exynos and Others */
			irq_set_affinity(pcie_irq, dhdp->info->cpumask_primary);
#endif /* CONFIG_ARCH_SM8150 */
			break;
#if defined(CONFIG_SOC_EXYNOS9810) || defined(CONFIG_SOC_EXYNOS9820)
		case PCIE_IRQ_AFFINITY_BIG_CORE_EXYNOS:
			DHD_ERROR(("%s, PCIe IRQ:%u set Core %d\n",
				__FUNCTION__, pcie_irq, PCIE_IRQ_CPU_CORE));
			irq_set_affinity(pcie_irq, cpumask_of(PCIE_IRQ_CPU_CORE));
			break;
#endif /* CONFIG_SOC_EXYNOS9810 || CONFIG_SOC_EXYNOS9820 */
		default:
			DHD_ERROR(("%s, Unknown PCIe affinity cmd=0x%x\n",
				__FUNCTION__, affinity_cmd));
	}
}
#endif /* SET_PCIE_IRQ_CPU_CORE */

int
dhd_write_file(const char *filepath, char *buf, int buf_len)
{
	struct file *fp = NULL;
	int ret = 0;
	/* change to KERNEL_DS address limit */
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t old_fs;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */

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
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */

	return ret;
}

int
dhd_read_file(const char *filepath, char *buf, int buf_len)
{
	struct file *fp = NULL;
	int ret;
	/* change to KERNEL_DS address limit */
#if defined(KERNEL_DS) && defined(USER_DS)
	mm_segment_t old_fs;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* KERNEL_DS && USER_DS */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
#if defined(KERNEL_DS) && defined(USER_DS)
		set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */
		DHD_ERROR(("%s: File %s doesn't exist\n", __FUNCTION__, filepath));
		return BCME_ERROR;
	}

	ret = compat_kernel_read(fp, 0, buf, buf_len);
	filp_close(fp, NULL);

	/* restore previous address limit */
#if defined(KERNEL_DS) && defined(USER_DS)
	set_fs(old_fs);
#endif /* KERNEL_DS && USER_DS */

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

#ifdef DHD_BANDSTEER
/*
 * Function return true only if there exactly two GO interfaces
 * TODO: Make it flexible to have AP + AP
 */
s32
dhd_bandsteer_get_ifaces(void *pub, void *ifaces)
{
	dhd_if_t    *iflist;  /* For supporting multiple interfaces */
	uint8 idx;
	uint8 ap_idx_count = 0;
	dhd_pub_t *dhd = (dhd_pub_t *) pub;
	dhd_bandsteer_iface_info_t *bsd_ifp = (dhd_bandsteer_iface_info_t *)ifaces;

	DHD_INFO(("%s: entered\n", __FUNCTION__));
	for (idx = 0; idx < DHD_MAX_IFS; idx++) {
		iflist =  dhd->info->iflist[idx];
		if (iflist == NULL) {
			continue;
		}

		if (iflist->net != NULL) {
			if (iflist->net->ieee80211_ptr != NULL) {
				if (
			(iflist->net->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) ||
			(iflist->net->ieee80211_ptr->iftype == NL80211_IFTYPE_AP)) {
					ap_idx_count++;
					if (ap_idx_count > 2) {
						continue;
					}
					bsd_ifp->ndev = iflist->net;
					bsd_ifp->bssidx = iflist->bssidx;
					bsd_ifp++;
				}
			}
		}
	}
	if (ap_idx_count == 2) {
		return BCME_OK;
	} else {
		return BCME_ERROR;
	}
}

void
dhd_bandsteer_schedule_work_on_timeout(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac)
{
	dhd_bandsteer_context_t *dhd_bandsteer_cntx = dhd_bandsteer_mac->dhd_bandsteer_cntx;
	dhd_pub_t *dhd = (dhd_pub_t *) dhd_bandsteer_cntx->dhd_pub;

	dhd_deferred_schedule_work(dhd->info->dhd_deferred_wq,
		(void *)dhd_bandsteer_mac, DHD_WQ_WORK_BANDSTEER_STEP_MOVE,
		dhd_bandsteer_workqueue_wrapper, DHD_WQ_WORK_PRIORITY_LOW);
}
#endif /* DHD_BANDSTEER */

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
		DHD_ERROR(("error: failed to open %s\n", FILTER_IE_PATH));
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
		buf = NULL;
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
		p_filter_iov = NULL;
	}
	return ret;
}
#endif /* FILTER_IE */
#ifdef DHD_WAKE_STATUS
wake_counts_t*
dhd_get_wakecount(dhd_pub_t *dhdp)
{
	return dhd_bus_get_wakecount(dhdp);
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
#endif // endif
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
	struct timespec64 curtime;
	unsigned long long local_time;

	struct rtc_time tm;

	if (!strlen(str)) {
		ktime_get_real_ts64(&curtime);
		local_time = (u64)(curtime.tv_sec -
				(sys_tz.tz_minuteswest * DHD_LOG_DUMP_TS_MULTIPLIER_VALUE));
		rtc_time_to_tm(local_time, &tm);

		snprintf(str, DEBUG_DUMP_TIME_BUF_LEN, DHD_LOG_DUMP_TS_FMT_YYMMDDHHMMSSMSMS,
				tm.tm_year - 100, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
				tm.tm_sec, (int)(curtime.tv_nsec/NSEC_PER_MSEC));
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

/*
 * DHD RING
 */
#define DHD_RING_ERR_INTERNAL(fmt, ...) DHD_ERROR(("EWPF-" fmt, ##__VA_ARGS__))
#define DHD_RING_TRACE_INTERNAL(fmt, ...) DHD_INFO(("EWPF-" fmt, ##__VA_ARGS__))

#define DHD_RING_ERR(x) DHD_RING_ERR_INTERNAL x
#define DHD_RING_TRACE(x) DHD_RING_TRACE_INTERNAL x

#define DHD_RING_MAGIC 0x20170910
#define DHD_RING_IDX_INVALID	0xffffffff

#define DHD_RING_SYNC_LOCK_INIT(osh)		dhd_os_spin_lock_init(osh)
#define DHD_RING_SYNC_LOCK_DEINIT(osh, lock)	dhd_os_spin_lock_deinit(osh, lock)
#define DHD_RING_SYNC_LOCK(lock, flags)		(flags) = dhd_os_spin_lock(lock)
#define DHD_RING_SYNC_UNLOCK(lock, flags)	dhd_os_spin_unlock(lock, flags)

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
	ret_ring->ring_sync = DHD_RING_SYNC_LOCK_INIT(dhdp->osh);
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
 * will return NULL if next pointer is locked
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
#ifdef DHD_DUMP_MNGR
static int
dhd_dump_file_manage_idx(dhd_dump_file_manage_t *fm_ptr, char *fname)
{
	int i;
	int fm_idx = -1;

	for (i = 0; i < DHD_DUMP_TYPE_COUNT_MAX; i++) {
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
int
compat_kernel_read(struct file *file, loff_t offset, char *addr, unsigned long count)
{
	return (int)kernel_read(file, addr, (size_t)count, &offset);
}
#else
int
compat_kernel_read(struct file *file, loff_t offset, char *addr, unsigned long count)
{
	return kernel_read(file, offset, addr, count);
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) */

#ifdef DHDTCPSYNC_FLOOD_BLK
static void dhd_blk_tsfl_handler(struct work_struct * work)
{
	dhd_if_t *ifp = NULL;
	dhd_pub_t *dhdp = NULL;
	/* Ignore compiler warnings due to -Werror=cast-qual */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif /* STRICT_GCC_WARNINGS  && __GNUC__ */
	ifp = container_of(work, dhd_if_t, blk_tsfl_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif /* STRICT_GCC_WARNINGS  && __GNUC__ */
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
	}
}
#endif /* DHDTCPSYNC_FLOOD_BLK */

#ifdef DHD_4WAYM4_FAIL_DISCONNECT
static void dhd_m4_state_handler(struct work_struct *work)
{
	dhd_if_t *ifp = NULL;
	/* Ignore compiler warnings due to -Werror=cast-qual */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	struct delayed_work *dw = to_delayed_work(work);
	ifp = container_of(dw, dhd_if_t, m4state_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

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
		dhd_prot_hdrpull(dhdp, NULL, txp, NULL, NULL);

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

#ifdef DHD_HP2P
unsigned long
dhd_os_hp2plock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;
	unsigned long flags = 0;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		spin_lock_irqsave(&dhd->hp2p_lock, flags);
	}

	return flags;
}

void
dhd_os_hp2punlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		spin_unlock_irqrestore(&dhd->hp2p_lock, flags);
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
