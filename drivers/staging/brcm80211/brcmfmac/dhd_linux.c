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

#ifdef CONFIG_WIFI_CONTROL_FUNC
#include <linux/platform_device.h>
#endif
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mmc/sdio_func.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <bcmdefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>

#include <proto/ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>

#include <wl_cfg80211.h>

#define EPI_VERSION_STR         "4.218.248.5"

#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
#include <linux/wifi_tiwlan.h>

struct semaphore wifi_control_sem;

struct dhd_bus *g_bus;

static struct wifi_platform_data *wifi_control_data;
static struct resource *wifi_irqres;

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
	printk(KERN_ERR "%s = %d\n", __func__, on);
	if (wifi_control_data && wifi_control_data->set_carddetect)
		wifi_control_data->set_carddetect(on);
	return 0;
}

int wifi_set_power(int on, unsigned long msec)
{
	printk(KERN_ERR "%s = %d\n", __func__, on);
	if (wifi_control_data && wifi_control_data->set_power)
		wifi_control_data->set_power(on);
	if (msec)
		mdelay(msec);
	return 0;
}

int wifi_set_reset(int on, unsigned long msec)
{
	printk(KERN_ERR "%s = %d\n", __func__, on);
	if (wifi_control_data && wifi_control_data->set_reset)
		wifi_control_data->set_reset(on);
	if (msec)
		mdelay(msec);
	return 0;
}

static int wifi_probe(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
	    (struct wifi_platform_data *)(pdev->dev.platform_data);

	printk(KERN_ERR "## %s\n", __func__);
	wifi_irqres =
	    platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					 "bcm4329_wlan_irq");
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

	printk(KERN_ERR "## %s\n", __func__);
	wifi_control_data = wifi_ctrl;

	wifi_set_carddetect(0);	/* CardDetect (1->0) */
	wifi_set_power(0, 0);	/* Power Off */

	up(&wifi_control_sem);
	return 0;
}

static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
	DHD_TRACE(("##> %s\n", __func__));
	return 0;
}

static int wifi_resume(struct platform_device *pdev)
{
	DHD_TRACE(("##> %s\n", __func__));
	return 0;
}

static struct platform_driver wifi_device = {
	.probe = wifi_probe,
	.remove = wifi_remove,
	.suspend = wifi_suspend,
	.resume = wifi_resume,
	.driver = {
		   .name = "bcm4329_wlan",
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
#endif	/* defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC) */

#if defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = false;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#endif	/*  defined(CONFIG_PM_SLEEP) */

#if defined(OOB_INTR_ONLY)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif	/* defined(OOB_INTR_ONLY) */

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11n wireless LAN fullmac driver.");
MODULE_SUPPORTED_DEVICE("Broadcom 802.11n WLAN fullmac cards");
MODULE_LICENSE("Dual BSD/GPL");

#define DRV_MODULE_NAME "brcmfmac"

/* Linux wireless extension support */
#if defined(CONFIG_WIRELESS_EXT)
#include <wl_iw.h>
extern wl_iw_extra_params_t g_wl_iw_params;
#endif		/* defined(CONFIG_WIRELESS_EXT) */

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf,
			    uint len);
#endif		/* defined(CONFIG_HAS_EARLYSUSPEND) */

#ifdef PKT_FILTER_SUPPORT
extern void dhd_pktfilter_offload_set(dhd_pub_t *dhd, char *arg);
extern void dhd_pktfilter_offload_enable(dhd_pub_t *dhd, char *arg, int enable,
					 int master_mode);
#endif

/* Interface control information */
typedef struct dhd_if {
	struct dhd_info *info;	/* back pointer to dhd_info */
	/* OS/stack specifics */
	struct net_device *net;
	struct net_device_stats stats;
	int idx;		/* iface idx in dongle */
	int state;		/* interface state */
	uint subunit;		/* subunit */
	u8 mac_addr[ETHER_ADDR_LEN];	/* assigned MAC address */
	bool attached;		/* Delayed attachment when unset */
	bool txflowcontrol;	/* Per interface flow control indicator */
	char name[IFNAMSIZ];	/* linux interface name */
} dhd_if_t;

/* Local private structure (extension of pub) */
typedef struct dhd_info {
#if defined(CONFIG_WIRELESS_EXT)
	wl_iw_t iw;		/* wireless extensions state (must be first) */
#endif				/* defined(CONFIG_WIRELESS_EXT) */

	dhd_pub_t pub;

	/* OS/stack specifics */
	dhd_if_t *iflist[DHD_MAX_IFS];

	struct semaphore proto_sem;
	wait_queue_head_t ioctl_resp_wait;
	struct timer_list timer;
	bool wd_timer_valid;
	struct tasklet_struct tasklet;
	spinlock_t sdlock;
	spinlock_t txqlock;
	/* Thread based operation */
	bool threads_only;
	struct semaphore sdsem;
	struct task_struct *watchdog_tsk;
	struct semaphore watchdog_sem;
	struct task_struct *dpc_tsk;
	struct semaphore dpc_sem;

	/* Thread to issue ioctl for multicast */
	struct task_struct *sysioc_tsk;
	struct semaphore sysioc_sem;
	bool set_multicast;
	bool set_macaddress;
	struct ether_addr macvalue;
	wait_queue_head_t ctrl_wait;
	atomic_t pend_8021x_cnt;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif				/* CONFIG_HAS_EARLYSUSPEND */
} dhd_info_t;

/* Definitions to provide path to the firmware and nvram
 * example nvram_path[MOD_PARAM_PATHLEN]="/projects/wlan/nvram.txt"
 */
char firmware_path[MOD_PARAM_PATHLEN];
char nvram_path[MOD_PARAM_PATHLEN];

/* load firmware and/or nvram values from the filesystem */
module_param_string(firmware_path, firmware_path, MOD_PARAM_PATHLEN, 0);
module_param_string(nvram_path, nvram_path, MOD_PARAM_PATHLEN, 0);

/* Error bits */
module_param(dhd_msg_level, int, 0);

/* Spawn a thread for system ioctls (set mac, set mcast) */
uint dhd_sysioc = true;
module_param(dhd_sysioc, uint, 0);

/* Watchdog interval */
uint dhd_watchdog_ms = 10;
module_param(dhd_watchdog_ms, uint, 0);

#ifdef DHD_DEBUG
/* Console poll interval */
uint dhd_console_ms;
module_param(dhd_console_ms, uint, 0);
#endif				/* DHD_DEBUG */

/* ARP offload agent mode : Enable ARP Host Auto-Reply
and ARP Peer Auto-Reply */
uint dhd_arp_mode = 0xb;
module_param(dhd_arp_mode, uint, 0);

/* ARP offload enable */
uint dhd_arp_enable = true;
module_param(dhd_arp_enable, uint, 0);

/* Global Pkt filter enable control */
uint dhd_pkt_filter_enable = true;
module_param(dhd_pkt_filter_enable, uint, 0);

/*  Pkt filter init setup */
uint dhd_pkt_filter_init;
module_param(dhd_pkt_filter_init, uint, 0);

/* Pkt filter mode control */
uint dhd_master_mode = true;
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

/* Contorl fw roaming */
#ifdef CUSTOMER_HW2
uint dhd_roam;
#else
uint dhd_roam = 1;
#endif

/* Control radio state */
uint dhd_radio_up = 1;

/* Network inteface name */
char iface_name[IFNAMSIZ];
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);

/* The following are specific to the SDIO dongle */

/* IOCTL response timeout */
int dhd_ioctl_timeout_msec = IOCTL_RESP_TIMEOUT;

/* Idle timeout for backplane clock */
int dhd_idletime = DHD_IDLETIME_TICKS;
module_param(dhd_idletime, int, 0);

/* Use polling */
uint dhd_poll = false;
module_param(dhd_poll, uint, 0);

/* Use cfg80211 */
uint dhd_cfg80211 = true;
module_param(dhd_cfg80211, uint, 0);

/* Use interrupts */
uint dhd_intr = true;
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
uint dhd_pktgen;
module_param(dhd_pktgen, uint, 0);

/* Echo packet len (0 => sawtooth, max 2040) */
uint dhd_pktgen_len;
module_param(dhd_pktgen_len, uint, 0);
#endif

#define FAVORITE_WIFI_CP	(!!dhd_cfg80211)
#define IS_CFG80211_FAVORITE() FAVORITE_WIFI_CP
#define DBG_CFG80211_GET() ((dhd_cfg80211 & WL_DBG_MASK) >> 1)
#define NO_FW_REQ() (dhd_cfg80211 & 0x80)

