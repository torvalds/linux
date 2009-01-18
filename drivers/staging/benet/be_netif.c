/*
 * Copyright (C) 2005 - 2008 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */
/*
 * be_netif.c
 *
 * This file contains various entry points of drivers seen by tcp/ip stack.
 */

#include <linux/if_vlan.h>
#include <linux/in.h>
#include "benet.h"
#include <linux/ip.h>
#include <linux/inet_lro.h>

/* Strings to print Link properties */
static const char *link_speed[] = {
	"Invalid link Speed Value",
	"10 Mbps",
	"100 Mbps",
	"1 Gbps",
	"10 Gbps"
};

static const char *link_duplex[] = {
	"Invalid Duplex Value",
	"Half Duplex",
	"Full Duplex"
};

static const char *link_state[] = {
	"",
	"(active)"
};

void be_print_link_info(struct BE_LINK_STATUS *lnk_status)
{
	u16 si, di, ai;

	/* Port 0 */
	if (lnk_status->mac0_speed && lnk_status->mac0_duplex) {
		/* Port is up and running */
		si = (lnk_status->mac0_speed < 5) ? lnk_status->mac0_speed : 0;
		di = (lnk_status->mac0_duplex < 3) ?
		    lnk_status->mac0_duplex : 0;
		ai = (lnk_status->active_port == 0) ? 1 : 0;
		printk(KERN_INFO "PortNo. 0: Speed - %s %s %s\n",
		       link_speed[si], link_duplex[di], link_state[ai]);
	} else
		printk(KERN_INFO "PortNo. 0: Down\n");

	/* Port 1 */
	if (lnk_status->mac1_speed && lnk_status->mac1_duplex) {
		/* Port is up and running */
		si = (lnk_status->mac1_speed < 5) ? lnk_status->mac1_speed : 0;
		di = (lnk_status->mac1_duplex < 3) ?
		    lnk_status->mac1_duplex : 0;
		ai = (lnk_status->active_port == 0) ? 1 : 0;
		printk(KERN_INFO "PortNo. 1: Speed - %s %s %s\n",
		       link_speed[si], link_duplex[di], link_state[ai]);
	} else
		printk(KERN_INFO "PortNo. 1: Down\n");

	return;
}

static int
be_get_frag_header(struct skb_frag_struct *frag, void **mac_hdr,
		   void **ip_hdr, void **tcpudp_hdr,
		   u64 *hdr_flags, void *priv)
{
	struct ethhdr *eh;
	struct vlan_ethhdr *veh;
	struct iphdr *iph;
	u8 *va = page_address(frag->page) + frag->page_offset;
	unsigned long ll_hlen;

	/* find the mac header, abort if not IPv4 */

	prefetch(va);
	eh = (struct ethhdr *)va;
	*mac_hdr = eh;
	ll_hlen = ETH_HLEN;
	if (eh->h_proto != htons(ETH_P_IP)) {
		if (eh->h_proto == htons(ETH_P_8021Q)) {
			veh = (struct vlan_ethhdr *)va;
			if (veh->h_vlan_encapsulated_proto != htons(ETH_P_IP))
				return -1;

			ll_hlen += VLAN_HLEN;

		} else {
			return -1;
		}
	}
	*hdr_flags = LRO_IPV4;

	iph = (struct iphdr *)(va + ll_hlen);
	*ip_hdr = iph;
	if (iph->protocol != IPPROTO_TCP)
		return -1;
	*hdr_flags |= LRO_TCP;
	*tcpudp_hdr = (u8 *) (*ip_hdr) + (iph->ihl << 2);

	return 0;
}

