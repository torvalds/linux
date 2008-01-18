/*
 *  PS3 gelic network driver.
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2006, 2007 Sony Corporation
 *
 * This file is based on: spider_net.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *           Jens Osterkamp <Jens.Osterkamp@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include <linux/dma-mapping.h>
#include <net/checksum.h>
#include <asm/firmware.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

#include "ps3_gelic_net.h"

#define DRV_NAME "Gelic Network Driver"
#define DRV_VERSION "1.0"

MODULE_AUTHOR("SCE Inc.");
MODULE_DESCRIPTION("Gelic Network driver");
MODULE_LICENSE("GPL");

static inline struct device *ctodev(struct gelic_net_card *card)
{
	return &card->dev->core;
}
static inline u64 bus_id(struct gelic_net_card *card)
{
	return card->dev->bus_id;
}
static inline u64 dev_id(struct gelic_net_card *card)
{
	return card->dev->dev_id;
}

/* set irq_mask */
static int gelic_net_set_irq_mask(struct gelic_net_card *card, u64 mask)
{
	int status;

	status = lv1_net_set_interrupt_mask(bus_id(card), dev_id(card),
					    mask, 0);
	if (status)
		dev_info(ctodev(card),
			 "lv1_net_set_interrupt_mask failed %d\n", status);
	return status;
}
static inline void gelic_net_rx_irq_on(struct gelic_net_card *card)
{
	gelic_net_set_irq_mask(card, card->ghiintmask | GELIC_NET_RXINT);
}
static inline void gelic_net_rx_irq_off(struct gelic_net_card *card)
{
	gelic_net_set_irq_mask(card, card->ghiintmask & ~GELIC_NET_RXINT);
}
/**
 * gelic_net_get_descr_status -- returns the status of a descriptor
 * @descr: descriptor to look at
 *
 * returns the status as in the dmac_cmd_status field of the descriptor
 */
static enum gelic_net_descr_status
gelic_net_get_descr_status(struct gelic_net_descr *descr)
{
	u32 cmd_status;

	cmd_status = descr->dmac_cmd_status;
	cmd_status >>= GELIC_NET_DESCR_IND_PROC_SHIFT;
	return cmd_status;
}

/**
 * gelic_net_set_descr_status -- sets the status of a descriptor
 * @descr: descriptor to change
 * @status: status to set in the descriptor
 *
 * changes the status to the specified value. Doesn't change other bits
 * in the status
 */
static void gelic_net_set_descr_status(struct gelic_net_descr *descr,
				       enum gelic_net_descr_status status)
{
	u32 cmd_status;

	/* read the status */
	cmd_status = descr->dmac_cmd_status;
	/* clean the upper 4 bits */
	cmd_status &= GELIC_NET_DESCR_IND_PROC_MASKO;
	/* add the status to it */
	cmd_status |= ((u32)status) << GELIC_NET_DESCR_IND_PROC_SHIFT;
	/* and write it back */
	descr->dmac_cmd_status = cmd_status;
	/*
	 * dma_cmd_status field is used to indicate whether the descriptor
	 * is valid or not.
	 * Usually caller of this function wants to inform that to the
	 * hardware, so we assure here the hardware sees the change.
	 */
	wmb();
}

/**
 * gelic_net_free_chain - free descriptor chain
 * @card: card structure
 * @descr_in: address of desc
 */
static void gelic_net_free_chain(struct gelic_net_card *card,
				 struct gelic_net_descr *descr_in)
{
	struct gelic_net_descr *descr;

	for (descr = descr_in; descr && descr->bus_addr; descr = descr->next) {
		dma_unmap_single(ctodev(card), descr->bus_addr,
				 GELIC_NET_DESCR_SIZE, DMA_BIDIRECTIONAL);
		descr->bus_addr = 0;
	}
}

/**
 * gelic_net_init_chain - links descriptor chain
 * @card: card structure
 * @chain: address of chain
 * @start_descr: address of descriptor array
 * @no: number of descriptors
 *
 * we manage a circular list that mirrors the hardware structure,
 * except that the hardware uses bus addresses.
 *
 * returns 0 on success, <0 on failure
 */
static int gelic_net_init_chain(struct gelic_net_card *card,
				struct gelic_net_descr_chain *chain,
				struct gelic_net_descr *start_descr, int no)
{
	int i;
	struct gelic_net_descr *descr;

	descr = start_descr;
	memset(descr, 0, sizeof(*descr) * no);

	/* set up the hardware pointers in each descriptor */
	for (i = 0; i < no; i++, descr++) {
		gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);
		descr->bus_addr =
			dma_map_single(ctodev(card), descr,
				       GELIC_NET_DESCR_SIZE,
				       DMA_BIDIRECTIONAL);

		if (!descr->bus_addr)
			goto iommu_error;

		descr->next = descr + 1;
		descr->prev = descr - 1;
	}
	/* make them as ring */
	(descr - 1)->next = start_descr;
	start_descr->prev = (descr - 1);

	/* chain bus addr of hw descriptor */
	descr = start_descr;
	for (i = 0; i < no; i++, descr++) {
		descr->next_descr_addr = descr->next->bus_addr;
	}

	chain->head = start_descr;
	chain->tail = start_descr;

	/* do not chain last hw descriptor */
	(descr - 1)->next_descr_addr = 0;

	return 0;

iommu_error:
	for (i--, descr--; 0 <= i; i--, descr--)
		if (descr->bus_addr)
			dma_unmap_single(ctodev(card), descr->bus_addr,
					 GELIC_NET_DESCR_SIZE,
					 DMA_BIDIRECTIONAL);
	return -ENOMEM;
}

/**
 * gelic_net_prepare_rx_descr - reinitializes a rx descriptor
 * @card: card structure
 * @descr: descriptor to re-init
 *
 * return 0 on succes, <0 on failure
 *
 * allocates a new rx skb, iommu-maps it and attaches it to the descriptor.
 * Activate the descriptor state-wise
 */
static int gelic_net_prepare_rx_descr(struct gelic_net_card *card,
				      struct gelic_net_descr *descr)
{
	int offset;
	unsigned int bufsize;

