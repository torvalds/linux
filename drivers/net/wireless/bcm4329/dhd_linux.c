/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
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
 * $Id: dhd_linux.c,v 1.65.4.9.2.12.2.104.4.29 2010/11/04 01:14:41 Exp $
 */

#ifdef CONFIG_WIFI_CONTROL_FUNC
#include <linux/platform_device.h>
#endif
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/fcntl.h>
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>

#include <proto/ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <wl_iw.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
#include <linux/wlan_plat.h>

struct semaphore wifi_control_sem;

struct dhd_bus *g_bus;

static struct wifi_platform_data *wifi_control_data = NULL;
static struct resource *wifi_irqres = NULL;

int wifi_get_irq_number(unsigned long *irq_flags_ptr)
{
	if (wifi_irqres) {
		*irq_flags_ptr = wifi_irqres->flags & IRQF_TRIGGER_MASK;
		return (int)wifi_irqres->start;
	}
#ifdef CUSTOM_OOB_GPIO_NUM
	return CUSTOM_OOB_GPIO_NUM;
#else
	return -1;
#endif
}

int wifi_set_carddetect(int on)
{
	printk("%s = %d\n", __FUNCTION__, on);
	if (wifi_control_data && wifi_control_data->set_carddetect) {
		wifi_control_data->set_carddetect(on);
	}
	return 0;
}

int wifi_set_power(int on, unsigned long msec)
{
	printk("%s = %d\n", __FUNCTION__, on);
	if (wifi_control_data && wifi_control_data->set_power) {
		wifi_control_data->set_power(on);
	}
	if (msec)
		mdelay(msec);
	return 0;
}

int wifi_set_reset(int on, unsigned long msec)
{
	printk("%s = %d\n", __FUNCTION__, on);
	if (wifi_control_data && wifi_control_data->set_reset) {
		wifi_control_data->set_reset(on);
	}
	if (msec)
		mdelay(msec);
	return 0;
}

int wifi_get_mac_addr(unsigned char *buf)
{
	printk("%s\n", __FUNCTION__);
	if (!buf)
		return -EINVAL;
	if (wifi_control_data && wifi_control_data->get_mac_addr) {
		return wifi_control_data->get_mac_addr(buf);
	}
	return -EOPNOTSUPP;
}

static int wifi_probe(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	DHD_TRACE(("## %s\n", __FUNCTION__));
	wifi_irqres = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "bcm4329_wlan_irq");
	wifi_control_data = wifi_ctrl;

	wifi_set_power(1, 0);	/* Power On */
	wifi_set_carddetect(1);	/* CardDetect (0->1) */

	up(&wifi_control_sem);
	return 0;
}

static int wifi_remove(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	DHD_TRACE(("## %s\n", __FUNCTION__));
	wifi_control_data = wifi_ctrl;

	wifi_set_power(0, 0);	/* Power Off */
	wifi_set_carddetect(0);	/* CardDetect (1->0) */

	up(&wifi_control_sem);
	return 0;
}
static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
	DHD_TRACE(("##> %s\n", __FUNCTION__));
	return 0;
}
static int wifi_resume(struct platform_device *pdev)
{
	DHD_TRACE(("##> %s\n", __FUNCTION__));
	return 0;
}

static struct platform_driver wifi_device = {
	.probe          = wifi_probe,
	.remove         = wifi_remove,
	.suspend        = wifi_suspend,
	.resume         = wifi_resume,
	.driver         = {
	.name   = "bcm4329_wlan",
	}
};

int wifi_add_dev(void)
{
	DHD_TRACE(("## Calling platform_driver_register\n"));
	return platform_driver_register(&wifi_device);
}

void wifi_del_dev(void)
{
	DHD_TRACE(("## Unregister platform_driver_register\n"));
	platform_driver_unregister(&wifi_device);
}
#endif /* defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC) */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = FALSE;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP) */

#if defined(OOB_INTR_ONLY)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif /* defined(OOB_INTR_ONLY) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
MODULE_LICENSE("GPL v2");
#endif /* LinuxVer */

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 15)
const char *
print_tainted()
{
	return "";
}
#endif	/* LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 15) */

/* Linux wireless extension support */
#if defined(CONFIG_WIRELESS_EXT)
#include <wl_iw.h>
#endif /* defined(CONFIG_WIRELESS_EXT) */

extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) */

#ifdef PKT_FILTER_SUPPORT
extern void dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg);
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
#endif

/* Interface control information */
typedef struct dhd_if {
	struct dhd_info *info;			/* back pointer to dhd_info */
	/* OS/stack specifics */
	struct net_device *net;
	struct net_device_stats stats;
	int 			idx;			/* iface idx in dongle */
	int 			state;			/* interface state */
	uint 			subunit;		/* subunit */
	uint8			mac_addr[ETHER_ADDR_LEN];	/* assigned MAC address */
	bool			attached;		/* Delayed attachment when unset */
	bool			txflowcontrol;	/* Per interface flow control indicator */
	char			name[IFNAMSIZ+1]; /* linux interface name */
} dhd_if_t;

/* Local private structure (extension of pub) */
typedef struct dhd_info {
#if defined(CONFIG_WIRELESS_EXT)
	wl_iw_t		iw;		/* wireless extensions state (must be first) */
#endif /* defined(CONFIG_WIRELESS_EXT) */

	dhd_pub_t pub;

	/* OS/stack specifics */
	dhd_if_t *iflist[DHD_MAX_IFS];

	struct semaphore proto_sem;
	wait_queue_head_t ioctl_resp_wait;
	struct timer_list timer;
	bool wd_timer_valid;
	struct tasklet_struct tasklet;
	spinlock_t	sdlock;
	spinlock_t	txqlock;
	spinlock_t	dhd_lock;

	/* Thread based operation */
	bool threads_only;
	struct semaphore sdsem;
	long watchdog_pid;
	struct semaphore watchdog_sem;
	struct completion watchdog_exited;
	long dpc_pid;
	struct semaphore dpc_sem;
	struct completion dpc_exited;

	/* Wakelocks */
#ifdef CONFIG_HAS_WAKELOCK
	struct wake_lock wl_wifi;   /* Wifi wakelock */
	struct wake_lock wl_rxwake; /* Wifi rx wakelock */
#endif
	spinlock_t wl_lock;
	int wl_count;
	int wl_packet;

	int hang_was_sent; /* flag that message was send at least once */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	struct mutex wl_start_lock; /* mutex when START called to prevent any other Linux calls */
#endif
	/* Thread to issue ioctl for multicast */
	long sysioc_pid;
	struct semaphore sysioc_sem;
	struct completion sysioc_exited;
	bool set_multicast;
	bool set_macaddress;
	struct ether_addr macvalue;
	wait_queue_head_t ctrl_wait;
	atomic_t pend_8021x_cnt;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif /* CONFIG_HAS_EARLYSUSPEND */
} dhd_info_t;

/* Definitions to provide path to the firmware and nvram
 * example nvram_path[MOD_PARAM_PATHLEN]="/projects/wlan/nvram.txt"
 */
char firmware_path[MOD_PARAM_PATHLEN];
char nvram_path[MOD_PARAM_PATHLEN];

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
struct semaphore dhd_registration_sem;
#define DHD_REGISTRATION_TIMEOUT  12000  /* msec : allowed time to finished dhd registration */
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
/* load firmware and/or nvram values from the filesystem */
module_param_string(firmware_path, firmware_path, MOD_PARAM_PATHLEN, 0);
module_param_string(nvram_path, nvram_path, MOD_PARAM_PATHLEN, 0);

/* Error bits */
module_param(dhd_msg_level, int, 0);

/* Spawn a thread for system ioctls (set mac, set mcast) */
uint dhd_sysioc = TRUE;
module_param(dhd_sysioc, uint, 0);

/* Watchdog interval */
uint dhd_watchdog_ms = 10;
module_param(dhd_watchdog_ms, uint, 0);

#ifdef DHD_DEBUG
/* Console poll interval */
uint dhd_console_ms = 0;
module_param(dhd_console_ms, uint, 0);
#endif /* DHD_DEBUG */

/* ARP offload agent mode : Enable ARP Host Auto-Reply and ARP Peer Auto-Reply */
uint dhd_arp_mode = 0xb;
module_param(dhd_arp_mode, uint, 0);

/* ARP offload enable */
uint dhd_arp_enable = TRUE;
module_param(dhd_arp_enable, uint, 0);

/* Global Pkt filter enable control */
uint dhd_pkt_filter_enable = TRUE;
module_param(dhd_pkt_filter_enable, uint, 0);

/*  Pkt filter init setup */
uint dhd_pkt_filter_init = 0;
module_param(dhd_pkt_filter_init, uint, 0);

/* Pkt filter mode control */
uint dhd_master_mode = TRUE;
module_param(dhd_master_mode, uint, 1);

/* Watchdog thread priority, -1 to use kernel timer */
int dhd_watchdog_prio = 97;
module_param(dhd_watchdog_prio, int, 0);

/* DPC thread priority, -1 to use tasklet */
int dhd_dpc_prio = 98;
module_param(dhd_dpc_prio, int, 0);

/* DPC thread priority, -1 to use tasklet */
extern int dhd_dongle_memsize;
module_param(dhd_dongle_memsize, int, 0);

/* Control fw roaming */
#ifdef CUSTOMER_HW2
uint dhd_roam = 0;
#else
uint dhd_roam = 1;
#endif

/* Control radio state */
uint dhd_radio_up = 1;

/* Network inteface name */
char iface_name[IFNAMSIZ];
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else /* Linux 2.4 (w/o preemption patch) */
#define RAISE_RX_SOFTIRQ() \
	cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
	} while (0);
