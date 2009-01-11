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
#include <linux/if_vlan.h>
#include <linux/inet_lro.h>

#include "benet.h"

/* number of bytes of RX frame that are copied to skb->data */
#define BE_HDR_LEN 64

#define NETIF_RX(skb) netif_receive_skb(skb)
#define VLAN_ACCEL_RX(skb, pnob, vt) \
		vlan_hwaccel_rx(skb, pnob->vlan_grp, vt)

/*
    This function notifies BladeEngine of the number of completion
    entries processed from the specified completion queue by writing
    the number of popped entries to the door bell.

    pnob	- Pointer to the NetObject structure
    n		- Number of completion entries processed
    cq_id	- Queue ID of the completion queue for which notification
			is being done.
    re_arm	- 1  - rearm the completion ring to generate an event.
		- 0  - dont rearm the completion ring to generate an event
*/
void be_notify_cmpl(struct be_net_object *pnob, int n, int cq_id, int re_arm)
{
	struct CQ_DB_AMAP cqdb;

	cqdb.dw[0] = 0;
	AMAP_SET_BITS_PTR(CQ_DB, qid, &cqdb, cq_id);
	AMAP_SET_BITS_PTR(CQ_DB, rearm, &cqdb, re_arm);
	AMAP_SET_BITS_PTR(CQ_DB, num_popped, &cqdb, n);
	PD_WRITE(&pnob->fn_obj, cq_db, cqdb.dw[0]);
}

/*
 * adds additional receive frags indicated by BE starting from given
 * frag index (fi) to specified skb's frag list
 */
static void
add_skb_frags(struct be_net_object *pnob, struct sk_buff *skb,
	      u32 nresid, u32 fi)
{
	struct be_adapter *adapter = pnob->adapter;
	u32 sk_frag_idx, n;
	struct be_rx_page_info *rx_page_info;
	u32 frag_sz = pnob->rx_buf_size;

	sk_frag_idx = skb_shinfo(skb)->nr_frags;
	while (nresid) {
		index_inc(&fi, pnob->rx_q_len);

		rx_page_info = (struct be_rx_page_info *)pnob->rx_ctxt[fi];
		pnob->rx_ctxt[fi] = NULL;
		if ((rx_page_info->page_offset) ||
		    (pnob->rx_pg_shared == false)) {
			pci_unmap_page(adapter->pdev,
				       pci_unmap_addr(rx_page_info, bus),
				       frag_sz, PCI_DMA_FROMDEVICE);
		}

		n = min(nresid, frag_sz);
		skb_shinfo(skb)->frags[sk_frag_idx].page = rx_page_info->page;
		skb_shinfo(skb)->frags[sk_frag_idx].page_offset
		    = rx_page_info->page_offset;
		skb_shinfo(skb)->frags[sk_frag_idx].size = n;

		sk_frag_idx++;
		skb->len += n;
		skb->data_len += n;
		skb_shinfo(skb)->nr_frags++;
		nresid -= n;

		memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		atomic_dec(&pnob->rx_q_posted);
	}
}

/*
 * This function processes incoming nic packets over various Rx queues.
 * This function takes the adapter, the current Rx status descriptor
 * entry and the Rx completion queue ID as argument.
 */
static inline int process_nic_rx_completion(struct be_net_object *pnob,
					    struct ETH_RX_COMPL_AMAP *rxcp)
{
	struct be_adapter *adapter = pnob->adapter;
	struct sk_buff *skb;
	int udpcksm, tcpcksm;
	int n;
	u32 nresid, fi;
	u32 frag_sz = pnob->rx_buf_size;
	u8 *va;
	struct be_rx_page_info *rx_page_info;
	u32 numfrags, vtp, vtm, vlan_tag, pktsize;

	fi = AMAP_GET_BITS_PTR(ETH_RX_COMPL, fragndx, rxcp);
	BUG_ON(fi >= (int)pnob->rx_q_len);
	BUG_ON(fi < 0);

	rx_page_info = (struct be_rx_page_info *)pnob->rx_ctxt[fi];
	BUG_ON(!rx_page_info->page);
	pnob->rx_ctxt[fi] = NULL;

