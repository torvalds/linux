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

#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/inetdevice.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwil_types.h"
#include "p2p.h"
#include "cfg80211.h"
#include "fwil.h"
#include "fwsignal.h"
#include "feature.h"
#include "proto.h"
#include "pcie.h"
#include "common.h"

#define MAX_WAIT_FOR_8021X_TX			msecs_to_jiffies(950)

#define BRCMF_BSSIDX_INVALID			-1

char *brcmf_ifname(struct brcmf_if *ifp)
{
	if (!ifp)
		return "<if_null>";

	if (ifp->ndev)
		return ifp->ndev->name;

	return "<if_none>";
}

struct brcmf_if *brcmf_get_ifp(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_if *ifp;
	s32 bsscfgidx;

	if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
		brcmf_err("ifidx %d out of range\n", ifidx);
		return NULL;
	}

	ifp = NULL;
	bsscfgidx = drvr->if2bss[ifidx];
	if (bsscfgidx >= 0)
		ifp = drvr->iflist[bsscfgidx];

	return ifp;
}

static void _brcmf_set_multicast_list(struct work_struct *work)
{
	struct brcmf_if *ifp;
	struct net_device *ndev;
	struct netdev_hw_addr *ha;
	u32 cmd_value, cnt;
	__le32 cnt_le;
	char *buf, *bufp;
	u32 buflen;
	s32 err;

	ifp = container_of(work, struct brcmf_if, multicast_work);

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	ndev = ifp->ndev;

	/* Determine initial value of allmulti flag */
	cmd_value = (ndev->flags & IFF_ALLMULTI) ? true : false;

	/* Send down the multicast list first. */
	cnt = netdev_mc_count(ndev);
	buflen = sizeof(cnt) + (cnt * ETH_ALEN);
	buf = kmalloc(buflen, GFP_ATOMIC);
	if (!buf)
		return;
	bufp = buf;

	cnt_le = cpu_to_le32(cnt);
	memcpy(bufp, &cnt_le, sizeof(cnt_le));
	bufp += sizeof(cnt_le);

	netdev_for_each_mc_addr(ha, ndev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETH_ALEN);
		bufp += ETH_ALEN;
		cnt--;
	}

	err = brcmf_fil_iovar_data_set(ifp, "mcast_list", buf, buflen);
	if (err < 0) {
		brcmf_err("Setting mcast_list failed, %d\n", err);
		cmd_value = cnt ? true : cmd_value;
	}

	kfree(buf);

	/*
	 * Now send the allmulti setting.  This is based on the setting in the
	 * net_device flags, but might be modified above to be turned on if we
	 * were trying to set some addresses and dongle rejected it...
	 */
	err = brcmf_fil_iovar_int_set(ifp, "allmulti", cmd_value);
	if (err < 0)
		brcmf_err("Setting allmulti failed, %d\n", err);

	/*Finally, pick up the PROMISC flag */
	cmd_value = (ndev->flags & IFF_PROMISC) ? true : false;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PROMISC, cmd_value);
	if (err < 0)
		brcmf_err("Setting BRCMF_C_SET_PROMISC failed, %d\n",
			  err);
}

static void
_brcmf_set_mac_address(struct work_struct *work)
{
	struct brcmf_if *ifp;
	s32 err;

	ifp = container_of(work, struct brcmf_if, setmacaddr_work);

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", ifp->mac_addr,
				       ETH_ALEN);
	if (err < 0) {
		brcmf_err("Setting cur_etheraddr failed, %d\n", err);
	} else {
		brcmf_dbg(TRACE, "MAC address updated to %pM\n",
			  ifp->mac_addr);
		memcpy(ifp->ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
	}
}

#if IS_ENABLED(CONFIG_IPV6)
static void _brcmf_update_ndtable(struct work_struct *work)
{
	struct brcmf_if *ifp;
	int i, ret;

	ifp = container_of(work, struct brcmf_if, ndoffload_work);

	/* clear the table in firmware */
	ret = brcmf_fil_iovar_data_set(ifp, "nd_hostip_clear", NULL, 0);
	if (ret) {
		brcmf_dbg(TRACE, "fail to clear nd ip table err:%d\n", ret);
		return;
	}

	for (i = 0; i < ifp->ipv6addr_idx; i++) {
		ret = brcmf_fil_iovar_data_set(ifp, "nd_hostip",
					       &ifp->ipv6_addr_tbl[i],
					       sizeof(struct in6_addr));
		if (ret)
			brcmf_err("add nd ip err %d\n", ret);
	}
}
#else
static void _brcmf_update_ndtable(struct work_struct *work)
{
}
#endif

