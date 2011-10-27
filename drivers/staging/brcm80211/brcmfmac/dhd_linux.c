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
#include <linux/interrupt.h>
#include <linux/hardirq.h>
#include <net/cfg80211.h>
#include <defs.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include "wl_cfg80211.h"
#include "bcmchip.h"

#if defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
atomic_t brcmf_mmc_suspend;
#endif	/*  defined(CONFIG_PM_SLEEP) */

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11n wireless LAN fullmac driver.");
MODULE_SUPPORTED_DEVICE("Broadcom 802.11n WLAN fullmac cards");
MODULE_LICENSE("Dual BSD/GPL");


/* Interface control information */
struct brcmf_if {
	struct brcmf_info *info;	/* back pointer to brcmf_info */
	/* OS/stack specifics */
	struct net_device *net;
	struct net_device_stats stats;
	int idx;		/* iface idx in dongle */
	int state;		/* interface state */
	uint subunit;		/* subunit */
	u8 mac_addr[ETH_ALEN];	/* assigned MAC address */
	bool attached;		/* Delayed attachment when unset */
	bool txflowcontrol;	/* Per interface flow control indicator */
	char name[IFNAMSIZ];	/* linux interface name */
};

/* Local private structure (extension of pub) */
struct brcmf_info {
	struct brcmf_pub pub;

	/* OS/stack specifics */
	struct brcmf_if *iflist[BRCMF_MAX_IFS];

	struct semaphore proto_sem;
	wait_queue_head_t ioctl_resp_wait;

	/* Thread to issue ioctl for multicast */
	struct task_struct *sysioc_tsk;
	struct semaphore sysioc_sem;
	bool set_multicast;
	bool set_macaddress;
	u8 macvalue[ETH_ALEN];
	atomic_t pend_8021x_cnt;
};

/* Error bits */
module_param(brcmf_msg_level, int, 0);

/* Spawn a thread for system ioctls (set mac, set mcast) */
uint brcmf_sysioc = true;
module_param(brcmf_sysioc, uint, 0);

/* ARP offload agent mode : Enable ARP Host Auto-Reply
and ARP Peer Auto-Reply */
uint brcmf_arp_mode = 0xb;
module_param(brcmf_arp_mode, uint, 0);

/* ARP offload enable */
uint brcmf_arp_enable = true;
module_param(brcmf_arp_enable, uint, 0);

/* Global Pkt filter enable control */
uint brcmf_pkt_filter_enable = true;
module_param(brcmf_pkt_filter_enable, uint, 0);

/*  Pkt filter init setup */
uint brcmf_pkt_filter_init;
module_param(brcmf_pkt_filter_init, uint, 0);

/* Pkt filter mode control */
uint brcmf_master_mode = true;
module_param(brcmf_master_mode, uint, 0);

module_param(brcmf_dongle_memsize, int, 0);

/* Contorl fw roaming */
uint brcmf_roam = 1;

/* Control radio state */
uint brcmf_radio_up = 1;

/* Network inteface name */
char iface_name[IFNAMSIZ] = "wlan";
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);

/* The following are specific to the SDIO dongle */

/* IOCTL response timeout */
int brcmf_ioctl_timeout_msec = IOCTL_RESP_TIMEOUT;

/* Idle timeout for backplane clock */
int brcmf_idletime = BRCMF_IDLETIME_TICKS;
module_param(brcmf_idletime, int, 0);

/* Use polling */
uint brcmf_poll;
module_param(brcmf_poll, uint, 0);

/* Use interrupts */
uint brcmf_intr = true;
module_param(brcmf_intr, uint, 0);

/* SDIO Drive Strength (in milliamps) */
uint brcmf_sdiod_drive_strength = 6;
module_param(brcmf_sdiod_drive_strength, uint, 0);

/* Tx/Rx bounds */
module_param(brcmf_txbound, uint, 0);
module_param(brcmf_rxbound, uint, 0);

#ifdef SDTEST
/* Echo packet generator (pkts/s) */
uint brcmf_pktgen;
module_param(brcmf_pktgen, uint, 0);

/* Echo packet len (0 => sawtooth, max 2040) */
uint brcmf_pktgen_len;
module_param(brcmf_pktgen_len, uint, 0);
#endif

static int brcmf_toe_get(struct brcmf_info *drvr_priv, int idx, u32 *toe_ol);
static int brcmf_toe_set(struct brcmf_info *drvr_priv, int idx, u32 toe_ol);
static int brcmf_host_event(struct brcmf_info *drvr_priv, int *ifidx, void *pktdata,
			    struct brcmf_event_msg *event_ptr,
			    void **data_ptr);

/*
 * Generalized timeout mechanism.  Uses spin sleep with exponential
 * back-off until
 * the sleep time reaches one jiffy, then switches over to task delay.  Usage:
 *
 *      brcmf_timeout_start(&tmo, usec);
 *      while (!brcmf_timeout_expired(&tmo))
 *              if (poll_something())
 *                      break;
 *      if (brcmf_timeout_expired(&tmo))
 *              fatal();
 */

void brcmf_timeout_start(struct brcmf_timeout *tmo, uint usec)
{
	tmo->limit = usec;
	tmo->increment = 0;
	tmo->elapsed = 0;
	tmo->tick = 1000000 / HZ;
}

int brcmf_timeout_expired(struct brcmf_timeout *tmo)
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

static int brcmf_net2idx(struct brcmf_info *drvr_priv, struct net_device *net)
{
	int i = 0;

	while (i < BRCMF_MAX_IFS) {
		if (drvr_priv->iflist[i] && (drvr_priv->iflist[i]->net == net))
			return i;
		i++;
	}

	return BRCMF_BAD_IF;
}

int brcmf_ifname2idx(struct brcmf_info *drvr_priv, char *name)
{
	int i = BRCMF_MAX_IFS;

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0)
		if (drvr_priv->iflist[i]
		    && !strncmp(drvr_priv->iflist[i]->name, name, IFNAMSIZ))
			break;

	BRCMF_TRACE(("%s: return idx %d for \"%s\"\n", __func__, i, name));

	return i;		/* default - the primary interface */
}

