/*
 * Driver for (BCM4706)? GBit MAC core on BCMA bus.
 *
 * Copyright (C) 2012 Rafał Miłecki <zajec5@gmail.com>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bgmac.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <bcm47xx_nvram.h>

static const struct bcma_device_id bgmac_bcma_tbl[] = {
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_4706_MAC_GBIT, BCMA_ANY_REV, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_MAC_GBIT, BCMA_ANY_REV, BCMA_ANY_CLASS),
	{},
};
MODULE_DEVICE_TABLE(bcma, bgmac_bcma_tbl);

static bool bgmac_wait_value(struct bcma_device *core, u16 reg, u32 mask,
			     u32 value, int timeout)
{
	u32 val;
	int i;

	for (i = 0; i < timeout / 10; i++) {
		val = bcma_read32(core, reg);
		if ((val & mask) == value)
			return true;
		udelay(10);
	}
	pr_err("Timeout waiting for reg 0x%X\n", reg);
	return false;
}

/**************************************************
 * DMA
 **************************************************/

static void bgmac_dma_tx_reset(struct bgmac *bgmac, struct bgmac_dma_ring *ring)
{
	u32 val;
	int i;

	if (!ring->mmio_base)
		return;

	/* Suspend DMA TX ring first.
	 * bgmac_wait_value doesn't support waiting for any of few values, so
	 * implement whole loop here.
	 */
	bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_CTL,
		    BGMAC_DMA_TX_SUSPEND);
	for (i = 0; i < 10000 / 10; i++) {
		val = bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_TX_STATUS);
		val &= BGMAC_DMA_TX_STAT;
		if (val == BGMAC_DMA_TX_STAT_DISABLED ||
		    val == BGMAC_DMA_TX_STAT_IDLEWAIT ||
		    val == BGMAC_DMA_TX_STAT_STOPPED) {
			i = 0;
			break;
		}
		udelay(10);
	}
	if (i)
		bgmac_err(bgmac, "Timeout suspending DMA TX ring 0x%X (BGMAC_DMA_TX_STAT: 0x%08X)\n",
			  ring->mmio_base, val);

	/* Remove SUSPEND bit */
	bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_CTL, 0);
	if (!bgmac_wait_value(bgmac->core,
			      ring->mmio_base + BGMAC_DMA_TX_STATUS,
			      BGMAC_DMA_TX_STAT, BGMAC_DMA_TX_STAT_DISABLED,
			      10000)) {
		bgmac_warn(bgmac, "DMA TX ring 0x%X wasn't disabled on time, waiting additional 300us\n",
			   ring->mmio_base);
		udelay(300);
		val = bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_TX_STATUS);
		if ((val & BGMAC_DMA_TX_STAT) != BGMAC_DMA_TX_STAT_DISABLED)
			bgmac_err(bgmac, "Reset of DMA TX ring 0x%X failed\n",
				  ring->mmio_base);
	}
}

static void bgmac_dma_tx_enable(struct bgmac *bgmac,
				struct bgmac_dma_ring *ring)
{
	u32 ctl;

	ctl = bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_TX_CTL);
	if (bgmac->core->id.rev >= 4) {
		ctl &= ~BGMAC_DMA_TX_BL_MASK;
		ctl |= BGMAC_DMA_TX_BL_128 << BGMAC_DMA_TX_BL_SHIFT;

		ctl &= ~BGMAC_DMA_TX_MR_MASK;
		ctl |= BGMAC_DMA_TX_MR_2 << BGMAC_DMA_TX_MR_SHIFT;

		ctl &= ~BGMAC_DMA_TX_PC_MASK;
		ctl |= BGMAC_DMA_TX_PC_16 << BGMAC_DMA_TX_PC_SHIFT;

		ctl &= ~BGMAC_DMA_TX_PT_MASK;
		ctl |= BGMAC_DMA_TX_PT_8 << BGMAC_DMA_TX_PT_SHIFT;
	}
	ctl |= BGMAC_DMA_TX_ENABLE;
	ctl |= BGMAC_DMA_TX_PARITY_DISABLE;
	bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_CTL, ctl);
}

static netdev_tx_t bgmac_dma_tx_add(struct bgmac *bgmac,
				    struct bgmac_dma_ring *ring,
				    struct sk_buff *skb)
{
	struct device *dma_dev = bgmac->core->dma_dev;
	struct net_device *net_dev = bgmac->net_dev;
	struct bgmac_dma_desc *dma_desc;
	struct bgmac_slot_info *slot;
	u32 ctl0, ctl1;
	int free_slots;

	if (skb->len > BGMAC_DESC_CTL1_LEN) {
		bgmac_err(bgmac, "Too long skb (%d)\n", skb->len);
		goto err_stop_drop;
	}

	if (ring->start <= ring->end)
		free_slots = ring->start - ring->end + BGMAC_TX_RING_SLOTS;
	else
		free_slots = ring->start - ring->end;
	if (free_slots == 1) {
		bgmac_err(bgmac, "TX ring is full, queue should be stopped!\n");
		netif_stop_queue(net_dev);
		return NETDEV_TX_BUSY;
	}

	slot = &ring->slots[ring->end];
	slot->skb = skb;
	slot->dma_addr = dma_map_single(dma_dev, skb->data, skb->len,
					DMA_TO_DEVICE);
	if (dma_mapping_error(dma_dev, slot->dma_addr)) {
		bgmac_err(bgmac, "Mapping error of skb on ring 0x%X\n",
			  ring->mmio_base);
		goto err_stop_drop;
	}

	ctl0 = BGMAC_DESC_CTL0_IOC | BGMAC_DESC_CTL0_SOF | BGMAC_DESC_CTL0_EOF;
	if (ring->end == ring->num_slots - 1)
		ctl0 |= BGMAC_DESC_CTL0_EOT;
	ctl1 = skb->len & BGMAC_DESC_CTL1_LEN;

	dma_desc = ring->cpu_base;
	dma_desc += ring->end;
	dma_desc->addr_low = cpu_to_le32(lower_32_bits(slot->dma_addr));
	dma_desc->addr_high = cpu_to_le32(upper_32_bits(slot->dma_addr));
	dma_desc->ctl0 = cpu_to_le32(ctl0);
	dma_desc->ctl1 = cpu_to_le32(ctl1);

	netdev_sent_queue(net_dev, skb->len);

	wmb();

	/* Increase ring->end to point empty slot. We tell hardware the first
	 * slot it should *not* read.
	 */
	if (++ring->end >= BGMAC_TX_RING_SLOTS)
		ring->end = 0;
	bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_INDEX,
		    ring->index_base +
		    ring->end * sizeof(struct bgmac_dma_desc));

	/* Always keep one slot free to allow detecting bugged calls. */
	if (--free_slots == 1)
		netif_stop_queue(net_dev);

	return NETDEV_TX_OK;

err_stop_drop:
	netif_stop_queue(net_dev);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

/* Free transmitted packets */
static void bgmac_dma_tx_free(struct bgmac *bgmac, struct bgmac_dma_ring *ring)
{
	struct device *dma_dev = bgmac->core->dma_dev;
	int empty_slot;
	bool freed = false;
	unsigned bytes_compl = 0, pkts_compl = 0;

	/* The last slot that hardware didn't consume yet */
	empty_slot = bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_TX_STATUS);
	empty_slot &= BGMAC_DMA_TX_STATDPTR;
	empty_slot -= ring->index_base;
	empty_slot &= BGMAC_DMA_TX_STATDPTR;
	empty_slot /= sizeof(struct bgmac_dma_desc);

	while (ring->start != empty_slot) {
		struct bgmac_slot_info *slot = &ring->slots[ring->start];

		if (slot->skb) {
			/* Unmap no longer used buffer */
			dma_unmap_single(dma_dev, slot->dma_addr,
					 slot->skb->len, DMA_TO_DEVICE);
			slot->dma_addr = 0;

			bytes_compl += slot->skb->len;
			pkts_compl++;

			/* Free memory! :) */
			dev_kfree_skb(slot->skb);
			slot->skb = NULL;
		} else {
			bgmac_err(bgmac, "Hardware reported transmission for empty TX ring slot %d! End of ring: %d\n",
				  ring->start, ring->end);
		}

		if (++ring->start >= BGMAC_TX_RING_SLOTS)
			ring->start = 0;
		freed = true;
	}

	netdev_completed_queue(bgmac->net_dev, pkts_compl, bytes_compl);

	if (freed && netif_queue_stopped(bgmac->net_dev))
		netif_wake_queue(bgmac->net_dev);
}