static int brcmf_netdev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct sockaddr *sa = (struct sockaddr *)addr;

	memcpy(&ifp->mac_addr, sa->sa_data, ETH_ALEN);
	schedule_work(&ifp->setmacaddr_work);
	return 0;
}

static void brcmf_netdev_set_multicast_list(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	schedule_work(&ifp->multicast_work);
}

static netdev_tx_t brcmf_netdev_start_xmit(struct sk_buff *skb,
					   struct net_device *ndev)
{
	int ret;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct ethhdr *eh = (struct ethhdr *)(skb->data);

	brcmf_dbg(DATA, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	/* Can the device send data? */
	if (drvr->bus_if->state != BRCMF_BUS_UP) {
		brcmf_err("xmit rejected state=%d\n", drvr->bus_if->state);
		netif_stop_queue(ndev);
		dev_kfree_skb(skb);
		ret = -ENODEV;
		goto done;
	}

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < drvr->hdrlen) {
		struct sk_buff *skb2;

		brcmf_dbg(INFO, "%s: insufficient headroom\n",
			  brcmf_ifname(ifp));
		drvr->bus_if->tx_realloc++;
		skb2 = skb_realloc_headroom(skb, drvr->hdrlen);
		dev_kfree_skb(skb);
		skb = skb2;
		if (skb == NULL) {
			brcmf_err("%s: skb_realloc_headroom failed\n",
				  brcmf_ifname(ifp));
			ret = -ENOMEM;
			goto done;
		}
	}

	/* validate length for ether packet */
	if (skb->len < sizeof(*eh)) {
		ret = -EINVAL;
		dev_kfree_skb(skb);
		goto done;
	}

	if (eh->h_proto == htons(ETH_P_PAE))
		atomic_inc(&ifp->pend_8021x_cnt);

	ret = brcmf_fws_process_skb(ifp, skb);

done:
	if (ret) {
		ifp->stats.tx_dropped++;
	} else {
		ifp->stats.tx_packets++;
		ifp->stats.tx_bytes += skb->len;
	}

	/* Return ok: we always eat the packet */
	return NETDEV_TX_OK;
}

void brcmf_txflowblock_if(struct brcmf_if *ifp,
			  enum brcmf_netif_stop_reason reason, bool state)
{
	unsigned long flags;

	if (!ifp || !ifp->ndev)
		return;

	brcmf_dbg(TRACE, "enter: bsscfgidx=%d stop=0x%X reason=%d state=%d\n",
		  ifp->bsscfgidx, ifp->netif_stop, reason, state);

	spin_lock_irqsave(&ifp->netif_stop_lock, flags);
	if (state) {
		if (!ifp->netif_stop)
			netif_stop_queue(ifp->ndev);
		ifp->netif_stop |= reason;
	} else {
		ifp->netif_stop &= ~reason;
		if (!ifp->netif_stop)
			netif_wake_queue(ifp->ndev);
	}
	spin_unlock_irqrestore(&ifp->netif_stop_lock, flags);
}

void brcmf_txflowblock(struct device *dev, bool state)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	brcmf_fws_bus_blocked(drvr, state);
}

void brcmf_netif_rx(struct brcmf_if *ifp, struct sk_buff *skb)
{
	if (skb->pkt_type == PACKET_MULTICAST)
		ifp->stats.multicast++;

	if (!(ifp->ndev->flags & IFF_UP)) {
		brcmu_pkt_buf_free_skb(skb);
		return;
	}

	ifp->stats.rx_bytes += skb->len;
	ifp->stats.rx_packets++;

	brcmf_dbg(DATA, "rx proto=0x%X\n", ntohs(skb->protocol));
	if (in_interrupt())
		netif_rx(skb);
	else
		/* If the receive is not processed inside an ISR,
		 * the softirqd must be woken explicitly to service
		 * the NET_RX_SOFTIRQ.  This is handled by netif_rx_ni().
		 */
		netif_rx_ni(skb);
}

static int brcmf_rx_hdrpull(struct brcmf_pub *drvr, struct sk_buff *skb,
			    struct brcmf_if **ifp)
{
	int ret;

	/* process and remove protocol-specific header */
	ret = brcmf_proto_hdrpull(drvr, true, skb, ifp);

	if (ret || !(*ifp) || !(*ifp)->ndev) {
		if (ret != -ENODATA && *ifp)
			(*ifp)->stats.rx_errors++;
		brcmu_pkt_buf_free_skb(skb);
		return -ENODATA;
	}