/* Version string to report */
#ifdef DHD_DEBUG
#define DHD_COMPILED "\nCompiled in " SRCBASE
#else
#define DHD_COMPILED
#endif

static char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR
#ifdef DHD_DEBUG
"\nCompiled in " " on " __DATE__ " at " __TIME__
#endif
;

#if defined(CONFIG_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif				/* defined(CONFIG_WIRELESS_EXT) */

static void dhd_dpc(unsigned long data);
/* forward decl */
extern int dhd_wait_pend8021x(struct net_device *dev);

#ifdef TOE
#ifndef BDC
#error TOE requires BDC
#endif				/* !BDC */
static int dhd_toe_get(dhd_info_t *dhd, int idx, u32 *toe_ol);
static int dhd_toe_set(dhd_info_t *dhd, int idx, u32 toe_ol);
#endif				/* TOE */

static int dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
			     wl_event_msg_t *event_ptr, void **data_ptr);

#if defined(CONFIG_PM_SLEEP)
static int dhd_sleep_pm_callback(struct notifier_block *nfb,
				 unsigned long action, void *ignored)
{
	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		dhd_mmc_suspend = true;
		return NOTIFY_OK;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		dhd_mmc_suspend = false;
		return NOTIFY_OK;
	}
	return 0;
}

static struct notifier_block dhd_sleep_pm_notifier = {
	.notifier_call = dhd_sleep_pm_callback,
	.priority = 0
};

extern int register_pm_notifier(struct notifier_block *nb);
extern int unregister_pm_notifier(struct notifier_block *nb);
#endif	/* defined(CONFIG_PM_SLEEP) */
	/* && defined(DHD_GPL) */
static void dhd_set_packet_filter(int value, dhd_pub_t *dhd)
{
#ifdef PKT_FILTER_SUPPORT
	DHD_TRACE(("%s: %d\n", __func__, value));
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
	/* wl_pkt_filter_enable_t       enable_parm; */
	char iovbuf[32];
	int bcn_li_dtim = 3;
#ifdef CUSTOMER_HW2
	uint roamvar = 1;
#endif				/* CUSTOMER_HW2 */

	DHD_TRACE(("%s: enter, value = %d in_suspend=%d\n",
		   __func__, value, dhd->in_suspend));

	if (dhd && dhd->up) {
		if (value && dhd->in_suspend) {

			/* Kernel suspended */
			DHD_TRACE(("%s: force extra Suspend setting\n",
				   __func__));

			dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM,
					 (char *)&power_mode,
					 sizeof(power_mode));

			/* Enable packet filter, only allow unicast
				 packet to send up */
			dhd_set_packet_filter(1, dhd);

			/* if dtim skip setup as default force it
			 * to wake each thrid dtim
			 * for better power saving.
			 * Note that side effect is chance to miss BC/MC
			 * packet
			 */
			if ((dhd->dtim_skip == 0) || (dhd->dtim_skip == 1))
				bcn_li_dtim = 3;
			else
				bcn_li_dtim = dhd->dtim_skip;
			bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
				    4, iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf,
					 sizeof(iovbuf));
#ifdef CUSTOMER_HW2
			/* Disable build-in roaming to allowed \
			 * supplicant to take of romaing
			 */
			bcm_mkiovar("roam_off", (char *)&roamvar, 4,
				    iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf,
					 sizeof(iovbuf));
#endif				/* CUSTOMER_HW2 */
		} else {

			/* Kernel resumed  */
			DHD_TRACE(("%s: Remove extra suspend setting\n",
				   __func__));

			power_mode = PM_FAST;
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM,
					 (char *)&power_mode,
					 sizeof(power_mode));

			/* disable pkt filter */
			dhd_set_packet_filter(0, dhd);

			/* restore pre-suspend setting for dtim_skip */
			bcm_mkiovar("bcn_li_dtim", (char *)&dhd->dtim_skip,
				    4, iovbuf, sizeof(iovbuf));

			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf,
					 sizeof(iovbuf));
#ifdef CUSTOMER_HW2
			roamvar = 0;
			bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf,
				    sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf,
					 sizeof(iovbuf));
#endif				/* CUSTOMER_HW2 */
		}
	}

	return 0;
}

static void dhd_suspend_resume_helper(struct dhd_info *dhd, int val)
{
	dhd_pub_t *dhdp = &dhd->pub;

	dhd_os_proto_block(dhdp);
	/* Set flag when early suspend was called */
	dhdp->in_suspend = val;
	if (!dhdp->suspend_disable_flag)
		dhd_set_suspend(val, dhdp);
	dhd_os_proto_unblock(dhdp);
}

static void dhd_early_suspend(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);

	DHD_TRACE(("%s: enter\n", __func__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 1);

}

static void dhd_late_resume(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);

	DHD_TRACE(("%s: enter\n", __func__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 0);
}
#endif				/* defined(CONFIG_HAS_EARLYSUSPEND) */

/*
 * Generalized timeout mechanism.  Uses spin sleep with exponential
 * back-off until
 * the sleep time reaches one jiffy, then switches over to task delay.  Usage:
 *
 *      dhd_timeout_start(&tmo, usec);
 *      while (!dhd_timeout_expired(&tmo))
 *              if (poll_something())
 *                      break;
 *      if (dhd_timeout_expired(&tmo))
 *              fatal();
 */

void dhd_timeout_start(dhd_timeout_t *tmo, uint usec)
{
	tmo->limit = usec;
	tmo->increment = 0;
	tmo->elapsed = 0;
	tmo->tick = 1000000 / HZ;
}

int dhd_timeout_expired(dhd_timeout_t *tmo)
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
		udelay(tmo->increment);
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

static int dhd_net2idx(dhd_info_t *dhd, struct net_device *net)
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

int dhd_ifname2idx(dhd_info_t *dhd, char *name)
{
	int i = DHD_MAX_IFS;

	ASSERT(dhd);

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0)
		if (dhd->iflist[i]
		    && !strncmp(dhd->iflist[i]->name, name, IFNAMSIZ))
			break;

	DHD_TRACE(("%s: return idx %d for \"%s\"\n", __func__, i, name));

	return i;		/* default - the primary interface */
}

char *dhd_ifname(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *) dhdp->info;

	ASSERT(dhd);

	if (ifidx < 0 || ifidx >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: ifidx %d out of range\n", __func__, ifidx));
		return "<if_bad>";
	}

	if (dhd->iflist[ifidx] == NULL) {
		DHD_ERROR(("%s: null i/f %d\n", __func__, ifidx));
		return "<if_null>";
	}

	if (dhd->iflist[ifidx]->net)
		return dhd->iflist[ifidx]->net->name;

	return "<if_none>";
}

static void _dhd_set_multicast_list(dhd_info_t *dhd, int ifidx)
{
	struct net_device *dev;
	struct netdev_hw_addr *ha;
	u32 allmulti, cnt;

	wl_ioctl_t ioc;
	char *buf, *bufp;
	uint buflen;
	int ret;

	ASSERT(dhd && dhd->iflist[ifidx]);
	dev = dhd->iflist[ifidx]->net;
	cnt = netdev_mc_count(dev);

	/* Determine initial value of allmulti flag */
	allmulti = (dev->flags & IFF_ALLMULTI) ? true : false;

	/* Send down the multicast list first. */

	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETHER_ADDR_LEN);
	bufp = buf = kmalloc(buflen, GFP_ATOMIC);
	if (!bufp) {
		DHD_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
			   dhd_ifname(&dhd->pub, ifidx), cnt));
		return;
	}

	strcpy(bufp, "mcast_list");
	bufp += strlen("mcast_list") + 1;

	cnt = htol32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);

	netdev_for_each_mc_addr(ha, dev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
		cnt--;
	}

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = true;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set mcast_list failed, cnt %d\n",
			   dhd_ifname(&dhd->pub, ifidx), cnt));
		allmulti = cnt ? true : allmulti;
	}

	kfree(buf);

	/* Now send the allmulti setting.  This is based on the setting in the
	 * net_device flags, but might be modified above to be turned on if we
	 * were trying to set some addresses and dongle rejected it...
	 */

	buflen = sizeof("allmulti") + sizeof(allmulti);
	buf = kmalloc(buflen, GFP_ATOMIC);
	if (!buf) {
		DHD_ERROR(("%s: out of memory for allmulti\n",
			   dhd_ifname(&dhd->pub, ifidx)));
		return;
	}
	allmulti = htol32(allmulti);

	if (!bcm_mkiovar
	    ("allmulti", (void *)&allmulti, sizeof(allmulti), buf, buflen)) {
		DHD_ERROR(("%s: mkiovar failed for allmulti, datalen %d "
			"buflen %u\n", dhd_ifname(&dhd->pub, ifidx),
			(int)sizeof(allmulti), buflen));
		kfree(buf);
		return;
	}

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = true;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set allmulti %d failed\n",
			   dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}

	kfree(buf);

	/* Finally, pick up the PROMISC flag as well, like the NIC
		 driver does */

	allmulti = (dev->flags & IFF_PROMISC) ? true : false;
	allmulti = htol32(allmulti);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_PROMISC;
	ioc.buf = &allmulti;
	ioc.len = sizeof(allmulti);
	ioc.set = true;

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

	DHD_TRACE(("%s enter\n", __func__));
	if (!bcm_mkiovar
	    ("cur_etheraddr", (char *)addr, ETHER_ADDR_LEN, buf, 32)) {
		DHD_ERROR(("%s: mkiovar failed for cur_etheraddr\n",
			   dhd_ifname(&dhd->pub, ifidx)));
		return -1;
	}
	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = 32;
	ioc.set = true;

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set cur_etheraddr failed\n",
			   dhd_ifname(&dhd->pub, ifidx)));
	} else {
		memcpy(dhd->iflist[ifidx]->net->dev_addr, addr, ETHER_ADDR_LEN);
	}

	return ret;
}