char *brcmf_ifname(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_info *drvr_priv = drvr->info;

	if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
		BRCMF_ERROR(("%s: ifidx %d out of range\n", __func__, ifidx));
		return "<if_bad>";
	}

	if (drvr_priv->iflist[ifidx] == NULL) {
		BRCMF_ERROR(("%s: null i/f %d\n", __func__, ifidx));
		return "<if_null>";
	}

	if (drvr_priv->iflist[ifidx]->net)
		return drvr_priv->iflist[ifidx]->net->name;

	return "<if_none>";
}

static void _brcmf_set_multicast_list(struct brcmf_info *drvr_priv, int ifidx)
{
	struct net_device *dev;
	struct netdev_hw_addr *ha;
	u32 allmulti, cnt;

	struct brcmf_ioctl ioc;
	char *buf, *bufp;
	uint buflen;
	int ret;

	dev = drvr_priv->iflist[ifidx]->net;
	cnt = netdev_mc_count(dev);

	/* Determine initial value of allmulti flag */
	allmulti = (dev->flags & IFF_ALLMULTI) ? true : false;

	/* Send down the multicast list first. */

	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETH_ALEN);
	bufp = buf = kmalloc(buflen, GFP_ATOMIC);
	if (!bufp) {
		BRCMF_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx), cnt));
		return;
	}

	strcpy(bufp, "mcast_list");
	bufp += strlen("mcast_list") + 1;

	cnt = cpu_to_le32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);

	netdev_for_each_mc_addr(ha, dev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETH_ALEN);
		bufp += ETH_ALEN;
		cnt--;
	}

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = BRCMF_C_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = true;

	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		BRCMF_ERROR(("%s: set mcast_list failed, cnt %d\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx), cnt));
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
		BRCMF_ERROR(("%s: out of memory for allmulti\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx)));
		return;
	}
	allmulti = cpu_to_le32(allmulti);

	if (!brcmu_mkiovar
	    ("allmulti", (void *)&allmulti, sizeof(allmulti), buf, buflen)) {
		BRCMF_ERROR(("%s: mkiovar failed for allmulti, datalen %d "
			     "buflen %u\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx),
			     (int)sizeof(allmulti), buflen));
		kfree(buf);
		return;
	}

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = BRCMF_C_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = true;

	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		BRCMF_ERROR(("%s: set allmulti %d failed\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx),
			     le32_to_cpu(allmulti)));
	}

	kfree(buf);

	/* Finally, pick up the PROMISC flag as well, like the NIC
		 driver does */

	allmulti = (dev->flags & IFF_PROMISC) ? true : false;
	allmulti = cpu_to_le32(allmulti);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = BRCMF_C_SET_PROMISC;
	ioc.buf = &allmulti;
	ioc.len = sizeof(allmulti);
	ioc.set = true;

	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		BRCMF_ERROR(("%s: set promisc %d failed\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx),
			     le32_to_cpu(allmulti)));
	}
}