	skb->protocol = eth_type_trans(skb, (*ifp)->ndev);
	return 0;
}

void brcmf_rx_frame(struct device *dev, struct sk_buff *skb, bool handle_event)
{
	struct brcmf_if *ifp;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(DATA, "Enter: %s: rxp=%p\n", dev_name(dev), skb);

	if (brcmf_rx_hdrpull(drvr, skb, &ifp))
		return;

	if (brcmf_proto_is_reorder_skb(skb)) {
		brcmf_proto_rxreorder(ifp, skb);
	} else {
		/* Process special event packets */
		if (handle_event)
			brcmf_fweh_process_skb(ifp->drvr, skb);

		brcmf_netif_rx(ifp, skb);
	}
}

void brcmf_rx_event(struct device *dev, struct sk_buff *skb)
{
	struct brcmf_if *ifp;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(EVENT, "Enter: %s: rxp=%p\n", dev_name(dev), skb);

	if (brcmf_rx_hdrpull(drvr, skb, &ifp))
		return;

	brcmf_fweh_process_skb(ifp->drvr, skb);
	brcmu_pkt_buf_free_skb(skb);
}

void brcmf_txfinalize(struct brcmf_if *ifp, struct sk_buff *txp, bool success)
{
	struct ethhdr *eh;
	u16 type;

	eh = (struct ethhdr *)(txp->data);
	type = ntohs(eh->h_proto);

	if (type == ETH_P_PAE) {
		atomic_dec(&ifp->pend_8021x_cnt);
		if (waitqueue_active(&ifp->pend_8021x_wait))
			wake_up(&ifp->pend_8021x_wait);
	}

	if (!success)
		ifp->stats.tx_errors++;

	brcmu_pkt_buf_free_skb(txp);
}

void brcmf_txcomplete(struct device *dev, struct sk_buff *txp, bool success)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_if *ifp;

	/* await txstatus signal for firmware if active */
	if (brcmf_fws_fc_active(drvr->fws)) {
		if (!success)
			brcmf_fws_bustxfail(drvr->fws, txp);
	} else {
		if (brcmf_proto_hdrpull(drvr, false, txp, &ifp))
			brcmu_pkt_buf_free_skb(txp);
		else
			brcmf_txfinalize(ifp, txp, success);
	}
}

static struct net_device_stats *brcmf_netdev_get_stats(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	return &ifp->stats;
}

static void brcmf_ethtool_get_drvinfo(struct net_device *ndev,
				    struct ethtool_drvinfo *info)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	char drev[BRCMU_DOTREV_LEN] = "n/a";

	if (drvr->revinfo.result == 0)
		brcmu_dotrev_str(drvr->revinfo.driverrev, drev);
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, drev, sizeof(info->version));
	strlcpy(info->fw_version, drvr->fwver, sizeof(info->fw_version));
	strlcpy(info->bus_info, dev_name(drvr->bus_if->dev),
		sizeof(info->bus_info));
}

static const struct ethtool_ops brcmf_ethtool_ops = {
	.get_drvinfo = brcmf_ethtool_get_drvinfo,
};

static int brcmf_netdev_stop(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	brcmf_cfg80211_down(ndev);

	brcmf_fil_iovar_data_set(ifp, "arp_hostip_clear", NULL, 0);

	brcmf_net_setcarrier(ifp, false);

	return 0;
}

static int brcmf_netdev_open(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus_if = drvr->bus_if;
	u32 toe_ol;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	/* If bus is not ready, can't continue */
	if (bus_if->state != BRCMF_BUS_UP) {
		brcmf_err("failed bus is not ready\n");
		return -EAGAIN;
	}

	atomic_set(&ifp->pend_8021x_cnt, 0);

	/* Get current TOE mode from dongle */
	if (brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_ol) >= 0
	    && (toe_ol & TOE_TX_CSUM_OL) != 0)
		ndev->features |= NETIF_F_IP_CSUM;
	else
		ndev->features &= ~NETIF_F_IP_CSUM;

	if (brcmf_cfg80211_up(ndev)) {
		brcmf_err("failed to bring up cfg80211\n");
		return -EIO;
	}

	/* Clear, carrier, set when connected or AP mode. */
	netif_carrier_off(ndev);
	return 0;
}

static const struct net_device_ops brcmf_netdev_ops_pri = {
	.ndo_open = brcmf_netdev_open,
	.ndo_stop = brcmf_netdev_stop,
	.ndo_get_stats = brcmf_netdev_get_stats,
	.ndo_start_xmit = brcmf_netdev_start_xmit,
	.ndo_set_mac_address = brcmf_netdev_set_mac_address,
	.ndo_set_rx_mode = brcmf_netdev_set_multicast_list
};