	/*
	 * If one page is used per fragment or if this is the second half of
	 *  of the page, unmap the page here
	 */
	if ((rx_page_info->page_offset) || (pnob->rx_pg_shared == false)) {
		pci_unmap_page(adapter->pdev,
			       pci_unmap_addr(rx_page_info, bus), frag_sz,
			       PCI_DMA_FROMDEVICE);
	}

	atomic_dec(&pnob->rx_q_posted);
	udpcksm = AMAP_GET_BITS_PTR(ETH_RX_COMPL, udpcksm, rxcp);
	tcpcksm = AMAP_GET_BITS_PTR(ETH_RX_COMPL, tcpcksm, rxcp);
	pktsize = AMAP_GET_BITS_PTR(ETH_RX_COMPL, pktsize, rxcp);
	/*
	 * get rid of RX flush completions first.
	 */
	if ((tcpcksm) && (udpcksm) && (pktsize == 32)) {
		put_page(rx_page_info->page);
		memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		return 0;
	}
	skb = netdev_alloc_skb(pnob->netdev, BE_HDR_LEN + NET_IP_ALIGN);
	if (skb == NULL) {
		dev_info(&pnob->netdev->dev, "alloc_skb() failed\n");
		put_page(rx_page_info->page);
		memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		goto free_frags;
	}
	skb_reserve(skb, NET_IP_ALIGN);

	skb->dev = pnob->netdev;

	n = min(pktsize, frag_sz);

	va = page_address(rx_page_info->page) + rx_page_info->page_offset;
	prefetch(va);

	skb->len = n;
	skb->data_len = n;
	if (n <= BE_HDR_LEN) {
		memcpy(skb->data, va, n);
		put_page(rx_page_info->page);
		skb->data_len -= n;
		skb->tail += n;
	} else {

		/* Setup the SKB with page buffer information */
		skb_shinfo(skb)->frags[0].page = rx_page_info->page;
		skb_shinfo(skb)->nr_frags++;

		/* Copy the header into the skb_data */
		memcpy(skb->data, va, BE_HDR_LEN);
		skb_shinfo(skb)->frags[0].page_offset =
		    rx_page_info->page_offset + BE_HDR_LEN;
		skb_shinfo(skb)->frags[0].size = n - BE_HDR_LEN;
		skb->data_len -= BE_HDR_LEN;
		skb->tail += BE_HDR_LEN;
	}
	memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
	nresid = pktsize - n;

	skb->protocol = eth_type_trans(skb, pnob->netdev);

	if ((tcpcksm || udpcksm) && adapter->rx_csum)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;
	/*
	 * if we have more bytes left, the frame has been
	 * given to us in multiple fragments.  This happens
	 * with Jumbo frames. Add the remaining fragments to
	 * skb->frags[] array.
	 */
	if (nresid)
		add_skb_frags(pnob, skb, nresid, fi);

	/* update the the true size of the skb. */
	skb->truesize = skb->len + sizeof(struct sk_buff);

	/*
	 * If a 802.3 frame or 802.2 LLC frame
	 * (i.e) contains length field in MAC Hdr
	 * and frame len is greater than 64 bytes
	 */
	if (((skb->protocol == ntohs(ETH_P_802_2)) ||
	     (skb->protocol == ntohs(ETH_P_802_3)))
	    && (pktsize > BE_HDR_LEN)) {
		/*
		 * If the length given in Mac Hdr is less than frame size
		 * Erraneous frame, Drop it
		 */
		if ((ntohs(*(u16 *) (va + 12)) + ETH_HLEN) < pktsize) {
			/* Increment Non Ether type II frames dropped */
			adapter->be_stat.bes_802_3_dropped_frames++;

			kfree_skb(skb);
			return 0;
		}
		/*
		 * else if the length given in Mac Hdr is greater than
		 * frame size, should not be seeing this sort of frames
		 * dump the pkt and pass to stack
		 */
		else if ((ntohs(*(u16 *) (va + 12)) + ETH_HLEN) > pktsize) {
			/* Increment Non Ether type II frames malformed */
			adapter->be_stat.bes_802_3_malformed_frames++;
		}
	}