#endif /* LINUX_VERSION_CODE  */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define BLOCKABLE()	(!in_atomic())
#else
#define BLOCKABLE()	(!in_interrupt())
#endif

/* The following are specific to the SDIO dongle */

/* IOCTL response timeout */
int dhd_ioctl_timeout_msec = IOCTL_RESP_TIMEOUT;

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

/* Tx/Rx bounds */
extern uint dhd_txbound;
extern uint dhd_rxbound;
module_param(dhd_txbound, uint, 0);
module_param(dhd_rxbound, uint, 0);

/* Deferred transmits */
extern uint dhd_deferred_tx;
module_param(dhd_deferred_tx, uint, 0);



#ifdef SDTEST
/* Echo packet generator (pkts/s) */
uint dhd_pktgen = 0;
module_param(dhd_pktgen, uint, 0);

/* Echo packet len (0 => sawtooth, max 2040) */
uint dhd_pktgen_len = 0;
module_param(dhd_pktgen_len, uint, 0);
#endif

/* Version string to report */
#ifdef DHD_DEBUG
#ifndef SRCBASE
#define SRCBASE        "drivers/net/wireless/bcm4329"
#endif
#define DHD_COMPILED "\nCompiled in " SRCBASE
#else
#define DHD_COMPILED
#endif

static char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR
#ifdef DHD_DEBUG
"\nCompiled in " SRCBASE " on " __DATE__ " at " __TIME__
#endif
;


#if defined(CONFIG_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif /* defined(CONFIG_WIRELESS_EXT) */

static void dhd_dpc(ulong data);
/* forward decl */
extern int dhd_wait_pend8021x(struct net_device *dev);

#ifdef TOE
#ifndef BDC
#error TOE requires BDC
#endif /* !BDC */
static int dhd_toe_get(dhd_info_t *dhd, int idx, uint32 *toe_ol);
static int dhd_toe_set(dhd_info_t *dhd, int idx, uint32 toe_ol);
#endif /* TOE */

static int dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
                             wl_event_msg_t *event_ptr, void **data_ptr);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
static int dhd_sleep_pm_callback(struct notifier_block *nfb, unsigned long action, void *ignored)
{
	int ret = NOTIFY_DONE;

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		dhd_mmc_suspend = TRUE;
		ret = NOTIFY_OK;
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		dhd_mmc_suspend = FALSE;
		ret = NOTIFY_OK;
		break;
	}
	smp_mb();
	return ret;
}

static struct notifier_block dhd_sleep_pm_notifier = {
	.notifier_call = dhd_sleep_pm_callback,
	.priority = 0
};
extern int register_pm_notifier(struct notifier_block *nb);
extern int unregister_pm_notifier(struct notifier_block *nb);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP) */

static void dhd_set_packet_filter(int value, dhd_pub_t *dhd)
{
#ifdef PKT_FILTER_SUPPORT
	DHD_TRACE(("%s: %d\n", __FUNCTION__, value));
	/* 1 - Enable packet filter, only allow unicast packet to send up */
	/* 0 - Disable packet filter */
	if (dhd_pkt_filter_enable) {
		int i;

		for (i = 0; i < dhd->pktfilter_count; i++) {
			dhd_pktfilter_offload_set(dhd, dhd->pktfilter[i]);
			dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
					value, dhd_master_mode);
		}
	}
#endif
}



#if defined(CONFIG_HAS_EARLYSUSPEND)
static int dhd_set_suspend(int value, dhd_pub_t *dhd)
{
	int power_mode = PM_MAX;
	/* wl_pkt_filter_enable_t	enable_parm; */
	char iovbuf[32];
	int bcn_li_dtim = 3;
#ifdef CUSTOMER_HW2
	uint roamvar = 1;
#endif /* CUSTOMER_HW2 */

	DHD_TRACE(("%s: enter, value = %d in_suspend = %d\n",
			__FUNCTION__, value, dhd->in_suspend));

	if (dhd && dhd->up) {
		if (value && dhd->in_suspend) {

			/* Kernel suspended */
			DHD_TRACE(("%s: force extra Suspend setting \n", __FUNCTION__));

			dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM,
				(char *)&power_mode, sizeof(power_mode));

			/* Enable packet filter, only allow unicast packet to send up */
			dhd_set_packet_filter(1, dhd);

			/* if dtim skip setup as default force it to wake each thrid dtim
			 *  for better power saving.
			 *  Note that side effect is chance to miss BC/MC packet
			*/
			if ((dhd->dtim_skip == 0) || (dhd->dtim_skip == 1))
				bcn_li_dtim = 3;
			else
				bcn_li_dtim = dhd->dtim_skip;
			bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
				4, iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#ifdef CUSTOMER_HW2
			/* Disable build-in roaming during suspend */
			bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#endif /* CUSTOMER_HW2 */

		} else {

			/* Kernel resumed  */
			DHD_TRACE(("%s: Remove extra suspend setting \n", __FUNCTION__));

			power_mode = PM_FAST;
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM, (char *)&power_mode,
				sizeof(power_mode));

			/* disable pkt filter */
			dhd_set_packet_filter(0, dhd);

			/* restore pre-suspend setting for dtim_skip */
			bcm_mkiovar("bcn_li_dtim", (char *)&dhd->dtim_skip,
				4, iovbuf, sizeof(iovbuf));

			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#ifdef CUSTOMER_HW2
			roamvar = dhd_roam;
			bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#endif /* CUSTOMER_HW2 */
		}
	}

	return 0;
}

static void dhd_suspend_resume_helper(struct dhd_info *dhd, int val)
{
	dhd_pub_t *dhdp = &dhd->pub;

	dhd_os_wake_lock(dhdp);
	dhd_os_proto_block(dhdp);
	/* Set flag when early suspend was called */
	dhdp->in_suspend = val;
	if (!dhdp->suspend_disable_flag)
		dhd_set_suspend(val, dhdp);
	dhd_os_proto_unblock(dhdp);
	dhd_os_wake_unlock(dhdp);
}

static void dhd_early_suspend(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);

	DHD_TRACE(("%s: enter\n", __FUNCTION__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 1);
}

static void dhd_late_resume(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);

	DHD_TRACE(("%s: enter\n", __FUNCTION__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 0);
}
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) */

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
	tmo->tick = 1000000 / HZ;
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

	if (tmo->increment < tmo->tick) {
		OSL_DELAY(tmo->increment);
		tmo->increment *= 2;
		if (tmo->increment > tmo->tick)
			tmo->increment = tmo->tick;
	} else {
		wait_queue_head_t delay_wait;
		DECLARE_WAITQUEUE(wait, current);
		int pending;
		init_waitqueue_head(&delay_wait);
		add_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		pending = signal_pending(current);
		remove_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (pending)
			return 1;	/* Interrupted */
	}

	return 0;
}

static int
dhd_net2idx(dhd_info_t *dhd, struct net_device *net)
{
	int i = 0;

	ASSERT(dhd);
	while (i < DHD_MAX_IFS) {
		if (dhd->iflist[i] && (dhd->iflist[i]->net == net))
			return i;
		i++;
	}

	return DHD_BAD_IF;
}

int
dhd_ifname2idx(dhd_info_t *dhd, char *name)
{
	int i = DHD_MAX_IFS;

	ASSERT(dhd);

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0)
		if (dhd->iflist[i] && !strncmp(dhd->iflist[i]->name, name, IFNAMSIZ))
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

	ASSERT(dhd && dhd->iflist[ifidx]);
	dev = dhd->iflist[ifidx]->net;

	netif_addr_lock_bh(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	cnt = netdev_mc_count(dev);
#else
	cnt = dev->mc_count;
#endif
	netif_addr_unlock_bh(dev);

	/* Determine initial value of allmulti flag */
	allmulti = (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;

	/* Send down the multicast list first. */
	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETHER_ADDR_LEN);
	if (!(bufp = buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
		           dhd_ifname(&dhd->pub, ifidx), cnt));
		return;
	}

	strcpy(bufp, "mcast_list");
	bufp += strlen("mcast_list") + 1;

	cnt = htol32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);

	netif_addr_lock_bh(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	netdev_for_each_mc_addr(ha, dev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
		cnt--;
	}
#else
	for (mclist = dev->mc_list;(mclist && (cnt > 0)); cnt--, mclist = mclist->next) {
		memcpy(bufp, (void *)mclist->dmi_addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
	}
#endif
	netif_addr_unlock_bh(dev);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
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

	buflen = sizeof("allmulti") + sizeof(allmulti);
	if (!(buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for allmulti\n", dhd_ifname(&dhd->pub, ifidx)));
		return;
	}
	allmulti = htol32(allmulti);

	if (!bcm_mkiovar("allmulti", (void*)&allmulti, sizeof(allmulti), buf, buflen)) {
		DHD_ERROR(("%s: mkiovar failed for allmulti, datalen %d buflen %u\n",
		           dhd_ifname(&dhd->pub, ifidx), (int)sizeof(allmulti), buflen));
		MFREE(dhd->pub.osh, buf, buflen);
		return;
	}


	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set allmulti %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}

	MFREE(dhd->pub.osh, buf, buflen);

	/* Finally, pick up the PROMISC flag as well, like the NIC driver does */

	allmulti = (dev->flags & IFF_PROMISC) ? TRUE : FALSE;
	allmulti = htol32(allmulti);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_PROMISC;
	ioc.buf = &allmulti;
	ioc.len = sizeof(allmulti);
	ioc.set = TRUE;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set promisc %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}
}

