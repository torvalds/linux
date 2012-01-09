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
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <defs.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include "wl_cfg80211.h"
#include "bcmchip.h"

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11n wireless LAN fullmac driver.");
MODULE_SUPPORTED_DEVICE("Broadcom 802.11n WLAN fullmac cards");
MODULE_LICENSE("Dual BSD/GPL");


/* Interface control information */
struct brcmf_if {
	struct brcmf_info *info;	/* back pointer to brcmf_info */
	/* OS/stack specifics */
	struct net_device *ndev;
	struct net_device_stats stats;
	int idx;		/* iface idx in dongle */
	int state;		/* interface state */
	u8 mac_addr[ETH_ALEN];	/* assigned MAC address */
};

/* Local private structure (extension of pub) */
struct brcmf_info {
	struct brcmf_pub pub;

	/* OS/stack specifics */
	struct brcmf_if *iflist[BRCMF_MAX_IFS];

	struct mutex proto_block;

	struct work_struct setmacaddr_work;
	struct work_struct multicast_work;
	u8 macvalue[ETH_ALEN];
	atomic_t pend_8021x_cnt;
};

/* Error bits */
module_param(brcmf_msg_level, int, 0);


static int brcmf_net2idx(struct brcmf_info *drvr_priv, struct net_device *ndev)
{
	int i = 0;

	while (i < BRCMF_MAX_IFS) {
		if (drvr_priv->iflist[i] && drvr_priv->iflist[i]->ndev == ndev)
			return i;
		i++;
	}

	return BRCMF_BAD_IF;
}

int brcmf_ifname2idx(struct brcmf_info *drvr_priv, char *name)
{
	int i = BRCMF_MAX_IFS;
	struct brcmf_if *ifp;

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0) {
		ifp = drvr_priv->iflist[i];
		if (ifp && !strncmp(ifp->ndev->name, name, IFNAMSIZ))
			break;
	}

	brcmf_dbg(TRACE, "return idx %d for \"%s\"\n", i, name);

	return i;		/* default - the primary interface */
}

char *brcmf_ifname(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_info *drvr_priv = drvr->info;

	if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
		brcmf_dbg(ERROR, "ifidx %d out of range\n", ifidx);
		return "<if_bad>";
	}

	if (drvr_priv->iflist[ifidx] == NULL) {
		brcmf_dbg(ERROR, "null i/f %d\n", ifidx);
		return "<if_null>";
	}

	if (drvr_priv->iflist[ifidx]->ndev)
		return drvr_priv->iflist[ifidx]->ndev->name;

	return "<if_none>";
}

