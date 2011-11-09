/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
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
 * $Id: dhd.h 290844 2011-10-20 08:54:39Z $
 */

/****************
 * Common types *
 */

#ifndef _dhd_h_
#define _dhd_h_

#if defined(CHROMIUMOS_COMPAT_WIRELESS)
#include <linux/sched.h>
#endif
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
#include <linux/wakelock.h>
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined (CONFIG_HAS_WAKELOCK) */
/* The kernel threading is sdio-specific */
struct task_struct;
struct sched_param;
int setScheduler(struct task_struct *p, int policy, struct sched_param *param);

#define ALL_INTERFACES	0xff

#include <wlioctl.h>


/* Forward decls */
struct dhd_bus;
struct dhd_prot;
struct dhd_info;
struct dhd_cmn;

/* The level of bus communication with the dongle */
enum dhd_bus_state {
	DHD_BUS_DOWN,		/* Not ready for frame transfers */
	DHD_BUS_LOAD,		/* Download access only (CPU reset) */
	DHD_BUS_DATA		/* Ready for frame transfers */
};

/* Firmware requested operation mode */
#define STA_MASK			0x0001
#define HOSTAPD_MASK			0x0002
#define WFD_MASK			0x0004
#define SOFTAP_FW_MASK			0x0008

/* max sequential rxcntl timeouts to set HANG event */
#define MAX_CNTL_TIMEOUT  2

enum dhd_bus_wake_state {
	WAKE_LOCK_OFF,
	WAKE_LOCK_PRIV,
	WAKE_LOCK_DPC,
	WAKE_LOCK_IOCTL,
	WAKE_LOCK_DOWNLOAD,
	WAKE_LOCK_TMOUT,
	WAKE_LOCK_WATCHDOG,
	WAKE_LOCK_LINK_DOWN_TMOUT,
	WAKE_LOCK_PNO_FIND_TMOUT,
	WAKE_LOCK_SOFTAP_SET,
	WAKE_LOCK_SOFTAP_STOP,
	WAKE_LOCK_SOFTAP_START,
	WAKE_LOCK_SOFTAP_THREAD,
	WAKE_LOCK_MAX
};

enum dhd_prealloc_index {
	DHD_PREALLOC_PROT = 0,
	DHD_PREALLOC_RXBUF,
	DHD_PREALLOC_DATABUF,
	DHD_PREALLOC_OSL_BUF
};

typedef enum  {
	DHD_IF_NONE = 0,
	DHD_IF_ADD,
	DHD_IF_DEL,
	DHD_IF_CHANGE,
	DHD_IF_DELETING
} dhd_if_state_t;


#if defined(DHD_USE_STATIC_BUF)

uint8* dhd_os_prealloc(void *osh, int section, uint size);
void dhd_os_prefree(void *osh, void *addr, uint size);
#define DHD_OS_PREALLOC(osh, section, size) dhd_os_prealloc(osh, section, size)
#define DHD_OS_PREFREE(osh, addr, size) dhd_os_prefree(osh, addr, size)

#else

#define DHD_OS_PREALLOC(osh, section, size) MALLOC(osh, size)
#define DHD_OS_PREFREE(osh, addr, size) MFREE(osh, addr, size)

#endif /* defined(DHD_USE_STATIC_BUF) */

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif

