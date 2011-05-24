/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/****************
 * Common types *
 */

#ifndef _dhd_h_
#define _dhd_h_

#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/suspend.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
/* The kernel threading is sdio-specific */

#include <wlioctl.h>

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

/* Common structure for module and instance linkage */
typedef struct dhd_pub {
	/* Linkage ponters */
	struct dhd_bus *bus;	/* Bus module handle */
	struct dhd_prot *prot;	/* Protocol module handle */
	struct dhd_info *info;	/* Info module handle */

	/* Internal dhd items */
	bool up;		/* Driver up/down (to OS) */
	bool txoff;		/* Transmit flow-controlled */
	bool dongle_reset;	/* true = DEVRESET put dongle into reset */
	enum dhd_bus_state busstate;
	uint hdrlen;		/* Total DHD header length (proto + bus) */
	uint maxctl;		/* Max size rxctl request from proto to bus */
	uint rxsz;		/* Rx buffer size bus module should use */
	u8 wme_dp;		/* wme discard priority */

	/* Dongle media info */
	bool iswl;		/* Dongle-resident driver is wl */
	unsigned long drv_version;	/* Version of dongle-resident driver */
	u8 mac[ETH_ALEN];			/* MAC address obtained from dongle */
	dngl_stats_t dstats;		/* Stats for dongle-based data */

	/* Additional stats for the bus level */
	unsigned long tx_packets;	/* Data packets sent to dongle */
	unsigned long tx_multicast;	/* Multicast data packets sent to dongle */
	unsigned long tx_errors;	/* Errors in sending data to dongle */
	unsigned long tx_ctlpkts;	/* Control packets sent to dongle */
	unsigned long tx_ctlerrs;	/* Errors sending control frames to dongle */
	unsigned long rx_packets;	/* Packets sent up the network interface */
	unsigned long rx_multicast;	/* Multicast packets sent up the network
					 interface */
	unsigned long rx_errors;	/* Errors processing rx data packets */
	unsigned long rx_ctlpkts;	/* Control frames processed from dongle */
	unsigned long rx_ctlerrs;	/* Errors in processing rx control frames */
	unsigned long rx_dropped;	/* Packets dropped locally (no memory) */
	unsigned long rx_flushed;	/* Packets flushed due to
				unscheduled sendup thread */
	unsigned long wd_dpc_sched;	/* Number of times dhd dpc scheduled by
					 watchdog timer */

	unsigned long rx_readahead_cnt;	/* Number of packets where header read-ahead
					 was used. */
	unsigned long tx_realloc;	/* Number of tx packets we had to realloc for
					 headroom */
	unsigned long fc_packets;	/* Number of flow control pkts recvd */

	/* Last error return */
	int bcmerror;
	uint tickcnt;

	/* Last error from dongle */
	int dongle_error;

	/* Suspend disable flag  flag */
	int suspend_disable_flag;	/* "1" to disable all extra powersaving
					 during suspend */
	int in_suspend;		/* flag set to 1 when early suspend called */
#ifdef PNO_SUPPORT
	int pno_enable;		/* pno status : "1" is pno enable */
#endif				/* PNO_SUPPORT */
	int dtim_skip;		/* dtim skip , default 0 means wake each dtim */

	/* Pkt filter defination */
	char *pktfilter[100];
	int pktfilter_count;

	u8 country_code[WLC_CNTRY_BUF_SZ];
	char eventmask[WL_EVENTING_MASK_LEN];

} dhd_pub_t;

#if defined(CONFIG_PM_SLEEP)
extern atomic_t dhd_mmc_suspend;
#define DHD_PM_RESUME_WAIT_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
#define _DHD_PM_RESUME_WAIT(a, b) do { \
		int retry = 0; \
		while (atomic_read(&dhd_mmc_suspend) && retry++ != b) { \
			wait_event_timeout(a, false, HZ/100); \
		} \
	}	while (0)
#define DHD_PM_RESUME_WAIT(a)	_DHD_PM_RESUME_WAIT(a, 30)
#define DHD_PM_RESUME_WAIT_FOREVER(a)	_DHD_PM_RESUME_WAIT(a, ~0)
#define DHD_PM_RESUME_RETURN_ERROR(a)	\
	do { if (atomic_read(&dhd_mmc_suspend)) return a; } while (0)