static int benet_open(struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;
	struct net_lro_mgr *lro_mgr;

	if (adapter->dev_state < BE_DEV_STATE_INIT)
		return -EAGAIN;

	lro_mgr = &pnob->lro_mgr;
	lro_mgr->dev = netdev;

	lro_mgr->features = LRO_F_NAPI;
	lro_mgr->ip_summed = CHECKSUM_UNNECESSARY;
	lro_mgr->ip_summed_aggr = CHECKSUM_UNNECESSARY;
	lro_mgr->max_desc = BE_MAX_LRO_DESCRIPTORS;
	lro_mgr->lro_arr = pnob->lro_desc;
	lro_mgr->get_frag_header = be_get_frag_header;
	lro_mgr->max_aggr = adapter->max_rx_coal;
	lro_mgr->frag_align_pad = 2;
	if (lro_mgr->max_aggr > MAX_SKB_FRAGS)
		lro_mgr->max_aggr = MAX_SKB_FRAGS;

	adapter->max_rx_coal = BE_LRO_MAX_PKTS;

	be_update_link_status(adapter);

	/*
	 * Set carrier on only if Physical Link up
	 * Either of the port link status up signifies this
	 */
	if ((adapter->port0_link_sts == BE_PORT_LINK_UP) ||
	    (adapter->port1_link_sts == BE_PORT_LINK_UP)) {
		netif_start_queue(netdev);
		netif_carrier_on(netdev);
	}

	adapter->dev_state = BE_DEV_STATE_OPEN;
	napi_enable(&pnob->napi);
	be_enable_intr(pnob);
	be_enable_eq_intr(pnob);
	/*
	 * RX completion queue may be in dis-armed state. Arm it.
	 */
	be_notify_cmpl(pnob, 0, pnob->rx_cq_id, 1);

	return 0;
}

static int benet_close(struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;

	netif_stop_queue(netdev);
	synchronize_irq(netdev->irq);

	be_wait_nic_tx_cmplx_cmpl(pnob);
	adapter->dev_state = BE_DEV_STATE_INIT;
	netif_carrier_off(netdev);

	adapter->port0_link_sts = BE_PORT_LINK_DOWN;
	adapter->port1_link_sts = BE_PORT_LINK_DOWN;
	be_disable_intr(pnob);
	be_disable_eq_intr(pnob);
	napi_disable(&pnob->napi);

	return 0;
}

/*
 * Setting a Mac Address for BE
 * Takes netdev and a void pointer as arguments.
 * The pointer holds the new addres to be used.
 */
static int benet_set_mac_addr(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;
	struct be_net_object *pnob = netdev_priv(netdev);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	be_rxf_mac_address_read_write(&pnob->fn_obj, 0, 0, false, true, false,
				netdev->dev_addr, NULL, NULL);
	/*
	 * Since we are doing Active-Passive failover, both
	 * ports should have matching MAC addresses everytime.
	 */
	be_rxf_mac_address_read_write(&pnob->fn_obj, 1, 0, false, true, false,
				      netdev->dev_addr, NULL, NULL);

	return 0;
}

void be_get_stats_timer_handler(unsigned long context)
{
	struct be_timer_ctxt *ctxt = (struct be_timer_ctxt *)context;

	if (atomic_read(&ctxt->get_stat_flag)) {
		atomic_dec(&ctxt->get_stat_flag);
		up((void *)ctxt->get_stat_sem_addr);
	}
	del_timer(&ctxt->get_stats_timer);
	return;
}

void be_get_stat_cb(void *context, int status,
		    struct MCC_WRB_AMAP *optional_wrb)
{
	struct be_timer_ctxt *ctxt = (struct be_timer_ctxt *)context;
	/*
	 * just up the semaphore if the get_stat_flag
	 * reads 1. so that the waiter can continue.
	 * If it is 0, then it was handled by the timer handler.
	 */
	del_timer(&ctxt->get_stats_timer);
	if (atomic_read(&ctxt->get_stat_flag)) {
		atomic_dec(&ctxt->get_stat_flag);
		up((void *)ctxt->get_stat_sem_addr);
	}
}

struct net_device_stats *benet_get_stats(struct net_device *dev)
{
	struct be_net_object *pnob = netdev_priv(dev);
	struct be_adapter *adapter = pnob->adapter;
	u64 pa;
	struct be_timer_ctxt *ctxt = &adapter->timer_ctxt;

	if (adapter->dev_state != BE_DEV_STATE_OPEN) {
		/* Return previously read stats */
		return &(adapter->benet_stats);
	}
	/* Get Physical Addr */
	pa = pci_map_single(adapter->pdev, adapter->eth_statsp,
			    sizeof(struct FWCMD_ETH_GET_STATISTICS),
			    PCI_DMA_FROMDEVICE);
	ctxt->get_stat_sem_addr = (unsigned long)&adapter->get_eth_stat_sem;
	atomic_inc(&ctxt->get_stat_flag);

	be_rxf_query_eth_statistics(&pnob->fn_obj, adapter->eth_statsp,
				    cpu_to_le64(pa), be_get_stat_cb, ctxt,
				    NULL);

	ctxt->get_stats_timer.data = (unsigned long)ctxt;
	mod_timer(&ctxt->get_stats_timer, (jiffies + (HZ * 2)));
	down((void *)ctxt->get_stat_sem_addr);	/* callback will unblock us */