/* Common structure for module and instance linkage */
typedef struct dhd_pub {
	/* Linkage ponters */
	osl_t *osh;		/* OSL handle */
	struct dhd_bus *bus;	/* Bus module handle */
	struct dhd_prot *prot;	/* Protocol module handle */
	struct dhd_info  *info; /* Info module handle */
	struct dhd_cmn	*cmn;	/* dhd_common module handle */

	/* Internal dhd items */
	bool up;		/* Driver up/down (to OS) */
	bool txoff;		/* Transmit flow-controlled */
	bool dongle_reset;  /* TRUE = DEVRESET put dongle into reset */
	enum dhd_bus_state busstate;
	uint hdrlen;		/* Total DHD header length (proto + bus) */
	uint maxctl;		/* Max size rxctl request from proto to bus */
	uint rxsz;		/* Rx buffer size bus module should use */
	uint8 wme_dp;	/* wme discard priority */

	/* Dongle media info */
	bool iswl;		/* Dongle-resident driver is wl */
	ulong drv_version;	/* Version of dongle-resident driver */
	struct ether_addr mac;	/* MAC address obtained from dongle */
	dngl_stats_t dstats;	/* Stats for dongle-based data */

	/* Additional stats for the bus level */
	ulong tx_packets;	/* Data packets sent to dongle */
	ulong tx_multicast;	/* Multicast data packets sent to dongle */
	ulong tx_errors;	/* Errors in sending data to dongle */
	ulong tx_ctlpkts;	/* Control packets sent to dongle */
	ulong tx_ctlerrs;	/* Errors sending control frames to dongle */
	ulong rx_packets;	/* Packets sent up the network interface */
	ulong rx_multicast;	/* Multicast packets sent up the network interface */
	ulong rx_errors;	/* Errors processing rx data packets */
	ulong rx_ctlpkts;	/* Control frames processed from dongle */
	ulong rx_ctlerrs;	/* Errors in processing rx control frames */
	ulong rx_dropped;	/* Packets dropped locally (no memory) */
	ulong rx_flushed;  /* Packets flushed due to unscheduled sendup thread */
	ulong wd_dpc_sched;   /* Number of times dhd dpc scheduled by watchdog timer */

	ulong rx_readahead_cnt;	/* Number of packets where header read-ahead was used. */
	ulong tx_realloc;	/* Number of tx packets we had to realloc for headroom */
	ulong fc_packets;       /* Number of flow control pkts recvd */

	/* Last error return */
	int bcmerror;
	uint tickcnt;

	/* Last error from dongle */
	int dongle_error;

	/* Suspend disable flag and "in suspend" flag */
	int suspend_disable_flag; /* "1" to disable all extra powersaving during suspend */
	int in_suspend;			/* flag set to 1 when early suspend called */
#ifdef PNO_SUPPORT
	int pno_enable;                 /* pno status : "1" is pno enable */
#endif /* PNO_SUPPORT */
	int dtim_skip;         /* dtim skip , default 0 means wake each dtim */

	/* Pkt filter defination */
	char * pktfilter[100];
	int pktfilter_count;

	wl_country_t dhd_cspec;		/* Current Locale info */
	char eventmask[WL_EVENTING_MASK_LEN];
	int	op_mode;				/* STA, HostAPD, WFD, SoftAP */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_HAS_WAKELOCK)
	struct wake_lock 	wakelock[WAKE_LOCK_MAX];
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined (CONFIG_HAS_WAKELOCK) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	struct mutex 	wl_start_stop_lock; /* lock/unlock for Android start/stop */
	struct mutex 	wl_softap_lock;		 /* lock/unlock for any SoftAP/STA settings */
#endif 

	uint16	maxdatablks;
#ifdef PROP_TXSTATUS
	int   wlfc_enabled;
	void* wlfc_state;
#endif
	bool	dongle_isolation;
	int   hang_was_sent;
	int   rxcnt_timeout;		/* counter rxcnt timeout to send HANG */
	int   txcnt_timeout;		/* counter txcnt timeout to send HANG */
#ifdef WLMEDIA_HTSF
	uint8 htsfdlystat_sz; /* Size of delay stats, max 255B */
#endif
} dhd_pub_t;