#define DHD_PM_RESUME_RETURN	do { \
	if (atomic_read(&dhd_mmc_suspend)) \
		return; \
	} while (0)

#define DHD_SPINWAIT_SLEEP_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
#define SPINWAIT_SLEEP(a, exp, us) do { \
		uint countdown = (us) + 9999; \
		while ((exp) && (countdown >= 10000)) { \
			wait_event_timeout(a, false, HZ/100); \
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
			udelay(10);  \
			countdown -= 10;  \
		} \
	} while (0)

#endif	/* defined(CONFIG_PM_SLEEP) */
#define DHD_IF_VIF	0x01	/* Virtual IF (Hidden from user) */

static inline void MUTEX_LOCK_INIT(dhd_pub_t *dhdp)
{
}

static inline void MUTEX_LOCK(dhd_pub_t *dhdp)
{
}

static inline void MUTEX_UNLOCK(dhd_pub_t *dhdp)
{
}

static inline void MUTEX_LOCK_SOFTAP_SET_INIT(dhd_pub_t *dhdp)
{
}

static inline void MUTEX_LOCK_SOFTAP_SET(dhd_pub_t *dhdp)
{
}

static inline void MUTEX_UNLOCK_SOFTAP_SET(dhd_pub_t *dhdp)
{
}

static inline void MUTEX_LOCK_WL_SCAN_SET_INIT(void)
{
}

static inline void MUTEX_LOCK_WL_SCAN_SET(void)
{
}

static inline void MUTEX_UNLOCK_WL_SCAN_SET(void)
{
}

typedef struct dhd_if_event {
	u8 ifidx;
	u8 action;
	u8 flags;
	u8 bssidx;
} dhd_if_event_t;

/*
 * Exported from dhd OS modules (dhd_linux/dhd_ndis)
 */

/* Indication from bus module regarding presence/insertion of dongle.
 * Return dhd_pub_t pointer, used as handle to OS module in later calls.
 * Returned structure should have bus and prot pointers filled in.
 * bus_hdrlen specifies required headroom for bus module header.
 */
extern dhd_pub_t *dhd_attach(struct dhd_bus *bus,
				uint bus_hdrlen);
extern int dhd_net_attach(dhd_pub_t *dhdp, int idx);

/* Indication from bus module regarding removal/absence of dongle */
extern void dhd_detach(dhd_pub_t *dhdp);

/* Indication from bus module to change flow-control state */
extern void dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool on);

extern bool dhd_prec_enq(dhd_pub_t *dhdp, struct pktq *q,
			 struct sk_buff *pkt, int prec);

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
extern void dhd_rx_frame(dhd_pub_t *dhdp, int ifidx,
			 struct sk_buff *rxp, int numpkt);

/* Return pointer to interface name */
extern char *dhd_ifname(dhd_pub_t *dhdp, int idx);

/* Request scheduling of the bus dpc */
extern void dhd_sched_dpc(dhd_pub_t *dhdp);

/* Notify tx completion */
extern void dhd_txcomplete(dhd_pub_t *dhdp, struct sk_buff *txp, bool success);

/* Query ioctl */
extern int dhdcdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf,
			      uint len);

/* OS independent layer functions */
extern int dhd_os_proto_block(dhd_pub_t *pub);
extern int dhd_os_proto_unblock(dhd_pub_t *pub);
extern int dhd_os_ioctl_resp_wait(dhd_pub_t *pub, uint *condition,
				  bool *pending);
