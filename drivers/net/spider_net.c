/*
 * Network device driver for Cell Processor-Based Blade
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

#include <linux/compiler.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <asm/bitops.h>
#include <asm/pci-bridge.h>
#include <net/checksum.h>

#include "spider_net.h"

MODULE_AUTHOR("Utz Bacher <utz.bacher@de.ibm.com> and Jens Osterkamp " \
	      "<Jens.Osterkamp@de.ibm.com>");
MODULE_DESCRIPTION("Spider Southbridge Gigabit Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);

static int rx_descriptors = SPIDER_NET_RX_DESCRIPTORS_DEFAULT;
static int tx_descriptors = SPIDER_NET_TX_DESCRIPTORS_DEFAULT;

module_param(rx_descriptors, int, 0444);
module_param(tx_descriptors, int, 0444);

MODULE_PARM_DESC(rx_descriptors, "number of descriptors used " \
		 "in rx chains");
MODULE_PARM_DESC(tx_descriptors, "number of descriptors used " \
		 "in tx chain");

char spider_net_driver_name[] = "spidernet";

static struct pci_device_id spider_net_pci_tbl[] = {
	{ PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_SPIDER_NET,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, spider_net_pci_tbl);

/**
 * spider_net_read_reg - reads an SMMIO register of a card
 * @card: device structure
 * @reg: register to read from
 *
 * returns the content of the specified SMMIO register.
 */
static inline u32
spider_net_read_reg(struct spider_net_card *card, u32 reg)
{
	/* We use the powerpc specific variants instead of readl_be() because
	 * we know spidernet is not a real PCI device and we can thus avoid the
	 * performance hit caused by the PCI workarounds.
	 */
	return in_be32(card->regs + reg);
}

/**
 * spider_net_write_reg - writes to an SMMIO register of a card
 * @card: device structure
 * @reg: register to write to
 * @value: value to write into the specified SMMIO register
 */
static inline void
spider_net_write_reg(struct spider_net_card *card, u32 reg, u32 value)
{
	/* We use the powerpc specific variants instead of writel_be() because
	 * we know spidernet is not a real PCI device and we can thus avoid the
	 * performance hit caused by the PCI workarounds.
	 */
	out_be32(card->regs + reg, value);
}

/** spider_net_write_phy - write to phy register
 * @netdev: adapter to be written to
 * @mii_id: id of MII
 * @reg: PHY register
 * @val: value to be written to phy register
 *
 * spider_net_write_phy_register writes to an arbitrary PHY
 * register via the spider GPCWOPCMD register. We assume the queue does
 * not run full (not more than 15 commands outstanding).
 **/
static void
spider_net_write_phy(struct net_device *netdev, int mii_id,
		     int reg, int val)
{
	struct spider_net_card *card = netdev_priv(netdev);
	u32 writevalue;

	writevalue = ((u32)mii_id << 21) |
		((u32)reg << 16) | ((u32)val);

	spider_net_write_reg(card, SPIDER_NET_GPCWOPCMD, writevalue);
}

/** spider_net_read_phy - read from phy register
 * @netdev: network device to be read from
 * @mii_id: id of MII
 * @reg: PHY register
 *
 * Returns value read from PHY register
 *
 * spider_net_write_phy reads from an arbitrary PHY
 * register via the spider GPCROPCMD register
 **/
static int
spider_net_read_phy(struct net_device *netdev, int mii_id, int reg)
{
	struct spider_net_card *card = netdev_priv(netdev);
	u32 readvalue;

	readvalue = ((u32)mii_id << 21) | ((u32)reg << 16);
	spider_net_write_reg(card, SPIDER_NET_GPCROPCMD, readvalue);

	/* we don't use semaphores to wait for an SPIDER_NET_GPROPCMPINT
	 * interrupt, as we poll for the completion of the read operation
	 * in spider_net_read_phy. Should take about 50 us */
	do {
		readvalue = spider_net_read_reg(card, SPIDER_NET_GPCROPCMD);
	} while (readvalue & SPIDER_NET_GPREXEC);

	readvalue &= SPIDER_NET_GPRDAT_MASK;

	return readvalue;
}

/**
 * spider_net_rx_irq_off - switch off rx irq on this spider card
 * @card: device structure
 *
 * switches off rx irq by masking them out in the GHIINTnMSK register
 */
static void
spider_net_rx_irq_off(struct spider_net_card *card)
{
	u32 regvalue;

	regvalue = SPIDER_NET_INT0_MASK_VALUE & (~SPIDER_NET_RXINT);
	spider_net_write_reg(card, SPIDER_NET_GHIINT0MSK, regvalue);
}

/**
 * spider_net_rx_irq_on - switch on rx irq on this spider card
 * @card: device structure
 *
 * switches on rx irq by enabling them in the GHIINTnMSK register
 */
static void
spider_net_rx_irq_on(struct spider_net_card *card)
{
	u32 regvalue;

	regvalue = SPIDER_NET_INT0_MASK_VALUE | SPIDER_NET_RXINT;
	spider_net_write_reg(card, SPIDER_NET_GHIINT0MSK, regvalue);
}

/**
 * spider_net_set_promisc - sets the unicast address or the promiscuous mode
 * @card: card structure
 *
 * spider_net_set_promisc sets the unicast destination address filter and
 * thus either allows for non-promisc mode or promisc mode
 */
static void
spider_net_set_promisc(struct spider_net_card *card)
{
	u32 macu, macl;
	struct net_device *netdev = card->netdev;

	if (netdev->flags & IFF_PROMISC) {
		/* clear destination entry 0 */
		spider_net_write_reg(card, SPIDER_NET_GMRUAFILnR, 0);
		spider_net_write_reg(card, SPIDER_NET_GMRUAFILnR + 0x04, 0);
		spider_net_write_reg(card, SPIDER_NET_GMRUA0FIL15R,
				     SPIDER_NET_PROMISC_VALUE);
	} else {
		macu = netdev->dev_addr[0];
		macu <<= 8;
		macu |= netdev->dev_addr[1];
		memcpy(&macl, &netdev->dev_addr[2], sizeof(macl));

		macu |= SPIDER_NET_UA_DESCR_VALUE;
		spider_net_write_reg(card, SPIDER_NET_GMRUAFILnR, macu);
		spider_net_write_reg(card, SPIDER_NET_GMRUAFILnR + 0x04, macl);
		spider_net_write_reg(card, SPIDER_NET_GMRUA0FIL15R,
				     SPIDER_NET_NONPROMISC_VALUE);
	}
}

/**
 * spider_net_get_mac_address - read mac address from spider card
 * @card: device structure
 *
 * reads MAC address from GMACUNIMACU and GMACUNIMACL registers
 */
static int
spider_net_get_mac_address(struct net_device *netdev)
{
	struct spider_net_card *card = netdev_priv(netdev);
	u32 macl, macu;

	macl = spider_net_read_reg(card, SPIDER_NET_GMACUNIMACL);
	macu = spider_net_read_reg(card, SPIDER_NET_GMACUNIMACU);

	netdev->dev_addr[0] = (macu >> 24) & 0xff;
	netdev->dev_addr[1] = (macu >> 16) & 0xff;
	netdev->dev_addr[2] = (macu >> 8) & 0xff;
	netdev->dev_addr[3] = macu & 0xff;
	netdev->dev_addr[4] = (macl >> 8) & 0xff;
	netdev->dev_addr[5] = macl & 0xff;

	if (!is_valid_ether_addr(&netdev->dev_addr[0]))
		return -EINVAL;

	return 0;
}

/**
 * spider_net_get_descr_status -- returns the status of a descriptor
 * @descr: descriptor to look at
 *
 * returns the status as in the dmac_cmd_status field of the descriptor
 */
static inline int
spider_net_get_descr_status(struct spider_net_descr *descr)
{
	return descr->dmac_cmd_status & SPIDER_NET_DESCR_IND_PROC_MASK;
}

/**
 * spider_net_free_chain - free descriptor chain
 * @card: card structure
 * @chain: address of chain
 *
 */
static void
spider_net_free_chain(struct spider_net_card *card,
		      struct spider_net_descr_chain *chain)
{
	struct spider_net_descr *descr;

	for (descr = chain->tail; !descr->bus_addr; descr = descr->next) {
		pci_unmap_single(card->pdev, descr->bus_addr,
				 SPIDER_NET_DESCR_SIZE, PCI_DMA_BIDIRECTIONAL);
		descr->bus_addr = 0;
	}
}

/**
 * spider_net_init_chain - links descriptor chain
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
static int
spider_net_init_chain(struct spider_net_card *card,
		       struct spider_net_descr_chain *chain,
		       struct spider_net_descr *start_descr,
		       int no)
{
	int i;
	struct spider_net_descr *descr;
	dma_addr_t buf;

	descr = start_descr;
	memset(descr, 0, sizeof(*descr) * no);

	/* set up the hardware pointers in each descriptor */
	for (i=0; i<no; i++, descr++) {
		descr->dmac_cmd_status = SPIDER_NET_DESCR_NOT_IN_USE;

		buf = pci_map_single(card->pdev, descr,
				     SPIDER_NET_DESCR_SIZE,
				     PCI_DMA_BIDIRECTIONAL);

		if (pci_dma_mapping_error(buf))
			goto iommu_error;

		descr->bus_addr = buf;
		descr->next = descr + 1;
		descr->prev = descr - 1;

	}
	/* do actual circular list */
	(descr-1)->next = start_descr;
	start_descr->prev = descr-1;

	spin_lock_init(&chain->lock);
	chain->head = start_descr;
	chain->tail = start_descr;

	return 0;