	if (gelic_net_get_descr_status(descr) !=  GELIC_NET_DESCR_NOT_IN_USE) {
		dev_info(ctodev(card), "%s: ERROR status \n", __func__);
	}
	/* we need to round up the buffer size to a multiple of 128 */
	bufsize = ALIGN(GELIC_NET_MAX_MTU, GELIC_NET_RXBUF_ALIGN);

	/* and we need to have it 128 byte aligned, therefore we allocate a
	 * bit more */
	descr->skb = netdev_alloc_skb(card->netdev,
		bufsize + GELIC_NET_RXBUF_ALIGN - 1);
	if (!descr->skb) {
		descr->buf_addr = 0; /* tell DMAC don't touch memory */
		dev_info(ctodev(card),
			 "%s:allocate skb failed !!\n", __func__);
		return -ENOMEM;
	}
	descr->buf_size = bufsize;
	descr->dmac_cmd_status = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_error = 0;

	offset = ((unsigned long)descr->skb->data) &
		(GELIC_NET_RXBUF_ALIGN - 1);
	if (offset)
		skb_reserve(descr->skb, GELIC_NET_RXBUF_ALIGN - offset);
	/* io-mmu-map the skb */
	descr->buf_addr = dma_map_single(ctodev(card), descr->skb->data,
					 GELIC_NET_MAX_MTU,
					 DMA_FROM_DEVICE);
	if (!descr->buf_addr) {
		dev_kfree_skb_any(descr->skb);
		descr->skb = NULL;
		dev_info(ctodev(card),
			 "%s:Could not iommu-map rx buffer\n", __func__);
		gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);
		return -ENOMEM;
	} else {
		gelic_net_set_descr_status(descr, GELIC_NET_DESCR_CARDOWNED);
		return 0;
	}
}

/**
 * gelic_net_release_rx_chain - free all skb of rx descr
 * @card: card structure
 *
 */
static void gelic_net_release_rx_chain(struct gelic_net_card *card)
{
	struct gelic_net_descr *descr = card->rx_chain.head;

	do {
		if (descr->skb) {
			dma_unmap_single(ctodev(card),
					 descr->buf_addr,
					 descr->skb->len,
					 DMA_FROM_DEVICE);
			descr->buf_addr = 0;
			dev_kfree_skb_any(descr->skb);
			descr->skb = NULL;
			gelic_net_set_descr_status(descr,
						   GELIC_NET_DESCR_NOT_IN_USE);
		}
		descr = descr->next;
	} while (descr != card->rx_chain.head);
}

/**
 * gelic_net_fill_rx_chain - fills descriptors/skbs in the rx chains
 * @card: card structure
 *
 * fills all descriptors in the rx chain: allocates skbs
 * and iommu-maps them.
 * returns 0 on success, <0 on failure
 */
static int gelic_net_fill_rx_chain(struct gelic_net_card *card)
{
	struct gelic_net_descr *descr = card->rx_chain.head;
	int ret;

	do {
		if (!descr->skb) {
			ret = gelic_net_prepare_rx_descr(card, descr);
			if (ret)
				goto rewind;
		}
		descr = descr->next;
	} while (descr != card->rx_chain.head);

	return 0;
rewind:
	gelic_net_release_rx_chain(card);
	return ret;
}

/**
 * gelic_net_alloc_rx_skbs - allocates rx skbs in rx descriptor chains
 * @card: card structure
 *
 * returns 0 on success, <0 on failure
 */
static int gelic_net_alloc_rx_skbs(struct gelic_net_card *card)
{
	struct gelic_net_descr_chain *chain;
	int ret;
	chain = &card->rx_chain;
	ret = gelic_net_fill_rx_chain(card);
	chain->head = card->rx_top->prev; /* point to the last */
	return ret;
}

/**
 * gelic_net_release_tx_descr - processes a used tx descriptor
 * @card: card structure
 * @descr: descriptor to release
 *
 * releases a used tx descriptor (unmapping, freeing of skb)
 */
static void gelic_net_release_tx_descr(struct gelic_net_card *card,
			    struct gelic_net_descr *descr)
{
	struct sk_buff *skb = descr->skb;

	BUG_ON(!(descr->data_status & (1 << GELIC_NET_TXDESC_TAIL)));

	dma_unmap_single(ctodev(card), descr->buf_addr, skb->len,
			 DMA_TO_DEVICE);
	dev_kfree_skb_any(skb);

	descr->buf_addr = 0;
	descr->buf_size = 0;
	descr->next_descr_addr = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_status = 0;
	descr->data_error = 0;
	descr->skb = NULL;

	/* set descr status */
	gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);
}

/**
 * gelic_net_release_tx_chain - processes sent tx descriptors
 * @card: adapter structure
 * @stop: net_stop sequence
 *
 * releases the tx descriptors that gelic has finished with
 */
static void gelic_net_release_tx_chain(struct gelic_net_card *card, int stop)
{
	struct gelic_net_descr_chain *tx_chain;
	enum gelic_net_descr_status status;
	int release = 0;

	for (tx_chain = &card->tx_chain;
	     tx_chain->head != tx_chain->tail && tx_chain->tail;
	     tx_chain->tail = tx_chain->tail->next) {
		status = gelic_net_get_descr_status(tx_chain->tail);
		switch (status) {
		case GELIC_NET_DESCR_RESPONSE_ERROR:
		case GELIC_NET_DESCR_PROTECTION_ERROR:
		case GELIC_NET_DESCR_FORCE_END:
			if (printk_ratelimit())
				dev_info(ctodev(card),
					 "%s: forcing end of tx descriptor " \
					 "with status %x\n",
					 __func__, status);
			card->netdev->stats.tx_dropped++;
			break;

		case GELIC_NET_DESCR_COMPLETE:
			if (tx_chain->tail->skb) {
				card->netdev->stats.tx_packets++;
				card->netdev->stats.tx_bytes +=
					tx_chain->tail->skb->len;
			}
			break;

		case GELIC_NET_DESCR_CARDOWNED:
			/* pending tx request */
		default:
			/* any other value (== GELIC_NET_DESCR_NOT_IN_USE) */
			if (!stop)
				goto out;
		}
		gelic_net_release_tx_descr(card, tx_chain->tail);
		release ++;
	}
out:
	if (!stop && release)
		netif_wake_queue(card->netdev);
}

