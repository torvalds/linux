// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43legacy wireless driver

  DMA ringbuffer and descriptor allocation/management

  Copyright (c) 2005, 2006 Michael Buesch <m@bues.ch>

  Some code in this file is derived from the b44.c driver
  Copyright (C) 2002 David S. Miller
  Copyright (C) Pekka Pietikainen


*/

#include "b43legacy.h"
#include "dma.h"
#include "main.h"
#include "debugfs.h"
#include "xmit.h"

#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/dst.h>

/* 32bit DMA ops. */
static
struct b43legacy_dmadesc32 *op32_idx2desc(struct b43legacy_dmaring *ring,
					  int slot,
					  struct b43legacy_dmadesc_meta **meta)
{
	struct b43legacy_dmadesc32 *desc;

	*meta = &(ring->meta[slot]);
	desc = ring->descbase;
	desc = &(desc[slot]);

	return desc;
}

static void op32_fill_descriptor(struct b43legacy_dmaring *ring,
				 struct b43legacy_dmadesc32 *desc,
				 dma_addr_t dmaaddr, u16 bufsize,
				 int start, int end, int irq)
{
	struct b43legacy_dmadesc32 *descbase = ring->descbase;
	int slot;
	u32 ctl;
	u32 addr;
	u32 addrext;

	slot = (int)(desc - descbase);
	B43legacy_WARN_ON(!(slot >= 0 && slot < ring->nr_slots));

	addr = (u32)(dmaaddr & ~SSB_DMA_TRANSLATION_MASK);
	addrext = (u32)(dmaaddr & SSB_DMA_TRANSLATION_MASK)
		   >> SSB_DMA_TRANSLATION_SHIFT;
	addr |= ring->dev->dma.translation;
	ctl = (bufsize - ring->frameoffset)
	      & B43legacy_DMA32_DCTL_BYTECNT;
	if (slot == ring->nr_slots - 1)
		ctl |= B43legacy_DMA32_DCTL_DTABLEEND;
	if (start)
		ctl |= B43legacy_DMA32_DCTL_FRAMESTART;
	if (end)
		ctl |= B43legacy_DMA32_DCTL_FRAMEEND;
	if (irq)
		ctl |= B43legacy_DMA32_DCTL_IRQ;
	ctl |= (addrext << B43legacy_DMA32_DCTL_ADDREXT_SHIFT)
	       & B43legacy_DMA32_DCTL_ADDREXT_MASK;

	desc->control = cpu_to_le32(ctl);
	desc->address = cpu_to_le32(addr);
}

static void op32_poke_tx(struct b43legacy_dmaring *ring, int slot)
{
	b43legacy_dma_write(ring, B43legacy_DMA32_TXINDEX,
			    (u32)(slot * sizeof(struct b43legacy_dmadesc32)));
}

static void op32_tx_suspend(struct b43legacy_dmaring *ring)
{
	b43legacy_dma_write(ring, B43legacy_DMA32_TXCTL,
			    b43legacy_dma_read(ring, B43legacy_DMA32_TXCTL)
			    | B43legacy_DMA32_TXSUSPEND);
}

static void op32_tx_resume(struct b43legacy_dmaring *ring)
{
	b43legacy_dma_write(ring, B43legacy_DMA32_TXCTL,
			    b43legacy_dma_read(ring, B43legacy_DMA32_TXCTL)
			    & ~B43legacy_DMA32_TXSUSPEND);
}

static int op32_get_current_rxslot(struct b43legacy_dmaring *ring)
{
	u32 val;

	val = b43legacy_dma_read(ring, B43legacy_DMA32_RXSTATUS);
	val &= B43legacy_DMA32_RXDPTR;

	return (val / sizeof(struct b43legacy_dmadesc32));
}

static void op32_set_current_rxslot(struct b43legacy_dmaring *ring,
				    int slot)
{
	b43legacy_dma_write(ring, B43legacy_DMA32_RXINDEX,
			    (u32)(slot * sizeof(struct b43legacy_dmadesc32)));
}

static inline int free_slots(struct b43legacy_dmaring *ring)
{
	return (ring->nr_slots - ring->used_slots);
}

static inline int next_slot(struct b43legacy_dmaring *ring, int slot)
{
	B43legacy_WARN_ON(!(slot >= -1 && slot <= ring->nr_slots - 1));
	if (slot == ring->nr_slots - 1)
		return 0;
	return slot + 1;
}

static inline int prev_slot(struct b43legacy_dmaring *ring, int slot)
{
	B43legacy_WARN_ON(!(slot >= 0 && slot <= ring->nr_slots - 1));
	if (slot == 0)
		return ring->nr_slots - 1;
	return slot - 1;
}

#ifdef CONFIG_B43LEGACY_DEBUG
static void update_max_used_slots(struct b43legacy_dmaring *ring,
				  int current_used_slots)
{
	if (current_used_slots <= ring->max_used_slots)
		return;
	ring->max_used_slots = current_used_slots;
	if (b43legacy_debug(ring->dev, B43legacy_DBG_DMAVERBOSE))
		b43legacydbg(ring->dev->wl,
		       "max_used_slots increased to %d on %s ring %d\n",
		       ring->max_used_slots,
		       ring->tx ? "TX" : "RX",
		       ring->index);
}
#else
static inline
void update_max_used_slots(struct b43legacy_dmaring *ring,
			   int current_used_slots)
{ }
#endif /* DEBUG */

/* Request a slot for usage. */
static inline
int request_slot(struct b43legacy_dmaring *ring)
{
	int slot;

	B43legacy_WARN_ON(!ring->tx);
	B43legacy_WARN_ON(ring->stopped);
	B43legacy_WARN_ON(free_slots(ring) == 0);

	slot = next_slot(ring, ring->current_slot);
	ring->current_slot = slot;
	ring->used_slots++;

	update_max_used_slots(ring, ring->used_slots);

	return slot;
}