static void bgmac_dma_rx_reset(struct bgmac *bgmac, struct bgmac_dma_ring *ring)
{
	if (!ring->mmio_base)
		return;

	bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_RX_CTL, 0);
	if (!bgmac_wait_value(bgmac->core,
			      ring->mmio_base + BGMAC_DMA_RX_STATUS,
			      BGMAC_DMA_RX_STAT, BGMAC_DMA_RX_STAT_DISABLED,
			      10000))
		bgmac_err(bgmac, "Reset of ring 0x%X RX failed\n",
			  ring->mmio_base);
}

static void bgmac_dma_rx_enable(struct bgmac *bgmac,
				struct bgmac_dma_ring *ring)
{
	u32 ctl;

	ctl = bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_RX_CTL);
	if (bgmac->core->id.rev >= 4) {
		ctl &= ~BGMAC_DMA_RX_BL_MASK;
		ctl |= BGMAC_DMA_RX_BL_128 << BGMAC_DMA_RX_BL_SHIFT;

		ctl &= ~BGMAC_DMA_RX_PC_MASK;
		ctl |= BGMAC_DMA_RX_PC_8 << BGMAC_DMA_RX_PC_SHIFT;

		ctl &= ~BGMAC_DMA_RX_PT_MASK;
		ctl |= BGMAC_DMA_RX_PT_1 << BGMAC_DMA_RX_PT_SHIFT;
	}
	ctl &= BGMAC_DMA_RX_ADDREXT_MASK;
	ctl |= BGMAC_DMA_RX_ENABLE;
	ctl |= BGMAC_DMA_RX_PARITY_DISABLE;
	ctl |= BGMAC_DMA_RX_OVERFLOW_CONT;
	ctl |= BGMAC_RX_FRAME_OFFSET << BGMAC_DMA_RX_FRAME_OFFSET_SHIFT;
	bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_RX_CTL, ctl);
}

static int bgmac_dma_rx_skb_for_slot(struct bgmac *bgmac,
				     struct bgmac_slot_info *slot)
{
	struct device *dma_dev = bgmac->core->dma_dev;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	struct bgmac_rx_header *rx;

	/* Alloc skb */
	skb = netdev_alloc_skb(bgmac->net_dev, BGMAC_RX_BUF_SIZE);
	if (!skb)
		return -ENOMEM;

	/* Poison - if everything goes fine, hardware will overwrite it */
	rx = (struct bgmac_rx_header *)skb->data;
	rx->len = cpu_to_le16(0xdead);
	rx->flags = cpu_to_le16(0xbeef);

	/* Map skb for the DMA */
	dma_addr = dma_map_single(dma_dev, skb->data,
				  BGMAC_RX_BUF_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(dma_dev, dma_addr)) {
		bgmac_err(bgmac, "DMA mapping error\n");
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	/* Update the slot */
	slot->skb = skb;
	slot->dma_addr = dma_addr;

	return 0;
}

static void bgmac_dma_rx_setup_desc(struct bgmac *bgmac,
				    struct bgmac_dma_ring *ring, int desc_idx)
{
	struct bgmac_dma_desc *dma_desc = ring->cpu_base + desc_idx;
	u32 ctl0 = 0, ctl1 = 0;

	if (desc_idx == ring->num_slots - 1)
		ctl0 |= BGMAC_DESC_CTL0_EOT;
	ctl1 |= BGMAC_RX_BUF_SIZE & BGMAC_DESC_CTL1_LEN;
	/* Is there any BGMAC device that requires extension? */
	/* ctl1 |= (addrext << B43_DMA64_DCTL1_ADDREXT_SHIFT) &
	 * B43_DMA64_DCTL1_ADDREXT_MASK;
	 */

	dma_desc->addr_low = cpu_to_le32(lower_32_bits(ring->slots[desc_idx].dma_addr));
	dma_desc->addr_high = cpu_to_le32(upper_32_bits(ring->slots[desc_idx].dma_addr));
	dma_desc->ctl0 = cpu_to_le32(ctl0);
	dma_desc->ctl1 = cpu_to_le32(ctl1);
}

static int bgmac_dma_rx_read(struct bgmac *bgmac, struct bgmac_dma_ring *ring,
			     int weight)
{
	u32 end_slot;
	int handled = 0;

	end_slot = bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_RX_STATUS);
	end_slot &= BGMAC_DMA_RX_STATDPTR;
	end_slot -= ring->index_base;
	end_slot &= BGMAC_DMA_RX_STATDPTR;
	end_slot /= sizeof(struct bgmac_dma_desc);

	ring->end = end_slot;

	while (ring->start != ring->end) {
		struct device *dma_dev = bgmac->core->dma_dev;
		struct bgmac_slot_info *slot = &ring->slots[ring->start];
		struct sk_buff *skb = slot->skb;
		struct bgmac_rx_header *rx;
		u16 len, flags;

		/* Unmap buffer to make it accessible to the CPU */
		dma_sync_single_for_cpu(dma_dev, slot->dma_addr,
					BGMAC_RX_BUF_SIZE, DMA_FROM_DEVICE);

		/* Get info from the header */
		rx = (struct bgmac_rx_header *)skb->data;
		len = le16_to_cpu(rx->len);
		flags = le16_to_cpu(rx->flags);

		do {
			dma_addr_t old_dma_addr = slot->dma_addr;
			int err;

			/* Check for poison and drop or pass the packet */
			if (len == 0xdead && flags == 0xbeef) {
				bgmac_err(bgmac, "Found poisoned packet at slot %d, DMA issue!\n",
					  ring->start);
				dma_sync_single_for_device(dma_dev,
							   slot->dma_addr,
							   BGMAC_RX_BUF_SIZE,
							   DMA_FROM_DEVICE);
				break;
			}

			/* Omit CRC. */
			len -= ETH_FCS_LEN;

			/* Prepare new skb as replacement */
			err = bgmac_dma_rx_skb_for_slot(bgmac, slot);
			if (err) {
				/* Poison the old skb */
				rx->len = cpu_to_le16(0xdead);
				rx->flags = cpu_to_le16(0xbeef);

				dma_sync_single_for_device(dma_dev,
							   slot->dma_addr,
							   BGMAC_RX_BUF_SIZE,
							   DMA_FROM_DEVICE);
				break;
			}
			bgmac_dma_rx_setup_desc(bgmac, ring, ring->start);

			/* Unmap old skb, we'll pass it to the netfif */
			dma_unmap_single(dma_dev, old_dma_addr,
					 BGMAC_RX_BUF_SIZE, DMA_FROM_DEVICE);

			skb_put(skb, BGMAC_RX_FRAME_OFFSET + len);
			skb_pull(skb, BGMAC_RX_FRAME_OFFSET);

			skb_checksum_none_assert(skb);
			skb->protocol = eth_type_trans(skb, bgmac->net_dev);
			netif_receive_skb(skb);
			handled++;
		} while (0);

		if (++ring->start >= BGMAC_RX_RING_SLOTS)
			ring->start = 0;

		if (handled >= weight) /* Should never be greater */
			break;
	}