/**
 * gelic_net_set_multi - sets multicast addresses and promisc flags
 * @netdev: interface device structure
 *
 * gelic_net_set_multi configures multicast addresses as needed for the
 * netdev interface. It also sets up multicast, allmulti and promisc
 * flags appropriately
 */
static void gelic_net_set_multi(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	struct dev_mc_list *mc;
	unsigned int i;
	uint8_t *p;
	u64 addr;
	int status;

	/* clear all multicast address */
	status = lv1_net_remove_multicast_address(bus_id(card), dev_id(card),
						  0, 1);
	if (status)
		dev_err(ctodev(card),
			"lv1_net_remove_multicast_address failed %d\n",
			status);
	/* set broadcast address */
	status = lv1_net_add_multicast_address(bus_id(card), dev_id(card),
					       GELIC_NET_BROADCAST_ADDR, 0);
	if (status)
		dev_err(ctodev(card),
			"lv1_net_add_multicast_address failed, %d\n",
			status);

	if (netdev->flags & IFF_ALLMULTI
		|| netdev->mc_count > GELIC_NET_MC_COUNT_MAX) { /* list max */
		status = lv1_net_add_multicast_address(bus_id(card),
						       dev_id(card),
						       0, 1);
		if (status)
			dev_err(ctodev(card),
				"lv1_net_add_multicast_address failed, %d\n",
				status);
		return;
	}

	/* set multicast address */
	for (mc = netdev->mc_list; mc; mc = mc->next) {
		addr = 0;
		p = mc->dmi_addr;
		for (i = 0; i < ETH_ALEN; i++) {
			addr <<= 8;
			addr |= *p++;
		}
		status = lv1_net_add_multicast_address(bus_id(card),
						       dev_id(card),
						       addr, 0);
		if (status)
			dev_err(ctodev(card),
				"lv1_net_add_multicast_address failed, %d\n",
				status);
	}
}

/**
 * gelic_net_enable_rxdmac - enables the receive DMA controller
 * @card: card structure
 *
 * gelic_net_enable_rxdmac enables the DMA controller by setting RX_DMA_EN
 * in the GDADMACCNTR register
 */
static inline void gelic_net_enable_rxdmac(struct gelic_net_card *card)
{
	int status;

	status = lv1_net_start_rx_dma(bus_id(card), dev_id(card),
				card->rx_chain.tail->bus_addr, 0);
	if (status)
		dev_info(ctodev(card),
			 "lv1_net_start_rx_dma failed, status=%d\n", status);
}

/**
 * gelic_net_disable_rxdmac - disables the receive DMA controller
 * @card: card structure
 *
 * gelic_net_disable_rxdmac terminates processing on the DMA controller by
 * turing off DMA and issueing a force end
 */
static inline void gelic_net_disable_rxdmac(struct gelic_net_card *card)
{
	int status;

	/* this hvc blocks until the DMA in progress really stopped */
	status = lv1_net_stop_rx_dma(bus_id(card), dev_id(card), 0);
	if (status)
		dev_err(ctodev(card),
			"lv1_net_stop_rx_dma faild, %d\n", status);
}

/**
 * gelic_net_disable_txdmac - disables the transmit DMA controller
 * @card: card structure
 *
 * gelic_net_disable_txdmac terminates processing on the DMA controller by
 * turing off DMA and issueing a force end
 */
static inline void gelic_net_disable_txdmac(struct gelic_net_card *card)
{
	int status;

	/* this hvc blocks until the DMA in progress really stopped */
	status = lv1_net_stop_tx_dma(bus_id(card), dev_id(card), 0);
	if (status)
		dev_err(ctodev(card),
			"lv1_net_stop_tx_dma faild, status=%d\n", status);
}

/**
 * gelic_net_stop - called upon ifconfig down
 * @netdev: interface device structure
 *
 * always returns 0
 */
static int gelic_net_stop(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	napi_disable(&card->napi);
	netif_stop_queue(netdev);

	/* turn off DMA, force end */
	gelic_net_disable_rxdmac(card);
	gelic_net_disable_txdmac(card);

	gelic_net_set_irq_mask(card, 0);

	/* disconnect event port */
	free_irq(card->netdev->irq, card->netdev);
	ps3_sb_event_receive_port_destroy(card->dev, card->netdev->irq);
	card->netdev->irq = NO_IRQ;

	netif_carrier_off(netdev);

	/* release chains */
	gelic_net_release_tx_chain(card, 1);
	gelic_net_release_rx_chain(card);

	gelic_net_free_chain(card, card->tx_top);
	gelic_net_free_chain(card, card->rx_top);

	return 0;
}

/**
 * gelic_net_get_next_tx_descr - returns the next available tx descriptor
 * @card: device structure to get descriptor from
 *
 * returns the address of the next descriptor, or NULL if not available.
 */
static struct gelic_net_descr *
gelic_net_get_next_tx_descr(struct gelic_net_card *card)
{
	if (!card->tx_chain.head)
		return NULL;
	/*  see if the next descriptor is free */
	if (card->tx_chain.tail != card->tx_chain.head->next &&
	    gelic_net_get_descr_status(card->tx_chain.head) ==
	    GELIC_NET_DESCR_NOT_IN_USE)
		return card->tx_chain.head;
	else
		return NULL;

}

/**
 * gelic_net_set_txdescr_cmdstat - sets the tx descriptor command field
 * @descr: descriptor structure to fill out
 * @skb: packet to consider
 *
 * fills out the command and status field of the descriptor structure,
 * depending on hardware checksum settings. This function assumes a wmb()
 * has executed before.
 */