int brcmf_net_attach(struct brcmf_if *ifp, bool rtnl_locked)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;
	s32 err;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx,
		  ifp->mac_addr);
	ndev = ifp->ndev;

	/* set appropriate operations */
	ndev->netdev_ops = &brcmf_netdev_ops_pri;

	ndev->hard_header_len += drvr->hdrlen;
	ndev->ethtool_ops = &brcmf_ethtool_ops;

	drvr->rxsz = ndev->mtu + ndev->hard_header_len +
			      drvr->hdrlen;

	/* set the mac address */
	memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);

	INIT_WORK(&ifp->setmacaddr_work, _brcmf_set_mac_address);
	INIT_WORK(&ifp->multicast_work, _brcmf_set_multicast_list);
	INIT_WORK(&ifp->ndoffload_work, _brcmf_update_ndtable);

	if (rtnl_locked)
		err = register_netdevice(ndev);
	else
		err = register_netdev(ndev);
	if (err != 0) {
		brcmf_err("couldn't register the net device\n");
		goto fail;
	}

	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);
	return 0;

fail:
	drvr->iflist[ifp->bsscfgidx] = NULL;
	ndev->netdev_ops = NULL;
	free_netdev(ndev);
	return -EBADE;
}

static void brcmf_net_detach(struct net_device *ndev)
{
	if (ndev->reg_state == NETREG_REGISTERED)
		unregister_netdev(ndev);
	else
		brcmf_cfg80211_free_netdev(ndev);
}

void brcmf_net_setcarrier(struct brcmf_if *ifp, bool on)
{
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d carrier=%d\n", ifp->bsscfgidx,
		  on);

	ndev = ifp->ndev;
	brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_DISCONNECTED, !on);
	if (on) {
		if (!netif_carrier_ok(ndev))
			netif_carrier_on(ndev);

	} else {
		if (netif_carrier_ok(ndev))
			netif_carrier_off(ndev);
	}
}

static int brcmf_net_p2p_open(struct net_device *ndev)
{
	brcmf_dbg(TRACE, "Enter\n");

	return brcmf_cfg80211_up(ndev);
}

static int brcmf_net_p2p_stop(struct net_device *ndev)
{
	brcmf_dbg(TRACE, "Enter\n");

	return brcmf_cfg80211_down(ndev);
}

static netdev_tx_t brcmf_net_p2p_start_xmit(struct sk_buff *skb,
					    struct net_device *ndev)
{
	if (skb)
		dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops brcmf_netdev_ops_p2p = {
	.ndo_open = brcmf_net_p2p_open,
	.ndo_stop = brcmf_net_p2p_stop,
	.ndo_start_xmit = brcmf_net_p2p_start_xmit
};

static int brcmf_net_p2p_attach(struct brcmf_if *ifp)
{
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx,
		  ifp->mac_addr);
	ndev = ifp->ndev;

	ndev->netdev_ops = &brcmf_netdev_ops_p2p;

	/* set the mac address */
	memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);

	if (register_netdev(ndev) != 0) {
		brcmf_err("couldn't register the p2p net device\n");
		goto fail;
	}

	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

	return 0;

fail:
	ifp->drvr->iflist[ifp->bsscfgidx] = NULL;
	ndev->netdev_ops = NULL;
	free_netdev(ndev);
	return -EBADE;
}