/* Mac80211-queue to b43legacy-ring mapping */
static struct b43legacy_dmaring *priority_to_txring(
						struct b43legacy_wldev *dev,
						int queue_priority)
{
	struct b43legacy_dmaring *ring;

/*FIXME: For now we always run on TX-ring-1 */
return dev->dma.tx_ring1;

	/* 0 = highest priority */
	switch (queue_priority) {
	default:
		B43legacy_WARN_ON(1);
		fallthrough;
	case 0:
		ring = dev->dma.tx_ring3;
		break;
	case 1:
		ring = dev->dma.tx_ring2;
		break;
	case 2:
		ring = dev->dma.tx_ring1;
		break;
	case 3:
		ring = dev->dma.tx_ring0;
		break;
	case 4:
		ring = dev->dma.tx_ring4;
		break;
	case 5:
		ring = dev->dma.tx_ring5;
		break;
	}

	return ring;
}

static u16 b43legacy_dmacontroller_base(enum b43legacy_dmatype type,
					int controller_idx)
{
	static const u16 map32[] = {
		B43legacy_MMIO_DMA32_BASE0,
		B43legacy_MMIO_DMA32_BASE1,
		B43legacy_MMIO_DMA32_BASE2,
		B43legacy_MMIO_DMA32_BASE3,
		B43legacy_MMIO_DMA32_BASE4,
		B43legacy_MMIO_DMA32_BASE5,
	};

	B43legacy_WARN_ON(!(controller_idx >= 0 &&
			  controller_idx < ARRAY_SIZE(map32)));
	return map32[controller_idx];
}

static inline
dma_addr_t map_descbuffer(struct b43legacy_dmaring *ring,
			  unsigned char *buf,
			  size_t len,
			  int tx)
{
	dma_addr_t dmaaddr;

	if (tx)
		dmaaddr = dma_map_single(ring->dev->dev->dma_dev,
					     buf, len,
					     DMA_TO_DEVICE);
	else
		dmaaddr = dma_map_single(ring->dev->dev->dma_dev,
					     buf, len,
					     DMA_FROM_DEVICE);

	return dmaaddr;
}

static inline
void unmap_descbuffer(struct b43legacy_dmaring *ring,
		      dma_addr_t addr,
		      size_t len,
		      int tx)
{
	if (tx)
		dma_unmap_single(ring->dev->dev->dma_dev,
				     addr, len,
				     DMA_TO_DEVICE);
	else
		dma_unmap_single(ring->dev->dev->dma_dev,
				     addr, len,
				     DMA_FROM_DEVICE);
}

static inline
void sync_descbuffer_for_cpu(struct b43legacy_dmaring *ring,
			     dma_addr_t addr,
			     size_t len)
{
	B43legacy_WARN_ON(ring->tx);

	dma_sync_single_for_cpu(ring->dev->dev->dma_dev,
				addr, len, DMA_FROM_DEVICE);
}

static inline
void sync_descbuffer_for_device(struct b43legacy_dmaring *ring,
				dma_addr_t addr,
				size_t len)
{
	B43legacy_WARN_ON(ring->tx);

	dma_sync_single_for_device(ring->dev->dev->dma_dev,
				   addr, len, DMA_FROM_DEVICE);
}

static inline
void free_descriptor_buffer(struct b43legacy_dmaring *ring,
			    struct b43legacy_dmadesc_meta *meta,
			    int irq_context)
{
	if (meta->skb) {
		if (irq_context)
			dev_kfree_skb_irq(meta->skb);
		else
			dev_kfree_skb(meta->skb);
		meta->skb = NULL;
	}
}

static int alloc_ringmemory(struct b43legacy_dmaring *ring)
{
	/* GFP flags must match the flags in free_ringmemory()! */
	ring->descbase = dma_alloc_coherent(ring->dev->dev->dma_dev,
					    B43legacy_DMA_RINGMEMSIZE,
					    &(ring->dmabase), GFP_KERNEL);
	if (!ring->descbase)
		return -ENOMEM;

	return 0;
}

static void free_ringmemory(struct b43legacy_dmaring *ring)
{
	dma_free_coherent(ring->dev->dev->dma_dev, B43legacy_DMA_RINGMEMSIZE,
			  ring->descbase, ring->dmabase);
}

/* Reset the RX DMA channel */
static int b43legacy_dmacontroller_rx_reset(struct b43legacy_wldev *dev,
					    u16 mmio_base,
					    enum b43legacy_dmatype type)
{
	int i;
	u32 value;
	u16 offset;

	might_sleep();

	offset = B43legacy_DMA32_RXCTL;
	b43legacy_write32(dev, mmio_base + offset, 0);
	for (i = 0; i < 10; i++) {
		offset = B43legacy_DMA32_RXSTATUS;
		value = b43legacy_read32(dev, mmio_base + offset);
		value &= B43legacy_DMA32_RXSTATE;
		if (value == B43legacy_DMA32_RXSTAT_DISABLED) {
			i = -1;
			break;
		}
		msleep(1);
	}
	if (i != -1) {
		b43legacyerr(dev->wl, "DMA RX reset timed out\n");
		return -ENODEV;
	}

	return 0;
}

/* Reset the RX DMA channel */
static int b43legacy_dmacontroller_tx_reset(struct b43legacy_wldev *dev,
					    u16 mmio_base,
					    enum b43legacy_dmatype type)
{
	int i;
	u32 value;
	u16 offset;

	might_sleep();

	for (i = 0; i < 10; i++) {
		offset = B43legacy_DMA32_TXSTATUS;
		value = b43legacy_read32(dev, mmio_base + offset);
		value &= B43legacy_DMA32_TXSTATE;
		if (value == B43legacy_DMA32_TXSTAT_DISABLED ||
		    value == B43legacy_DMA32_TXSTAT_IDLEWAIT ||
		    value == B43legacy_DMA32_TXSTAT_STOPPED)
			break;
		msleep(1);
	}
	offset = B43legacy_DMA32_TXCTL;
	b43legacy_write32(dev, mmio_base + offset, 0);
	for (i = 0; i < 10; i++) {
		offset = B43legacy_DMA32_TXSTATUS;
		value = b43legacy_read32(dev, mmio_base + offset);
		value &= B43legacy_DMA32_TXSTATE;
		if (value == B43legacy_DMA32_TXSTAT_DISABLED) {
			i = -1;
			break;
		}
		msleep(1);
	}
	if (i != -1) {
		b43legacyerr(dev->wl, "DMA TX reset timed out\n");
		return -ENODEV;
	}
	/* ensure the reset is completed. */
	msleep(1);