static void gelic_net_set_txdescr_cmdstat(struct gelic_net_descr *descr,
					  struct sk_buff *skb)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		descr->dmac_cmd_status = GELIC_NET_DMAC_CMDSTAT_NOCS |
			GELIC_NET_DMAC_CMDSTAT_END_FRAME;
	else {
		/* is packet ip?
		 * if yes: tcp? udp? */
		if (skb->protocol == htons(ETH_P_IP)) {
			if (ip_hdr(skb)->protocol == IPPROTO_TCP)
				descr->dmac_cmd_status =
					GELIC_NET_DMAC_CMDSTAT_TCPCS |
					GELIC_NET_DMAC_CMDSTAT_END_FRAME;

			else if (ip_hdr(skb)->protocol == IPPROTO_UDP)
				descr->dmac_cmd_status =
					GELIC_NET_DMAC_CMDSTAT_UDPCS |
					GELIC_NET_DMAC_CMDSTAT_END_FRAME;
			else	/*
				 * the stack should checksum non-tcp and non-udp
				 * packets on his own: NETIF_F_IP_CSUM
				 */
				descr->dmac_cmd_status =
					GELIC_NET_DMAC_CMDSTAT_NOCS |
					GELIC_NET_DMAC_CMDSTAT_END_FRAME;
		}
	}
}

static inline struct sk_buff *gelic_put_vlan_tag(struct sk_buff *skb,
						 unsigned short tag)
{
	struct vlan_ethhdr *veth;
	static unsigned int c;

	if (skb_headroom(skb) < VLAN_HLEN) {
		struct sk_buff *sk_tmp = skb;
		pr_debug("%s: hd=%d c=%ud\n", __func__, skb_headroom(skb), c);
		skb = skb_realloc_headroom(sk_tmp, VLAN_HLEN);
		if (!skb)
			return NULL;
		dev_kfree_skb_any(sk_tmp);
	}
	veth = (struct vlan_ethhdr *)skb_push(skb, VLAN_HLEN);

	/* Move the mac addresses to the top of buffer */
	memmove(skb->data, skb->data + VLAN_HLEN, 2 * ETH_ALEN);

	veth->h_vlan_proto = __constant_htons(ETH_P_8021Q);
	veth->h_vlan_TCI = htons(tag);

	return skb;
}

/**
 * gelic_net_prepare_tx_descr_v - get dma address of skb_data
 * @card: card structure
 * @descr: descriptor structure
 * @skb: packet to use
 *
 * returns 0 on success, <0 on failure.
 *
 */
static int gelic_net_prepare_tx_descr_v(struct gelic_net_card *card,
					struct gelic_net_descr *descr,
					struct sk_buff *skb)
{
	dma_addr_t buf;

	if (card->vlan_index != -1) {
		struct sk_buff *skb_tmp;
		skb_tmp = gelic_put_vlan_tag(skb,
					     card->vlan_id[card->vlan_index]);
		if (!skb_tmp)
			return -ENOMEM;
		skb = skb_tmp;
	}

	buf = dma_map_single(ctodev(card), skb->data, skb->len, DMA_TO_DEVICE);

	if (!buf) {
		dev_err(ctodev(card),
			"dma map 2 failed (%p, %i). Dropping packet\n",
			skb->data, skb->len);
		return -ENOMEM;
	}

	descr->buf_addr = buf;
	descr->buf_size = skb->len;
	descr->skb = skb;
	descr->data_status = 0;
	descr->next_descr_addr = 0; /* terminate hw descr */
	gelic_net_set_txdescr_cmdstat(descr, skb);

	/* bump free descriptor pointer */
	card->tx_chain.head = descr->next;
	return 0;
}

/**
 * gelic_net_kick_txdma - enables TX DMA processing
 * @card: card structure
 * @descr: descriptor address to enable TX processing at
 *
 */
static int gelic_net_kick_txdma(struct gelic_net_card *card,
				struct gelic_net_descr *descr)
{
	int status = 0;

	if (card->tx_dma_progress)
		return 0;

	if (gelic_net_get_descr_status(descr) == GELIC_NET_DESCR_CARDOWNED) {
		card->tx_dma_progress = 1;
		status = lv1_net_start_tx_dma(bus_id(card), dev_id(card),
					      descr->bus_addr, 0);
		if (status)
			dev_info(ctodev(card), "lv1_net_start_txdma failed," \
				 "status=%d\n", status);
	}
	return status;
}

/**
 * gelic_net_xmit - transmits a frame over the device
 * @skb: packet to send out
 * @netdev: interface device structure
 *
 * returns 0 on success, <0 on failure
 */
static int gelic_net_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	struct gelic_net_descr *descr;
	int result;
	unsigned long flags;

	spin_lock_irqsave(&card->tx_dma_lock, flags);

	gelic_net_release_tx_chain(card, 0);

	descr = gelic_net_get_next_tx_descr(card);
	if (!descr) {
		/*
		 * no more descriptors free
		 */
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&card->tx_dma_lock, flags);
		return NETDEV_TX_BUSY;
	}

	result = gelic_net_prepare_tx_descr_v(card, descr, skb);
	if (result) {
		/*
		 * DMA map failed.  As chanses are that failure
		 * would continue, just release skb and return
		 */
		card->netdev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		spin_unlock_irqrestore(&card->tx_dma_lock, flags);
		return NETDEV_TX_OK;
	}
	/*
	 * link this prepared descriptor to previous one
	 * to achieve high performance
	 */
	descr->prev->next_descr_addr = descr->bus_addr;
	/*
	 * as hardware descriptor is modified in the above lines,
	 * ensure that the hardware sees it
	 */
	wmb();
	if (gelic_net_kick_txdma(card, descr)) {
		/*
		 * kick failed.
		 * release descriptors which were just prepared
		 */
		card->netdev->stats.tx_dropped++;
		gelic_net_release_tx_descr(card, descr);
		gelic_net_release_tx_descr(card, descr->next);
		card->tx_chain.tail = descr->next->next;
		dev_info(ctodev(card), "%s: kick failure\n", __func__);
	} else {
		/* OK, DMA started/reserved */
		netdev->trans_start = jiffies;
	}

	spin_unlock_irqrestore(&card->tx_dma_lock, flags);
	return NETDEV_TX_OK;
}