typedef struct dhd_cmn {
	osl_t *osh;		/* OSL handle */
	dhd_pub_t *dhd;
} dhd_cmn_t;


	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)

	#define DHD_PM_RESUME_WAIT_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
	#define _DHD_PM_RESUME_WAIT(a, b) do {\
			int retry = 0; \
			SMP_RD_BARRIER_DEPENDS(); \
			while (dhd_mmc_suspend && retry++ != b) { \
				SMP_RD_BARRIER_DEPENDS(); \
				wait_event_interruptible_timeout(a, !dhd_mmc_suspend, HZ/100); \
			} \
		} while (0)
	#define DHD_PM_RESUME_WAIT(a) 		_DHD_PM_RESUME_WAIT(a, 200)
	#define DHD_PM_RESUME_WAIT_FOREVER(a) 	_DHD_PM_RESUME_WAIT(a, ~0)
	#define DHD_PM_RESUME_RETURN_ERROR(a)	do { if (dhd_mmc_suspend) return a; } while (0)
	#define DHD_PM_RESUME_RETURN		do { if (dhd_mmc_suspend) return; } while (0)

	#define DHD_SPINWAIT_SLEEP_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
	#define SPINWAIT_SLEEP(a, exp, us) do { \
		uint countdown = (us) + 9999; \
		while ((exp) && (countdown >= 10000)) { \
			wait_event_interruptible_timeout(a, FALSE, HZ/100); \
			countdown -= 10000; \
		} \
	} while (0)

	#else

	#define DHD_PM_RESUME_WAIT_INIT(a)
	#define DHD_PM_RESUME_WAIT(a)
	#define DHD_PM_RESUME_WAIT_FOREVER(a)
	#define DHD_PM_RESUME_RETURN_ERROR(a)
	#define DHD_PM_RESUME_RETURN

	#define DHD_SPINWAIT_SLEEP_INIT(a)
	#define SPINWAIT_SLEEP(a, exp, us)  do { \
		uint countdown = (us) + 9; \
		while ((exp) && (countdown >= 10)) { \
			OSL_DELAY(10);  \
			countdown -= 10;  \
		} \
	} while (0)

	#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP) */
#ifndef DHDTHREAD
#undef	SPINWAIT_SLEEP
#define SPINWAIT_SLEEP(a, exp, us) SPINWAIT(exp, us)
#endif /* DHDTHREAD */
#define DHD_IF_VIF	0x01	/* Virtual IF (Hidden from user) */

unsigned long dhd_os_spin_lock(dhd_pub_t *pub);
void dhd_os_spin_unlock(dhd_pub_t *pub, unsigned long flags);

/*  Wakelock Functions */
extern int dhd_os_wake_lock(dhd_pub_t *pub);
extern int dhd_os_wake_unlock(dhd_pub_t *pub);
extern int dhd_os_wake_lock_timeout(dhd_pub_t *pub);
extern int dhd_os_wake_lock_timeout_enable(dhd_pub_t *pub, int val);

inline static void MUTEX_LOCK_SOFTAP_SET_INIT(dhd_pub_t * dhdp)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_init(&dhdp->wl_softap_lock);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
}

inline static void MUTEX_LOCK_SOFTAP_SET(dhd_pub_t * dhdp)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_lock(&dhdp->wl_softap_lock);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
}

inline static void MUTEX_UNLOCK_SOFTAP_SET(dhd_pub_t * dhdp)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_unlock(&dhdp->wl_softap_lock);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
}

#define DHD_OS_WAKE_LOCK(pub) 			dhd_os_wake_lock(pub)
#define DHD_OS_WAKE_UNLOCK(pub) 		dhd_os_wake_unlock(pub)
#define DHD_OS_WAKE_LOCK_TIMEOUT(pub)		dhd_os_wake_lock_timeout(pub)
#define DHD_OS_WAKE_LOCK_TIMEOUT_ENABLE(pub, val)	dhd_os_wake_lock_timeout_enable(pub, val)

#define DHD_PACKET_TIMEOUT	1
#define DHD_EVENT_TIMEOUT	2

/* interface operations (register, remove) should be atomic, use this lock to prevent race
 * condition among wifi on/off and interface operation functions
 */
void dhd_net_if_lock(struct net_device *dev);
void dhd_net_if_unlock(struct net_device *dev);

typedef struct dhd_if_event {
	uint8 ifidx;
	uint8 action;
	uint8 flags;
	uint8 bssidx;
	uint8 is_AP;
} dhd_if_event_t;

