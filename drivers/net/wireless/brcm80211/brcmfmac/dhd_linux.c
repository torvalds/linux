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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11n wireless LAN fullmac driver.");
MODULE_SUPPORTED_DEVICE("Broadcom 802.11n WLAN fullmac cards");
MODULE_LICENSE("Dual BSD/GPL");


/* Interface control information */
struct brcmf_if {
	struct brcmf_pub *drvr;	/* back pointer to brcmf_pub */
	/* OS/stack specifics */
	struct net_device *ndev;
	struct net_device_stats stats;
	int idx;		/* iface idx in dongle */
	u8 mac_addr[ETH_ALEN];	/* assigned MAC address */
};

/* Error bits */
int brcmf_msg_level = BRCMF_ERROR_VAL;
module_param(brcmf_msg_level, int, 0);

int brcmf_ifname2idx(struct brcmf_pub *drvr, char *name)
{
	int i = BRCMF_MAX_IFS;
	struct brcmf_if *ifp;

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0) {
		ifp = drvr->iflist[i];
		if (ifp && !strncmp(ifp->ndev->name, name, IFNAMSIZ))
			break;
	}

	brcmf_dbg(TRACE, "return idx %d for \"%s\"\n", i, name);

	return i;		/* default - the primary interface */
}

char *brcmf_ifname(struct brcmf_pub *drvr, int ifidx)
{
	if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
		brcmf_dbg(ERROR, "ifidx %d out of range\n", ifidx);
		return "<if_bad>";
	}

	if (drvr->iflist[ifidx] == NULL) {
		brcmf_dbg(ERROR, "null i/f %d\n", ifidx);
		return "<if_null>";
	}

	if (drvr->iflist[ifidx]->ndev)
		return drvr->iflist[ifidx]->ndev->name;

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

	struct brcmf_pub *drvr = container_of(work, struct brcmf_pub,
						    multicast_work);

	ndev = drvr->iflist[0]->ndev;
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

	ret = brcmf_proto_dcmd(drvr, 0, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: set mcast_list failed, cnt %d\n",
			  brcmf_ifname(drvr, 0), cnt);
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
			  brcmf_ifname(drvr, 0),
			  (int)sizeof(dcmd_value), buflen);
		kfree(buf);
		return;
	}

	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = BRCMF_C_SET_VAR;
	dcmd.buf = buf;
	dcmd.len = buflen;
	dcmd.set = true;

	ret = brcmf_proto_dcmd(drvr, 0, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: set allmulti %d failed\n",
			  brcmf_ifname(drvr, 0),
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

	ret = brcmf_proto_dcmd(drvr, 0, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: set promisc %d failed\n",
			  brcmf_ifname(drvr, 0),
			  le32_to_cpu(dcmd_le_value));
	}
}

static void
_brcmf_set_mac_address(struct work_struct *work)
{
	char buf[32];
	struct brcmf_dcmd dcmd;
	int ret;

	struct brcmf_pub *drvr = container_of(work, struct brcmf_pub,
						    setmacaddr_work);

	brcmf_dbg(TRACE, "enter\n");
	if (!brcmf_c_mkiovar("cur_etheraddr", (char *)drvr->macvalue,
			   ETH_ALEN, buf, 32)) {
		brcmf_dbg(ERROR, "%s: mkiovar failed for cur_etheraddr\n",
			  brcmf_ifname(drvr, 0));
		return;
	}
	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = BRCMF_C_SET_VAR;
	dcmd.buf = buf;
	dcmd.len = 32;
	dcmd.set = true;

	ret = brcmf_proto_dcmd(drvr, 0, &dcmd, dcmd.len);
	if (ret < 0)
		brcmf_dbg(ERROR, "%s: set cur_etheraddr failed\n",
			  brcmf_ifname(drvr, 0));
	else
		memcpy(drvr->iflist[0]->ndev->dev_addr,
		       drvr->macvalue, ETH_ALEN);

	return;
}

static int brcmf_netdev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct sockaddr *sa = (struct sockaddr *)addr;

	memcpy(&drvr->macvalue, sa->sa_data, ETH_ALEN);
	schedule_work(&drvr->setmacaddr_work);
	return 0;
}

static void brcmf_netdev_set_multicast_list(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	schedule_work(&drvr->multicast_work);
}