static int
_dhd_set_mac_address(dhd_info_t *dhd, int ifidx, struct ether_addr *addr)
{
	char buf[32];
	wl_ioctl_t ioc;
	int ret;

	DHD_TRACE(("%s enter\n", __FUNCTION__));
	if (!bcm_mkiovar("cur_etheraddr", (char*)addr, ETHER_ADDR_LEN, buf, 32)) {
		DHD_ERROR(("%s: mkiovar failed for cur_etheraddr\n", dhd_ifname(&dhd->pub, ifidx)));
		return -1;
	}
	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = 32;
	ioc.set = TRUE;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set cur_etheraddr failed\n", dhd_ifname(&dhd->pub, ifidx)));
	} else {
		memcpy(dhd->iflist[ifidx]->net->dev_addr, addr, ETHER_ADDR_LEN);
	}

	return ret;
}

#ifdef SOFTAP
extern struct net_device *ap_net_dev;
/* semaphore that the soft AP CODE waits on */
extern struct semaphore ap_eth_sema;
#endif

static void
dhd_op_if(dhd_if_t *ifp)
{
	dhd_info_t *dhd;
	int ret = 0, err = 0;
#ifdef SOFTAP
	unsigned long flags;
#endif

	ASSERT(ifp && ifp->info && ifp->idx);	/* Virtual interfaces only */

	dhd = ifp->info;

	DHD_TRACE(("%s: idx %d, state %d\n", __FUNCTION__, ifp->idx, ifp->state));

	switch (ifp->state) {
	case WLC_E_IF_ADD:
		/*
		 * Delete the existing interface before overwriting it
		 * in case we missed the WLC_E_IF_DEL event.
		 */
		if (ifp->net != NULL) {
			DHD_ERROR(("%s: ERROR: netdev:%s already exists, try free & unregister \n",
			 __FUNCTION__, ifp->net->name));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			free_netdev(ifp->net);
		}
		/* Allocate etherdev, including space for private structure */
		if (!(ifp->net = alloc_etherdev(sizeof(dhd)))) {
			DHD_ERROR(("%s: OOM - alloc_etherdev\n", __FUNCTION__));
			ret = -ENOMEM;
		}
		if (ret == 0) {
			strcpy(ifp->net->name, ifp->name);
			memcpy(netdev_priv(ifp->net), &dhd, sizeof(dhd));
			if ((err = dhd_net_attach(&dhd->pub, ifp->idx)) != 0) {
				DHD_ERROR(("%s: dhd_net_attach failed, err %d\n",
					__FUNCTION__, err));
				ret = -EOPNOTSUPP;
			} else {
#ifdef SOFTAP
				flags = dhd_os_spin_lock(&dhd->pub);
				/* save ptr to wl0.1 netdev for use in wl_iw.c  */
				ap_net_dev = ifp->net;
				 /* signal to the SOFTAP 'sleeper' thread, wl0.1 is ready */
				up(&ap_eth_sema);
				dhd_os_spin_unlock(&dhd->pub, flags);
#endif
				DHD_TRACE(("\n ==== pid:%x, net_device for if:%s created ===\n\n",
					current->pid, ifp->net->name));
				ifp->state = 0;
			}
		}
		break;
	case WLC_E_IF_DEL:
		if (ifp->net != NULL) {
			DHD_TRACE(("\n%s: got 'WLC_E_IF_DEL' state\n", __FUNCTION__));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			ret = DHD_DEL_IF;	/* Make sure the free_netdev() is called */
		}
		break;
	default:
		DHD_ERROR(("%s: bad op %d\n", __FUNCTION__, ifp->state));
		ASSERT(!ifp->state);
		break;
	}

	if (ret < 0) {
		if (ifp->net) {
			free_netdev(ifp->net);
		}
		dhd->iflist[ifp->idx] = NULL;
		MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
#ifdef SOFTAP
		flags = dhd_os_spin_lock(&dhd->pub);
		if (ifp->net == ap_net_dev)
			ap_net_dev = NULL;     /* NULL SOFTAP global as well */
		dhd_os_spin_unlock(&dhd->pub, flags);
#endif /*  SOFTAP */
	}
}

static int
_dhd_sysioc_thread(void *data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;
	int i;
#ifdef SOFTAP
	bool in_ap = FALSE;
	unsigned long flags;
#endif

	DAEMONIZE("dhd_sysioc");

	while (down_interruptible(&dhd->sysioc_sem) == 0) {
		dhd_os_start_lock(&dhd->pub);
		dhd_os_wake_lock(&dhd->pub);
		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				DHD_TRACE(("%s: interface %d\n",__FUNCTION__, i));
#ifdef SOFTAP
				flags = dhd_os_spin_lock(&dhd->pub);
				in_ap = (ap_net_dev != NULL);
				dhd_os_spin_unlock(&dhd->pub, flags);
#endif /* SOFTAP */
				if (dhd->iflist[i]->state)
					dhd_op_if(dhd->iflist[i]);
#ifdef SOFTAP
				if (dhd->iflist[i] == NULL) {
					DHD_TRACE(("%s: interface %d just been removed!\n\n", __FUNCTION__, i));
					continue;
				}

				if (in_ap && dhd->set_macaddress) {
					DHD_TRACE(("attempt to set MAC for %s in AP Mode blocked.\n", dhd->iflist[i]->net->name));
					dhd->set_macaddress = FALSE;
					continue;
				}

				if (in_ap && dhd->set_multicast)  {
					DHD_TRACE(("attempt to set MULTICAST list for %s in AP Mode blocked.\n", dhd->iflist[i]->net->name));
					dhd->set_multicast = FALSE;
					continue;
				}
#endif /* SOFTAP */
				if (dhd->set_multicast) {
					dhd->set_multicast = FALSE;
					_dhd_set_multicast_list(dhd, i);
				}
				if (dhd->set_macaddress) {
					dhd->set_macaddress = FALSE;
					_dhd_set_mac_address(dhd, i, &dhd->macvalue);
				}
			}
		}
		dhd_os_wake_unlock(&dhd->pub);
		dhd_os_start_unlock(&dhd->pub);
	}
	DHD_TRACE(("%s: stopped\n",__FUNCTION__));
	complete_and_exit(&dhd->sysioc_exited, 0);
}

static int
dhd_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;

	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;

	DHD_TRACE(("%s: Enter\n",__FUNCTION__));
	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return -1;

	ASSERT(dhd->sysioc_pid >= 0);
	memcpy(&dhd->macvalue, sa->sa_data, ETHER_ADDR_LEN);
	dhd->set_macaddress = TRUE;
	up(&dhd->sysioc_sem);

	return ret;
}

static void
dhd_set_multicast_list(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ifidx;

	DHD_TRACE(("%s: Enter\n",__FUNCTION__));
	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return;

	ASSERT(dhd->sysioc_pid >= 0);
	dhd->set_multicast = TRUE;
	up(&dhd->sysioc_sem);
}

int
dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);

	/* Reject if down */
	if (!dhdp->up || (dhdp->busstate == DHD_BUS_DOWN)) {
		return -ENODEV;
	}

	/* Update multicast statistic */
	if (PKTLEN(dhdp->osh, pktbuf) >= ETHER_ADDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhdp->osh, pktbuf);
		struct ether_header *eh = (struct ether_header *)pktdata;

		if (ETHER_ISMULTI(eh->ether_dhost))
			dhdp->tx_multicast++;
		if (ntoh16(eh->ether_type) == ETHER_TYPE_802_1X)
			atomic_inc(&dhd->pend_8021x_cnt);
	}

	/* Look into the packet and update the packet priority */
	if ((PKTPRIO(pktbuf) == 0))
		pktsetprio(pktbuf, FALSE);

	/* If the protocol uses a data header, apply it */
	dhd_prot_hdrpush(dhdp, ifidx, pktbuf);

	/* Use bus module to send data frame */
#ifdef BCMDBUS
	ret = dbus_send_pkt(dhdp->dbus, pktbuf, NULL /* pktinfo */);
#else
	ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif /* BCMDBUS */

	return ret;
}

static int
dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	void *pktbuf;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_os_wake_lock(&dhd->pub);

	/* Reject if down */
	if (!dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN)) {
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d\n",
			 __FUNCTION__, dhd->pub.up, dhd->pub.busstate));
		netif_stop_queue(net);
		/* Send Event when bus down detected during data session */
		if (dhd->pub.busstate == DHD_BUS_DOWN)  {
			DHD_ERROR(("%s: Event HANG send up\n", __FUNCTION__));
			net_os_send_hang_message(net);
		}
		dhd_os_wake_unlock(&dhd->pub);
		return -ENODEV;
	}

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx %d\n", __FUNCTION__, ifidx));
		netif_stop_queue(net);
		dhd_os_wake_unlock(&dhd->pub);
		return -ENODEV;
	}

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < dhd->pub.hdrlen) {
		struct sk_buff *skb2;

		DHD_INFO(("%s: insufficient headroom\n",
		          dhd_ifname(&dhd->pub, ifidx)));
		dhd->pub.tx_realloc++;
		skb2 = skb_realloc_headroom(skb, dhd->pub.hdrlen);
		dev_kfree_skb(skb);
		if ((skb = skb2) == NULL) {
			DHD_ERROR(("%s: skb_realloc_headroom failed\n",
			           dhd_ifname(&dhd->pub, ifidx)));
			ret = -ENOMEM;
			goto done;
		}
	}

	/* Convert to packet */
	if (!(pktbuf = PKTFRMNATIVE(dhd->pub.osh, skb))) {
		DHD_ERROR(("%s: PKTFRMNATIVE failed\n",
		           dhd_ifname(&dhd->pub, ifidx)));
		dev_kfree_skb_any(skb);
		ret = -ENOMEM;
		goto done;
	}

	ret = dhd_sendpkt(&dhd->pub, ifidx, pktbuf);

done:
	if (ret)
		dhd->pub.dstats.tx_dropped++;
	else
		dhd->pub.tx_packets++;

	dhd_os_wake_unlock(&dhd->pub);

	/* Return ok: we always eat the packet */
	return 0;
}