	return 0;
}

/* Check if a DMA mapping address is invalid. */
static bool b43legacy_dma_mapping_error(struct b43legacy_dmaring *ring,
					 dma_addr_t addr,
					 size_t buffersize,
					 bool dma_to_device)
{
	if (unlikely(dma_mapping_error(ring->dev->dev->dma_dev, addr)))
		return true;

	switch (ring->type) {
	case B43legacy_DMA_30BIT:
		if ((u64)addr + buffersize > (1ULL << 30))
			goto address_error;
		break;
	case B43legacy_DMA_32BIT:
		if ((u64)addr + buffersize > (1ULL << 32))
			goto address_error;
		break;
	}

	/* The address is OK. */
	return false;

address_error:
	/* We can't support this address. Unmap it again. */
	unmap_descbuffer(ring, addr, buffersize, dma_to_device);

	return true;
}

static int setup_rx_descbuffer(struct b43legacy_dmaring *ring,
			       struct b43legacy_dmadesc32 *desc,
			       struct b43legacy_dmadesc_meta *meta,
			       gfp_t gfp_flags)
{
	struct b43legacy_rxhdr_fw3 *rxhdr;
	struct b43legacy_hwtxstatus *txstat;
	dma_addr_t dmaaddr;
	struct sk_buff *skb;

	B43legacy_WARN_ON(ring->tx);

	skb = __dev_alloc_skb(ring->rx_buffersize, gfp_flags);
	if (unlikely(!skb))
		return -ENOMEM;
	dmaaddr = map_descbuffer(ring, skb->data,
				 ring->rx_buffersize, 0);
	if (b43legacy_dma_mapping_error(ring, dmaaddr, ring->rx_buffersize, 0)) {
		/* ugh. try to realloc in zone_dma */
		gfp_flags |= GFP_DMA;

		dev_kfree_skb_any(skb);

		skb = __dev_alloc_skb(ring->rx_buffersize, gfp_flags);
		if (unlikely(!skb))
			return -ENOMEM;
		dmaaddr = map_descbuffer(ring, skb->data,
					 ring->rx_buffersize, 0);
	}

	if (b43legacy_dma_mapping_error(ring, dmaaddr, ring->rx_buffersize, 0)) {
		dev_kfree_skb_any(skb);
		return -EIO;
	}

	meta->skb = skb;
	meta->dmaaddr = dmaaddr;
	op32_fill_descriptor(ring, desc, dmaaddr, ring->rx_buffersize, 0, 0, 0);

	rxhdr = (struct b43legacy_rxhdr_fw3 *)(skb->data);
	rxhdr->frame_len = 0;
	txstat = (struct b43legacy_hwtxstatus *)(skb->data);
	txstat->cookie = 0;

	return 0;
}

/* Allocate the initial descbuffers.
 * This is used for an RX ring only.
 */
static int alloc_initial_descbuffers(struct b43legacy_dmaring *ring)
{
	int i;
	int err = -ENOMEM;
	struct b43legacy_dmadesc32 *desc;
	struct b43legacy_dmadesc_meta *meta;

	for (i = 0; i < ring->nr_slots; i++) {
		desc = op32_idx2desc(ring, i, &meta);

		err = setup_rx_descbuffer(ring, desc, meta, GFP_KERNEL);
		if (err) {
			b43legacyerr(ring->dev->wl,
			       "Failed to allocate initial descbuffers\n");
			goto err_unwind;
		}
	}
	mb(); /* all descbuffer setup before next line */
	ring->used_slots = ring->nr_slots;
	err = 0;
out:
	return err;

err_unwind:
	for (i--; i >= 0; i--) {
		desc = op32_idx2desc(ring, i, &meta);

		unmap_descbuffer(ring, meta->dmaaddr, ring->rx_buffersize, 0);
		dev_kfree_skb(meta->skb);
	}
	goto out;
}

/* Do initial setup of the DMA controller.
 * Reset the controller, write the ring busaddress
 * and switch the "enable" bit on.
 */
static int dmacontroller_setup(struct b43legacy_dmaring *ring)
{
	int err = 0;
	u32 value;
	u32 addrext;
	u32 trans = ring->dev->dma.translation;
	u32 ringbase = (u32)(ring->dmabase);

	if (ring->tx) {
		addrext = (ringbase & SSB_DMA_TRANSLATION_MASK)
			  >> SSB_DMA_TRANSLATION_SHIFT;
		value = B43legacy_DMA32_TXENABLE;
		value |= (addrext << B43legacy_DMA32_TXADDREXT_SHIFT)
			& B43legacy_DMA32_TXADDREXT_MASK;
		b43legacy_dma_write(ring, B43legacy_DMA32_TXCTL, value);
		b43legacy_dma_write(ring, B43legacy_DMA32_TXRING,
				    (ringbase & ~SSB_DMA_TRANSLATION_MASK)
				    | trans);
	} else {
		err = alloc_initial_descbuffers(ring);
		if (err)
			goto out;

		addrext = (ringbase & SSB_DMA_TRANSLATION_MASK)
			  >> SSB_DMA_TRANSLATION_SHIFT;
		value = (ring->frameoffset <<
			 B43legacy_DMA32_RXFROFF_SHIFT);
		value |= B43legacy_DMA32_RXENABLE;
		value |= (addrext << B43legacy_DMA32_RXADDREXT_SHIFT)
			 & B43legacy_DMA32_RXADDREXT_MASK;
		b43legacy_dma_write(ring, B43legacy_DMA32_RXCTL, value);
		b43legacy_dma_write(ring, B43legacy_DMA32_RXRING,
				    (ringbase & ~SSB_DMA_TRANSLATION_MASK)
				    | trans);
		b43legacy_dma_write(ring, B43legacy_DMA32_RXINDEX, 200);
	}

out:
	return err;
}

