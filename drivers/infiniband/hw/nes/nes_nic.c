/*
 * Copyright (c) 2006 - 2009 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/slab.h>
#include <net/tcp.h>

#include <net/inet_common.h>
#include <linux/inet.h>

#include "nes.h"

static struct nic_qp_map nic_qp_mapping_0[] = {
	{16,0,0,1},{24,4,0,0},{28,8,0,0},{32,12,0,0},
	{20,2,2,1},{26,6,2,0},{30,10,2,0},{34,14,2,0},
	{18,1,1,1},{25,5,1,0},{29,9,1,0},{33,13,1,0},
	{22,3,3,1},{27,7,3,0},{31,11,3,0},{35,15,3,0}
};

static struct nic_qp_map nic_qp_mapping_1[] = {
	{18,1,1,1},{25,5,1,0},{29,9,1,0},{33,13,1,0},
	{22,3,3,1},{27,7,3,0},{31,11,3,0},{35,15,3,0}
};

static struct nic_qp_map nic_qp_mapping_2[] = {
	{20,2,2,1},{26,6,2,0},{30,10,2,0},{34,14,2,0}
};

static struct nic_qp_map nic_qp_mapping_3[] = {
	{22,3,3,1},{27,7,3,0},{31,11,3,0},{35,15,3,0}
};

static struct nic_qp_map nic_qp_mapping_4[] = {
	{28,8,0,0},{32,12,0,0}
};

static struct nic_qp_map nic_qp_mapping_5[] = {
	{29,9,1,0},{33,13,1,0}
};

static struct nic_qp_map nic_qp_mapping_6[] = {
	{30,10,2,0},{34,14,2,0}
};

static struct nic_qp_map nic_qp_mapping_7[] = {
	{31,11,3,0},{35,15,3,0}
};

static struct nic_qp_map *nic_qp_mapping_per_function[] = {
	nic_qp_mapping_0, nic_qp_mapping_1, nic_qp_mapping_2, nic_qp_mapping_3,
	nic_qp_mapping_4, nic_qp_mapping_5, nic_qp_mapping_6, nic_qp_mapping_7
};

static const u32 default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
		| NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;
static int debug = -1;
static int nics_per_function = 1;

/**
 * nes_netdev_poll
 */
static int nes_netdev_poll(struct napi_struct *napi, int budget)
{
	struct nes_vnic *nesvnic = container_of(napi, struct nes_vnic, napi);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_hw_nic_cq *nescq = &nesvnic->nic_cq;

	nesvnic->budget = budget;
	nescq->cqes_pending = 0;
	nescq->rx_cqes_completed = 0;
	nescq->cqe_allocs_pending = 0;
	nescq->rx_pkts_indicated = 0;

	nes_nic_ce_handler(nesdev, nescq);

	if (nescq->cqes_pending == 0) {
		napi_complete(napi);
		/* clear out completed cqes and arm */
		nes_write32(nesdev->regs+NES_CQE_ALLOC, NES_CQE_ALLOC_NOTIFY_NEXT |
				nescq->cq_number | (nescq->cqe_allocs_pending << 16));
		nes_read32(nesdev->regs+NES_CQE_ALLOC);
	} else {
		/* clear out completed cqes but don't arm */
		nes_write32(nesdev->regs+NES_CQE_ALLOC,
				nescq->cq_number | (nescq->cqe_allocs_pending << 16));
		nes_debug(NES_DBG_NETDEV, "%s: exiting with work pending\n",
				nesvnic->netdev->name);
	}
	return nescq->rx_pkts_indicated;
}


/**
 * nes_netdev_open - Activate the network interface; ifconfig
 * ethx up.
 */
static int nes_netdev_open(struct net_device *netdev)
{
	u32 macaddr_low;
	u16 macaddr_high;
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	int ret;
	int i;
	struct nes_vnic *first_nesvnic = NULL;
	u32 nic_active_bit;
	u32 nic_active;
	struct list_head *list_pos, *list_temp;

	assert(nesdev != NULL);

	if (nesvnic->netdev_open == 1)
		return 0;

	if (netif_msg_ifup(nesvnic))
		printk(KERN_INFO PFX "%s: enabling interface\n", netdev->name);

	ret = nes_init_nic_qp(nesdev, netdev);
	if (ret) {
		return ret;
	}

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	if ((!nesvnic->of_device_registered) && (nesvnic->rdma_enabled)) {
		nesvnic->nesibdev = nes_init_ofa_device(netdev);
		if (nesvnic->nesibdev == NULL) {
			printk(KERN_ERR PFX "%s: nesvnic->nesibdev alloc failed", netdev->name);
		} else {
			nesvnic->nesibdev->nesvnic = nesvnic;
			ret = nes_register_ofa_device(nesvnic->nesibdev);
			if (ret) {
				printk(KERN_ERR PFX "%s: Unable to register RDMA device, ret = %d\n",
						netdev->name, ret);
			}
		}
	}
	/* Set packet filters */
	nic_active_bit = 1 << nesvnic->nic_index;
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_ACTIVE);
	nic_active |= nic_active_bit;
	nes_write_indexed(nesdev, NES_IDX_NIC_ACTIVE, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_MULTICAST_ENABLE);
	nic_active |= nic_active_bit;
	nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ENABLE, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_BROADCAST_ON);
	nic_active |= nic_active_bit;
	nes_write_indexed(nesdev, NES_IDX_NIC_BROADCAST_ON, nic_active);

	macaddr_high  = ((u16)netdev->dev_addr[0]) << 8;
	macaddr_high += (u16)netdev->dev_addr[1];

	macaddr_low   = ((u32)netdev->dev_addr[2]) << 24;
	macaddr_low  += ((u32)netdev->dev_addr[3]) << 16;
	macaddr_low  += ((u32)netdev->dev_addr[4]) << 8;
	macaddr_low  += (u32)netdev->dev_addr[5];

	/* Program the various MAC regs */
	for (i = 0; i < NES_MAX_PORT_COUNT; i++) {
		if (nesvnic->qp_nic_index[i] == 0xf) {
			break;
		}
		nes_debug(NES_DBG_NETDEV, "i=%d, perfect filter table index= %d, PERF FILTER LOW"
				" (Addr:%08X) = %08X, HIGH = %08X.\n",
				i, nesvnic->qp_nic_index[i],
				NES_IDX_PERFECT_FILTER_LOW+
					(nesvnic->qp_nic_index[i] * 8),
				macaddr_low,
				(u32)macaddr_high | NES_MAC_ADDR_VALID |
				((((u32)nesvnic->nic_index) << 16)));
		nes_write_indexed(nesdev,
				NES_IDX_PERFECT_FILTER_LOW + (nesvnic->qp_nic_index[i] * 8),
				macaddr_low);
		nes_write_indexed(nesdev,
				NES_IDX_PERFECT_FILTER_HIGH + (nesvnic->qp_nic_index[i] * 8),
				(u32)macaddr_high | NES_MAC_ADDR_VALID |
				((((u32)nesvnic->nic_index) << 16)));
	}


	nes_write32(nesdev->regs+NES_CQE_ALLOC, NES_CQE_ALLOC_NOTIFY_NEXT |
			nesvnic->nic_cq.cq_number);
	nes_read32(nesdev->regs+NES_CQE_ALLOC);
	list_for_each_safe(list_pos, list_temp, &nesdev->nesadapter->nesvnic_list[nesdev->mac_index]) {
		first_nesvnic = container_of(list_pos, struct nes_vnic, list);
		if (first_nesvnic->netdev_open == 1)
			break;
	}
	if (first_nesvnic->netdev_open == 0) {
		nes_debug(NES_DBG_INIT, "Setting up MAC interrupt mask.\n");
		nes_write_indexed(nesdev, NES_IDX_MAC_INT_MASK + (0x200 * nesdev->mac_index),
				~(NES_MAC_INT_LINK_STAT_CHG | NES_MAC_INT_XGMII_EXT |
				NES_MAC_INT_TX_UNDERFLOW | NES_MAC_INT_TX_ERROR));
		first_nesvnic = nesvnic;
	}
	if (first_nesvnic->linkup) {
		/* Enable network packets */
		nesvnic->linkup = 1;
		netif_start_queue(netdev);
		netif_carrier_on(netdev);
	}
	napi_enable(&nesvnic->napi);
	nesvnic->netdev_open = 1;

	return 0;
}


/**
 * nes_netdev_stop
 */