typedef enum dhd_attach_states
{
	DHD_ATTACH_STATE_INIT = 0x0,
	DHD_ATTACH_STATE_NET_ALLOC = 0x1,
	DHD_ATTACH_STATE_DHD_ALLOC = 0x2,
	DHD_ATTACH_STATE_ADD_IF = 0x4,
	DHD_ATTACH_STATE_PROT_ATTACH = 0x8,
	DHD_ATTACH_STATE_WL_ATTACH = 0x10,
	DHD_ATTACH_STATE_THREADS_CREATED = 0x20,
	DHD_ATTACH_STATE_WAKELOCKS_INIT = 0x40,
	DHD_ATTACH_STATE_CFG80211 = 0x80,
	DHD_ATTACH_STATE_EARLYSUSPEND_DONE = 0x100,
	DHD_ATTACH_STATE_DONE = 0x200
} dhd_attach_states_t;

/* Value -1 means we are unsuccessful in creating the kthread. */
#define DHD_PID_KT_INVALID 	-1
/* Value -2 means we are unsuccessful in both creating the kthread and tasklet */
#define DHD_PID_KT_TL_INVALID	-2

/*
 * Exported from dhd OS modules (dhd_linux/dhd_ndis)
 */

/* To allow osl_attach/detach calls from os-independent modules */
osl_t *dhd_osl_attach(void *pdev, uint bustype);
void dhd_osl_detach(osl_t *osh);

/* Indication from bus module regarding presence/insertion of dongle.
 * Return dhd_pub_t pointer, used as handle to OS module in later calls.
 * Returned structure should have bus and prot pointers filled in.
 * bus_hdrlen specifies required headroom for bus module header.
 */
extern dhd_pub_t *dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen);
extern int dhd_net_attach(dhd_pub_t *dhdp, int idx);

/* Indication from bus module regarding removal/absence of dongle */
extern void dhd_detach(dhd_pub_t *dhdp);
extern void dhd_free(dhd_pub_t *dhdp);

/* Indication from bus module to change flow-control state */
extern void dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool on);

extern bool dhd_prec_enq(dhd_pub_t *dhdp, struct pktq *q, void *pkt, int prec);

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
extern void dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *rxp, int numpkt, uint8 chan);

/* Return pointer to interface name */
extern char *dhd_ifname(dhd_pub_t *dhdp, int idx);

/* Request scheduling of the bus dpc */
extern void dhd_sched_dpc(dhd_pub_t *dhdp);

/* Notify tx completion */
extern void dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success);

/* OS independent layer functions */
extern int dhd_os_proto_block(dhd_pub_t * pub);
extern int dhd_os_proto_unblock(dhd_pub_t * pub);
extern int dhd_os_ioctl_resp_wait(dhd_pub_t * pub, uint * condition, bool * pending);
extern int dhd_os_ioctl_resp_wake(dhd_pub_t * pub);
extern unsigned int dhd_os_get_ioctl_resp_timeout(void);
extern void dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec);
extern void * dhd_os_open_image(char * filename);
extern int dhd_os_get_image_block(char * buf, int len, void * image);
extern void dhd_os_close_image(void * image);
extern void dhd_os_wd_timer(void *bus, uint wdtick);
extern void dhd_os_sdlock(dhd_pub_t * pub);
extern void dhd_os_sdunlock(dhd_pub_t * pub);
extern void dhd_os_sdlock_txq(dhd_pub_t * pub);
extern void dhd_os_sdunlock_txq(dhd_pub_t * pub);
extern void dhd_os_sdlock_rxq(dhd_pub_t * pub);
extern void dhd_os_sdunlock_rxq(dhd_pub_t * pub);
extern void dhd_os_sdlock_sndup_rxq(dhd_pub_t * pub);
extern void dhd_customer_gpio_wlan_ctrl(int onoff);
extern int dhd_custom_get_mac_address(unsigned char *buf);
extern void dhd_os_sdunlock_sndup_rxq(dhd_pub_t * pub);
extern void dhd_os_sdlock_eventq(dhd_pub_t * pub);
extern void dhd_os_sdunlock_eventq(dhd_pub_t * pub);
extern int dhd_pno_enable(dhd_pub_t *dhd, int pfn_enabled);
extern int dhd_pno_clean(dhd_pub_t *dhd);
extern int dhd_pno_set(dhd_pub_t *dhd, wlc_ssid_t* ssids_local, int nssid,
                       ushort  scan_fr, int pno_repeat, int pno_freq_expo_max);