/**
 * gelic_net_pass_skb_up - takes an skb from a descriptor and passes it on
 * @descr: descriptor to process
 * @card: card structure
 *
 * iommu-unmaps the skb, fills out skb structure and passes the data to the
 * stack. The descriptor state is not changed.
 */
static void gelic_net_pass_skb_up(struct gelic_net_descr *descr,
				 struct gelic_net_card *card)
{
	struct sk_buff *skb;
	struct net_device *netdev;
	u32 data_status, data_error;

	data_status = descr->data_status;
	data_error = descr->data_error;
	netdev = card->netdev;
	/* unmap skb buffer */
	skb = descr->skb;
	dma_unmap_single(ctodev(card), descr->buf_addr, GELIC_NET_MAX_MTU,
			 DMA_FROM_DEVICE);

	skb_put(skb, descr->valid_size? descr->valid_size : descr->result_size);
	if (!descr->valid_size)
		dev_info(ctodev(card), "buffer full %x %x %x\n",
			 descr->result_size, descr->buf_size,
			 descr->dmac_cmd_status);

	descr->skb = NULL;
	/*
	 * the card put 2 bytes vlan tag in front
	 * of the ethernet frame
	 */
	skb_pull(skb, 2);
	skb->protocol = eth_type_trans(skb, netdev);

	/* checksum offload */
	if (card->rx_csum) {
		if ((data_status & GELIC_NET_DATA_STATUS_CHK_MASK) &&
		    (!(data_error & GELIC_NET_DATA_ERROR_CHK_MASK)))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	} else
		skb->ip_summed = CHECKSUM_NONE;

	/* update netdevice statistics */
	card->netdev->stats.rx_packets++;
	card->netdev->stats.rx_bytes += skb->len;

	/* pass skb up to stack */
	netif_receive_skb(skb);
}

/**
 * gelic_net_decode_one_descr - processes an rx descriptor
 * @card: card structure
 *
 * returns 1 if a packet has been sent to the stack, otherwise 0
 *
 * processes an rx descriptor by iommu-unmapping the data buffer and passing
 * the packet up to the stack
 */
static int gelic_net_decode_one_descr(struct gelic_net_card *card)
{
	enum gelic_net_descr_status status;
	struct gelic_net_descr_chain *chain = &card->rx_chain;
	struct gelic_net_descr *descr = chain->tail;
	int dmac_chain_ended;

	status = gelic_net_get_descr_status(descr);
	/* is this descriptor terminated with next_descr == NULL? */
	dmac_chain_ended =
		descr->dmac_cmd_status & GELIC_NET_DMAC_CMDSTAT_RXDCEIS;

	if (status == GELIC_NET_DESCR_CARDOWNED)
		return 0;

	if (status == GELIC_NET_DESCR_NOT_IN_USE) {
		dev_dbg(ctodev(card), "dormant descr? %p\n", descr);
		return 0;
	}

	if ((status == GELIC_NET_DESCR_RESPONSE_ERROR) ||
	    (status == GELIC_NET_DESCR_PROTECTION_ERROR) ||
	    (status == GELIC_NET_DESCR_FORCE_END)) {
		dev_info(ctodev(card), "dropping RX descriptor with state %x\n",
			 status);
		card->netdev->stats.rx_dropped++;
		goto refill;
	}

	if (status == GELIC_NET_DESCR_BUFFER_FULL) {
		/*
		 * Buffer full would occur if and only if
		 * the frame length was longer than the size of this
		 * descriptor's buffer.  If the frame length was equal
		 * to or shorter than buffer'size, FRAME_END condition
		 * would occur.
		 * Anyway this frame was longer than the MTU,
		 * just drop it.
		 */
		dev_info(ctodev(card), "overlength frame\n");
		goto refill;
	}
	/*
	 * descriptoers any other than FRAME_END here should
	 * be treated as error.
	 */
	if (status != GELIC_NET_DESCR_FRAME_END) {
		dev_dbg(ctodev(card), "RX descriptor with state %x\n",
			status);
		goto refill;
	}

	/* ok, we've got a packet in descr */
	gelic_net_pass_skb_up(descr, card);
refill:
	/*
	 * So that always DMAC can see the end
	 * of the descriptor chain to avoid
	 * from unwanted DMAC overrun.
	 */
	descr->next_descr_addr = 0;

	/* change the descriptor state: */
	gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);

	/*
	 * this call can fail, but for now, just leave this
	 * decriptor without skb
	 */
	gelic_net_prepare_rx_descr(card, descr);

	chain->head = descr;
	chain->tail = descr->next;

	/*
	 * Set this descriptor the end of the chain.
	 */
	descr->prev->next_descr_addr = descr->bus_addr;

	/*
	 * If dmac chain was met, DMAC stopped.
	 * thus re-enable it
	 */
	if (dmac_chain_ended) {
		card->rx_dma_restart_required = 1;
		dev_dbg(ctodev(card), "reenable rx dma scheduled\n");
	}

	return 1;
}

/**
 * gelic_net_poll - NAPI poll function called by the stack to return packets
 * @netdev: interface device structure
 * @budget: number of packets we can pass to the stack at most
 *
 * returns 0 if no more packets available to the driver/stack. Returns 1,
 * if the quota is exceeded, but the driver has still packets.
 *
 */
static int gelic_net_poll(struct napi_struct *napi, int budget)
{
	struct gelic_net_card *card = container_of(napi, struct gelic_net_card, napi);
	struct net_device *netdev = card->netdev;
	int packets_done = 0;

	while (packets_done < budget) {
		if (!gelic_net_decode_one_descr(card))
			break;

		packets_done++;
	}

	if (packets_done < budget) {
		netif_rx_complete(netdev, napi);
		gelic_net_rx_irq_on(card);
	}
	return packets_done;
}
/**
 * gelic_net_change_mtu - changes the MTU of an interface
 * @netdev: interface device structure
 * @new_mtu: new MTU value
 *
 * returns 0 on success, <0 on failure
 */