/* Shutdown the DMA controller. */
static void dmacontroller_cleanup(struct b43legacy_dmaring *ring)
{
	if (ring->tx) {
		b43legacy_dmacontroller_tx_reset(ring->dev, ring->mmio_base,
						 ring->type);
		b43legacy_dma_write(ring, B43legacy_DMA32_TXRING, 0);
	} else {
		b43legacy_dmacontroller_rx_reset(ring->dev, ring->mmio_base,
						 ring->type);
		b43legacy_dma_write(ring, B43legacy_DMA32_RXRING, 0);
	}
}

static void free_all_descbuffers(struct b43legacy_dmaring *ring)
{
	struct b43legacy_dmadesc_meta *meta;
	int i;

	if (!ring->used_slots)
		return;
	for (i = 0; i < ring->nr_slots; i++) {
		op32_idx2desc(ring, i, &meta);

		if (!meta->skb) {
			B43legacy_WARN_ON(!ring->tx);
			continue;
		}
		if (ring->tx)
			unmap_descbuffer(ring, meta->dmaaddr,
					 meta->skb->len, 1);
		else
			unmap_descbuffer(ring, meta->dmaaddr,
					 ring->rx_buffersize, 0);
		free_descriptor_buffer(ring, meta, 0);
	}
}

static enum b43legacy_dmatype b43legacy_engine_type(struct b43legacy_wldev *dev)
{
	u32 tmp;
	u16 mmio_base;

	mmio_base = b43legacy_dmacontroller_base(0, 0);
	b43legacy_write32(dev,
			mmio_base + B43legacy_DMA32_TXCTL,
			B43legacy_DMA32_TXADDREXT_MASK);
	tmp = b43legacy_read32(dev, mmio_base +
			       B43legacy_DMA32_TXCTL);
	if (tmp & B43legacy_DMA32_TXADDREXT_MASK)
		return B43legacy_DMA_32BIT;
	return B43legacy_DMA_30BIT;
}

/* Main initialization function. */
static
struct b43legacy_dmaring *b43legacy_setup_dmaring(struct b43legacy_wldev *dev,
						  int controller_index,
						  int for_tx,
						  enum b43legacy_dmatype type)
{
	struct b43legacy_dmaring *ring;
	int err;
	int nr_slots;
	dma_addr_t dma_test;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		goto out;
	ring->type = type;
	ring->dev = dev;

	nr_slots = B43legacy_RXRING_SLOTS;
	if (for_tx)
		nr_slots = B43legacy_TXRING_SLOTS;

	ring->meta = kcalloc(nr_slots, sizeof(struct b43legacy_dmadesc_meta),
			     GFP_KERNEL);
	if (!ring->meta)
		goto err_kfree_ring;
	if (for_tx) {
		ring->txhdr_cache = kcalloc(nr_slots,
					sizeof(struct b43legacy_txhdr_fw3),
					GFP_KERNEL);
		if (!ring->txhdr_cache)
			goto err_kfree_meta;

		/* test for ability to dma to txhdr_cache */
		dma_test = dma_map_single(dev->dev->dma_dev, ring->txhdr_cache,
					      sizeof(struct b43legacy_txhdr_fw3),
					      DMA_TO_DEVICE);

		if (b43legacy_dma_mapping_error(ring, dma_test,
					sizeof(struct b43legacy_txhdr_fw3), 1)) {
			/* ugh realloc */
			kfree(ring->txhdr_cache);
			ring->txhdr_cache = kcalloc(nr_slots,
					sizeof(struct b43legacy_txhdr_fw3),
					GFP_KERNEL | GFP_DMA);
			if (!ring->txhdr_cache)
				goto err_kfree_meta;

			dma_test = dma_map_single(dev->dev->dma_dev,
					ring->txhdr_cache,
					sizeof(struct b43legacy_txhdr_fw3),
					DMA_TO_DEVICE);

			if (b43legacy_dma_mapping_error(ring, dma_test,
					sizeof(struct b43legacy_txhdr_fw3), 1))
				goto err_kfree_txhdr_cache;
		}

		dma_unmap_single(dev->dev->dma_dev, dma_test,
				 sizeof(struct b43legacy_txhdr_fw3),
				 DMA_TO_DEVICE);
	}

	ring->nr_slots = nr_slots;
	ring->mmio_base = b43legacy_dmacontroller_base(type, controller_index);
	ring->index = controller_index;
	if (for_tx) {
		ring->tx = true;
		ring->current_slot = -1;
	} else {
		if (ring->index == 0) {
			ring->rx_buffersize = B43legacy_DMA0_RX_BUFFERSIZE;
			ring->frameoffset = B43legacy_DMA0_RX_FRAMEOFFSET;
		} else if (ring->index == 3) {
			ring->rx_buffersize = B43legacy_DMA3_RX_BUFFERSIZE;
			ring->frameoffset = B43legacy_DMA3_RX_FRAMEOFFSET;
		} else
			B43legacy_WARN_ON(1);
	}
#ifdef CONFIG_B43LEGACY_DEBUG
	ring->last_injected_overflow = jiffies;
#endif

	err = alloc_ringmemory(ring);
	if (err)
		goto err_kfree_txhdr_cache;
	err = dmacontroller_setup(ring);
	if (err)
		goto err_free_ringmemory;

out:
	return ring;

err_free_ringmemory:
	free_ringmemory(ring);
err_kfree_txhdr_cache:
	kfree(ring->txhdr_cache);
err_kfree_meta:
	kfree(ring->meta);
err_kfree_ring:
	kfree(ring);
	ring = NULL;
	goto out;
}