extern int dhd_os_ioctl_resp_wake(dhd_pub_t *pub);
extern unsigned int dhd_os_get_ioctl_resp_timeout(void);
extern void dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec);
extern void *dhd_os_open_image(char *filename);
extern int dhd_os_get_image_block(char *buf, int len, void *image);
extern void dhd_os_close_image(void *image);
extern void dhd_os_wd_timer(void *bus, uint wdtick);
extern void dhd_os_sdlock(dhd_pub_t *pub);
extern void dhd_os_sdunlock(dhd_pub_t *pub);
extern void dhd_os_sdlock_txq(dhd_pub_t *pub);
extern void dhd_os_sdunlock_txq(dhd_pub_t *pub);
extern void dhd_os_sdlock_rxq(dhd_pub_t *pub);
extern void dhd_os_sdunlock_rxq(dhd_pub_t *pub);
extern void dhd_os_sdlock_sndup_rxq(dhd_pub_t *pub);
extern void dhd_customer_gpio_wlan_ctrl(int onoff);
extern int dhd_custom_get_mac_address(unsigned char *buf);
extern void dhd_os_sdunlock_sndup_rxq(dhd_pub_t *pub);
extern void dhd_os_sdlock_eventq(dhd_pub_t *pub);
extern void dhd_os_sdunlock_eventq(dhd_pub_t *pub);
#ifdef DHD_DEBUG
extern int write_to_file(dhd_pub_t *dhd, u8 *buf, int size);
#endif				/* DHD_DEBUG */
#if defined(OOB_INTR_ONLY)
extern int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr);
#endif				/* defined(OOB_INTR_ONLY) */
extern void dhd_os_sdtxlock(dhd_pub_t *pub);
extern void dhd_os_sdtxunlock(dhd_pub_t *pub);

int setScheduler(struct task_struct *p, int policy, struct sched_param *param);

typedef struct {
	u32 limit;		/* Expiration time (usec) */
	u32 increment;	/* Current expiration increment (usec) */
	u32 elapsed;		/* Current elapsed time (usec) */
	u32 tick;		/* O/S tick time (usec) */
} dhd_timeout_t;

extern void dhd_timeout_start(dhd_timeout_t *tmo, uint usec);
extern int dhd_timeout_expired(dhd_timeout_t *tmo);

extern int dhd_ifname2idx(struct dhd_info *dhd, char *name);
extern u8 *dhd_bssidx2bssid(dhd_pub_t *dhd, int idx);
extern int wl_host_event(struct dhd_info *dhd, int *idx, void *pktdata,
			 wl_event_msg_t *, void **data_ptr);

extern void dhd_common_init(void);

extern int dhd_add_if(struct dhd_info *dhd, int ifidx, void *handle,
		      char *name, u8 *mac_addr, u32 flags, u8 bssidx);
extern void dhd_del_if(struct dhd_info *dhd, int ifidx);

extern void dhd_vif_add(struct dhd_info *dhd, int ifidx, char *name);
extern void dhd_vif_del(struct dhd_info *dhd, int ifidx);

extern void dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx);
extern void dhd_vif_sendup(struct dhd_info *dhd, int ifidx, unsigned char * cp,
			   int len);

/* Send packet to dongle via data channel */
extern int dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, struct sk_buff *pkt);

/* Send event to host */
extern void dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event,
			     void *data);
extern int dhd_bus_devreset(dhd_pub_t *dhdp, u8 flag);
extern uint dhd_bus_status(dhd_pub_t *dhdp);
extern int dhd_bus_start(dhd_pub_t *dhdp);

enum cust_gpio_modes {
	WLAN_RESET_ON,
	WLAN_RESET_OFF,
	WLAN_POWER_ON,
	WLAN_POWER_OFF
};
/*
 * Insmod parameters for debug/test
 */

/* Watchdog timer interval */
extern uint dhd_watchdog_ms;

#if defined(DHD_DEBUG)
/* Console output poll interval */
extern uint dhd_console_ms;
#endif				/* defined(DHD_DEBUG) */

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

extern void dhd_wait_for_event(dhd_pub_t *dhd, bool * lockvar);
extern void dhd_wait_event_wakeup(dhd_pub_t *dhd);

extern u32 g_assert_type;

#ifdef BCMDBG
#define ASSERT(exp) \
	  do { if (!(exp)) osl_assert(#exp, __FILE__, __LINE__); } while (0)
extern void osl_assert(char *exp, char *file, int line);
#else
#define ASSERT(exp)	do {} while (0)
#endif  /* defined(BCMDBG) */

#endif				/* _dhd_h_ */