static int _brcmf_set_mac_address(struct brcmf_info *drvr_priv, int ifidx, u8 *addr)
{
	char buf[32];
	struct brcmf_ioctl ioc;
	int ret;

	BRCMF_TRACE(("%s enter\n", __func__));
	if (!brcmu_mkiovar
	    ("cur_etheraddr", (char *)addr, ETH_ALEN, buf, 32)) {
		BRCMF_ERROR(("%s: mkiovar failed for cur_etheraddr\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx)));
		return -1;
	}
	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = BRCMF_C_SET_VAR;
	ioc.buf = buf;
	ioc.len = 32;
	ioc.set = true;

	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		BRCMF_ERROR(("%s: set cur_etheraddr failed\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx)));
	} else {
		memcpy(drvr_priv->iflist[ifidx]->net->dev_addr, addr, ETH_ALEN);
	}

	return ret;
}

#ifdef SOFTAP
extern struct net_device *ap_net_dev;
#endif

/* Virtual interfaces only ((ifp && ifp->info && ifp->idx == true) */
static void brcmf_op_if(struct brcmf_if *ifp)
{
	struct brcmf_info *drvr_priv;
	int ret = 0, err = 0;

	drvr_priv = ifp->info;

	BRCMF_TRACE(("%s: idx %d, state %d\n", __func__, ifp->idx, ifp->state));

	switch (ifp->state) {
	case BRCMF_E_IF_ADD:
		/*
		 * Delete the existing interface before overwriting it
		 * in case we missed the BRCMF_E_IF_DEL event.
		 */
		if (ifp->net != NULL) {
			BRCMF_ERROR(("%s: ERROR: netdev:%s already exists, "
				     "try free & unregister\n",
				     __func__, ifp->net->name));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			free_netdev(ifp->net);
		}
		/* Allocate etherdev, including space for private structure */
		ifp->net = alloc_etherdev(sizeof(drvr_priv));
		if (!ifp->net) {
			BRCMF_ERROR(("%s: OOM - alloc_etherdev\n", __func__));
			ret = -ENOMEM;
		}
		if (ret == 0) {
			strcpy(ifp->net->name, ifp->name);
			memcpy(netdev_priv(ifp->net), &drvr_priv, sizeof(drvr_priv));
			err = brcmf_net_attach(&drvr_priv->pub, ifp->idx);
			if (err != 0) {
				BRCMF_ERROR(("%s: brcmf_net_attach failed, "
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
				BRCMF_TRACE(("\n ==== pid:%x, net_device for "
					     "if:%s created ===\n\n",
					     current->pid, ifp->net->name));
				ifp->state = 0;
			}
		}
		break;
	case BRCMF_E_IF_DEL:
		if (ifp->net != NULL) {
			BRCMF_TRACE(("\n%s: got 'WLC_E_IF_DEL' state\n",
				     __func__));
			netif_stop_queue(ifp->net);
			unregister_netdev(ifp->net);
			ret = BRCMF_DEL_IF;	/* Make sure the free_netdev()
							 is called */
		}
		break;
	default:
		BRCMF_ERROR(("%s: bad op %d\n", __func__, ifp->state));
		break;
	}

	if (ret < 0) {
		if (ifp->net)
			free_netdev(ifp->net);

		drvr_priv->iflist[ifp->idx] = NULL;
		kfree(ifp);
#ifdef SOFTAP
		if (ifp->net == ap_net_dev)
			ap_net_dev = NULL;	/*  NULL  SOFTAP global
							 wl0.1 as well */
#endif				/*  SOFTAP */
	}
}

static int _brcmf_sysioc_thread(void *data)
{
	struct brcmf_info *drvr_priv = (struct brcmf_info *) data;
	int i;
#ifdef SOFTAP
	bool in_ap = false;
#endif

	allow_signal(SIGTERM);

	while (down_interruptible(&drvr_priv->sysioc_sem) == 0) {
		if (kthread_should_stop())
			break;
		for (i = 0; i < BRCMF_MAX_IFS; i++) {
			struct brcmf_if *ifentry = drvr_priv->iflist[i];
			if (ifentry) {
#ifdef SOFTAP
				in_ap = (ap_net_dev != NULL);
#endif				/* SOFTAP */
				if (ifentry->state)
					brcmf_op_if(ifentry);
#ifdef SOFTAP
				if (drvr_priv->iflist[i] == NULL) {
					BRCMF_TRACE(("\n\n %s: interface %d "
						     "removed!\n", __func__,
						     i));
					continue;
				}

				if (in_ap && drvr_priv->set_macaddress) {
					BRCMF_TRACE(("attempt to set MAC for"
						     " %s in AP Mode,"
						     " blocked.\n",
						     ifentry->net->name));
					drvr_priv->set_macaddress = false;
					continue;
				}

				if (in_ap && drvr_priv->set_multicast) {
					BRCMF_TRACE(("attempt to set MULTICAST "
						     "list for %s in AP Mode, "
						     "blocked.\n",
						     ifentry->net->name));
					drvr_priv->set_multicast = false;
					continue;
				}
#endif				/* SOFTAP */
				if (drvr_priv->set_multicast) {
					drvr_priv->set_multicast = false;
					_brcmf_set_multicast_list(drvr_priv, i);
				}
				if (drvr_priv->set_macaddress) {
					drvr_priv->set_macaddress = false;
					_brcmf_set_mac_address(drvr_priv, i,
						drvr_priv->macvalue);
				}
			}
		}
	}
	return 0;
}

static int brcmf_netdev_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;

	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;

	ifidx = brcmf_net2idx(drvr_priv, dev);
	if (ifidx == BRCMF_BAD_IF)
		return -1;

	memcpy(&drvr_priv->macvalue, sa->sa_data, ETH_ALEN);
	drvr_priv->set_macaddress = true;
	up(&drvr_priv->sysioc_sem);

	return ret;
}

static void brcmf_netdev_set_multicast_list(struct net_device *dev)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(dev);
	int ifidx;

	ifidx = brcmf_net2idx(drvr_priv, dev);
	if (ifidx == BRCMF_BAD_IF)
		return;

	drvr_priv->set_multicast = true;
	up(&drvr_priv->sysioc_sem);
}

int brcmf_sendpkt(struct brcmf_pub *drvr, int ifidx, struct sk_buff *pktbuf)
{
	struct brcmf_info *drvr_priv = drvr->info;

	/* Reject if down */
	if (!drvr->up || (drvr->busstate == BRCMF_BUS_DOWN))
		return -ENODEV;

	/* Update multicast statistic */
	if (pktbuf->len >= ETH_ALEN) {
		u8 *pktdata = (u8 *) (pktbuf->data);
		struct ethhdr *eh = (struct ethhdr *)pktdata;

		if (is_multicast_ether_addr(eh->h_dest))
			drvr->tx_multicast++;
		if (ntohs(eh->h_proto) == ETH_P_PAE)
			atomic_inc(&drvr_priv->pend_8021x_cnt);
	}

	/* If the protocol uses a data header, apply it */
	brcmf_proto_hdrpush(drvr, ifidx, pktbuf);

	/* Use bus module to send data frame */
	return brcmf_sdbrcm_bus_txdata(drvr->bus, pktbuf);
}

static int brcmf_netdev_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(net);
	int ifidx;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	/* Reject if down */
	if (!drvr_priv->pub.up || (drvr_priv->pub.busstate == BRCMF_BUS_DOWN)) {
		BRCMF_ERROR(("%s: xmit rejected pub.up=%d busstate=%d\n",
			     __func__, drvr_priv->pub.up,
			     drvr_priv->pub.busstate));
		netif_stop_queue(net);
		return -ENODEV;
	}

	ifidx = brcmf_net2idx(drvr_priv, net);
	if (ifidx == BRCMF_BAD_IF) {
		BRCMF_ERROR(("%s: bad ifidx %d\n", __func__, ifidx));
		netif_stop_queue(net);
		return -ENODEV;
	}

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < drvr_priv->pub.hdrlen) {
		struct sk_buff *skb2;

		BRCMF_INFO(("%s: insufficient headroom\n",
			    brcmf_ifname(&drvr_priv->pub, ifidx)));
		drvr_priv->pub.tx_realloc++;
		skb2 = skb_realloc_headroom(skb, drvr_priv->pub.hdrlen);
		dev_kfree_skb(skb);
		skb = skb2;
		if (skb == NULL) {
			BRCMF_ERROR(("%s: skb_realloc_headroom failed\n",
				     brcmf_ifname(&drvr_priv->pub, ifidx)));
			ret = -ENOMEM;
			goto done;
		}
	}

	ret = brcmf_sendpkt(&drvr_priv->pub, ifidx, skb);

done:
	if (ret)
		drvr_priv->pub.dstats.tx_dropped++;
	else
		drvr_priv->pub.tx_packets++;

	/* Return ok: we always eat the packet */
	return 0;
}

void brcmf_txflowcontrol(struct brcmf_pub *drvr, int ifidx, bool state)
{
	struct net_device *net;
	struct brcmf_info *drvr_priv = drvr->info;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	drvr->txoff = state;
	net = drvr_priv->iflist[ifidx]->net;
	if (state == ON)
		netif_stop_queue(net);
	else
		netif_wake_queue(net);
}

void brcmf_rx_frame(struct brcmf_pub *drvr, int ifidx, struct sk_buff *skb,
		  int numpkt)
{
	struct brcmf_info *drvr_priv = drvr->info;
	unsigned char *eth;
	uint len;
	void *data;
	struct sk_buff *pnext, *save_pktbuf;
	int i;
	struct brcmf_if *ifp;
	struct brcmf_event_msg event;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	save_pktbuf = skb;

	for (i = 0; skb && i < numpkt; i++, skb = pnext) {

		pnext = skb->next;
		skb->next = NULL;

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

		ifp = drvr_priv->iflist[ifidx];
		if (ifp == NULL)
			ifp = drvr_priv->iflist[0];

		skb->dev = ifp->net;
		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST)
			drvr_priv->pub.rx_multicast++;

		skb->data = eth;
		skb->len = len;

		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* Process special event packets and then discard them */
		if (ntohs(skb->protocol) == ETH_P_LINK_CTL)
			brcmf_host_event(drvr_priv, &ifidx,
					  skb_mac_header(skb),
					  &event, &data);

		if (drvr_priv->iflist[ifidx] &&
		    !drvr_priv->iflist[ifidx]->state)
			ifp = drvr_priv->iflist[ifidx];

		if (ifp->net)
			ifp->net->last_rx = jiffies;

		drvr->dstats.rx_bytes += skb->len;
		drvr->rx_packets++;	/* Local count */

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

void brcmf_txcomplete(struct brcmf_pub *drvr, struct sk_buff *txp, bool success)
{
	uint ifidx;
	struct brcmf_info *drvr_priv = drvr->info;
	struct ethhdr *eh;
	u16 type;

	brcmf_proto_hdrpull(drvr, &ifidx, txp);

	eh = (struct ethhdr *)(txp->data);
	type = ntohs(eh->h_proto);

	if (type == ETH_P_PAE)
		atomic_dec(&drvr_priv->pend_8021x_cnt);

}

static struct net_device_stats *brcmf_netdev_get_stats(struct net_device *net)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(net);
	struct brcmf_if *ifp;
	int ifidx;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	ifidx = brcmf_net2idx(drvr_priv, net);
	if (ifidx == BRCMF_BAD_IF)
		return NULL;

	ifp = drvr_priv->iflist[ifidx];

	if (drvr_priv->pub.up) {
		/* Use the protocol to get dongle stats */
		brcmf_proto_dstats(&drvr_priv->pub);
	}

	/* Copy dongle stats to net device stats */
	ifp->stats.rx_packets = drvr_priv->pub.dstats.rx_packets;
	ifp->stats.tx_packets = drvr_priv->pub.dstats.tx_packets;
	ifp->stats.rx_bytes = drvr_priv->pub.dstats.rx_bytes;
	ifp->stats.tx_bytes = drvr_priv->pub.dstats.tx_bytes;
	ifp->stats.rx_errors = drvr_priv->pub.dstats.rx_errors;
	ifp->stats.tx_errors = drvr_priv->pub.dstats.tx_errors;
	ifp->stats.rx_dropped = drvr_priv->pub.dstats.rx_dropped;
	ifp->stats.tx_dropped = drvr_priv->pub.dstats.tx_dropped;
	ifp->stats.multicast = drvr_priv->pub.dstats.multicast;

	return &ifp->stats;
}

/* Retrieve current toe component enables, which are kept
	 as a bitmap in toe_ol iovar */
static int brcmf_toe_get(struct brcmf_info *drvr_priv, int ifidx, u32 *toe_ol)
{
	struct brcmf_ioctl ioc;
	char buf[32];
	int ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = BRCMF_C_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint) sizeof(buf);
	ioc.set = false;

	strcpy(buf, "toe_ol");
	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		/* Check for older dongle image that doesn't support toe_ol */
		if (ret == -EIO) {
			BRCMF_ERROR(("%s: toe not supported by device\n",
				     brcmf_ifname(&drvr_priv->pub, ifidx)));
			return -EOPNOTSUPP;
		}

		BRCMF_INFO(("%s: could not get toe_ol: ret=%d\n",
			    brcmf_ifname(&drvr_priv->pub, ifidx), ret));
		return ret;
	}

	memcpy(toe_ol, buf, sizeof(u32));
	return 0;
}