void
dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool state)
{
	struct net_device *net;
	dhd_info_t *dhd = dhdp->info;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhdp->txoff = state;
	ASSERT(dhd && dhd->iflist[ifidx]);
	net = dhd->iflist[ifidx]->net;
	if (state == ON)
		netif_stop_queue(net);
	else
		netif_wake_queue(net);
}

void
dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *pktbuf, int numpkt)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	uchar *eth;
	uint len;
	void * data, *pnext, *save_pktbuf;
	int i;
	dhd_if_t *ifp;
	wl_event_msg_t event;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	save_pktbuf = pktbuf;

	for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {

		pnext = PKTNEXT(dhdp->osh, pktbuf);
		PKTSETNEXT(wl->sh.osh, pktbuf, NULL);


		skb = PKTTONATIVE(dhdp->osh, pktbuf);

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

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL)
			ifp = dhd->iflist[0];

		ASSERT(ifp);
		skb->dev = ifp->net;
		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST) {
			dhd->pub.rx_multicast++;
		}

		skb->data = eth;
		skb->len = len;

		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* Process special event packets and then discard them */
		if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM)
			dhd_wl_host_event(dhd, &ifidx,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
			skb->mac_header,
#else
			skb->mac.raw,
#endif
			&event,
			&data);

		ASSERT(ifidx < DHD_MAX_IFS && dhd->iflist[ifidx]);
		if (dhd->iflist[ifidx] && !dhd->iflist[ifidx]->state)
			ifp = dhd->iflist[ifidx];

		if (ifp->net)
			ifp->net->last_rx = jiffies;

		dhdp->dstats.rx_bytes += skb->len;
		dhdp->rx_packets++; /* Local count */

		if (in_interrupt()) {
			netif_rx(skb);
		} else {
			/* If the receive is not processed inside an ISR,
			 * the softirqd must be woken explicitly to service
			 * the NET_RX_SOFTIRQ.  In 2.6 kernels, this is handled
			 * by netif_rx_ni(), but in earlier kernels, we need
			 * to do it manually.
			 */
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
	dhd_os_wake_lock_timeout_enable(dhdp);
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
	uint ifidx;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh;
	uint16 type;

	dhd_prot_hdrpull(dhdp, &ifidx, txp);

	eh = (struct ether_header *)PKTDATA(dhdp->osh, txp);
	type  = ntoh16(eh->ether_type);

	if (type == ETHER_TYPE_802_1X)
		atomic_dec(&dhd->pend_8021x_cnt);

}

static struct net_device_stats *
dhd_get_stats(struct net_device *net)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	dhd_if_t *ifp;
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF)
		return NULL;

	ifp = dhd->iflist[ifidx];
	ASSERT(dhd && ifp);

	if (dhd->pub.up) {
		/* Use the protocol to get dongle stats */
		dhd_prot_dstats(&dhd->pub);
	}

	/* Copy dongle stats to net device stats */
	ifp->stats.rx_packets = dhd->pub.dstats.rx_packets;
	ifp->stats.tx_packets = dhd->pub.dstats.tx_packets;
	ifp->stats.rx_bytes = dhd->pub.dstats.rx_bytes;
	ifp->stats.tx_bytes = dhd->pub.dstats.tx_bytes;
	ifp->stats.rx_errors = dhd->pub.dstats.rx_errors;
	ifp->stats.tx_errors = dhd->pub.dstats.tx_errors;
	ifp->stats.rx_dropped = dhd->pub.dstats.rx_dropped;
	ifp->stats.tx_dropped = dhd->pub.dstats.tx_dropped;
	ifp->stats.multicast = dhd->pub.dstats.multicast;

	return &ifp->stats;
}

static int
dhd_watchdog_thread(void *data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
#ifdef DHD_SCHED
	if (dhd_watchdog_prio > 0) {
		struct sched_param param;
		param.sched_priority = (dhd_watchdog_prio < MAX_RT_PRIO)?
			dhd_watchdog_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}
#endif /* DHD_SCHED */

	DAEMONIZE("dhd_watchdog");

	/* Run until signal received */
	while (1) {
		if (down_interruptible (&dhd->watchdog_sem) == 0) {

			if (dhd->pub.dongle_reset == FALSE) {
				/* Call the bus module watchdog */
				dhd_bus_watchdog(&dhd->pub);
			}
			/* Count the tick for reference */
			dhd->pub.tickcnt++;

			/* Reschedule the watchdog */
			if (dhd->wd_timer_valid)
				mod_timer(&dhd->timer, jiffies + dhd_watchdog_ms * HZ / 1000);

			dhd_os_wake_unlock(&dhd->pub);
		}
		else
			break;
	}

	complete_and_exit(&dhd->watchdog_exited, 0);
}

static void
dhd_watchdog(ulong data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;

	dhd_os_wake_lock(&dhd->pub);
	if (dhd->watchdog_pid >= 0) {
		up(&dhd->watchdog_sem);
		return;
	}

	/* Call the bus module watchdog */
	dhd_bus_watchdog(&dhd->pub);

	/* Count the tick for reference */
	dhd->pub.tickcnt++;

	/* Reschedule the watchdog */
	if (dhd->wd_timer_valid)
		mod_timer(&dhd->timer, jiffies + dhd_watchdog_ms * HZ / 1000);
	dhd_os_wake_unlock(&dhd->pub);
}

static int
dhd_dpc_thread(void *data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
#ifdef DHD_SCHED
	if (dhd_dpc_prio > 0)
	{
		struct sched_param param;
		param.sched_priority = (dhd_dpc_prio < MAX_RT_PRIO)?dhd_dpc_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}
#endif /* DHD_SCHED */

	DAEMONIZE("dhd_dpc");

	/* Run until signal received */
	while (1) {
		if (down_interruptible(&dhd->dpc_sem) == 0) {
			/* Call bus dpc unless it indicated down (then clean stop) */
			if (dhd->pub.busstate != DHD_BUS_DOWN) {
				if (dhd_bus_dpc(dhd->pub.bus)) {
					up(&dhd->dpc_sem);
				}
				else {
					dhd_os_wake_unlock(&dhd->pub);
				}
			} else {
				dhd_bus_stop(dhd->pub.bus, TRUE);
				dhd_os_wake_unlock(&dhd->pub);
			}
		}
		else
			break;
	}

	complete_and_exit(&dhd->dpc_exited, 0);
}

static void
dhd_dpc(ulong data)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)data;

	/* Call bus dpc unless it indicated down (then clean stop) */
	if (dhd->pub.busstate != DHD_BUS_DOWN) {
		if (dhd_bus_dpc(dhd->pub.bus))
			tasklet_schedule(&dhd->tasklet);
	} else {
		dhd_bus_stop(dhd->pub.bus, TRUE);
	}
}

void
dhd_sched_dpc(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	dhd_os_wake_lock(dhdp);
	if (dhd->dpc_pid >= 0) {
		up(&dhd->dpc_sem);
		return;
	}

	tasklet_schedule(&dhd->tasklet);
}