	vtp = AMAP_GET_BITS_PTR(ETH_RX_COMPL, vtp, rxcp);
	vtm = AMAP_GET_BITS_PTR(ETH_RX_COMPL, vtm, rxcp);
	if (vtp && vtm) {
		/* Vlan tag present in pkt and BE found
		 * that the tag matched an entry in VLAN table
		 */
		if (!pnob->vlan_grp || pnob->num_vlans == 0) {
			/* But we have no VLANs configured.
			 * This should never happen.  Drop the packet.
			 */
			dev_info(&pnob->netdev->dev,
			       "BladeEngine: Unexpected vlan tagged packet\n");
			kfree_skb(skb);
			return 0;
		}
		/* pass the VLAN packet to stack */
		vlan_tag = AMAP_GET_BITS_PTR(ETH_RX_COMPL, vlan_tag, rxcp);
		VLAN_ACCEL_RX(skb, pnob, be16_to_cpu(vlan_tag));

	} else {
		NETIF_RX(skb);
	}
	return 0;

free_frags:
	/* free all frags associated with the current rxcp */
	numfrags = AMAP_GET_BITS_PTR(ETH_RX_COMPL, numfrags, rxcp);
	while (numfrags-- > 1) {
		index_inc(&fi, pnob->rx_q_len);

		rx_page_info = (struct be_rx_page_info *)
		    pnob->rx_ctxt[fi];
		pnob->rx_ctxt[fi] = (void *)NULL;
		if (rx_page_info->page_offset || !pnob->rx_pg_shared) {
			pci_unmap_page(adapter->pdev,
				       pci_unmap_addr(rx_page_info, bus),
				       frag_sz, PCI_DMA_FROMDEVICE);
		}

		put_page(rx_page_info->page);
		memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		atomic_dec(&pnob->rx_q_posted);
	}
	return -ENOMEM;
}

static void process_nic_rx_completion_lro(struct be_net_object *pnob,
					  struct ETH_RX_COMPL_AMAP *rxcp)
{
	struct be_adapter *adapter = pnob->adapter;
	struct skb_frag_struct rx_frags[BE_MAX_FRAGS_PER_FRAME];
	unsigned int udpcksm, tcpcksm;
	u32 numfrags, vlanf, vtm, vlan_tag, nresid;
	u16 vlant;
	unsigned int fi, idx, n;
	struct be_rx_page_info *rx_page_info;
	u32 frag_sz = pnob->rx_buf_size, pktsize;
	bool rx_coal = (adapter->max_rx_coal <= 1) ? 0 : 1;
	u8 err, *va;
	__wsum csum = 0;

	if (AMAP_GET_BITS_PTR(ETH_RX_COMPL, ipsec, rxcp)) {
		/*  Drop the pkt and move to the next completion.  */
		adapter->be_stat.bes_rx_misc_pkts++;
		return;
	}
	err = AMAP_GET_BITS_PTR(ETH_RX_COMPL, err, rxcp);
	if (err || !rx_coal) {
		/* We won't coalesce Rx pkts if the err bit set.
		 * take the path of normal completion processing */
		process_nic_rx_completion(pnob, rxcp);
		return;
	}

	fi = AMAP_GET_BITS_PTR(ETH_RX_COMPL, fragndx, rxcp);
	BUG_ON(fi >= (int)pnob->rx_q_len);
	BUG_ON(fi < 0);
	rx_page_info = (struct be_rx_page_info *)pnob->rx_ctxt[fi];
	BUG_ON(!rx_page_info->page);
	pnob->rx_ctxt[fi] = (void *)NULL;
	/*  If one page is used per fragment or if this is the
	 * second half of the page, unmap the page here
	 */
	if (rx_page_info->page_offset || !pnob->rx_pg_shared) {
		pci_unmap_page(adapter->pdev,
			       pci_unmap_addr(rx_page_info, bus),
			       frag_sz, PCI_DMA_FROMDEVICE);
	}

	numfrags = AMAP_GET_BITS_PTR(ETH_RX_COMPL, numfrags, rxcp);
	udpcksm = AMAP_GET_BITS_PTR(ETH_RX_COMPL, udpcksm, rxcp);
	tcpcksm = AMAP_GET_BITS_PTR(ETH_RX_COMPL, tcpcksm, rxcp);
	vlan_tag = AMAP_GET_BITS_PTR(ETH_RX_COMPL, vlan_tag, rxcp);
	vlant = be16_to_cpu(vlan_tag);
	vlanf = AMAP_GET_BITS_PTR(ETH_RX_COMPL, vtp, rxcp);
	vtm = AMAP_GET_BITS_PTR(ETH_RX_COMPL, vtm, rxcp);
	pktsize = AMAP_GET_BITS_PTR(ETH_RX_COMPL, pktsize, rxcp);