struct brcmf_if *brcmf_add_if(struct brcmf_pub *drvr, s32 bsscfgidx, s32 ifidx,
			      bool is_p2pdev, char *name, u8 *mac_addr)
{
	struct brcmf_if *ifp;
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx, ifidx);

	ifp = drvr->iflist[bsscfgidx];
	/*
	 * Delete the existing interface before overwriting it
	 * in case we missed the BRCMF_E_IF_DEL event.
	 */
	if (ifp) {
		if (ifidx) {
			brcmf_err("ERROR: netdev:%s already exists\n",
				  ifp->ndev->name);
			netif_stop_queue(ifp->ndev);
			brcmf_net_detach(ifp->ndev);
			drvr->iflist[bsscfgidx] = NULL;
		} else {
			brcmf_dbg(INFO, "netdev:%s ignore IF event\n",
				  ifp->ndev->name);
			return ERR_PTR(-EINVAL);
		}
	}

	if (!drvr->settings->p2p_enable && is_p2pdev) {
		/* this is P2P_DEVICE interface */
		brcmf_dbg(INFO, "allocate non-netdev interface\n");
		ifp = kzalloc(sizeof(*ifp), GFP_KERNEL);
		if (!ifp)
			return ERR_PTR(-ENOMEM);
	} else {
		brcmf_dbg(INFO, "allocate netdev interface\n");
		/* Allocate netdev, including space for private structure */
		ndev = alloc_netdev(sizeof(*ifp), is_p2pdev ? "p2p%d" : name,
				    NET_NAME_UNKNOWN, ether_setup);
		if (!ndev)
			return ERR_PTR(-ENOMEM);

		ndev->destructor = brcmf_cfg80211_free_netdev;
		ifp = netdev_priv(ndev);
		ifp->ndev = ndev;
		/* store mapping ifidx to bsscfgidx */
		if (drvr->if2bss[ifidx] == BRCMF_BSSIDX_INVALID)
			drvr->if2bss[ifidx] = bsscfgidx;
	}

	ifp->drvr = drvr;
	drvr->iflist[bsscfgidx] = ifp;
	ifp->ifidx = ifidx;
	ifp->bsscfgidx = bsscfgidx;

	init_waitqueue_head(&ifp->pend_8021x_wait);
	spin_lock_init(&ifp->netif_stop_lock);

	if (mac_addr != NULL)
		memcpy(ifp->mac_addr, mac_addr, ETH_ALEN);

	brcmf_dbg(TRACE, " ==== pid:%x, if:%s (%pM) created ===\n",
		  current->pid, name, ifp->mac_addr);

	return ifp;
}

static void brcmf_del_if(struct brcmf_pub *drvr, s32 bsscfgidx)
{
	struct brcmf_if *ifp;

	ifp = drvr->iflist[bsscfgidx];
	drvr->iflist[bsscfgidx] = NULL;
	if (!ifp) {
		brcmf_err("Null interface, bsscfgidx=%d\n", bsscfgidx);
		return;
	}
	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx,
		  ifp->ifidx);
	if (drvr->if2bss[ifp->ifidx] == bsscfgidx)
		drvr->if2bss[ifp->ifidx] = BRCMF_BSSIDX_INVALID;
	if (ifp->ndev) {
		if (bsscfgidx == 0) {
			if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
				rtnl_lock();
				brcmf_netdev_stop(ifp->ndev);
				rtnl_unlock();
			}
		} else {
			netif_stop_queue(ifp->ndev);
		}

		if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
			cancel_work_sync(&ifp->setmacaddr_work);
			cancel_work_sync(&ifp->multicast_work);
			cancel_work_sync(&ifp->ndoffload_work);
		}
		brcmf_net_detach(ifp->ndev);
	} else {
		/* Only p2p device interfaces which get dynamically created
		 * end up here. In this case the p2p module should be informed
		 * about the removal of the interface within the firmware. If
		 * not then p2p commands towards the firmware will cause some
		 * serious troublesome side effects. The p2p module will clean
		 * up the ifp if needed.
		 */
		brcmf_p2p_ifp_removed(ifp);
		kfree(ifp);
	}
}

void brcmf_remove_interface(struct brcmf_if *ifp)
{
	if (!ifp || WARN_ON(ifp->drvr->iflist[ifp->bsscfgidx] != ifp))
		return;
	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", ifp->bsscfgidx,
		  ifp->ifidx);
	brcmf_fws_del_interface(ifp);
	brcmf_del_if(ifp->drvr, ifp->bsscfgidx);
}

int brcmf_get_next_free_bsscfgidx(struct brcmf_pub *drvr)
{
	int ifidx;
	int bsscfgidx;
	bool available;
	int highest;

	available = false;
	bsscfgidx = 2;
	highest = 2;
	for (ifidx = 0; ifidx < BRCMF_MAX_IFS; ifidx++) {
		if (drvr->iflist[ifidx]) {
			if (drvr->iflist[ifidx]->bsscfgidx == bsscfgidx)
				bsscfgidx = highest + 1;
			else if (drvr->iflist[ifidx]->bsscfgidx > highest)
				highest = drvr->iflist[ifidx]->bsscfgidx;
		} else {
			available = true;
		}
	}

	return available ? bsscfgidx : -ENOMEM;
}