static int gelic_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	/* no need to re-alloc skbs or so -- the max mtu is about 2.3k
	 * and mtu is outbound only anyway */
	if ((new_mtu < GELIC_NET_MIN_MTU) ||
	    (new_mtu > GELIC_NET_MAX_MTU)) {
		return -EINVAL;
	}
	netdev->mtu = new_mtu;
	return 0;
}

/**
 * gelic_net_interrupt - event handler for gelic_net
 */
static irqreturn_t gelic_net_interrupt(int irq, void *ptr)
{
	unsigned long flags;
	struct net_device *netdev = ptr;
	struct gelic_net_card *card = netdev_priv(netdev);
	u64 status;

	status = card->irq_status;

	if (!status)
		return IRQ_NONE;

	if (card->rx_dma_restart_required) {
		card->rx_dma_restart_required = 0;
		gelic_net_enable_rxdmac(card);
	}

	if (status & GELIC_NET_RXINT) {
		gelic_net_rx_irq_off(card);
		netif_rx_schedule(netdev, &card->napi);
	}

	if (status & GELIC_NET_TXINT) {
		spin_lock_irqsave(&card->tx_dma_lock, flags);
		card->tx_dma_progress = 0;
		gelic_net_release_tx_chain(card, 0);
		/* kick outstanding tx descriptor if any */
		gelic_net_kick_txdma(card, card->tx_chain.tail);
		spin_unlock_irqrestore(&card->tx_dma_lock, flags);
	}
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * gelic_net_poll_controller - artificial interrupt for netconsole etc.
 * @netdev: interface device structure
 *
 * see Documentation/networking/netconsole.txt
 */
static void gelic_net_poll_controller(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	gelic_net_set_irq_mask(card, 0);
	gelic_net_interrupt(netdev->irq, netdev);
	gelic_net_set_irq_mask(card, card->ghiintmask);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

/**
 * gelic_net_open_device - open device and map dma region
 * @card: card structure
 */
static int gelic_net_open_device(struct gelic_net_card *card)
{
	int result;

	result = ps3_sb_event_receive_port_setup(card->dev, PS3_BINDING_CPU_ANY,
		&card->netdev->irq);

	if (result) {
		dev_info(ctodev(card),
			 "%s:%d: gelic_net_open_device failed (%d)\n",
			 __func__, __LINE__, result);
		result = -EPERM;
		goto fail_alloc_irq;
	}

	result = request_irq(card->netdev->irq, gelic_net_interrupt,
			     IRQF_DISABLED, card->netdev->name, card->netdev);

	if (result) {
		dev_info(ctodev(card), "%s:%d: request_irq failed (%d)\n",
			__func__, __LINE__, result);
		goto fail_request_irq;
	}

	return 0;

fail_request_irq:
	ps3_sb_event_receive_port_destroy(card->dev, card->netdev->irq);
	card->netdev->irq = NO_IRQ;
fail_alloc_irq:
	return result;
}


/**
 * gelic_net_open - called upon ifonfig up
 * @netdev: interface device structure
 *
 * returns 0 on success, <0 on failure
 *
 * gelic_net_open allocates all the descriptors and memory needed for
 * operation, sets up multicast list and enables interrupts
 */
static int gelic_net_open(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	dev_dbg(ctodev(card), " -> %s:%d\n", __func__, __LINE__);

	gelic_net_open_device(card);

	if (gelic_net_init_chain(card, &card->tx_chain,
			card->descr, GELIC_NET_TX_DESCRIPTORS))
		goto alloc_tx_failed;
	if (gelic_net_init_chain(card, &card->rx_chain,
				 card->descr + GELIC_NET_TX_DESCRIPTORS,
				 GELIC_NET_RX_DESCRIPTORS))
		goto alloc_rx_failed;

	/* head of chain */
	card->tx_top = card->tx_chain.head;
	card->rx_top = card->rx_chain.head;
	dev_dbg(ctodev(card), "descr rx %p, tx %p, size %#lx, num %#x\n",
		card->rx_top, card->tx_top, sizeof(struct gelic_net_descr),
		GELIC_NET_RX_DESCRIPTORS);
	/* allocate rx skbs */
	if (gelic_net_alloc_rx_skbs(card))
		goto alloc_skbs_failed;

	napi_enable(&card->napi);

	card->tx_dma_progress = 0;
	card->ghiintmask = GELIC_NET_RXINT | GELIC_NET_TXINT;

	gelic_net_set_irq_mask(card, card->ghiintmask);
	gelic_net_enable_rxdmac(card);

	netif_start_queue(netdev);
	netif_carrier_on(netdev);

	return 0;

alloc_skbs_failed:
	gelic_net_free_chain(card, card->rx_top);
alloc_rx_failed:
	gelic_net_free_chain(card, card->tx_top);
alloc_tx_failed:
	return -ENOMEM;
}

static void gelic_net_get_drvinfo (struct net_device *netdev,
				   struct ethtool_drvinfo *info)
{
	strncpy(info->driver, DRV_NAME, sizeof(info->driver) - 1);
	strncpy(info->version, DRV_VERSION, sizeof(info->version) - 1);
}

static int gelic_net_get_settings(struct net_device *netdev,
				  struct ethtool_cmd *cmd)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	int status;
	u64 v1, v2;
	int speed, duplex;

	speed = duplex = -1;
	status = lv1_net_control(bus_id(card), dev_id(card),
			GELIC_NET_GET_ETH_PORT_STATUS, GELIC_NET_PORT, 0, 0,
			&v1, &v2);
	if (status) {
		/* link down */
	} else {
		if (v1 & GELIC_NET_FULL_DUPLEX) {
			duplex = DUPLEX_FULL;
		} else {
			duplex = DUPLEX_HALF;
		}

		if (v1 & GELIC_NET_SPEED_10 ) {
			speed = SPEED_10;
		} else if (v1 & GELIC_NET_SPEED_100) {
			speed = SPEED_100;
		} else if (v1 & GELIC_NET_SPEED_1000) {
			speed = SPEED_1000;
		}
	}
	cmd->supported = SUPPORTED_TP | SUPPORTED_Autoneg |
			SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
			SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full;
	cmd->advertising = cmd->supported;
	cmd->speed = speed;
	cmd->duplex = duplex;
	cmd->autoneg = AUTONEG_ENABLE; /* always enabled */
	cmd->port = PORT_TP;

	return 0;
}

static u32 gelic_net_get_link(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	int status;
	u64 v1, v2;
	int link;

	status = lv1_net_control(bus_id(card), dev_id(card),
			GELIC_NET_GET_ETH_PORT_STATUS, GELIC_NET_PORT, 0, 0,
			&v1, &v2);
	if (status)
		return 0; /* link down */

	if (v1 & GELIC_NET_LINK_UP)
		link = 1;
	else
		link = 0;

	return link;
}

static int gelic_net_nway_reset(struct net_device *netdev)
{
	if (netif_running(netdev)) {
		gelic_net_stop(netdev);
		gelic_net_open(netdev);
	}
	return 0;
}

static u32 gelic_net_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_IP_CSUM) != 0;
}