/* Set current toe component enables in toe_ol iovar,
	 and set toe global enable iovar */
static int brcmf_toe_set(struct brcmf_info *drvr_priv, int ifidx, u32 toe_ol)
{
	struct brcmf_ioctl ioc;
	char buf[32];
	int toe, ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = BRCMF_C_SET_VAR;
	ioc.buf = buf;
	ioc.len = (uint) sizeof(buf);
	ioc.set = true;

	/* Set toe_ol as requested */

	strcpy(buf, "toe_ol");
	memcpy(&buf[sizeof("toe_ol")], &toe_ol, sizeof(u32));

	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		BRCMF_ERROR(("%s: could not set toe_ol: ret=%d\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx), ret));
		return ret;
	}

	/* Enable toe globally only if any components are enabled. */

	toe = (toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe, sizeof(u32));

	ret = brcmf_proto_ioctl(&drvr_priv->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		BRCMF_ERROR(("%s: could not set toe: ret=%d\n",
			     brcmf_ifname(&drvr_priv->pub, ifidx), ret));
		return ret;
	}

	return 0;
}

static void brcmf_ethtool_get_drvinfo(struct net_device *net,
				    struct ethtool_drvinfo *info)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(net);

	sprintf(info->driver, KBUILD_MODNAME);
	sprintf(info->version, "%lu", drvr_priv->pub.drv_version);
	sprintf(info->fw_version, "%s", BCM4329_FW_NAME);
	sprintf(info->bus_info, "%s",
		dev_name(&brcmf_cfg80211_get_sdio_func()->dev));
}