static void _brcmf_set_multicast_list(struct work_struct *work)
{
	struct net_device *ndev;
	struct netdev_hw_addr *ha;
	u32 dcmd_value, cnt;
	__le32 cnt_le;
	__le32 dcmd_le_value;

	struct brcmf_dcmd dcmd;
	char *buf, *bufp;
	uint buflen;
	int ret;

	struct brcmf_info *drvr_priv = container_of(work, struct brcmf_info,
						    multicast_work);

	ndev = drvr_priv->iflist[0]->ndev;
	cnt = netdev_mc_count(ndev);

	/* Determine initial value of allmulti flag */
	dcmd_value = (ndev->flags & IFF_ALLMULTI) ? true : false;

	/* Send down the multicast list first. */

	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETH_ALEN);
	bufp = buf = kmalloc(buflen, GFP_ATOMIC);
	if (!bufp)
		return;

	strcpy(bufp, "mcast_list");
	bufp += strlen("mcast_list") + 1;

	cnt_le = cpu_to_le32(cnt);
	memcpy(bufp, &cnt_le, sizeof(cnt));
	bufp += sizeof(cnt_le);

	netdev_for_each_mc_addr(ha, ndev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETH_ALEN);
		bufp += ETH_ALEN;
		cnt--;
	}

	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = BRCMF_C_SET_VAR;
	dcmd.buf = buf;
	dcmd.len = buflen;
	dcmd.set = true;

	ret = brcmf_proto_dcmd(&drvr_priv->pub, 0, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: set mcast_list failed, cnt %d\n",
			  brcmf_ifname(&drvr_priv->pub, 0), cnt);
		dcmd_value = cnt ? true : dcmd_value;
	}

	kfree(buf);

	/* Now send the allmulti setting.  This is based on the setting in the
	 * net_device flags, but might be modified above to be turned on if we
	 * were trying to set some addresses and dongle rejected it...
	 */

	buflen = sizeof("allmulti") + sizeof(dcmd_value);
	buf = kmalloc(buflen, GFP_ATOMIC);
	if (!buf)
		return;

	dcmd_le_value = cpu_to_le32(dcmd_value);

	if (!brcmf_c_mkiovar
	    ("allmulti", (void *)&dcmd_le_value,
	    sizeof(dcmd_le_value), buf, buflen)) {
		brcmf_dbg(ERROR, "%s: mkiovar failed for allmulti, datalen %d buflen %u\n",
			  brcmf_ifname(&drvr_priv->pub, 0),
			  (int)sizeof(dcmd_value), buflen);
		kfree(buf);
		return;
	}

	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = BRCMF_C_SET_VAR;
	dcmd.buf = buf;
	dcmd.len = buflen;
	dcmd.set = true;

	ret = brcmf_proto_dcmd(&drvr_priv->pub, 0, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: set allmulti %d failed\n",
			  brcmf_ifname(&drvr_priv->pub, 0),
			  le32_to_cpu(dcmd_le_value));
	}

	kfree(buf);

	/* Finally, pick up the PROMISC flag as well, like the NIC
		 driver does */

	dcmd_value = (ndev->flags & IFF_PROMISC) ? true : false;
	dcmd_le_value = cpu_to_le32(dcmd_value);

	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = BRCMF_C_SET_PROMISC;
	dcmd.buf = &dcmd_le_value;
	dcmd.len = sizeof(dcmd_le_value);
	dcmd.set = true;

	ret = brcmf_proto_dcmd(&drvr_priv->pub, 0, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: set promisc %d failed\n",
			  brcmf_ifname(&drvr_priv->pub, 0),
			  le32_to_cpu(dcmd_le_value));
	}
}

static void
_brcmf_set_mac_address(struct work_struct *work)
{
	char buf[32];
	struct brcmf_dcmd dcmd;
	int ret;

	struct brcmf_info *drvr_priv = container_of(work, struct brcmf_info,
						    setmacaddr_work);

	brcmf_dbg(TRACE, "enter\n");
	if (!brcmf_c_mkiovar("cur_etheraddr", (char *)drvr_priv->macvalue,
			   ETH_ALEN, buf, 32)) {
		brcmf_dbg(ERROR, "%s: mkiovar failed for cur_etheraddr\n",
			  brcmf_ifname(&drvr_priv->pub, 0));
		return;
	}
	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = BRCMF_C_SET_VAR;
	dcmd.buf = buf;
	dcmd.len = 32;
	dcmd.set = true;

	ret = brcmf_proto_dcmd(&drvr_priv->pub, 0, &dcmd, dcmd.len);
	if (ret < 0)
		brcmf_dbg(ERROR, "%s: set cur_etheraddr failed\n",
			  brcmf_ifname(&drvr_priv->pub, 0));
	else
		memcpy(drvr_priv->iflist[0]->ndev->dev_addr,
		       drvr_priv->macvalue, ETH_ALEN);

	return;
}

static int brcmf_netdev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;

	ifidx = brcmf_net2idx(drvr_priv, ndev);
	if (ifidx == BRCMF_BAD_IF)
		return -1;

	memcpy(&drvr_priv->macvalue, sa->sa_data, ETH_ALEN);
	schedule_work(&drvr_priv->setmacaddr_work);
	return 0;
}