static int gelic_net_set_tx_csum(struct net_device *netdev, u32 data)
{
	if (data)
		netdev->features |= NETIF_F_IP_CSUM;
	else
		netdev->features &= ~NETIF_F_IP_CSUM;

	return 0;
}

static u32 gelic_net_get_rx_csum(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	return card->rx_csum;
}

static int gelic_net_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	card->rx_csum = data;
	return 0;
}

static struct ethtool_ops gelic_net_ethtool_ops = {
	.get_drvinfo	= gelic_net_get_drvinfo,
	.get_settings	= gelic_net_get_settings,
	.get_link	= gelic_net_get_link,
	.nway_reset	= gelic_net_nway_reset,
	.get_tx_csum	= gelic_net_get_tx_csum,
	.set_tx_csum	= gelic_net_set_tx_csum,
	.get_rx_csum	= gelic_net_get_rx_csum,
	.set_rx_csum	= gelic_net_set_rx_csum,
};

/**
 * gelic_net_tx_timeout_task - task scheduled by the watchdog timeout
 * function (to be called not under interrupt status)
 * @work: work is context of tx timout task
 *
 * called as task when tx hangs, resets interface (if interface is up)
 */
static void gelic_net_tx_timeout_task(struct work_struct *work)
{
	struct gelic_net_card *card =
		container_of(work, struct gelic_net_card, tx_timeout_task);
	struct net_device *netdev = card->netdev;

	dev_info(ctodev(card), "%s:Timed out. Restarting... \n", __func__);

	if (!(netdev->flags & IFF_UP))
		goto out;

	netif_device_detach(netdev);
	gelic_net_stop(netdev);

	gelic_net_open(netdev);
	netif_device_attach(netdev);

out:
	atomic_dec(&card->tx_timeout_task_counter);
}

/**
 * gelic_net_tx_timeout - called when the tx timeout watchdog kicks in.
 * @netdev: interface device structure
 *
 * called, if tx hangs. Schedules a task that resets the interface
 */
static void gelic_net_tx_timeout(struct net_device *netdev)
{
	struct gelic_net_card *card;

	card = netdev_priv(netdev);
	atomic_inc(&card->tx_timeout_task_counter);
	if (netdev->flags & IFF_UP)
		schedule_work(&card->tx_timeout_task);
	else
		atomic_dec(&card->tx_timeout_task_counter);
}

/**
 * gelic_net_setup_netdev_ops - initialization of net_device operations
 * @netdev: net_device structure
 *
 * fills out function pointers in the net_device structure
 */
static void gelic_net_setup_netdev_ops(struct net_device *netdev)
{
	netdev->open = &gelic_net_open;
	netdev->stop = &gelic_net_stop;
	netdev->hard_start_xmit = &gelic_net_xmit;
	netdev->set_multicast_list = &gelic_net_set_multi;
	netdev->change_mtu = &gelic_net_change_mtu;
	/* tx watchdog */
	netdev->tx_timeout = &gelic_net_tx_timeout;
	netdev->watchdog_timeo = GELIC_NET_WATCHDOG_TIMEOUT;
	netdev->ethtool_ops = &gelic_net_ethtool_ops;
}

/**
 * gelic_net_setup_netdev - initialization of net_device
 * @card: card structure
 *
 * Returns 0 on success or <0 on failure
 *
 * gelic_net_setup_netdev initializes the net_device structure
 **/
static int gelic_net_setup_netdev(struct gelic_net_card *card)
{
	struct net_device *netdev = card->netdev;
	struct sockaddr addr;
	unsigned int i;
	int status;
	u64 v1, v2;
	DECLARE_MAC_BUF(mac);

	SET_NETDEV_DEV(netdev, &card->dev->core);
	spin_lock_init(&card->tx_dma_lock);

	card->rx_csum = GELIC_NET_RX_CSUM_DEFAULT;

	gelic_net_setup_netdev_ops(netdev);

	netif_napi_add(netdev, &card->napi,
		       gelic_net_poll, GELIC_NET_NAPI_WEIGHT);

	netdev->features = NETIF_F_IP_CSUM;

	status = lv1_net_control(bus_id(card), dev_id(card),
				 GELIC_NET_GET_MAC_ADDRESS,
				 0, 0, 0, &v1, &v2);
	if (status || !is_valid_ether_addr((u8 *)&v1)) {
		dev_info(ctodev(card),
			 "%s:lv1_net_control GET_MAC_ADDR failed %d\n",
			 __func__, status);
		return -EINVAL;
	}
	v1 <<= 16;
	memcpy(addr.sa_data, &v1, ETH_ALEN);
	memcpy(netdev->dev_addr, addr.sa_data, ETH_ALEN);
	dev_info(ctodev(card), "MAC addr %s\n",
		 print_mac(mac, netdev->dev_addr));

	card->vlan_index = -1;	/* no vlan */
	for (i = 0; i < GELIC_NET_VLAN_MAX; i++) {
		status = lv1_net_control(bus_id(card), dev_id(card),
					GELIC_NET_GET_VLAN_ID,
					i + 1, /* index; one based */
					0, 0, &v1, &v2);
		if (status == GELIC_NET_VLAN_NO_ENTRY) {
			dev_dbg(ctodev(card),
				"GELIC_VLAN_ID no entry:%d, VLAN disabled\n",
				status);
			card->vlan_id[i] = 0;
		} else if (status) {
			dev_dbg(ctodev(card),
				"%s:GELIC_NET_VLAN_ID faild, status=%d\n",
				__func__, status);
			card->vlan_id[i] = 0;
		} else {
			card->vlan_id[i] = (u32)v1;
			dev_dbg(ctodev(card), "vlan_id:%d, %lx\n", i, v1);
		}
	}

	if (card->vlan_id[GELIC_NET_VLAN_WIRED - 1]) {
		card->vlan_index = GELIC_NET_VLAN_WIRED - 1;
		netdev->hard_header_len += VLAN_HLEN;
	}

	status = register_netdev(netdev);
	if (status) {
		dev_err(ctodev(card), "%s:Couldn't register net_device: %d\n",
			__func__, status);
		return status;
	}

	return 0;
}