static int nes_netdev_stop(struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	u32 nic_active_mask;
	u32 nic_active;
	struct nes_vnic *first_nesvnic = NULL;
	struct list_head *list_pos, *list_temp;

	nes_debug(NES_DBG_SHUTDOWN, "nesvnic=%p, nesdev=%p, netdev=%p %s\n",
			nesvnic, nesdev, netdev, netdev->name);
	if (nesvnic->netdev_open == 0)
		return 0;

	if (netif_msg_ifdown(nesvnic))
		printk(KERN_INFO PFX "%s: disabling interface\n", netdev->name);

	/* Disable network packets */
	napi_disable(&nesvnic->napi);
	netif_stop_queue(netdev);
	list_for_each_safe(list_pos, list_temp, &nesdev->nesadapter->nesvnic_list[nesdev->mac_index]) {
		first_nesvnic = container_of(list_pos, struct nes_vnic, list);
		if ((first_nesvnic->netdev_open == 1) && (first_nesvnic != nesvnic))
			break;
	}

	if ((first_nesvnic->netdev_open == 1) && (first_nesvnic != nesvnic)  &&
		(PCI_FUNC(first_nesvnic->nesdev->pcidev->devfn) !=
		PCI_FUNC(nesvnic->nesdev->pcidev->devfn))) {
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_MASK+
				(0x200*nesdev->mac_index), 0xffffffff);
			nes_write_indexed(first_nesvnic->nesdev,
				NES_IDX_MAC_INT_MASK+
				(0x200*first_nesvnic->nesdev->mac_index),
			~(NES_MAC_INT_LINK_STAT_CHG | NES_MAC_INT_XGMII_EXT |
			NES_MAC_INT_TX_UNDERFLOW | NES_MAC_INT_TX_ERROR));
	} else {
		nes_write_indexed(nesdev, NES_IDX_MAC_INT_MASK+(0x200*nesdev->mac_index), 0xffffffff);
	}

	nic_active_mask = ~((u32)(1 << nesvnic->nic_index));
	nes_write_indexed(nesdev, NES_IDX_PERFECT_FILTER_HIGH+
			(nesvnic->perfect_filter_index*8), 0);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_ACTIVE);
	nic_active &= nic_active_mask;
	nes_write_indexed(nesdev, NES_IDX_NIC_ACTIVE, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL);
	nic_active &= nic_active_mask;
	nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_MULTICAST_ENABLE);
	nic_active &= nic_active_mask;
	nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ENABLE, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL);
	nic_active &= nic_active_mask;
	nes_write_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_BROADCAST_ON);
	nic_active &= nic_active_mask;
	nes_write_indexed(nesdev, NES_IDX_NIC_BROADCAST_ON, nic_active);


	if (nesvnic->of_device_registered) {
		nes_destroy_ofa_device(nesvnic->nesibdev);
		nesvnic->nesibdev = NULL;
		nesvnic->of_device_registered = 0;
	}
	nes_destroy_nic_qp(nesvnic);

	nesvnic->netdev_open = 0;

	return 0;
}


/**
 * nes_nic_send
 */
static int nes_nic_send(struct sk_buff *skb, struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_hw_nic *nesnic = &nesvnic->nic;
	struct nes_hw_nic_sq_wqe *nic_sqe;
	struct tcphdr *tcph;
	__le16 *wqe_fragment_length;
	u32 wqe_misc;
	u16 wqe_fragment_index = 1;	/* first fragment (0) is used by copy buffer */
	u16 skb_fragment_index;
	dma_addr_t bus_address;

	nic_sqe = &nesnic->sq_vbase[nesnic->sq_head];
	wqe_fragment_length = (__le16 *)&nic_sqe->wqe_words[NES_NIC_SQ_WQE_LENGTH_0_TAG_IDX];

	/* setup the VLAN tag if present */
	if (vlan_tx_tag_present(skb)) {
		nes_debug(NES_DBG_NIC_TX, "%s: VLAN packet to send... VLAN = %08X\n",
				netdev->name, vlan_tx_tag_get(skb));
		wqe_misc = NES_NIC_SQ_WQE_TAGVALUE_ENABLE;
		wqe_fragment_length[0] = (__force __le16) vlan_tx_tag_get(skb);
	} else
		wqe_misc = 0;

	/* bump past the vlan tag */
	wqe_fragment_length++;
	/*	wqe_fragment_address = (u64 *)&nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_LOW_IDX]; */

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tcph = tcp_hdr(skb);
		if (1) {
			if (skb_is_gso(skb)) {
				/* nes_debug(NES_DBG_NIC_TX, "%s: TSO request... seg size = %u\n",
						netdev->name, skb_is_gso(skb)); */
				wqe_misc |= NES_NIC_SQ_WQE_LSO_ENABLE |
						NES_NIC_SQ_WQE_COMPLETION | (u16)skb_is_gso(skb);
				set_wqe_32bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_LSO_INFO_IDX,
						((u32)tcph->doff) |
						(((u32)(((unsigned char *)tcph) - skb->data)) << 4));
			} else {
				wqe_misc |= NES_NIC_SQ_WQE_COMPLETION;
			}
		}
	} else {	/* CHECKSUM_HW */
		wqe_misc |= NES_NIC_SQ_WQE_DISABLE_CHKSUM | NES_NIC_SQ_WQE_COMPLETION;
	}

	set_wqe_32bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_TOTAL_LENGTH_IDX,
				skb->len);
	memcpy(&nesnic->first_frag_vbase[nesnic->sq_head].buffer,
			skb->data, min(((unsigned int)NES_FIRST_FRAG_SIZE), skb_headlen(skb)));
	wqe_fragment_length[0] = cpu_to_le16(min(((unsigned int)NES_FIRST_FRAG_SIZE),
			skb_headlen(skb)));
	wqe_fragment_length[1] = 0;
	if (skb_headlen(skb) > NES_FIRST_FRAG_SIZE) {
		if ((skb_shinfo(skb)->nr_frags + 1) > 4) {
			nes_debug(NES_DBG_NIC_TX, "%s: Packet with %u fragments not sent, skb_headlen=%u\n",
					netdev->name, skb_shinfo(skb)->nr_frags + 2, skb_headlen(skb));
			kfree_skb(skb);
			nesvnic->tx_sw_dropped++;
			return NETDEV_TX_LOCKED;
		}
		set_bit(nesnic->sq_head, nesnic->first_frag_overflow);
		bus_address = pci_map_single(nesdev->pcidev, skb->data + NES_FIRST_FRAG_SIZE,
				skb_headlen(skb) - NES_FIRST_FRAG_SIZE, PCI_DMA_TODEVICE);
		wqe_fragment_length[wqe_fragment_index++] =
				cpu_to_le16(skb_headlen(skb) - NES_FIRST_FRAG_SIZE);
		wqe_fragment_length[wqe_fragment_index] = 0;
		set_wqe_64bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_FRAG1_LOW_IDX,
				((u64)(bus_address)));
		nesnic->tx_skb[nesnic->sq_head] = skb;
	}

	if (skb_headlen(skb) == skb->len) {
		if (skb_headlen(skb) <= NES_FIRST_FRAG_SIZE) {
			nic_sqe->wqe_words[NES_NIC_SQ_WQE_LENGTH_2_1_IDX] = 0;
			nesnic->tx_skb[nesnic->sq_head] = skb;
		}
	} else {
		/* Deal with Fragments */
		nesnic->tx_skb[nesnic->sq_head] = skb;
		for (skb_fragment_index = 0; skb_fragment_index < skb_shinfo(skb)->nr_frags;
				skb_fragment_index++) {
			bus_address = pci_map_page( nesdev->pcidev,
					skb_shinfo(skb)->frags[skb_fragment_index].page,
					skb_shinfo(skb)->frags[skb_fragment_index].page_offset,
					skb_shinfo(skb)->frags[skb_fragment_index].size,
					PCI_DMA_TODEVICE);
			wqe_fragment_length[wqe_fragment_index] =
					cpu_to_le16(skb_shinfo(skb)->frags[skb_fragment_index].size);
			set_wqe_64bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_FRAG0_LOW_IDX+(2*wqe_fragment_index),
				bus_address);
			wqe_fragment_index++;
			if (wqe_fragment_index < 5)
				wqe_fragment_length[wqe_fragment_index] = 0;
		}
	}

	set_wqe_32bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_MISC_IDX, wqe_misc);
	nesnic->sq_head++;
	nesnic->sq_head &= nesnic->sq_size - 1;

	return NETDEV_TX_OK;
}


/**
 * nes_netdev_start_xmit
 */
static int nes_netdev_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_hw_nic *nesnic = &nesvnic->nic;
	struct nes_hw_nic_sq_wqe *nic_sqe;
	struct tcphdr *tcph;
	/* struct udphdr *udph; */
#define NES_MAX_TSO_FRAGS MAX_SKB_FRAGS
	/* 64K segment plus overflow on each side */
	dma_addr_t tso_bus_address[NES_MAX_TSO_FRAGS];
	dma_addr_t bus_address;
	u32 tso_frag_index;
	u32 tso_frag_count;
	u32 tso_wqe_length;
	u32 curr_tcp_seq;
	u32 wqe_count=1;
	u32 send_rc;
	struct iphdr *iph;
	__le16 *wqe_fragment_length;
	u32 nr_frags;
	u32 original_first_length;
	/* u64 *wqe_fragment_address; */
	/* first fragment (0) is used by copy buffer */
	u16 wqe_fragment_index=1;
	u16 hoffset;
	u16 nhoffset;
	u16 wqes_needed;
	u16 wqes_available;
	u32 old_head;
	u32 wqe_misc;

	/*
	 * nes_debug(NES_DBG_NIC_TX, "%s Request to tx NIC packet length %u, headlen %u,"
	 *		" (%u frags), tso_size=%u\n",
	 *		netdev->name, skb->len, skb_headlen(skb),
	 *		skb_shinfo(skb)->nr_frags, skb_is_gso(skb));
	 */

	if (!netif_carrier_ok(netdev))
		return NETDEV_TX_OK;

	if (netif_queue_stopped(netdev))
		return NETDEV_TX_BUSY;

	/* Check if SQ is full */
	if ((((nesnic->sq_tail+(nesnic->sq_size*2))-nesnic->sq_head) & (nesnic->sq_size - 1)) == 1) {
		if (!netif_queue_stopped(netdev)) {
			netif_stop_queue(netdev);
			barrier();
			if ((((((volatile u16)nesnic->sq_tail)+(nesnic->sq_size*2))-nesnic->sq_head) & (nesnic->sq_size - 1)) != 1) {
				netif_start_queue(netdev);
				goto sq_no_longer_full;
			}
		}
		nesvnic->sq_full++;
		return NETDEV_TX_BUSY;
	}