	/* Adding port0 and port1 stats. */
	adapter->benet_stats.rx_packets =
	    adapter->eth_statsp->params.response.p0recvdtotalframes +
	    adapter->eth_statsp->params.response.p1recvdtotalframes;
	adapter->benet_stats.tx_packets =
	    adapter->eth_statsp->params.response.p0xmitunicastframes +
	    adapter->eth_statsp->params.response.p1xmitunicastframes;
	adapter->benet_stats.tx_bytes =
	    adapter->eth_statsp->params.response.p0xmitbyteslsd +
	    adapter->eth_statsp->params.response.p1xmitbyteslsd;
	adapter->benet_stats.rx_errors =
	    adapter->eth_statsp->params.response.p0crcerrors +
	    adapter->eth_statsp->params.response.p1crcerrors;
	adapter->benet_stats.rx_errors +=
	    adapter->eth_statsp->params.response.p0alignmentsymerrs +
	    adapter->eth_statsp->params.response.p1alignmentsymerrs;
	adapter->benet_stats.rx_errors +=
	    adapter->eth_statsp->params.response.p0inrangelenerrors +
	    adapter->eth_statsp->params.response.p1inrangelenerrors;
	adapter->benet_stats.rx_bytes =
	    adapter->eth_statsp->params.response.p0recvdtotalbytesLSD +
	    adapter->eth_statsp->params.response.p1recvdtotalbytesLSD;
	adapter->benet_stats.rx_crc_errors =
	    adapter->eth_statsp->params.response.p0crcerrors +
	    adapter->eth_statsp->params.response.p1crcerrors;

	adapter->benet_stats.tx_packets +=
	    adapter->eth_statsp->params.response.p0xmitmulticastframes +
	    adapter->eth_statsp->params.response.p1xmitmulticastframes;
	adapter->benet_stats.tx_packets +=
	    adapter->eth_statsp->params.response.p0xmitbroadcastframes +
	    adapter->eth_statsp->params.response.p1xmitbroadcastframes;
	adapter->benet_stats.tx_errors = 0;

	adapter->benet_stats.multicast =
	    adapter->eth_statsp->params.response.p0xmitmulticastframes +
	    adapter->eth_statsp->params.response.p1xmitmulticastframes;

	adapter->benet_stats.rx_fifo_errors =
	    adapter->eth_statsp->params.response.p0rxfifooverflowdropped +
	    adapter->eth_statsp->params.response.p1rxfifooverflowdropped;
	adapter->benet_stats.rx_frame_errors =
	    adapter->eth_statsp->params.response.p0alignmentsymerrs +
	    adapter->eth_statsp->params.response.p1alignmentsymerrs;
	adapter->benet_stats.rx_length_errors =
	    adapter->eth_statsp->params.response.p0inrangelenerrors +
	    adapter->eth_statsp->params.response.p1inrangelenerrors;
	adapter->benet_stats.rx_length_errors +=
	    adapter->eth_statsp->params.response.p0outrangeerrors +
	    adapter->eth_statsp->params.response.p1outrangeerrors;
	adapter->benet_stats.rx_length_errors +=
	    adapter->eth_statsp->params.response.p0frametoolongerrors +
	    adapter->eth_statsp->params.response.p1frametoolongerrors;

	pci_unmap_single(adapter->pdev, (ulong) adapter->eth_statsp,
			 sizeof(struct FWCMD_ETH_GET_STATISTICS),
			 PCI_DMA_FROMDEVICE);
	return &(adapter->benet_stats);

}

static void be_start_tx(struct be_net_object *pnob, u32 nposted)
{
#define CSR_ETH_MAX_SQPOSTS 255
	struct SQ_DB_AMAP sqdb;

	sqdb.dw[0] = 0;

	AMAP_SET_BITS_PTR(SQ_DB, cid, &sqdb, pnob->tx_q_id);
	while (nposted) {
		if (nposted > CSR_ETH_MAX_SQPOSTS) {
			AMAP_SET_BITS_PTR(SQ_DB, numPosted, &sqdb,
					  CSR_ETH_MAX_SQPOSTS);
			nposted -= CSR_ETH_MAX_SQPOSTS;
		} else {
			AMAP_SET_BITS_PTR(SQ_DB, numPosted, &sqdb, nposted);
			nposted = 0;
		}
		PD_WRITE(&pnob->fn_obj, etx_sq_db, sqdb.dw[0]);
	}

	return;
}