int brcmf_sendpkt(struct brcmf_pub *drvr, int ifidx, struct sk_buff *pktbuf)
{
	/* Reject if down */
	if (!drvr->bus_if->drvr_up || (drvr->bus_if->state == BRCMF_BUS_DOWN))
		return -ENODEV;

	/* Update multicast statistic */
	if (pktbuf->len >= ETH_ALEN) {
		u8 *pktdata = (u8 *) (pktbuf->data);
		struct ethhdr *eh = (struct ethhdr *)pktdata;

		if (is_multicast_ether_addr(eh->h_dest))
			drvr->tx_multicast++;
		if (ntohs(eh->h_proto) == ETH_P_PAE)
			atomic_inc(&drvr->pend_8021x_cnt);
	}

	/* If the protocol uses a data header, apply it */
	brcmf_proto_hdrpush(drvr, ifidx, pktbuf);

	/* Use bus module to send data frame */
	return drvr->bus_if->brcmf_bus_txdata(drvr->dev, pktbuf);
}

static int brcmf_netdev_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int ret;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	/* Reject if down */
	if (!drvr->bus_if->drvr_up ||
	    (drvr->bus_if->state == BRCMF_BUS_DOWN)) {
		brcmf_dbg(ERROR, "xmit rejected drvup=%d state=%d\n",
			  drvr->bus_if->drvr_up,
			  drvr->bus_if->state);
		netif_stop_queue(ndev);
		return -ENODEV;
	}

	if (!drvr->iflist[ifp->idx]) {
		brcmf_dbg(ERROR, "bad ifidx %d\n", ifp->idx);
		netif_stop_queue(ndev);
		return -ENODEV;
	}

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < drvr->hdrlen) {
		struct sk_buff *skb2;

		brcmf_dbg(INFO, "%s: insufficient headroom\n",
			  brcmf_ifname(drvr, ifp->idx));
		drvr->bus_if->tx_realloc++;
		skb2 = skb_realloc_headroom(skb, drvr->hdrlen);
		dev_kfree_skb(skb);
		skb = skb2;
		if (skb == NULL) {
			brcmf_dbg(ERROR, "%s: skb_realloc_headroom failed\n",
				  brcmf_ifname(drvr, ifp->idx));
			ret = -ENOMEM;
			goto done;
		}
	}

	ret = brcmf_sendpkt(drvr, ifp->idx, skb);

done:
	if (ret)
		drvr->bus_if->dstats.tx_dropped++;
	else
		drvr->bus_if->dstats.tx_packets++;

	/* Return ok: we always eat the packet */
	return 0;
}

void brcmf_txflowcontrol(struct device *dev, int ifidx, bool state)
{
	struct net_device *ndev;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	ndev = drvr->iflist[ifidx]->ndev;
	if (state == ON)
		netif_stop_queue(ndev);
	else
		netif_wake_queue(ndev);
}

static int brcmf_host_event(struct brcmf_pub *drvr, int *ifidx,
			    void *pktdata, struct brcmf_event_msg *event,
			    void **data)
{
	int bcmerror = 0;

	bcmerror = brcmf_c_host_event(drvr, ifidx, pktdata, event, data);
	if (bcmerror != 0)
		return bcmerror;

	if (drvr->iflist[*ifidx]->ndev)
		brcmf_cfg80211_event(drvr->iflist[*ifidx]->ndev,
				     event, *data);

	return bcmerror;
}

void brcmf_rx_frame(struct device *dev, int ifidx,
		    struct sk_buff_head *skb_list)
{
	unsigned char *eth;
	uint len;
	void *data;
	struct sk_buff *skb, *pnext;
	struct brcmf_if *ifp;
	struct brcmf_event_msg event;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	skb_queue_walk_safe(skb_list, skb, pnext) {
		skb_unlink(skb, skb_list);

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

		ifp = drvr->iflist[ifidx];
		if (ifp == NULL)
			ifp = drvr->iflist[0];

		if (!ifp || !ifp->ndev ||
		    ifp->ndev->reg_state != NETREG_REGISTERED) {
			brcmu_pkt_buf_free_skb(skb);
			continue;
		}

		skb->dev = ifp->ndev;
		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST)
			bus_if->dstats.multicast++;

		skb->data = eth;
		skb->len = len;

		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* Process special event packets and then discard them */
		if (ntohs(skb->protocol) == ETH_P_LINK_CTL)
			brcmf_host_event(drvr, &ifidx,
					  skb_mac_header(skb),
					  &event, &data);

		if (drvr->iflist[ifidx]) {
			ifp = drvr->iflist[ifidx];
			ifp->ndev->last_rx = jiffies;
		}