struct ethtool_ops brcmf_ethtool_ops = {
	.get_drvinfo = brcmf_ethtool_get_drvinfo
};

static int brcmf_ethtool(struct brcmf_info *drvr_priv, void *uaddr)
{
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	u32 cmd;
	struct ethtool_value edata;
	u32 toe_cmpnt, csum_dir;
	int ret;

	BRCMF_TRACE(("%s: Enter\n", __func__));

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

		/* if requested, identify ourselves */
		if (strcmp(drvname, "?dhd") == 0) {
			sprintf(info.driver, "dhd");
			strcpy(info.version, BRCMF_VERSION_STR);
		}

		/* otherwise, require dongle to be up */
		else if (!drvr_priv->pub.up) {
			BRCMF_ERROR(("%s: dongle is not up\n", __func__));
			return -ENODEV;
		}

		/* finally, report dongle driver type */
		else if (drvr_priv->pub.iswl)
			sprintf(info.driver, "wl");
		else
			sprintf(info.driver, "xx");

		sprintf(info.version, "%lu", drvr_priv->pub.drv_version);
		if (copy_to_user(uaddr, &info, sizeof(info)))
			return -EFAULT;
		BRCMF_CTL(("%s: given %*s, returning %s\n", __func__,
			   (int)sizeof(drvname), drvname, info.driver));
		break;

		/* Get toe offload components from dongle */
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		ret = brcmf_toe_get(drvr_priv, 0, &toe_cmpnt);
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
		ret = brcmf_toe_get(drvr_priv, 0, &toe_cmpnt);
		if (ret < 0)
			return ret;

		csum_dir =
		    (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		ret = brcmf_toe_set(drvr_priv, 0, toe_cmpnt);
		if (ret < 0)
			return ret;

		/* If setting TX checksum mode, tell Linux the new mode */
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				drvr_priv->iflist[0]->net->features |=
				    NETIF_F_IP_CSUM;
			else
				drvr_priv->iflist[0]->net->features &=
				    ~NETIF_F_IP_CSUM;
		}

		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int brcmf_netdev_ioctl_entry(struct net_device *net, struct ifreq *ifr,
				    int cmd)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(net);
	struct brcmf_c_ioctl ioc;
	int bcmerror = 0;
	int buflen = 0;
	void *buf = NULL;
	uint driver = 0;
	int ifidx;
	bool is_set_key_cmd;

	ifidx = brcmf_net2idx(drvr_priv, net);
	BRCMF_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __func__, ifidx, cmd));

	if (ifidx == BRCMF_BAD_IF)
		return -1;

	if (cmd == SIOCETHTOOL)
		return brcmf_ethtool(drvr_priv, (void *)ifr->ifr_data);

	if (cmd != SIOCDEVPRIVATE)
		return -EOPNOTSUPP;

	memset(&ioc, 0, sizeof(ioc));

	/* Copy the ioc control structure part of ioctl request */
	if (copy_from_user(&ioc, ifr->ifr_data, sizeof(struct brcmf_ioctl))) {
		bcmerror = -EINVAL;
		goto done;
	}

	/* Copy out any buffer passed */
	if (ioc.buf) {
		buflen = min_t(int, ioc.len, BRCMF_IOCTL_MAXLEN);
		/* optimization for direct ioctl calls from kernel */
		/*
		   if (segment_eq(get_fs(), KERNEL_DS)) {
		   buf = ioc.buf;
		   } else {
		 */
		{
			buf = kmalloc(buflen, GFP_ATOMIC);
			if (!buf) {
				bcmerror = -ENOMEM;
				goto done;
			}
			if (copy_from_user(buf, ioc.buf, buflen)) {
				bcmerror = -EINVAL;
				goto done;
			}
		}
	}

	/* To differentiate read 4 more byes */
	if ((copy_from_user(&driver, (char *)ifr->ifr_data +
			    sizeof(struct brcmf_ioctl), sizeof(uint)) != 0)) {
		bcmerror = -EINVAL;
		goto done;
	}

	if (!capable(CAP_NET_ADMIN)) {
		bcmerror = -EPERM;
		goto done;
	}

	/* check for local brcmf ioctl and handle it */
	if (driver == BRCMF_IOCTL_MAGIC) {
		bcmerror = brcmf_c_ioctl((void *)&drvr_priv->pub, &ioc, buf, buflen);
		if (bcmerror)
			drvr_priv->pub.bcmerror = bcmerror;
		goto done;
	}

	/* send to dongle (must be up, and wl) */
	if ((drvr_priv->pub.busstate != BRCMF_BUS_DATA)) {
		BRCMF_ERROR(("%s DONGLE_DOWN,__func__\n", __func__));
		bcmerror = -EIO;
		goto done;
	}

	if (!drvr_priv->pub.iswl) {
		bcmerror = -EIO;
		goto done;
	}

	/*
	 * Intercept BRCMF_C_SET_KEY IOCTL - serialize M4 send and
	 * set key IOCTL to prevent M4 encryption.
	 */
	is_set_key_cmd = ((ioc.cmd == BRCMF_C_SET_KEY) ||
			  ((ioc.cmd == BRCMF_C_SET_VAR) &&
			   !(strncmp("wsec_key", ioc.buf, 9))) ||
			  ((ioc.cmd == BRCMF_C_SET_VAR) &&
			   !(strncmp("bsscfg:wsec_key", ioc.buf, 15))));
	if (is_set_key_cmd)
		brcmf_netdev_wait_pend8021x(net);

	bcmerror =
	    brcmf_proto_ioctl(&drvr_priv->pub, ifidx, (struct brcmf_ioctl *)&ioc,
			      buf, buflen);