extern int dhd_pno_get_status(dhd_pub_t *dhd);
extern int dhd_dev_pno_reset(struct net_device *dev);
extern int dhd_dev_pno_set(struct net_device *dev, wlc_ssid_t* ssids_local,
                           int nssid, ushort  scan_fr, int pno_repeat, int pno_freq_expo_max);
extern int dhd_dev_pno_enable(struct net_device *dev,  int pfn_enabled);
extern int dhd_dev_get_pno_status(struct net_device *dev);
extern int dhd_get_dtim_skip(dhd_pub_t *dhd);
extern bool dhd_check_ap_wfd_mode_set(dhd_pub_t *dhd);
extern bool dhd_os_check_hang(dhd_pub_t *dhdp, int ifidx, int ret);

#define DHD_UNICAST_FILTER_NUM		0
#define DHD_BROADCAST_FILTER_NUM	1
#define DHD_MULTICAST4_FILTER_NUM	2
#define DHD_MULTICAST6_FILTER_NUM	3
extern int net_os_set_packet_filter(struct net_device *dev, int val);
extern int net_os_rxfilter_add_remove(struct net_device *dev, int val, int num);

#ifdef DHD_DEBUG
extern int write_to_file(dhd_pub_t *dhd, uint8 *buf, int size);
#endif /* DHD_DEBUG */
#if defined(OOB_INTR_ONLY)
extern int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr);
#endif /* defined(OOB_INTR_ONLY) */
extern void dhd_os_sdtxlock(dhd_pub_t * pub);
extern void dhd_os_sdtxunlock(dhd_pub_t * pub);

typedef struct {
	uint32 limit;		/* Expiration time (usec) */
	uint32 increment;	/* Current expiration increment (usec) */
	uint32 elapsed;		/* Current elapsed time (usec) */
	uint32 tick;		/* O/S tick time (usec) */
} dhd_timeout_t;

extern void dhd_timeout_start(dhd_timeout_t *tmo, uint usec);
extern int dhd_timeout_expired(dhd_timeout_t *tmo);

extern int dhd_ifname2idx(struct dhd_info *dhd, char *name);
extern int dhd_net2idx(struct dhd_info *dhd, struct net_device *net);
extern struct net_device * dhd_idx2net(struct dhd_pub *dhd_pub, int ifidx);
extern int wl_host_event(dhd_pub_t *dhd_pub, int *idx, void *pktdata,
                         wl_event_msg_t *, void **data_ptr);
extern void wl_event_to_host_order(wl_event_msg_t * evt);

extern int dhd_wl_ioctl(dhd_pub_t *dhd_pub, int ifindex, wl_ioctl_t *ioc, void *buf, int len);
extern int dhd_wl_ioctl_cmd(dhd_pub_t *dhd_pub, int cmd, void *arg, int len, uint8 set,
                            int ifindex);

extern struct dhd_cmn *dhd_common_init(osl_t *osh);
extern void dhd_common_deinit(dhd_pub_t *dhd_pub, dhd_cmn_t *sa_cmn);

extern int dhd_add_if(struct dhd_info *dhd, int ifidx, void *handle,
	char *name, uint8 *mac_addr, uint32 flags, uint8 bssidx);
extern void dhd_del_if(struct dhd_info *dhd, int ifidx);

extern void dhd_vif_add(struct dhd_info *dhd, int ifidx, char * name);
extern void dhd_vif_del(struct dhd_info *dhd, int ifidx);

extern void dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx);
extern void dhd_vif_sendup(struct dhd_info *dhd, int ifidx, uchar *cp, int len);