	return handled;
}

/* Does ring support unaligned addressing? */
static bool bgmac_dma_unaligned(struct bgmac *bgmac,
				struct bgmac_dma_ring *ring,
				enum bgmac_dma_ring_type ring_type)
{
	switch (ring_type) {
	case BGMAC_DMA_RING_TX:
		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_RINGLO,
			    0xff0);
		if (bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_TX_RINGLO))
			return true;
		break;
	case BGMAC_DMA_RING_RX:
		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_RX_RINGLO,
			    0xff0);
		if (bgmac_read(bgmac, ring->mmio_base + BGMAC_DMA_RX_RINGLO))
			return true;
		break;
	}
	return false;
}

static void bgmac_dma_ring_free(struct bgmac *bgmac,
				struct bgmac_dma_ring *ring)
{
	struct device *dma_dev = bgmac->core->dma_dev;
	struct bgmac_slot_info *slot;
	int size;
	int i;

	for (i = 0; i < ring->num_slots; i++) {
		slot = &ring->slots[i];
		if (slot->skb) {
			if (slot->dma_addr)
				dma_unmap_single(dma_dev, slot->dma_addr,
						 slot->skb->len, DMA_TO_DEVICE);
			dev_kfree_skb(slot->skb);
		}
	}

	if (ring->cpu_base) {
		/* Free ring of descriptors */
		size = ring->num_slots * sizeof(struct bgmac_dma_desc);
		dma_free_coherent(dma_dev, size, ring->cpu_base,
				  ring->dma_base);
	}
}

static void bgmac_dma_free(struct bgmac *bgmac)
{
	int i;

	for (i = 0; i < BGMAC_MAX_TX_RINGS; i++)
		bgmac_dma_ring_free(bgmac, &bgmac->tx_ring[i]);
	for (i = 0; i < BGMAC_MAX_RX_RINGS; i++)
		bgmac_dma_ring_free(bgmac, &bgmac->rx_ring[i]);
}

static int bgmac_dma_alloc(struct bgmac *bgmac)
{
	struct device *dma_dev = bgmac->core->dma_dev;
	struct bgmac_dma_ring *ring;
	static const u16 ring_base[] = { BGMAC_DMA_BASE0, BGMAC_DMA_BASE1,
					 BGMAC_DMA_BASE2, BGMAC_DMA_BASE3, };
	int size; /* ring size: different for Tx and Rx */
	int err;
	int i;

	BUILD_BUG_ON(BGMAC_MAX_TX_RINGS > ARRAY_SIZE(ring_base));
	BUILD_BUG_ON(BGMAC_MAX_RX_RINGS > ARRAY_SIZE(ring_base));

	if (!(bcma_aread32(bgmac->core, BCMA_IOST) & BCMA_IOST_DMA64)) {
		bgmac_err(bgmac, "Core does not report 64-bit DMA\n");
		return -ENOTSUPP;
	}

	for (i = 0; i < BGMAC_MAX_TX_RINGS; i++) {
		ring = &bgmac->tx_ring[i];
		ring->num_slots = BGMAC_TX_RING_SLOTS;
		ring->mmio_base = ring_base[i];

		/* Alloc ring of descriptors */
		size = ring->num_slots * sizeof(struct bgmac_dma_desc);
		ring->cpu_base = dma_zalloc_coherent(dma_dev, size,
						     &ring->dma_base,
						     GFP_KERNEL);
		if (!ring->cpu_base) {
			bgmac_err(bgmac, "Allocation of TX ring 0x%X failed\n",
				  ring->mmio_base);
			goto err_dma_free;
		}

		ring->unaligned = bgmac_dma_unaligned(bgmac, ring,
						      BGMAC_DMA_RING_TX);
		if (ring->unaligned)
			ring->index_base = lower_32_bits(ring->dma_base);
		else
			ring->index_base = 0;

		/* No need to alloc TX slots yet */
	}

	for (i = 0; i < BGMAC_MAX_RX_RINGS; i++) {
		int j;

		ring = &bgmac->rx_ring[i];
		ring->num_slots = BGMAC_RX_RING_SLOTS;
		ring->mmio_base = ring_base[i];

		/* Alloc ring of descriptors */
		size = ring->num_slots * sizeof(struct bgmac_dma_desc);
		ring->cpu_base = dma_zalloc_coherent(dma_dev, size,
						     &ring->dma_base,
						     GFP_KERNEL);
		if (!ring->cpu_base) {
			bgmac_err(bgmac, "Allocation of RX ring 0x%X failed\n",
				  ring->mmio_base);
			err = -ENOMEM;
			goto err_dma_free;
		}

		ring->unaligned = bgmac_dma_unaligned(bgmac, ring,
						      BGMAC_DMA_RING_RX);
		if (ring->unaligned)
			ring->index_base = lower_32_bits(ring->dma_base);
		else
			ring->index_base = 0;

		/* Alloc RX slots */
		for (j = 0; j < ring->num_slots; j++) {
			err = bgmac_dma_rx_skb_for_slot(bgmac, &ring->slots[j]);
			if (err) {
				bgmac_err(bgmac, "Can't allocate skb for slot in RX ring\n");
				goto err_dma_free;
			}
		}
	}

	return 0;

err_dma_free:
	bgmac_dma_free(bgmac);
	return -ENOMEM;
}

static void bgmac_dma_init(struct bgmac *bgmac)
{
	struct bgmac_dma_ring *ring;
	int i;

	for (i = 0; i < BGMAC_MAX_TX_RINGS; i++) {
		ring = &bgmac->tx_ring[i];

		if (!ring->unaligned)
			bgmac_dma_tx_enable(bgmac, ring);
		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_RINGLO,
			    lower_32_bits(ring->dma_base));
		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_TX_RINGHI,
			    upper_32_bits(ring->dma_base));
		if (ring->unaligned)
			bgmac_dma_tx_enable(bgmac, ring);

		ring->start = 0;
		ring->end = 0;	/* Points the slot that should *not* be read */
	}

	for (i = 0; i < BGMAC_MAX_RX_RINGS; i++) {
		int j;

		ring = &bgmac->rx_ring[i];

		if (!ring->unaligned)
			bgmac_dma_rx_enable(bgmac, ring);
		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_RX_RINGLO,
			    lower_32_bits(ring->dma_base));
		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_RX_RINGHI,
			    upper_32_bits(ring->dma_base));
		if (ring->unaligned)
			bgmac_dma_rx_enable(bgmac, ring);

		for (j = 0; j < ring->num_slots; j++)
			bgmac_dma_rx_setup_desc(bgmac, ring, j);

		bgmac_write(bgmac, ring->mmio_base + BGMAC_DMA_RX_INDEX,
			    ring->index_base +
			    ring->num_slots * sizeof(struct bgmac_dma_desc));

		ring->start = 0;
		ring->end = 0;
	}
}

/**************************************************
 * PHY ops
 **************************************************/