	atomic_dec(&pnob->rx_q_posted);

	if (tcpcksm && udpcksm && pktsize == 32) {
		/* flush completion entries */
		put_page(rx_page_info->page);
		memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		return;
	}
	/* Only one of udpcksum and tcpcksum can be set */
	BUG_ON(udpcksm && tcpcksm);

	/* jumbo frames could come in multiple fragments */
	BUG_ON(numfrags != ((pktsize + (frag_sz - 1)) / frag_sz));
	n = min(pktsize, frag_sz);
	nresid = pktsize - n;	/* will be useful for jumbo pkts */
	idx = 0;

	va = page_address(rx_page_info->page) + rx_page_info->page_offset;
	prefetch(va);
	rx_frags[idx].page = rx_page_info->page;
	rx_frags[idx].page_offset = (rx_page_info->page_offset);
	rx_frags[idx].size = n;
	memset(rx_page_info, 0, sizeof(struct be_rx_page_info));

	/* If we got multiple fragments, we have more data. */
	while (nresid) {
		idx++;
		index_inc(&fi, pnob->rx_q_len);

		rx_page_info = (struct be_rx_page_info *)pnob->rx_ctxt[fi];
		pnob->rx_ctxt[fi] = (void *)NULL;
		if (rx_page_info->page_offset || !pnob->rx_pg_shared) {
			pci_unmap_page(adapter->pdev,
				       pci_unmap_addr(rx_page_info, bus),
				       frag_sz, PCI_DMA_FROMDEVICE);
		}

		n = min(nresid, frag_sz);
		rx_frags[idx].page = rx_page_info->page;
		rx_frags[idx].page_offset = (rx_page_info->page_offset);
		rx_frags[idx].size = n;

		nresid -= n;
		memset(rx_page_info, 0, sizeof(struct be_rx_page_info));
		atomic_dec(&pnob->rx_q_posted);
	}

	if (likely(!(vlanf && vtm))) {
		lro_receive_frags(&pnob->lro_mgr, rx_frags,
				  pktsize, pktsize,
				  (void *)(unsigned long)csum, csum);
	} else {
		/* Vlan tag present in pkt and BE found
		 * that the tag matched an entry in VLAN table
		 */
		if (unlikely(!pnob->vlan_grp || pnob->num_vlans == 0)) {
			/* But we have no VLANs configured.
			 * This should never happen.  Drop the packet.
			 */
			dev_info(&pnob->netdev->dev,
			       "BladeEngine: Unexpected vlan tagged packet\n");
			return;
		}
		/* pass the VLAN packet to stack */
		lro_vlan_hwaccel_receive_frags(&pnob->lro_mgr,
					       rx_frags, pktsize, pktsize,
					       pnob->vlan_grp, vlant,
					       (void *)(unsigned long)csum,
					       csum);
	}

	adapter->be_stat.bes_rx_coal++;
}

struct ETH_RX_COMPL_AMAP *be_get_rx_cmpl(struct be_net_object *pnob)
{
	struct ETH_RX_COMPL_AMAP *rxcp = &pnob->rx_cq[pnob->rx_cq_tl];
	u32 valid, ct;

	valid = AMAP_GET_BITS_PTR(ETH_RX_COMPL, valid, rxcp);
	if (valid == 0)
		return NULL;

	ct = AMAP_GET_BITS_PTR(ETH_RX_COMPL, ct, rxcp);
	if (ct != 0) {
		/* Invalid chute #. treat as error */
		AMAP_SET_BITS_PTR(ETH_RX_COMPL, err, rxcp, 1);
	}

	be_adv_rxcq_tl(pnob);
	AMAP_SET_BITS_PTR(ETH_RX_COMPL, valid, rxcp, 0);
	return rxcp;
}