iommu_error:
	descr = start_descr;
	for (i=0; i < no; i++, descr++)
		if (descr->bus_addr)
			pci_unmap_single(card->pdev, descr->bus_addr,
					 SPIDER_NET_DESCR_SIZE,
					 PCI_DMA_BIDIRECTIONAL);
	return -ENOMEM;
}

/**
 * spider_net_free_rx_chain_contents - frees descr contents in rx chain
 * @card: card structure
 *
 * returns 0 on success, <0 on failure
 */
static void
spider_net_free_rx_chain_contents(struct spider_net_card *card)
{
	struct spider_net_descr *descr;

	descr = card->rx_chain.head;
	do {
		if (descr->skb) {
			dev_kfree_skb(descr->skb);
			pci_unmap_single(card->pdev, descr->buf_addr,
					 SPIDER_NET_MAX_FRAME,
					 PCI_DMA_BIDIRECTIONAL);
		}
		descr = descr->next;
	} while (descr != card->rx_chain.head);
}

/**
 * spider_net_prepare_rx_descr - reinitializes a rx descriptor
 * @card: card structure
 * @descr: descriptor to re-init
 *
 * return 0 on succes, <0 on failure
 *
 * allocates a new rx skb, iommu-maps it and attaches it to the descriptor.
 * Activate the descriptor state-wise
 */
static int
spider_net_prepare_rx_descr(struct spider_net_card *card,
			    struct spider_net_descr *descr)
{
	dma_addr_t buf;
	int error = 0;
	int offset;
	int bufsize;

	/* we need to round up the buffer size to a multiple of 128 */
	bufsize = (SPIDER_NET_MAX_FRAME + SPIDER_NET_RXBUF_ALIGN - 1) &
		(~(SPIDER_NET_RXBUF_ALIGN - 1));

	/* and we need to have it 128 byte aligned, therefore we allocate a
	 * bit more */
	/* allocate an skb */
	descr->skb = dev_alloc_skb(bufsize + SPIDER_NET_RXBUF_ALIGN - 1);
	if (!descr->skb) {
		if (netif_msg_rx_err(card) && net_ratelimit())
			pr_err("Not enough memory to allocate rx buffer\n");
		card->spider_stats.alloc_rx_skb_error++;
		return -ENOMEM;
	}
	descr->buf_size = bufsize;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_status = 0;
	descr->data_error = 0;

	offset = ((unsigned long)descr->skb->data) &
		(SPIDER_NET_RXBUF_ALIGN - 1);
	if (offset)
		skb_reserve(descr->skb, SPIDER_NET_RXBUF_ALIGN - offset);
	/* io-mmu-map the skb */
	buf = pci_map_single(card->pdev, descr->skb->data,
			SPIDER_NET_MAX_FRAME, PCI_DMA_FROMDEVICE);
	descr->buf_addr = buf;
	if (pci_dma_mapping_error(buf)) {
		dev_kfree_skb_any(descr->skb);
		if (netif_msg_rx_err(card) && net_ratelimit())
			pr_err("Could not iommu-map rx buffer\n");
		card->spider_stats.rx_iommu_map_error++;
		descr->dmac_cmd_status = SPIDER_NET_DESCR_NOT_IN_USE;
	} else {
		descr->dmac_cmd_status = SPIDER_NET_DESCR_CARDOWNED |
					 SPIDER_NET_DMAC_NOINTR_COMPLETE;
	}

	return error;
}

/**
 * spider_net_enable_rxchtails - sets RX dmac chain tail addresses
 * @card: card structure
 *
 * spider_net_enable_rxchtails sets the RX DMAC chain tail adresses in the
 * chip by writing to the appropriate register. DMA is enabled in
 * spider_net_enable_rxdmac.
 */
static inline void
spider_net_enable_rxchtails(struct spider_net_card *card)
{
	/* assume chain is aligned correctly */
	spider_net_write_reg(card, SPIDER_NET_GDADCHA ,
			     card->rx_chain.tail->bus_addr);
}

/**
 * spider_net_enable_rxdmac - enables a receive DMA controller
 * @card: card structure
 *
 * spider_net_enable_rxdmac enables the DMA controller by setting RX_DMA_EN
 * in the GDADMACCNTR register
 */
static inline void
spider_net_enable_rxdmac(struct spider_net_card *card)
{
	wmb();
	spider_net_write_reg(card, SPIDER_NET_GDADMACCNTR,
			     SPIDER_NET_DMA_RX_VALUE);
}

/**
 * spider_net_refill_rx_chain - refills descriptors/skbs in the rx chains
 * @card: card structure
 *
 * refills descriptors in the rx chain: allocates skbs and iommu-maps them.
 */
static void
spider_net_refill_rx_chain(struct spider_net_card *card)
{
	struct spider_net_descr_chain *chain = &card->rx_chain;
	unsigned long flags;

	/* one context doing the refill (and a second context seeing that
	 * and omitting it) is ok. If called by NAPI, we'll be called again
	 * as spider_net_decode_one_descr is called several times. If some
	 * interrupt calls us, the NAPI is about to clean up anyway. */
	if (!spin_trylock_irqsave(&chain->lock, flags))
		return;

	while (spider_net_get_descr_status(chain->head) ==
			SPIDER_NET_DESCR_NOT_IN_USE) {
		if (spider_net_prepare_rx_descr(card, chain->head))
			break;
		chain->head = chain->head->next;
	}

	spin_unlock_irqrestore(&chain->lock, flags);
}

/**
 * spider_net_alloc_rx_skbs - allocates rx skbs in rx descriptor chains
 * @card: card structure
 *
 * returns 0 on success, <0 on failure
 */
static int
spider_net_alloc_rx_skbs(struct spider_net_card *card)
{
	int result;
	struct spider_net_descr_chain *chain;

	result = -ENOMEM;

	chain = &card->rx_chain;
	/* put at least one buffer into the chain. if this fails,
	 * we've got a problem. if not, spider_net_refill_rx_chain
	 * will do the rest at the end of this function */
	if (spider_net_prepare_rx_descr(card, chain->head))
		goto error;
	else
		chain->head = chain->head->next;

	/* this will allocate the rest of the rx buffers; if not, it's
	 * business as usual later on */
	spider_net_refill_rx_chain(card);
	spider_net_enable_rxdmac(card);
	return 0;

error:
	spider_net_free_rx_chain_contents(card);
	return result;
}

/**
 * spider_net_get_multicast_hash - generates hash for multicast filter table
 * @addr: multicast address
 *
 * returns the hash value.
 *
 * spider_net_get_multicast_hash calculates a hash value for a given multicast
 * address, that is used to set the multicast filter tables
 */
static u8
spider_net_get_multicast_hash(struct net_device *netdev, __u8 *addr)
{
	u32 crc;
	u8 hash;
	char addr_for_crc[ETH_ALEN] = { 0, };
	int i, bit;

	for (i = 0; i < ETH_ALEN * 8; i++) {
		bit = (addr[i / 8] >> (i % 8)) & 1;
		addr_for_crc[ETH_ALEN - 1 - i / 8] += bit << (7 - (i % 8));
	}

	crc = crc32_be(~0, addr_for_crc, netdev->addr_len);

	hash = (crc >> 27);
	hash <<= 3;
	hash |= crc & 7;
	hash &= 0xff;

	return hash;
}

/**
 * spider_net_set_multi - sets multicast addresses and promisc flags
 * @netdev: interface device structure
 *
 * spider_net_set_multi configures multicast addresses as needed for the
 * netdev interface. It also sets up multicast, allmulti and promisc
 * flags appropriately
 */
static void
spider_net_set_multi(struct net_device *netdev)
{
	struct dev_mc_list *mc;
	u8 hash;
	int i;
	u32 reg;
	struct spider_net_card *card = netdev_priv(netdev);
	unsigned long bitmask[SPIDER_NET_MULTICAST_HASHES / BITS_PER_LONG] =
		{0, };

	spider_net_set_promisc(card);

	if (netdev->flags & IFF_ALLMULTI) {
		for (i = 0; i < SPIDER_NET_MULTICAST_HASHES; i++) {
			set_bit(i, bitmask);
		}
		goto write_hash;
	}

	/* well, we know, what the broadcast hash value is: it's xfd
	hash = spider_net_get_multicast_hash(netdev, netdev->broadcast); */
	set_bit(0xfd, bitmask);

	for (mc = netdev->mc_list; mc; mc = mc->next) {
		hash = spider_net_get_multicast_hash(netdev, mc->dmi_addr);
		set_bit(hash, bitmask);
	}

write_hash:
	for (i = 0; i < SPIDER_NET_MULTICAST_HASHES / 4; i++) {
		reg = 0;
		if (test_bit(i * 4, bitmask))
			reg += 0x08;
		reg <<= 8;
		if (test_bit(i * 4 + 1, bitmask))
			reg += 0x08;
		reg <<= 8;
		if (test_bit(i * 4 + 2, bitmask))
			reg += 0x08;
		reg <<= 8;
		if (test_bit(i * 4 + 3, bitmask))
			reg += 0x08;

		spider_net_write_reg(card, SPIDER_NET_GMRMHFILnR + i * 4, reg);
	}
}

/**
 * spider_net_disable_rxdmac - disables the receive DMA controller
 * @card: card structure
 *
 * spider_net_disable_rxdmac terminates processing on the DMA controller by
 * turing off DMA and issueing a force end
 */
static void
spider_net_disable_rxdmac(struct spider_net_card *card)
{
	spider_net_write_reg(card, SPIDER_NET_GDADMACCNTR,
			     SPIDER_NET_DMA_RX_FEND_VALUE);
}