		bus_if->dstats.rx_bytes += skb->len;
		bus_if->dstats.rx_packets++;	/* Local count */

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

void brcmf_txcomplete(struct device *dev, struct sk_buff *txp, bool success)
{
	uint ifidx;
	struct ethhdr *eh;
	u16 type;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_proto_hdrpull(dev, &ifidx, txp);

	eh = (struct ethhdr *)(txp->data);
	type = ntohs(eh->h_proto);

	if (type == ETH_P_PAE)
		atomic_dec(&drvr->pend_8021x_cnt);

}

static struct net_device_stats *brcmf_netdev_get_stats(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_bus *bus_if = ifp->drvr->bus_if;

	brcmf_dbg(TRACE, "Enter\n");

	/* Copy dongle stats to net device stats */
	ifp->stats.rx_packets = bus_if->dstats.rx_packets;
	ifp->stats.tx_packets = bus_if->dstats.tx_packets;
	ifp->stats.rx_bytes = bus_if->dstats.rx_bytes;
	ifp->stats.tx_bytes = bus_if->dstats.tx_bytes;
	ifp->stats.rx_errors = bus_if->dstats.rx_errors;
	ifp->stats.tx_errors = bus_if->dstats.tx_errors;
	ifp->stats.rx_dropped = bus_if->dstats.rx_dropped;
	ifp->stats.tx_dropped = bus_if->dstats.tx_dropped;
	ifp->stats.multicast = bus_if->dstats.multicast;

	return &ifp->stats;
}

/* Retrieve current toe component enables, which are kept
	 as a bitmap in toe_ol iovar */
static int brcmf_toe_get(struct brcmf_pub *drvr, int ifidx, u32 *toe_ol)
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
	ret = brcmf_proto_dcmd(drvr, ifidx, &dcmd, dcmd.len);
	if (ret < 0) {
		/* Check for older dongle image that doesn't support toe_ol */
		if (ret == -EIO) {
			brcmf_dbg(ERROR, "%s: toe not supported by device\n",
				  brcmf_ifname(drvr, ifidx));
			return -EOPNOTSUPP;
		}

		brcmf_dbg(INFO, "%s: could not get toe_ol: ret=%d\n",
			  brcmf_ifname(drvr, ifidx), ret);
		return ret;
	}

	memcpy(&toe_le, buf, sizeof(u32));
	*toe_ol = le32_to_cpu(toe_le);
	return 0;
}

/* Set current toe component enables in toe_ol iovar,
	 and set toe global enable iovar */
static int brcmf_toe_set(struct brcmf_pub *drvr, int ifidx, u32 toe_ol)
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

	ret = brcmf_proto_dcmd(drvr, ifidx, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: could not set toe_ol: ret=%d\n",
			  brcmf_ifname(drvr, ifidx), ret);
		return ret;
	}

	/* Enable toe globally only if any components are enabled. */
	toe_le = cpu_to_le32(toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe_le, sizeof(u32));

	ret = brcmf_proto_dcmd(drvr, ifidx, &dcmd, dcmd.len);
	if (ret < 0) {
		brcmf_dbg(ERROR, "%s: could not set toe: ret=%d\n",
			  brcmf_ifname(drvr, ifidx), ret);
		return ret;
	}

	return 0;
}

static void brcmf_ethtool_get_drvinfo(struct net_device *ndev,
				    struct ethtool_drvinfo *info)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	sprintf(info->driver, KBUILD_MODNAME);
	sprintf(info->version, "%lu", drvr->drv_version);
	sprintf(info->bus_info, "%s", dev_name(drvr->dev));
}

static const struct ethtool_ops brcmf_ethtool_ops = {
	.get_drvinfo = brcmf_ethtool_get_drvinfo,
};