static void brcmf_netdev_set_multicast_list(struct net_device *ndev)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	int ifidx;

	ifidx = brcmf_net2idx(drvr_priv, ndev);
	if (ifidx == BRCMF_BAD_IF)
		return;

	schedule_work(&drvr_priv->multicast_work);
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

static int brcmf_netdev_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int ret;
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	int ifidx;

	brcmf_dbg(TRACE, "Enter\n");

	/* Reject if down */
	if (!drvr_priv->pub.up || (drvr_priv->pub.busstate == BRCMF_BUS_DOWN)) {
		brcmf_dbg(ERROR, "xmit rejected pub.up=%d busstate=%d\n",
			  drvr_priv->pub.up, drvr_priv->pub.busstate);
		netif_stop_queue(ndev);
		return -ENODEV;
	}

	ifidx = brcmf_net2idx(drvr_priv, ndev);
	if (ifidx == BRCMF_BAD_IF) {
		brcmf_dbg(ERROR, "bad ifidx %d\n", ifidx);
		netif_stop_queue(ndev);
		return -ENODEV;
	}

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < drvr_priv->pub.hdrlen) {
		struct sk_buff *skb2;

		brcmf_dbg(INFO, "%s: insufficient headroom\n",
			  brcmf_ifname(&drvr_priv->pub, ifidx));
		drvr_priv->pub.tx_realloc++;
		skb2 = skb_realloc_headroom(skb, drvr_priv->pub.hdrlen);
		dev_kfree_skb(skb);
		skb = skb2;
		if (skb == NULL) {
			brcmf_dbg(ERROR, "%s: skb_realloc_headroom failed\n",
				  brcmf_ifname(&drvr_priv->pub, ifidx));
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
	struct net_device *ndev;
	struct brcmf_info *drvr_priv = drvr->info;

	brcmf_dbg(TRACE, "Enter\n");

	drvr->txoff = state;
	ndev = drvr_priv->iflist[ifidx]->ndev;
	if (state == ON)
		netif_stop_queue(ndev);
	else
		netif_wake_queue(ndev);
}

static int brcmf_host_event(struct brcmf_info *drvr_priv, int *ifidx,
			    void *pktdata, struct brcmf_event_msg *event,
			    void **data)
{
	int bcmerror = 0;

	bcmerror = brcmf_c_host_event(drvr_priv, ifidx, pktdata, event, data);
	if (bcmerror != 0)
		return bcmerror;

	if (drvr_priv->iflist[*ifidx]->ndev)
		brcmf_cfg80211_event(drvr_priv->iflist[*ifidx]->ndev,
				     event, *data);

	return bcmerror;
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

	brcmf_dbg(TRACE, "Enter\n");

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

		skb->dev = ifp->ndev;
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

		if (ifp->ndev)
			ifp->ndev->last_rx = jiffies;

		drvr->dstats.rx_bytes += skb->len;
		drvr->rx_packets++;	/* Local count */

		if (in_interrupt())
			netif_rx(skb);
		else
			/* If the receive is not processed inside an ISR,
			 * the softirqd must be woken explicitly to service
			 * the NET_RX_SOFTIRQ.  In 2.6 kernels, this is handled
			 * by netif_rx_ni(), but in earlier kernels, we need
			 * to do it manually.
			 */
			netif_rx_ni(skb);
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

static struct net_device_stats *brcmf_netdev_get_stats(struct net_device *ndev)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	struct brcmf_if *ifp;
	int ifidx;

	brcmf_dbg(TRACE, "Enter\n");

	ifidx = brcmf_net2idx(drvr_priv, ndev);
	if (ifidx == BRCMF_BAD_IF)
		return NULL;

	ifp = drvr_priv->iflist[ifidx];

	if (drvr_priv->pub.up)
		/* Use the protocol to get dongle stats */
		brcmf_proto_dstats(&drvr_priv->pub);

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
	struct brcmf_dcmd dcmd;
	__le32 toe_le;
	char buf[32];
	int ret;

	memset(&dcmd, 0, sizeof(dcmd));

	dcmd.cmd = BRCMF_C_GET_VAR;
	dcmd.buf = buf;
	dcmd.len = (uint) sizeof(buf);
	dcmd.set = false;

	strcpy(buf, "toe_ol");
	ret = brcmf_proto_dcmd(&drvr_priv->pub, ifidx, &dcmd, dcmd.len);
	if (ret < 0) {
		/* Check for older dongle image that doesn't support toe_ol */
		if (ret == -EIO) {
			brcmf_dbg(ERROR, "%s: toe not supported by device\n",
				  brcmf_ifname(&drvr_priv->pub, ifidx));
			return -EOPNOTSUPP;
		}

		brcmf_dbg(INFO, "%s: could not get toe_ol: ret=%d\n",
			  brcmf_ifname(&drvr_priv->pub, ifidx), ret);
		return ret;
	}

	memcpy(&toe_le, buf, sizeof(u32));
	*toe_ol = le32_to_cpu(toe_le);
	return 0;
}

/* Set current toe component enables in toe_ol iovar,
	 and set toe global enable iovar */
static int brcmf_toe_set(struct brcmf_info *drvr_priv, int ifidx, u32 toe_ol)
{
	struct brcmf_dcmd dcmd;
	char buf[32];
	int ret;
	__le32 toe_le = cpu_to_le32(toe_ol);

	memset(&dcmd, 0, sizeof(dcmd));

	dcmd.cmd = BRCMF_C_SET_VAR;
	dcmd.buf = buf;
	dcmd.len = (uint) sizeof(buf);
	dcmd.set = true;

	/* Set toe_ol as requested */
	strcpy(buf, "toe_ol");
	memcpy(&buf[sizeof("toe_ol")], &toe_le, sizeof(u32));

	ret = brcmf_proto_dcmd(&drvr_priv->pub, ifidx, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: could not set toe_ol: ret=%d\n",
			  brcmf_ifname(&drvr_priv->pub, ifidx), ret);
		return ret;
	}

	/* Enable toe globally only if any components are enabled. */
	toe_le = cpu_to_le32(toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe_le, sizeof(u32));

	ret = brcmf_proto_dcmd(&drvr_priv->pub, ifidx, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: could not set toe: ret=%d\n",
			  brcmf_ifname(&drvr_priv->pub, ifidx), ret);
		return ret;
	}

	return 0;
}