#ifdef SOFTAP
extern struct net_device *ap_net_dev;
#endif

static void dhd_op_if(dhd_if_t *ifp)
{
	dhd_info_t *dhd;
	int ret = 0, err = 0;

	ASSERT(ifp && ifp->info && ifp->idx);	/* Virtual interfaces only */

	dhd = ifp->info;

	DHD_TRACE(("%s: idx %d, state %d\n", __func__, ifp->idx, ifp->state));

	switch (ifp->state) {
	case WLC_E_IF_ADD:
		/*
		 * Delete the existing interface before overwriting it
		 * in case we missed the WLC_E_IF_DEL event.
		 */
		if (ifp->net != NULL) {
			DHD_ERROR(("%s: ERROR: netdev:%s already exists, "
			"try free & unregister\n",
			__func__, ifp->net->name));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			free_netdev(ifp->net);
		}
		/* Allocate etherdev, including space for private structure */
		ifp->net = alloc_etherdev(sizeof(dhd));
		if (!ifp->net) {
			DHD_ERROR(("%s: OOM - alloc_etherdev\n", __func__));
			ret = -ENOMEM;
		}
		if (ret == 0) {
			strcpy(ifp->net->name, ifp->name);
			memcpy(netdev_priv(ifp->net), &dhd, sizeof(dhd));
			err = dhd_net_attach(&dhd->pub, ifp->idx);
			if (err != 0) {
				DHD_ERROR(("%s: dhd_net_attach failed, "
					"err %d\n",
					__func__, err));
				ret = -EOPNOTSUPP;
			} else {
#ifdef SOFTAP
				/* semaphore that the soft AP CODE
					 waits on */
				extern struct semaphore ap_eth_sema;

				/* save ptr to wl0.1 netdev for use
					 in wl_iw.c  */
				ap_net_dev = ifp->net;
				/* signal to the SOFTAP 'sleeper' thread,
					 wl0.1 is ready */
				up(&ap_eth_sema);
#endif
				DHD_TRACE(("\n ==== pid:%x, net_device for "
					"if:%s created ===\n\n",
					current->pid, ifp->net->name));
				ifp->state = 0;
			}
		}
		break;
	case WLC_E_IF_DEL:
		if (ifp->net != NULL) {
			DHD_TRACE(("\n%s: got 'WLC_E_IF_DEL' state\n",
				   __func__));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			ret = DHD_DEL_IF;	/* Make sure the free_netdev()
							 is called */
		}
		break;
	default:
		DHD_ERROR(("%s: bad op %d\n", __func__, ifp->state));
		ASSERT(!ifp->state);
		break;
	}

	if (ret < 0) {
		if (ifp->net)
			free_netdev(ifp->net);

		dhd->iflist[ifp->idx] = NULL;
		kfree(ifp);
#ifdef SOFTAP
		if (ifp->net == ap_net_dev)
			ap_net_dev = NULL;	/*  NULL  SOFTAP global
							 wl0.1 as well */
#endif				/*  SOFTAP */
	}
}

static int _dhd_sysioc_thread(void *data)
{
	dhd_info_t *dhd = (dhd_info_t *) data;
	int i;
#ifdef SOFTAP
	bool in_ap = false;
#endif

	allow_signal(SIGTERM);

	while (down_interruptible(&dhd->sysioc_sem) == 0) {
		if (kthread_should_stop())
			break;
		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
#ifdef SOFTAP
				in_ap = (ap_net_dev != NULL);
#endif				/* SOFTAP */
				if (dhd->iflist[i]->state)
					dhd_op_if(dhd->iflist[i]);
#ifdef SOFTAP
				if (dhd->iflist[i] == NULL) {
					DHD_TRACE(("\n\n %s: interface %d "
						"removed!\n", __func__, i));
					continue;
				}

				if (in_ap && dhd->set_macaddress) {
					DHD_TRACE(("attempt to set MAC for %s "
						"in AP Mode," "blocked. \n",
						dhd->iflist[i]->net->name));
					dhd->set_macaddress = false;
					continue;
				}

				if (in_ap && dhd->set_multicast) {
					DHD_TRACE(("attempt to set MULTICAST list for %s" "in AP Mode, blocked. \n",
						dhd->iflist[i]->net->name));
					dhd->set_multicast = false;
					continue;
				}
#endif				/* SOFTAP */
				if (dhd->set_multicast) {
					dhd->set_multicast = false;
					_dhd_set_multicast_list(dhd, i);
				}
				if (dhd->set_macaddress) {
					dhd->set_macaddress = false;
					_dhd_set_mac_address(dhd, i,
							     &dhd->macvalue);
				}
			}
		}
	}
	return 0;
}

static int dhd_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;

	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;

	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return -1;

	ASSERT(dhd->sysioc_tsk);
	memcpy(&dhd->macvalue, sa->sa_data, ETHER_ADDR_LEN);
	dhd->set_macaddress = true;
	up(&dhd->sysioc_sem);

	return ret;
}

static void dhd_set_multicast_list(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(dev);
	int ifidx;

	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return;

	ASSERT(dhd->sysioc_tsk);
	dhd->set_multicast = true;
	up(&dhd->sysioc_sem);
}

int dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret;
	dhd_info_t *dhd = (dhd_info_t *) (dhdp->info);

	/* Reject if down */
	if (!dhdp->up || (dhdp->busstate == DHD_BUS_DOWN))
		return -ENODEV;

	/* Update multicast statistic */
	if (PKTLEN(pktbuf) >= ETHER_ADDR_LEN) {
		u8 *pktdata = (u8 *) PKTDATA(pktbuf);
		struct ether_header *eh = (struct ether_header *)pktdata;

		if (ETHER_ISMULTI(eh->ether_dhost))
			dhdp->tx_multicast++;
		if (ntoh16(eh->ether_type) == ETHER_TYPE_802_1X)
			atomic_inc(&dhd->pend_8021x_cnt);
	}

	/* If the protocol uses a data header, apply it */
	dhd_prot_hdrpush(dhdp, ifidx, pktbuf);

	/* Use bus module to send data frame */
#ifdef BCMDBUS
	ret = dbus_send_pkt(dhdp->dbus, pktbuf, NULL /* pktinfo */);
#else
	WAKE_LOCK_TIMEOUT(dhdp, WAKE_LOCK_TMOUT, 25);
	ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif				/* BCMDBUS */

	return ret;
}

static int dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	void *pktbuf;
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(net);
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Reject if down */
	if (!dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN)) {
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d\n",
			   __func__, dhd->pub.up, dhd->pub.busstate));
		netif_stop_queue(net);
		return -ENODEV;
	}

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx %d\n", __func__, ifidx));
		netif_stop_queue(net);
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
		skb = skb2;
		if (skb == NULL) {
			DHD_ERROR(("%s: skb_realloc_headroom failed\n",
				   dhd_ifname(&dhd->pub, ifidx)));
			ret = -ENOMEM;
			goto done;
		}
	}

	/* Convert to packet */
	pktbuf = PKTFRMNATIVE(dhd->pub.osh, skb);
	if (!pktbuf) {
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

	/* Return ok: we always eat the packet */
	return 0;
}

void dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool state)
{
	struct net_device *net;
	dhd_info_t *dhd = dhdp->info;

	DHD_TRACE(("%s: Enter\n", __func__));

	dhdp->txoff = state;
	ASSERT(dhd && dhd->iflist[ifidx]);
	net = dhd->iflist[ifidx]->net;
	if (state == ON)
		netif_stop_queue(net);
	else
		netif_wake_queue(net);
}

void dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *pktbuf, int numpkt)
{
	dhd_info_t *dhd = (dhd_info_t *) dhdp->info;
	struct sk_buff *skb;
	unsigned char *eth;
	uint len;
	void *data, *pnext, *save_pktbuf;
	int i;
	dhd_if_t *ifp;
	wl_event_msg_t event;

	DHD_TRACE(("%s: Enter\n", __func__));

	save_pktbuf = pktbuf;

	for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {

		pnext = PKTNEXT(pktbuf);
		PKTSETNEXT(pktbuf, NULL);

		skb = PKTTONATIVE(dhdp->osh, pktbuf);

		/* Get the protocol, maintain skb around eth_type_trans()
		 * The main reason for this hack is for the limitation of
		 * Linux 2.4 where 'eth_type_trans' uses the
		 * 'net->hard_header_len'
		 * to perform skb_pull inside vs ETH_HLEN. Since to avoid
		 * coping of the packet coming from the network stack to add
		 * BDC, Hardware header etc, during network interface
		 * registration
		 * we set the 'net->hard_header_len' to ETH_HLEN + extra space
		 * required
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

		if (skb->pkt_type == PACKET_MULTICAST)
			dhd->pub.rx_multicast++;

		skb->data = eth;
		skb->len = len;

		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* Process special event packets and then discard them */
		if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM)
			dhd_wl_host_event(dhd, &ifidx,
					  skb->mac_header,
					  &event, &data);

		ASSERT(ifidx < DHD_MAX_IFS && dhd->iflist[ifidx]);
		if (dhd->iflist[ifidx] && !dhd->iflist[ifidx]->state)
			ifp = dhd->iflist[ifidx];

		if (ifp->net)
			ifp->net->last_rx = jiffies;

		dhdp->dstats.rx_bytes += skb->len;
		dhdp->rx_packets++;	/* Local count */

		if (in_interrupt()) {
			netif_rx(skb);
		} else {
			/* If the receive is not processed inside an ISR,
			 * the softirqd must be woken explicitly to service
			 * the NET_RX_SOFTIRQ.  In 2.6 kernels, this is handled
			 * by netif_rx_ni(), but in earlier kernels, we need
			 * to do it manually.
			 */
			netif_rx_ni(skb);
		}
	}
}

void dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx)
{
	/* Linux version has nothing to do */
	return;
}

void dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success)
{
	uint ifidx;
	dhd_info_t *dhd = (dhd_info_t *) (dhdp->info);
	struct ether_header *eh;
	u16 type;

	dhd_prot_hdrpull(dhdp, &ifidx, txp);

	eh = (struct ether_header *)PKTDATA(txp);
	type = ntoh16(eh->ether_type);

	if (type == ETHER_TYPE_802_1X)
		atomic_dec(&dhd->pend_8021x_cnt);

}

static struct net_device_stats *dhd_get_stats(struct net_device *net)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(net);
	dhd_if_t *ifp;
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __func__));

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

static int dhd_watchdog_thread(void *data)
{
	dhd_info_t *dhd = (dhd_info_t *) data;
	WAKE_LOCK_INIT(&dhd->pub, WAKE_LOCK_WATCHDOG, "dhd_watchdog_thread");

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
#ifdef DHD_SCHED
	if (dhd_watchdog_prio > 0) {
		struct sched_param param;
		param.sched_priority = (dhd_watchdog_prio < MAX_RT_PRIO) ?
		    dhd_watchdog_prio : (MAX_RT_PRIO - 1);
		setScheduler(current, SCHED_FIFO, &param);
	}
#endif				/* DHD_SCHED */

	allow_signal(SIGTERM);
	/* Run until signal received */
	while (1) {
		if (kthread_should_stop())
			break;
		if (down_interruptible(&dhd->watchdog_sem) == 0) {
			if (dhd->pub.dongle_reset == false) {
				WAKE_LOCK(&dhd->pub, WAKE_LOCK_WATCHDOG);
				/* Call the bus module watchdog */
				dhd_bus_watchdog(&dhd->pub);
				WAKE_UNLOCK(&dhd->pub, WAKE_LOCK_WATCHDOG);
			}
			/* Count the tick for reference */
			dhd->pub.tickcnt++;
		} else
			break;
	}

	WAKE_LOCK_DESTROY(&dhd->pub, WAKE_LOCK_WATCHDOG);
	return 0;
}

static void dhd_watchdog(unsigned long data)
{
	dhd_info_t *dhd = (dhd_info_t *) data;

	if (dhd->watchdog_tsk) {
		up(&dhd->watchdog_sem);

		/* Reschedule the watchdog */
		if (dhd->wd_timer_valid) {
			mod_timer(&dhd->timer,
				  jiffies + dhd_watchdog_ms * HZ / 1000);
		}
		return;
	}

	/* Call the bus module watchdog */
	dhd_bus_watchdog(&dhd->pub);

	/* Count the tick for reference */
	dhd->pub.tickcnt++;

	/* Reschedule the watchdog */
	if (dhd->wd_timer_valid)
		mod_timer(&dhd->timer, jiffies + dhd_watchdog_ms * HZ / 1000);
}

static int dhd_dpc_thread(void *data)
{
	dhd_info_t *dhd = (dhd_info_t *) data;

	WAKE_LOCK_INIT(&dhd->pub, WAKE_LOCK_DPC, "dhd_dpc_thread");
	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
#ifdef DHD_SCHED
	if (dhd_dpc_prio > 0) {
		struct sched_param param;
		param.sched_priority =
		    (dhd_dpc_prio <
		     MAX_RT_PRIO) ? dhd_dpc_prio : (MAX_RT_PRIO - 1);
		setScheduler(current, SCHED_FIFO, &param);
	}
#endif				/* DHD_SCHED */

	allow_signal(SIGTERM);
	/* Run until signal received */
	while (1) {
		if (kthread_should_stop())
			break;
		if (down_interruptible(&dhd->dpc_sem) == 0) {
			/* Call bus dpc unless it indicated down
				 (then clean stop) */
			if (dhd->pub.busstate != DHD_BUS_DOWN) {
				WAKE_LOCK(&dhd->pub, WAKE_LOCK_DPC);
				if (dhd_bus_dpc(dhd->pub.bus)) {
					up(&dhd->dpc_sem);
					WAKE_LOCK_TIMEOUT(&dhd->pub,
							  WAKE_LOCK_TMOUT, 25);
				}
				WAKE_UNLOCK(&dhd->pub, WAKE_LOCK_DPC);
			} else {
				dhd_bus_stop(dhd->pub.bus, true);
			}
		} else
			break;
	}

	WAKE_LOCK_DESTROY(&dhd->pub, WAKE_LOCK_DPC);
	return 0;
}

static void dhd_dpc(unsigned long data)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *) data;

	/* Call bus dpc unless it indicated down (then clean stop) */
	if (dhd->pub.busstate != DHD_BUS_DOWN) {
		if (dhd_bus_dpc(dhd->pub.bus))
			tasklet_schedule(&dhd->tasklet);
	} else {
		dhd_bus_stop(dhd->pub.bus, true);
	}
}

void dhd_sched_dpc(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *) dhdp->info;

	if (dhd->dpc_tsk) {
		up(&dhd->dpc_sem);
		return;
	}

	tasklet_schedule(&dhd->tasklet);
}

#ifdef TOE
/* Retrieve current toe component enables, which are kept
	 as a bitmap in toe_ol iovar */
static int dhd_toe_get(dhd_info_t *dhd, int ifidx, u32 *toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint) sizeof(buf);
	ioc.set = false;

	strcpy(buf, "toe_ol");
	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		/* Check for older dongle image that doesn't support toe_ol */
		if (ret == -EIO) {
			DHD_ERROR(("%s: toe not supported by device\n",
				   dhd_ifname(&dhd->pub, ifidx)));
			return -EOPNOTSUPP;
		}

		DHD_INFO(("%s: could not get toe_ol: ret=%d\n",
			  dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	memcpy(toe_ol, buf, sizeof(u32));
	return 0;
}

/* Set current toe component enables in toe_ol iovar,
	 and set toe global enable iovar */
static int dhd_toe_set(dhd_info_t *dhd, int ifidx, u32 toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int toe, ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = (uint) sizeof(buf);
	ioc.set = true;

	/* Set toe_ol as requested */

	strcpy(buf, "toe_ol");
	memcpy(&buf[sizeof("toe_ol")], &toe_ol, sizeof(u32));

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: could not set toe_ol: ret=%d\n",
			   dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	/* Enable toe globally only if any components are enabled. */

	toe = (toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe, sizeof(u32));

	ret = dhd_prot_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: could not set toe: ret=%d\n",
			   dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	return 0;
}
#endif				/* TOE */

static void dhd_ethtool_get_drvinfo(struct net_device *net,
				    struct ethtool_drvinfo *info)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(net);

	sprintf(info->driver, DRV_MODULE_NAME);
	sprintf(info->version, "%lu", dhd->pub.drv_version);
	sprintf(info->fw_version, "%s", wl_cfg80211_get_fwname());
	sprintf(info->bus_info, "%s", dev_name(&wl_cfg80211_get_sdio_func()->dev));
}