static int brcmf_ethtool(struct brcmf_pub *drvr, void __user *uaddr)
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
		else if (!drvr->bus_if->drvr_up) {
			brcmf_dbg(ERROR, "dongle is not up\n");
			return -ENODEV;
		}

		/* finally, report dongle driver type */
		else if (drvr->iswl)
			sprintf(info.driver, "wl");
		else
			sprintf(info.driver, "xx");

		sprintf(info.version, "%lu", drvr->drv_version);
		if (copy_to_user(uaddr, &info, sizeof(info)))
			return -EFAULT;
		brcmf_dbg(CTL, "given %*s, returning %s\n",
			  (int)sizeof(drvname), drvname, info.driver);
		break;

		/* Get toe offload components from dongle */
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		ret = brcmf_toe_get(drvr, 0, &toe_cmpnt);
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
		ret = brcmf_toe_get(drvr, 0, &toe_cmpnt);
		if (ret < 0)
			return ret;

		csum_dir =
		    (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		ret = brcmf_toe_set(drvr, 0, toe_cmpnt);
		if (ret < 0)
			return ret;

		/* If setting TX checksum mode, tell Linux the new mode */
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				drvr->iflist[0]->ndev->features |=
				    NETIF_F_IP_CSUM;
			else
				drvr->iflist[0]->ndev->features &=
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
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	brcmf_dbg(TRACE, "ifidx %d, cmd 0x%04x\n", ifp->idx, cmd);

	if (!drvr->iflist[ifp->idx])
		return -1;

	if (cmd == SIOCETHTOOL)
		return brcmf_ethtool(drvr, ifr->ifr_data);

	return -EOPNOTSUPP;
}