/* Main cleanup function. */
static void b43legacy_destroy_dmaring(struct b43legacy_dmaring *ring)
{
	if (!ring)
		return;

	b43legacydbg(ring->dev->wl, "DMA-%u 0x%04X (%s) max used slots:"
		     " %d/%d\n", (unsigned int)(ring->type), ring->mmio_base,
		     (ring->tx) ? "TX" : "RX", ring->max_used_slots,
		     ring->nr_slots);
	/* Device IRQs are disabled prior entering this function,
	 * so no need to take care of concurrency with rx handler stuff.
	 */
	dmacontroller_cleanup(ring);
	free_all_descbuffers(ring);
	free_ringmemory(ring);

	kfree(ring->txhdr_cache);
	kfree(ring->meta);
	kfree(ring);
}

void b43legacy_dma_free(struct b43legacy_wldev *dev)
{
	struct b43legacy_dma *dma;

	if (b43legacy_using_pio(dev))
		return;
	dma = &dev->dma;

	b43legacy_destroy_dmaring(dma->rx_ring3);
	dma->rx_ring3 = NULL;
	b43legacy_destroy_dmaring(dma->rx_ring0);
	dma->rx_ring0 = NULL;

	b43legacy_destroy_dmaring(dma->tx_ring5);
	dma->tx_ring5 = NULL;
	b43legacy_destroy_dmaring(dma->tx_ring4);
	dma->tx_ring4 = NULL;
	b43legacy_destroy_dmaring(dma->tx_ring3);
	dma->tx_ring3 = NULL;
	b43legacy_destroy_dmaring(dma->tx_ring2);
	dma->tx_ring2 = NULL;
	b43legacy_destroy_dmaring(dma->tx_ring1);
	dma->tx_ring1 = NULL;
	b43legacy_destroy_dmaring(dma->tx_ring0);
	dma->tx_ring0 = NULL;
}

int b43legacy_dma_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_dma *dma = &dev->dma;
	struct b43legacy_dmaring *ring;
	enum b43legacy_dmatype type = b43legacy_engine_type(dev);
	int err;

	err = dma_set_mask_and_coherent(dev->dev->dma_dev, DMA_BIT_MASK(type));
	if (err) {
#ifdef CONFIG_B43LEGACY_PIO
		b43legacywarn(dev->wl, "DMA for this device not supported. "
			"Falling back to PIO\n");
		dev->__using_pio = true;
		return -EAGAIN;
#else
		b43legacyerr(dev->wl, "DMA for this device not supported and "
		       "no PIO support compiled in\n");
		return -EOPNOTSUPP;
#endif
	}
	dma->translation = ssb_dma_translation(dev->dev);

	err = -ENOMEM;
	/* setup TX DMA channels. */
	ring = b43legacy_setup_dmaring(dev, 0, 1, type);
	if (!ring)
		goto out;
	dma->tx_ring0 = ring;

	ring = b43legacy_setup_dmaring(dev, 1, 1, type);
	if (!ring)
		goto err_destroy_tx0;
	dma->tx_ring1 = ring;

	ring = b43legacy_setup_dmaring(dev, 2, 1, type);
	if (!ring)
		goto err_destroy_tx1;
	dma->tx_ring2 = ring;

	ring = b43legacy_setup_dmaring(dev, 3, 1, type);
	if (!ring)
		goto err_destroy_tx2;
	dma->tx_ring3 = ring;

	ring = b43legacy_setup_dmaring(dev, 4, 1, type);
	if (!ring)
		goto err_destroy_tx3;
	dma->tx_ring4 = ring;

	ring = b43legacy_setup_dmaring(dev, 5, 1, type);
	if (!ring)
		goto err_destroy_tx4;
	dma->tx_ring5 = ring;

	/* setup RX DMA channels. */
	ring = b43legacy_setup_dmaring(dev, 0, 0, type);
	if (!ring)
		goto err_destroy_tx5;
	dma->rx_ring0 = ring;

	if (dev->dev->id.revision < 5) {
		ring = b43legacy_setup_dmaring(dev, 3, 0, type);
		if (!ring)
			goto err_destroy_rx0;
		dma->rx_ring3 = ring;
	}

	b43legacydbg(dev->wl, "%u-bit DMA initialized\n", (unsigned int)type);
	err = 0;
out:
	return err;

err_destroy_rx0:
	b43legacy_destroy_dmaring(dma->rx_ring0);
	dma->rx_ring0 = NULL;
err_destroy_tx5:
	b43legacy_destroy_dmaring(dma->tx_ring5);
	dma->tx_ring5 = NULL;
err_destroy_tx4:
	b43legacy_destroy_dmaring(dma->tx_ring4);
	dma->tx_ring4 = NULL;
err_destroy_tx3:
	b43legacy_destroy_dmaring(dma->tx_ring3);
	dma->tx_ring3 = NULL;
err_destroy_tx2:
	b43legacy_destroy_dmaring(dma->tx_ring2);
	dma->tx_ring2 = NULL;
err_destroy_tx1:
	b43legacy_destroy_dmaring(dma->tx_ring1);
	dma->tx_ring1 = NULL;
err_destroy_tx0:
	b43legacy_destroy_dmaring(dma->tx_ring0);
	dma->tx_ring0 = NULL;
	goto out;
}

/* Generate a cookie for the TX header. */
static u16 generate_cookie(struct b43legacy_dmaring *ring,
			   int slot)
{
	u16 cookie = 0x1000;

	/* Use the upper 4 bits of the cookie as
	 * DMA controller ID and store the slot number
	 * in the lower 12 bits.
	 * Note that the cookie must never be 0, as this
	 * is a special value used in RX path.
	 */
	switch (ring->index) {
	case 0:
		cookie = 0xA000;
		break;
	case 1:
		cookie = 0xB000;
		break;
	case 2:
		cookie = 0xC000;
		break;
	case 3:
		cookie = 0xD000;
		break;
	case 4:
		cookie = 0xE000;
		break;
	case 5:
		cookie = 0xF000;
		break;
	}
	B43legacy_WARN_ON(!(((u16)slot & 0xF000) == 0x0000));
	cookie |= (u16)slot;

	return cookie;
}