sq_no_longer_full:
	nr_frags = skb_shinfo(skb)->nr_frags;
	if (skb_headlen(skb) > NES_FIRST_FRAG_SIZE) {
		nr_frags++;
	}
	/* Check if too many fragments */
	if (unlikely((nr_frags > 4))) {
		if (skb_is_gso(skb)) {
			nesvnic->segmented_tso_requests++;
			nesvnic->tso_requests++;
			old_head = nesnic->sq_head;
			/* Basically 4 fragments available per WQE with extended fragments */
			wqes_needed = nr_frags >> 2;
			wqes_needed += (nr_frags&3)?1:0;
			wqes_available = (((nesnic->sq_tail+nesnic->sq_size)-nesnic->sq_head) - 1) &
					(nesnic->sq_size - 1);

			if (unlikely(wqes_needed > wqes_available)) {
				if (!netif_queue_stopped(netdev)) {
					netif_stop_queue(netdev);
					barrier();
					wqes_available = (((((volatile u16)nesnic->sq_tail)+nesnic->sq_size)-nesnic->sq_head) - 1) &
						(nesnic->sq_size - 1);
					if (wqes_needed <= wqes_available) {
						netif_start_queue(netdev);
						goto tso_sq_no_longer_full;
					}
				}
				nesvnic->sq_full++;
				nes_debug(NES_DBG_NIC_TX, "%s: HNIC SQ full- TSO request has too many frags!\n",
						netdev->name);
				return NETDEV_TX_BUSY;
			}
tso_sq_no_longer_full:
			/* Map all the buffers */
			for (tso_frag_count=0; tso_frag_count < skb_shinfo(skb)->nr_frags;
					tso_frag_count++) {
				tso_bus_address[tso_frag_count] = pci_map_page( nesdev->pcidev,
						skb_shinfo(skb)->frags[tso_frag_count].page,
						skb_shinfo(skb)->frags[tso_frag_count].page_offset,
						skb_shinfo(skb)->frags[tso_frag_count].size,
						PCI_DMA_TODEVICE);
			}

			tso_frag_index = 0;
			curr_tcp_seq = ntohl(tcp_hdr(skb)->seq);
			hoffset = skb_transport_header(skb) - skb->data;
			nhoffset = skb_network_header(skb) - skb->data;
			original_first_length = hoffset + ((((struct tcphdr *)skb_transport_header(skb))->doff)<<2);

			for (wqe_count=0; wqe_count<((u32)wqes_needed); wqe_count++) {
				tso_wqe_length = 0;
				nic_sqe = &nesnic->sq_vbase[nesnic->sq_head];
				wqe_fragment_length =
						(__le16 *)&nic_sqe->wqe_words[NES_NIC_SQ_WQE_LENGTH_0_TAG_IDX];
				/* setup the VLAN tag if present */
				if (vlan_tx_tag_present(skb)) {
					nes_debug(NES_DBG_NIC_TX, "%s: VLAN packet to send... VLAN = %08X\n",
							netdev->name, vlan_tx_tag_get(skb) );
					wqe_misc = NES_NIC_SQ_WQE_TAGVALUE_ENABLE;
					wqe_fragment_length[0] = (__force __le16) vlan_tx_tag_get(skb);
				} else
					wqe_misc = 0;

				/* bump past the vlan tag */
				wqe_fragment_length++;

				/* Assumes header totally fits in allocated buffer and is in first fragment */
				if (original_first_length > NES_FIRST_FRAG_SIZE) {
					nes_debug(NES_DBG_NIC_TX, "ERROR: SKB header too big, headlen=%u, FIRST_FRAG_SIZE=%u\n",
							original_first_length, NES_FIRST_FRAG_SIZE);
					nes_debug(NES_DBG_NIC_TX, "%s Request to tx NIC packet length %u, headlen %u,"
							" (%u frags), tso_size=%u\n",
							netdev->name,
							skb->len, skb_headlen(skb),
							skb_shinfo(skb)->nr_frags, skb_is_gso(skb));
				}
				memcpy(&nesnic->first_frag_vbase[nesnic->sq_head].buffer,
						skb->data, min(((unsigned int)NES_FIRST_FRAG_SIZE),
						original_first_length));
				iph = (struct iphdr *)
				(&nesnic->first_frag_vbase[nesnic->sq_head].buffer[nhoffset]);
				tcph = (struct tcphdr *)
				(&nesnic->first_frag_vbase[nesnic->sq_head].buffer[hoffset]);
				if ((wqe_count+1)!=(u32)wqes_needed) {
					tcph->fin = 0;
					tcph->psh = 0;
					tcph->rst = 0;
					tcph->urg = 0;
				}
				if (wqe_count) {
					tcph->syn = 0;
				}
				tcph->seq = htonl(curr_tcp_seq);
				wqe_fragment_length[0] = cpu_to_le16(min(((unsigned int)NES_FIRST_FRAG_SIZE),
						original_first_length));

				wqe_fragment_index = 1;
				if ((wqe_count==0) && (skb_headlen(skb) > original_first_length)) {
					set_bit(nesnic->sq_head, nesnic->first_frag_overflow);
					bus_address = pci_map_single(nesdev->pcidev, skb->data + original_first_length,
							skb_headlen(skb) - original_first_length, PCI_DMA_TODEVICE);
					wqe_fragment_length[wqe_fragment_index++] =
						cpu_to_le16(skb_headlen(skb) - original_first_length);
					wqe_fragment_length[wqe_fragment_index] = 0;
					set_wqe_64bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_FRAG1_LOW_IDX,
									bus_address);
					tso_wqe_length += skb_headlen(skb) -
							original_first_length;
				}
				while (wqe_fragment_index < 5) {
					wqe_fragment_length[wqe_fragment_index] =
							cpu_to_le16(skb_shinfo(skb)->frags[tso_frag_index].size);
					set_wqe_64bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_FRAG0_LOW_IDX+(2*wqe_fragment_index),
						(u64)tso_bus_address[tso_frag_index]);
					wqe_fragment_index++;
					tso_wqe_length += skb_shinfo(skb)->frags[tso_frag_index++].size;
					if (wqe_fragment_index < 5)
						wqe_fragment_length[wqe_fragment_index] = 0;
					if (tso_frag_index == tso_frag_count)
						break;
				}
				if ((wqe_count+1) == (u32)wqes_needed) {
					nesnic->tx_skb[nesnic->sq_head] = skb;
				} else {
					nesnic->tx_skb[nesnic->sq_head] = NULL;
				}
				wqe_misc |= NES_NIC_SQ_WQE_COMPLETION | (u16)skb_is_gso(skb);
				if ((tso_wqe_length + original_first_length) > skb_is_gso(skb)) {
					wqe_misc |= NES_NIC_SQ_WQE_LSO_ENABLE;
				} else {
					iph->tot_len = htons(tso_wqe_length + original_first_length - nhoffset);
				}

				set_wqe_32bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_MISC_IDX,
						 wqe_misc);
				set_wqe_32bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_LSO_INFO_IDX,
						((u32)tcph->doff) | (((u32)hoffset) << 4));

				set_wqe_32bit_value(nic_sqe->wqe_words, NES_NIC_SQ_WQE_TOTAL_LENGTH_IDX,
						tso_wqe_length + original_first_length);
				curr_tcp_seq += tso_wqe_length;
				nesnic->sq_head++;
				nesnic->sq_head &= nesnic->sq_size-1;
			}
		} else {
			nesvnic->linearized_skbs++;
			hoffset = skb_transport_header(skb) - skb->data;
			nhoffset = skb_network_header(skb) - skb->data;
			skb_linearize(skb);
			skb_set_transport_header(skb, hoffset);
			skb_set_network_header(skb, nhoffset);
			send_rc = nes_nic_send(skb, netdev);
			if (send_rc != NETDEV_TX_OK)
				return NETDEV_TX_OK;
		}
	} else {
		send_rc = nes_nic_send(skb, netdev);
		if (send_rc != NETDEV_TX_OK)
			return NETDEV_TX_OK;
	}

	barrier();

	if (wqe_count)
		nes_write32(nesdev->regs+NES_WQE_ALLOC,
				(wqe_count << 24) | (1 << 23) | nesvnic->nic.qp_id);

	netdev->trans_start = jiffies;

	return NETDEV_TX_OK;
}