/**
 * spider_net_prepare_tx_descr - fill tx descriptor with skb data
 * @card: card structure
 * @descr: descriptor structure to fill out
 * @skb: packet to use
 *
 * returns 0 on success, <0 on failure.
 *
 * fills out the descriptor structure with skb data and len. Copies data,
 * if needed (32bit DMA!)
 */
static int
spider_net_prepare_tx_descr(struct spider_net_card *card,
			    struct sk_buff *skb)
{
	struct spider_net_descr *descr;
	dma_addr_t buf;
	unsigned long flags;

	buf = pci_map_single(card->pdev, skb->data, skb->len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(buf)) {
		if (netif_msg_tx_err(card) && net_ratelimit())
			pr_err("could not iommu-map packet (%p, %i). "
				  "Dropping packet\n", skb->data, skb->len);
		card->spider_stats.tx_iommu_map_error++;
		return -ENOMEM;
	}

	spin_lock_irqsave(&card->tx_chain.lock, flags);
	descr = card->tx_chain.head;
	card->tx_chain.head = descr->next;

	descr->buf_addr = buf;
	descr->buf_size = skb->len;
	descr->next_descr_addr = 0;
	descr->skb = skb;
	descr->data_status = 0;

	descr->dmac_cmd_status =
			SPIDER_NET_DESCR_CARDOWNED | SPIDER_NET_DMAC_NOCS;
	spin_unlock_irqrestore(&card->tx_chain.lock, flags);

	if (skb->protocol == htons(ETH_P_IP))
		switch (skb->nh.iph->protocol) {
		case IPPROTO_TCP:
			descr->dmac_cmd_status |= SPIDER_NET_DMAC_TCP;
			break;
		case IPPROTO_UDP:
			descr->dmac_cmd_status |= SPIDER_NET_DMAC_UDP;
			break;
		}

	/* Chain the bus address, so that the DMA engine finds this descr. */
	descr->prev->next_descr_addr = descr->bus_addr;

	card->netdev->trans_start = jiffies; /* set netdev watchdog timer */
	return 0;
}

static int
spider_net_set_low_watermark(struct spider_net_card *card)
{
	unsigned long flags;
	int status;
	int cnt=0;
	int i;
	struct spider_net_descr *descr = card->tx_chain.tail;

	/* Measure the length of the queue. Measurement does not
	 * need to be precise -- does not need a lock. */
	while (descr != card->tx_chain.head) {
		status = descr->dmac_cmd_status & SPIDER_NET_DESCR_NOT_IN_USE;
		if (status == SPIDER_NET_DESCR_NOT_IN_USE)
			break;
		descr = descr->next;
		cnt++;
	}

	/* If TX queue is short, don't even bother with interrupts */
	if (cnt < card->num_tx_desc/4)
		return cnt;

	/* Set low-watermark 3/4th's of the way into the queue. */
	descr = card->tx_chain.tail;
	cnt = (cnt*3)/4;
	for (i=0;i<cnt; i++)
		descr = descr->next;

	/* Set the new watermark, clear the old watermark */
	spin_lock_irqsave(&card->tx_chain.lock, flags);
	descr->dmac_cmd_status |= SPIDER_NET_DESCR_TXDESFLG;
	if (card->low_watermark && card->low_watermark != descr)
		card->low_watermark->dmac_cmd_status =
		     card->low_watermark->dmac_cmd_status & ~SPIDER_NET_DESCR_TXDESFLG;
	card->low_watermark = descr;
	spin_unlock_irqrestore(&card->tx_chain.lock, flags);
	return cnt;
}

/**
 * spider_net_release_tx_chain - processes sent tx descriptors
 * @card: adapter structure
 * @brutal: if set, don't care about whether descriptor seems to be in use
 *
 * returns 0 if the tx ring is empty, otherwise 1.
 *
 * spider_net_release_tx_chain releases the tx descriptors that spider has
 * finished with (if non-brutal) or simply release tx descriptors (if brutal).
 * If some other context is calling this function, we return 1 so that we're
 * scheduled again (if we were scheduled) and will not loose initiative.
 */