static void brcmf_ethtool_get_drvinfo(struct net_device *ndev,
				    struct ethtool_drvinfo *info)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);

	sprintf(info->driver, KBUILD_MODNAME);
	sprintf(info->version, "%lu", drvr_priv->pub.drv_version);
	sprintf(info->fw_version, "%s", BCM4329_FW_NAME);
	sprintf(info->bus_info, "%s",
		dev_name(brcmf_bus_get_device(drvr_priv->pub.bus)));
}

static struct ethtool_ops brcmf_ethtool_ops = {
	.get_drvinfo = brcmf_ethtool_get_drvinfo
};

static int brcmf_ethtool(struct brcmf_info *drvr_priv, void __user *uaddr)
{
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	u32 cmd;
	struct ethtool_value edata;
	u32 toe_cmpnt, csum_dir;
	int ret;

	brcmf_dbg(TRACE, "Enter\n");

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
			brcmf_dbg(ERROR, "dongle is not up\n");
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
		brcmf_dbg(CTL, "given %*s, returning %s\n",
			  (int)sizeof(drvname), drvname, info.driver);
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
				drvr_priv->iflist[0]->ndev->features |=
				    NETIF_F_IP_CSUM;
			else
				drvr_priv->iflist[0]->ndev->features &=
				    ~NETIF_F_IP_CSUM;
		}

		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int brcmf_netdev_ioctl_entry(struct net_device *ndev, struct ifreq *ifr,
				    int cmd)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	int ifidx;

	ifidx = brcmf_net2idx(drvr_priv, ndev);
	brcmf_dbg(TRACE, "ifidx %d, cmd 0x%04x\n", ifidx, cmd);

	if (ifidx == BRCMF_BAD_IF)
		return -1;

	if (cmd == SIOCETHTOOL)
		return brcmf_ethtool(drvr_priv, ifr->ifr_data);

	return -EOPNOTSUPP;
}