/**
 * nes_netdev_get_stats
 */
static struct net_device_stats *nes_netdev_get_stats(struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	u64 u64temp;
	u32 u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_RX_DISCARD + (nesvnic->nic_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->endnode_nstat_rx_discard += u32temp;

	u64temp = (u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_RX_OCTETS_LO + (nesvnic->nic_index*0x200));
	u64temp += ((u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_RX_OCTETS_HI + (nesvnic->nic_index*0x200))) << 32;

	nesvnic->endnode_nstat_rx_octets += u64temp;
	nesvnic->netstats.rx_bytes += u64temp;

	u64temp = (u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_RX_FRAMES_LO + (nesvnic->nic_index*0x200));
	u64temp += ((u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_RX_FRAMES_HI + (nesvnic->nic_index*0x200))) << 32;

	nesvnic->endnode_nstat_rx_frames += u64temp;
	nesvnic->netstats.rx_packets += u64temp;

	u64temp = (u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_TX_OCTETS_LO + (nesvnic->nic_index*0x200));
	u64temp += ((u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_TX_OCTETS_HI + (nesvnic->nic_index*0x200))) << 32;

	nesvnic->endnode_nstat_tx_octets += u64temp;
	nesvnic->netstats.tx_bytes += u64temp;

	u64temp = (u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_TX_FRAMES_LO + (nesvnic->nic_index*0x200));
	u64temp += ((u64)nes_read_indexed(nesdev,
			NES_IDX_ENDNODE0_NSTAT_TX_FRAMES_HI + (nesvnic->nic_index*0x200))) << 32;

	nesvnic->endnode_nstat_tx_frames += u64temp;
	nesvnic->netstats.tx_packets += u64temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_SHORT_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_short_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_OVERSIZED_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_oversized_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_JABBER_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_jabber_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_SYMBOL_ERR_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_symbol_err_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_LENGTH_ERR_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_length_errors += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_CRC_ERR_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_crc_errors += u32temp;
	nesvnic->netstats.rx_crc_errors += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_TX_ERRORS + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->nesdev->mac_tx_errors += u32temp;
	nesvnic->netstats.tx_errors += u32temp;

	return &nesvnic->netstats;
}


/**
 * nes_netdev_tx_timeout
 */
static void nes_netdev_tx_timeout(struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);

	if (netif_msg_timer(nesvnic))
		nes_debug(NES_DBG_NIC_TX, "%s: tx timeout\n", netdev->name);
}


/**
 * nes_netdev_set_mac_address
 */
static int nes_netdev_set_mac_address(struct net_device *netdev, void *p)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct sockaddr *mac_addr = p;
	int i;
	u32 macaddr_low;
	u16 macaddr_high;

	if (!is_valid_ether_addr(mac_addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, mac_addr->sa_data, netdev->addr_len);
	printk(PFX "%s: Address length = %d, Address = %pM\n",
	       __func__, netdev->addr_len, mac_addr->sa_data);
	macaddr_high  = ((u16)netdev->dev_addr[0]) << 8;
	macaddr_high += (u16)netdev->dev_addr[1];
	macaddr_low   = ((u32)netdev->dev_addr[2]) << 24;
	macaddr_low  += ((u32)netdev->dev_addr[3]) << 16;
	macaddr_low  += ((u32)netdev->dev_addr[4]) << 8;
	macaddr_low  += (u32)netdev->dev_addr[5];

	for (i = 0; i < NES_MAX_PORT_COUNT; i++) {
		if (nesvnic->qp_nic_index[i] == 0xf) {
			break;
		}
		nes_write_indexed(nesdev,
				NES_IDX_PERFECT_FILTER_LOW + (nesvnic->qp_nic_index[i] * 8),
				macaddr_low);
		nes_write_indexed(nesdev,
				NES_IDX_PERFECT_FILTER_HIGH + (nesvnic->qp_nic_index[i] * 8),
				(u32)macaddr_high | NES_MAC_ADDR_VALID |
				((((u32)nesvnic->nic_index) << 16)));
	}
	return 0;
}


static void set_allmulti(struct nes_device *nesdev, u32 nic_active_bit)
{
	u32 nic_active;

	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL);
	nic_active |= nic_active_bit;
	nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL, nic_active);
	nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL);
	nic_active &= ~nic_active_bit;
	nes_write_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL, nic_active);
}

#define get_addr(addrs, index) ((addrs) + (index) * ETH_ALEN)

/**
 * nes_netdev_set_multicast_list
 */
static void nes_netdev_set_multicast_list(struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesvnic->nesdev->nesadapter;
	u32 nic_active_bit;
	u32 nic_active;
	u32 perfect_filter_register_address;
	u32 macaddr_low;
	u16 macaddr_high;
	u8 mc_all_on = 0;
	u8 mc_index;
	int mc_nic_index = -1;
	u8 pft_entries_preallocated = max(nesadapter->adapter_fcn_count *
					nics_per_function, 4);
	u8 max_pft_entries_avaiable = NES_PFT_SIZE - pft_entries_preallocated;
	unsigned long flags;
	int mc_count = netdev_mc_count(netdev);

	spin_lock_irqsave(&nesadapter->resource_lock, flags);
	nic_active_bit = 1 << nesvnic->nic_index;

	if (netdev->flags & IFF_PROMISC) {
		nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL);
		nic_active |= nic_active_bit;
		nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL, nic_active);
		nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL);
		nic_active |= nic_active_bit;
		nes_write_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL, nic_active);
		mc_all_on = 1;
	} else if ((netdev->flags & IFF_ALLMULTI) ||
			   (nesvnic->nic_index > 3)) {
		set_allmulti(nesdev, nic_active_bit);
		mc_all_on = 1;
	} else {
		nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL);
		nic_active &= ~nic_active_bit;
		nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL, nic_active);
		nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL);
		nic_active &= ~nic_active_bit;
		nes_write_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL, nic_active);
	}

	nes_debug(NES_DBG_NIC_RX, "Number of MC entries = %d, Promiscous = %d, All Multicast = %d.\n",
		  mc_count, !!(netdev->flags & IFF_PROMISC),
		  !!(netdev->flags & IFF_ALLMULTI));
	if (!mc_all_on) {
		char *addrs;
		int i;
		struct netdev_hw_addr *ha;

		addrs = kmalloc(ETH_ALEN * mc_count, GFP_ATOMIC);
		if (!addrs) {
			set_allmulti(nesdev, nic_active_bit);
			goto unlock;
		}
		i = 0;
		netdev_for_each_mc_addr(ha, netdev)
			memcpy(get_addr(addrs, i++), ha->addr, ETH_ALEN);

		perfect_filter_register_address = NES_IDX_PERFECT_FILTER_LOW +
						pft_entries_preallocated * 0x8;
		for (i = 0, mc_index = 0; mc_index < max_pft_entries_avaiable;
		     mc_index++) {
			while (i < mc_count && nesvnic->mcrq_mcast_filter &&
			((mc_nic_index = nesvnic->mcrq_mcast_filter(nesvnic,
					get_addr(addrs, i++))) == 0));
			if (mc_nic_index < 0)
				mc_nic_index = nesvnic->nic_index;
			while (nesadapter->pft_mcast_map[mc_index] < 16 &&
				nesadapter->pft_mcast_map[mc_index] !=
					nesvnic->nic_index &&
					mc_index < max_pft_entries_avaiable) {
						nes_debug(NES_DBG_NIC_RX,
					"mc_index=%d skipping nic_index=%d,\
					used for=%d \n", mc_index,
					nesvnic->nic_index,
					nesadapter->pft_mcast_map[mc_index]);
				mc_index++;
			}
			if (mc_index >= max_pft_entries_avaiable)
				break;
			if (i < mc_count) {
				char *addr = get_addr(addrs, i++);

				nes_debug(NES_DBG_NIC_RX, "Assigning MC Address %pM to register 0x%04X nic_idx=%d\n",
					  addr,
					  perfect_filter_register_address+(mc_index * 8),
					  mc_nic_index);
				macaddr_high  = ((u16) addr[0]) << 8;
				macaddr_high += (u16) addr[1];
				macaddr_low   = ((u32) addr[2]) << 24;
				macaddr_low  += ((u32) addr[3]) << 16;
				macaddr_low  += ((u32) addr[4]) << 8;
				macaddr_low  += (u32) addr[5];
				nes_write_indexed(nesdev,
						perfect_filter_register_address+(mc_index * 8),
						macaddr_low);
				nes_write_indexed(nesdev,
						perfect_filter_register_address+4+(mc_index * 8),
						(u32)macaddr_high | NES_MAC_ADDR_VALID |
						((((u32)(1<<mc_nic_index)) << 16)));
				nesadapter->pft_mcast_map[mc_index] =
							nesvnic->nic_index;
			} else {
				nes_debug(NES_DBG_NIC_RX, "Clearing MC Address at register 0x%04X\n",
						  perfect_filter_register_address+(mc_index * 8));
				nes_write_indexed(nesdev,
						perfect_filter_register_address+4+(mc_index * 8),
						0);
				nesadapter->pft_mcast_map[mc_index] = 255;
			}
		}
		kfree(addrs);
		/* PFT is not large enough */
		if (i < mc_count)
			set_allmulti(nesdev, nic_active_bit);
	}