done:
	if (!bcmerror && buf && ioc.buf) {
		if (copy_to_user(ioc.buf, buf, buflen))
			bcmerror = -EFAULT;
	}

	kfree(buf);

	if (bcmerror > 0)
		bcmerror = 0;

	return bcmerror;
}

static int brcmf_netdev_stop(struct net_device *net)
{
#if !defined(IGNORE_ETH0_DOWN)
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(net);

	BRCMF_TRACE(("%s: Enter\n", __func__));
	brcmf_cfg80211_down();
	if (drvr_priv->pub.up == 0)
		return 0;

	/* Set state and stop OS transmissions */
	drvr_priv->pub.up = 0;
	netif_stop_queue(net);
#else
	BRCMF_ERROR(("BYPASS %s:due to BRCM compilation: under investigation\n",
		     __func__));
#endif				/* !defined(IGNORE_ETH0_DOWN) */

	return 0;
}

static int brcmf_netdev_open(struct net_device *net)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **) netdev_priv(net);
	u32 toe_ol;
	int ifidx = brcmf_net2idx(drvr_priv, net);
	s32 ret = 0;

	BRCMF_TRACE(("%s: ifidx %d\n", __func__, ifidx));

	if (ifidx == 0) {	/* do it only for primary eth0 */

		/* try to bring up bus */
		ret = brcmf_bus_start(&drvr_priv->pub);
		if (ret != 0) {
			BRCMF_ERROR(("%s: failed with code %d\n",
				     __func__, ret));
			return -1;
		}
		atomic_set(&drvr_priv->pend_8021x_cnt, 0);

		memcpy(net->dev_addr, drvr_priv->pub.mac, ETH_ALEN);

		/* Get current TOE mode from dongle */
		if (brcmf_toe_get(drvr_priv, ifidx, &toe_ol) >= 0
		    && (toe_ol & TOE_TX_CSUM_OL) != 0)
			drvr_priv->iflist[ifidx]->net->features |=
				NETIF_F_IP_CSUM;
		else
			drvr_priv->iflist[ifidx]->net->features &=
				~NETIF_F_IP_CSUM;
	}
	/* Allow transmit calls */
	netif_start_queue(net);
	drvr_priv->pub.up = 1;
	if (unlikely(brcmf_cfg80211_up())) {
		BRCMF_ERROR(("%s: failed to bring up cfg80211\n",
			     __func__));
		return -1;
	}

	return ret;
}

int
brcmf_add_if(struct brcmf_info *drvr_priv, int ifidx, void *handle, char *name,
	   u8 *mac_addr, u32 flags, u8 bssidx)
{
	struct brcmf_if *ifp;

	BRCMF_TRACE(("%s: idx %d, handle->%p\n", __func__, ifidx, handle));

	ifp = drvr_priv->iflist[ifidx];
	if (!ifp) {
		ifp = kmalloc(sizeof(struct brcmf_if), GFP_ATOMIC);
		if (!ifp) {
			BRCMF_ERROR(("%s: OOM - struct brcmf_if\n", __func__));
			return -ENOMEM;
		}
	}

	memset(ifp, 0, sizeof(struct brcmf_if));
	ifp->info = drvr_priv;
	drvr_priv->iflist[ifidx] = ifp;
	strlcpy(ifp->name, name, IFNAMSIZ);
	if (mac_addr != NULL)
		memcpy(&ifp->mac_addr, mac_addr, ETH_ALEN);

	if (handle == NULL) {
		ifp->state = BRCMF_E_IF_ADD;
		ifp->idx = ifidx;
		up(&drvr_priv->sysioc_sem);
	} else
		ifp->net = (struct net_device *)handle;

	return 0;
}

void brcmf_del_if(struct brcmf_info *drvr_priv, int ifidx)
{
	struct brcmf_if *ifp;

	BRCMF_TRACE(("%s: idx %d\n", __func__, ifidx));

	ifp = drvr_priv->iflist[ifidx];
	if (!ifp) {
		BRCMF_ERROR(("%s: Null interface\n", __func__));
		return;
	}

	ifp->state = BRCMF_E_IF_DEL;
	ifp->idx = ifidx;
	up(&drvr_priv->sysioc_sem);
}

struct brcmf_pub *brcmf_attach(struct brcmf_bus *bus, uint bus_hdrlen)
{
	struct brcmf_info *drvr_priv = NULL;
	struct net_device *net;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	/* Allocate etherdev, including space for private structure */
	net = alloc_etherdev(sizeof(drvr_priv));
	if (!net) {
		BRCMF_ERROR(("%s: OOM - alloc_etherdev\n", __func__));
		goto fail;
	}

	/* Allocate primary brcmf_info */
	drvr_priv = kzalloc(sizeof(struct brcmf_info), GFP_ATOMIC);
	if (!drvr_priv) {
		BRCMF_ERROR(("%s: OOM - alloc brcmf_info\n", __func__));
		goto fail;
	}

	/*
	 * Save the brcmf_info into the priv
	 */
	memcpy(netdev_priv(net), &drvr_priv, sizeof(drvr_priv));

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

	if (brcmf_add_if(drvr_priv, 0, (void *)net, net->name, NULL, 0, 0) ==
	    BRCMF_BAD_IF)
		goto fail;

	net->netdev_ops = NULL;
	sema_init(&drvr_priv->proto_sem, 1);
	/* Initialize other structure content */
	init_waitqueue_head(&drvr_priv->ioctl_resp_wait);

	/* Link to info module */
	drvr_priv->pub.info = drvr_priv;

	/* Link to bus module */
	drvr_priv->pub.bus = bus;
	drvr_priv->pub.hdrlen = bus_hdrlen;

	/* Attach and link in the protocol */
	if (brcmf_proto_attach(&drvr_priv->pub) != 0) {
		BRCMF_ERROR(("brcmf_prot_attach failed\n"));
		goto fail;
	}