struct ethtool_ops dhd_ethtool_ops = {
	.get_drvinfo = dhd_ethtool_get_drvinfo
};

static int dhd_ethtool(dhd_info_t *dhd, void *uaddr)
{
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	u32 cmd;
#ifdef TOE
	struct ethtool_value edata;
	u32 toe_cmpnt, csum_dir;
	int ret;
#endif

	DHD_TRACE(("%s: Enter\n", __func__));

	/* all ethtool calls start with a cmd word */
	if (copy_from_user(&cmd, uaddr, sizeof(u32)))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:
		/* Copy out any request driver name */
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		strncpy(drvname, info.driver, sizeof(info.driver));
		drvname[sizeof(info.driver) - 1] = '\0';

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
			DHD_ERROR(("%s: dongle is not up\n", __func__));
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
		DHD_CTL(("%s: given %*s, returning %s\n", __func__,
			 (int)sizeof(drvname), drvname, info.driver));
		break;

#ifdef TOE
		/* Get toe offload components from dongle */
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		ret = dhd_toe_get(dhd, 0, &toe_cmpnt);
		if (ret < 0)
			return ret;

		csum_dir =
		    (cmd == ETHTOOL_GTXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

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
		ret = dhd_toe_get(dhd, 0, &toe_cmpnt);
		if (ret < 0)
			return ret;

		csum_dir =
		    (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		ret = dhd_toe_set(dhd, 0, toe_cmpnt);
		if (ret < 0)
			return ret;

		/* If setting TX checksum mode, tell Linux the new mode */
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				dhd->iflist[0]->net->features |=
				    NETIF_F_IP_CSUM;
			else
				dhd->iflist[0]->net->features &=
				    ~NETIF_F_IP_CSUM;
		}

		break;
#endif				/* TOE */

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr, int cmd)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int buflen = 0;
	void *buf = NULL;
	uint driver = 0;
	int ifidx;
	bool is_set_key_cmd;

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __func__, ifidx, cmd));

	if (ifidx == DHD_BAD_IF)
		return -1;

#if defined(CONFIG_WIRELESS_EXT)
	/* linux wireless extensions */
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		/* may recurse, do NOT lock */
		return wl_iw_ioctl(net, ifr, cmd);
	}
#endif				/* defined(CONFIG_WIRELESS_EXT) */

	if (cmd == SIOCETHTOOL)
		return dhd_ethtool(dhd, (void *)ifr->ifr_data);

	if (cmd != SIOCDEVPRIVATE)
		return -EOPNOTSUPP;

	memset(&ioc, 0, sizeof(ioc));

	/* Copy the ioc control structure part of ioctl request */
	if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
		bcmerror = -BCME_BADADDR;
		goto done;
	}

	/* Copy out any buffer passed */
	if (ioc.buf) {
		buflen = min_t(int, ioc.len, DHD_IOCTL_MAXLEN);
		/* optimization for direct ioctl calls from kernel */
		/*
		   if (segment_eq(get_fs(), KERNEL_DS)) {
		   buf = ioc.buf;
		   } else {
		 */
		{
			buf = kmalloc(buflen, GFP_ATOMIC);
			if (!buf) {
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
	if ((dhd->pub.busstate != DHD_BUS_DATA)) {
		DHD_ERROR(("%s DONGLE_DOWN,__func__\n", __func__));
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
	if (is_set_key_cmd)
		dhd_wait_pend8021x(net);

	WAKE_LOCK_INIT(&dhd->pub, WAKE_LOCK_IOCTL, "dhd_ioctl_entry");
	WAKE_LOCK(&dhd->pub, WAKE_LOCK_IOCTL);

	bcmerror =
	    dhd_prot_ioctl(&dhd->pub, ifidx, (wl_ioctl_t *)&ioc, buf, buflen);

	WAKE_UNLOCK(&dhd->pub, WAKE_LOCK_IOCTL);
	WAKE_LOCK_DESTROY(&dhd->pub, WAKE_LOCK_IOCTL);
done:
	if (!bcmerror && buf && ioc.buf) {
		if (copy_to_user(ioc.buf, buf, buflen))
			bcmerror = -EFAULT;
	}

	if (buf)
		kfree(buf);

	return OSL_ERROR(bcmerror);
}

static int dhd_stop(struct net_device *net)
{
#if !defined(IGNORE_ETH0_DOWN)
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(net);

	DHD_TRACE(("%s: Enter\n", __func__));
	if (IS_CFG80211_FAVORITE()) {
		wl_cfg80211_down();
	}
	if (dhd->pub.up == 0)
		return 0;

	/* Set state and stop OS transmissions */
	dhd->pub.up = 0;
	netif_stop_queue(net);
#else
	DHD_ERROR(("BYPASS %s:due to BRCM compilation : under investigation\n",
		__func__));
#endif				/* !defined(IGNORE_ETH0_DOWN) */

	return 0;
}

static int dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(net);
#ifdef TOE
	u32 toe_ol;
#endif
	int ifidx = dhd_net2idx(dhd, net);
	s32 ret = 0;

	DHD_TRACE(("%s: ifidx %d\n", __func__, ifidx));

	if (ifidx == 0) {	/* do it only for primary eth0 */

		/* try to bring up bus */
		ret = dhd_bus_start(&dhd->pub);
		if (ret != 0) {
			DHD_ERROR(("%s: failed with code %d\n", __func__, ret));
			return -1;
		}
		atomic_set(&dhd->pend_8021x_cnt, 0);

		memcpy(net->dev_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);

#ifdef TOE
		/* Get current TOE mode from dongle */
		if (dhd_toe_get(dhd, ifidx, &toe_ol) >= 0
		    && (toe_ol & TOE_TX_CSUM_OL) != 0)
			dhd->iflist[ifidx]->net->features |= NETIF_F_IP_CSUM;
		else
			dhd->iflist[ifidx]->net->features &= ~NETIF_F_IP_CSUM;
#endif
	}
	/* Allow transmit calls */
	netif_start_queue(net);
	dhd->pub.up = 1;
	if (IS_CFG80211_FAVORITE()) {
		if (unlikely(wl_cfg80211_up())) {
			DHD_ERROR(("%s: failed to bring up cfg80211\n",
				   __func__));
			return -1;
		}
	}

	return ret;
}

osl_t *dhd_osl_attach(void *pdev, uint bustype)
{
	return osl_attach(pdev, bustype, true);
}

void dhd_osl_detach(osl_t *osh)
{
	osl_detach(osh);
}

int
dhd_add_if(dhd_info_t *dhd, int ifidx, void *handle, char *name,
	   u8 *mac_addr, u32 flags, u8 bssidx)
{
	dhd_if_t *ifp;

	DHD_TRACE(("%s: idx %d, handle->%p\n", __func__, ifidx, handle));

	ASSERT(dhd && (ifidx < DHD_MAX_IFS));

	ifp = dhd->iflist[ifidx];
	if (!ifp && !(ifp = kmalloc(sizeof(dhd_if_t), GFP_ATOMIC))) {
		DHD_ERROR(("%s: OOM - dhd_if_t\n", __func__));
		return -ENOMEM;
	}

	memset(ifp, 0, sizeof(dhd_if_t));
	ifp->info = dhd;
	dhd->iflist[ifidx] = ifp;
	strlcpy(ifp->name, name, IFNAMSIZ);
	if (mac_addr != NULL)
		memcpy(&ifp->mac_addr, mac_addr, ETHER_ADDR_LEN);

	if (handle == NULL) {
		ifp->state = WLC_E_IF_ADD;
		ifp->idx = ifidx;
		ASSERT(dhd->sysioc_tsk);
		up(&dhd->sysioc_sem);
	} else
		ifp->net = (struct net_device *)handle;

	return 0;
}

void dhd_del_if(dhd_info_t *dhd, int ifidx)
{
	dhd_if_t *ifp;

	DHD_TRACE(("%s: idx %d\n", __func__, ifidx));

	ASSERT(dhd && ifidx && (ifidx < DHD_MAX_IFS));
	ifp = dhd->iflist[ifidx];
	if (!ifp) {
		DHD_ERROR(("%s: Null interface\n", __func__));
		return;
	}

	ifp->state = WLC_E_IF_DEL;
	ifp->idx = ifidx;
	ASSERT(dhd->sysioc_tsk);
	up(&dhd->sysioc_sem);
}

dhd_pub_t *dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen)
{
	dhd_info_t *dhd = NULL;
	struct net_device *net;

	DHD_TRACE(("%s: Enter\n", __func__));
	/* updates firmware nvram path if it was provided as module
		 paramters */
	if ((firmware_path != NULL) && (firmware_path[0] != '\0'))
		strcpy(fw_path, firmware_path);
	if ((nvram_path != NULL) && (nvram_path[0] != '\0'))
		strcpy(nv_path, nvram_path);

	/* Allocate etherdev, including space for private structure */
	net = alloc_etherdev(sizeof(dhd));
	if (!net) {
		DHD_ERROR(("%s: OOM - alloc_etherdev\n", __func__));
		goto fail;
	}

	/* Allocate primary dhd_info */
	dhd = kmalloc(sizeof(dhd_info_t), GFP_ATOMIC);
	if (!dhd) {
		DHD_ERROR(("%s: OOM - alloc dhd_info\n", __func__));
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

	if (dhd_add_if(dhd, 0, (void *)net, net->name, NULL, 0, 0) ==
	    DHD_BAD_IF)
		goto fail;

	net->netdev_ops = NULL;
	sema_init(&dhd->proto_sem, 1);
	/* Initialize other structure content */
	init_waitqueue_head(&dhd->ioctl_resp_wait);
	init_waitqueue_head(&dhd->ctrl_wait);

	/* Initialize the spinlocks */
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);

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
#endif	/* defined(CONFIG_WIRELESS_EXT) */

	/* Attach and link in the cfg80211 */
	if (IS_CFG80211_FAVORITE()) {
		if (unlikely(wl_cfg80211_attach(net, &dhd->pub))) {
			DHD_ERROR(("wl_cfg80211_attach failed\n"));
			goto fail;
		}
		if (!NO_FW_REQ()) {
			strcpy(fw_path, wl_cfg80211_get_fwname());
			strcpy(nv_path, wl_cfg80211_get_nvramname());
		}
		wl_cfg80211_dbg_level(DBG_CFG80211_GET());
	}

	/* Set up the watchdog timer */
	init_timer(&dhd->timer);
	dhd->timer.data = (unsigned long) dhd;
	dhd->timer.function = dhd_watchdog;

	/* Initialize thread based operation and lock */
	sema_init(&dhd->sdsem, 1);
	if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0))
		dhd->threads_only = true;
	else
		dhd->threads_only = false;

	if (dhd_dpc_prio >= 0) {
		/* Initialize watchdog thread */
		sema_init(&dhd->watchdog_sem, 0);
		dhd->watchdog_tsk = kthread_run(dhd_watchdog_thread, dhd,
						"dhd_watchdog");
		if (IS_ERR(dhd->watchdog_tsk)) {
			printk(KERN_WARNING
				"dhd_watchdog thread failed to start\n");
			dhd->watchdog_tsk = NULL;
		}
	} else {
		dhd->watchdog_tsk = NULL;
	}

	/* Set up the bottom half handler */
	if (dhd_dpc_prio >= 0) {
		/* Initialize DPC thread */
		sema_init(&dhd->dpc_sem, 0);
		dhd->dpc_tsk = kthread_run(dhd_dpc_thread, dhd, "dhd_dpc");
		if (IS_ERR(dhd->dpc_tsk)) {
			printk(KERN_WARNING
				"dhd_dpc thread failed to start\n");
			dhd->dpc_tsk = NULL;
		}
	} else {
		tasklet_init(&dhd->tasklet, dhd_dpc, (unsigned long) dhd);
		dhd->dpc_tsk = NULL;
	}

	if (dhd_sysioc) {
		sema_init(&dhd->sysioc_sem, 0);
		dhd->sysioc_tsk = kthread_run(_dhd_sysioc_thread, dhd,
						"_dhd_sysioc");
		if (IS_ERR(dhd->sysioc_tsk)) {
			printk(KERN_WARNING
				"_dhd_sysioc thread failed to start\n");
			dhd->sysioc_tsk = NULL;
		}
	} else
		dhd->sysioc_tsk = NULL;

	/*
	 * Save the dhd_info into the priv
	 */
	memcpy(netdev_priv(net), &dhd, sizeof(dhd));