unlock:
	spin_unlock_irqrestore(&nesadapter->resource_lock, flags);
}


/**
 * nes_netdev_change_mtu
 */
static int nes_netdev_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct nes_vnic	*nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	int ret = 0;
	u8 jumbomode = 0;
	u32 nic_active;
	u32 nic_active_bit;
	u32 uc_all_active;
	u32 mc_all_active;

	if ((new_mtu < ETH_ZLEN) || (new_mtu > max_mtu))
		return -EINVAL;

	netdev->mtu = new_mtu;
	nesvnic->max_frame_size	= new_mtu + VLAN_ETH_HLEN;

	if (netdev->mtu	> 1500)	{
		jumbomode=1;
	}
	nes_nic_init_timer_defaults(nesdev, jumbomode);

	if (netif_running(netdev)) {
		nic_active_bit = 1 << nesvnic->nic_index;
		mc_all_active = nes_read_indexed(nesdev,
				NES_IDX_NIC_MULTICAST_ALL) & nic_active_bit;
		uc_all_active = nes_read_indexed(nesdev,
				NES_IDX_NIC_UNICAST_ALL)  & nic_active_bit;

		nes_netdev_stop(netdev);
		nes_netdev_open(netdev);

		nic_active = nes_read_indexed(nesdev,
					NES_IDX_NIC_MULTICAST_ALL);
		nic_active |= mc_all_active;
		nes_write_indexed(nesdev, NES_IDX_NIC_MULTICAST_ALL,
							nic_active);

		nic_active = nes_read_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL);
		nic_active |= uc_all_active;
		nes_write_indexed(nesdev, NES_IDX_NIC_UNICAST_ALL, nic_active);
	}

	return ret;
}


static const char nes_ethtool_stringset[][ETH_GSTRING_LEN] = {
	"Link Change Interrupts",
	"Linearized SKBs",
	"T/GSO Requests",
	"Pause Frames Sent",
	"Pause Frames Received",
	"Internal Routing Errors",
	"SQ SW Dropped SKBs",
	"SQ Full",
	"Segmented TSO Requests",
	"Rx Symbol Errors",
	"Rx Jabber Errors",
	"Rx Oversized Frames",
	"Rx Short Frames",
	"Rx Length Errors",
	"Rx CRC Errors",
	"Rx Port Discard",
	"Endnode Rx Discards",
	"Endnode Rx Octets",
	"Endnode Rx Frames",
	"Endnode Tx Octets",
	"Endnode Tx Frames",
	"Tx Errors",
	"mh detected",
	"mh pauses",
	"Retransmission Count",
	"CM Connects",
	"CM Accepts",
	"Disconnects",
	"Connected Events",
	"Connect Requests",
	"CM Rejects",
	"ModifyQP Timeouts",
	"CreateQPs",
	"SW DestroyQPs",
	"DestroyQPs",
	"CM Closes",
	"CM Packets Sent",
	"CM Packets Bounced",
	"CM Packets Created",
	"CM Packets Rcvd",
	"CM Packets Dropped",
	"CM Packets Retrans",
	"CM Listens Created",
	"CM Listens Destroyed",
	"CM Backlog Drops",
	"CM Loopbacks",
	"CM Nodes Created",
	"CM Nodes Destroyed",
	"CM Accel Drops",
	"CM Resets Received",
	"Free 4Kpbls",
	"Free 256pbls",
	"Timer Inits",
	"LRO aggregated",
	"LRO flushed",
	"LRO no_desc",
};
#define NES_ETHTOOL_STAT_COUNT  ARRAY_SIZE(nes_ethtool_stringset)

/**
 * nes_netdev_get_rx_csum
 */
static u32 nes_netdev_get_rx_csum (struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);

	if (nesvnic->rx_checksum_disabled)
		return 0;
	else
		return 1;
}


/**
 * nes_netdev_set_rc_csum
 */
static int nes_netdev_set_rx_csum(struct net_device *netdev, u32 enable)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);

	if (enable)
		nesvnic->rx_checksum_disabled = 0;
	else
		nesvnic->rx_checksum_disabled = 1;
	return 0;
}


/**
 * nes_netdev_get_sset_count
 */
static int nes_netdev_get_sset_count(struct net_device *netdev, int stringset)
{
	if (stringset == ETH_SS_STATS)
		return NES_ETHTOOL_STAT_COUNT;
	else
		return -EINVAL;
}


/**
 * nes_netdev_get_strings
 */
static void nes_netdev_get_strings(struct net_device *netdev, u32 stringset,
		u8 *ethtool_strings)
{
	if (stringset == ETH_SS_STATS)
		memcpy(ethtool_strings,
				&nes_ethtool_stringset,
				sizeof(nes_ethtool_stringset));
}


/**
 * nes_netdev_get_ethtool_stats
 */

static void nes_netdev_get_ethtool_stats(struct net_device *netdev,
		struct ethtool_stats *target_ethtool_stats, u64 *target_stat_values)
{
	u64 u64temp;
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 nic_count;
	u32 u32temp;
	u32 index = 0;