static void update_rx_rate(struct be_adapter *adapter)
{
	/* update the rate once in two seconds */
	if ((jiffies - adapter->eth_rx_jiffies) > 2 * (HZ)) {
		u32 r;
		r = adapter->eth_rx_bytes /
		    ((jiffies - adapter->eth_rx_jiffies) / (HZ));
		r = (r / 1000000);	/* MB/Sec */

		/* Mega Bits/Sec */
		adapter->be_stat.bes_eth_rx_rate = (r * 8);
		adapter->eth_rx_jiffies = jiffies;
		adapter->eth_rx_bytes = 0;
	}
}

static int process_rx_completions(struct be_net_object *pnob, int max_work)
{
	struct be_adapter *adapter = pnob->adapter;
	struct ETH_RX_COMPL_AMAP *rxcp;
	u32 nc = 0;
	unsigned int pktsize;

	while (max_work && (rxcp = be_get_rx_cmpl(pnob))) {
		prefetch(rxcp);
		pktsize = AMAP_GET_BITS_PTR(ETH_RX_COMPL, pktsize, rxcp);
		process_nic_rx_completion_lro(pnob, rxcp);
		adapter->eth_rx_bytes += pktsize;
		update_rx_rate(adapter);
		nc++;
		max_work--;
		adapter->be_stat.bes_rx_compl++;
	}
	if (likely(adapter->max_rx_coal > 1)) {
		adapter->be_stat.bes_rx_flush++;
		lro_flush_all(&pnob->lro_mgr);
	}

	/* Refill the queue */
	if (atomic_read(&pnob->rx_q_posted) < 900)
		be_post_eth_rx_buffs(pnob);

	return nc;
}

static struct ETH_TX_COMPL_AMAP *be_get_tx_cmpl(struct be_net_object *pnob)
{
	struct ETH_TX_COMPL_AMAP *txcp = &pnob->tx_cq[pnob->tx_cq_tl];
	u32 valid;

	valid = AMAP_GET_BITS_PTR(ETH_TX_COMPL, valid, txcp);
	if (valid == 0)
		return NULL;

	AMAP_SET_BITS_PTR(ETH_TX_COMPL, valid, txcp, 0);
	be_adv_txcq_tl(pnob);
	return txcp;

}

void process_one_tx_compl(struct be_net_object *pnob, u32 end_idx)
{
	struct be_adapter *adapter = pnob->adapter;
	int cur_index, tx_wrbs_completed = 0;
	struct sk_buff *skb;
	u64 busaddr, pa, pa_lo, pa_hi;
	struct ETH_WRB_AMAP *wrb;
	u32 frag_len, last_index, j;

	last_index = tx_compl_lastwrb_idx_get(pnob);
	BUG_ON(last_index != end_idx);
	pnob->tx_ctxt[pnob->tx_q_tl] = NULL;
	do {
		cur_index = pnob->tx_q_tl;
		wrb = &pnob->tx_q[cur_index];
		pa_hi = AMAP_GET_BITS_PTR(ETH_WRB, frag_pa_hi, wrb);
		pa_lo = AMAP_GET_BITS_PTR(ETH_WRB, frag_pa_lo, wrb);
		frag_len = AMAP_GET_BITS_PTR(ETH_WRB, frag_len, wrb);
		busaddr = (pa_hi << 32) | pa_lo;
		if (busaddr != 0) {
			pa = le64_to_cpu(busaddr);
			pci_unmap_single(adapter->pdev, pa,
					 frag_len, PCI_DMA_TODEVICE);
		}
		if (cur_index == last_index) {
			skb = (struct sk_buff *)pnob->tx_ctxt[cur_index];
			BUG_ON(!skb);
			for (j = 0; j < skb_shinfo(skb)->nr_frags; j++) {
				struct skb_frag_struct *frag;
				frag = &skb_shinfo(skb)->frags[j];
				pci_unmap_page(adapter->pdev,
					       (ulong) frag->page, frag->size,
					       PCI_DMA_TODEVICE);
			}
			kfree_skb(skb);
			pnob->tx_ctxt[cur_index] = NULL;
		} else {
			BUG_ON(pnob->tx_ctxt[cur_index]);
		}
		tx_wrbs_completed++;
		be_adv_txq_tl(pnob);
	} while (cur_index != last_index);
	atomic_sub(tx_wrbs_completed, &pnob->tx_q_used);
}

/* there is no need to take an SMP lock here since currently
 * we have only one instance of the tasklet that does completion
 * processing.
 */