static int
spider_net_release_tx_chain(struct spider_net_card *card, int brutal)
{
	struct spider_net_descr_chain *chain = &card->tx_chain;
	struct spider_net_descr *descr;
	struct sk_buff *skb;
	u32 buf_addr;
	unsigned long flags;
	int status;

	while (chain->tail != chain->head) {
		spin_lock_irqsave(&chain->lock, flags);
		descr = chain->tail;

		status = spider_net_get_descr_status(descr);
		switch (status) {
		case SPIDER_NET_DESCR_COMPLETE:
			card->netdev_stats.tx_packets++;
			card->netdev_stats.tx_bytes += descr->skb->len;
			break;

		case SPIDER_NET_DESCR_CARDOWNED:
			if (!brutal) {
				spin_unlock_irqrestore(&chain->lock, flags);
				return 1;
			}

			/* fallthrough, if we release the descriptors
			 * brutally (then we don't care about
			 * SPIDER_NET_DESCR_CARDOWNED) */

		case SPIDER_NET_DESCR_RESPONSE_ERROR:
		case SPIDER_NET_DESCR_PROTECTION_ERROR:
		case SPIDER_NET_DESCR_FORCE_END:
			if (netif_msg_tx_err(card))
				pr_err("%s: forcing end of tx descriptor "
				       "with status x%02x\n",
				       card->netdev->name, status);
			card->netdev_stats.tx_errors++;
			break;

		default:
			card->netdev_stats.tx_dropped++;
			if (!brutal) {
				spin_unlock_irqrestore(&chain->lock, flags);
				return 1;
			}
		}

		chain->tail = descr->next;
		descr->dmac_cmd_status |= SPIDER_NET_DESCR_NOT_IN_USE;
		skb = descr->skb;
		buf_addr = descr->buf_addr;
		spin_unlock_irqrestore(&chain->lock, flags);

		/* unmap the skb */
		if (skb) {
			pci_unmap_single(card->pdev, buf_addr, skb->len,
					PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
		}
	}
	return 0;
}

/**
 * spider_net_kick_tx_dma - enables TX DMA processing
 * @card: card structure
 * @descr: descriptor address to enable TX processing at
 *
 * This routine will start the transmit DMA running if
 * it is not already running. This routine ned only be
 * called when queueing a new packet to an empty tx queue.
 * Writes the current tx chain head as start address
 * of the tx descriptor chain and enables the transmission
 * DMA engine.
 */
static inline void
spider_net_kick_tx_dma(struct spider_net_card *card)
{
	struct spider_net_descr *descr;

	if (spider_net_read_reg(card, SPIDER_NET_GDTDMACCNTR) &
			SPIDER_NET_TX_DMA_EN)
		goto out;

	descr = card->tx_chain.tail;
	for (;;) {
		if (spider_net_get_descr_status(descr) ==
				SPIDER_NET_DESCR_CARDOWNED) {
			spider_net_write_reg(card, SPIDER_NET_GDTDCHA,
					descr->bus_addr);
			spider_net_write_reg(card, SPIDER_NET_GDTDMACCNTR,
					SPIDER_NET_DMA_TX_VALUE);
			break;
		}
		if (descr == card->tx_chain.head)
			break;
		descr = descr->next;
	}

out:
	mod_timer(&card->tx_timer, jiffies + SPIDER_NET_TX_TIMER);
}

/**
 * spider_net_xmit - transmits a frame over the device
 * @skb: packet to send out
 * @netdev: interface device structure
 *
 * returns 0 on success, !0 on failure
 */
static int
spider_net_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	int cnt;
	struct spider_net_card *card = netdev_priv(netdev);
	struct spider_net_descr_chain *chain = &card->tx_chain;

	spider_net_release_tx_chain(card, 0);

	if ((chain->head->next == chain->tail->prev) ||
	   (spider_net_prepare_tx_descr(card, skb) != 0)) {

		card->netdev_stats.tx_dropped++;
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	cnt = spider_net_set_low_watermark(card);
	if (cnt < 5)
		spider_net_kick_tx_dma(card);
	return NETDEV_TX_OK;
}

/**
 * spider_net_cleanup_tx_ring - cleans up the TX ring
 * @card: card structure
 *
 * spider_net_cleanup_tx_ring is called by either the tx_timer
 * or from the NAPI polling routine.
 * This routine releases resources associted with transmitted
 * packets, including updating the queue tail pointer.
 */
static void
spider_net_cleanup_tx_ring(struct spider_net_card *card)
{
	if ((spider_net_release_tx_chain(card, 0) != 0) &&
	    (card->netdev->flags & IFF_UP)) {
		spider_net_kick_tx_dma(card);
		netif_wake_queue(card->netdev);
	}
}

/**
 * spider_net_do_ioctl - called for device ioctls
 * @netdev: interface device structure
 * @ifr: request parameter structure for ioctl
 * @cmd: command code for ioctl
 *
 * returns 0 on success, <0 on failure. Currently, we have no special ioctls.
 * -EOPNOTSUPP is returned, if an unknown ioctl was requested
 */
static int
spider_net_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * spider_net_pass_skb_up - takes an skb from a descriptor and passes it on
 * @descr: descriptor to process
 * @card: card structure
 * @napi: whether caller is in NAPI context
 *
 * returns 1 on success, 0 if no packet was passed to the stack
 *
 * iommu-unmaps the skb, fills out skb structure and passes the data to the
 * stack. The descriptor state is not changed.
 */
static int
spider_net_pass_skb_up(struct spider_net_descr *descr,
		       struct spider_net_card *card, int napi)
{
	struct sk_buff *skb;
	struct net_device *netdev;
	u32 data_status, data_error;

	data_status = descr->data_status;
	data_error = descr->data_error;

	netdev = card->netdev;

	/* unmap descriptor */
	pci_unmap_single(card->pdev, descr->buf_addr, SPIDER_NET_MAX_FRAME,
			PCI_DMA_FROMDEVICE);

	/* the cases we'll throw away the packet immediately */
	if (data_error & SPIDER_NET_DESTROY_RX_FLAGS) {
		if (netif_msg_rx_err(card))
			pr_err("error in received descriptor found, "
			       "data_status=x%08x, data_error=x%08x\n",
			       data_status, data_error);
		card->spider_stats.rx_desc_error++;
		return 0;
	}

	skb = descr->skb;
	skb->dev = netdev;
	skb_put(skb, descr->valid_size);

	/* the card seems to add 2 bytes of junk in front
	 * of the ethernet frame */
#define SPIDER_MISALIGN		2
	skb_pull(skb, SPIDER_MISALIGN);
	skb->protocol = eth_type_trans(skb, netdev);

	/* checksum offload */
	if (card->options.rx_csum) {
		if ( ( (data_status & SPIDER_NET_DATA_STATUS_CKSUM_MASK) ==
		       SPIDER_NET_DATA_STATUS_CKSUM_MASK) &&
		     !(data_error & SPIDER_NET_DATA_ERR_CKSUM_MASK))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	} else
		skb->ip_summed = CHECKSUM_NONE;

	if (data_status & SPIDER_NET_VLAN_PACKET) {
		/* further enhancements: HW-accel VLAN
		 * vlan_hwaccel_receive_skb
		 */
	}

	/* pass skb up to stack */
	if (napi)
		netif_receive_skb(skb);
	else
		netif_rx_ni(skb);

	/* update netdevice statistics */
	card->netdev_stats.rx_packets++;
	card->netdev_stats.rx_bytes += skb->len;

	return 1;
}

/**
 * spider_net_decode_one_descr - processes an rx descriptor
 * @card: card structure
 * @napi: whether caller is in NAPI context
 *
 * returns 1 if a packet has been sent to the stack, otherwise 0
 *
 * processes an rx descriptor by iommu-unmapping the data buffer and passing
 * the packet up to the stack. This function is called in softirq
 * context, e.g. either bottom half from interrupt or NAPI polling context
 */
static int
spider_net_decode_one_descr(struct spider_net_card *card, int napi)
{
	struct spider_net_descr_chain *chain = &card->rx_chain;
	struct spider_net_descr *descr = chain->tail;
	int status;
	int result;

	status = spider_net_get_descr_status(descr);

	if (status == SPIDER_NET_DESCR_CARDOWNED) {
		/* nothing in the descriptor yet */
		result=0;
		goto out;
	}

	if (status == SPIDER_NET_DESCR_NOT_IN_USE) {
		/* not initialized yet, the ring must be empty */
		spider_net_refill_rx_chain(card);
		spider_net_enable_rxdmac(card);
		result=0;
		goto out;
	}

	/* descriptor definitively used -- move on tail */
	chain->tail = descr->next;

	result = 0;
	if ( (status == SPIDER_NET_DESCR_RESPONSE_ERROR) ||
	     (status == SPIDER_NET_DESCR_PROTECTION_ERROR) ||
	     (status == SPIDER_NET_DESCR_FORCE_END) ) {
		if (netif_msg_rx_err(card))
			pr_err("%s: dropping RX descriptor with state %d\n",
			       card->netdev->name, status);
		card->netdev_stats.rx_dropped++;
		pci_unmap_single(card->pdev, descr->buf_addr,
				SPIDER_NET_MAX_FRAME, PCI_DMA_FROMDEVICE);
		dev_kfree_skb_irq(descr->skb);
		goto refill;
	}

	if ( (status != SPIDER_NET_DESCR_COMPLETE) &&
	     (status != SPIDER_NET_DESCR_FRAME_END) ) {
		if (netif_msg_rx_err(card)) {
			pr_err("%s: RX descriptor with state %d\n",
			       card->netdev->name, status);
			card->spider_stats.rx_desc_unk_state++;
		}
		goto refill;
	}

	/* ok, we've got a packet in descr */
	result = spider_net_pass_skb_up(descr, card, napi);
refill:
	descr->dmac_cmd_status = SPIDER_NET_DESCR_NOT_IN_USE;
	/* change the descriptor state: */
	if (!napi)
		spider_net_refill_rx_chain(card);
out:
	return result;
}

/**
 * spider_net_poll - NAPI poll function called by the stack to return packets
 * @netdev: interface device structure
 * @budget: number of packets we can pass to the stack at most
 *
 * returns 0 if no more packets available to the driver/stack. Returns 1,
 * if the quota is exceeded, but the driver has still packets.
 *
 * spider_net_poll returns all packets from the rx descriptors to the stack
 * (using netif_receive_skb). If all/enough packets are up, the driver
 * reenables interrupts and returns 0. If not, 1 is returned.
 */
static int
spider_net_poll(struct net_device *netdev, int *budget)
{
	struct spider_net_card *card = netdev_priv(netdev);
	int packets_to_do, packets_done = 0;
	int no_more_packets = 0;

	spider_net_cleanup_tx_ring(card);
	packets_to_do = min(*budget, netdev->quota);

	while (packets_to_do) {
		if (spider_net_decode_one_descr(card, 1)) {
			packets_done++;
			packets_to_do--;
		} else {
			/* no more packets for the stack */
			no_more_packets = 1;
			break;
		}
	}

	netdev->quota -= packets_done;
	*budget -= packets_done;
	spider_net_refill_rx_chain(card);

	/* if all packets are in the stack, enable interrupts and return 0 */
	/* if not, return 1 */
	if (no_more_packets) {
		netif_rx_complete(netdev);
		spider_net_rx_irq_on(card);
		return 0;
	}

	return 1;
}

/**
 * spider_net_vlan_rx_reg - initializes VLAN structures in the driver and card
 * @netdev: interface device structure
 * @grp: vlan_group structure that is registered (NULL on destroying interface)
 */
static void
spider_net_vlan_rx_reg(struct net_device *netdev, struct vlan_group *grp)
{
	/* further enhancement... yet to do */
	return;
}

/**
 * spider_net_vlan_rx_add - adds VLAN id to the card filter
 * @netdev: interface device structure
 * @vid: VLAN id to add
 */
static void
spider_net_vlan_rx_add(struct net_device *netdev, uint16_t vid)
{
	/* further enhancement... yet to do */
	/* add vid to card's VLAN filter table */
	return;
}

/**
 * spider_net_vlan_rx_kill - removes VLAN id to the card filter
 * @netdev: interface device structure
 * @vid: VLAN id to remove
 */
static void
spider_net_vlan_rx_kill(struct net_device *netdev, uint16_t vid)
{
	/* further enhancement... yet to do */
	/* remove vid from card's VLAN filter table */
}

/**
 * spider_net_get_stats - get interface statistics
 * @netdev: interface device structure
 *
 * returns the interface statistics residing in the spider_net_card struct
 */
static struct net_device_stats *
spider_net_get_stats(struct net_device *netdev)
{
	struct spider_net_card *card = netdev_priv(netdev);
	struct net_device_stats *stats = &card->netdev_stats;
	return stats;
}

/**
 * spider_net_change_mtu - changes the MTU of an interface
 * @netdev: interface device structure
 * @new_mtu: new MTU value
 *
 * returns 0 on success, <0 on failure
 */
static int
spider_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	/* no need to re-alloc skbs or so -- the max mtu is about 2.3k
	 * and mtu is outbound only anyway */
	if ( (new_mtu < SPIDER_NET_MIN_MTU ) ||
		(new_mtu > SPIDER_NET_MAX_MTU) )
		return -EINVAL;
	netdev->mtu = new_mtu;
	return 0;
}

/**
 * spider_net_set_mac - sets the MAC of an interface
 * @netdev: interface device structure
 * @ptr: pointer to new MAC address
 *
 * Returns 0 on success, <0 on failure. Currently, we don't support this
 * and will always return EOPNOTSUPP.
 */
static int
spider_net_set_mac(struct net_device *netdev, void *p)
{
	struct spider_net_card *card = netdev_priv(netdev);
	u32 macl, macu, regvalue;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* switch off GMACTPE and GMACRPE */
	regvalue = spider_net_read_reg(card, SPIDER_NET_GMACOPEMD);
	regvalue &= ~((1 << 5) | (1 << 6));
	spider_net_write_reg(card, SPIDER_NET_GMACOPEMD, regvalue);

	/* write mac */
	macu = (addr->sa_data[0]<<24) + (addr->sa_data[1]<<16) +
		(addr->sa_data[2]<<8) + (addr->sa_data[3]);
	macl = (addr->sa_data[4]<<8) + (addr->sa_data[5]);
	spider_net_write_reg(card, SPIDER_NET_GMACUNIMACU, macu);
	spider_net_write_reg(card, SPIDER_NET_GMACUNIMACL, macl);

	/* switch GMACTPE and GMACRPE back on */
	regvalue = spider_net_read_reg(card, SPIDER_NET_GMACOPEMD);
	regvalue |= ((1 << 5) | (1 << 6));
	spider_net_write_reg(card, SPIDER_NET_GMACOPEMD, regvalue);

	spider_net_set_promisc(card);

	/* look up, whether we have been successful */
	if (spider_net_get_mac_address(netdev))
		return -EADDRNOTAVAIL;
	if (memcmp(netdev->dev_addr,addr->sa_data,netdev->addr_len))
		return -EADDRNOTAVAIL;

	return 0;
}