	target_ethtool_stats->n_stats = NES_ETHTOOL_STAT_COUNT;
	target_stat_values[index] = nesvnic->nesdev->link_status_interrupts;
	target_stat_values[++index] = nesvnic->linearized_skbs;
	target_stat_values[++index] = nesvnic->tso_requests;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_TX_PAUSE_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->nesdev->mac_pause_frames_sent += u32temp;
	target_stat_values[++index] = nesvnic->nesdev->mac_pause_frames_sent;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_PAUSE_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->nesdev->mac_pause_frames_received += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_PORT_RX_DISCARDS + (nesvnic->nesdev->mac_index*0x40));
	nesvnic->nesdev->port_rx_discards += u32temp;
	nesvnic->netstats.rx_dropped += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_PORT_TX_DISCARDS + (nesvnic->nesdev->mac_index*0x40));
	nesvnic->nesdev->port_tx_discards += u32temp;
	nesvnic->netstats.tx_dropped += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_SHORT_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_short_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_OVERSIZED_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_oversized_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_JABBER_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_jabber_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_SYMBOL_ERR_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_dropped += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_symbol_err_frames += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_LENGTH_ERR_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->netstats.rx_length_errors += u32temp;
	nesvnic->nesdev->mac_rx_errors += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_RX_CRC_ERR_FRAMES + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->nesdev->mac_rx_errors += u32temp;
	nesvnic->nesdev->mac_rx_crc_errors += u32temp;
	nesvnic->netstats.rx_crc_errors += u32temp;

	u32temp = nes_read_indexed(nesdev,
			NES_IDX_MAC_TX_ERRORS + (nesvnic->nesdev->mac_index*0x200));
	nesvnic->nesdev->mac_tx_errors += u32temp;
	nesvnic->netstats.tx_errors += u32temp;

	for (nic_count = 0; nic_count < NES_MAX_PORT_COUNT; nic_count++) {
		if (nesvnic->qp_nic_index[nic_count] == 0xf)
			break;

		u32temp = nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_RX_DISCARD +
				(nesvnic->qp_nic_index[nic_count]*0x200));
		nesvnic->netstats.rx_dropped += u32temp;
		nesvnic->endnode_nstat_rx_discard += u32temp;

		u64temp = (u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_RX_OCTETS_LO +
				(nesvnic->qp_nic_index[nic_count]*0x200));
		u64temp += ((u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_RX_OCTETS_HI +
				(nesvnic->qp_nic_index[nic_count]*0x200))) << 32;

		nesvnic->endnode_nstat_rx_octets += u64temp;
		nesvnic->netstats.rx_bytes += u64temp;

		u64temp = (u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_RX_FRAMES_LO +
				(nesvnic->qp_nic_index[nic_count]*0x200));
		u64temp += ((u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_RX_FRAMES_HI +
				(nesvnic->qp_nic_index[nic_count]*0x200))) << 32;

		nesvnic->endnode_nstat_rx_frames += u64temp;
		nesvnic->netstats.rx_packets += u64temp;

		u64temp = (u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_TX_OCTETS_LO +
				(nesvnic->qp_nic_index[nic_count]*0x200));
		u64temp += ((u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_TX_OCTETS_HI +
				(nesvnic->qp_nic_index[nic_count]*0x200))) << 32;

		nesvnic->endnode_nstat_tx_octets += u64temp;
		nesvnic->netstats.tx_bytes += u64temp;

		u64temp = (u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_TX_FRAMES_LO +
				(nesvnic->qp_nic_index[nic_count]*0x200));
		u64temp += ((u64)nes_read_indexed(nesdev,
				NES_IDX_ENDNODE0_NSTAT_TX_FRAMES_HI +
				(nesvnic->qp_nic_index[nic_count]*0x200))) << 32;

		nesvnic->endnode_nstat_tx_frames += u64temp;
		nesvnic->netstats.tx_packets += u64temp;

		u32temp = nes_read_indexed(nesdev,
				NES_IDX_IPV4_TCP_REXMITS + (nesvnic->qp_nic_index[nic_count]*0x200));
		nesvnic->endnode_ipv4_tcp_retransmits += u32temp;
	}

	target_stat_values[++index] = nesvnic->nesdev->mac_pause_frames_received;
	target_stat_values[++index] = nesdev->nesadapter->nic_rx_eth_route_err;
	target_stat_values[++index] = nesvnic->tx_sw_dropped;
	target_stat_values[++index] = nesvnic->sq_full;
	target_stat_values[++index] = nesvnic->segmented_tso_requests;
	target_stat_values[++index] = nesvnic->nesdev->mac_rx_symbol_err_frames;
	target_stat_values[++index] = nesvnic->nesdev->mac_rx_jabber_frames;
	target_stat_values[++index] = nesvnic->nesdev->mac_rx_oversized_frames;
	target_stat_values[++index] = nesvnic->nesdev->mac_rx_short_frames;
	target_stat_values[++index] = nesvnic->netstats.rx_length_errors;
	target_stat_values[++index] = nesvnic->nesdev->mac_rx_crc_errors;
	target_stat_values[++index] = nesvnic->nesdev->port_rx_discards;
	target_stat_values[++index] = nesvnic->endnode_nstat_rx_discard;
	target_stat_values[++index] = nesvnic->endnode_nstat_rx_octets;
	target_stat_values[++index] = nesvnic->endnode_nstat_rx_frames;
	target_stat_values[++index] = nesvnic->endnode_nstat_tx_octets;
	target_stat_values[++index] = nesvnic->endnode_nstat_tx_frames;
	target_stat_values[++index] = nesvnic->nesdev->mac_tx_errors;
	target_stat_values[++index] = mh_detected;
	target_stat_values[++index] = mh_pauses_sent;
	target_stat_values[++index] = nesvnic->endnode_ipv4_tcp_retransmits;
	target_stat_values[++index] = atomic_read(&cm_connects);
	target_stat_values[++index] = atomic_read(&cm_accepts);
	target_stat_values[++index] = atomic_read(&cm_disconnects);
	target_stat_values[++index] = atomic_read(&cm_connecteds);
	target_stat_values[++index] = atomic_read(&cm_connect_reqs);
	target_stat_values[++index] = atomic_read(&cm_rejects);
	target_stat_values[++index] = atomic_read(&mod_qp_timouts);
	target_stat_values[++index] = atomic_read(&qps_created);
	target_stat_values[++index] = atomic_read(&sw_qps_destroyed);
	target_stat_values[++index] = atomic_read(&qps_destroyed);
	target_stat_values[++index] = atomic_read(&cm_closes);
	target_stat_values[++index] = cm_packets_sent;
	target_stat_values[++index] = cm_packets_bounced;
	target_stat_values[++index] = cm_packets_created;
	target_stat_values[++index] = cm_packets_received;
	target_stat_values[++index] = cm_packets_dropped;
	target_stat_values[++index] = cm_packets_retrans;
	target_stat_values[++index] = atomic_read(&cm_listens_created);
	target_stat_values[++index] = atomic_read(&cm_listens_destroyed);
	target_stat_values[++index] = cm_backlog_drops;
	target_stat_values[++index] = atomic_read(&cm_loopbacks);
	target_stat_values[++index] = atomic_read(&cm_nodes_created);
	target_stat_values[++index] = atomic_read(&cm_nodes_destroyed);
	target_stat_values[++index] = atomic_read(&cm_accel_dropped_pkts);
	target_stat_values[++index] = atomic_read(&cm_resets_recvd);
	target_stat_values[++index] = nesadapter->free_4kpbl;
	target_stat_values[++index] = nesadapter->free_256pbl;
	target_stat_values[++index] = int_mod_timer_init;
	target_stat_values[++index] = nesvnic->lro_mgr.stats.aggregated;
	target_stat_values[++index] = nesvnic->lro_mgr.stats.flushed;
	target_stat_values[++index] = nesvnic->lro_mgr.stats.no_desc;
}

/**
 * nes_netdev_get_drvinfo
 */
static void nes_netdev_get_drvinfo(struct net_device *netdev,
		struct ethtool_drvinfo *drvinfo)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_adapter *nesadapter = nesvnic->nesdev->nesadapter;

	strcpy(drvinfo->driver, DRV_NAME);
	strcpy(drvinfo->bus_info, pci_name(nesvnic->nesdev->pcidev));
	sprintf(drvinfo->fw_version, "%u.%u", nesadapter->firmware_version>>16,
				nesadapter->firmware_version & 0x000000ff);
	strcpy(drvinfo->version, DRV_VERSION);
	drvinfo->testinfo_len = 0;
	drvinfo->eedump_len = 0;
	drvinfo->regdump_len = 0;
}


/**
 * nes_netdev_set_coalesce
 */
static int nes_netdev_set_coalesce(struct net_device *netdev,
		struct ethtool_coalesce	*et_coalesce)
{
	struct nes_vnic	*nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_tune_timer *shared_timer = &nesadapter->tune_timer;
	unsigned long flags;

	spin_lock_irqsave(&nesadapter->periodic_timer_lock, flags);
	if (et_coalesce->rx_max_coalesced_frames_low) {
		shared_timer->threshold_low = et_coalesce->rx_max_coalesced_frames_low;
	}
	if (et_coalesce->rx_max_coalesced_frames_irq) {
		shared_timer->threshold_target = et_coalesce->rx_max_coalesced_frames_irq;
	}
	if (et_coalesce->rx_max_coalesced_frames_high) {
		shared_timer->threshold_high = et_coalesce->rx_max_coalesced_frames_high;
	}
	if (et_coalesce->rx_coalesce_usecs_low) {
		shared_timer->timer_in_use_min = et_coalesce->rx_coalesce_usecs_low;
	}
	if (et_coalesce->rx_coalesce_usecs_high) {
		shared_timer->timer_in_use_max = et_coalesce->rx_coalesce_usecs_high;
	}
	spin_unlock_irqrestore(&nesadapter->periodic_timer_lock, flags);

	/* using this to drive total interrupt moderation */
	nesadapter->et_rx_coalesce_usecs_irq = et_coalesce->rx_coalesce_usecs_irq;
	if (et_coalesce->use_adaptive_rx_coalesce) {
		nesadapter->et_use_adaptive_rx_coalesce	= 1;
		nesadapter->timer_int_limit = NES_TIMER_INT_LIMIT_DYNAMIC;
		nesadapter->et_rx_coalesce_usecs_irq = 0;
		if (et_coalesce->pkt_rate_low) {
			nesadapter->et_pkt_rate_low = et_coalesce->pkt_rate_low;
		}
	} else {
		nesadapter->et_use_adaptive_rx_coalesce	= 0;
		nesadapter->timer_int_limit = NES_TIMER_INT_LIMIT;
		if (nesadapter->et_rx_coalesce_usecs_irq) {
			nes_write32(nesdev->regs+NES_PERIODIC_CONTROL,
					0x80000000 | ((u32)(nesadapter->et_rx_coalesce_usecs_irq*8)));
		}
	}
	return 0;
}


/**
 * nes_netdev_get_coalesce
 */
static int nes_netdev_get_coalesce(struct net_device *netdev,
		struct ethtool_coalesce	*et_coalesce)
{
	struct nes_vnic	*nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct ethtool_coalesce	temp_et_coalesce;
	struct nes_hw_tune_timer *shared_timer = &nesadapter->tune_timer;
	unsigned long flags;

	memset(&temp_et_coalesce, 0, sizeof(temp_et_coalesce));
	temp_et_coalesce.rx_coalesce_usecs_irq    = nesadapter->et_rx_coalesce_usecs_irq;
	temp_et_coalesce.use_adaptive_rx_coalesce = nesadapter->et_use_adaptive_rx_coalesce;
	temp_et_coalesce.rate_sample_interval     = nesadapter->et_rate_sample_interval;
	temp_et_coalesce.pkt_rate_low =	nesadapter->et_pkt_rate_low;
	spin_lock_irqsave(&nesadapter->periodic_timer_lock,	flags);
	temp_et_coalesce.rx_max_coalesced_frames_low  = shared_timer->threshold_low;
	temp_et_coalesce.rx_max_coalesced_frames_irq  = shared_timer->threshold_target;
	temp_et_coalesce.rx_max_coalesced_frames_high = shared_timer->threshold_high;
	temp_et_coalesce.rx_coalesce_usecs_low  = shared_timer->timer_in_use_min;
	temp_et_coalesce.rx_coalesce_usecs_high = shared_timer->timer_in_use_max;
	if (nesadapter->et_use_adaptive_rx_coalesce) {
		temp_et_coalesce.rx_coalesce_usecs_irq = shared_timer->timer_in_use;
	}
	spin_unlock_irqrestore(&nesadapter->periodic_timer_lock, flags);
	memcpy(et_coalesce, &temp_et_coalesce, sizeof(*et_coalesce));
	return 0;
}