/* called only from within this driver. Sends a command to the dongle. */
s32 brcmf_exec_dcmd(struct net_device *ndev, u32 cmd, void *arg, u32 len)
{
	struct brcmf_dcmd dcmd;
	s32 err = 0;
	int buflen = 0;
	bool is_set_key_cmd;
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	int ifidx;

	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = cmd;
	dcmd.buf = arg;
	dcmd.len = len;

	ifidx = brcmf_net2idx(drvr_priv, ndev);

	if (dcmd.buf != NULL)
		buflen = min_t(uint, dcmd.len, BRCMF_DCMD_MAXLEN);

	/* send to dongle (must be up, and wl) */
	if ((drvr_priv->pub.busstate != BRCMF_BUS_DATA)) {
		brcmf_dbg(ERROR, "DONGLE_DOWN\n");
		err = -EIO;
		goto done;
	}

	if (!drvr_priv->pub.iswl) {
		err = -EIO;
		goto done;
	}

	/*
	 * Intercept BRCMF_C_SET_KEY CMD - serialize M4 send and
	 * set key CMD to prevent M4 encryption.
	 */
	is_set_key_cmd = ((dcmd.cmd == BRCMF_C_SET_KEY) ||
			  ((dcmd.cmd == BRCMF_C_SET_VAR) &&
			   !(strncmp("wsec_key", dcmd.buf, 9))) ||
			  ((dcmd.cmd == BRCMF_C_SET_VAR) &&
			   !(strncmp("bsscfg:wsec_key", dcmd.buf, 15))));
	if (is_set_key_cmd)
		brcmf_netdev_wait_pend8021x(ndev);

	err = brcmf_proto_dcmd(&drvr_priv->pub, ifidx, &dcmd, buflen);

done:
	if (err > 0)
		err = 0;

	return err;
}

static int brcmf_netdev_stop(struct net_device *ndev)
{
	struct brcmf_pub *drvr = *(struct brcmf_pub **) netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter\n");
	brcmf_cfg80211_down(drvr->config);
	if (drvr->up == 0)
		return 0;

	/* Set state and stop OS transmissions */
	drvr->up = 0;
	netif_stop_queue(ndev);

	return 0;
}

static int brcmf_netdev_open(struct net_device *ndev)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)
					netdev_priv(ndev);
	u32 toe_ol;
	int ifidx = brcmf_net2idx(drvr_priv, ndev);
	s32 ret = 0;

	brcmf_dbg(TRACE, "ifidx %d\n", ifidx);

	if (ifidx == 0) {	/* do it only for primary eth0 */

		/* try to bring up bus */
		ret = brcmf_bus_start(&drvr_priv->pub);
		if (ret != 0) {
			brcmf_dbg(ERROR, "failed with code %d\n", ret);
			return -1;
		}
		atomic_set(&drvr_priv->pend_8021x_cnt, 0);

		memcpy(ndev->dev_addr, drvr_priv->pub.mac, ETH_ALEN);

		/* Get current TOE mode from dongle */
		if (brcmf_toe_get(drvr_priv, ifidx, &toe_ol) >= 0
		    && (toe_ol & TOE_TX_CSUM_OL) != 0)
			drvr_priv->iflist[ifidx]->ndev->features |=
				NETIF_F_IP_CSUM;
		else
			drvr_priv->iflist[ifidx]->ndev->features &=
				~NETIF_F_IP_CSUM;
	}
	/* Allow transmit calls */
	netif_start_queue(ndev);
	drvr_priv->pub.up = 1;
	if (brcmf_cfg80211_up(drvr_priv->pub.config)) {
		brcmf_dbg(ERROR, "failed to bring up cfg80211\n");
		return -1;
	}

	return ret;
}