static void process_nic_tx_completions(struct be_net_object *pnob)
{
	struct be_adapter *adapter = pnob->adapter;
	struct ETH_TX_COMPL_AMAP *txcp;
	struct net_device *netdev = pnob->netdev;
	u32 end_idx, num_processed = 0;

	adapter->be_stat.bes_tx_events++;

	while ((txcp = be_get_tx_cmpl(pnob))) {
		end_idx = AMAP_GET_BITS_PTR(ETH_TX_COMPL, wrb_index, txcp);
		process_one_tx_compl(pnob, end_idx);
		num_processed++;
		adapter->be_stat.bes_tx_compl++;
	}
	be_notify_cmpl(pnob, num_processed, pnob->tx_cq_id, 1);
	/*
	 * We got Tx completions and have usable WRBs.
	 * If the netdev's queue has been stopped
	 * because we had run out of WRBs, wake it now.
	 */
	spin_lock(&adapter->txq_lock);
	if (netif_queue_stopped(netdev)
	    && atomic_read(&pnob->tx_q_used) < pnob->tx_q_len / 2) {
		netif_wake_queue(netdev);
	}
	spin_unlock(&adapter->txq_lock);
}

static u32 post_rx_buffs(struct be_net_object *pnob, struct list_head *rxbl)
{
	u32 nposted = 0;
	struct ETH_RX_D_AMAP *rxd = NULL;
	struct be_recv_buffer *rxbp;
	void **rx_ctxp;
	struct RQ_DB_AMAP rqdb;

	rx_ctxp = pnob->rx_ctxt;

	while (!list_empty(rxbl) &&
	       (rx_ctxp[pnob->rx_q_hd] == NULL) && nposted < 255) {

		rxbp = list_first_entry(rxbl, struct be_recv_buffer, rxb_list);
		list_del(&rxbp->rxb_list);
		rxd = pnob->rx_q + pnob->rx_q_hd;
		AMAP_SET_BITS_PTR(ETH_RX_D, fragpa_lo, rxd, rxbp->rxb_pa_lo);
		AMAP_SET_BITS_PTR(ETH_RX_D, fragpa_hi, rxd, rxbp->rxb_pa_hi);

		rx_ctxp[pnob->rx_q_hd] = rxbp->rxb_ctxt;
		be_adv_rxq_hd(pnob);
		nposted++;
	}

	if (nposted) {
		/* Now press the door bell to notify BladeEngine. */
		rqdb.dw[0] = 0;
		AMAP_SET_BITS_PTR(RQ_DB, numPosted, &rqdb, nposted);
		AMAP_SET_BITS_PTR(RQ_DB, rq, &rqdb, pnob->rx_q_id);
		PD_WRITE(&pnob->fn_obj, erx_rq_db, rqdb.dw[0]);
	}
	atomic_add(nposted, &pnob->rx_q_posted);
	return nposted;
}