/* called only from within this driver. Sends a command to the dongle. */
s32 brcmf_exec_dcmd(struct net_device *ndev, u32 cmd, void *arg, u32 len)
{
	struct brcmf_dcmd dcmd;
	s32 err = 0;
	int buflen = 0;
	bool is_set_key_cmd;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	memset(&dcmd, 0, sizeof(dcmd));
	dcmd.cmd = cmd;
	dcmd.buf = arg;
	dcmd.len = len;

	if (dcmd.buf != NULL)
		buflen = min_t(uint, dcmd.len, BRCMF_DCMD_MAXLEN);

	/* send to dongle (must be up, and wl) */
	if ((drvr->bus_if->state != BRCMF_BUS_DATA)) {
		brcmf_dbg(ERROR, "DONGLE_DOWN\n");
		err = -EIO;
		goto done;
	}

	if (!drvr->iswl) {
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

	err = brcmf_proto_dcmd(drvr, ifp->idx, &dcmd, buflen);

done:
	if (err > 0)
		err = 0;

	return err;
}

int brcmf_netlink_dcmd(struct net_device *ndev, struct brcmf_dcmd *dcmd)
{
	brcmf_dbg(TRACE, "enter: cmd %x buf %p len %d\n",
		  dcmd->cmd, dcmd->buf, dcmd->len);

	return brcmf_exec_dcmd(ndev, dcmd->cmd, dcmd->buf, dcmd->len);
}

static int brcmf_netdev_stop(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	brcmf_dbg(TRACE, "Enter\n");
	brcmf_cfg80211_down(drvr->config);
	if (drvr->bus_if->drvr_up == 0)
		return 0;

	/* Set state and stop OS transmissions */
	drvr->bus_if->drvr_up = false;
	netif_stop_queue(ndev);

	return 0;
}

static int brcmf_netdev_open(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus_if = drvr->bus_if;
	u32 toe_ol;
	s32 ret = 0;
	uint up = 0;

	brcmf_dbg(TRACE, "ifidx %d\n", ifp->idx);

	if (ifp->idx == 0) {	/* do it only for primary eth0 */
		/* If bus is not ready, can't continue */
		if (bus_if->state != BRCMF_BUS_DATA) {
			brcmf_dbg(ERROR, "failed bus is not ready\n");
			return -EAGAIN;
		}

		atomic_set(&drvr->pend_8021x_cnt, 0);

		memcpy(ndev->dev_addr, drvr->mac, ETH_ALEN);

		/* Get current TOE mode from dongle */
		if (brcmf_toe_get(drvr, ifp->idx, &toe_ol) >= 0
		    && (toe_ol & TOE_TX_CSUM_OL) != 0)
			drvr->iflist[ifp->idx]->ndev->features |=
				NETIF_F_IP_CSUM;
		else
			drvr->iflist[ifp->idx]->ndev->features &=
				~NETIF_F_IP_CSUM;
	}

	/* make sure RF is ready for work */
	brcmf_proto_cdc_set_dcmd(drvr, 0, BRCMF_C_UP, (char *)&up, sizeof(up));

	/* Allow transmit calls */
	netif_start_queue(ndev);
	drvr->bus_if->drvr_up = true;
	if (brcmf_cfg80211_up(drvr->config)) {
		brcmf_dbg(ERROR, "failed to bring up cfg80211\n");
		return -1;
	}

	return ret;
}

static const struct net_device_ops brcmf_netdev_ops_pri = {
	.ndo_open = brcmf_netdev_open,
	.ndo_stop = brcmf_netdev_stop,
	.ndo_get_stats = brcmf_netdev_get_stats,
	.ndo_do_ioctl = brcmf_netdev_ioctl_entry,
	.ndo_start_xmit = brcmf_netdev_start_xmit,
	.ndo_set_mac_address = brcmf_netdev_set_mac_address,
	.ndo_set_rx_mode = brcmf_netdev_set_multicast_list
};

static int brcmf_net_attach(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;
	u8 temp_addr[ETH_ALEN];

	brcmf_dbg(TRACE, "ifidx %d\n", ifp->idx);

	ndev = drvr->iflist[ifp->idx]->ndev;
	ndev->netdev_ops = &brcmf_netdev_ops_pri;

	/*
	 * determine mac address to use
	 */
	if (is_valid_ether_addr(ifp->mac_addr))
		memcpy(temp_addr, ifp->mac_addr, ETH_ALEN);
	else
		memcpy(temp_addr, drvr->mac, ETH_ALEN);

	if (ifp->idx == 1) {
		brcmf_dbg(TRACE, "ACCESS POINT MAC:\n");
		/*  ACCESSPOINT INTERFACE CASE */
		temp_addr[0] |= 0X02;	/* set bit 2 ,
			 - Locally Administered address  */

	}
	ndev->hard_header_len = ETH_HLEN + drvr->hdrlen;
	ndev->ethtool_ops = &brcmf_ethtool_ops;

	drvr->rxsz = ndev->mtu + ndev->hard_header_len +
			      drvr->hdrlen;

	memcpy(ndev->dev_addr, temp_addr, ETH_ALEN);

	/* attach to cfg80211 for primary interface */
	if (!ifp->idx) {
		drvr->config = brcmf_cfg80211_attach(ndev, drvr->dev, drvr);
		if (drvr->config == NULL) {
			brcmf_dbg(ERROR, "wl_cfg80211_attach failed\n");
			goto fail;
		}
	}

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

int
brcmf_add_if(struct device *dev, int ifidx, char *name, u8 *mac_addr)
{
	struct brcmf_if *ifp;
	struct net_device *ndev;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "idx %d\n", ifidx);

	ifp = drvr->iflist[ifidx];
	/*
	 * Delete the existing interface before overwriting it
	 * in case we missed the BRCMF_E_IF_DEL event.
	 */
	if (ifp) {
		brcmf_dbg(ERROR, "ERROR: netdev:%s already exists, try free & unregister\n",
			  ifp->ndev->name);
		netif_stop_queue(ifp->ndev);
		unregister_netdev(ifp->ndev);
		free_netdev(ifp->ndev);
		drvr->iflist[ifidx] = NULL;
	}

	/* Allocate netdev, including space for private structure */
	ndev = alloc_netdev(sizeof(struct brcmf_if), name, ether_setup);
	if (!ndev) {
		brcmf_dbg(ERROR, "OOM - alloc_netdev\n");
		return -ENOMEM;
	}

	ifp = netdev_priv(ndev);
	ifp->ndev = ndev;
	ifp->drvr = drvr;
	drvr->iflist[ifidx] = ifp;
	ifp->idx = ifidx;
	if (mac_addr != NULL)
		memcpy(&ifp->mac_addr, mac_addr, ETH_ALEN);

	if (brcmf_net_attach(ifp)) {
		brcmf_dbg(ERROR, "brcmf_net_attach failed");
		free_netdev(ifp->ndev);
		drvr->iflist[ifidx] = NULL;
		return -EOPNOTSUPP;
	}

	brcmf_dbg(TRACE, " ==== pid:%x, net_device for if:%s created ===\n",
		  current->pid, ifp->ndev->name);

	return 0;
}

void brcmf_del_if(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_if *ifp;

	brcmf_dbg(TRACE, "idx %d\n", ifidx);

	ifp = drvr->iflist[ifidx];
	if (!ifp) {
		brcmf_dbg(ERROR, "Null interface\n");
		return;
	}
	if (ifp->ndev) {
		if (ifidx == 0) {
			if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
				rtnl_lock();
				brcmf_netdev_stop(ifp->ndev);
				rtnl_unlock();
			}
		} else {
			netif_stop_queue(ifp->ndev);
		}

		unregister_netdev(ifp->ndev);
		drvr->iflist[ifidx] = NULL;
		if (ifidx == 0)
			brcmf_cfg80211_detach(drvr->config);
		free_netdev(ifp->ndev);
	}
}