/* Inspect a cookie and find out to which controller/slot it belongs. */
static
struct b43legacy_dmaring *parse_cookie(struct b43legacy_wldev *dev,
				      u16 cookie, int *slot)
{
	struct b43legacy_dma *dma = &dev->dma;
	struct b43legacy_dmaring *ring = NULL;

	switch (cookie & 0xF000) {
	case 0xA000:
		ring = dma->tx_ring0;
		break;
	case 0xB000:
		ring = dma->tx_ring1;
		break;
	case 0xC000:
		ring = dma->tx_ring2;
		break;
	case 0xD000:
		ring = dma->tx_ring3;
		break;
	case 0xE000:
		ring = dma->tx_ring4;
		break;
	case 0xF000:
		ring = dma->tx_ring5;
		break;
	default:
		B43legacy_WARN_ON(1);
	}
	*slot = (cookie & 0x0FFF);
	B43legacy_WARN_ON(!(ring && *slot >= 0 && *slot < ring->nr_slots));

	return ring;
}

static int dma_tx_fragment(struct b43legacy_dmaring *ring,
			    struct sk_buff **in_skb)
{
	struct sk_buff *skb = *in_skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 *header;
	int slot, old_top_slot, old_used_slots;
	int err;
	struct b43legacy_dmadesc32 *desc;
	struct b43legacy_dmadesc_meta *meta;
	struct b43legacy_dmadesc_meta *meta_hdr;
	struct sk_buff *bounce_skb;

#define SLOTS_PER_PACKET  2
	B43legacy_WARN_ON(skb_shinfo(skb)->nr_frags != 0);

	old_top_slot = ring->current_slot;
	old_used_slots = ring->used_slots;

	/* Get a slot for the header. */
	slot = request_slot(ring);
	desc = op32_idx2desc(ring, slot, &meta_hdr);
	memset(meta_hdr, 0, sizeof(*meta_hdr));

	header = &(ring->txhdr_cache[slot * sizeof(
			       struct b43legacy_txhdr_fw3)]);
	err = b43legacy_generate_txhdr(ring->dev, header,
				 skb->data, skb->len, info,
				 generate_cookie(ring, slot));
	if (unlikely(err)) {
		ring->current_slot = old_top_slot;
		ring->used_slots = old_used_slots;
		return err;
	}

	meta_hdr->dmaaddr = map_descbuffer(ring, (unsigned char *)header,
					   sizeof(struct b43legacy_txhdr_fw3), 1);
	if (b43legacy_dma_mapping_error(ring, meta_hdr->dmaaddr,
					sizeof(struct b43legacy_txhdr_fw3), 1)) {
		ring->current_slot = old_top_slot;
		ring->used_slots = old_used_slots;
		return -EIO;
	}
	op32_fill_descriptor(ring, desc, meta_hdr->dmaaddr,
			     sizeof(struct b43legacy_txhdr_fw3), 1, 0, 0);

	/* Get a slot for the payload. */
	slot = request_slot(ring);
	desc = op32_idx2desc(ring, slot, &meta);
	memset(meta, 0, sizeof(*meta));

	meta->skb = skb;
	meta->is_last_fragment = true;

	meta->dmaaddr = map_descbuffer(ring, skb->data, skb->len, 1);
	/* create a bounce buffer in zone_dma on mapping failure. */
	if (b43legacy_dma_mapping_error(ring, meta->dmaaddr, skb->len, 1)) {
		bounce_skb = alloc_skb(skb->len, GFP_KERNEL | GFP_DMA);
		if (!bounce_skb) {
			ring->current_slot = old_top_slot;
			ring->used_slots = old_used_slots;
			err = -ENOMEM;
			goto out_unmap_hdr;
		}

		skb_put_data(bounce_skb, skb->data, skb->len);
		memcpy(bounce_skb->cb, skb->cb, sizeof(skb->cb));
		bounce_skb->dev = skb->dev;
		skb_set_queue_mapping(bounce_skb, skb_get_queue_mapping(skb));
		info = IEEE80211_SKB_CB(bounce_skb);

		dev_kfree_skb_any(skb);
		skb = bounce_skb;
		*in_skb = bounce_skb;
		meta->skb = skb;
		meta->dmaaddr = map_descbuffer(ring, skb->data, skb->len, 1);
		if (b43legacy_dma_mapping_error(ring, meta->dmaaddr, skb->len, 1)) {
			ring->current_slot = old_top_slot;
			ring->used_slots = old_used_slots;
			err = -EIO;
			goto out_free_bounce;
		}
	}

	op32_fill_descriptor(ring, desc, meta->dmaaddr,
			     skb->len, 0, 1, 1);

	wmb();	/* previous stuff MUST be done */
	/* Now transfer the whole frame. */
	op32_poke_tx(ring, next_slot(ring, slot));
	return 0;

out_free_bounce:
	dev_kfree_skb_any(skb);
out_unmap_hdr:
	unmap_descbuffer(ring, meta_hdr->dmaaddr,
			 sizeof(struct b43legacy_txhdr_fw3), 1);
	return err;
}

static inline
int should_inject_overflow(struct b43legacy_dmaring *ring)
{
#ifdef CONFIG_B43LEGACY_DEBUG
	if (unlikely(b43legacy_debug(ring->dev,
				     B43legacy_DBG_DMAOVERFLOW))) {
		/* Check if we should inject another ringbuffer overflow
		 * to test handling of this situation in the stack. */
		unsigned long next_overflow;

		next_overflow = ring->last_injected_overflow + HZ;
		if (time_after(jiffies, next_overflow)) {
			ring->last_injected_overflow = jiffies;
			b43legacydbg(ring->dev->wl,
			       "Injecting TX ring overflow on "
			       "DMA controller %d\n", ring->index);
			return 1;
		}
	}
#endif /* CONFIG_B43LEGACY_DEBUG */
	return 0;
}

int b43legacy_dma_tx(struct b43legacy_wldev *dev,
		     struct sk_buff *skb)
{
	struct b43legacy_dmaring *ring;
	int err = 0;