#ifdef CONFIG_INET
#define ARPOL_MAX_ENTRIES	8
static int brcmf_inetaddr_changed(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct brcmf_pub *drvr = container_of(nb, struct brcmf_pub,
					      inetaddr_notifier);
	struct in_ifaddr *ifa = data;
	struct net_device *ndev = ifa->ifa_dev->dev;
	struct brcmf_if *ifp;
	int idx, i, ret;
	u32 val;
	__be32 addr_table[ARPOL_MAX_ENTRIES] = {0};

	/* Find out if the notification is meant for us */
	for (idx = 0; idx < BRCMF_MAX_IFS; idx++) {
		ifp = drvr->iflist[idx];
		if (ifp && ifp->ndev == ndev)
			break;
		if (idx == BRCMF_MAX_IFS - 1)
			return NOTIFY_DONE;
	}

	/* check if arp offload is supported */
	ret = brcmf_fil_iovar_int_get(ifp, "arpoe", &val);
	if (ret)
		return NOTIFY_OK;

	/* old version only support primary index */
	ret = brcmf_fil_iovar_int_get(ifp, "arp_version", &val);
	if (ret)
		val = 1;
	if (val == 1)
		ifp = drvr->iflist[0];

	/* retrieve the table from firmware */
	ret = brcmf_fil_iovar_data_get(ifp, "arp_hostip", addr_table,
				       sizeof(addr_table));
	if (ret) {
		brcmf_err("fail to get arp ip table err:%d\n", ret);
		return NOTIFY_OK;
	}

	for (i = 0; i < ARPOL_MAX_ENTRIES; i++)
		if (ifa->ifa_address == addr_table[i])
			break;

	switch (action) {
	case NETDEV_UP:
		if (i == ARPOL_MAX_ENTRIES) {
			brcmf_dbg(TRACE, "add %pI4 to arp table\n",
				  &ifa->ifa_address);
			/* set it directly */
			ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip",
				&ifa->ifa_address, sizeof(ifa->ifa_address));
			if (ret)
				brcmf_err("add arp ip err %d\n", ret);
		}
		break;
	case NETDEV_DOWN:
		if (i < ARPOL_MAX_ENTRIES) {
			addr_table[i] = 0;
			brcmf_dbg(TRACE, "remove %pI4 from arp table\n",
				  &ifa->ifa_address);
			/* clear the table in firmware */
			ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip_clear",
						       NULL, 0);
			if (ret) {
				brcmf_err("fail to clear arp ip table err:%d\n",
					  ret);
				return NOTIFY_OK;
			}
			for (i = 0; i < ARPOL_MAX_ENTRIES; i++) {
				if (addr_table[i] == 0)
					continue;
				ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip",
							       &addr_table[i],
							       sizeof(addr_table[i]));
				if (ret)
					brcmf_err("add arp ip err %d\n",
						  ret);
			}
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int brcmf_inet6addr_changed(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct brcmf_pub *drvr = container_of(nb, struct brcmf_pub,
					      inet6addr_notifier);
	struct inet6_ifaddr *ifa = data;
	struct brcmf_if *ifp;
	int i;
	struct in6_addr *table;

	/* Only handle primary interface */
	ifp = drvr->iflist[0];
	if (!ifp)
		return NOTIFY_DONE;
	if (ifp->ndev != ifa->idev->dev)
		return NOTIFY_DONE;

	table = ifp->ipv6_addr_tbl;
	for (i = 0; i < NDOL_MAX_ENTRIES; i++)
		if (ipv6_addr_equal(&ifa->addr, &table[i]))
			break;

	switch (action) {
	case NETDEV_UP:
		if (i == NDOL_MAX_ENTRIES) {
			if (ifp->ipv6addr_idx < NDOL_MAX_ENTRIES) {
				table[ifp->ipv6addr_idx++] = ifa->addr;
			} else {
				for (i = 0; i < NDOL_MAX_ENTRIES - 1; i++)
					table[i] = table[i + 1];
				table[NDOL_MAX_ENTRIES - 1] = ifa->addr;
			}
		}
		break;
	case NETDEV_DOWN:
		if (i < NDOL_MAX_ENTRIES)
			for (; i < ifp->ipv6addr_idx; i++)
				table[i] = table[i + 1];
		break;
	default:
		break;
	}

	schedule_work(&ifp->ndoffload_work);

	return NOTIFY_OK;
}
#endif

int brcmf_attach(struct device *dev, struct brcmf_mp_device *settings)
{
	struct brcmf_pub *drvr = NULL;
	int ret = 0;
	int i;

	brcmf_dbg(TRACE, "Enter\n");

	/* Allocate primary brcmf_info */
	drvr = kzalloc(sizeof(struct brcmf_pub), GFP_ATOMIC);
	if (!drvr)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(drvr->if2bss); i++)
		drvr->if2bss[i] = BRCMF_BSSIDX_INVALID;

	mutex_init(&drvr->proto_block);

	/* Link to bus module */
	drvr->hdrlen = 0;
	drvr->bus_if = dev_get_drvdata(dev);
	drvr->bus_if->drvr = drvr;
	drvr->settings = settings;

	/* attach debug facilities */
	brcmf_debug_attach(drvr);

	/* Attach and link in the protocol */
	ret = brcmf_proto_attach(drvr);
	if (ret != 0) {
		brcmf_err("brcmf_prot_attach failed\n");
		goto fail;
	}

	/* attach firmware event handler */
	brcmf_fweh_attach(drvr);

	return ret;