int
brcmf_add_if(struct brcmf_info *drvr_priv, int ifidx, struct net_device *ndev,
	     char *name, u8 *mac_addr, u32 flags, u8 bssidx)
{
	struct brcmf_if *ifp;
	int ret = 0, err = 0;

	brcmf_dbg(TRACE, "idx %d, handle->%p\n", ifidx, ndev);

	ifp = drvr_priv->iflist[ifidx];
	if (!ifp) {
		ifp = kmalloc(sizeof(struct brcmf_if), GFP_ATOMIC);
		if (!ifp)
			return -ENOMEM;
	}

	memset(ifp, 0, sizeof(struct brcmf_if));
	ifp->info = drvr_priv;
	drvr_priv->iflist[ifidx] = ifp;
	if (mac_addr != NULL)
		memcpy(&ifp->mac_addr, mac_addr, ETH_ALEN);

	if (ndev == NULL) {
		ifp->state = BRCMF_E_IF_ADD;
		ifp->idx = ifidx;
		/*
		 * Delete the existing interface before overwriting it
		 * in case we missed the BRCMF_E_IF_DEL event.
		 */
		if (ifp->ndev != NULL) {
			brcmf_dbg(ERROR, "ERROR: netdev:%s already exists, try free & unregister\n",
				  ifp->ndev->name);
			netif_stop_queue(ifp->ndev);
			unregister_netdev(ifp->ndev);
			free_netdev(ifp->ndev);
		}

		/* Allocate netdev, including space for private structure */
		ifp->ndev = alloc_netdev(sizeof(drvr_priv), "wlan%d",
					 ether_setup);
		if (!ifp->ndev) {
			brcmf_dbg(ERROR, "OOM - alloc_netdev\n");
			ret = -ENOMEM;
		}

		if (ret == 0) {
			memcpy(netdev_priv(ifp->ndev), &drvr_priv,
			       sizeof(drvr_priv));
			err = brcmf_net_attach(&drvr_priv->pub, ifp->idx);
			if (err != 0) {
				brcmf_dbg(ERROR, "brcmf_net_attach failed, err %d\n",
					  err);
				ret = -EOPNOTSUPP;
			} else {
				brcmf_dbg(TRACE, " ==== pid:%x, net_device for if:%s created ===\n",
					  current->pid, ifp->ndev->name);
				ifp->state = 0;
			}
		}

		if (ret < 0) {
			if (ifp->ndev)
				free_netdev(ifp->ndev);

			drvr_priv->iflist[ifp->idx] = NULL;
			kfree(ifp);
		}
	} else
		ifp->ndev = ndev;

	return 0;
}

void brcmf_del_if(struct brcmf_info *drvr_priv, int ifidx)
{
	struct brcmf_if *ifp;

	brcmf_dbg(TRACE, "idx %d\n", ifidx);

	ifp = drvr_priv->iflist[ifidx];
	if (!ifp) {
		brcmf_dbg(ERROR, "Null interface\n");
		return;
	}

	ifp->state = BRCMF_E_IF_DEL;
	ifp->idx = ifidx;
	if (ifp->ndev != NULL) {
		netif_stop_queue(ifp->ndev);
		unregister_netdev(ifp->ndev);
		free_netdev(ifp->ndev);
		drvr_priv->iflist[ifidx] = NULL;
		kfree(ifp);
	}
}