#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	g_bus = bus;
#endif
#if defined(CONFIG_PM_SLEEP)
	register_pm_notifier(&dhd_sleep_pm_notifier);
#endif	/* defined(CONFIG_PM_SLEEP) */
	/* && defined(DHD_GPL) */
	/* Init lock suspend to prevent kernel going to suspend */
	WAKE_LOCK_INIT(&dhd->pub, WAKE_LOCK_TMOUT, "dhd_wake_lock");
	WAKE_LOCK_INIT(&dhd->pub, WAKE_LOCK_LINK_DOWN_TMOUT,
		       "dhd_wake_lock_link_dw_event");
	WAKE_LOCK_INIT(&dhd->pub, WAKE_LOCK_PNO_FIND_TMOUT,
		       "dhd_wake_lock_link_pno_find_event");
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

int dhd_bus_start(dhd_pub_t *dhdp)
{
	int ret = -1;
	dhd_info_t *dhd = (dhd_info_t *) dhdp->info;
#ifdef EMBEDDED_PLATFORM
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" +
						 '\0' + bitvec  */
#endif				/* EMBEDDED_PLATFORM */

	ASSERT(dhd);

	DHD_TRACE(("%s:\n", __func__));

	/* try to download image and nvram to the dongle */
	if (dhd->pub.busstate == DHD_BUS_DOWN) {
		WAKE_LOCK_INIT(dhdp, WAKE_LOCK_DOWNLOAD, "dhd_bus_start");
		WAKE_LOCK(dhdp, WAKE_LOCK_DOWNLOAD);
		if (!(dhd_bus_download_firmware(dhd->pub.bus, dhd->pub.osh,
						fw_path, nv_path))) {
			DHD_ERROR(("%s: dhdsdio_probe_download failed. "
				"firmware = %s nvram = %s\n",
				__func__, fw_path, nv_path));
			WAKE_UNLOCK(dhdp, WAKE_LOCK_DOWNLOAD);
			WAKE_LOCK_DESTROY(dhdp, WAKE_LOCK_DOWNLOAD);
			return -1;
		}

		WAKE_UNLOCK(dhdp, WAKE_LOCK_DOWNLOAD);
		WAKE_LOCK_DESTROY(dhdp, WAKE_LOCK_DOWNLOAD);
	}

	/* Start the watchdog timer */
	dhd->pub.tickcnt = 0;
	dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);

	/* Bring up the bus */
	ret = dhd_bus_init(&dhd->pub, true);
	if (ret != 0) {
		DHD_ERROR(("%s, dhd_bus_init failed %d\n", __func__, ret));
		return ret;
	}
#if defined(OOB_INTR_ONLY)
	/* Host registration for OOB interrupt */
	if (bcmsdh_register_oob_intr(dhdp)) {
		del_timer_sync(&dhd->timer);
		dhd->wd_timer_valid = false;
		DHD_ERROR(("%s Host failed to resgister for OOB\n", __func__));
		return -ENODEV;
	}

	/* Enable oob at firmware */
	dhd_enable_oob_intr(dhd->pub.bus, true);
#endif				/* defined(OOB_INTR_ONLY) */

	/* If bus is not ready, can't come up */
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		del_timer_sync(&dhd->timer);
		dhd->wd_timer_valid = false;
		DHD_ERROR(("%s failed bus is not ready\n", __func__));
		return -ENODEV;
	}
#ifdef EMBEDDED_PLATFORM
	bcm_mkiovar("event_msgs", dhdp->eventmask, WL_EVENTING_MASK_LEN, iovbuf,
		    sizeof(iovbuf));
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
#ifdef PNO_SUPPORT
	setbit(dhdp->eventmask, WLC_E_PFN_NET_FOUND);
#endif				/* PNO_SUPPORT */

/* enable dongle roaming event */

	dhdp->pktfilter_count = 1;
	/* Setup filter to allow only unicast */
	dhdp->pktfilter[0] = "100 0 0 0 0x01 0x00";
#endif				/* EMBEDDED_PLATFORM */

	/* Bus is ready, do any protocol initialization */
	ret = dhd_prot_init(&dhd->pub);
	if (ret < 0)
		return ret;

	return 0;
}