/* Send packet to dongle via data channel */
extern int dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pkt);

/* send up locally generated event */
extern void dhd_sendup_event_common(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data);
/* Send event to host */
extern void dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data);
extern int dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag);
extern uint dhd_bus_status(dhd_pub_t *dhdp);
extern int  dhd_bus_start(dhd_pub_t *dhdp);
extern int dhd_bus_membytes(dhd_pub_t *dhdp, bool set, uint32 address, uint8 *data, uint size);
extern void dhd_print_buf(void *pbuf, int len, int bytes_per_line);
extern bool dhd_is_associated(dhd_pub_t *dhd, void *bss_buf);

#if defined(KEEP_ALIVE)
extern int dhd_keep_alive_onoff(dhd_pub_t *dhd);
#endif /* KEEP_ALIVE */

#ifdef ARP_OFFLOAD_SUPPORT
extern void dhd_arp_offload_set(dhd_pub_t * dhd, int arp_mode);
extern void dhd_arp_offload_enable(dhd_pub_t * dhd, int arp_enable);
#endif /* ARP_OFFLOAD_SUPPORT */


typedef enum cust_gpio_modes {
	WLAN_RESET_ON,
	WLAN_RESET_OFF,
	WLAN_POWER_ON,
	WLAN_POWER_OFF
} cust_gpio_modes_t;

extern int wl_iw_iscan_set_scan_broadcast_prep(struct net_device *dev, uint flag);
extern int wl_iw_send_priv_event(struct net_device *dev, char *flag);
/*
 * Insmod parameters for debug/test
 */

/* Watchdog timer interval */
extern uint dhd_watchdog_ms;

#if defined(DHD_DEBUG)
/* Console output poll interval */
extern uint dhd_console_ms;
extern uint wl_msg_level;
#endif /* defined(DHD_DEBUG) */

/* Use interrupts */
extern uint dhd_intr;

/* Use polling */
extern uint dhd_poll;

/* ARP offload agent mode */
extern uint dhd_arp_mode;

/* ARP offload enable */
extern uint dhd_arp_enable;

/* Pkt filte enable control */
extern uint dhd_pkt_filter_enable;

/*  Pkt filter init setup */
extern uint dhd_pkt_filter_init;

/* Pkt filter mode control */
extern uint dhd_master_mode;

/* Roaming mode control */
extern uint dhd_roam_disable;

/* Roaming mode control */
extern uint dhd_radio_up;

/* Initial idletime ticks (may be -1 for immediate idle, 0 for no idle) */
extern int dhd_idletime;
#define DHD_IDLETIME_TICKS 1

/* SDIO Drive Strength */
extern uint dhd_sdiod_drive_strength;

/* Override to force tx queueing all the time */
extern uint dhd_force_tx_queueing;
/* Default KEEP_ALIVE Period is 55 sec to prevent AP from sending Keep Alive probe frame */
#define KEEP_ALIVE_PERIOD 55000
#define NULL_PKT_STR	"null_pkt"

#ifdef SDTEST
/* Echo packet generator (SDIO), pkts/s */
extern uint dhd_pktgen;

/* Echo packet len (0 => sawtooth, max 1800) */
extern uint dhd_pktgen_len;
#define MAX_PKTGEN_LEN 1800
#endif


/* optionally set by a module_param_string() */
#define MOD_PARAM_PATHLEN	2048
extern char fw_path[MOD_PARAM_PATHLEN];
extern char nv_path[MOD_PARAM_PATHLEN];

#ifdef SOFTAP
extern char fw_path2[MOD_PARAM_PATHLEN];
#endif

/* Flag to indicate if we should download firmware on driver load */
extern uint dhd_download_fw_on_driverload;

/* For supporting multiple interfaces */
#define DHD_MAX_IFS	16
#define DHD_DEL_IF	-0xe
#define DHD_BAD_IF	-0xf

