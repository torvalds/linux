/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
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
 * $Id: dhd.h,v 1.32.4.7.2.4.14.49.4.9 2011/01/14 22:40:45 Exp $
 */

/****************
 * Common types *
 */

#ifndef _dhd_h_
#define _dhd_h_

#if defined(LINUX)
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

/* The kernel threading is sdio-specific */
#else /* LINUX */
#define ENOMEM		1
#define EFAULT      2
#define EINVAL		3
#define EIO			4
#define ETIMEDOUT	5
#define ERESTARTSYS 6
#endif /* LINUX */

#include <wlioctl.h>

#ifdef DHD_DEBUG
#ifndef DHD_DEBUG_TRAP
#define DHD_DEBUG_TRAP
#endif
#endif

/* Forward decls */
struct dhd_bus;
struct dhd_prot;
struct dhd_info;

/* The level of bus communication with the dongle */
enum dhd_bus_state {
	DHD_BUS_DOWN,		/* Not ready for frame transfers */
	DHD_BUS_LOAD,		/* Download access only (CPU reset) */
	DHD_BUS_DATA		/* Ready for frame transfers */
};

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
#ifdef DHD_USE_STATIC_BUF
extern void * dhd_os_prealloc(int section, unsigned long size);
#endif
/* Common structure for module and instance linkage */
typedef struct dhd_pub {
	/* Linkage ponters */
	osl_t *osh;		/* OSL handle */
	struct dhd_bus *bus;	/* Bus module handle */
	struct dhd_prot *prot;	/* Protocol module handle */
	struct dhd_info  *info; /* Info module handle */

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
	ulong fc_packets;	/* Number of flow control pkts recvd */

	/* Last error return */
	int bcmerror;
	uint tickcnt;

	/* Last error from dongle */
	int dongle_error;

	/* Suspend disable flag and "in suspend" flag */
	int suspend_disable_flag; /* "1" to disable all extra powersaving during suspend */
	int in_suspend;			/* flag set to 1 when early suspend called */
	int hang_was_sent;	/* flag that message was send at least once */
#ifdef PNO_SUPPORT
	int pno_enable;                 /* pno status : "1" is pno enable */
#endif /* PNO_SUPPORT */
	int dtim_skip;         /* dtim skip , default 0 means wake each dtim */

	/* Pkt filter defination */
	char * pktfilter[100];
	int pktfilter_count;

	wl_country_t dhd_cspec;		/* Current Locale info */
	char eventmask[WL_EVENTING_MASK_LEN];

} dhd_pub_t;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)

	#define DHD_PM_RESUME_WAIT_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
	#define _DHD_PM_RESUME_WAIT(a, b) do { \
			int retry = 0; \
			smp_mb(); \
			while (dhd_mmc_suspend && retry++ != b) { \
				wait_event_interruptible_timeout(a, FALSE, HZ/100); \
			} \
		} 	while (0)
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

#define DHD_IF_VIF	0x01	/* Virtual IF (Hidden from user) */

inline static void NETIF_ADDR_LOCK(struct net_device *dev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	netif_addr_lock_bh(dev);
#endif
}

inline static void NETIF_ADDR_UNLOCK(struct net_device *dev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	netif_addr_unlock_bh(dev);
#endif
}

/* Wakelock Functions */
extern int dhd_os_wake_lock(dhd_pub_t *pub);
extern int dhd_os_wake_unlock(dhd_pub_t *pub);
extern int dhd_os_wake_lock_timeout(dhd_pub_t *pub);
extern int dhd_os_wake_lock_timeout_enable(dhd_pub_t *pub);

extern void dhd_os_start_lock(dhd_pub_t *pub);
extern void dhd_os_start_unlock(dhd_pub_t *pub);
extern unsigned long dhd_os_spin_lock(dhd_pub_t *pub);
extern void dhd_os_spin_unlock(dhd_pub_t *pub, unsigned long flags);

typedef struct dhd_if_event {
	uint8 ifidx;
	uint8 action;
	uint8 flags;
	uint8 bssidx;
} dhd_if_event_t;

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

/* Indication from bus module to change flow-control state */
extern void dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool on);

extern bool dhd_prec_enq(dhd_pub_t *dhdp, struct pktq *q, void *pkt, int prec);

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
extern void dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *rxp, int numpkt);