/**
 * gelic_net_alloc_card - allocates net_device and card structure
 *
 * returns the card structure or NULL in case of errors
 *
 * the card and net_device structures are linked to each other
 */
static struct gelic_net_card *gelic_net_alloc_card(void)
{
	struct net_device *netdev;
	struct gelic_net_card *card;
	size_t alloc_size;

	alloc_size = sizeof (*card) +
		sizeof (struct gelic_net_descr) * GELIC_NET_RX_DESCRIPTORS +
		sizeof (struct gelic_net_descr) * GELIC_NET_TX_DESCRIPTORS;
	/*
	 * we assume private data is allocated 32 bytes (or more) aligned
	 * so that gelic_net_descr should be 32 bytes aligned.
	 * Current alloc_etherdev() does do it because NETDEV_ALIGN
	 * is 32.
	 * check this assumption here.
	 */
	BUILD_BUG_ON(NETDEV_ALIGN < 32);
	BUILD_BUG_ON(offsetof(struct gelic_net_card, irq_status) % 8);
	BUILD_BUG_ON(offsetof(struct gelic_net_card, descr) % 32);

	netdev = alloc_etherdev(alloc_size);
	if (!netdev)
		return NULL;

	card = netdev_priv(netdev);
	card->netdev = netdev;
	INIT_WORK(&card->tx_timeout_task, gelic_net_tx_timeout_task);
	init_waitqueue_head(&card->waitq);
	atomic_set(&card->tx_timeout_task_counter, 0);

	return card;
}

/**
 * ps3_gelic_driver_probe - add a device to the control of this driver
 */
static int ps3_gelic_driver_probe (struct ps3_system_bus_device *dev)
{
	struct gelic_net_card *card = gelic_net_alloc_card();
	int result;

	if (!card) {
		dev_info(&dev->core, "gelic_net_alloc_card failed\n");
		result = -ENOMEM;
		goto fail_alloc_card;
	}

	ps3_system_bus_set_driver_data(dev, card);
	card->dev = dev;

	result = ps3_open_hv_device(dev);

	if (result) {
		dev_dbg(&dev->core, "ps3_open_hv_device failed\n");
		goto fail_open;
	}

	result = ps3_dma_region_create(dev->d_region);

	if (result) {
		dev_dbg(&dev->core, "ps3_dma_region_create failed(%d)\n",
			result);
		BUG_ON("check region type");
		goto fail_dma_region;
	}

	result = lv1_net_set_interrupt_status_indicator(bus_id(card),
							dev_id(card),
		ps3_mm_phys_to_lpar(__pa(&card->irq_status)),
		0);

	if (result) {
		dev_dbg(&dev->core,
			"lv1_net_set_interrupt_status_indicator failed: %s\n",
			ps3_result(result));
		result = -EIO;
		goto fail_status_indicator;
	}

	result = gelic_net_setup_netdev(card);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: ps3_dma_region_create failed: "
			"(%d)\n", __func__, __LINE__, result);
		goto fail_setup_netdev;
	}

	return 0;

fail_setup_netdev:
	lv1_net_set_interrupt_status_indicator(bus_id(card),
					       bus_id(card),
					       0 , 0);
fail_status_indicator:
	ps3_dma_region_free(dev->d_region);
fail_dma_region:
	ps3_close_hv_device(dev);
fail_open:
	ps3_system_bus_set_driver_data(dev, NULL);
	free_netdev(card->netdev);
fail_alloc_card:
	return result;
}

/**
 * ps3_gelic_driver_remove - remove a device from the control of this driver
 */

static int ps3_gelic_driver_remove (struct ps3_system_bus_device *dev)
{
	struct gelic_net_card *card = ps3_system_bus_get_driver_data(dev);

	wait_event(card->waitq,
		   atomic_read(&card->tx_timeout_task_counter) == 0);

	lv1_net_set_interrupt_status_indicator(bus_id(card), dev_id(card),
					       0 , 0);

	unregister_netdev(card->netdev);
	free_netdev(card->netdev);

	ps3_system_bus_set_driver_data(dev, NULL);

	ps3_dma_region_free(dev->d_region);

	ps3_close_hv_device(dev);

	return 0;
}

static struct ps3_system_bus_driver ps3_gelic_driver = {
	.match_id = PS3_MATCH_ID_GELIC,
	.probe = ps3_gelic_driver_probe,
	.remove = ps3_gelic_driver_remove,
	.shutdown = ps3_gelic_driver_remove,
	.core.name = "ps3_gelic_driver",
	.core.owner = THIS_MODULE,
};

static int __init ps3_gelic_driver_init (void)
{
	return firmware_has_feature(FW_FEATURE_PS3_LV1)
		? ps3_system_bus_driver_register(&ps3_gelic_driver)
		: -ENODEV;
}

static void __exit ps3_gelic_driver_exit (void)
{
	ps3_system_bus_driver_unregister(&ps3_gelic_driver);
}

module_init (ps3_gelic_driver_init);
module_exit (ps3_gelic_driver_exit);

MODULE_ALIAS(PS3_MODULE_ALIAS_GELIC);

