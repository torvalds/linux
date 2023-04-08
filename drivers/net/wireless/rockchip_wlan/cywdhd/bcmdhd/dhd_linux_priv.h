/*
 * DHD Linux header file - contains private structure definition of the Linux specific layer
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
 * $Id$
 */

#ifndef __DHD_LINUX_PRIV_H__
#define __DHD_LINUX_PRIV_H__

#include <osl.h>

#ifdef SHOW_LOGTRACE
#include <linux/syscalls.h>
#include <event_log.h>
#endif /* SHOW_LOGTRACE */
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <dhd_linux.h>
#include <dhd_bus.h>

#ifdef PCIE_FULL_DONGLE
#include <bcmmsgbuf.h>
#include <dhd_flowring.h>
#endif /* PCIE_FULL_DONGLE */

/*
 * Do not include this header except for the dhd_linux.c dhd_linux_sysfs.c
 * Local private structure (extension of pub)
 */
typedef struct dhd_info {
#if defined(WL_WIRELESS_EXT)
	wl_iw_t		iw;		/* wireless extensions state (must be first) */
#endif /* defined(WL_WIRELESS_EXT) */
	dhd_pub_t pub;
	 /* for supporting multiple interfaces.
	  * static_ifs hold the net ifaces without valid FW IF
	  */
	dhd_if_t *iflist[DHD_MAX_IFS + DHD_MAX_STATIC_IFS];

	void *adapter;			/* adapter information, interrupt, fw path etc. */
	char fw_path[PATH_MAX];		/* path to firmware image */
	char nv_path[PATH_MAX];		/* path to nvram vars file */
#ifdef DHD_UCODE_DOWNLOAD
	char uc_path[PATH_MAX];	/* path to ucode image */
#endif /* DHD_UCODE_DOWNLOAD */

	/* serialize dhd iovars */
	struct mutex dhd_iovar_mutex;

	struct semaphore proto_sem;
#ifdef PROP_TXSTATUS
	spinlock_t	wlfc_spinlock;

#endif /* PROP_TXSTATUS */
	wait_queue_head_t ioctl_resp_wait;
	wait_queue_head_t d3ack_wait;
	wait_queue_head_t dhd_bus_busy_state_wait;
	wait_queue_head_t dmaxfer_wait;
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
	spinlock_t	dhd_lock;

	struct semaphore sdsem;
	tsk_ctl_t	thr_dpc_ctl;
	tsk_ctl_t	thr_wdt_ctl;

	tsk_ctl_t	thr_rxf_ctl;
	spinlock_t	rxf_lock;
	bool		rxthread_enabled;

	/* Wakelocks */
#if defined(CONFIG_PM_WAKELOCKS) || defined(CONFIG_HAS_WAKELOCK)
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
#endif /* CONFIG_PM_WAKELOCKS || CONFIG_HAS_WAKELOCK */

#if defined(OEM_ANDROID)
	/* net_device interface lock, prevent race conditions among net_dev interface
	 * calls and wifi_on or wifi_off
	 */
	struct mutex dhd_net_if_mutex;
	struct mutex dhd_suspend_mutex;
#if defined(PKT_FILTER_SUPPORT) && defined(APF)
	struct mutex dhd_apf_mutex;
#endif /* PKT_FILTER_SUPPORT && APF */
#endif /* OEM_ANDROID */
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
#endif // endif
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
	struct timer_list join_timer;
	u32 join_timeout_val;
	bool join_timer_active;
	uint scan_time_count;
	struct timer_list scan_timer;
	bool scan_timer_active;
#endif // endif
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
	struct work_struct	  rx_compl_dispatcher_work;

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

	/* Control RXP in runtime, enable by default */
	atomic_t                lb_rxp_active;

	/*
	 * When the NET_TX tries to send a TX packet put it into tx_pend_queue
	 * For now, the processing tasklet will also direcly operate on this
	 * queue
	 */
	struct sk_buff_head	tx_pend_queue  ____cacheline_aligned;

	/* Control RXP in runtime, enable by default */
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

	/* State returned by kernel function cpuhp_setup_state(). */
	int cpuhp_setup_state;
#endif /* DHD_LB */
#if defined(DNGL_AXI_ERROR_LOGGING) && defined(DHD_USE_WQ_FOR_DNGL_AXI_ERROR)
	struct work_struct	  axi_error_dispatcher_work;
#endif /* DNGL_AXI_ERROR_LOGGING && DHD_USE_WQ_FOR_DNGL_AXI_ERROR */
#ifdef SHOW_LOGTRACE
#ifdef DHD_USE_KTHREAD_FOR_LOGTRACE
	tsk_ctl_t	  thr_logtrace_ctl;
#else
	struct delayed_work	  event_log_dispatcher_work;
#endif /* DHD_USE_KTHREAD_FOR_LOGTRACE */
#endif /* SHOW_LOGTRACE */

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */
	struct kobject dhd_kobj;
	struct kobject dhd_conf_file_kobj;
	struct timer_list timesync_timer;
#if defined(BT_OVER_SDIO)
    char btfw_path[PATH_MAX];
#endif /* defined (BT_OVER_SDIO) */
#ifdef WL_MONITOR
	struct net_device *monitor_dev; /* monitor pseudo device */
	struct sk_buff *monitor_skb;
	uint	monitor_len;
	uint	monitor_type;   /* monitor pseudo device */
#endif /* WL_MONITOR */
#if defined(BT_OVER_SDIO)
    struct mutex bus_user_lock; /* lock for sdio bus apis shared between WLAN & BT */
    int     bus_user_count; /* User counts of sdio bus shared between WLAN & BT */
#endif /* BT_OVER_SDIO */
#ifdef SHOW_LOGTRACE
	struct sk_buff_head   evt_trace_queue     ____cacheline_aligned;
#endif // endif
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	struct workqueue_struct *tx_wq;
	struct workqueue_struct *rx_wq;
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
#ifdef DHD_DEBUG_UART
	bool duart_execute;
#endif	/* DHD_DEBUG_UART */
	struct mutex logdump_lock;
	/* indicates mem_dump was scheduled as work queue or called directly */
	bool scheduled_memdump;
	struct work_struct dhd_hang_process_work;
#ifdef DHD_HP2P
	spinlock_t	hp2p_lock;
#endif /* DHD_HP2P */
} dhd_info_t;