int brcmf_attach(uint bus_hdrlen, struct device *dev)
{
	struct brcmf_pub *drvr = NULL;
	int ret = 0;

	brcmf_dbg(TRACE, "Enter\n");

	/* Allocate primary brcmf_info */
	drvr = kzalloc(sizeof(struct brcmf_pub), GFP_ATOMIC);
	if (!drvr)
		return -ENOMEM;

	mutex_init(&drvr->proto_block);

	/* Link to bus module */
	drvr->hdrlen = bus_hdrlen;
	drvr->bus_if = dev_get_drvdata(dev);
	drvr->bus_if->drvr = drvr;
	drvr->dev = dev;

	/* create device debugfs folder */
	brcmf_debugfs_attach(drvr);

	/* Attach and link in the protocol */
	ret = brcmf_proto_attach(drvr);
	if (ret != 0) {
		brcmf_dbg(ERROR, "brcmf_prot_attach failed\n");
		goto fail;
	}

	INIT_WORK(&drvr->setmacaddr_work, _brcmf_set_mac_address);
	INIT_WORK(&drvr->multicast_work, _brcmf_set_multicast_list);

	INIT_LIST_HEAD(&drvr->bus_if->dcmd_list);

	return ret;

fail:
	brcmf_detach(dev);

	return ret;
}

int brcmf_bus_start(struct device *dev)
{
	int ret = -1;
	/* Room for "event_msgs" + '\0' + bitvec */
	char iovbuf[BRCMF_EVENTING_MASK_LEN + 12];
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "\n");

	/* Bring up the bus */
	ret = bus_if->brcmf_bus_init(dev);
	if (ret != 0) {
		brcmf_dbg(ERROR, "brcmf_sdbrcm_bus_init failed %d\n", ret);
		return ret;
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
	ret = brcmf_proto_init(drvr);
	if (ret < 0)
		return ret;

	/* add primary networking interface */
	ret = brcmf_add_if(dev, 0, "wlan%d", drvr->mac);
	if (ret < 0)
		return ret;

	/* signal bus ready */
	bus_if->state = BRCMF_BUS_DATA;
	return 0;
}

static void brcmf_bus_detach(struct brcmf_pub *drvr)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (drvr) {
		/* Stop the protocol module */
		brcmf_proto_stop(drvr);

		/* Stop the bus module */
		drvr->bus_if->brcmf_bus_stop(drvr->dev);
	}
}

void brcmf_detach(struct device *dev)
{
	int i;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");


	/* make sure primary interface removed last */
	for (i = BRCMF_MAX_IFS-1; i > -1; i--)
		if (drvr->iflist[i])
			brcmf_del_if(drvr, i);

	brcmf_bus_detach(drvr);

	if (drvr->prot) {
		cancel_work_sync(&drvr->setmacaddr_work);
		cancel_work_sync(&drvr->multicast_work);
		brcmf_proto_detach(drvr);
	}

	brcmf_debugfs_detach(drvr);
	bus_if->drvr = NULL;
	kfree(drvr);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_pub *drvr)
{
	return atomic_read(&drvr->pend_8021x_cnt);
}

#define MAX_WAIT_FOR_8021X_TX	10

int brcmf_netdev_wait_pend8021x(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	int timeout = 10 * HZ / 1000;
	int ntimes = MAX_WAIT_FOR_8021X_TX;
	int pend = brcmf_get_pend_8021x_cnt(drvr);

	while (ntimes && pend) {
		if (pend) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(timeout);
			set_current_state(TASK_RUNNING);
			ntimes--;
		}
		pend = brcmf_get_pend_8021x_cnt(drvr);
	}
	return pend;
}

#ifdef DEBUG
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
		filp_close(fp, NULL);
	/* restore previous address limit */
	set_fs(old_fs);

	return ret;
}
#endif				/* DEBUG */

static void brcmf_driver_init(struct work_struct *work)
{
	brcmf_debugfs_init();

#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_init();
#endif
#ifdef CONFIG_BRCMFMAC_USB
	brcmf_usb_init();
#endif
}
static DECLARE_WORK(brcmf_driver_work, brcmf_driver_init);

static int __init brcmfmac_module_init(void)
{
	if (!schedule_work(&brcmf_driver_work))
		return -EBUSY;

	return 0;
}

static void __exit brcmfmac_module_exit(void)
{
	cancel_work_sync(&brcmf_driver_work);

#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_exit();
#endif
#ifdef CONFIG_BRCMFMAC_USB
	brcmf_usb_exit();
#endif
	brcmf_debugfs_exit();
}

module_init(brcmfmac_module_init);
module_exit(brcmfmac_module_exit);