/**
 * spider_net_handle_rxram_full - cleans up RX ring upon RX RAM full interrupt
 * @card: card structure
 *
 * spider_net_handle_rxram_full empties the RX ring so that spider can put
 * more packets in it and empty its RX RAM. This is called in bottom half
 * context
 */
static void
spider_net_handle_rxram_full(struct spider_net_card *card)
{
	while (spider_net_decode_one_descr(card, 0))
		;
	spider_net_enable_rxchtails(card);
	spider_net_enable_rxdmac(card);
	netif_rx_schedule(card->netdev);
}

/**
 * spider_net_handle_error_irq - handles errors raised by an interrupt
 * @card: card structure
 * @status_reg: interrupt status register 0 (GHIINT0STS)
 *
 * spider_net_handle_error_irq treats or ignores all error conditions
 * found when an interrupt is presented
 */
static void
spider_net_handle_error_irq(struct spider_net_card *card, u32 status_reg)
{
	u32 error_reg1, error_reg2;
	u32 i;
	int show_error = 1;

	error_reg1 = spider_net_read_reg(card, SPIDER_NET_GHIINT1STS);
	error_reg2 = spider_net_read_reg(card, SPIDER_NET_GHIINT2STS);

	/* check GHIINT0STS ************************************/
	if (status_reg)
		for (i = 0; i < 32; i++)
			if (status_reg & (1<<i))
				switch (i)
	{
	/* let error_reg1 and error_reg2 evaluation decide, what to do
	case SPIDER_NET_PHYINT:
	case SPIDER_NET_GMAC2INT:
	case SPIDER_NET_GMAC1INT:
	case SPIDER_NET_GFIFOINT:
	case SPIDER_NET_DMACINT:
	case SPIDER_NET_GSYSINT:
		break; */

	case SPIDER_NET_GIPSINT:
		show_error = 0;
		break;

	case SPIDER_NET_GPWOPCMPINT:
		/* PHY write operation completed */
		show_error = 0;
		break;
	case SPIDER_NET_GPROPCMPINT:
		/* PHY read operation completed */
		/* we don't use semaphores, as we poll for the completion
		 * of the read operation in spider_net_read_phy. Should take
		 * about 50 us */
		show_error = 0;
		break;
	case SPIDER_NET_GPWFFINT:
		/* PHY command queue full */
		if (netif_msg_intr(card))
			pr_err("PHY write queue full\n");
		show_error = 0;
		break;

	/* case SPIDER_NET_GRMDADRINT: not used. print a message */
	/* case SPIDER_NET_GRMARPINT: not used. print a message */
	/* case SPIDER_NET_GRMMPINT: not used. print a message */

	case SPIDER_NET_GDTDEN0INT:
		/* someone has set TX_DMA_EN to 0 */
		show_error = 0;
		break;

	case SPIDER_NET_GDDDEN0INT: /* fallthrough */
	case SPIDER_NET_GDCDEN0INT: /* fallthrough */
	case SPIDER_NET_GDBDEN0INT: /* fallthrough */
	case SPIDER_NET_GDADEN0INT:
		/* someone has set RX_DMA_EN to 0 */
		show_error = 0;
		break;

	/* RX interrupts */
	case SPIDER_NET_GDDFDCINT:
	case SPIDER_NET_GDCFDCINT:
	case SPIDER_NET_GDBFDCINT:
	case SPIDER_NET_GDAFDCINT:
	/* case SPIDER_NET_GDNMINT: not used. print a message */
	/* case SPIDER_NET_GCNMINT: not used. print a message */
	/* case SPIDER_NET_GBNMINT: not used. print a message */
	/* case SPIDER_NET_GANMINT: not used. print a message */
	/* case SPIDER_NET_GRFNMINT: not used. print a message */
		show_error = 0;
		break;

	/* TX interrupts */
	case SPIDER_NET_GDTFDCINT:
		show_error = 0;
		break;
	case SPIDER_NET_GTTEDINT:
		show_error = 0;
		break;
	case SPIDER_NET_GDTDCEINT:
		/* chain end. If a descriptor should be sent, kick off
		 * tx dma
		if (card->tx_chain.tail != card->tx_chain.head)
			spider_net_kick_tx_dma(card);
		*/
		show_error = 0;
		break;

	/* case SPIDER_NET_G1TMCNTINT: not used. print a message */
	/* case SPIDER_NET_GFREECNTINT: not used. print a message */
	}

	/* check GHIINT1STS ************************************/
	if (error_reg1)
		for (i = 0; i < 32; i++)
			if (error_reg1 & (1<<i))
				switch (i)
	{
	case SPIDER_NET_GTMFLLINT:
		if (netif_msg_intr(card) && net_ratelimit())
			pr_err("Spider TX RAM full\n");
		show_error = 0;
		break;
	case SPIDER_NET_GRFDFLLINT: /* fallthrough */
	case SPIDER_NET_GRFCFLLINT: /* fallthrough */
	case SPIDER_NET_GRFBFLLINT: /* fallthrough */
	case SPIDER_NET_GRFAFLLINT: /* fallthrough */
	case SPIDER_NET_GRMFLLINT:
		if (netif_msg_intr(card) && net_ratelimit())
			pr_debug("Spider RX RAM full, incoming packets "
			       "might be discarded!\n");
		spider_net_rx_irq_off(card);
		tasklet_schedule(&card->rxram_full_tl);
		show_error = 0;
		break;

	/* case SPIDER_NET_GTMSHTINT: problem, print a message */
	case SPIDER_NET_GDTINVDINT:
		/* allrighty. tx from previous descr ok */
		show_error = 0;
		break;

	/* chain end */
	case SPIDER_NET_GDDDCEINT: /* fallthrough */
	case SPIDER_NET_GDCDCEINT: /* fallthrough */
	case SPIDER_NET_GDBDCEINT: /* fallthrough */
	case SPIDER_NET_GDADCEINT:
		if (netif_msg_intr(card))
			pr_err("got descriptor chain end interrupt, "
			       "restarting DMAC %c.\n",
			       'D'-(i-SPIDER_NET_GDDDCEINT)/3);
		spider_net_refill_rx_chain(card);
		spider_net_enable_rxdmac(card);
		show_error = 0;
		break;

	/* invalid descriptor */
	case SPIDER_NET_GDDINVDINT: /* fallthrough */
	case SPIDER_NET_GDCINVDINT: /* fallthrough */
	case SPIDER_NET_GDBINVDINT: /* fallthrough */
	case SPIDER_NET_GDAINVDINT:
		/* could happen when rx chain is full */
		spider_net_refill_rx_chain(card);
		spider_net_enable_rxdmac(card);
		show_error = 0;
		break;

	/* case SPIDER_NET_GDTRSERINT: problem, print a message */
	/* case SPIDER_NET_GDDRSERINT: problem, print a message */
	/* case SPIDER_NET_GDCRSERINT: problem, print a message */
	/* case SPIDER_NET_GDBRSERINT: problem, print a message */
	/* case SPIDER_NET_GDARSERINT: problem, print a message */
	/* case SPIDER_NET_GDSERINT: problem, print a message */
	/* case SPIDER_NET_GDTPTERINT: problem, print a message */
	/* case SPIDER_NET_GDDPTERINT: problem, print a message */
	/* case SPIDER_NET_GDCPTERINT: problem, print a message */
	/* case SPIDER_NET_GDBPTERINT: problem, print a message */
	/* case SPIDER_NET_GDAPTERINT: problem, print a message */
	default:
		show_error = 1;
		break;
	}

	/* check GHIINT2STS ************************************/
	if (error_reg2)
		for (i = 0; i < 32; i++)
			if (error_reg2 & (1<<i))
				switch (i)
	{
	/* there is nothing we can (want  to) do at this time. Log a
	 * message, we can switch on and off the specific values later on
	case SPIDER_NET_GPROPERINT:
	case SPIDER_NET_GMCTCRSNGINT:
	case SPIDER_NET_GMCTLCOLINT:
	case SPIDER_NET_GMCTTMOTINT:
	case SPIDER_NET_GMCRCAERINT:
	case SPIDER_NET_GMCRCALERINT:
	case SPIDER_NET_GMCRALNERINT:
	case SPIDER_NET_GMCROVRINT:
	case SPIDER_NET_GMCRRNTINT:
	case SPIDER_NET_GMCRRXERINT:
	case SPIDER_NET_GTITCSERINT:
	case SPIDER_NET_GTIFMTERINT:
	case SPIDER_NET_GTIPKTRVKINT:
	case SPIDER_NET_GTISPINGINT:
	case SPIDER_NET_GTISADNGINT:
	case SPIDER_NET_GTISPDNGINT:
	case SPIDER_NET_GRIFMTERINT:
	case SPIDER_NET_GRIPKTRVKINT:
	case SPIDER_NET_GRISPINGINT:
	case SPIDER_NET_GRISADNGINT:
	case SPIDER_NET_GRISPDNGINT:
		break;
	*/
		default:
			break;
	}

	if ((show_error) && (netif_msg_intr(card)))
		pr_err("Got error interrupt on %s, GHIINT0STS = 0x%08x, "
		       "GHIINT1STS = 0x%08x, GHIINT2STS = 0x%08x\n",
		       card->netdev->name,
		       status_reg, error_reg1, error_reg2);

	/* clear interrupt sources */
	spider_net_write_reg(card, SPIDER_NET_GHIINT1STS, error_reg1);
	spider_net_write_reg(card, SPIDER_NET_GHIINT2STS, error_reg2);
}