static void update_tx_rate(struct be_adapter *adapter)
{
	/* update the rate once in two seconds */
	if ((jiffies - adapter->eth_tx_jiffies) > 2 * (HZ)) {
		u32 r;
		r = adapter->eth_tx_bytes /
		    ((jiffies - adapter->eth_tx_jiffies) / (HZ));
		r = (r / 1000000);	/* M bytes/s */
		adapter->be_stat.bes_eth_tx_rate = (r * 8); /* M bits/s */
		adapter->eth_tx_jiffies = jiffies;
		adapter->eth_tx_bytes = 0;
	}
}

static int wrb_cnt_in_skb(struct sk_buff *skb)
{
	int cnt = 0;
	while (skb) {
		if (skb->len > skb->data_len)
			cnt++;
		cnt += skb_shinfo(skb)->nr_frags;
		skb = skb_shinfo(skb)->frag_list;
	}
	BUG_ON(cnt > BE_MAX_TX_FRAG_COUNT);
	return cnt;
}

static void wrb_fill(struct ETH_WRB_AMAP *wrb, u64 addr, int len)
{
	AMAP_SET_BITS_PTR(ETH_WRB, frag_pa_hi, wrb, addr >> 32);
	AMAP_SET_BITS_PTR(ETH_WRB, frag_pa_lo, wrb, addr & 0xFFFFFFFF);
	AMAP_SET_BITS_PTR(ETH_WRB, frag_len, wrb, len);
}

static void wrb_fill_extra(struct ETH_WRB_AMAP *wrb, struct sk_buff *skb,
			   struct be_net_object *pnob)
{
	wrb->dw[2] = 0;
	wrb->dw[3] = 0;
	AMAP_SET_BITS_PTR(ETH_WRB, crc, wrb, 1);
	if (skb_shinfo(skb)->gso_segs > 1 && skb_shinfo(skb)->gso_size) {
		AMAP_SET_BITS_PTR(ETH_WRB, lso, wrb, 1);
		AMAP_SET_BITS_PTR(ETH_WRB, lso_mss, wrb,
				  skb_shinfo(skb)->gso_size);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u8 proto = ((struct iphdr *)ip_hdr(skb))->protocol;
		if (proto == IPPROTO_TCP)
			AMAP_SET_BITS_PTR(ETH_WRB, tcpcs, wrb, 1);
		else if (proto == IPPROTO_UDP)
			AMAP_SET_BITS_PTR(ETH_WRB, udpcs, wrb, 1);
	}
	if (pnob->vlan_grp && vlan_tx_tag_present(skb)) {
		AMAP_SET_BITS_PTR(ETH_WRB, vlan, wrb, 1);
		AMAP_SET_BITS_PTR(ETH_WRB, vlan_tag, wrb, vlan_tx_tag_get(skb));
	}
}

static inline void wrb_copy_extra(struct ETH_WRB_AMAP *to,
				  struct ETH_WRB_AMAP *from)
{

	to->dw[2] = from->dw[2];
	to->dw[3] = from->dw[3];
}

/* Returns the actual count of wrbs used including a possible dummy */
static int copy_skb_to_txq(struct be_net_object *pnob, struct sk_buff *skb,
			   u32 wrb_cnt, u32 *copied)
{
	u64 busaddr;
	struct ETH_WRB_AMAP *wrb = NULL, *first = NULL;
	u32 i;
	bool dummy = true;
	struct pci_dev *pdev = pnob->adapter->pdev;

	if (wrb_cnt & 1)
		wrb_cnt++;
	else
		dummy = false;

	atomic_add(wrb_cnt, &pnob->tx_q_used);

	while (skb) {
		if (skb->len > skb->data_len) {
			int len = skb->len - skb->data_len;
			busaddr = pci_map_single(pdev, skb->data, len,
						 PCI_DMA_TODEVICE);
			busaddr = cpu_to_le64(busaddr);
			wrb = &pnob->tx_q[pnob->tx_q_hd];
			if (first == NULL) {
				wrb_fill_extra(wrb, skb, pnob);
				first = wrb;
			} else {
				wrb_copy_extra(wrb, first);
			}
			wrb_fill(wrb, busaddr, len);
			be_adv_txq_hd(pnob);
			*copied += len;
		}

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			struct skb_frag_struct *frag =
			    &skb_shinfo(skb)->frags[i];
			busaddr = pci_map_page(pdev, frag->page,
					       frag->page_offset, frag->size,
					       PCI_DMA_TODEVICE);
			busaddr = cpu_to_le64(busaddr);
			wrb = &pnob->tx_q[pnob->tx_q_hd];
			if (first == NULL) {
				wrb_fill_extra(wrb, skb, pnob);
				first = wrb;
			} else {
				wrb_copy_extra(wrb, first);
			}
			wrb_fill(wrb, busaddr, frag->size);
			be_adv_txq_hd(pnob);
			*copied += frag->size;
		}
		skb = skb_shinfo(skb)->frag_list;
	}

	if (dummy) {
		wrb = &pnob->tx_q[pnob->tx_q_hd];
		BUG_ON(first == NULL);
		wrb_copy_extra(wrb, first);
		wrb_fill(wrb, 0, 0);
		be_adv_txq_hd(pnob);
	}
	AMAP_SET_BITS_PTR(ETH_WRB, complete, wrb, 1);
	AMAP_SET_BITS_PTR(ETH_WRB, last, wrb, 1);
	return wrb_cnt;
}