	/* Attach and link in the cfg80211 */
	if (unlikely(brcmf_cfg80211_attach(net, &drvr_priv->pub))) {
		BRCMF_ERROR(("wl_cfg80211_attach failed\n"));
		goto fail;
	}

	if (brcmf_sysioc) {
		sema_init(&drvr_priv->sysioc_sem, 0);
		drvr_priv->sysioc_tsk = kthread_run(_brcmf_sysioc_thread, drvr_priv,
						"_brcmf_sysioc");
		if (IS_ERR(drvr_priv->sysioc_tsk)) {
			printk(KERN_WARNING
				"_brcmf_sysioc thread failed to start\n");
			drvr_priv->sysioc_tsk = NULL;
		}
	} else
		drvr_priv->sysioc_tsk = NULL;

	/*
	 * Save the brcmf_info into the priv
	 */
	memcpy(netdev_priv(net), &drvr_priv, sizeof(drvr_priv));

#if defined(CONFIG_PM_SLEEP)
	atomic_set(&brcmf_mmc_suspend, false);
#endif	/* defined(CONFIG_PM_SLEEP) */
	return &drvr_priv->pub;

fail:
	if (net)
		free_netdev(net);
	if (drvr_priv)
		brcmf_detach(&drvr_priv->pub);

	return NULL;
}

int brcmf_bus_start(struct brcmf_pub *drvr)
{
	int ret = -1;
	struct brcmf_info *drvr_priv = drvr->info;
	/* Room for "event_msgs" + '\0' + bitvec */
	char iovbuf[BRCMF_EVENTING_MASK_LEN + 12];

	BRCMF_TRACE(("%s:\n", __func__));

	/* Bring up the bus */
	ret = brcmf_sdbrcm_bus_init(&drvr_priv->pub, true);
	if (ret != 0) {
		BRCMF_ERROR(("%s, brcmf_sdbrcm_bus_init failed %d\n", __func__,
			     ret));
		return ret;
	}

	/* If bus is not ready, can't come up */
	if (drvr_priv->pub.busstate != BRCMF_BUS_DATA) {
		BRCMF_ERROR(("%s failed bus is not ready\n", __func__));
		return -ENODEV;
	}

	brcmu_mkiovar("event_msgs", drvr->eventmask, BRCMF_EVENTING_MASK_LEN,
		      iovbuf, sizeof(iovbuf));
	brcmf_proto_cdc_query_ioctl(drvr, 0, BRCMF_C_GET_VAR, iovbuf,
				    sizeof(iovbuf));
	memcpy(drvr->eventmask, iovbuf, BRCMF_EVENTING_MASK_LEN);

	setbit(drvr->eventmask, BRCMF_E_SET_SSID);
	setbit(drvr->eventmask, BRCMF_E_PRUNE);
	setbit(drvr->eventmask, BRCMF_E_AUTH);
	setbit(drvr->eventmask, BRCMF_E_REASSOC);
	setbit(drvr->eventmask, BRCMF_E_REASSOC_IND);
	setbit(drvr->eventmask, BRCMF_E_DEAUTH_IND);
	setbit(drvr->eventmask, BRCMF_E_DISASSOC_IND);
	setbit(drvr->eventmask, BRCMF_E_DISASSOC);
	setbit(drvr->eventmask, BRCMF_E_JOIN);
	setbit(drvr->eventmask, BRCMF_E_ASSOC_IND);
	setbit(drvr->eventmask, BRCMF_E_PSK_SUP);
	setbit(drvr->eventmask, BRCMF_E_LINK);
	setbit(drvr->eventmask, BRCMF_E_NDIS_LINK);
	setbit(drvr->eventmask, BRCMF_E_MIC_ERROR);
	setbit(drvr->eventmask, BRCMF_E_PMKID_CACHE);
	setbit(drvr->eventmask, BRCMF_E_TXFAIL);
	setbit(drvr->eventmask, BRCMF_E_JOIN_START);
	setbit(drvr->eventmask, BRCMF_E_SCAN_COMPLETE);

/* enable dongle roaming event */

	drvr->pktfilter_count = 1;
	/* Setup filter to allow only unicast */
	drvr->pktfilter[0] = "100 0 0 0 0x01 0x00";

	/* Bus is ready, do any protocol initialization */
	ret = brcmf_proto_init(&drvr_priv->pub);
	if (ret < 0)
		return ret;

	return 0;
}

static struct net_device_ops brcmf_netdev_ops_pri = {
	.ndo_open = brcmf_netdev_open,
	.ndo_stop = brcmf_netdev_stop,
	.ndo_get_stats = brcmf_netdev_get_stats,
	.ndo_do_ioctl = brcmf_netdev_ioctl_entry,
	.ndo_start_xmit = brcmf_netdev_start_xmit,
	.ndo_set_mac_address = brcmf_netdev_set_mac_address,
	.ndo_set_multicast_list = brcmf_netdev_set_multicast_list
};

int brcmf_net_attach(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_info *drvr_priv = drvr->info;
	struct net_device *net;
	u8 temp_addr[ETH_ALEN] = {
		0x00, 0x90, 0x4c, 0x11, 0x22, 0x33};

	BRCMF_TRACE(("%s: ifidx %d\n", __func__, ifidx));

	net = drvr_priv->iflist[ifidx]->net;
	net->netdev_ops = &brcmf_netdev_ops_pri;

	/*
	 * We have to use the primary MAC for virtual interfaces
	 */
	if (ifidx != 0) {
		/* for virtual interfaces use the primary MAC  */
		memcpy(temp_addr, drvr_priv->pub.mac, ETH_ALEN);

	}

	if (ifidx == 1) {
		BRCMF_TRACE(("%s ACCESS POINT MAC:\n", __func__));
		/*  ACCESSPOINT INTERFACE CASE */
		temp_addr[0] |= 0X02;	/* set bit 2 ,
			 - Locally Administered address  */

	}
	net->hard_header_len = ETH_HLEN + drvr_priv->pub.hdrlen;
	net->ethtool_ops = &brcmf_ethtool_ops;

	drvr_priv->pub.rxsz = net->mtu + net->hard_header_len +
				drvr_priv->pub.hdrlen;

	memcpy(net->dev_addr, temp_addr, ETH_ALEN);

	if (register_netdev(net) != 0) {
		BRCMF_ERROR(("%s: couldn't register the net device\n",
			     __func__));
		goto fail;
	}

	BRCMF_INFO(("%s: Broadcom Dongle Host Driver\n", net->name));

	return 0;

fail:
	net->netdev_ops = NULL;
	return -EBADE;
}