int
dhd_iovar(dhd_pub_t *pub, int ifidx, char *name, char *cmd_buf, uint cmd_len,
	  int set)
{
	char buf[strlen(name) + 1 + cmd_len];
	int len = sizeof(buf);
	wl_ioctl_t ioc;
	int ret;

	len = bcm_mkiovar(name, cmd_buf, cmd_len, buf, len);

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = set ? WLC_SET_VAR : WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = set;

	ret = dhd_prot_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (!set && ret >= 0)
		memcpy(cmd_buf, buf, cmd_len);

	return ret;
}

static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_open,
	.ndo_stop = dhd_stop,
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
	.ndo_set_multicast_list = dhd_set_multicast_list
};

static struct net_device_ops dhd_ops_virt = {
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
	.ndo_set_multicast_list = dhd_set_multicast_list
};

int dhd_net_attach(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *) dhdp->info;
	struct net_device *net;
	u8 temp_addr[ETHER_ADDR_LEN] = {
		0x00, 0x90, 0x4c, 0x11, 0x22, 0x33};

	DHD_TRACE(("%s: ifidx %d\n", __func__, ifidx));

	ASSERT(dhd && dhd->iflist[ifidx]);

	net = dhd->iflist[ifidx]->net;
	ASSERT(net);

	ASSERT(!net->netdev_ops);
	net->netdev_ops = &dhd_ops_pri;

	/*
	 * We have to use the primary MAC for virtual interfaces
	 */
	if (ifidx != 0) {
		/* for virtual interfaces use the primary MAC  */
		memcpy(temp_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);

	}

	if (ifidx == 1) {
		DHD_TRACE(("%s ACCESS POINT MAC: \n", __func__));
		/*  ACCESSPOINT INTERFACE CASE */
		temp_addr[0] |= 0X02;	/* set bit 2 ,
			 - Locally Administered address  */

	}
	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
	net->ethtool_ops = &dhd_ethtool_ops;

#if defined(CONFIG_WIRELESS_EXT)
	if (!IS_CFG80211_FAVORITE()) {
#if WIRELESS_EXT < 19
		net->get_wireless_stats = dhd_get_wireless_stats;
#endif				/* WIRELESS_EXT < 19 */
#if WIRELESS_EXT > 12
		net->wireless_handlers =
		    (struct iw_handler_def *)&wl_iw_handler_def;
#endif				/* WIRELESS_EXT > 12 */
	}
#endif				/* defined(CONFIG_WIRELESS_EXT) */

	dhd->pub.rxsz = net->mtu + net->hard_header_len + dhd->pub.hdrlen;

	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	if (register_netdev(net) != 0) {
		DHD_ERROR(("%s: couldn't register the net device\n",
			__func__));
		goto fail;
	}

	printf("%s: Broadcom Dongle Host Driver\n", net->name);

	return 0;

fail:
	net->netdev_ops = NULL;
	return BCME_ERROR;
}

void dhd_bus_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (dhdp) {
		dhd = (dhd_info_t *) dhdp->info;
		if (dhd) {
			/* Stop the protocol module */
			dhd_prot_stop(&dhd->pub);

			/* Stop the bus module */
			dhd_bus_stop(dhd->pub.bus, true);
#if defined(OOB_INTR_ONLY)
			bcmsdh_unregister_oob_intr();
#endif				/* defined(OOB_INTR_ONLY) */

			/* Clear the watchdog timer */
			del_timer_sync(&dhd->timer);
			dhd->wd_timer_valid = false;
		}
	}
}

void dhd_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (dhdp) {
		dhd = (dhd_info_t *) dhdp->info;
		if (dhd) {
			dhd_if_t *ifp;
			int i;

#if defined(CONFIG_HAS_EARLYSUSPEND)
			if (dhd->early_suspend.suspend)
				unregister_early_suspend(&dhd->early_suspend);
#endif				/* defined(CONFIG_HAS_EARLYSUSPEND) */

			for (i = 1; i < DHD_MAX_IFS; i++)
				if (dhd->iflist[i])
					dhd_del_if(dhd, i);

			ifp = dhd->iflist[0];
			ASSERT(ifp);
			if (ifp->net->netdev_ops == &dhd_ops_pri) {
				dhd_stop(ifp->net);
				unregister_netdev(ifp->net);
			}

			if (dhd->watchdog_tsk) {
				send_sig(SIGTERM, dhd->watchdog_tsk, 1);
				kthread_stop(dhd->watchdog_tsk);
				dhd->watchdog_tsk = NULL;
			}

			if (dhd->dpc_tsk) {
				send_sig(SIGTERM, dhd->dpc_tsk, 1);
				kthread_stop(dhd->dpc_tsk);
				dhd->dpc_tsk = NULL;
			} else
				tasklet_kill(&dhd->tasklet);

			if (dhd->sysioc_tsk) {
				send_sig(SIGTERM, dhd->sysioc_tsk, 1);
				kthread_stop(dhd->sysioc_tsk);
				dhd->sysioc_tsk = NULL;
			}

			dhd_bus_detach(dhdp);

			if (dhdp->prot)
				dhd_prot_detach(dhdp);

#if defined(CONFIG_WIRELESS_EXT)
			wl_iw_detach();
#endif				/* (CONFIG_WIRELESS_EXT) */

			if (IS_CFG80211_FAVORITE())
				wl_cfg80211_detach();

#if defined(CONFIG_PM_SLEEP)
			unregister_pm_notifier(&dhd_sleep_pm_notifier);
#endif	/* defined(CONFIG_PM_SLEEP) */
			/* && defined(DHD_GPL) */
			WAKE_LOCK_DESTROY(dhdp, WAKE_LOCK_TMOUT);
			WAKE_LOCK_DESTROY(dhdp, WAKE_LOCK_LINK_DOWN_TMOUT);
			WAKE_LOCK_DESTROY(dhdp, WAKE_LOCK_PNO_FIND_TMOUT);
			free_netdev(ifp->net);
			kfree(ifp);
			kfree(dhd);
		}
	}
}

static void __exit dhd_module_cleanup(void)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	dhd_bus_unregister();
#if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC)
	wifi_del_dev();
#endif
	/* Call customer gpio to turn off power with WL_REG_ON signal */
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
}

static int __init dhd_module_init(void)
{
	int error;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Sanity check on the module parameters */
	do {
		/* Both watchdog and DPC as tasklets are ok */
		if ((dhd_watchdog_prio < 0) && (dhd_dpc_prio < 0))
			break;

		/* If both watchdog and DPC are threads, TX must be deferred */
		if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0)
		    && dhd_deferred_tx)
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
		DHD_ERROR(("%s: platform_driver_register failed\n", __func__));
		goto faild;
	}

	/* Waiting callback after platform_driver_register is done or
		 exit with error */
	if (down_timeout(&wifi_control_sem, msecs_to_jiffies(1000)) != 0) {
		printk(KERN_ERR "%s: platform_driver_register timeout\n",
			__func__);
		/* remove device */
		wifi_del_dev();
		goto faild;
	}
#endif	/* #if defined(CUSTOMER_HW2) && defined(CONFIG_WIFI_CONTROL_FUNC) */

	error = dhd_bus_register();

	if (!error)
		printf("\n%s\n", dhd_version);
	else {
		DHD_ERROR(("%s: sdio_register_driver failed\n", __func__));
		goto faild;
	}
	return error;

faild:
	/* turn off power and exit */
	dhd_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
	return -EINVAL;
}

module_init(dhd_module_init);
module_exit(dhd_module_cleanup);

/*
 * OS specific functions required to implement DHD driver in OS independent way
 */
int dhd_os_proto_block(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *) (pub->info);

	if (dhd) {
		down(&dhd->proto_sem);
		return 1;
	}
	return 0;
}

int dhd_os_proto_unblock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *) (pub->info);

	if (dhd) {
		up(&dhd->proto_sem);
		return 1;
	}

	return 0;
}

unsigned int dhd_os_get_ioctl_resp_timeout(void)
{
	return (unsigned int)dhd_ioctl_timeout_msec;
}

void dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec)
{
	dhd_ioctl_timeout_msec = (int)timeout_msec;
}

int dhd_os_ioctl_resp_wait(dhd_pub_t *pub, uint *condition, bool *pending)
{
	dhd_info_t *dhd = (dhd_info_t *) (pub->info);
	DECLARE_WAITQUEUE(wait, current);
	int timeout = dhd_ioctl_timeout_msec;

	/* Convert timeout in millsecond to jiffies */
	timeout = timeout * HZ / 1000;

	/* Wait until control frame is available */
	add_wait_queue(&dhd->ioctl_resp_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (!(*condition) && (!signal_pending(current) && timeout))
		timeout = schedule_timeout(timeout);

	if (signal_pending(current))
		*pending = true;

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dhd->ioctl_resp_wait, &wait);

	return timeout;
}