fail:
	brcmf_detach(dev);

	return ret;
}

static int brcmf_revinfo_read(struct seq_file *s, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(s->private);
	struct brcmf_rev_info *ri = &bus_if->drvr->revinfo;
	char drev[BRCMU_DOTREV_LEN];
	char brev[BRCMU_BOARDREV_LEN];

	seq_printf(s, "vendorid: 0x%04x\n", ri->vendorid);
	seq_printf(s, "deviceid: 0x%04x\n", ri->deviceid);
	seq_printf(s, "radiorev: %s\n", brcmu_dotrev_str(ri->radiorev, drev));
	seq_printf(s, "chipnum: %u (%x)\n", ri->chipnum, ri->chipnum);
	seq_printf(s, "chiprev: %u\n", ri->chiprev);
	seq_printf(s, "chippkg: %u\n", ri->chippkg);
	seq_printf(s, "corerev: %u\n", ri->corerev);
	seq_printf(s, "boardid: 0x%04x\n", ri->boardid);
	seq_printf(s, "boardvendor: 0x%04x\n", ri->boardvendor);
	seq_printf(s, "boardrev: %s\n", brcmu_boardrev_str(ri->boardrev, brev));
	seq_printf(s, "driverrev: %s\n", brcmu_dotrev_str(ri->driverrev, drev));
	seq_printf(s, "ucoderev: %u\n", ri->ucoderev);
	seq_printf(s, "bus: %u\n", ri->bus);
	seq_printf(s, "phytype: %u\n", ri->phytype);
	seq_printf(s, "phyrev: %u\n", ri->phyrev);
	seq_printf(s, "anarev: %u\n", ri->anarev);
	seq_printf(s, "nvramrev: %08x\n", ri->nvramrev);

	return 0;
}

int brcmf_bus_start(struct device *dev)
{
	int ret = -1;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_if *ifp;
	struct brcmf_if *p2p_ifp;

	brcmf_dbg(TRACE, "\n");

	/* add primary networking interface */
	ifp = brcmf_add_if(drvr, 0, 0, false, "wlan%d", NULL);
	if (IS_ERR(ifp))
		return PTR_ERR(ifp);

	p2p_ifp = NULL;

	/* signal bus ready */
	brcmf_bus_change_state(bus_if, BRCMF_BUS_UP);

	/* Bus is ready, do any initialization */
	ret = brcmf_c_preinit_dcmds(ifp);
	if (ret < 0)
		goto fail;

	brcmf_debugfs_add_entry(drvr, "revinfo", brcmf_revinfo_read);

	/* assure we have chipid before feature attach */
	if (!bus_if->chip) {
		bus_if->chip = drvr->revinfo.chipnum;
		bus_if->chiprev = drvr->revinfo.chiprev;
		brcmf_dbg(INFO, "firmware revinfo: chip %x (%d) rev %d\n",
			  bus_if->chip, bus_if->chip, bus_if->chiprev);
	}
	brcmf_feat_attach(drvr);

	ret = brcmf_fws_init(drvr);
	if (ret < 0)
		goto fail;

	brcmf_fws_add_interface(ifp);

	drvr->config = brcmf_cfg80211_attach(drvr, bus_if->dev,
					     drvr->settings->p2p_enable);
	if (drvr->config == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = brcmf_net_attach(ifp, false);

	if ((!ret) && (drvr->settings->p2p_enable)) {
		p2p_ifp = drvr->iflist[1];
		if (p2p_ifp)
			ret = brcmf_net_p2p_attach(p2p_ifp);
	}

	if (ret)
		goto fail;

#ifdef CONFIG_INET
	drvr->inetaddr_notifier.notifier_call = brcmf_inetaddr_changed;
	ret = register_inetaddr_notifier(&drvr->inetaddr_notifier);
	if (ret)
		goto fail;

#if IS_ENABLED(CONFIG_IPV6)
	drvr->inet6addr_notifier.notifier_call = brcmf_inet6addr_changed;
	ret = register_inet6addr_notifier(&drvr->inet6addr_notifier);
	if (ret) {
		unregister_inetaddr_notifier(&drvr->inetaddr_notifier);
		goto fail;
	}
#endif
#endif /* CONFIG_INET */

	return 0;

fail:
	brcmf_err("failed: %d\n", ret);
	if (drvr->config) {
		brcmf_cfg80211_detach(drvr->config);
		drvr->config = NULL;
	}
	if (drvr->fws) {
		brcmf_fws_del_interface(ifp);
		brcmf_fws_deinit(drvr);
	}
	if (ifp)
		brcmf_net_detach(ifp->ndev);
	if (p2p_ifp)
		brcmf_net_detach(p2p_ifp->ndev);
	drvr->iflist[0] = NULL;
	drvr->iflist[1] = NULL;
	if (drvr->settings->ignore_probe_fail)
		ret = 0;

	return ret;
}

void brcmf_bus_add_txhdrlen(struct device *dev, uint len)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	if (drvr) {
		drvr->hdrlen += len;
	}
}