/**
 * spider_net_interrupt - interrupt handler for spider_net
 * @irq: interupt number
 * @ptr: pointer to net_device
 * @regs: PU registers
 *
 * returns IRQ_HANDLED, if interrupt was for driver, or IRQ_NONE, if no
 * interrupt found raised by card.
 *
 * This is the interrupt handler, that turns off
 * interrupts for this device and makes the stack poll the driver
 */
static irqreturn_t
spider_net_interrupt(int irq, void *ptr)
{
	struct net_device *netdev = ptr;
	struct spider_net_card *card = netdev_priv(netdev);
	u32 status_reg;

	status_reg = spider_net_read_reg(card, SPIDER_NET_GHIINT0STS);

	if (!status_reg)
		return IRQ_NONE;

	if (status_reg & SPIDER_NET_RXINT ) {
		spider_net_rx_irq_off(card);
		netif_rx_schedule(netdev);
	}
	if (status_reg & SPIDER_NET_TXINT)
		netif_rx_schedule(netdev);

	if (status_reg & SPIDER_NET_ERRINT )
		spider_net_handle_error_irq(card, status_reg);

	/* clear interrupt sources */
	spider_net_write_reg(card, SPIDER_NET_GHIINT0STS, status_reg);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * spider_net_poll_controller - artificial interrupt for netconsole etc.
 * @netdev: interface device structure
 *
 * see Documentation/networking/netconsole.txt
 */
static void
spider_net_poll_controller(struct net_device *netdev)
{
	disable_irq(netdev->irq);
	spider_net_interrupt(netdev->irq, netdev);
	enable_irq(netdev->irq);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

/**
 * spider_net_init_card - initializes the card
 * @card: card structure
 *
 * spider_net_init_card initializes the card so that other registers can
 * be used
 */
static void
spider_net_init_card(struct spider_net_card *card)
{
	spider_net_write_reg(card, SPIDER_NET_CKRCTRL,
			     SPIDER_NET_CKRCTRL_STOP_VALUE);

	spider_net_write_reg(card, SPIDER_NET_CKRCTRL,
			     SPIDER_NET_CKRCTRL_RUN_VALUE);
}

/**
 * spider_net_enable_card - enables the card by setting all kinds of regs
 * @card: card structure
 *
 * spider_net_enable_card sets a lot of SMMIO registers to enable the device
 */
static void
spider_net_enable_card(struct spider_net_card *card)
{
	int i;
	/* the following array consists of (register),(value) pairs
	 * that are set in this function. A register of 0 ends the list */
	u32 regs[][2] = {
		{ SPIDER_NET_GRESUMINTNUM, 0 },
		{ SPIDER_NET_GREINTNUM, 0 },

		/* set interrupt frame number registers */
		/* clear the single DMA engine registers first */
		{ SPIDER_NET_GFAFRMNUM, SPIDER_NET_GFXFRAMES_VALUE },
		{ SPIDER_NET_GFBFRMNUM, SPIDER_NET_GFXFRAMES_VALUE },
		{ SPIDER_NET_GFCFRMNUM, SPIDER_NET_GFXFRAMES_VALUE },
		{ SPIDER_NET_GFDFRMNUM, SPIDER_NET_GFXFRAMES_VALUE },
		/* then set, what we really need */
		{ SPIDER_NET_GFFRMNUM, SPIDER_NET_FRAMENUM_VALUE },

		/* timer counter registers and stuff */
		{ SPIDER_NET_GFREECNNUM, 0 },
		{ SPIDER_NET_GONETIMENUM, 0 },
		{ SPIDER_NET_GTOUTFRMNUM, 0 },

		/* RX mode setting */
		{ SPIDER_NET_GRXMDSET, SPIDER_NET_RXMODE_VALUE },
		/* TX mode setting */
		{ SPIDER_NET_GTXMDSET, SPIDER_NET_TXMODE_VALUE },
		/* IPSEC mode setting */
		{ SPIDER_NET_GIPSECINIT, SPIDER_NET_IPSECINIT_VALUE },

		{ SPIDER_NET_GFTRESTRT, SPIDER_NET_RESTART_VALUE },

		{ SPIDER_NET_GMRWOLCTRL, 0 },
		{ SPIDER_NET_GTESTMD, 0x10000000 },
		{ SPIDER_NET_GTTQMSK, 0x00400040 },

		{ SPIDER_NET_GMACINTEN, 0 },

		/* flow control stuff */
		{ SPIDER_NET_GMACAPAUSE, SPIDER_NET_MACAPAUSE_VALUE },
		{ SPIDER_NET_GMACTXPAUSE, SPIDER_NET_TXPAUSE_VALUE },

		{ SPIDER_NET_GMACBSTLMT, SPIDER_NET_BURSTLMT_VALUE },
		{ 0, 0}
	};

	i = 0;
	while (regs[i][0]) {
		spider_net_write_reg(card, regs[i][0], regs[i][1]);
		i++;
	}

	/* clear unicast filter table entries 1 to 14 */
	for (i = 1; i <= 14; i++) {
		spider_net_write_reg(card,
				     SPIDER_NET_GMRUAFILnR + i * 8,
				     0x00080000);
		spider_net_write_reg(card,
				     SPIDER_NET_GMRUAFILnR + i * 8 + 4,
				     0x00000000);
	}

	spider_net_write_reg(card, SPIDER_NET_GMRUA0FIL15R, 0x08080000);

	spider_net_write_reg(card, SPIDER_NET_ECMODE, SPIDER_NET_ECMODE_VALUE);

	/* set chain tail adress for RX chains and
	 * enable DMA */
	spider_net_enable_rxchtails(card);
	spider_net_enable_rxdmac(card);

	spider_net_write_reg(card, SPIDER_NET_GRXDMAEN, SPIDER_NET_WOL_VALUE);

	spider_net_write_reg(card, SPIDER_NET_GMACLENLMT,
			     SPIDER_NET_LENLMT_VALUE);
	spider_net_write_reg(card, SPIDER_NET_GMACMODE,
			     SPIDER_NET_MACMODE_VALUE);
	spider_net_write_reg(card, SPIDER_NET_GMACOPEMD,
			     SPIDER_NET_OPMODE_VALUE);

	/* set interrupt mask registers */
	spider_net_write_reg(card, SPIDER_NET_GHIINT0MSK,
			     SPIDER_NET_INT0_MASK_VALUE);
	spider_net_write_reg(card, SPIDER_NET_GHIINT1MSK,
			     SPIDER_NET_INT1_MASK_VALUE);
	spider_net_write_reg(card, SPIDER_NET_GHIINT2MSK,
			     SPIDER_NET_INT2_MASK_VALUE);

	spider_net_write_reg(card, SPIDER_NET_GDTDMACCNTR,
			     SPIDER_NET_GDTBSTA);
}

/**
 * spider_net_open - called upon ifonfig up
 * @netdev: interface device structure
 *
 * returns 0 on success, <0 on failure
 *
 * spider_net_open allocates all the descriptors and memory needed for
 * operation, sets up multicast list and enables interrupts
 */
int
spider_net_open(struct net_device *netdev)
{
	struct spider_net_card *card = netdev_priv(netdev);
	struct spider_net_descr *descr;
	int i, result;

	result = -ENOMEM;
	if (spider_net_init_chain(card, &card->tx_chain, card->descr,
	                          card->num_tx_desc))
		goto alloc_tx_failed;

	card->low_watermark = NULL;

	/* rx_chain is after tx_chain, so offset is descr + tx_count */
	if (spider_net_init_chain(card, &card->rx_chain,
	                          card->descr + card->num_tx_desc,
	                          card->num_rx_desc))
		goto alloc_rx_failed;

	descr = card->rx_chain.head;
	for (i=0; i < card->num_rx_desc; i++, descr++)
		descr->next_descr_addr = descr->next->bus_addr;

	/* allocate rx skbs */
	if (spider_net_alloc_rx_skbs(card))
		goto alloc_skbs_failed;

	spider_net_set_multi(netdev);

	/* further enhancement: setup hw vlan, if needed */

	result = -EBUSY;
	if (request_irq(netdev->irq, spider_net_interrupt,
			     IRQF_SHARED, netdev->name, netdev))
		goto register_int_failed;

	spider_net_enable_card(card);

	netif_start_queue(netdev);
	netif_carrier_on(netdev);
	netif_poll_enable(netdev);

	return 0;

register_int_failed:
	spider_net_free_rx_chain_contents(card);
alloc_skbs_failed:
	spider_net_free_chain(card, &card->rx_chain);
alloc_rx_failed:
	spider_net_free_chain(card, &card->tx_chain);
alloc_tx_failed:
	return result;
}

/**
 * spider_net_setup_phy - setup PHY
 * @card: card structure
 *
 * returns 0 on success, <0 on failure
 *
 * spider_net_setup_phy is used as part of spider_net_probe. Sets
 * the PHY to 1000 Mbps
 **/
static int
spider_net_setup_phy(struct spider_net_card *card)
{
	struct mii_phy *phy = &card->phy;

	spider_net_write_reg(card, SPIDER_NET_GDTDMASEL,
			     SPIDER_NET_DMASEL_VALUE);
	spider_net_write_reg(card, SPIDER_NET_GPCCTRL,
			     SPIDER_NET_PHY_CTRL_VALUE);
	phy->mii_id = 1;
	phy->dev = card->netdev;
	phy->mdio_read = spider_net_read_phy;
	phy->mdio_write = spider_net_write_phy;

	mii_phy_probe(phy, phy->mii_id);

	if (phy->def->ops->setup_forced)
		phy->def->ops->setup_forced(phy, SPEED_1000, DUPLEX_FULL);

	phy->def->ops->enable_fiber(phy);

	phy->def->ops->read_link(phy);
	pr_info("Found %s with %i Mbps, %s-duplex.\n", phy->def->name,
		phy->speed, phy->duplex==1 ? "Full" : "Half");

	return 0;
}

/**
 * spider_net_download_firmware - loads firmware into the adapter
 * @card: card structure
 * @firmware_ptr: pointer to firmware data
 *
 * spider_net_download_firmware loads the firmware data into the
 * adapter. It assumes the length etc. to be allright.
 */
static int
spider_net_download_firmware(struct spider_net_card *card,
			     const void *firmware_ptr)
{
	int sequencer, i;
	const u32 *fw_ptr = firmware_ptr;

	/* stop sequencers */
	spider_net_write_reg(card, SPIDER_NET_GSINIT,
			     SPIDER_NET_STOP_SEQ_VALUE);

	for (sequencer = 0; sequencer < SPIDER_NET_FIRMWARE_SEQS;
	     sequencer++) {
		spider_net_write_reg(card,
				     SPIDER_NET_GSnPRGADR + sequencer * 8, 0);
		for (i = 0; i < SPIDER_NET_FIRMWARE_SEQWORDS; i++) {
			spider_net_write_reg(card, SPIDER_NET_GSnPRGDAT +
					     sequencer * 8, *fw_ptr);
			fw_ptr++;
		}
	}

	if (spider_net_read_reg(card, SPIDER_NET_GSINIT))
		return -EIO;

	spider_net_write_reg(card, SPIDER_NET_GSINIT,
			     SPIDER_NET_RUN_SEQ_VALUE);

	return 0;
}

/**
 * spider_net_init_firmware - reads in firmware parts
 * @card: card structure
 *
 * Returns 0 on success, <0 on failure
 *
 * spider_net_init_firmware opens the sequencer firmware and does some basic
 * checks. This function opens and releases the firmware structure. A call
 * to download the firmware is performed before the release.
 *
 * Firmware format
 * ===============
 * spider_fw.bin is expected to be a file containing 6*1024*4 bytes, 4k being
 * the program for each sequencer. Use the command
 *    tail -q -n +2 Seq_code1_0x088.txt Seq_code2_0x090.txt              \
 *         Seq_code3_0x098.txt Seq_code4_0x0A0.txt Seq_code5_0x0A8.txt   \
 *         Seq_code6_0x0B0.txt | xxd -r -p -c4 > spider_fw.bin
 *
 * to generate spider_fw.bin, if you have sequencer programs with something
 * like the following contents for each sequencer:
 *    <ONE LINE COMMENT>
 *    <FIRST 4-BYTES-WORD FOR SEQUENCER>
 *    <SECOND 4-BYTES-WORD FOR SEQUENCER>
 *     ...
 *    <1024th 4-BYTES-WORD FOR SEQUENCER>
 */
static int
spider_net_init_firmware(struct spider_net_card *card)
{
	struct firmware *firmware = NULL;
	struct device_node *dn;
	const u8 *fw_prop = NULL;
	int err = -ENOENT;
	int fw_size;

	if (request_firmware((const struct firmware **)&firmware,
			     SPIDER_NET_FIRMWARE_NAME, &card->pdev->dev) == 0) {
		if ( (firmware->size != SPIDER_NET_FIRMWARE_LEN) &&
		     netif_msg_probe(card) ) {
			pr_err("Incorrect size of spidernet firmware in " \
			       "filesystem. Looking in host firmware...\n");
			goto try_host_fw;
		}
		err = spider_net_download_firmware(card, firmware->data);

		release_firmware(firmware);
		if (err)
			goto try_host_fw;

		goto done;
	}

try_host_fw:
	dn = pci_device_to_OF_node(card->pdev);
	if (!dn)
		goto out_err;

	fw_prop = get_property(dn, "firmware", &fw_size);
	if (!fw_prop)
		goto out_err;

	if ( (fw_size != SPIDER_NET_FIRMWARE_LEN) &&
	     netif_msg_probe(card) ) {
		pr_err("Incorrect size of spidernet firmware in " \
		       "host firmware\n");
		goto done;
	}

	err = spider_net_download_firmware(card, fw_prop);

done:
	return err;
out_err:
	if (netif_msg_probe(card))
		pr_err("Couldn't find spidernet firmware in filesystem " \
		       "or host firmware\n");
	return err;
}

/**
 * spider_net_workaround_rxramfull - work around firmware bug
 * @card: card structure
 *
 * no return value
 **/
static void
spider_net_workaround_rxramfull(struct spider_net_card *card)
{
	int i, sequencer = 0;

	/* cancel reset */
	spider_net_write_reg(card, SPIDER_NET_CKRCTRL,
			     SPIDER_NET_CKRCTRL_RUN_VALUE);

	/* empty sequencer data */
	for (sequencer = 0; sequencer < SPIDER_NET_FIRMWARE_SEQS;
	     sequencer++) {
		spider_net_write_reg(card, SPIDER_NET_GSnPRGADR +
				     sequencer * 8, 0x0);
		for (i = 0; i < SPIDER_NET_FIRMWARE_SEQWORDS; i++) {
			spider_net_write_reg(card, SPIDER_NET_GSnPRGDAT +
					     sequencer * 8, 0x0);
		}
	}

	/* set sequencer operation */
	spider_net_write_reg(card, SPIDER_NET_GSINIT, 0x000000fe);

	/* reset */
	spider_net_write_reg(card, SPIDER_NET_CKRCTRL,
			     SPIDER_NET_CKRCTRL_STOP_VALUE);
}

/**
 * spider_net_stop - called upon ifconfig down
 * @netdev: interface device structure
 *
 * always returns 0
 */
int
spider_net_stop(struct net_device *netdev)
{
	struct spider_net_card *card = netdev_priv(netdev);

	tasklet_kill(&card->rxram_full_tl);
	netif_poll_disable(netdev);
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);
	del_timer_sync(&card->tx_timer);

	/* disable/mask all interrupts */
	spider_net_write_reg(card, SPIDER_NET_GHIINT0MSK, 0);
	spider_net_write_reg(card, SPIDER_NET_GHIINT1MSK, 0);
	spider_net_write_reg(card, SPIDER_NET_GHIINT2MSK, 0);

	/* free_irq(netdev->irq, netdev);*/
	free_irq(to_pci_dev(netdev->class_dev.dev)->irq, netdev);

	spider_net_write_reg(card, SPIDER_NET_GDTDMACCNTR,
			     SPIDER_NET_DMA_TX_FEND_VALUE);

	/* turn off DMA, force end */
	spider_net_disable_rxdmac(card);

	/* release chains */
	spider_net_release_tx_chain(card, 1);

	spider_net_free_chain(card, &card->tx_chain);
	spider_net_free_chain(card, &card->rx_chain);

	return 0;
}

/**
 * spider_net_tx_timeout_task - task scheduled by the watchdog timeout
 * function (to be called not under interrupt status)
 * @data: data, is interface device structure
 *
 * called as task when tx hangs, resets interface (if interface is up)
 */
static void
spider_net_tx_timeout_task(void *data)
{
	struct net_device *netdev = data;
	struct spider_net_card *card = netdev_priv(netdev);

	if (!(netdev->flags & IFF_UP))
		goto out;

	netif_device_detach(netdev);
	spider_net_stop(netdev);

	spider_net_workaround_rxramfull(card);
	spider_net_init_card(card);

	if (spider_net_setup_phy(card))
		goto out;
	if (spider_net_init_firmware(card))
		goto out;

	spider_net_open(netdev);
	spider_net_kick_tx_dma(card);
	netif_device_attach(netdev);

out:
	atomic_dec(&card->tx_timeout_task_counter);
}

/**
 * spider_net_tx_timeout - called when the tx timeout watchdog kicks in.
 * @netdev: interface device structure
 *
 * called, if tx hangs. Schedules a task that resets the interface
 */
static void
spider_net_tx_timeout(struct net_device *netdev)
{
	struct spider_net_card *card;

	card = netdev_priv(netdev);
	atomic_inc(&card->tx_timeout_task_counter);
	if (netdev->flags & IFF_UP)
		schedule_work(&card->tx_timeout_task);
	else
		atomic_dec(&card->tx_timeout_task_counter);
	card->spider_stats.tx_timeouts++;
}

/**
 * spider_net_setup_netdev_ops - initialization of net_device operations
 * @netdev: net_device structure
 *
 * fills out function pointers in the net_device structure
 */
static void
spider_net_setup_netdev_ops(struct net_device *netdev)
{
	netdev->open = &spider_net_open;
	netdev->stop = &spider_net_stop;
	netdev->hard_start_xmit = &spider_net_xmit;
	netdev->get_stats = &spider_net_get_stats;
	netdev->set_multicast_list = &spider_net_set_multi;
	netdev->set_mac_address = &spider_net_set_mac;
	netdev->change_mtu = &spider_net_change_mtu;
	netdev->do_ioctl = &spider_net_do_ioctl;
	/* tx watchdog */
	netdev->tx_timeout = &spider_net_tx_timeout;
	netdev->watchdog_timeo = SPIDER_NET_WATCHDOG_TIMEOUT;
	/* NAPI */
	netdev->poll = &spider_net_poll;
	netdev->weight = SPIDER_NET_NAPI_WEIGHT;
	/* HW VLAN */
	netdev->vlan_rx_register = &spider_net_vlan_rx_reg;
	netdev->vlan_rx_add_vid = &spider_net_vlan_rx_add;
	netdev->vlan_rx_kill_vid = &spider_net_vlan_rx_kill;
#ifdef CONFIG_NET_POLL_CONTROLLER
	/* poll controller */
	netdev->poll_controller = &spider_net_poll_controller;
#endif /* CONFIG_NET_POLL_CONTROLLER */
	/* ethtool ops */
	netdev->ethtool_ops = &spider_net_ethtool_ops;
}

/**
 * spider_net_setup_netdev - initialization of net_device
 * @card: card structure
 *
 * Returns 0 on success or <0 on failure
 *
 * spider_net_setup_netdev initializes the net_device structure
 **/
static int
spider_net_setup_netdev(struct spider_net_card *card)
{
	int result;
	struct net_device *netdev = card->netdev;
	struct device_node *dn;
	struct sockaddr addr;
	const u8 *mac;

	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &card->pdev->dev);

	pci_set_drvdata(card->pdev, netdev);

	card->rxram_full_tl.data = (unsigned long) card;
	card->rxram_full_tl.func =
		(void (*)(unsigned long)) spider_net_handle_rxram_full;
	init_timer(&card->tx_timer);
	card->tx_timer.function =
		(void (*)(unsigned long)) spider_net_cleanup_tx_ring;
	card->tx_timer.data = (unsigned long) card;
	netdev->irq = card->pdev->irq;

	card->options.rx_csum = SPIDER_NET_RX_CSUM_DEFAULT;

	card->num_tx_desc = tx_descriptors;
	card->num_rx_desc = rx_descriptors;

	spider_net_setup_netdev_ops(netdev);

	netdev->features = NETIF_F_HW_CSUM | NETIF_F_LLTX;
	/* some time: NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX |
	 *		NETIF_F_HW_VLAN_FILTER */

	netdev->irq = card->pdev->irq;

	dn = pci_device_to_OF_node(card->pdev);
	if (!dn)
		return -EIO;

	mac = get_property(dn, "local-mac-address", NULL);
	if (!mac)
		return -EIO;
	memcpy(addr.sa_data, mac, ETH_ALEN);

	result = spider_net_set_mac(netdev, &addr);
	if ((result) && (netif_msg_probe(card)))
		pr_err("Failed to set MAC address: %i\n", result);

	result = register_netdev(netdev);
	if (result) {
		if (netif_msg_probe(card))
			pr_err("Couldn't register net_device: %i\n",
				  result);
		return result;
	}

	if (netif_msg_probe(card))
		pr_info("Initialized device %s.\n", netdev->name);

	return 0;
}