/* For each skb transmitted, tx_ctxt stores the num of wrbs in the
 * start index and skb pointer in the end index
 */
static inline void be_tx_wrb_info_remember(struct be_net_object *pnob,
					   struct sk_buff *skb, int wrb_cnt,
					   u32 start)
{
	*(u32 *) (&pnob->tx_ctxt[start]) = wrb_cnt;
	index_adv(&start, wrb_cnt - 1, pnob->tx_q_len);
	pnob->tx_ctxt[start] = skb;
}

static int benet_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;
	u32 wrb_cnt, copied = 0;
	u32 start = pnob->tx_q_hd;

	adapter->be_stat.bes_tx_reqs++;

	wrb_cnt = wrb_cnt_in_skb(skb);
	spin_lock_bh(&adapter->txq_lock);
	if ((pnob->tx_q_len - 2 - atomic_read(&pnob->tx_q_used)) <= wrb_cnt) {
		netif_stop_queue(pnob->netdev);
		spin_unlock_bh(&adapter->txq_lock);
		adapter->be_stat.bes_tx_fails++;
		return NETDEV_TX_BUSY;
	}
	spin_unlock_bh(&adapter->txq_lock);

	wrb_cnt = copy_skb_to_txq(pnob, skb, wrb_cnt, &copied);
	be_tx_wrb_info_remember(pnob, skb, wrb_cnt, start);

	be_start_tx(pnob, wrb_cnt);

	adapter->eth_tx_bytes += copied;
	adapter->be_stat.bes_tx_wrbs += wrb_cnt;
	update_tx_rate(adapter);
	netdev->trans_start = jiffies;

	return NETDEV_TX_OK;
}

/*
 * This is the driver entry point to change the mtu of the device
 * Returns 0 for success and errno for failure.
 */