extern int dhd_sysfs_init(dhd_info_t *dhd);
extern void dhd_sysfs_exit(dhd_info_t *dhd);
extern void dhd_dbg_ring_proc_create(dhd_pub_t *dhdp);
extern void dhd_dbg_ring_proc_destroy(dhd_pub_t *dhdp);

int __dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf);

#if defined(DHD_LB)
#if defined(DHD_LB_TXP)
int dhd_lb_sendpkt(dhd_info_t *dhd, struct net_device *net, int ifidx, void *skb);
void dhd_tx_dispatcher_work(struct work_struct * work);
void dhd_tx_dispatcher_fn(dhd_pub_t *dhdp);
void dhd_lb_tx_dispatch(dhd_pub_t *dhdp);
void dhd_lb_tx_handler(unsigned long data);
#endif /* DHD_LB_TXP */

#if defined(DHD_LB_RXP)
int dhd_napi_poll(struct napi_struct *napi, int budget);
void dhd_rx_napi_dispatcher_fn(struct work_struct * work);
void dhd_lb_rx_napi_dispatch(dhd_pub_t *dhdp);
void dhd_lb_rx_pkt_enqueue(dhd_pub_t *dhdp, void *pkt, int ifidx);
#endif /* DHD_LB_RXP */

void dhd_lb_set_default_cpus(dhd_info_t *dhd);
void dhd_cpumasks_deinit(dhd_info_t *dhd);
int dhd_cpumasks_init(dhd_info_t *dhd);

void dhd_select_cpu_candidacy(dhd_info_t *dhd);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
int dhd_cpu_startup_callback(unsigned int cpu);
int dhd_cpu_teardown_callback(unsigned int cpu);
#else
int dhd_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu);
#endif /* LINUX_VERSION_CODE < 4.10.0 */

int dhd_register_cpuhp_callback(dhd_info_t *dhd);
int dhd_unregister_cpuhp_callback(dhd_info_t *dhd);

#if defined(DHD_LB_TXC)
void dhd_lb_tx_compl_dispatch(dhd_pub_t *dhdp);
#endif /* DHD_LB_TXC */

#if defined(DHD_LB_RXC)
void dhd_lb_rx_compl_dispatch(dhd_pub_t *dhdp);
void dhd_rx_compl_dispatcher_fn(struct work_struct * work);
#endif /* DHD_LB_RXC */

#endif /* DHD_LB */

#if defined(DHD_LB_IRQSET) || defined(DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON)
void dhd_irq_set_affinity(dhd_pub_t *dhdp, const struct cpumask *cpumask);
#endif /* DHD_LB_IRQSET || DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON */

#endif /* __DHD_LINUX_PRIV_H__ */