void be_post_eth_rx_buffs(struct be_net_object *pnob)
{
	struct be_adapter *adapter = pnob->adapter;
	u32 num_bufs, r;
	u64 busaddr = 0, tmp_pa;
	u32 max_bufs, pg_hd;
	u32 frag_size;
	struct be_recv_buffer *rxbp;
	struct list_head rxbl;
	struct be_rx_page_info *rx_page_info;
	struct page *page = NULL;
	u32 page_order = 0;
	gfp_t alloc_flags = GFP_ATOMIC;

	BUG_ON(!adapter);

	max_bufs = 64;		/* should be even # <= 255. */

	frag_size = pnob->rx_buf_size;
	page_order = get_order(frag_size);

	if (frag_size == 8192)
		alloc_flags |= (gfp_t) __GFP_COMP;
	/*
	 * Form a linked list of RECV_BUFFFER structure to be be posted.
	 * We will post even number of buffer so that pages can be
	 * shared.
	 */
	INIT_LIST_HEAD(&rxbl);

	for (num_bufs = 0; num_bufs < max_bufs &&
		!pnob->rx_page_info[pnob->rx_pg_info_hd].page; ++num_bufs) {

		rxbp = &pnob->eth_rx_bufs[num_bufs];
		pg_hd = pnob->rx_pg_info_hd;
		rx_page_info = &pnob->rx_page_info[pg_hd];

		if (!page) {
			page = alloc_pages(alloc_flags, page_order);
			if (unlikely(page == NULL)) {
				adapter->be_stat.bes_ethrx_post_fail++;
				pnob->rxbuf_post_fail++;
				break;
			}
			pnob->rxbuf_post_fail = 0;
			busaddr = pci_map_page(adapter->pdev, page, 0,
					       frag_size, PCI_DMA_FROMDEVICE);
			rx_page_info->page_offset = 0;
			rx_page_info->page = page;
			/*
			 * If we are sharing a page among two skbs,
			 * alloc a new one on the next iteration
			 */
			if (pnob->rx_pg_shared == false)
				page = NULL;
		} else {
			get_page(page);
			rx_page_info->page_offset += frag_size;
			rx_page_info->page = page;
			/*
			 * We are finished with the alloced page,
			 * Alloc a new one on the next iteration
			 */
			page = NULL;
		}
		rxbp->rxb_ctxt = (void *)rx_page_info;
		index_inc(&pnob->rx_pg_info_hd, pnob->rx_q_len);

		pci_unmap_addr_set(rx_page_info, bus, busaddr);
		tmp_pa = busaddr + rx_page_info->page_offset;
		rxbp->rxb_pa_lo = (tmp_pa & 0xFFFFFFFF);
		rxbp->rxb_pa_hi = (tmp_pa >> 32);
		rxbp->rxb_len = frag_size;
		list_add_tail(&rxbp->rxb_list, &rxbl);
	}			/* End of for */

	r = post_rx_buffs(pnob, &rxbl);
	BUG_ON(r != num_bufs);
	return;
}

/*
 * Interrupt service for network function.  We just schedule the
 * tasklet which does all completion processing.
 */
irqreturn_t be_int(int irq, void *dev)
{
	struct net_device *netdev = dev;
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;
	u32 isr;

	isr = CSR_READ(&pnob->fn_obj, cev.isr1);
	if (unlikely(!isr))
		return IRQ_NONE;

	spin_lock(&adapter->int_lock);
	adapter->isr |= isr;
	spin_unlock(&adapter->int_lock);

	adapter->be_stat.bes_ints++;

	tasklet_schedule(&adapter->sts_handler);
	return IRQ_HANDLED;
}

/*
 * Poll function called by NAPI with a work budget.
 * We process as many UC. BC and MC receive completions
 * as the budget allows and return the actual number of
 * RX ststutses processed.
 */
int be_poll(struct napi_struct *napi, int budget)
{
	struct be_net_object *pnob =
			container_of(napi, struct be_net_object, napi);
	u32 work_done;

	pnob->adapter->be_stat.bes_polls++;
	work_done = process_rx_completions(pnob, budget);
	BUG_ON(work_done > budget);

	/* All consumed */
	if (work_done < budget) {
		netif_rx_complete(napi);
		/* enable intr */
		be_notify_cmpl(pnob, work_done, pnob->rx_cq_id, 1);
	} else {
		/* More to be consumed; continue with interrupts disabled */
		be_notify_cmpl(pnob, work_done, pnob->rx_cq_id, 0);
	}
	return work_done;
}

static struct EQ_ENTRY_AMAP *get_event(struct be_net_object *pnob)
{
	struct EQ_ENTRY_AMAP *eqp = &(pnob->event_q[pnob->event_q_tl]);
	if (!AMAP_GET_BITS_PTR(EQ_ENTRY, Valid, eqp))
		return NULL;
	be_adv_eq_tl(pnob);
	return eqp;
}

/*
 * Processes all valid events in the event ring associated with given
 * NetObject.  Also, notifies BE the number of events processed.
 */
static inline u32 process_events(struct be_net_object *pnob)
{
	struct be_adapter *adapter = pnob->adapter;
	struct EQ_ENTRY_AMAP *eqp;
	u32 rid, num_events = 0;
	struct net_device *netdev = pnob->netdev;

	while ((eqp = get_event(pnob)) != NULL) {
		adapter->be_stat.bes_events++;
		rid = AMAP_GET_BITS_PTR(EQ_ENTRY, ResourceID, eqp);
		if (rid == pnob->rx_cq_id) {
			adapter->be_stat.bes_rx_events++;
			netif_rx_schedule(&pnob->napi);
		} else if (rid == pnob->tx_cq_id) {
			process_nic_tx_completions(pnob);
		} else if (rid == pnob->mcc_cq_id) {
			be_mcc_process_cq(&pnob->mcc_q_obj, 1);
		} else {
			dev_info(&netdev->dev,
					"Invalid EQ ResourceID %d\n", rid);
		}
		AMAP_SET_BITS_PTR(EQ_ENTRY, Valid, eqp, 0);
		AMAP_SET_BITS_PTR(EQ_ENTRY, ResourceID, eqp, 0);
		num_events++;
	}
	return num_events;
}