#ifdef TOE
/* Retrieve current toe component enables, which are kept as a bitmap in toe_ol iovar */
static int
dhd_toe_get(dhd_info_t *dhd, int ifidx, uint32 *toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = FALSE;

	strcpy(buf, "toe_ol");
	if ((ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		/* Check for older dongle image that doesn't support toe_ol */
		if (ret == -EIO) {
			DHD_ERROR(("%s: toe not supported by device\n",
				dhd_ifname(&dhd->pub, ifidx)));
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
	wl_ioctl_t ioc;
	char buf[32];
	int toe, ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = TRUE;

	/* Set toe_ol as requested */

	strcpy(buf, "toe_ol");
	memcpy(&buf[sizeof("toe_ol")], &toe_ol, sizeof(uint32));

	if ((ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		DHD_ERROR(("%s: could not set toe_ol: ret=%d\n",
			dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	/* Enable toe globally only if any components are enabled. */

	toe = (toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe, sizeof(uint32));

	if ((ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		DHD_ERROR(("%s: could not set toe: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	return 0;
}
#endif /* TOE */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void dhd_ethtool_get_drvinfo(struct net_device *net,
                                    struct ethtool_drvinfo *info)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);

	sprintf(info->driver, "wl");
	sprintf(info->version, "%lu", dhd->pub.drv_version);
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
		strncpy(drvname, info.driver, sizeof(info.driver));
		drvname[sizeof(info.driver)-1] = '\0';

		/* clear struct for return */
		memset(&info, 0, sizeof(info));
		info.cmd = cmd;

		/* if dhd requested, identify ourselves */
		if (strcmp(drvname, "?dhd") == 0) {
			sprintf(info.driver, "dhd");
			strcpy(info.version, EPI_VERSION_STR);
		}

		/* otherwise, require dongle to be up */
		else if (!dhd->pub.up) {
			DHD_ERROR(("%s: dongle is not up\n", __FUNCTION__));
			return -ENODEV;
		}

		/* finally, report dongle driver type */
		else if (dhd->pub.iswl)
			sprintf(info.driver, "wl");
		else
			sprintf(info.driver, "xx");

		sprintf(info.version, "%lu", dhd->pub.drv_version);
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

static int
dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr, int cmd)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int buflen = 0;
	void *buf = NULL;
	uint driver = 0;
	int ifidx;
	bool is_set_key_cmd;
	int ret;

	dhd_os_wake_lock(&dhd->pub);

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __FUNCTION__, ifidx, cmd));

	if (ifidx == DHD_BAD_IF) {
		dhd_os_wake_unlock(&dhd->pub);
		return -1;
	}

#if defined(CONFIG_WIRELESS_EXT)
	/* linux wireless extensions */
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		/* may recurse, do NOT lock */
		ret = wl_iw_ioctl(net, ifr, cmd);
		dhd_os_wake_unlock(&dhd->pub);
		return ret;
	}
#endif /* defined(CONFIG_WIRELESS_EXT) */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
	if (cmd == SIOCETHTOOL) {
		ret = dhd_ethtool(dhd, (void*)ifr->ifr_data);
		dhd_os_wake_unlock(&dhd->pub);
		return ret;
	}
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2) */

	if (cmd != SIOCDEVPRIVATE) {
		dhd_os_wake_unlock(&dhd->pub);
		return -EOPNOTSUPP;
	}

	memset(&ioc, 0, sizeof(ioc));

	/* Copy the ioc control structure part of ioctl request */
	if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
		bcmerror = -BCME_BADADDR;
		goto done;
	}

	/* Copy out any buffer passed */
	if (ioc.buf) {
		buflen = MIN(ioc.len, DHD_IOCTL_MAXLEN);
		/* optimization for direct ioctl calls from kernel */
		/*
		if (segment_eq(get_fs(), KERNEL_DS)) {
			buf = ioc.buf;
		} else {
		*/
		{
			if (!(buf = (char*)MALLOC(dhd->pub.osh, buflen))) {
				bcmerror = -BCME_NOMEM;
				goto done;
			}
			if (copy_from_user(buf, ioc.buf, buflen)) {
				bcmerror = -BCME_BADADDR;
				goto done;
			}
		}
	}

	/* To differentiate between wl and dhd read 4 more byes */
	if ((copy_from_user(&driver, (char *)ifr->ifr_data + sizeof(wl_ioctl_t),
		sizeof(uint)) != 0)) {
		bcmerror = -BCME_BADADDR;
		goto done;
	}

	if (!capable(CAP_NET_ADMIN)) {
		bcmerror = -BCME_EPERM;
		goto done;
	}

	/* check for local dhd ioctl and handle it */
	if (driver == DHD_IOCTL_MAGIC) {
		bcmerror = dhd_ioctl((void *)&dhd->pub, &ioc, buf, buflen);
		if (bcmerror)
			dhd->pub.bcmerror = bcmerror;
		goto done;
	}

	/* send to dongle (must be up, and wl) */
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		DHD_ERROR(("%s DONGLE_DOWN\n", __FUNCTION__));
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	if (!dhd->pub.iswl) {
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	/* Intercept WLC_SET_KEY IOCTL - serialize M4 send and set key IOCTL to
	 * prevent M4 encryption.
	 */
	is_set_key_cmd = ((ioc.cmd == WLC_SET_KEY) ||
	                 ((ioc.cmd == WLC_SET_VAR) &&
	                        !(strncmp("wsec_key", ioc.buf, 9))) ||
	                 ((ioc.cmd == WLC_SET_VAR) &&
	                        !(strncmp("bsscfg:wsec_key", ioc.buf, 15))));
	if (is_set_key_cmd) {
		dhd_wait_pend8021x(net);
	}

	bcmerror = dhd_prot_ioctl(&dhd->pub, ifidx, (wl_ioctl_t *)&ioc, buf, buflen);

done:
	if ((bcmerror == -ETIMEDOUT) || ((dhd->pub.busstate == DHD_BUS_DOWN) &&
			(!dhd->pub.dongle_reset))) {
		DHD_ERROR(("%s: Event HANG send up\n", __FUNCTION__));
		net_os_send_hang_message(net);
	}

	if (!bcmerror && buf && ioc.buf) {
		if (copy_to_user(ioc.buf, buf, buflen))
			bcmerror = -EFAULT;
	}

	if (buf)
		MFREE(dhd->pub.osh, buf, buflen);

	dhd_os_wake_unlock(&dhd->pub);

	return OSL_ERROR(bcmerror);
}

static int
dhd_stop(struct net_device *net)
{
#if !defined(IGNORE_ETH0_DOWN)
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	if (dhd->pub.up == 0) {
		return 0;
	}

	/* Set state and stop OS transmissions */
	dhd->pub.up = 0;
	netif_stop_queue(net);
#else
	DHD_ERROR(("BYPASS %s:due to BRCM compilation : under investigation ...\n", __FUNCTION__));
#endif /* !defined(IGNORE_ETH0_DOWN) */

	OLD_MOD_DEC_USE_COUNT;
	return 0;
}

static int
dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
#ifdef TOE
	uint32 toe_ol;
#endif
	int ifidx;

	/*  Force start if ifconfig_up gets called before START command */
	wl_control_wl_start(net);

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	if ((dhd->iflist[ifidx]) && (dhd->iflist[ifidx]->state == WLC_E_IF_DEL)) {
		DHD_ERROR(("%s: Error: called when IF already deleted\n", __FUNCTION__));
		return -1;
	}

	if (ifidx == 0) { /* do it only for primary eth0 */

		atomic_set(&dhd->pend_8021x_cnt, 0);

	memcpy(net->dev_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);

#ifdef TOE
	/* Get current TOE mode from dongle */
	if (dhd_toe_get(dhd, ifidx, &toe_ol) >= 0 && (toe_ol & TOE_TX_CSUM_OL) != 0)
		dhd->iflist[ifidx]->net->features |= NETIF_F_IP_CSUM;
	else
		dhd->iflist[ifidx]->net->features &= ~NETIF_F_IP_CSUM;
#endif
	}
	/* Allow transmit calls */
	netif_start_queue(net);
	dhd->pub.up = 1;

	OLD_MOD_INC_USE_COUNT;
	return 0;
}

osl_t *
dhd_osl_attach(void *pdev, uint bustype)
{
	return osl_attach(pdev, bustype, TRUE);
}

void
dhd_osl_detach(osl_t *osh)
{
	if (MALLOCED(osh)) {
		DHD_ERROR(("%s: MEMORY LEAK %d bytes\n", __FUNCTION__, MALLOCED(osh)));
	}
	osl_detach(osh);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && 1
	up(&dhd_registration_sem);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
}

int
dhd_add_if(dhd_info_t *dhd, int ifidx, void *handle, char *name,
	uint8 *mac_addr, uint32 flags, uint8 bssidx)
{
	dhd_if_t *ifp;

	DHD_TRACE(("%s: idx %d, handle->%p\n", __FUNCTION__, ifidx, handle));

	ASSERT(dhd && (ifidx < DHD_MAX_IFS));

	ifp = dhd->iflist[ifidx];
	if (!ifp && !(ifp = MALLOC(dhd->pub.osh, sizeof(dhd_if_t)))) {
		DHD_ERROR(("%s: OOM - dhd_if_t\n", __FUNCTION__));
		return -ENOMEM;
	}

	memset(ifp, 0, sizeof(dhd_if_t));
	ifp->info = dhd;
	dhd->iflist[ifidx] = ifp;
	strncpy(ifp->name, name, IFNAMSIZ);
	ifp->name[IFNAMSIZ] = '\0';
	if (mac_addr != NULL)
		memcpy(&ifp->mac_addr, mac_addr, ETHER_ADDR_LEN);

	if (handle == NULL) {
		ifp->state = WLC_E_IF_ADD;
		ifp->idx = ifidx;
		ASSERT(dhd->sysioc_pid >= 0);
		up(&dhd->sysioc_sem);
	} else
		ifp->net = (struct net_device *)handle;

	return 0;
}

void
dhd_del_if(dhd_info_t *dhd, int ifidx)
{
	dhd_if_t *ifp;

	DHD_TRACE(("%s: idx %d\n", __FUNCTION__, ifidx));

	ASSERT(dhd && ifidx && (ifidx < DHD_MAX_IFS));
	ifp = dhd->iflist[ifidx];
	if (!ifp) {
		DHD_ERROR(("%s: Null interface\n", __FUNCTION__));
		return;
	}

	ifp->state = WLC_E_IF_DEL;
	ifp->idx = ifidx;
	ASSERT(dhd->sysioc_pid >= 0);
	up(&dhd->sysioc_sem);
}

dhd_pub_t *
dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen)
{
	dhd_info_t *dhd = NULL;
	struct net_device *net;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	/* updates firmware nvram path if it was provided as module paramters */
	if ((firmware_path != NULL) && (firmware_path[0] != '\0'))
		strcpy(fw_path, firmware_path);
	if ((nvram_path != NULL) && (nvram_path[0] != '\0'))
		strcpy(nv_path, nvram_path);

	/* Allocate etherdev, including space for private structure */
	if (!(net = alloc_etherdev(sizeof(dhd)))) {
		DHD_ERROR(("%s: OOM - alloc_etherdev\n", __FUNCTION__));
		goto fail;
	}

	/* Allocate primary dhd_info */
	if (!(dhd = MALLOC(osh, sizeof(dhd_info_t)))) {
		DHD_ERROR(("%s: OOM - alloc dhd_info\n", __FUNCTION__));
		goto fail;
	}

	memset(dhd, 0, sizeof(dhd_info_t));

	/*
	 * Save the dhd_info into the priv
	 */
	memcpy(netdev_priv(net), &dhd, sizeof(dhd));
	dhd->pub.osh = osh;

	/* Set network interface name if it was provided as module parameter */
	if (iface_name[0]) {
		int len;
		char ch;
		strncpy(net->name, iface_name, IFNAMSIZ);
		net->name[IFNAMSIZ - 1] = 0;
		len = strlen(net->name);
		ch = net->name[len - 1];
		if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
			strcat(net->name, "%d");
	}

	if (dhd_add_if(dhd, 0, (void *)net, net->name, NULL, 0, 0) == DHD_BAD_IF)
		goto fail;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif

	init_MUTEX(&dhd->proto_sem);
	/* Initialize other structure content */
	init_waitqueue_head(&dhd->ioctl_resp_wait);
	init_waitqueue_head(&dhd->ctrl_wait);

	/* Initialize the spinlocks */
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);
	spin_lock_init(&dhd->dhd_lock);

	/* Initialize Wakelock stuff */
	spin_lock_init(&dhd->wl_lock);
	dhd->wl_count = 0;
	dhd->wl_packet = 0;
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&dhd->wl_wifi, WAKE_LOCK_SUSPEND, "wlan_wake");
	wake_lock_init(&dhd->wl_rxwake, WAKE_LOCK_SUSPEND, "wlan_rx_wake");
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_init(&dhd->wl_start_lock);
#endif
	/* Link to info module */
	dhd->pub.info = dhd;

	/* Link to bus module */
	dhd->pub.bus = bus;
	dhd->pub.hdrlen = bus_hdrlen;

	/* Attach and link in the protocol */
	if (dhd_prot_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_prot_attach failed\n"));
		goto fail;
	}