static void brcmf_bus_detach(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	if (drvr) {
		drvr_priv = drvr->info;
		if (drvr_priv) {
			/* Stop the protocol module */
			brcmf_proto_stop(&drvr_priv->pub);

			/* Stop the bus module */
			brcmf_sdbrcm_bus_stop(drvr_priv->pub.bus, true);
		}
	}
}

void brcmf_detach(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	if (drvr) {
		drvr_priv = drvr->info;
		if (drvr_priv) {
			struct brcmf_if *ifp;
			int i;

			for (i = 1; i < BRCMF_MAX_IFS; i++)
				if (drvr_priv->iflist[i])
					brcmf_del_if(drvr_priv, i);

			ifp = drvr_priv->iflist[0];
			if (ifp->net->netdev_ops == &brcmf_netdev_ops_pri) {
				brcmf_netdev_stop(ifp->net);
				unregister_netdev(ifp->net);
			}

			if (drvr_priv->sysioc_tsk) {
				send_sig(SIGTERM, drvr_priv->sysioc_tsk, 1);
				kthread_stop(drvr_priv->sysioc_tsk);
				drvr_priv->sysioc_tsk = NULL;
			}

			brcmf_bus_detach(drvr);

			if (drvr->prot)
				brcmf_proto_detach(drvr);

			brcmf_cfg80211_detach();

			free_netdev(ifp->net);
			kfree(ifp);
			kfree(drvr_priv);
		}
	}
}

static void __exit brcmf_module_cleanup(void)
{
	BRCMF_TRACE(("%s: Enter\n", __func__));

	brcmf_bus_unregister();
}

static int __init brcmf_module_init(void)
{
	int error;

	BRCMF_TRACE(("%s: Enter\n", __func__));

	error = brcmf_bus_register();

	if (error) {
		BRCMF_ERROR(("%s: brcmf_bus_register failed\n", __func__));
		goto failed;
	}
	return 0;

failed:
	return -EINVAL;
}

module_init(brcmf_module_init);
module_exit(brcmf_module_cleanup);

int brcmf_os_proto_block(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv = drvr->info;

	if (drvr_priv) {
		down(&drvr_priv->proto_sem);
		return 1;
	}
	return 0;
}

int brcmf_os_proto_unblock(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv = drvr->info;

	if (drvr_priv) {
		up(&drvr_priv->proto_sem);
		return 1;
	}

	return 0;
}

unsigned int brcmf_os_get_ioctl_resp_timeout(void)
{
	return (unsigned int)brcmf_ioctl_timeout_msec;
}

void brcmf_os_set_ioctl_resp_timeout(unsigned int timeout_msec)
{
	brcmf_ioctl_timeout_msec = (int)timeout_msec;
}

int brcmf_os_ioctl_resp_wait(struct brcmf_pub *drvr, uint *condition,
			     bool *pending)
{
	struct brcmf_info *drvr_priv = drvr->info;
	DECLARE_WAITQUEUE(wait, current);
	int timeout = brcmf_ioctl_timeout_msec;

	/* Convert timeout in millsecond to jiffies */
	timeout = timeout * HZ / 1000;

	/* Wait until control frame is available */
	add_wait_queue(&drvr_priv->ioctl_resp_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (!(*condition) && (!signal_pending(current) && timeout))
		timeout = schedule_timeout(timeout);

	if (signal_pending(current))
		*pending = true;

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&drvr_priv->ioctl_resp_wait, &wait);

	return timeout;
}

int brcmf_os_ioctl_resp_wake(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv = drvr->info;

	if (waitqueue_active(&drvr_priv->ioctl_resp_wait))
		wake_up_interruptible(&drvr_priv->ioctl_resp_wait);

	return 0;
}

static int brcmf_host_event(struct brcmf_info *drvr_priv, int *ifidx, void *pktdata,
			    struct brcmf_event_msg *event, void **data)
{
	int bcmerror = 0;

	bcmerror = brcmf_c_host_event(drvr_priv, ifidx, pktdata, event, data);
	if (bcmerror != 0)
		return bcmerror;

	if (drvr_priv->iflist[*ifidx]->net)
		brcmf_cfg80211_event(drvr_priv->iflist[*ifidx]->net,
				     event, *data);

	return bcmerror;
}

int brcmf_netdev_reset(struct net_device *dev, u8 flag)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)netdev_priv(dev);

	brcmf_bus_devreset(&drvr_priv->pub, flag);

	return 1;
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_info *drvr_priv)
{
	return atomic_read(&drvr_priv->pend_8021x_cnt);
}

#define MAX_WAIT_FOR_8021X_TX	10

int brcmf_netdev_wait_pend8021x(struct net_device *dev)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)netdev_priv(dev);
	int timeout = 10 * HZ / 1000;
	int ntimes = MAX_WAIT_FOR_8021X_TX;
	int pend = brcmf_get_pend_8021x_cnt(drvr_priv);

	while (ntimes && pend) {
		if (pend) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(timeout);
			set_current_state(TASK_RUNNING);
			ntimes--;
		}
		pend = brcmf_get_pend_8021x_cnt(drvr_priv);
	}
	return pend;
}

#ifdef BCMDBG
int brcmf_write_to_file(struct brcmf_pub *drvr, u8 *buf, int size)
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
		BRCMF_ERROR(("%s: open file error\n", __func__));
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
#endif				/* BCMDBG */