struct brcmf_pub *brcmf_attach(struct brcmf_bus *bus, uint bus_hdrlen)
{
	struct brcmf_info *drvr_priv = NULL;
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter\n");

	/* Allocate netdev, including space for private structure */
	ndev = alloc_netdev(sizeof(drvr_priv), "wlan%d", ether_setup);
	if (!ndev) {
		brcmf_dbg(ERROR, "OOM - alloc_netdev\n");
		goto fail;
	}

	/* Allocate primary brcmf_info */
	drvr_priv = kzalloc(sizeof(struct brcmf_info), GFP_ATOMIC);
	if (!drvr_priv)
		goto fail;

	/*
	 * Save the brcmf_info into the priv
	 */
	memcpy(netdev_priv(ndev), &drvr_priv, sizeof(drvr_priv));

	if (brcmf_add_if(drvr_priv, 0, ndev, ndev->name, NULL, 0, 0) ==
	    BRCMF_BAD_IF)
		goto fail;

	ndev->netdev_ops = NULL;
	mutex_init(&drvr_priv->proto_block);

	/* Link to info module */
	drvr_priv->pub.info = drvr_priv;

	/* Link to bus module */
	drvr_priv->pub.bus = bus;
	drvr_priv->pub.hdrlen = bus_hdrlen;

	/* Attach and link in the protocol */
	if (brcmf_proto_attach(&drvr_priv->pub) != 0) {
		brcmf_dbg(ERROR, "brcmf_prot_attach failed\n");
		goto fail;
	}

	/* Attach and link in the cfg80211 */
	drvr_priv->pub.config =
			brcmf_cfg80211_attach(ndev,
					      brcmf_bus_get_device(bus),
					      &drvr_priv->pub);
	if (drvr_priv->pub.config == NULL) {
		brcmf_dbg(ERROR, "wl_cfg80211_attach failed\n");
		goto fail;
	}

	INIT_WORK(&drvr_priv->setmacaddr_work, _brcmf_set_mac_address);
	INIT_WORK(&drvr_priv->multicast_work, _brcmf_set_multicast_list);

	/*
	 * Save the brcmf_info into the priv
	 */
	memcpy(netdev_priv(ndev), &drvr_priv, sizeof(drvr_priv));

	return &drvr_priv->pub;

fail:
	if (ndev)
		free_netdev(ndev);
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

	brcmf_dbg(TRACE, "\n");

	/* Bring up the bus */
	ret = brcmf_sdbrcm_bus_init(&drvr_priv->pub);
	if (ret != 0) {
		brcmf_dbg(ERROR, "brcmf_sdbrcm_bus_init failed %d\n", ret);
		return ret;
	}

	/* If bus is not ready, can't come up */
	if (drvr_priv->pub.busstate != BRCMF_BUS_DATA) {
		brcmf_dbg(ERROR, "failed bus is not ready\n");
		return -ENODEV;
	}