#if defined(CONFIG_WIRELESS_EXT)
	/* Attach and link in the iw */
	if (wl_iw_attach(net, (void *)&dhd->pub) != 0) {
		DHD_ERROR(("wl_iw_attach failed\n"));
		goto fail;
	}
#endif /* defined(CONFIG_WIRELESS_EXT) */

	/* Set up the watchdog timer */
	init_timer(&dhd->timer);
	dhd->timer.data = (ulong)dhd;
	dhd->timer.function = dhd_watchdog;

	/* Initialize thread based operation and lock */
	init_MUTEX(&dhd->sdsem);
	if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0)) {
		dhd->threads_only = TRUE;
	}
	else {
		dhd->threads_only = FALSE;
	}

	if (dhd_dpc_prio >= 0) {
		/* Initialize watchdog thread */
		sema_init(&dhd->watchdog_sem, 0);
		init_completion(&dhd->watchdog_exited);
		dhd->watchdog_pid = kernel_thread(dhd_watchdog_thread, dhd, 0);
	} else {
		dhd->watchdog_pid = -1;
	}

	/* Set up the bottom half handler */
	if (dhd_dpc_prio >= 0) {
		/* Initialize DPC thread */
		sema_init(&dhd->dpc_sem, 0);
		init_completion(&dhd->dpc_exited);
		dhd->dpc_pid = kernel_thread(dhd_dpc_thread, dhd, 0);
	} else {
		tasklet_init(&dhd->tasklet, dhd_dpc, (ulong)dhd);
		dhd->dpc_pid = -1;
	}

	if (dhd_sysioc) {
		sema_init(&dhd->sysioc_sem, 0);
		init_completion(&dhd->sysioc_exited);
		dhd->sysioc_pid = kernel_thread(_dhd_sysioc_thread, dhd, 0);
	} else {
		dhd->sysioc_pid = -1;
	}

	/*
	 * Save the dhd_info into the priv
	 */
	memcpy(netdev_priv(net), &dhd, sizeof(dhd));

#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	g_bus = bus;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
	register_pm_notifier(&dhd_sleep_pm_notifier);
#endif /*  (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP) */

#ifdef CONFIG_HAS_EARLYSUSPEND
	dhd->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 20;
	dhd->early_suspend.suspend = dhd_early_suspend;
	dhd->early_suspend.resume = dhd_late_resume;
	register_early_suspend(&dhd->early_suspend);
#endif

	return &dhd->pub;

fail:
	if (net)
		free_netdev(net);
	if (dhd)
		dhd_detach(&dhd->pub);

	return NULL;
}


int
dhd_bus_start(dhd_pub_t *dhdp)
{
	int ret = -1;
	dhd_info_t *dhd = (dhd_info_t*)dhdp->info;
#ifdef EMBEDDED_PLATFORM
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
#endif /* EMBEDDED_PLATFORM */

	ASSERT(dhd);

	DHD_TRACE(("%s: \n", __FUNCTION__));

	/* try to download image and nvram to the dongle */
	if  (dhd->pub.busstate == DHD_BUS_DOWN) {
		if (!(dhd_bus_download_firmware(dhd->pub.bus, dhd->pub.osh,
		                                fw_path, nv_path))) {
			DHD_ERROR(("%s: dhdsdio_probe_download failed. firmware = %s nvram = %s\n",
			           __FUNCTION__, fw_path, nv_path));
			return -1;
		}
	}

	/* Start the watchdog timer */
	dhd->pub.tickcnt = 0;
	dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);

	/* Bring up the bus */
	if ((ret = dhd_bus_init(&dhd->pub, TRUE)) != 0) {
		DHD_ERROR(("%s, dhd_bus_init failed %d\n", __FUNCTION__, ret));
		return ret;
	}
#if defined(OOB_INTR_ONLY)
	/* Host registration for OOB interrupt */
	if (bcmsdh_register_oob_intr(dhdp)) {
		del_timer_sync(&dhd->timer);
		dhd->wd_timer_valid = FALSE;
		DHD_ERROR(("%s Host failed to resgister for OOB\n", __FUNCTION__));
		return -ENODEV;
	}

	/* Enable oob at firmware */
	dhd_enable_oob_intr(dhd->pub.bus, TRUE);
#endif /* defined(OOB_INTR_ONLY) */

	/* If bus is not ready, can't come up */
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		del_timer_sync(&dhd->timer);
		dhd->wd_timer_valid = FALSE;
		DHD_ERROR(("%s failed bus is not ready\n", __FUNCTION__));
		return -ENODEV;
	}

#ifdef EMBEDDED_PLATFORM
	bcm_mkiovar("event_msgs", dhdp->eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	dhdcdc_query_ioctl(dhdp, 0, WLC_GET_VAR, iovbuf, sizeof(iovbuf));
	bcopy(iovbuf, dhdp->eventmask, WL_EVENTING_MASK_LEN);

	setbit(dhdp->eventmask, WLC_E_SET_SSID);
	setbit(dhdp->eventmask, WLC_E_PRUNE);
	setbit(dhdp->eventmask, WLC_E_AUTH);
	setbit(dhdp->eventmask, WLC_E_REASSOC);
	setbit(dhdp->eventmask, WLC_E_REASSOC_IND);
	setbit(dhdp->eventmask, WLC_E_DEAUTH_IND);
	setbit(dhdp->eventmask, WLC_E_DISASSOC_IND);
	setbit(dhdp->eventmask, WLC_E_DISASSOC);
	setbit(dhdp->eventmask, WLC_E_JOIN);
	setbit(dhdp->eventmask, WLC_E_ASSOC_IND);
	setbit(dhdp->eventmask, WLC_E_PSK_SUP);
	setbit(dhdp->eventmask, WLC_E_LINK);
	setbit(dhdp->eventmask, WLC_E_NDIS_LINK);
	setbit(dhdp->eventmask, WLC_E_MIC_ERROR);
	setbit(dhdp->eventmask, WLC_E_PMKID_CACHE);
	setbit(dhdp->eventmask, WLC_E_TXFAIL);
	setbit(dhdp->eventmask, WLC_E_JOIN_START);
	setbit(dhdp->eventmask, WLC_E_SCAN_COMPLETE);
	setbit(dhdp->eventmask, WLC_E_RELOAD);
#ifdef PNO_SUPPORT
	setbit(dhdp->eventmask, WLC_E_PFN_NET_FOUND);
#endif /* PNO_SUPPORT */

/* enable dongle roaming event */
	setbit(dhdp->eventmask, WLC_E_ROAM);

	dhdp->pktfilter_count = 1;
	/* Setup filter to allow only unicast */
	dhdp->pktfilter[0] = "100 0 0 0 0x01 0x00";
#endif /* EMBEDDED_PLATFORM */

	/* Bus is ready, do any protocol initialization */
	if ((ret = dhd_prot_init(&dhd->pub)) < 0)
		return ret;

	return 0;
}

int
dhd_iovar(dhd_pub_t *pub, int ifidx, char *name, char *cmd_buf, uint cmd_len, int set)
{
	char buf[strlen(name) + 1 + cmd_len];
	int len = sizeof(buf);
	wl_ioctl_t ioc;
	int ret;

	len = bcm_mkiovar(name, cmd_buf, cmd_len, buf, len);

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = set? WLC_SET_VAR : WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = set;

	ret = dhd_prot_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (!set && ret >= 0)
		memcpy(cmd_buf, buf, cmd_len);

	return ret;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 31))
static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_open,
	.ndo_stop = dhd_stop,
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
	.ndo_set_multicast_list = dhd_set_multicast_list,
};

static struct net_device_ops dhd_ops_virt = {
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
	.ndo_set_multicast_list = dhd_set_multicast_list,
};
#endif