static int benet_change_mtu(struct net_device *netdev, int new_mtu)
{
	/*
	 * BE supports jumbo frame size upto 9000 bytes including the link layer
	 * header. Considering the different variants of frame formats possible
	 * like VLAN, SNAP/LLC, the maximum possible value for MTU is 8974 bytes
	 */

	if (new_mtu < (ETH_ZLEN + ETH_FCS_LEN) || (new_mtu > BE_MAX_MTU)) {
		dev_info(&netdev->dev, "Invalid MTU requested. "
			       "Must be between %d and %d bytes\n",
				       (ETH_ZLEN + ETH_FCS_LEN), BE_MAX_MTU);
		return -EINVAL;
	}
	dev_info(&netdev->dev, "MTU changed from %d to %d\n",
						netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	return 0;
}

/*
 * This is the driver entry point to register a vlan with the device
 */
static void benet_vlan_register(struct net_device *netdev,
				struct vlan_group *grp)
{
	struct be_net_object *pnob = netdev_priv(netdev);

	be_disable_eq_intr(pnob);
	pnob->vlan_grp = grp;
	pnob->num_vlans = 0;
	be_enable_eq_intr(pnob);
}

/*
 * This is the driver entry point to add a vlan vlan_id
 * with the device netdev
 */
static void benet_vlan_add_vid(struct net_device *netdev, u16 vlan_id)
{
	struct be_net_object *pnob = netdev_priv(netdev);

	if (pnob->num_vlans == (BE_NUM_VLAN_SUPPORTED - 1)) {
		/* no  way to return an error */
		dev_info(&netdev->dev,
		       "BladeEngine: Cannot configure more than %d Vlans\n",
			       BE_NUM_VLAN_SUPPORTED);
		return;
	}
	/* The new vlan tag will be in the slot indicated by num_vlans. */
	pnob->vlan_tag[pnob->num_vlans++] = vlan_id;
	be_rxf_vlan_config(&pnob->fn_obj, false, pnob->num_vlans,
			   pnob->vlan_tag, NULL, NULL, NULL);
}

/*
 * This is the driver entry point to remove a vlan vlan_id
 * with the device netdev
 */
static void benet_vlan_rem_vid(struct net_device *netdev, u16 vlan_id)
{
	struct be_net_object *pnob = netdev_priv(netdev);

	u32 i, value;

	/*
	 * In Blade Engine, we support 32 vlan tag filters across both ports.
	 * To program a vlan tag, the RXF_RTPR_CSR register is used.
	 * Each 32-bit value of RXF_RTDR_CSR can address 2 vlan tag entries.
	 * The Vlan table is of depth 16. thus we support 32 tags.
	 */

	value = vlan_id | VLAN_VALID_BIT;
	for (i = 0; i < BE_NUM_VLAN_SUPPORTED; i++) {
		if (pnob->vlan_tag[i] == vlan_id)
			break;
	}

	if (i == BE_NUM_VLAN_SUPPORTED)
		return;
	/* Now compact the vlan tag array by removing hole created. */
	while ((i + 1) < BE_NUM_VLAN_SUPPORTED) {
		pnob->vlan_tag[i] = pnob->vlan_tag[i + 1];
		i++;
	}
	if ((i + 1) == BE_NUM_VLAN_SUPPORTED)
		pnob->vlan_tag[i] = (u16) 0x0;
	pnob->num_vlans--;
	be_rxf_vlan_config(&pnob->fn_obj, false, pnob->num_vlans,
			   pnob->vlan_tag, NULL, NULL, NULL);
}

/*
 * This function is called to program multicast
 * address in the multicast filter of the ASIC.
 */
static void be_set_multicast_filter(struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct dev_mc_list *mc_ptr;
	u8 mac_addr[32][ETH_ALEN];
	int i;

	if (netdev->flags & IFF_ALLMULTI) {
		/* set BE in Multicast promiscuous */
		be_rxf_multicast_config(&pnob->fn_obj, true, 0, NULL, NULL,
					NULL, NULL);
		return;
	}

	for (mc_ptr = netdev->mc_list, i = 0; mc_ptr;
	     mc_ptr = mc_ptr->next, i++) {
		memcpy(&mac_addr[i][0], mc_ptr->dmi_addr, ETH_ALEN);
	}

	/* reset the promiscuous mode also. */
	be_rxf_multicast_config(&pnob->fn_obj, false, i,
				&mac_addr[0][0], NULL, NULL, NULL);
}

/*
 * This is the driver entry point to set multicast list
 * with the device netdev. This function will be used to
 * set promiscuous mode or multicast promiscuous mode
 * or multicast mode....
 */
static void benet_set_multicast_list(struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);

	if (netdev->flags & IFF_PROMISC) {
		be_rxf_promiscuous(&pnob->fn_obj, 1, 1, NULL, NULL, NULL);
	} else {
		be_rxf_promiscuous(&pnob->fn_obj, 0, 0, NULL, NULL, NULL);
		be_set_multicast_filter(netdev);
	}
}

int benet_init(struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;

	ether_setup(netdev);

	netdev->open = &benet_open;
	netdev->stop = &benet_close;
	netdev->hard_start_xmit = &benet_xmit;

	netdev->get_stats = &benet_get_stats;

	netdev->set_multicast_list = &benet_set_multicast_list;

	netdev->change_mtu = &benet_change_mtu;
	netdev->set_mac_address = &benet_set_mac_addr;

	netdev->vlan_rx_register = benet_vlan_register;
	netdev->vlan_rx_add_vid = benet_vlan_add_vid;
	netdev->vlan_rx_kill_vid = benet_vlan_rem_vid;

	netdev->features =
	    NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_HW_VLAN_RX | NETIF_F_TSO |
	    NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_FILTER | NETIF_F_IP_CSUM;

	netdev->flags |= IFF_MULTICAST;

	/* If device is DAC Capable, set the HIGHDMA flag for netdevice. */
	if (adapter->dma_64bit_cap)
		netdev->features |= NETIF_F_HIGHDMA;

	SET_ETHTOOL_OPS(netdev, &be_ethtool_ops);
	return 0;
}