static void brcmf_bus_detach(struct brcmf_pub *drvr)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (drvr) {
		/* Stop the bus module */
		brcmf_bus_stop(drvr->bus_if);
	}
}

void brcmf_dev_reset(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	if (drvr == NULL)
		return;

	if (drvr->iflist[0])
		brcmf_fil_cmd_int_set(drvr->iflist[0], BRCMF_C_TERMINATED, 1);
}

void brcmf_detach(struct device *dev)
{
	s32 i;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	if (drvr == NULL)
		return;

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&drvr->inetaddr_notifier);
#endif

#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&drvr->inet6addr_notifier);
#endif

	/* stop firmware event handling */
	brcmf_fweh_detach(drvr);
	if (drvr->config)
		brcmf_p2p_detach(&drvr->config->p2p);

	brcmf_bus_change_state(bus_if, BRCMF_BUS_DOWN);

	/* make sure primary interface removed last */
	for (i = BRCMF_MAX_IFS-1; i > -1; i--)
		brcmf_remove_interface(drvr->iflist[i]);

	brcmf_cfg80211_detach(drvr->config);

	brcmf_fws_deinit(drvr);

	brcmf_bus_detach(drvr);

	brcmf_proto_detach(drvr);

	brcmf_debug_detach(drvr);
	bus_if->drvr = NULL;
	kfree(drvr);
}

s32 brcmf_iovar_data_set(struct device *dev, char *name, void *data, u32 len)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_if *ifp = bus_if->drvr->iflist[0];

	return brcmf_fil_iovar_data_set(ifp, name, data, len);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_if *ifp)
{
	return atomic_read(&ifp->pend_8021x_cnt);
}

int brcmf_netdev_wait_pend8021x(struct brcmf_if *ifp)
{
	int err;

	err = wait_event_timeout(ifp->pend_8021x_wait,
				 !brcmf_get_pend_8021x_cnt(ifp),
				 MAX_WAIT_FOR_8021X_TX);

	WARN_ON(!err);

	return !err;
}

void brcmf_bus_change_state(struct brcmf_bus *bus, enum brcmf_bus_state state)
{
	struct brcmf_pub *drvr = bus->drvr;
	struct net_device *ndev;
	int ifidx;

	brcmf_dbg(TRACE, "%d -> %d\n", bus->state, state);
	bus->state = state;

	if (state == BRCMF_BUS_UP) {
		for (ifidx = 0; ifidx < BRCMF_MAX_IFS; ifidx++) {
			if ((drvr->iflist[ifidx]) &&
			    (drvr->iflist[ifidx]->ndev)) {
				ndev = drvr->iflist[ifidx]->ndev;
				if (netif_queue_stopped(ndev))
					netif_wake_queue(ndev);
			}
		}
	}
}

static void brcmf_driver_register(struct work_struct *work)
{
#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_register();
#endif
#ifdef CONFIG_BRCMFMAC_USB
	brcmf_usb_register();
#endif
#ifdef CONFIG_BRCMFMAC_PCIE
	brcmf_pcie_register();
#endif
}
static DECLARE_WORK(brcmf_driver_work, brcmf_driver_register);

int __init brcmf_core_init(void)
{
	if (!schedule_work(&brcmf_driver_work))
		return -EBUSY;

	return 0;
}

void __exit brcmf_core_exit(void)
{
	cancel_work_sync(&brcmf_driver_work);

#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_exit();
#endif
#ifdef CONFIG_BRCMFMAC_USB
	brcmf_usb_exit();
#endif
#ifdef CONFIG_BRCMFMAC_PCIE
	brcmf_pcie_exit();
#endif
}