int
dhd_net_attach(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct net_device *net;
	uint8 temp_addr[ETHER_ADDR_LEN] = { 0x00, 0x90, 0x4c, 0x11, 0x22, 0x33 };

	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	ASSERT(dhd && dhd->iflist[ifidx]);
	net = dhd->iflist[ifidx]->net;

	ASSERT(net);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
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
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
		net->open = dhd_open;
		net->stop = dhd_stop;
#else
		net->netdev_ops = &dhd_ops_pri;
#endif

	/*
	 * We have to use the primary MAC for virtual interfaces
	 */
	if (ifidx != 0) {
		/* for virtual interfaces use the primary MAC  */
		memcpy(temp_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
	}

	if (ifidx == 1) {
		DHD_TRACE(("%s ACCESS POINT MAC: \n", __FUNCTION__));
		/*  ACCESSPOINT INTERFACE CASE */
		temp_addr[0] |= 0x02;  /* set bit 2 , - Locally Administered address  */
	}
	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	net->ethtool_ops = &dhd_ethtool_ops;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

#if defined(CONFIG_WIRELESS_EXT)
#if WIRELESS_EXT < 19
	net->get_wireless_stats = dhd_get_wireless_stats;
#endif /* WIRELESS_EXT < 19 */
#if WIRELESS_EXT > 12
	net->wireless_handlers = (struct iw_handler_def *)&wl_iw_handler_def;
#endif /* WIRELESS_EXT > 12 */
#endif /* defined(CONFIG_WIRELESS_EXT) */

	dhd->pub.rxsz = net->mtu + net->hard_header_len + dhd->pub.hdrlen;

	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	if (register_netdev(net) != 0) {
		DHD_ERROR(("%s: couldn't register the net device\n", __FUNCTION__));
		goto fail;
	}

	printf("%s: Broadcom Dongle Host Driver mac=%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", net->name,
	       dhd->pub.mac.octet[0], dhd->pub.mac.octet[1], dhd->pub.mac.octet[2],
	       dhd->pub.mac.octet[3], dhd->pub.mac.octet[4], dhd->pub.mac.octet[5]);

#if defined(CONFIG_WIRELESS_EXT)
#ifdef SOFTAP
	if (ifidx == 0)
		/* Don't call for SOFTAP Interface in SOFTAP MODE */
		wl_iw_iscan_set_scan_broadcast_prep(net, 1);
#else
		wl_iw_iscan_set_scan_broadcast_prep(net, 1);
#endif /* SOFTAP */
#endif /* CONFIG_WIRELESS_EXT */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	up(&dhd_registration_sem);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
	return 0;

fail:
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif
	return BCME_ERROR;
}

void
dhd_bus_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		dhd = (dhd_info_t *)dhdp->info;
		if (dhd) {
			/* Stop the protocol module */
			dhd_prot_stop(&dhd->pub);

			/* Stop the bus module */
			dhd_bus_stop(dhd->pub.bus, TRUE);
#if defined(OOB_INTR_ONLY)
			bcmsdh_unregister_oob_intr();
#endif /* defined(OOB_INTR_ONLY) */

			/* Clear the watchdog timer */
			del_timer_sync(&dhd->timer);
			dhd->wd_timer_valid = FALSE;
		}
	}
}

void
dhd_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		dhd = (dhd_info_t *)dhdp->info;
		if (dhd) {
			dhd_if_t *ifp;
			int i;

#if defined(CONFIG_HAS_EARLYSUSPEND)
			if (dhd->early_suspend.suspend)
				unregister_early_suspend(&dhd->early_suspend);
#endif	/* defined(CONFIG_HAS_EARLYSUSPEND) */
#if defined(CONFIG_WIRELESS_EXT)
			/* Attach and link in the iw */
			wl_iw_detach();
#endif
			if (dhd->sysioc_pid >= 0) {
				KILL_PROC(dhd->sysioc_pid, SIGTERM);
				wait_for_completion(&dhd->sysioc_exited);
			}

			for (i = 1; i < DHD_MAX_IFS; i++)
				if (dhd->iflist[i]) {
					dhd->iflist[i]->state = WLC_E_IF_DEL;
					dhd->iflist[i]->idx = i;
					dhd_op_if(dhd->iflist[i]);
				}

			ifp = dhd->iflist[0];
			ASSERT(ifp);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
			if (ifp->net->open) {
#else
			if (ifp->net->netdev_ops == &dhd_ops_pri) {
#endif
				dhd_stop(ifp->net);
				unregister_netdev(ifp->net);
			}

			if (dhd->watchdog_pid >= 0)
			{
				KILL_PROC(dhd->watchdog_pid, SIGTERM);
				wait_for_completion(&dhd->watchdog_exited);
			}

			if (dhd->dpc_pid >= 0)
			{
				KILL_PROC(dhd->dpc_pid, SIGTERM);
				wait_for_completion(&dhd->dpc_exited);
			}
			else
				tasklet_kill(&dhd->tasklet);

			dhd_bus_detach(dhdp);

			if (dhdp->prot)
				dhd_prot_detach(dhdp);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
			unregister_pm_notifier(&dhd_sleep_pm_notifier);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP) */
			free_netdev(ifp->net);
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock_destroy(&dhd->wl_wifi);
			wake_lock_destroy(&dhd->wl_rxwake);
#endif
			MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
			MFREE(dhd->pub.osh, dhd, sizeof(*dhd));
		}
	}
}

static void __exit
dhd_module_cleanup(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_bus_unregister();
#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	wifi_del_dev();
#endif
	/* Call customer gpio to turn off power with WL_REG_ON signal */
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
}

static int __init
dhd_module_init(void)
{
	int error;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Sanity check on the module parameters */
	do {
		/* Both watchdog and DPC as tasklets are ok */
		if ((dhd_watchdog_prio < 0) && (dhd_dpc_prio < 0))
			break;

		/* If both watchdog and DPC are threads, TX must be deferred */
		if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0) && dhd_deferred_tx)
			break;

		DHD_ERROR(("Invalid module parameters.\n"));
		return -EINVAL;
	} while (0);

	/* Call customer gpio to turn on power with WL_REG_ON signal */
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_ON);

#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	sema_init(&wifi_control_sem, 0);

	error = wifi_add_dev();
	if (error) {
		DHD_ERROR(("%s: platform_driver_register failed\n", __FUNCTION__));
		goto fail_0;
	}

	/* Waiting callback after platform_driver_register is done or exit with error */
	if (down_timeout(&wifi_control_sem,  msecs_to_jiffies(5000)) != 0) {
		error = -EINVAL;
		DHD_ERROR(("%s: platform_driver_register timeout\n", __FUNCTION__));
		goto fail_1;
	}
#endif /* #if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	sema_init(&dhd_registration_sem, 0);
#endif 

	error = dhd_bus_register();

	if (!error)
		printf("\n%s\n", dhd_version);
	else {
		DHD_ERROR(("%s: sdio_register_driver failed\n", __FUNCTION__));
		goto fail_1;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	/*
	 * Wait till MMC sdio_register_driver callback called and made driver attach.
	 * It's needed to make sync up exit from dhd insmod  and
	 * Kernel MMC sdio device callback registration
	 */
	if (down_timeout(&dhd_registration_sem,  msecs_to_jiffies(DHD_REGISTRATION_TIMEOUT)) != 0) {
		error = -EINVAL;
		DHD_ERROR(("%s: sdio_register_driver timeout\n", __FUNCTION__));
		goto fail_2;
	}
#endif
	return error;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
fail_2:
	dhd_bus_unregister();
#endif
fail_1:
#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	wifi_del_dev();
fail_0:
#endif /* defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC) */

	/* Call customer gpio to turn off power with WL_REG_ON signal */
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);

	return error;
}

module_init(dhd_module_init);
module_exit(dhd_module_cleanup);

/*
 * OS specific functions required to implement DHD driver in OS independent way
 */
int
dhd_os_proto_block(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		down(&dhd->proto_sem);
		return 1;
	}

	return 0;
}

int
dhd_os_proto_unblock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		up(&dhd->proto_sem);
		return 1;
	}

	return 0;
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
dhd_os_ioctl_resp_wait(dhd_pub_t *pub, uint *condition, bool *pending)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	DECLARE_WAITQUEUE(wait, current);
	int timeout = dhd_ioctl_timeout_msec;

	/* Convert timeout in millsecond to jiffies */
	/* timeout = timeout * HZ / 1000; */
	timeout = msecs_to_jiffies(timeout);

	/* Wait until control frame is available */
	add_wait_queue(&dhd->ioctl_resp_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	smp_mb();
	while (!(*condition) && (!signal_pending(current) && timeout)) {
		timeout = schedule_timeout(timeout);
		smp_mb();
	}

	if (signal_pending(current))
		*pending = TRUE;

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dhd->ioctl_resp_wait, &wait);

	return timeout;
}

int
dhd_os_ioctl_resp_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (waitqueue_active(&dhd->ioctl_resp_wait)) {
		wake_up_interruptible(&dhd->ioctl_resp_wait);
	}

	return 0;
}

void
dhd_os_wd_timer(void *bus, uint wdtick)
{
	dhd_pub_t *pub = bus;
	static uint save_dhd_watchdog_ms = 0;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;

	/* don't start the wd until fw is loaded */
	if (pub->busstate == DHD_BUS_DOWN)
		return;

	/* Totally stop the timer */
	if (!wdtick && dhd->wd_timer_valid == TRUE) {
		del_timer_sync(&dhd->timer);
		dhd->wd_timer_valid = FALSE;
		save_dhd_watchdog_ms = wdtick;
		return;
	}

	if (wdtick) {
		dhd_watchdog_ms = (uint)wdtick;

		/* Re arm the timer, at last watchdog period */
		mod_timer(&dhd->timer, jiffies + dhd_watchdog_ms * HZ / 1000);

		dhd->wd_timer_valid = TRUE;
		save_dhd_watchdog_ms = wdtick;
	}
}

void *
dhd_os_open_image(char *filename)
{
	struct file *fp;

	fp = filp_open(filename, O_RDONLY, 0);
	/*
	 * 2.6.11 (FC4) supports filp_open() but later revs don't?
	 * Alternative:
	 * fp = open_namei(AT_FDCWD, filename, O_RD, 0);
	 * ???
	 */
	 if (IS_ERR(fp))
		 fp = NULL;

	 return fp;
}

int
dhd_os_get_image_block(char *buf, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rdlen;

	if (!image)
		return 0;

	rdlen = kernel_read(fp, fp->f_pos, buf, len);
	if (rdlen > 0)
		fp->f_pos += rdlen;

	return rdlen;
}

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

	if (dhd->threads_only)
		down(&dhd->sdsem);
	else
		spin_lock_bh(&dhd->sdlock);
}

void
dhd_os_sdunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd->threads_only)
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