#ifdef PROP_TXSTATUS
/* Please be mindful that total pkttag space is 32 octets only */
typedef struct dhd_pkttag {
	/*
	b[11 ] - 1 = this packet was sent in response to one time packet request,
	do not increment credit on status for this one. [WLFC_CTL_TYPE_MAC_REQUEST_PACKET].
	b[10 ] - 1 = signal-only-packet to firmware [i.e. nothing to piggyback on]
	b[9  ] - 1 = packet is host->firmware (transmit direction)
	       - 0 = packet received from firmware (firmware->host)
	b[8  ] - 1 = packet was sent due to credit_request (pspoll),
	             packet does not count against FIFO credit.
	       - 0 = normal transaction, packet counts against FIFO credit
	b[7  ] - 1 = AP, 0 = STA
	b[6:4] - AC FIFO number
	b[3:0] - interface index
	*/
	uint16	if_flags;
	/* destination MAC address for this packet so that not every
	module needs to open the packet to find this
	*/
	uint8	dstn_ether[ETHER_ADDR_LEN];
	/*
	This 32-bit goes from host to device for every packet.
	*/
	uint32	htod_tag;
	/* bus specific stuff */
	union {
		struct {
			void* stuff;
			uint32 thing1;
			uint32 thing2;
		} sd;
		struct {
			void* bus;
			void* urb;
		} usb;
	} bus_specific;
} dhd_pkttag_t;

#define DHD_PKTTAG_SET_H2DTAG(tag, h2dvalue)	((dhd_pkttag_t*)(tag))->htod_tag = (h2dvalue)
#define DHD_PKTTAG_H2DTAG(tag)					(((dhd_pkttag_t*)(tag))->htod_tag)

#define DHD_PKTTAG_IFMASK		0xf
#define DHD_PKTTAG_IFTYPE_MASK	0x1
#define DHD_PKTTAG_IFTYPE_SHIFT	7
#define DHD_PKTTAG_FIFO_MASK	0x7
#define DHD_PKTTAG_FIFO_SHIFT	4

#define DHD_PKTTAG_SIGNALONLY_MASK			0x1
#define DHD_PKTTAG_SIGNALONLY_SHIFT			10

#define DHD_PKTTAG_ONETIMEPKTRQST_MASK		0x1
#define DHD_PKTTAG_ONETIMEPKTRQST_SHIFT		11

#define DHD_PKTTAG_PKTDIR_MASK			0x1
#define DHD_PKTTAG_PKTDIR_SHIFT			9

#define DHD_PKTTAG_CREDITCHECK_MASK		0x1
#define DHD_PKTTAG_CREDITCHECK_SHIFT	8

#define DHD_PKTTAG_INVALID_FIFOID 0x7

#define DHD_PKTTAG_SETFIFO(tag, fifo)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & ~(DHD_PKTTAG_FIFO_MASK << DHD_PKTTAG_FIFO_SHIFT)) | \
	(((fifo) & DHD_PKTTAG_FIFO_MASK) << DHD_PKTTAG_FIFO_SHIFT)
#define DHD_PKTTAG_FIFO(tag)			((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_FIFO_SHIFT) & DHD_PKTTAG_FIFO_MASK)

#define DHD_PKTTAG_SETIF(tag, if)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & ~DHD_PKTTAG_IFMASK) | ((if) & DHD_PKTTAG_IFMASK)
#define DHD_PKTTAG_IF(tag)	(((dhd_pkttag_t*)(tag))->if_flags & DHD_PKTTAG_IFMASK)

#define DHD_PKTTAG_SETIFTYPE(tag, isAP)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_IFTYPE_MASK << DHD_PKTTAG_IFTYPE_SHIFT)) | \
	(((isAP) & DHD_PKTTAG_IFTYPE_MASK) << DHD_PKTTAG_IFTYPE_SHIFT)
#define DHD_PKTTAG_IFTYPE(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_IFTYPE_SHIFT) & DHD_PKTTAG_IFTYPE_MASK)

#define DHD_PKTTAG_SETCREDITCHECK(tag, check)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_CREDITCHECK_MASK << DHD_PKTTAG_CREDITCHECK_SHIFT)) | \
	(((check) & DHD_PKTTAG_CREDITCHECK_MASK) << DHD_PKTTAG_CREDITCHECK_SHIFT)