/**
 * spider_net_alloc_card - allocates net_device and card structure
 *
 * returns the card structure or NULL in case of errors
 *
 * the card and net_device structures are linked to each other
 */
static struct spider_net_card *
spider_net_alloc_card(void)
{
	struct net_device *netdev;
	struct spider_net_card *card;
	size_t alloc_size;

	alloc_size = sizeof (*card) +
		sizeof (struct spider_net_descr) * rx_descriptors +
		sizeof (struct spider_net_descr) * tx_descriptors;
	netdev = alloc_etherdev(alloc_size);
	if (!netdev)
		return NULL;

	card = netdev_priv(netdev);
	card->netdev = netdev;
	card->msg_enable = SPIDER_NET_DEFAULT_MSG;
	INIT_WORK(&card->tx_timeout_task, spider_net_tx_timeout_task, netdev);
	init_waitqueue_head(&card->waitq);
	atomic_set(&card->tx_timeout_task_counter, 0);

	return card;
}

/**
 * spider_net_undo_pci_setup - releases PCI ressources
 * @card: card structure
 *
 * spider_net_undo_pci_setup releases the mapped regions
 */
static void
spider_net_undo_pci_setup(struct spider_net_card *card)
{
	iounmap(card->regs);
	pci_release_regions(card->pdev);
}