void
dhd_os_sdtxlock(dhd_pub_t *pub)
{
	dhd_os_sdlock(pub);
}

void
dhd_os_sdtxunlock(dhd_pub_t *pub)
{
	dhd_os_sdunlock(pub);
}

#ifdef DHD_USE_STATIC_BUF
void * dhd_os_prealloc(int section, unsigned long size)
{
#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	void *alloc_ptr = NULL;
	if (wifi_control_data && wifi_control_data->mem_prealloc)
	{
		alloc_ptr = wifi_control_data->mem_prealloc(section, size);
		if (alloc_ptr)
		{
			DHD_INFO(("success alloc section %d\n", section));
			bzero(alloc_ptr, size);
			return alloc_ptr;
		}
	}

	DHD_ERROR(("can't alloc section %d\n", section));
	return 0;
#else
return MALLOC(0, size);
#endif /* #if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC) */
}
#endif /* DHD_USE_STATIC_BUF */
#if defined(CONFIG_WIRELESS_EXT)
struct iw_statistics *
dhd_get_wireless_stats(struct net_device *dev)
{
	int res = 0;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	res = wl_iw_get_wireless_stats(dev, &dhd->iw.wstats);

	if (res == 0)
		return &dhd->iw.wstats;
	else
		return NULL;
}
#endif /* defined(CONFIG_WIRELESS_EXT) */

static int
dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
	wl_event_msg_t *event, void **data)
{
	int bcmerror = 0;

	ASSERT(dhd != NULL);

	bcmerror = wl_host_event(dhd, ifidx, pktdata, event, data);
	if (bcmerror != BCME_OK)
		return (bcmerror);

#if defined(CONFIG_WIRELESS_EXT)
	ASSERT(dhd->iflist[*ifidx] != NULL);

	if (ntoh32(event->event_type) == WLC_E_IF) {
		DHD_INFO(("<0> interface:%d OP:%d don't pass to wext,"
			"net_device might not be created yet\n",
				*ifidx, ntoh32(event->event_type)));
		return bcmerror;
	}

	ASSERT(dhd->iflist[*ifidx]->net != NULL);

	if (dhd->iflist[*ifidx]->net)
		wl_iw_event(dhd->iflist[*ifidx]->net, event, *data);
#endif /* defined(CONFIG_WIRELESS_EXT) */

	return (bcmerror);
}

/* send up locally generated event */
void
dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data)
{
	switch (ntoh32(event->event_type)) {
	default:
		break;
	}
}

void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;
	dhd_os_sdunlock(dhd);
	wait_event_interruptible_timeout(dhdinfo->ctrl_wait, (*lockvar == FALSE), HZ * 2);
	dhd_os_sdlock(dhd);
#endif
	return;
}

void dhd_wait_event_wakeup(dhd_pub_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;
	if (waitqueue_active(&dhdinfo->ctrl_wait))
		wake_up_interruptible(&dhdinfo->ctrl_wait);
#endif
	return;
}

int
dhd_dev_reset(struct net_device *dev, uint8 flag)
{
	int ret;

	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	/* Turning off watchdog */
	if (flag)
		dhd_os_wd_timer(&dhd->pub, 0);

	ret = dhd_bus_devreset(&dhd->pub, flag);
	if (ret) {
		DHD_ERROR(("%s: dhd_bus_devreset: %d\n", __FUNCTION__, ret));
		return ret;
	}

	/* Turning on watchdog back */
	if (!flag)
		dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);
	DHD_ERROR(("%s: WLAN OFF DONE\n", __FUNCTION__));

	return ret;
}

int net_os_set_suspend_disable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd) {
		ret = dhd->pub.suspend_disable_flag;
		dhd->pub.suspend_disable_flag = val;
	}
	return ret;
}

int net_os_set_suspend(struct net_device *dev, int val)
{
	int ret = 0;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd) {
		dhd_os_proto_block(&dhd->pub);
		ret = dhd_set_suspend(val, &dhd->pub);
		dhd_os_proto_unblock(&dhd->pub);
	}
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) */
	return ret;
}

int net_os_set_dtim_skip(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd)
		dhd->pub.dtim_skip = val;

	return 0;
}

int net_os_set_packet_filter(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	/* Packet filtering is set only if we still in early-suspend and
	 * we need either to turn it ON or turn it OFF
	 * We can always turn it OFF in case of early-suspend, but we turn it
	 * back ON only if suspend_disable_flag was not set
	*/
	if (dhd && dhd->pub.up) {
		dhd_os_proto_block(&dhd->pub);
		if (dhd->pub.in_suspend) {
			if (!val || (val && !dhd->pub.suspend_disable_flag))
				dhd_set_packet_filter(val, &dhd->pub);
		}
		dhd_os_proto_unblock(&dhd->pub);
	}
	return ret;
}


void
dhd_dev_init_ioctl(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	dhd_preinit_ioctls(&dhd->pub);
}

#ifdef PNO_SUPPORT
/* Linux wrapper to call common dhd_pno_clean */
int
dhd_dev_pno_reset(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_clean(&dhd->pub));
}


/* Linux wrapper to call common dhd_pno_enable */
int
dhd_dev_pno_enable(struct net_device *dev,  int pfn_enabled)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_enable(&dhd->pub, pfn_enabled));
}


/* Linux wrapper to call common dhd_pno_set */
int
dhd_dev_pno_set(struct net_device *dev, wlc_ssid_t* ssids_local, int nssid, ushort  scan_fr)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_set(&dhd->pub, ssids_local, nssid, scan_fr));
}

/* Linux wrapper to get  pno status */
int
dhd_dev_get_pno_status(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_get_status(&dhd->pub));
}

#endif /* PNO_SUPPORT */

int net_os_send_hang_message(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd) {
		if (!dhd->hang_was_sent) {
			dhd->hang_was_sent = 1;
			ret = wl_iw_send_priv_event(dev, "HANG");
		}
	}
	return ret;
}

void dhd_bus_country_set(struct net_device *dev, char *country_code)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd && dhd->pub.up)
		strncpy(dhd->pub.country_code, country_code, WLC_CNTRY_BUF_SZ);
}

void dhd_os_start_lock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd)
		mutex_lock(&dhd->wl_start_lock);
#endif
}

void dhd_os_start_unlock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd)
		mutex_unlock(&dhd->wl_start_lock);
#endif
}

static int
dhd_get_pend_8021x_cnt(dhd_info_t *dhd)
{
	return (atomic_read(&dhd->pend_8021x_cnt));
}

#define MAX_WAIT_FOR_8021X_TX	10

int
dhd_wait_pend8021x(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int timeout = 10 * HZ / 1000;
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
	return pend;
}

#ifdef DHD_DEBUG
int
write_to_file(dhd_pub_t *dhd, uint8 *buf, int size)
{
	int ret = 0;
	struct file *fp;
	mm_segment_t old_fs;
	loff_t pos = 0;

	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* open file to write */
	fp = filp_open("/tmp/mem_dump", O_WRONLY|O_CREAT, 0640);
	if (!fp) {
		printf("%s: open file error\n", __FUNCTION__);
		ret = -1;
		goto exit;
	}

	/* Write buf to file */
	fp->f_op->write(fp, buf, size, &pos);

exit:
	/* free buf before return */
	MFREE(dhd->osh, buf, size);
	/* close file before return */
	if (fp)
		filp_close(fp, current->files);
	/* restore previous address limit */
	set_fs(old_fs);

	return ret;
}
#endif /* DHD_DEBUG */

int dhd_os_wake_lock_timeout(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wl_lock, flags);
		ret = dhd->wl_packet;
#ifdef CONFIG_HAS_WAKELOCK
		if (dhd->wl_packet)
			wake_lock_timeout(&dhd->wl_rxwake, HZ);
#endif
		dhd->wl_packet = 0;
		spin_unlock_irqrestore(&dhd->wl_lock, flags);
	}
	/* printk("%s: %d\n", __FUNCTION__, ret); */
	return ret;
}

int net_os_wake_lock_timeout(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_timeout(&dhd->pub);
	return ret;
}

int dhd_os_wake_lock_timeout_enable(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd) {
		spin_lock_irqsave(&dhd->wl_lock, flags);
		dhd->wl_packet = 1;
		spin_unlock_irqrestore(&dhd->wl_lock, flags);
	}
	/* printk("%s\n",__func__); */
	return 0;
}

int net_os_wake_lock_timeout_enable(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_timeout_enable(&dhd->pub);
	return ret;
}

int dhd_os_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wl_lock, flags);
#ifdef CONFIG_HAS_WAKELOCK
		if (!dhd->wl_count)
			wake_lock(&dhd->wl_wifi);
#endif
		dhd->wl_count++;
		ret = dhd->wl_count;
		spin_unlock_irqrestore(&dhd->wl_lock, flags);
	}
	/* printk("%s: %d\n", __FUNCTION__, ret); */
	return ret;
}

int net_os_wake_lock(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
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
	if (dhd) {
		spin_lock_irqsave(&dhd->wl_lock, flags);
		if (dhd->wl_count) {
			dhd->wl_count--;
#ifdef CONFIG_HAS_WAKELOCK
			if (!dhd->wl_count)
				wake_unlock(&dhd->wl_wifi);
#endif
			ret = dhd->wl_count;
		}
		spin_unlock_irqrestore(&dhd->wl_lock, flags);
	}
	/* printk("%s: %d\n", __FUNCTION__, ret); */
	return ret;
}

int net_os_wake_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_unlock(&dhd->pub);
	return ret;
}

unsigned long dhd_os_spin_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags = 0;

	if (dhd)
		spin_lock_irqsave(&dhd->dhd_lock, flags);

	return flags;
}

void dhd_os_spin_unlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd)
		spin_unlock_irqrestore(&dhd->dhd_lock, flags);
}