static u16 bgmac_phy_read(struct bgmac *bgmac, u8 phyaddr, u8 reg)
{
	struct bcma_device *core;
	u16 phy_access_addr;
	u16 phy_ctl_addr;
	u32 tmp;

	BUILD_BUG_ON(BGMAC_PA_DATA_MASK != BCMA_GMAC_CMN_PA_DATA_MASK);
	BUILD_BUG_ON(BGMAC_PA_ADDR_MASK != BCMA_GMAC_CMN_PA_ADDR_MASK);
	BUILD_BUG_ON(BGMAC_PA_ADDR_SHIFT != BCMA_GMAC_CMN_PA_ADDR_SHIFT);
	BUILD_BUG_ON(BGMAC_PA_REG_MASK != BCMA_GMAC_CMN_PA_REG_MASK);
	BUILD_BUG_ON(BGMAC_PA_REG_SHIFT != BCMA_GMAC_CMN_PA_REG_SHIFT);
	BUILD_BUG_ON(BGMAC_PA_WRITE != BCMA_GMAC_CMN_PA_WRITE);
	BUILD_BUG_ON(BGMAC_PA_START != BCMA_GMAC_CMN_PA_START);
	BUILD_BUG_ON(BGMAC_PC_EPA_MASK != BCMA_GMAC_CMN_PC_EPA_MASK);
	BUILD_BUG_ON(BGMAC_PC_MCT_MASK != BCMA_GMAC_CMN_PC_MCT_MASK);
	BUILD_BUG_ON(BGMAC_PC_MCT_SHIFT != BCMA_GMAC_CMN_PC_MCT_SHIFT);
	BUILD_BUG_ON(BGMAC_PC_MTE != BCMA_GMAC_CMN_PC_MTE);

	if (bgmac->core->id.id == BCMA_CORE_4706_MAC_GBIT) {
		core = bgmac->core->bus->drv_gmac_cmn.core;
		phy_access_addr = BCMA_GMAC_CMN_PHY_ACCESS;
		phy_ctl_addr = BCMA_GMAC_CMN_PHY_CTL;
	} else {
		core = bgmac->core;
		phy_access_addr = BGMAC_PHY_ACCESS;
		phy_ctl_addr = BGMAC_PHY_CNTL;
	}

	tmp = bcma_read32(core, phy_ctl_addr);
	tmp &= ~BGMAC_PC_EPA_MASK;
	tmp |= phyaddr;
	bcma_write32(core, phy_ctl_addr, tmp);

	tmp = BGMAC_PA_START;
	tmp |= phyaddr << BGMAC_PA_ADDR_SHIFT;
	tmp |= reg << BGMAC_PA_REG_SHIFT;
	bcma_write32(core, phy_access_addr, tmp);

	if (!bgmac_wait_value(core, phy_access_addr, BGMAC_PA_START, 0, 1000)) {
		bgmac_err(bgmac, "Reading PHY %d register 0x%X failed\n",
			  phyaddr, reg);
		return 0xffff;
	}

	return bcma_read32(core, phy_access_addr) & BGMAC_PA_DATA_MASK;
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipphywr */
static int bgmac_phy_write(struct bgmac *bgmac, u8 phyaddr, u8 reg, u16 value)
{
	struct bcma_device *core;
	u16 phy_access_addr;
	u16 phy_ctl_addr;
	u32 tmp;

	if (bgmac->core->id.id == BCMA_CORE_4706_MAC_GBIT) {
		core = bgmac->core->bus->drv_gmac_cmn.core;
		phy_access_addr = BCMA_GMAC_CMN_PHY_ACCESS;
		phy_ctl_addr = BCMA_GMAC_CMN_PHY_CTL;
	} else {
		core = bgmac->core;
		phy_access_addr = BGMAC_PHY_ACCESS;
		phy_ctl_addr = BGMAC_PHY_CNTL;
	}

	tmp = bcma_read32(core, phy_ctl_addr);
	tmp &= ~BGMAC_PC_EPA_MASK;
	tmp |= phyaddr;
	bcma_write32(core, phy_ctl_addr, tmp);

	bgmac_write(bgmac, BGMAC_INT_STATUS, BGMAC_IS_MDIO);
	if (bgmac_read(bgmac, BGMAC_INT_STATUS) & BGMAC_IS_MDIO)
		bgmac_warn(bgmac, "Error setting MDIO int\n");

	tmp = BGMAC_PA_START;
	tmp |= BGMAC_PA_WRITE;
	tmp |= phyaddr << BGMAC_PA_ADDR_SHIFT;
	tmp |= reg << BGMAC_PA_REG_SHIFT;
	tmp |= value;
	bcma_write32(core, phy_access_addr, tmp);

	if (!bgmac_wait_value(core, phy_access_addr, BGMAC_PA_START, 0, 1000)) {
		bgmac_err(bgmac, "Writing to PHY %d register 0x%X failed\n",
			  phyaddr, reg);
		return -ETIMEDOUT;
	}

	return 0;
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipphyinit */
static void bgmac_phy_init(struct bgmac *bgmac)
{
	struct bcma_chipinfo *ci = &bgmac->core->bus->chipinfo;
	struct bcma_drv_cc *cc = &bgmac->core->bus->drv_cc;
	u8 i;

	if (ci->id == BCMA_CHIP_ID_BCM5356) {
		for (i = 0; i < 5; i++) {
			bgmac_phy_write(bgmac, i, 0x1f, 0x008b);
			bgmac_phy_write(bgmac, i, 0x15, 0x0100);
			bgmac_phy_write(bgmac, i, 0x1f, 0x000f);
			bgmac_phy_write(bgmac, i, 0x12, 0x2aaa);
			bgmac_phy_write(bgmac, i, 0x1f, 0x000b);
		}
	}
	if ((ci->id == BCMA_CHIP_ID_BCM5357 && ci->pkg != 10) ||
	    (ci->id == BCMA_CHIP_ID_BCM4749 && ci->pkg != 10) ||
	    (ci->id == BCMA_CHIP_ID_BCM53572 && ci->pkg != 9)) {
		bcma_chipco_chipctl_maskset(cc, 2, ~0xc0000000, 0);
		bcma_chipco_chipctl_maskset(cc, 4, ~0x80000000, 0);
		for (i = 0; i < 5; i++) {
			bgmac_phy_write(bgmac, i, 0x1f, 0x000f);
			bgmac_phy_write(bgmac, i, 0x16, 0x5284);
			bgmac_phy_write(bgmac, i, 0x1f, 0x000b);
			bgmac_phy_write(bgmac, i, 0x17, 0x0010);
			bgmac_phy_write(bgmac, i, 0x1f, 0x000f);
			bgmac_phy_write(bgmac, i, 0x16, 0x5296);
			bgmac_phy_write(bgmac, i, 0x17, 0x1073);
			bgmac_phy_write(bgmac, i, 0x17, 0x9073);
			bgmac_phy_write(bgmac, i, 0x16, 0x52b6);
			bgmac_phy_write(bgmac, i, 0x17, 0x9273);
			bgmac_phy_write(bgmac, i, 0x1f, 0x000b);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipphyreset */
static void bgmac_phy_reset(struct bgmac *bgmac)
{
	if (bgmac->phyaddr == BGMAC_PHY_NOREGS)
		return;

	bgmac_phy_write(bgmac, bgmac->phyaddr, MII_BMCR, BMCR_RESET);
	udelay(100);
	if (bgmac_phy_read(bgmac, bgmac->phyaddr, MII_BMCR) & BMCR_RESET)
		bgmac_err(bgmac, "PHY reset failed\n");
	bgmac_phy_init(bgmac);
}

/**************************************************
 * Chip ops
 **************************************************/

/* TODO: can we just drop @force? Can we don't reset MAC at all if there is
 * nothing to change? Try if after stabilizng driver.
 */
static void bgmac_cmdcfg_maskset(struct bgmac *bgmac, u32 mask, u32 set,
				 bool force)
{
	u32 cmdcfg = bgmac_read(bgmac, BGMAC_CMDCFG);
	u32 new_val = (cmdcfg & mask) | set;

	bgmac_set(bgmac, BGMAC_CMDCFG, BGMAC_CMDCFG_SR(bgmac->core->id.rev));
	udelay(2);

	if (new_val != cmdcfg || force)
		bgmac_write(bgmac, BGMAC_CMDCFG, new_val);

	bgmac_mask(bgmac, BGMAC_CMDCFG, ~BGMAC_CMDCFG_SR(bgmac->core->id.rev));
	udelay(2);
}

static void bgmac_write_mac_address(struct bgmac *bgmac, u8 *addr)
{
	u32 tmp;

	tmp = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3];
	bgmac_write(bgmac, BGMAC_MACADDR_HIGH, tmp);
	tmp = (addr[4] << 8) | addr[5];
	bgmac_write(bgmac, BGMAC_MACADDR_LOW, tmp);
}

static void bgmac_set_rx_mode(struct net_device *net_dev)
{
	struct bgmac *bgmac = netdev_priv(net_dev);

	if (net_dev->flags & IFF_PROMISC)
		bgmac_cmdcfg_maskset(bgmac, ~0, BGMAC_CMDCFG_PROM, true);
	else
		bgmac_cmdcfg_maskset(bgmac, ~BGMAC_CMDCFG_PROM, 0, true);
}

#if 0 /* We don't use that regs yet */
static void bgmac_chip_stats_update(struct bgmac *bgmac)
{
	int i;

	if (bgmac->core->id.id != BCMA_CORE_4706_MAC_GBIT) {
		for (i = 0; i < BGMAC_NUM_MIB_TX_REGS; i++)
			bgmac->mib_tx_regs[i] =
				bgmac_read(bgmac,
					   BGMAC_TX_GOOD_OCTETS + (i * 4));
		for (i = 0; i < BGMAC_NUM_MIB_RX_REGS; i++)
			bgmac->mib_rx_regs[i] =
				bgmac_read(bgmac,
					   BGMAC_RX_GOOD_OCTETS + (i * 4));
	}

	/* TODO: what else? how to handle BCM4706? Specs are needed */
}
#endif

static void bgmac_clear_mib(struct bgmac *bgmac)
{
	int i;

	if (bgmac->core->id.id == BCMA_CORE_4706_MAC_GBIT)
		return;

	bgmac_set(bgmac, BGMAC_DEV_CTL, BGMAC_DC_MROR);
	for (i = 0; i < BGMAC_NUM_MIB_TX_REGS; i++)
		bgmac_read(bgmac, BGMAC_TX_GOOD_OCTETS + (i * 4));
	for (i = 0; i < BGMAC_NUM_MIB_RX_REGS; i++)
		bgmac_read(bgmac, BGMAC_RX_GOOD_OCTETS + (i * 4));
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/gmac_speed */
static void bgmac_mac_speed(struct bgmac *bgmac)
{
	u32 mask = ~(BGMAC_CMDCFG_ES_MASK | BGMAC_CMDCFG_HD);
	u32 set = 0;

	switch (bgmac->mac_speed) {
	case SPEED_10:
		set |= BGMAC_CMDCFG_ES_10;
		break;
	case SPEED_100:
		set |= BGMAC_CMDCFG_ES_100;
		break;
	case SPEED_1000:
		set |= BGMAC_CMDCFG_ES_1000;
		break;
	case SPEED_2500:
		set |= BGMAC_CMDCFG_ES_2500;
		break;
	default:
		bgmac_err(bgmac, "Unsupported speed: %d\n", bgmac->mac_speed);
	}

	if (bgmac->mac_duplex == DUPLEX_HALF)
		set |= BGMAC_CMDCFG_HD;

	bgmac_cmdcfg_maskset(bgmac, mask, set, true);
}

static void bgmac_miiconfig(struct bgmac *bgmac)
{
	struct bcma_device *core = bgmac->core;
	struct bcma_chipinfo *ci = &core->bus->chipinfo;
	u8 imode;

	if (ci->id == BCMA_CHIP_ID_BCM4707 ||
	    ci->id == BCMA_CHIP_ID_BCM53018) {
		bcma_awrite32(core, BCMA_IOCTL,
			      bcma_aread32(core, BCMA_IOCTL) | 0x40 |
			      BGMAC_BCMA_IOCTL_SW_CLKEN);
		bgmac->mac_speed = SPEED_2500;
		bgmac->mac_duplex = DUPLEX_FULL;
		bgmac_mac_speed(bgmac);
	} else {
		imode = (bgmac_read(bgmac, BGMAC_DEV_STATUS) &
			BGMAC_DS_MM_MASK) >> BGMAC_DS_MM_SHIFT;
		if (imode == 0 || imode == 1) {
			bgmac->mac_speed = SPEED_100;
			bgmac->mac_duplex = DUPLEX_FULL;
			bgmac_mac_speed(bgmac);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipreset */
static void bgmac_chip_reset(struct bgmac *bgmac)
{
	struct bcma_device *core = bgmac->core;
	struct bcma_bus *bus = core->bus;
	struct bcma_chipinfo *ci = &bus->chipinfo;
	u32 flags;
	u32 iost;
	int i;

	if (bcma_core_is_enabled(core)) {
		if (!bgmac->stats_grabbed) {
			/* bgmac_chip_stats_update(bgmac); */
			bgmac->stats_grabbed = true;
		}

		for (i = 0; i < BGMAC_MAX_TX_RINGS; i++)
			bgmac_dma_tx_reset(bgmac, &bgmac->tx_ring[i]);

		bgmac_cmdcfg_maskset(bgmac, ~0, BGMAC_CMDCFG_ML, false);
		udelay(1);

		for (i = 0; i < BGMAC_MAX_RX_RINGS; i++)
			bgmac_dma_rx_reset(bgmac, &bgmac->rx_ring[i]);

		/* TODO: Clear software multicast filter list */
	}

	iost = bcma_aread32(core, BCMA_IOST);
	if ((ci->id == BCMA_CHIP_ID_BCM5357 && ci->pkg == BCMA_PKG_ID_BCM47186) ||
	    (ci->id == BCMA_CHIP_ID_BCM4749 && ci->pkg == 10) ||
	    (ci->id == BCMA_CHIP_ID_BCM53572 && ci->pkg == BCMA_PKG_ID_BCM47188))
		iost &= ~BGMAC_BCMA_IOST_ATTACHED;

	/* 3GMAC: for BCM4707, only do core reset at bgmac_probe() */
	if (ci->id != BCMA_CHIP_ID_BCM4707) {
		flags = 0;
		if (iost & BGMAC_BCMA_IOST_ATTACHED) {
			flags = BGMAC_BCMA_IOCTL_SW_CLKEN;
			if (!bgmac->has_robosw)
				flags |= BGMAC_BCMA_IOCTL_SW_RESET;
		}
		bcma_core_enable(core, flags);
	}

	/* Request Misc PLL for corerev > 2 */
	if (core->id.rev > 2 &&
	    ci->id != BCMA_CHIP_ID_BCM4707 &&
	    ci->id != BCMA_CHIP_ID_BCM53018) {
		bgmac_set(bgmac, BCMA_CLKCTLST,
			  BGMAC_BCMA_CLKCTLST_MISC_PLL_REQ);
		bgmac_wait_value(bgmac->core, BCMA_CLKCTLST,
				 BGMAC_BCMA_CLKCTLST_MISC_PLL_ST,
				 BGMAC_BCMA_CLKCTLST_MISC_PLL_ST,
				 1000);
	}

	if (ci->id == BCMA_CHIP_ID_BCM5357 ||
	    ci->id == BCMA_CHIP_ID_BCM4749 ||
	    ci->id == BCMA_CHIP_ID_BCM53572) {
		struct bcma_drv_cc *cc = &bgmac->core->bus->drv_cc;
		u8 et_swtype = 0;
		u8 sw_type = BGMAC_CHIPCTL_1_SW_TYPE_EPHY |
			     BGMAC_CHIPCTL_1_IF_TYPE_MII;
		char buf[4];

		if (bcm47xx_nvram_getenv("et_swtype", buf, sizeof(buf)) > 0) {
			if (kstrtou8(buf, 0, &et_swtype))
				bgmac_err(bgmac, "Failed to parse et_swtype (%s)\n",
					  buf);
			et_swtype &= 0x0f;
			et_swtype <<= 4;
			sw_type = et_swtype;
		} else if (ci->id == BCMA_CHIP_ID_BCM5357 && ci->pkg == BCMA_PKG_ID_BCM5358) {
			sw_type = BGMAC_CHIPCTL_1_SW_TYPE_EPHYRMII;
		} else if ((ci->id == BCMA_CHIP_ID_BCM5357 && ci->pkg == BCMA_PKG_ID_BCM47186) ||
			   (ci->id == BCMA_CHIP_ID_BCM4749 && ci->pkg == 10) ||
			   (ci->id == BCMA_CHIP_ID_BCM53572 && ci->pkg == BCMA_PKG_ID_BCM47188)) {
			sw_type = BGMAC_CHIPCTL_1_IF_TYPE_RGMII |
				  BGMAC_CHIPCTL_1_SW_TYPE_RGMII;
		}
		bcma_chipco_chipctl_maskset(cc, 1,
					    ~(BGMAC_CHIPCTL_1_IF_TYPE_MASK |
					      BGMAC_CHIPCTL_1_SW_TYPE_MASK),
					    sw_type);
	}

	if (iost & BGMAC_BCMA_IOST_ATTACHED && !bgmac->has_robosw)
		bcma_awrite32(core, BCMA_IOCTL,
			      bcma_aread32(core, BCMA_IOCTL) &
			      ~BGMAC_BCMA_IOCTL_SW_RESET);

	/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/gmac_reset
	 * Specs don't say about using BGMAC_CMDCFG_SR, but in this routine
	 * BGMAC_CMDCFG is read _after_ putting chip in a reset. So it has to
	 * be keps until taking MAC out of the reset.
	 */
	bgmac_cmdcfg_maskset(bgmac,
			     ~(BGMAC_CMDCFG_TE |
			       BGMAC_CMDCFG_RE |
			       BGMAC_CMDCFG_RPI |
			       BGMAC_CMDCFG_TAI |
			       BGMAC_CMDCFG_HD |
			       BGMAC_CMDCFG_ML |
			       BGMAC_CMDCFG_CFE |
			       BGMAC_CMDCFG_RL |
			       BGMAC_CMDCFG_RED |
			       BGMAC_CMDCFG_PE |
			       BGMAC_CMDCFG_TPI |
			       BGMAC_CMDCFG_PAD_EN |
			       BGMAC_CMDCFG_PF),
			     BGMAC_CMDCFG_PROM |
			     BGMAC_CMDCFG_NLC |
			     BGMAC_CMDCFG_CFE |
			     BGMAC_CMDCFG_SR(core->id.rev),
			     false);
	bgmac->mac_speed = SPEED_UNKNOWN;
	bgmac->mac_duplex = DUPLEX_UNKNOWN;

	bgmac_clear_mib(bgmac);
	if (core->id.id == BCMA_CORE_4706_MAC_GBIT)
		bcma_maskset32(bgmac->cmn, BCMA_GMAC_CMN_PHY_CTL, ~0,
			       BCMA_GMAC_CMN_PC_MTE);
	else
		bgmac_set(bgmac, BGMAC_PHY_CNTL, BGMAC_PC_MTE);
	bgmac_miiconfig(bgmac);
	bgmac_phy_init(bgmac);

	netdev_reset_queue(bgmac->net_dev);

	bgmac->int_status = 0;
}

static void bgmac_chip_intrs_on(struct bgmac *bgmac)
{
	bgmac_write(bgmac, BGMAC_INT_MASK, bgmac->int_mask);
}

static void bgmac_chip_intrs_off(struct bgmac *bgmac)
{
	bgmac_write(bgmac, BGMAC_INT_MASK, 0);
	bgmac_read(bgmac, BGMAC_INT_MASK);
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/gmac_enable */
static void bgmac_enable(struct bgmac *bgmac)
{
	struct bcma_chipinfo *ci = &bgmac->core->bus->chipinfo;
	u32 cmdcfg;
	u32 mode;
	u32 rxq_ctl;
	u32 fl_ctl;
	u16 bp_clk;
	u8 mdp;

	cmdcfg = bgmac_read(bgmac, BGMAC_CMDCFG);
	bgmac_cmdcfg_maskset(bgmac, ~(BGMAC_CMDCFG_TE | BGMAC_CMDCFG_RE),
			     BGMAC_CMDCFG_SR(bgmac->core->id.rev), true);
	udelay(2);
	cmdcfg |= BGMAC_CMDCFG_TE | BGMAC_CMDCFG_RE;
	bgmac_write(bgmac, BGMAC_CMDCFG, cmdcfg);

	mode = (bgmac_read(bgmac, BGMAC_DEV_STATUS) & BGMAC_DS_MM_MASK) >>
		BGMAC_DS_MM_SHIFT;
	if (ci->id != BCMA_CHIP_ID_BCM47162 || mode != 0)
		bgmac_set(bgmac, BCMA_CLKCTLST, BCMA_CLKCTLST_FORCEHT);
	if (ci->id == BCMA_CHIP_ID_BCM47162 && mode == 2)
		bcma_chipco_chipctl_maskset(&bgmac->core->bus->drv_cc, 1, ~0,
					    BGMAC_CHIPCTL_1_RXC_DLL_BYPASS);

	switch (ci->id) {
	case BCMA_CHIP_ID_BCM5357:
	case BCMA_CHIP_ID_BCM4749:
	case BCMA_CHIP_ID_BCM53572:
	case BCMA_CHIP_ID_BCM4716:
	case BCMA_CHIP_ID_BCM47162:
		fl_ctl = 0x03cb04cb;
		if (ci->id == BCMA_CHIP_ID_BCM5357 ||
		    ci->id == BCMA_CHIP_ID_BCM4749 ||
		    ci->id == BCMA_CHIP_ID_BCM53572)
			fl_ctl = 0x2300e1;
		bgmac_write(bgmac, BGMAC_FLOW_CTL_THRESH, fl_ctl);
		bgmac_write(bgmac, BGMAC_PAUSE_CTL, 0x27fff);
		break;
	}

	if (ci->id != BCMA_CHIP_ID_BCM4707 &&
	    ci->id != BCMA_CHIP_ID_BCM53018) {
		rxq_ctl = bgmac_read(bgmac, BGMAC_RXQ_CTL);
		rxq_ctl &= ~BGMAC_RXQ_CTL_MDP_MASK;
		bp_clk = bcma_pmu_get_bus_clock(&bgmac->core->bus->drv_cc) /
				1000000;
		mdp = (bp_clk * 128 / 1000) - 3;
		rxq_ctl |= (mdp << BGMAC_RXQ_CTL_MDP_SHIFT);
		bgmac_write(bgmac, BGMAC_RXQ_CTL, rxq_ctl);
	}
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipinit */
static void bgmac_chip_init(struct bgmac *bgmac, bool full_init)
{
	struct bgmac_dma_ring *ring;
	int i;

	/* 1 interrupt per received frame */
	bgmac_write(bgmac, BGMAC_INT_RECV_LAZY, 1 << BGMAC_IRL_FC_SHIFT);

	/* Enable 802.3x tx flow control (honor received PAUSE frames) */
	bgmac_cmdcfg_maskset(bgmac, ~BGMAC_CMDCFG_RPI, 0, true);

	bgmac_set_rx_mode(bgmac->net_dev);

	bgmac_write_mac_address(bgmac, bgmac->net_dev->dev_addr);

	if (bgmac->loopback)
		bgmac_cmdcfg_maskset(bgmac, ~0, BGMAC_CMDCFG_ML, false);
	else
		bgmac_cmdcfg_maskset(bgmac, ~BGMAC_CMDCFG_ML, 0, false);

	bgmac_write(bgmac, BGMAC_RXMAX_LENGTH, 32 + ETHER_MAX_LEN);

	if (full_init) {
		bgmac_dma_init(bgmac);
		if (1) /* FIXME: is there any case we don't want IRQs? */
			bgmac_chip_intrs_on(bgmac);
	} else {
		for (i = 0; i < BGMAC_MAX_RX_RINGS; i++) {
			ring = &bgmac->rx_ring[i];
			bgmac_dma_rx_enable(bgmac, ring);
		}
	}

	bgmac_enable(bgmac);
}

static irqreturn_t bgmac_interrupt(int irq, void *dev_id)
{
	struct bgmac *bgmac = netdev_priv(dev_id);

	u32 int_status = bgmac_read(bgmac, BGMAC_INT_STATUS);
	int_status &= bgmac->int_mask;

	if (!int_status)
		return IRQ_NONE;

	/* Ack */
	bgmac_write(bgmac, BGMAC_INT_STATUS, int_status);

	/* Disable new interrupts until handling existing ones */
	bgmac_chip_intrs_off(bgmac);

	bgmac->int_status = int_status;

	napi_schedule(&bgmac->napi);

	return IRQ_HANDLED;
}

static int bgmac_poll(struct napi_struct *napi, int weight)
{
	struct bgmac *bgmac = container_of(napi, struct bgmac, napi);
	struct bgmac_dma_ring *ring;
	int handled = 0;

	if (bgmac->int_status & BGMAC_IS_TX0) {
		ring = &bgmac->tx_ring[0];
		bgmac_dma_tx_free(bgmac, ring);
		bgmac->int_status &= ~BGMAC_IS_TX0;
	}

	if (bgmac->int_status & BGMAC_IS_RX) {
		ring = &bgmac->rx_ring[0];
		handled += bgmac_dma_rx_read(bgmac, ring, weight);
		bgmac->int_status &= ~BGMAC_IS_RX;
	}

	if (bgmac->int_status) {
		bgmac_err(bgmac, "Unknown IRQs: 0x%08X\n", bgmac->int_status);
		bgmac->int_status = 0;
	}

	if (handled < weight) {
		napi_complete(napi);
		bgmac_chip_intrs_on(bgmac);
	}

	return handled;
}

/**************************************************
 * net_device_ops
 **************************************************/

static int bgmac_open(struct net_device *net_dev)
{
	struct bgmac *bgmac = netdev_priv(net_dev);
	int err = 0;

	bgmac_chip_reset(bgmac);
	/* Specs say about reclaiming rings here, but we do that in DMA init */
	bgmac_chip_init(bgmac, true);

	err = request_irq(bgmac->core->irq, bgmac_interrupt, IRQF_SHARED,
			  KBUILD_MODNAME, net_dev);
	if (err < 0) {
		bgmac_err(bgmac, "IRQ request error: %d!\n", err);
		goto err_out;
	}
	napi_enable(&bgmac->napi);

	phy_start(bgmac->phy_dev);

	netif_carrier_on(net_dev);

err_out:
	return err;
}

static int bgmac_stop(struct net_device *net_dev)
{
	struct bgmac *bgmac = netdev_priv(net_dev);

	netif_carrier_off(net_dev);

	phy_stop(bgmac->phy_dev);

	napi_disable(&bgmac->napi);
	bgmac_chip_intrs_off(bgmac);
	free_irq(bgmac->core->irq, net_dev);

	bgmac_chip_reset(bgmac);

	return 0;
}

static netdev_tx_t bgmac_start_xmit(struct sk_buff *skb,
				    struct net_device *net_dev)
{
	struct bgmac *bgmac = netdev_priv(net_dev);
	struct bgmac_dma_ring *ring;

	/* No QOS support yet */
	ring = &bgmac->tx_ring[0];
	return bgmac_dma_tx_add(bgmac, ring, skb);
}

static int bgmac_set_mac_address(struct net_device *net_dev, void *addr)
{
	struct bgmac *bgmac = netdev_priv(net_dev);
	int ret;

	ret = eth_prepare_mac_addr_change(net_dev, addr);
	if (ret < 0)
		return ret;
	bgmac_write_mac_address(bgmac, (u8 *)addr);
	eth_commit_mac_addr_change(net_dev, addr);
	return 0;
}

static int bgmac_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct bgmac *bgmac = netdev_priv(net_dev);

	if (!netif_running(net_dev))
		return -EINVAL;

	return phy_mii_ioctl(bgmac->phy_dev, ifr, cmd);
}

static const struct net_device_ops bgmac_netdev_ops = {
	.ndo_open		= bgmac_open,
	.ndo_stop		= bgmac_stop,
	.ndo_start_xmit		= bgmac_start_xmit,
	.ndo_set_rx_mode	= bgmac_set_rx_mode,
	.ndo_set_mac_address	= bgmac_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl           = bgmac_ioctl,
};

/**************************************************
 * ethtool_ops
 **************************************************/

static int bgmac_get_settings(struct net_device *net_dev,
			      struct ethtool_cmd *cmd)
{
	struct bgmac *bgmac = netdev_priv(net_dev);

	return phy_ethtool_gset(bgmac->phy_dev, cmd);
}

static int bgmac_set_settings(struct net_device *net_dev,
			      struct ethtool_cmd *cmd)
{
	struct bgmac *bgmac = netdev_priv(net_dev);

	return phy_ethtool_sset(bgmac->phy_dev, cmd);
}

static void bgmac_get_drvinfo(struct net_device *net_dev,
			      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->bus_info, "BCMA", sizeof(info->bus_info));
}

static const struct ethtool_ops bgmac_ethtool_ops = {
	.get_settings		= bgmac_get_settings,
	.set_settings		= bgmac_set_settings,
	.get_drvinfo		= bgmac_get_drvinfo,
};

/**************************************************
 * MII
 **************************************************/

static int bgmac_mii_read(struct mii_bus *bus, int mii_id, int regnum)
{
	return bgmac_phy_read(bus->priv, mii_id, regnum);
}

static int bgmac_mii_write(struct mii_bus *bus, int mii_id, int regnum,
			   u16 value)
{
	return bgmac_phy_write(bus->priv, mii_id, regnum, value);
}

static void bgmac_adjust_link(struct net_device *net_dev)
{
	struct bgmac *bgmac = netdev_priv(net_dev);
	struct phy_device *phy_dev = bgmac->phy_dev;
	bool update = false;

	if (phy_dev->link) {
		if (phy_dev->speed != bgmac->mac_speed) {
			bgmac->mac_speed = phy_dev->speed;
			update = true;
		}

		if (phy_dev->duplex != bgmac->mac_duplex) {
			bgmac->mac_duplex = phy_dev->duplex;
			update = true;
		}
	}

	if (update) {
		bgmac_mac_speed(bgmac);
		phy_print_status(phy_dev);
	}
}

static int bgmac_mii_register(struct bgmac *bgmac)
{
	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;
	char bus_id[MII_BUS_ID_SIZE + 3];
	int i, err = 0;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "bgmac mii bus";
	sprintf(mii_bus->id, "%s-%d-%d", "bgmac", bgmac->core->bus->num,
		bgmac->core->core_unit);
	mii_bus->priv = bgmac;
	mii_bus->read = bgmac_mii_read;
	mii_bus->write = bgmac_mii_write;
	mii_bus->parent = &bgmac->core->dev;
	mii_bus->phy_mask = ~(1 << bgmac->phyaddr);

	mii_bus->irq = kmalloc_array(PHY_MAX_ADDR, sizeof(int), GFP_KERNEL);
	if (!mii_bus->irq) {
		err = -ENOMEM;
		goto err_free_bus;
	}
	for (i = 0; i < PHY_MAX_ADDR; i++)
		mii_bus->irq[i] = PHY_POLL;

	err = mdiobus_register(mii_bus);
	if (err) {
		bgmac_err(bgmac, "Registration of mii bus failed\n");
		goto err_free_irq;
	}

	bgmac->mii_bus = mii_bus;

	/* Connect to the PHY */
	snprintf(bus_id, sizeof(bus_id), PHY_ID_FMT, mii_bus->id,
		 bgmac->phyaddr);
	phy_dev = phy_connect(bgmac->net_dev, bus_id, &bgmac_adjust_link,
			      PHY_INTERFACE_MODE_MII);
	if (IS_ERR(phy_dev)) {
		bgmac_err(bgmac, "PHY connecton failed\n");
		err = PTR_ERR(phy_dev);
		goto err_unregister_bus;
	}
	bgmac->phy_dev = phy_dev;

	return err;

err_unregister_bus:
	mdiobus_unregister(mii_bus);
err_free_irq:
	kfree(mii_bus->irq);
err_free_bus:
	mdiobus_free(mii_bus);
	return err;
}

static void bgmac_mii_unregister(struct bgmac *bgmac)
{
	struct mii_bus *mii_bus = bgmac->mii_bus;

	mdiobus_unregister(mii_bus);
	kfree(mii_bus->irq);
	mdiobus_free(mii_bus);
}

/**************************************************
 * BCMA bus ops
 **************************************************/

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipattach */
static int bgmac_probe(struct bcma_device *core)
{
	struct bcma_chipinfo *ci = &core->bus->chipinfo;
	struct net_device *net_dev;
	struct bgmac *bgmac;
	struct ssb_sprom *sprom = &core->bus->sprom;
	u8 *mac = core->core_unit ? sprom->et1mac : sprom->et0mac;
	int err;

	/* We don't support 2nd, 3rd, ... units, SPROM has to be adjusted */
	if (core->core_unit > 1) {
		pr_err("Unsupported core_unit %d\n", core->core_unit);
		return -ENOTSUPP;
	}

	if (!is_valid_ether_addr(mac)) {
		dev_err(&core->dev, "Invalid MAC addr: %pM\n", mac);
		eth_random_addr(mac);
		dev_warn(&core->dev, "Using random MAC: %pM\n", mac);
	}

	/* Allocation and references */
	net_dev = alloc_etherdev(sizeof(*bgmac));
	if (!net_dev)
		return -ENOMEM;
	net_dev->netdev_ops = &bgmac_netdev_ops;
	net_dev->irq = core->irq;
	net_dev->ethtool_ops = &bgmac_ethtool_ops;
	bgmac = netdev_priv(net_dev);
	bgmac->net_dev = net_dev;
	bgmac->core = core;
	bcma_set_drvdata(core, bgmac);

	/* Defaults */
	memcpy(bgmac->net_dev->dev_addr, mac, ETH_ALEN);

	/* On BCM4706 we need common core to access PHY */
	if (core->id.id == BCMA_CORE_4706_MAC_GBIT &&
	    !core->bus->drv_gmac_cmn.core) {
		bgmac_err(bgmac, "GMAC CMN core not found (required for BCM4706)\n");
		err = -ENODEV;
		goto err_netdev_free;
	}
	bgmac->cmn = core->bus->drv_gmac_cmn.core;

	bgmac->phyaddr = core->core_unit ? sprom->et1phyaddr :
			 sprom->et0phyaddr;
	bgmac->phyaddr &= BGMAC_PHY_MASK;
	if (bgmac->phyaddr == BGMAC_PHY_MASK) {
		bgmac_err(bgmac, "No PHY found\n");
		err = -ENODEV;
		goto err_netdev_free;
	}
	bgmac_info(bgmac, "Found PHY addr: %d%s\n", bgmac->phyaddr,
		   bgmac->phyaddr == BGMAC_PHY_NOREGS ? " (NOREGS)" : "");

	if (core->bus->hosttype == BCMA_HOSTTYPE_PCI) {
		bgmac_err(bgmac, "PCI setup not implemented\n");
		err = -ENOTSUPP;
		goto err_netdev_free;
	}

	bgmac_chip_reset(bgmac);

	/* For Northstar, we have to take all GMAC core out of reset */
	if (ci->id == BCMA_CHIP_ID_BCM4707 ||
	    ci->id == BCMA_CHIP_ID_BCM53018) {
		struct bcma_device *ns_core;
		int ns_gmac;

		/* Northstar has 4 GMAC cores */
		for (ns_gmac = 0; ns_gmac < 4; ns_gmac++) {
			/* As Northstar requirement, we have to reset all GMACs
			 * before accessing one. bgmac_chip_reset() call
			 * bcma_core_enable() for this core. Then the other
			 * three GMACs didn't reset.  We do it here.
			 */
			ns_core = bcma_find_core_unit(core->bus,
						      BCMA_CORE_MAC_GBIT,
						      ns_gmac);
			if (ns_core && !bcma_core_is_enabled(ns_core))
				bcma_core_enable(ns_core, 0);
		}
	}

	err = bgmac_dma_alloc(bgmac);
	if (err) {
		bgmac_err(bgmac, "Unable to alloc memory for DMA\n");
		goto err_netdev_free;
	}

	bgmac->int_mask = BGMAC_IS_ERRMASK | BGMAC_IS_RX | BGMAC_IS_TX_MASK;
	if (bcm47xx_nvram_getenv("et0_no_txint", NULL, 0) == 0)
		bgmac->int_mask &= ~BGMAC_IS_TX_MASK;

	/* TODO: reset the external phy. Specs are needed */
	bgmac_phy_reset(bgmac);

	bgmac->has_robosw = !!(core->bus->sprom.boardflags_lo &
			       BGMAC_BFL_ENETROBO);
	if (bgmac->has_robosw)
		bgmac_warn(bgmac, "Support for Roboswitch not implemented\n");

	if (core->bus->sprom.boardflags_lo & BGMAC_BFL_ENETADM)
		bgmac_warn(bgmac, "Support for ADMtek ethernet switch not implemented\n");

	netif_napi_add(net_dev, &bgmac->napi, bgmac_poll, BGMAC_WEIGHT);

	err = bgmac_mii_register(bgmac);
	if (err) {
		bgmac_err(bgmac, "Cannot register MDIO\n");
		goto err_dma_free;
	}

	err = register_netdev(bgmac->net_dev);
	if (err) {
		bgmac_err(bgmac, "Cannot register net device\n");
		goto err_mii_unregister;
	}

	netif_carrier_off(net_dev);

	return 0;

err_mii_unregister:
	bgmac_mii_unregister(bgmac);
err_dma_free:
	bgmac_dma_free(bgmac);

err_netdev_free:
	bcma_set_drvdata(core, NULL);
	free_netdev(net_dev);

	return err;
}

static void bgmac_remove(struct bcma_device *core)
{
	struct bgmac *bgmac = bcma_get_drvdata(core);

	unregister_netdev(bgmac->net_dev);
	bgmac_mii_unregister(bgmac);
	netif_napi_del(&bgmac->napi);
	bgmac_dma_free(bgmac);
	bcma_set_drvdata(core, NULL);
	free_netdev(bgmac->net_dev);
}

static struct bcma_driver bgmac_bcma_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= bgmac_bcma_tbl,
	.probe		= bgmac_probe,
	.remove		= bgmac_remove,
};

static int __init bgmac_init(void)
{
	int err;

	err = bcma_driver_register(&bgmac_bcma_driver);
	if (err)
		return err;
	pr_info("Broadcom 47xx GBit MAC driver loaded\n");

	return 0;
}

static void __exit bgmac_exit(void)
{
	bcma_driver_unregister(&bgmac_bcma_driver);
}

module_init(bgmac_init)
module_exit(bgmac_exit)

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