/**
 * spider_net_setup_pci_dev - sets up the device in terms of PCI operations
 * @card: card structure
 * @pdev: PCI device
 *
 * Returns the card structure or NULL if any errors occur
 *
 * spider_net_setup_pci_dev initializes pdev and together with the
 * functions called in spider_net_open configures the device so that
 * data can be transferred over it
 * The net_device structure is attached to the card structure, if the
 * function returns without error.
 **/
static struct spider_net_card *
spider_net_setup_pci_dev(struct pci_dev *pdev)
{
	struct spider_net_card *card;
	unsigned long mmio_start, mmio_len;

	if (pci_enable_device(pdev)) {
		pr_err("Couldn't enable PCI device\n");
		return NULL;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		pr_err("Couldn't find proper PCI device base address.\n");
		goto out_disable_dev;
	}

	if (pci_request_regions(pdev, spider_net_driver_name)) {
		pr_err("Couldn't obtain PCI resources, aborting.\n");
		goto out_disable_dev;
	}

	pci_set_master(pdev);

	card = spider_net_alloc_card();
	if (!card) {
		pr_err("Couldn't allocate net_device structure, "
			  "aborting.\n");
		goto out_release_regions;
	}
	card->pdev = pdev;

	/* fetch base address and length of first resource */
	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	card->netdev->mem_start = mmio_start;
	card->netdev->mem_end = mmio_start + mmio_len;
	card->regs = ioremap(mmio_start, mmio_len);

	if (!card->regs) {
		pr_err("Couldn't obtain PCI resources, aborting.\n");
		goto out_release_regions;
	}

	return card;

out_release_regions:
	pci_release_regions(pdev);
out_disable_dev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return NULL;
}

/**
 * spider_net_probe - initialization of a device
 * @pdev: PCI device
 * @ent: entry in the device id list
 *
 * Returns 0 on success, <0 on failure
 *
 * spider_net_probe initializes pdev and registers a net_device
 * structure for it. After that, the device can be ifconfig'ed up
 **/
static int __devinit
spider_net_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = -EIO;
	struct spider_net_card *card;

	card = spider_net_setup_pci_dev(pdev);
	if (!card)
		goto out;

	spider_net_workaround_rxramfull(card);
	spider_net_init_card(card);

	err = spider_net_setup_phy(card);
	if (err)
		goto out_undo_pci;

	err = spider_net_init_firmware(card);
	if (err)
		goto out_undo_pci;

	err = spider_net_setup_netdev(card);
	if (err)
		goto out_undo_pci;

	return 0;

out_undo_pci:
	spider_net_undo_pci_setup(card);
	free_netdev(card->netdev);
out:
	return err;
}

/**
 * spider_net_remove - removal of a device
 * @pdev: PCI device
 *
 * Returns 0 on success, <0 on failure
 *
 * spider_net_remove is called to remove the device and unregisters the
 * net_device
 **/
static void __devexit
spider_net_remove(struct pci_dev *pdev)
{
	struct net_device *netdev;
	struct spider_net_card *card;

	netdev = pci_get_drvdata(pdev);
	card = netdev_priv(netdev);

	wait_event(card->waitq,
		   atomic_read(&card->tx_timeout_task_counter) == 0);

	unregister_netdev(netdev);

	/* switch off card */
	spider_net_write_reg(card, SPIDER_NET_CKRCTRL,
			     SPIDER_NET_CKRCTRL_STOP_VALUE);
	spider_net_write_reg(card, SPIDER_NET_CKRCTRL,
			     SPIDER_NET_CKRCTRL_RUN_VALUE);

	spider_net_undo_pci_setup(card);
	free_netdev(netdev);
}

static struct pci_driver spider_net_driver = {
	.name		= spider_net_driver_name,
	.id_table	= spider_net_pci_tbl,
	.probe		= spider_net_probe,
	.remove		= __devexit_p(spider_net_remove)
};

/**
 * spider_net_init - init function when the driver is loaded
 *
 * spider_net_init registers the device driver
 */
static int __init spider_net_init(void)
{
	printk(KERN_INFO "Spidernet version %s.\n", VERSION);

	if (rx_descriptors < SPIDER_NET_RX_DESCRIPTORS_MIN) {
		rx_descriptors = SPIDER_NET_RX_DESCRIPTORS_MIN;
		pr_info("adjusting rx descriptors to %i.\n", rx_descriptors);
	}
	if (rx_descriptors > SPIDER_NET_RX_DESCRIPTORS_MAX) {
		rx_descriptors = SPIDER_NET_RX_DESCRIPTORS_MAX;
		pr_info("adjusting rx descriptors to %i.\n", rx_descriptors);
	}
	if (tx_descriptors < SPIDER_NET_TX_DESCRIPTORS_MIN) {
		tx_descriptors = SPIDER_NET_TX_DESCRIPTORS_MIN;
		pr_info("adjusting tx descriptors to %i.\n", tx_descriptors);
	}
	if (tx_descriptors > SPIDER_NET_TX_DESCRIPTORS_MAX) {
		tx_descriptors = SPIDER_NET_TX_DESCRIPTORS_MAX;
		pr_info("adjusting tx descriptors to %i.\n", tx_descriptors);
	}

	return pci_register_driver(&spider_net_driver);
}

/**
 * spider_net_cleanup - exit function when driver is unloaded
 *
 * spider_net_cleanup unregisters the device driver
 */
static void __exit spider_net_cleanup(void)
{
	pci_unregister_driver(&spider_net_driver);
}

module_init(spider_net_init);
module_exit(spider_net_cleanup);