int dhd_os_ioctl_resp_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *) (pub->info);

	if (waitqueue_active(&dhd->ioctl_resp_wait))
		wake_up_interruptible(&dhd->ioctl_resp_wait);

	return 0;
}

void dhd_os_wd_timer(void *bus, uint wdtick)
{
	dhd_pub_t *pub = bus;
	static uint save_dhd_watchdog_ms;
	dhd_info_t *dhd = (dhd_info_t *) pub->info;

	/* don't start the wd until fw is loaded */
	if (pub->busstate == DHD_BUS_DOWN)
		return;

	/* Totally stop the timer */
	if (!wdtick && dhd->wd_timer_valid == true) {
		del_timer_sync(&dhd->timer);
		dhd->wd_timer_valid = false;
		save_dhd_watchdog_ms = wdtick;
		return;
	}

	if (wdtick) {
		dhd_watchdog_ms = (uint) wdtick;

		if (save_dhd_watchdog_ms != dhd_watchdog_ms) {

			if (dhd->wd_timer_valid == true)
				/* Stop timer and restart at new value */
				del_timer_sync(&dhd->timer);

			/* Create timer again when watchdog period is
			   dynamically changed or in the first instance
			 */
			dhd->timer.expires =
			    jiffies + dhd_watchdog_ms * HZ / 1000;
			add_timer(&dhd->timer);

		} else {
			/* Re arm the timer, at last watchdog period */
			mod_timer(&dhd->timer,
				  jiffies + dhd_watchdog_ms * HZ / 1000);
		}

		dhd->wd_timer_valid = true;
		save_dhd_watchdog_ms = wdtick;
	}
}

void *dhd_os_open_image(char *filename)
{
	struct file *fp;

	if (IS_CFG80211_FAVORITE() && !NO_FW_REQ())
		return wl_cfg80211_request_fw(filename);

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

int dhd_os_get_image_block(char *buf, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rdlen;

	if (IS_CFG80211_FAVORITE() && !NO_FW_REQ())
		return wl_cfg80211_read_fw(buf, len);

	if (!image)
		return 0;

	rdlen = kernel_read(fp, fp->f_pos, buf, len);
	if (rdlen > 0)
		fp->f_pos += rdlen;

	return rdlen;
}

void dhd_os_close_image(void *image)
{
	if (IS_CFG80211_FAVORITE() && !NO_FW_REQ())
		return wl_cfg80211_release_fw();
	if (image)
		filp_close((struct file *)image, NULL);
}

void dhd_os_sdlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *) (pub->info);

	if (dhd->threads_only)
		down(&dhd->sdsem);
	else
		spin_lock_bh(&dhd->sdlock);
}

void dhd_os_sdunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *) (pub->info);

	if (dhd->threads_only)
		up(&dhd->sdsem);
	else
		spin_unlock_bh(&dhd->sdlock);
}

void dhd_os_sdlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *) (pub->info);
	spin_lock_bh(&dhd->txqlock);
}

void dhd_os_sdunlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *) (pub->info);
	spin_unlock_bh(&dhd->txqlock);
}

void dhd_os_sdlock_rxq(dhd_pub_t *pub)
{
}

void dhd_os_sdunlock_rxq(dhd_pub_t *pub)
{
}

void dhd_os_sdtxlock(dhd_pub_t *pub)
{
	dhd_os_sdlock(pub);
}

void dhd_os_sdtxunlock(dhd_pub_t *pub)
{
	dhd_os_sdunlock(pub);
}

#if defined(CONFIG_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev)
{
	int res = 0;
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(dev);

	res = wl_iw_get_wireless_stats(dev, &dhd->iw.wstats);

	if (res == 0)
		return &dhd->iw.wstats;
	else
		return NULL;
}
#endif	/* defined(CONFIG_WIRELESS_EXT) */

static int
dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
		  wl_event_msg_t *event, void **data)
{
	int bcmerror = 0;

	ASSERT(dhd != NULL);

	bcmerror = wl_host_event(dhd, ifidx, pktdata, event, data);
	if (bcmerror != BCME_OK)
		return bcmerror;

#if defined(CONFIG_WIRELESS_EXT)
	if (!IS_CFG80211_FAVORITE()) {
		if ((dhd->iflist[*ifidx] == NULL)
		    || (dhd->iflist[*ifidx]->net == NULL)) {
			DHD_ERROR(("%s Exit null pointer\n", __func__));
			return bcmerror;
		}

		if (dhd->iflist[*ifidx]->net)
			wl_iw_event(dhd->iflist[*ifidx]->net, event, *data);
	}
#endif				/* defined(CONFIG_WIRELESS_EXT)  */

	if (IS_CFG80211_FAVORITE()) {
		ASSERT(dhd->iflist[*ifidx] != NULL);
		ASSERT(dhd->iflist[*ifidx]->net != NULL);
		if (dhd->iflist[*ifidx]->net)
			wl_cfg80211_event(dhd->iflist[*ifidx]->net, event,
					  *data);
	}

	return bcmerror;
}

/* send up locally generated event */
void dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data)
{
	switch (ntoh32(event->event_type)) {
	default:
		break;
	}
}

void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar)
{
	struct dhd_info *dhdinfo = dhd->info;
	dhd_os_sdunlock(dhd);
	wait_event_interruptible_timeout(dhdinfo->ctrl_wait,
					 (*lockvar == false), HZ * 2);
	dhd_os_sdlock(dhd);
	return;
}

void dhd_wait_event_wakeup(dhd_pub_t *dhd)
{
	struct dhd_info *dhdinfo = dhd->info;
	if (waitqueue_active(&dhdinfo->ctrl_wait))
		wake_up_interruptible(&dhdinfo->ctrl_wait);
	return;
}

int dhd_dev_reset(struct net_device *dev, u8 flag)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	/* Turning off watchdog */
	if (flag)
		dhd_os_wd_timer(&dhd->pub, 0);

	dhd_bus_devreset(&dhd->pub, flag);

	/* Turning on watchdog back */
	if (!flag)
		dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);
	DHD_ERROR(("%s:  WLAN OFF DONE\n", __func__));

	return 1;
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
#endif		/* defined(CONFIG_HAS_EARLYSUSPEND) */
	return ret;
}

int net_os_set_dtim_skip(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(dev);

	if (dhd)
		dhd->pub.dtim_skip = val;

	return 0;
}

int net_os_set_packet_filter(struct net_device *dev, int val)
{
	dhd_info_t *dhd = *(dhd_info_t **) netdev_priv(dev);
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

void dhd_dev_init_ioctl(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	dhd_preinit_ioctls(&dhd->pub);
}

#ifdef PNO_SUPPORT
/* Linux wrapper to call common dhd_pno_clean */
int dhd_dev_pno_reset(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return dhd_pno_clean(&dhd->pub);
}

/* Linux wrapper to call common dhd_pno_enable */
int dhd_dev_pno_enable(struct net_device *dev, int pfn_enabled)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return dhd_pno_enable(&dhd->pub, pfn_enabled);
}

/* Linux wrapper to call common dhd_pno_set */
int
dhd_dev_pno_set(struct net_device *dev, wlc_ssid_t *ssids_local, int nssid,
		unsigned char scan_fr)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return dhd_pno_set(&dhd->pub, ssids_local, nssid, scan_fr);
}

/* Linux wrapper to get  pno status */
int dhd_dev_get_pno_status(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return dhd_pno_get_status(&dhd->pub);
}

#endif				/* PNO_SUPPORT */

static int dhd_get_pend_8021x_cnt(dhd_info_t *dhd)
{
	return atomic_read(&dhd->pend_8021x_cnt);
}

#define MAX_WAIT_FOR_8021X_TX	10

int dhd_wait_pend8021x(struct net_device *dev)
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
int write_to_file(dhd_pub_t *dhd, u8 *buf, int size)
{
	int ret = 0;
	struct file *fp;
	mm_segment_t old_fs;
	loff_t pos = 0;

	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* open file to write */
	fp = filp_open("/tmp/mem_dump", O_WRONLY | O_CREAT, 0640);
	if (!fp) {
		printf("%s: open file error\n", __func__);
		ret = -1;
		goto exit;
	}

	/* Write buf to file */
	fp->f_op->write(fp, buf, size, &pos);

exit:
	/* free buf before return */
	kfree(buf);
	/* close file before return */
	if (fp)
		filp_close(fp, current->files);
	/* restore previous address limit */
	set_fs(old_fs);

	return ret;
}
#endif				/* DHD_DEBUG */