	brcmf_c_mkiovar("event_msgs", drvr->eventmask, BRCMF_EVENTING_MASK_LEN,
		      iovbuf, sizeof(iovbuf));
	brcmf_proto_cdc_query_dcmd(drvr, 0, BRCMF_C_GET_VAR, iovbuf,
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
	.ndo_set_rx_mode = brcmf_netdev_set_multicast_list
};

int brcmf_net_attach(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_info *drvr_priv = drvr->info;
	struct net_device *ndev;
	u8 temp_addr[ETH_ALEN] = {
		0x00, 0x90, 0x4c, 0x11, 0x22, 0x33};

	brcmf_dbg(TRACE, "ifidx %d\n", ifidx);

	ndev = drvr_priv->iflist[ifidx]->ndev;
	ndev->netdev_ops = &brcmf_netdev_ops_pri;

	/*
	 * We have to use the primary MAC for virtual interfaces
	 */
	if (ifidx != 0) {
		/* for virtual interfaces use the primary MAC  */
		memcpy(temp_addr, drvr_priv->pub.mac, ETH_ALEN);

	}

	if (ifidx == 1) {
		brcmf_dbg(TRACE, "ACCESS POINT MAC:\n");
		/*  ACCESSPOINT INTERFACE CASE */
		temp_addr[0] |= 0X02;	/* set bit 2 ,
			 - Locally Administered address  */

	}
	ndev->hard_header_len = ETH_HLEN + drvr_priv->pub.hdrlen;
	ndev->ethtool_ops = &brcmf_ethtool_ops;

	drvr_priv->pub.rxsz = ndev->mtu + ndev->hard_header_len +
			      drvr_priv->pub.hdrlen;

	memcpy(ndev->dev_addr, temp_addr, ETH_ALEN);

	if (register_netdev(ndev) != 0) {
		brcmf_dbg(ERROR, "couldn't register the net device\n");
		goto fail;
	}

	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

	return 0;

fail:
	ndev->netdev_ops = NULL;
	return -EBADE;
}

static void brcmf_bus_detach(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv;

	brcmf_dbg(TRACE, "Enter\n");

	if (drvr) {
		drvr_priv = drvr->info;
		if (drvr_priv) {
			/* Stop the protocol module */
			brcmf_proto_stop(&drvr_priv->pub);

			/* Stop the bus module */
			brcmf_sdbrcm_bus_stop(drvr_priv->pub.bus);
		}
	}
}

void brcmf_detach(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv;

	brcmf_dbg(TRACE, "Enter\n");

	if (drvr) {
		drvr_priv = drvr->info;
		if (drvr_priv) {
			struct brcmf_if *ifp;
			int i;

			for (i = 1; i < BRCMF_MAX_IFS; i++)
				if (drvr_priv->iflist[i])
					brcmf_del_if(drvr_priv, i);

			ifp = drvr_priv->iflist[0];
			if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
				rtnl_lock();
				brcmf_netdev_stop(ifp->ndev);
				rtnl_unlock();
				unregister_netdev(ifp->ndev);
			}

			cancel_work_sync(&drvr_priv->setmacaddr_work);
			cancel_work_sync(&drvr_priv->multicast_work);

			brcmf_bus_detach(drvr);

			if (drvr->prot)
				brcmf_proto_detach(drvr);

			brcmf_cfg80211_detach(drvr->config);

			free_netdev(ifp->ndev);
			kfree(ifp);
			kfree(drvr_priv);
		}
	}
}

static void __exit brcmf_module_cleanup(void)
{
	brcmf_dbg(TRACE, "Enter\n");

	brcmf_bus_unregister();
}

static int __init brcmf_module_init(void)
{
	int error;

	brcmf_dbg(TRACE, "Enter\n");

	error = brcmf_bus_register();

	if (error) {
		brcmf_dbg(ERROR, "brcmf_bus_register failed\n");
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
		mutex_lock(&drvr_priv->proto_block);
		return 1;
	}
	return 0;
}

int brcmf_os_proto_unblock(struct brcmf_pub *drvr)
{
	struct brcmf_info *drvr_priv = drvr->info;

	if (drvr_priv) {
		mutex_unlock(&drvr_priv->proto_block);
		return 1;
	}

	return 0;
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_info *drvr_priv)
{
	return atomic_read(&drvr_priv->pend_8021x_cnt);
}

#define MAX_WAIT_FOR_8021X_TX	10

int brcmf_netdev_wait_pend8021x(struct net_device *ndev)
{
	struct brcmf_info *drvr_priv = *(struct brcmf_info **)netdev_priv(ndev);
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
int brcmf_write_to_file(struct brcmf_pub *drvr, const u8 *buf, int size)
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
		brcmf_dbg(ERROR, "open file error\n");
		ret = -1;
		goto exit;
	}

	/* Write buf to file */
	fp->f_op->write(fp, (char __user *)buf, size, &pos);

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