	ring = priority_to_txring(dev, skb_get_queue_mapping(skb));
	B43legacy_WARN_ON(!ring->tx);

	if (unlikely(ring->stopped)) {
		/* We get here only because of a bug in mac80211.
		 * Because of a race, one packet may be queued after
		 * the queue is stopped, thus we got called when we shouldn't.
		 * For now, just refuse the transmit. */
		if (b43legacy_debug(dev, B43legacy_DBG_DMAVERBOSE))
			b43legacyerr(dev->wl, "Packet after queue stopped\n");
		return -ENOSPC;
	}

	if (WARN_ON(free_slots(ring) < SLOTS_PER_PACKET)) {
		/* If we get here, we have a real error with the queue
		 * full, but queues not stopped. */
		b43legacyerr(dev->wl, "DMA queue overflow\n");
		return -ENOSPC;
	}

	/* dma_tx_fragment might reallocate the skb, so invalidate pointers pointing
	 * into the skb data or cb now. */
	err = dma_tx_fragment(ring, &skb);
	if (unlikely(err == -ENOKEY)) {
		/* Drop this packet, as we don't have the encryption key
		 * anymore and must not transmit it unencrypted. */
		dev_kfree_skb_any(skb);
		return 0;
	}
	if (unlikely(err)) {
		b43legacyerr(dev->wl, "DMA tx mapping failure\n");
		return err;
	}
	if ((free_slots(ring) < SLOTS_PER_PACKET) ||
	    should_inject_overflow(ring)) {
		/* This TX ring is full. */
		unsigned int skb_mapping = skb_get_queue_mapping(skb);
		ieee80211_stop_queue(dev->wl->hw, skb_mapping);
		dev->wl->tx_queue_stopped[skb_mapping] = 1;
		ring->stopped = true;
		if (b43legacy_debug(dev, B43legacy_DBG_DMAVERBOSE))
			b43legacydbg(dev->wl, "Stopped TX ring %d\n",
			       ring->index);
	}
	return err;
}

void b43legacy_dma_handle_txstatus(struct b43legacy_wldev *dev,
				 const struct b43legacy_txstatus *status)
{
	struct b43legacy_dmaring *ring;
	struct b43legacy_dmadesc_meta *meta;
	int retry_limit;
	int slot;
	int firstused;

	ring = parse_cookie(dev, status->cookie, &slot);
	if (unlikely(!ring))
		return;
	B43legacy_WARN_ON(!ring->tx);

	/* Sanity check: TX packets are processed in-order on one ring.
	 * Check if the slot deduced from the cookie really is the first
	 * used slot. */
	firstused = ring->current_slot - ring->used_slots + 1;
	if (firstused < 0)
		firstused = ring->nr_slots + firstused;
	if (unlikely(slot != firstused)) {
		/* This possibly is a firmware bug and will result in
		 * malfunction, memory leaks and/or stall of DMA functionality.
		 */
		b43legacydbg(dev->wl, "Out of order TX status report on DMA "
			     "ring %d. Expected %d, but got %d\n",
			     ring->index, firstused, slot);
		return;
	}

	while (1) {
		B43legacy_WARN_ON(!(slot >= 0 && slot < ring->nr_slots));
		op32_idx2desc(ring, slot, &meta);

		if (meta->skb)
			unmap_descbuffer(ring, meta->dmaaddr,
					 meta->skb->len, 1);
		else
			unmap_descbuffer(ring, meta->dmaaddr,
					 sizeof(struct b43legacy_txhdr_fw3),
					 1);

		if (meta->is_last_fragment) {
			struct ieee80211_tx_info *info;
			BUG_ON(!meta->skb);
			info = IEEE80211_SKB_CB(meta->skb);

			/* preserve the confiured retry limit before clearing the status
			 * The xmit function has overwritten the rc's value with the actual
			 * retry limit done by the hardware */
			retry_limit = info->status.rates[0].count;
			ieee80211_tx_info_clear_status(info);

			if (status->acked)
				info->flags |= IEEE80211_TX_STAT_ACK;

			if (status->rts_count > dev->wl->hw->conf.short_frame_max_tx_count) {
				/*
				 * If the short retries (RTS, not data frame) have exceeded
				 * the limit, the hw will not have tried the selected rate,
				 * but will have used the fallback rate instead.
				 * Don't let the rate control count attempts for the selected
				 * rate in this case, otherwise the statistics will be off.
				 */
				info->status.rates[0].count = 0;
				info->status.rates[1].count = status->frame_count;
			} else {
				if (status->frame_count > retry_limit) {
					info->status.rates[0].count = retry_limit;
					info->status.rates[1].count = status->frame_count -
							retry_limit;

				} else {
					info->status.rates[0].count = status->frame_count;
					info->status.rates[1].idx = -1;
				}
			}

			/* Call back to inform the ieee80211 subsystem about the
			 * status of the transmission.
			 * Some fields of txstat are already filled in dma_tx().
			 */
			ieee80211_tx_status_irqsafe(dev->wl->hw, meta->skb);
			/* skb is freed by ieee80211_tx_status_irqsafe() */
			meta->skb = NULL;
		} else {
			/* No need to call free_descriptor_buffer here, as
			 * this is only the txhdr, which is not allocated.
			 */
			B43legacy_WARN_ON(meta->skb != NULL);
		}

		/* Everything unmapped and free'd. So it's not used anymore. */
		ring->used_slots--;

		if (meta->is_last_fragment)
			break;
		slot = next_slot(ring, slot);
	}
	dev->stats.last_tx = jiffies;
	if (ring->stopped) {
		B43legacy_WARN_ON(free_slots(ring) < SLOTS_PER_PACKET);
		ring->stopped = false;
	}

	if (dev->wl->tx_queue_stopped[ring->queue_prio]) {
		dev->wl->tx_queue_stopped[ring->queue_prio] = 0;
	} else {
		/* If the driver queue is running wake the corresponding
		 * mac80211 queue. */
		ieee80211_wake_queue(dev->wl->hw, ring->queue_prio);
		if (b43legacy_debug(dev, B43legacy_DBG_DMAVERBOSE))
			b43legacydbg(dev->wl, "Woke up TX ring %d\n",
				     ring->index);
	}
	/* Add work to the queue. */
	ieee80211_queue_work(dev->wl->hw, &dev->wl->tx_work);
}