#define DHD_PKTTAG_CREDITCHECK(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_CREDITCHECK_SHIFT) & DHD_PKTTAG_CREDITCHECK_MASK)

#define DHD_PKTTAG_SETPKTDIR(tag, dir)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_PKTDIR_MASK << DHD_PKTTAG_PKTDIR_SHIFT)) | \
	(((dir) & DHD_PKTTAG_PKTDIR_MASK) << DHD_PKTTAG_PKTDIR_SHIFT)
#define DHD_PKTTAG_PKTDIR(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_PKTDIR_SHIFT) & DHD_PKTTAG_PKTDIR_MASK)

#define DHD_PKTTAG_SETSIGNALONLY(tag, signalonly)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_SIGNALONLY_MASK << DHD_PKTTAG_SIGNALONLY_SHIFT)) | \
	(((signalonly) & DHD_PKTTAG_SIGNALONLY_MASK) << DHD_PKTTAG_SIGNALONLY_SHIFT)
#define DHD_PKTTAG_SIGNALONLY(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_SIGNALONLY_SHIFT) & DHD_PKTTAG_SIGNALONLY_MASK)

#define DHD_PKTTAG_SETONETIMEPKTRQST(tag)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_ONETIMEPKTRQST_MASK << DHD_PKTTAG_ONETIMEPKTRQST_SHIFT)) | \
	(1 << DHD_PKTTAG_ONETIMEPKTRQST_SHIFT)
#define DHD_PKTTAG_ONETIMEPKTRQST(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_ONETIMEPKTRQST_SHIFT) & DHD_PKTTAG_ONETIMEPKTRQST_MASK)

#define DHD_PKTTAG_SETDSTN(tag, dstn_MAC_ea)	memcpy(((dhd_pkttag_t*)((tag)))->dstn_ether, \
	(dstn_MAC_ea), ETHER_ADDR_LEN)
#define DHD_PKTTAG_DSTN(tag)	((dhd_pkttag_t*)(tag))->dstn_ether

typedef int (*f_commitpkt_t)(void* ctx, void* p);
int dhd_wlfc_enable(dhd_pub_t *dhd);
int dhd_wlfc_interface_event(struct dhd_info *, uint8 action, uint8 ifid, uint8 iftype, uint8* ea);
int dhd_wlfc_FIFOcreditmap_event(struct dhd_info *dhd, uint8* event_data);
int dhd_wlfc_event(struct dhd_info *dhd);
int dhd_os_wlfc_block(dhd_pub_t *pub);
int dhd_os_wlfc_unblock(dhd_pub_t *pub);

#ifdef PROP_TXSTATUS_DEBUG
#define DHD_WLFC_CTRINC_MAC_CLOSE(entry)	do { (entry)->closed_ct++; } while (0)
#define DHD_WLFC_CTRINC_MAC_OPEN(entry)		do { (entry)->opened_ct++; } while (0)
#else
#define DHD_WLFC_CTRINC_MAC_CLOSE(entry)	do {} while (0)
#define DHD_WLFC_CTRINC_MAC_OPEN(entry)		do {} while (0)
#endif

#endif /* PROP_TXSTATUS */

extern void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar);
extern void dhd_wait_event_wakeup(dhd_pub_t*dhd);

#ifdef ARP_OFFLOAD_SUPPORT
#define MAX_IPV4_ENTRIES	8
/* dhd_commn arp offload wrapers */
void dhd_aoe_hostip_clr(dhd_pub_t *dhd);
void dhd_aoe_arp_clr(dhd_pub_t *dhd);
int dhd_arp_get_arp_hostip_table(dhd_pub_t *dhd, void *buf, int buflen);
void dhd_arp_offload_add_ip(dhd_pub_t *dhd, uint32 ipaddr);
#endif /* ARP_OFFLOAD_SUPPORT */

#endif /* _dhd_h_ */