static void update_eqd(struct be_adapter *adapter, struct be_net_object *pnob)
{
	int status;
	struct be_eq_object *eq_objectp;

	/* update once a second */
	if ((jiffies - adapter->ips_jiffies) > 1 * (HZ)) {
		/* One second elapsed since last update  */
		u32 r, new_eqd = -1;
		r = adapter->be_stat.bes_ints - adapter->be_stat.bes_prev_ints;
		r = r / ((jiffies - adapter->ips_jiffies) / (HZ));
		adapter->be_stat.bes_ips = r;
		adapter->ips_jiffies = jiffies;
		adapter->be_stat.bes_prev_ints = adapter->be_stat.bes_ints;
		if (r > IPS_HI_WM && adapter->cur_eqd < adapter->max_eqd)
			new_eqd = (adapter->cur_eqd + 8);
		if (r < IPS_LO_WM && adapter->cur_eqd > adapter->min_eqd)
			new_eqd = (adapter->cur_eqd - 8);
		if (adapter->enable_aic && new_eqd != -1) {
			eq_objectp = &pnob->event_q_obj;
			status = be_eq_modify_delay(&pnob->fn_obj, 1,
						    &eq_objectp, &new_eqd, NULL,
						    NULL, NULL);
			if (status == BE_SUCCESS)
				adapter->cur_eqd = new_eqd;
		}
	}
}

/*
    This function notifies BladeEngine of how many events were processed
    from the event queue by ringing the corresponding door bell and
    optionally re-arms the event queue.
    n		- number of events processed
    re_arm	- 1 - re-arm the EQ, 0 - do not re-arm the EQ

*/
static void be_notify_event(struct be_net_object *pnob, int n, int re_arm)
{
	struct CQ_DB_AMAP eqdb;
	eqdb.dw[0] = 0;

	AMAP_SET_BITS_PTR(CQ_DB, qid, &eqdb, pnob->event_q_id);
	AMAP_SET_BITS_PTR(CQ_DB, rearm, &eqdb, re_arm);
	AMAP_SET_BITS_PTR(CQ_DB, event, &eqdb, 1);
	AMAP_SET_BITS_PTR(CQ_DB, num_popped, &eqdb, n);
	/*
	 * Under some situations we see an interrupt and no valid
	 * EQ entry.  To keep going, we need to ring the DB even if
	 * numPOsted is 0.
	 */
	PD_WRITE(&pnob->fn_obj, cq_db, eqdb.dw[0]);
	return;
}

/*
 * Called from the tasklet scheduled by ISR.  All real interrupt processing
 * is done here.
 */
void be_process_intr(unsigned long context)
{
	struct be_adapter *adapter = (struct be_adapter *)context;
	struct be_net_object *pnob = adapter->net_obj;
	u32 isr, n;
	ulong flags = 0;

	isr = adapter->isr;

	/*
	 * we create only one NIC event queue in Linux. Event is
	 * expected only in the first event queue
	 */
	BUG_ON(isr & 0xfffffffe);
	if ((isr & 1) == 0)
		return;		/* not our interrupt */
	n = process_events(pnob);
	/*
	 * Clear the event bit. adapter->isr is  set by
	 * hard interrupt.  Prevent race with lock.
	 */
	spin_lock_irqsave(&adapter->int_lock, flags);
	adapter->isr &= ~1;
	spin_unlock_irqrestore(&adapter->int_lock, flags);
	be_notify_event(pnob, n, 1);
	/*
	 * If previous allocation attempts had failed and
	 * BE has used up all posted buffers, post RX buffers here
	 */
	if (pnob->rxbuf_post_fail && atomic_read(&pnob->rx_q_posted) == 0)
		be_post_eth_rx_buffs(pnob);
	update_eqd(adapter, pnob);
	return;
}