/* Return pointer to interface name */
extern char *dhd_ifname(dhd_pub_t *dhdp, int idx);

/* Request scheduling of the bus dpc */
extern void dhd_sched_dpc(dhd_pub_t *dhdp);

/* Notify tx completion */
extern void dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success);

/* Query ioctl */
extern int  dhdcdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);

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
#ifdef DHD_DEBUG
extern int write_to_file(dhd_pub_t *dhd, uint8 *buf, int size);
#endif /* DHD_DEBUG */
#if defined(OOB_INTR_ONLY)
extern int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr);
#endif /* defined(OOB_INTR_ONLY) */
extern void dhd_os_sdtxlock(dhd_pub_t * pub);
extern void dhd_os_sdtxunlock(dhd_pub_t * pub);

int setScheduler(struct task_struct *p, int policy, struct sched_param *param);

typedef struct {
	uint32 limit;		/* Expiration time (usec) */
	uint32 increment;	/* Current expiration increment (usec) */
	uint32 elapsed;		/* Current elapsed time (usec) */
	uint32 tick;		/* O/S tick time (usec) */
} dhd_timeout_t;

extern void dhd_timeout_start(dhd_timeout_t *tmo, uint usec);
extern int dhd_timeout_expired(dhd_timeout_t *tmo);

extern int dhd_ifname2idx(struct dhd_info *dhd, char *name);
extern uint8 *dhd_bssidx2bssid(dhd_pub_t *dhd, int idx);
extern int wl_host_event(struct dhd_info *dhd, int *idx, void *pktdata,
                         wl_event_msg_t *, void **data_ptr);
extern void wl_event_to_host_order(wl_event_msg_t * evt);

extern void dhd_common_init(void);

extern int dhd_add_if(struct dhd_info *dhd, int ifidx, void *handle,
	char *name, uint8 *mac_addr, uint32 flags, uint8 bssidx);
extern void dhd_del_if(struct dhd_info *dhd, int ifidx);

extern void dhd_vif_add(struct dhd_info *dhd, int ifidx, char * name);
extern void dhd_vif_del(struct dhd_info *dhd, int ifidx);

extern void dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx);
extern void dhd_vif_sendup(struct dhd_info *dhd, int ifidx, uchar *cp, int len);


/* Send packet to dongle via data channel */
extern int dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pkt);

/* Send event to host */
extern void dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data);
extern int dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag);
extern uint dhd_bus_status(dhd_pub_t *dhdp);
extern int  dhd_bus_start(dhd_pub_t *dhdp);

extern void print_buf(void *pbuf, int len, int bytes_per_line);


typedef enum cust_gpio_modes {
	WLAN_RESET_ON,
	WLAN_RESET_OFF,
	WLAN_POWER_ON,
	WLAN_POWER_OFF
} cust_gpio_modes_t;

extern int wl_iw_iscan_set_scan_broadcast_prep(struct net_device *dev, uint flag);
extern int wl_iw_send_priv_event(struct net_device *dev, char *flag);
extern int net_os_send_hang_message(struct net_device *dev);

/*
 * Insmod parameters for debug/test
 */

/* Watchdog timer interval */
extern uint dhd_watchdog_ms;

#if defined(DHD_DEBUG)
/* Console output poll interval */
extern uint dhd_console_ms;
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
extern uint dhd_roam;

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

/* For supporting multiple interfaces */
#define DHD_MAX_IFS	16
#define DHD_DEL_IF	-0xe
#define DHD_BAD_IF	-0xf


extern void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar);
extern void dhd_wait_event_wakeup(dhd_pub_t*dhd);

/* dhd_commn arp offload wrapers */
extern void dhd_arp_cleanup(dhd_pub_t *dhd);
int dhd_arp_get_arp_hostip_table(dhd_pub_t *dhd, void *buf, int buflen);
void dhd_arp_offload_add_ip(dhd_pub_t *dhd, u32 ipaddr);

#define DHD_UNICAST_FILTER_NUM         0
#define DHD_BROADCAST_FILTER_NUM       1
#define DHD_MULTICAST4_FILTER_NUM      2
#define DHD_MULTICAST6_FILTER_NUM      3
extern int net_os_set_packet_filter(struct net_device *dev, int val);
extern int net_os_rxfilter_add_remove(struct net_device *dev, int val, int num);

#endif /* _dhd_h_ */