/**
 * nes_netdev_get_pauseparam
 */
static void nes_netdev_get_pauseparam(struct net_device *netdev,
		struct ethtool_pauseparam *et_pauseparam)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);

	et_pauseparam->autoneg = 0;
	et_pauseparam->rx_pause = (nesvnic->nesdev->disable_rx_flow_control == 0) ? 1:0;
	et_pauseparam->tx_pause = (nesvnic->nesdev->disable_tx_flow_control == 0) ? 1:0;
}


/**
 * nes_netdev_set_pauseparam
 */
static int nes_netdev_set_pauseparam(struct net_device *netdev,
		struct ethtool_pauseparam *et_pauseparam)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	u32 u32temp;

	if (et_pauseparam->autoneg) {
		/* TODO: should return unsupported */
		return 0;
	}
	if ((et_pauseparam->tx_pause == 1) && (nesdev->disable_tx_flow_control == 1)) {
		u32temp = nes_read_indexed(nesdev,
				NES_IDX_MAC_TX_CONFIG + (nesdev->mac_index*0x200));
		u32temp |= NES_IDX_MAC_TX_CONFIG_ENABLE_PAUSE;
		nes_write_indexed(nesdev,
				NES_IDX_MAC_TX_CONFIG_ENABLE_PAUSE + (nesdev->mac_index*0x200), u32temp);
		nesdev->disable_tx_flow_control = 0;
	} else if ((et_pauseparam->tx_pause == 0) && (nesdev->disable_tx_flow_control == 0)) {
		u32temp = nes_read_indexed(nesdev,
				NES_IDX_MAC_TX_CONFIG + (nesdev->mac_index*0x200));
		u32temp &= ~NES_IDX_MAC_TX_CONFIG_ENABLE_PAUSE;
		nes_write_indexed(nesdev,
				NES_IDX_MAC_TX_CONFIG_ENABLE_PAUSE + (nesdev->mac_index*0x200), u32temp);
		nesdev->disable_tx_flow_control = 1;
	}
	if ((et_pauseparam->rx_pause == 1) && (nesdev->disable_rx_flow_control == 1)) {
		u32temp = nes_read_indexed(nesdev,
				NES_IDX_MPP_DEBUG + (nesdev->mac_index*0x40));
		u32temp &= ~NES_IDX_MPP_DEBUG_PORT_DISABLE_PAUSE;
		nes_write_indexed(nesdev,
				NES_IDX_MPP_DEBUG + (nesdev->mac_index*0x40), u32temp);
		nesdev->disable_rx_flow_control = 0;
	} else if ((et_pauseparam->rx_pause == 0) && (nesdev->disable_rx_flow_control == 0)) {
		u32temp = nes_read_indexed(nesdev,
				NES_IDX_MPP_DEBUG + (nesdev->mac_index*0x40));
		u32temp |= NES_IDX_MPP_DEBUG_PORT_DISABLE_PAUSE;
		nes_write_indexed(nesdev,
				NES_IDX_MPP_DEBUG + (nesdev->mac_index*0x40), u32temp);
		nesdev->disable_rx_flow_control = 1;
	}

	return 0;
}


/**
 * nes_netdev_get_settings
 */
static int nes_netdev_get_settings(struct net_device *netdev, struct ethtool_cmd *et_cmd)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 mac_index = nesdev->mac_index;
	u8 phy_type = nesadapter->phy_type[mac_index];
	u8 phy_index = nesadapter->phy_index[mac_index];
	u16 phy_data;

	et_cmd->duplex = DUPLEX_FULL;
	et_cmd->port   = PORT_MII;
	et_cmd->maxtxpkt = 511;
	et_cmd->maxrxpkt = 511;

	if (nesadapter->OneG_Mode) {
		et_cmd->speed = SPEED_1000;
		if (phy_type == NES_PHY_TYPE_PUMA_1G) {
			et_cmd->supported   = SUPPORTED_1000baseT_Full;
			et_cmd->advertising = ADVERTISED_1000baseT_Full;
			et_cmd->autoneg     = AUTONEG_DISABLE;
			et_cmd->transceiver = XCVR_INTERNAL;
			et_cmd->phy_address = mac_index;
		} else {
			unsigned long flags;
			et_cmd->supported   = SUPPORTED_1000baseT_Full
					    | SUPPORTED_Autoneg;
			et_cmd->advertising = ADVERTISED_1000baseT_Full
					    | ADVERTISED_Autoneg;
			spin_lock_irqsave(&nesadapter->phy_lock, flags);
			nes_read_1G_phy_reg(nesdev, 0, phy_index, &phy_data);
			spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
			if (phy_data & 0x1000)
				et_cmd->autoneg = AUTONEG_ENABLE;
			else
				et_cmd->autoneg = AUTONEG_DISABLE;
			et_cmd->transceiver = XCVR_EXTERNAL;
			et_cmd->phy_address = phy_index;
		}
		return 0;
	}
	if ((phy_type == NES_PHY_TYPE_ARGUS) ||
	    (phy_type == NES_PHY_TYPE_SFP_D) ||
	    (phy_type == NES_PHY_TYPE_KR)) {
		et_cmd->transceiver = XCVR_EXTERNAL;
		et_cmd->port        = PORT_FIBRE;
		et_cmd->supported   = SUPPORTED_FIBRE;
		et_cmd->advertising = ADVERTISED_FIBRE;
		et_cmd->phy_address = phy_index;
	} else {
		et_cmd->transceiver = XCVR_INTERNAL;
		et_cmd->supported   = SUPPORTED_10000baseT_Full;
		et_cmd->advertising = ADVERTISED_10000baseT_Full;
		et_cmd->phy_address = mac_index;
	}
	et_cmd->speed = SPEED_10000;
	et_cmd->autoneg = AUTONEG_DISABLE;
	return 0;
}


/**
 * nes_netdev_set_settings
 */
static int nes_netdev_set_settings(struct net_device *netdev, struct ethtool_cmd *et_cmd)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;

	if ((nesadapter->OneG_Mode) &&
	    (nesadapter->phy_type[nesdev->mac_index] != NES_PHY_TYPE_PUMA_1G)) {
		unsigned long flags;
		u16 phy_data;
		u8 phy_index = nesadapter->phy_index[nesdev->mac_index];

		spin_lock_irqsave(&nesadapter->phy_lock, flags);
		nes_read_1G_phy_reg(nesdev, 0, phy_index, &phy_data);
		if (et_cmd->autoneg) {
			/* Turn on Full duplex, Autoneg, and restart autonegotiation */
			phy_data |= 0x1300;
		} else {
			/* Turn off autoneg */
			phy_data &= ~0x1000;
		}
		nes_write_1G_phy_reg(nesdev, 0, phy_index, phy_data);
		spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
	}

	return 0;
}


static const struct ethtool_ops nes_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_settings = nes_netdev_get_settings,
	.set_settings = nes_netdev_set_settings,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.get_rx_csum = nes_netdev_get_rx_csum,
	.get_sg = ethtool_op_get_sg,
	.get_strings = nes_netdev_get_strings,
	.get_sset_count = nes_netdev_get_sset_count,
	.get_ethtool_stats = nes_netdev_get_ethtool_stats,
	.get_drvinfo = nes_netdev_get_drvinfo,
	.get_coalesce = nes_netdev_get_coalesce,
	.set_coalesce = nes_netdev_set_coalesce,
	.get_pauseparam = nes_netdev_get_pauseparam,
	.set_pauseparam = nes_netdev_set_pauseparam,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.set_rx_csum = nes_netdev_set_rx_csum,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_flags = ethtool_op_get_flags,
	.set_flags = ethtool_op_set_flags,
};


static void nes_netdev_vlan_rx_register(struct net_device *netdev, struct vlan_group *grp)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 u32temp;
	unsigned long flags;

	spin_lock_irqsave(&nesadapter->phy_lock, flags);
	nesvnic->vlan_grp = grp;

	nes_debug(NES_DBG_NETDEV, "%s: %s\n", __func__, netdev->name);

	/* Enable/Disable VLAN Stripping */
	u32temp = nes_read_indexed(nesdev, NES_IDX_PCIX_DIAG);
	if (grp)
		u32temp &= 0xfdffffff;
	else
		u32temp	|= 0x02000000;

	nes_write_indexed(nesdev, NES_IDX_PCIX_DIAG, u32temp);
	spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
}

static const struct net_device_ops nes_netdev_ops = {
	.ndo_open 		= nes_netdev_open,
	.ndo_stop		= nes_netdev_stop,
	.ndo_start_xmit 	= nes_netdev_start_xmit,
	.ndo_get_stats		= nes_netdev_get_stats,
	.ndo_tx_timeout 	= nes_netdev_tx_timeout,
	.ndo_set_mac_address	= nes_netdev_set_mac_address,
	.ndo_set_multicast_list = nes_netdev_set_multicast_list,
	.ndo_change_mtu		= nes_netdev_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_register 	= nes_netdev_vlan_rx_register,
};

/**
 * nes_netdev_init - initialize network device
 */