static void dma_rx(struct b43legacy_dmaring *ring,
		   int *slot)
{
	struct b43legacy_dmadesc32 *desc;
	struct b43legacy_dmadesc_meta *meta;
	struct b43legacy_rxhdr_fw3 *rxhdr;
	struct sk_buff *skb;
	u16 len;
	int err;
	dma_addr_t dmaaddr;

	desc = op32_idx2desc(ring, *slot, &meta);

	sync_descbuffer_for_cpu(ring, meta->dmaaddr, ring->rx_buffersize);
	skb = meta->skb;

	if (ring->index == 3) {
		/* We received an xmit status. */
		struct b43legacy_hwtxstatus *hw =
				(struct b43legacy_hwtxstatus *)skb->data;
		int i = 0;

		while (hw->cookie == 0) {
			if (i > 100)
				break;
			i++;
			udelay(2);
			barrier();
		}
		b43legacy_handle_hwtxstatus(ring->dev, hw);
		/* recycle the descriptor buffer. */
		sync_descbuffer_for_device(ring, meta->dmaaddr,
					   ring->rx_buffersize);

		return;
	}
	rxhdr = (struct b43legacy_rxhdr_fw3 *)skb->data;
	len = le16_to_cpu(rxhdr->frame_len);
	if (len == 0) {
		int i = 0;

		do {
			udelay(2);
			barrier();
			len = le16_to_cpu(rxhdr->frame_len);
		} while (len == 0 && i++ < 5);
		if (unlikely(len == 0)) {
			/* recycle the descriptor buffer. */
			sync_descbuffer_for_device(ring, meta->dmaaddr,
						   ring->rx_buffersize);
			goto drop;
		}
	}
	if (unlikely(len > ring->rx_buffersize)) {
		/* The data did not fit into one descriptor buffer
		 * and is split over multiple buffers.
		 * This should never happen, as we try to allocate buffers
		 * big enough. So simply ignore this packet.
		 */
		int cnt = 0;
		s32 tmp = len;

		while (1) {
			desc = op32_idx2desc(ring, *slot, &meta);
			/* recycle the descriptor buffer. */
			sync_descbuffer_for_device(ring, meta->dmaaddr,
						   ring->rx_buffersize);
			*slot = next_slot(ring, *slot);
			cnt++;
			tmp -= ring->rx_buffersize;
			if (tmp <= 0)
				break;
		}
		b43legacyerr(ring->dev->wl, "DMA RX buffer too small "
		       "(len: %u, buffer: %u, nr-dropped: %d)\n",
		       len, ring->rx_buffersize, cnt);
		goto drop;
	}

	dmaaddr = meta->dmaaddr;
	err = setup_rx_descbuffer(ring, desc, meta, GFP_ATOMIC);
	if (unlikely(err)) {
		b43legacydbg(ring->dev->wl, "DMA RX: setup_rx_descbuffer()"
			     " failed\n");
		sync_descbuffer_for_device(ring, dmaaddr,
					   ring->rx_buffersize);
		goto drop;
	}

	unmap_descbuffer(ring, dmaaddr, ring->rx_buffersize, 0);
	skb_put(skb, len + ring->frameoffset);
	skb_pull(skb, ring->frameoffset);

	b43legacy_rx(ring->dev, skb, rxhdr);
drop:
	return;
}

void b43legacy_dma_rx(struct b43legacy_dmaring *ring)
{
	int slot;
	int current_slot;
	int used_slots = 0;

	B43legacy_WARN_ON(ring->tx);
	current_slot = op32_get_current_rxslot(ring);
	B43legacy_WARN_ON(!(current_slot >= 0 && current_slot <
			   ring->nr_slots));

	slot = ring->current_slot;
	for (; slot != current_slot; slot = next_slot(ring, slot)) {
		dma_rx(ring, &slot);
		update_max_used_slots(ring, ++used_slots);
	}
	op32_set_current_rxslot(ring, slot);
	ring->current_slot = slot;
}

static void b43legacy_dma_tx_suspend_ring(struct b43legacy_dmaring *ring)
{
	B43legacy_WARN_ON(!ring->tx);
	op32_tx_suspend(ring);
}

static void b43legacy_dma_tx_resume_ring(struct b43legacy_dmaring *ring)
{
	B43legacy_WARN_ON(!ring->tx);
	op32_tx_resume(ring);
}

void b43legacy_dma_tx_suspend(struct b43legacy_wldev *dev)
{
	b43legacy_power_saving_ctl_bits(dev, -1, 1);
	b43legacy_dma_tx_suspend_ring(dev->dma.tx_ring0);
	b43legacy_dma_tx_suspend_ring(dev->dma.tx_ring1);
	b43legacy_dma_tx_suspend_ring(dev->dma.tx_ring2);
	b43legacy_dma_tx_suspend_ring(dev->dma.tx_ring3);
	b43legacy_dma_tx_suspend_ring(dev->dma.tx_ring4);
	b43legacy_dma_tx_suspend_ring(dev->dma.tx_ring5);
}

void b43legacy_dma_tx_resume(struct b43legacy_wldev *dev)
{
	b43legacy_dma_tx_resume_ring(dev->dma.tx_ring5);
	b43legacy_dma_tx_resume_ring(dev->dma.tx_ring4);
	b43legacy_dma_tx_resume_ring(dev->dma.tx_ring3);
	b43legacy_dma_tx_resume_ring(dev->dma.tx_ring2);
	b43legacy_dma_tx_resume_ring(dev->dma.tx_ring1);
	b43legacy_dma_tx_resume_ring(dev->dma.tx_ring0);
	b43legacy_power_saving_ctl_bits(dev, -1, -1);
}