struct net_device *nes_netdev_init(struct nes_device *nesdev,
		void __iomem *mmio_addr)
{
	u64 u64temp;
	struct nes_vnic *nesvnic;
	struct net_device *netdev;
	struct nic_qp_map *curr_qp_map;
	u8 phy_type = nesdev->nesadapter->phy_type[nesdev->mac_index];

	netdev = alloc_etherdev(sizeof(struct nes_vnic));
	if (!netdev) {
		printk(KERN_ERR PFX "nesvnic etherdev alloc failed");
		return NULL;
	}
	nesvnic = netdev_priv(netdev);

	nes_debug(NES_DBG_INIT, "netdev = %p, %s\n", netdev, netdev->name);

	SET_NETDEV_DEV(netdev, &nesdev->pcidev->dev);

	netdev->watchdog_timeo = NES_TX_TIMEOUT;
	netdev->irq = nesdev->pcidev->irq;
	netdev->mtu = ETH_DATA_LEN;
	netdev->hard_header_len = ETH_HLEN;
	netdev->addr_len = ETH_ALEN;
	netdev->type = ARPHRD_ETHER;
	netdev->features = NETIF_F_HIGHDMA;
	netdev->netdev_ops = &nes_netdev_ops;
	netdev->ethtool_ops = &nes_ethtool_ops;
	netif_napi_add(netdev, &nesvnic->napi, nes_netdev_poll, 128);
	nes_debug(NES_DBG_INIT, "Enabling VLAN Insert/Delete.\n");
	netdev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;

	/* Fill in the port structure */
	nesvnic->netdev = netdev;
	nesvnic->nesdev = nesdev;
	nesvnic->msg_enable = netif_msg_init(debug, default_msg);
	nesvnic->netdev_index = nesdev->netdev_count;
	nesvnic->perfect_filter_index = nesdev->nesadapter->netdev_count;
	nesvnic->max_frame_size = netdev->mtu + netdev->hard_header_len + VLAN_HLEN;

	curr_qp_map = nic_qp_mapping_per_function[PCI_FUNC(nesdev->pcidev->devfn)];
	nesvnic->nic.qp_id = curr_qp_map[nesdev->netdev_count].qpid;
	nesvnic->nic_index = curr_qp_map[nesdev->netdev_count].nic_index;
	nesvnic->logical_port = curr_qp_map[nesdev->netdev_count].logical_port;

	/* Setup the burned in MAC address */
	u64temp = (u64)nesdev->nesadapter->mac_addr_low;
	u64temp += ((u64)nesdev->nesadapter->mac_addr_high) << 32;
	u64temp += nesvnic->nic_index;
	netdev->dev_addr[0] = (u8)(u64temp>>40);
	netdev->dev_addr[1] = (u8)(u64temp>>32);
	netdev->dev_addr[2] = (u8)(u64temp>>24);
	netdev->dev_addr[3] = (u8)(u64temp>>16);
	netdev->dev_addr[4] = (u8)(u64temp>>8);
	netdev->dev_addr[5] = (u8)u64temp;
	memcpy(netdev->perm_addr, netdev->dev_addr, 6);

	if ((nesvnic->logical_port < 2) || (nesdev->nesadapter->hw_rev != NE020_REV)) {
		netdev->features |= NETIF_F_TSO | NETIF_F_SG | NETIF_F_IP_CSUM;
		netdev->features |= NETIF_F_GSO | NETIF_F_TSO | NETIF_F_SG | NETIF_F_IP_CSUM;
	} else {
		netdev->features |= NETIF_F_SG | NETIF_F_IP_CSUM;
	}

	nes_debug(NES_DBG_INIT, "nesvnic = %p, reported features = 0x%lX, QPid = %d,"
			" nic_index = %d, logical_port = %d, mac_index = %d.\n",
			nesvnic, (unsigned long)netdev->features, nesvnic->nic.qp_id,
			nesvnic->nic_index, nesvnic->logical_port,  nesdev->mac_index);

	if (nesvnic->nesdev->nesadapter->port_count == 1 &&
		nesvnic->nesdev->nesadapter->adapter_fcn_count == 1) {

		nesvnic->qp_nic_index[0] = nesvnic->nic_index;
		nesvnic->qp_nic_index[1] = nesvnic->nic_index + 1;
		if (nes_drv_opt & NES_DRV_OPT_DUAL_LOGICAL_PORT) {
			nesvnic->qp_nic_index[2] = 0xf;
			nesvnic->qp_nic_index[3] = 0xf;
		} else {
			nesvnic->qp_nic_index[2] = nesvnic->nic_index + 2;
			nesvnic->qp_nic_index[3] = nesvnic->nic_index + 3;
		}
	} else {
		if (nesvnic->nesdev->nesadapter->port_count == 2 ||
			(nesvnic->nesdev->nesadapter->port_count == 1 &&
			nesvnic->nesdev->nesadapter->adapter_fcn_count == 2)) {
				nesvnic->qp_nic_index[0] = nesvnic->nic_index;
				nesvnic->qp_nic_index[1] = nesvnic->nic_index
									+ 2;
				nesvnic->qp_nic_index[2] = 0xf;
				nesvnic->qp_nic_index[3] = 0xf;
		} else {
			nesvnic->qp_nic_index[0] = nesvnic->nic_index;
			nesvnic->qp_nic_index[1] = 0xf;
			nesvnic->qp_nic_index[2] = 0xf;
			nesvnic->qp_nic_index[3] = 0xf;
		}
	}
	nesvnic->next_qp_nic_index = 0;

	if (nesdev->netdev_count == 0) {
		nesvnic->rdma_enabled = 1;
	} else {
		nesvnic->rdma_enabled = 0;
	}
	nesvnic->nic_cq.cq_number = nesvnic->nic.qp_id;
	spin_lock_init(&nesvnic->tx_lock);
	nesdev->netdev[nesdev->netdev_count] = netdev;

	nes_debug(NES_DBG_INIT, "Adding nesvnic (%p) to the adapters nesvnic_list for MAC%d.\n",
			nesvnic, nesdev->mac_index);
	list_add_tail(&nesvnic->list, &nesdev->nesadapter->nesvnic_list[nesdev->mac_index]);

	if ((nesdev->netdev_count == 0) &&
	    ((PCI_FUNC(nesdev->pcidev->devfn) == nesdev->mac_index) ||
	     ((phy_type == NES_PHY_TYPE_PUMA_1G) &&
	      (((PCI_FUNC(nesdev->pcidev->devfn) == 1) && (nesdev->mac_index == 2)) ||
	       ((PCI_FUNC(nesdev->pcidev->devfn) == 2) && (nesdev->mac_index == 1)))))) {
		u32 u32temp;
		u32 link_mask;
		u32 link_val;

		u32temp = nes_read_indexed(nesdev, NES_IDX_PHY_PCS_CONTROL_STATUS0 +
				(0x200 * (nesdev->mac_index & 1)));
		if (phy_type != NES_PHY_TYPE_PUMA_1G) {
			u32temp |= 0x00200000;
			nes_write_indexed(nesdev, NES_IDX_PHY_PCS_CONTROL_STATUS0 +
				(0x200 * (nesdev->mac_index & 1)), u32temp);
		}

		/* Check and set linkup here.  This is for back to back */
		/* configuration where second port won't get link interrupt */
		switch (phy_type) {
		case NES_PHY_TYPE_PUMA_1G:
			if (nesdev->mac_index < 2) {
				link_mask = 0x01010000;
				link_val = 0x01010000;
			} else {
				link_mask = 0x02020000;
				link_val = 0x02020000;
			}
			break;
		default:
			link_mask = 0x0f1f0000;
			link_val = 0x0f0f0000;
			break;
		}

		u32temp = nes_read_indexed(nesdev,
					   NES_IDX_PHY_PCS_CONTROL_STATUS0 +
					   (0x200 * (nesdev->mac_index & 1)));
		if ((u32temp & link_mask) == link_val)
			nesvnic->linkup = 1;

		/* clear the MAC interrupt status, assumes direct logical to physical mapping */
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS + (0x200 * nesdev->mac_index));
		nes_debug(NES_DBG_INIT, "Phy interrupt status = 0x%X.\n", u32temp);
		nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS + (0x200 * nesdev->mac_index), u32temp);

		nes_init_phy(nesdev);
	}

	return netdev;
}


/**
 * nes_netdev_destroy - destroy network device structure
 */
void nes_netdev_destroy(struct net_device *netdev)
{
	struct nes_vnic *nesvnic = netdev_priv(netdev);

	/* make sure 'stop' method is called by Linux stack */
	/* nes_netdev_stop(netdev); */

	list_del(&nesvnic->list);

	if (nesvnic->of_device_registered) {
		nes_destroy_ofa_device(nesvnic->nesibdev);
	}

	free_netdev(netdev);
}


/**
 * nes_nic_cm_xmit -- CM calls this to send out pkts
 */
int nes_nic_cm_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	int ret;

	skb->dev = netdev;
	ret = dev_queue_xmit(skb);
	if (ret) {
		nes_debug(NES_DBG_CM, "Bad return code from dev_queue_xmit %d\n", ret);
	}

	return ret;
}
