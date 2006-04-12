/*

  Broadcom BCM43xx wireless driver

  DMA ringbuffer and descriptor allocation/management

  Copyright (c) 2005 Michael Buesch <mbuesch@freenet.de>

  Some code in this file is derived from the b44.c driver
  Copyright (C) 2002 David S. Miller
  Copyright (C) Pekka Pietikainen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "bcm43xx.h"
#include "bcm43xx_dma.h"
#include "bcm43xx_main.h"
#include "bcm43xx_debugfs.h"
#include "bcm43xx_power.h"
#include "bcm43xx_xmit.h"

#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/skbuff.h>


static inline int free_slots(struct bcm43xx_dmaring *ring)
{
	return (ring->nr_slots - ring->used_slots);
}

static inline int next_slot(struct bcm43xx_dmaring *ring, int slot)
{
	assert(slot >= -1 && slot <= ring->nr_slots - 1);
	if (slot == ring->nr_slots - 1)
		return 0;
	return slot + 1;
}

static inline int prev_slot(struct bcm43xx_dmaring *ring, int slot)
{
	assert(slot >= 0 && slot <= ring->nr_slots - 1);
	if (slot == 0)
		return ring->nr_slots - 1;
	return slot - 1;
}

/* Request a slot for usage. */
static inline
int request_slot(struct bcm43xx_dmaring *ring)
{
	int slot;

	assert(ring->tx);
	assert(!ring->suspended);
	assert(free_slots(ring) != 0);

	slot = next_slot(ring, ring->current_slot);
	ring->current_slot = slot;
	ring->used_slots++;

	/* Check the number of available slots and suspend TX,
	 * if we are running low on free slots.
	 */
	if (unlikely(free_slots(ring) < ring->suspend_mark)) {
		netif_stop_queue(ring->bcm->net_dev);
		ring->suspended = 1;
	}
#ifdef CONFIG_BCM43XX_DEBUG
	if (ring->used_slots > ring->max_used_slots)
		ring->max_used_slots = ring->used_slots;
#endif /* CONFIG_BCM43XX_DEBUG*/

	return slot;
}

/* Return a slot to the free slots. */
static inline
void return_slot(struct bcm43xx_dmaring *ring, int slot)
{
	assert(ring->tx);

	ring->used_slots--;

	/* Check if TX is suspended and check if we have
	 * enough free slots to resume it again.
	 */
	if (unlikely(ring->suspended)) {
		if (free_slots(ring) >= ring->resume_mark) {
			ring->suspended = 0;
			netif_wake_queue(ring->bcm->net_dev);
		}
	}
}

static inline
dma_addr_t map_descbuffer(struct bcm43xx_dmaring *ring,
			  unsigned char *buf,
			  size_t len,
			  int tx)
{
	dma_addr_t dmaaddr;

	if (tx) {
		dmaaddr = dma_map_single(&ring->bcm->pci_dev->dev,
					 buf, len,
					 DMA_TO_DEVICE);
	} else {
		dmaaddr = dma_map_single(&ring->bcm->pci_dev->dev,
					 buf, len,
					 DMA_FROM_DEVICE);
	}

	return dmaaddr;
}

static inline
void unmap_descbuffer(struct bcm43xx_dmaring *ring,
		      dma_addr_t addr,
		      size_t len,
		      int tx)
{
	if (tx) {
		dma_unmap_single(&ring->bcm->pci_dev->dev,
				 addr, len,
				 DMA_TO_DEVICE);
	} else {
		dma_unmap_single(&ring->bcm->pci_dev->dev,
				 addr, len,
				 DMA_FROM_DEVICE);
	}
}

static inline
void sync_descbuffer_for_cpu(struct bcm43xx_dmaring *ring,
			     dma_addr_t addr,
			     size_t len)
{
	assert(!ring->tx);

	dma_sync_single_for_cpu(&ring->bcm->pci_dev->dev,
				addr, len, DMA_FROM_DEVICE);
}

static inline
void sync_descbuffer_for_device(struct bcm43xx_dmaring *ring,
				dma_addr_t addr,
				size_t len)
{
	assert(!ring->tx);

	dma_sync_single_for_device(&ring->bcm->pci_dev->dev,
				   addr, len, DMA_FROM_DEVICE);
}

/* Unmap and free a descriptor buffer. */
static inline
void free_descriptor_buffer(struct bcm43xx_dmaring *ring,
			    struct bcm43xx_dmadesc *desc,
			    struct bcm43xx_dmadesc_meta *meta,
			    int irq_context)
{
	assert(meta->skb);
	if (irq_context)
		dev_kfree_skb_irq(meta->skb);
	else
		dev_kfree_skb(meta->skb);
	meta->skb = NULL;
}

static int alloc_ringmemory(struct bcm43xx_dmaring *ring)
{
	struct device *dev = &(ring->bcm->pci_dev->dev);

	ring->vbase = dma_alloc_coherent(dev, BCM43xx_DMA_RINGMEMSIZE,
					 &(ring->dmabase), GFP_KERNEL);
	if (!ring->vbase) {
		printk(KERN_ERR PFX "DMA ringmemory allocation failed\n");
		return -ENOMEM;
	}
	if (ring->dmabase + BCM43xx_DMA_RINGMEMSIZE > BCM43xx_DMA_BUSADDRMAX) {
		printk(KERN_ERR PFX ">>>FATAL ERROR<<<  DMA RINGMEMORY >1G "
				    "(0x%08x, len: %lu)\n",
		       ring->dmabase, BCM43xx_DMA_RINGMEMSIZE);
		dma_free_coherent(dev, BCM43xx_DMA_RINGMEMSIZE,
				  ring->vbase, ring->dmabase);
		return -ENOMEM;
	}
	assert(!(ring->dmabase & 0x000003FF));
	memset(ring->vbase, 0, BCM43xx_DMA_RINGMEMSIZE);

	return 0;
}

static void free_ringmemory(struct bcm43xx_dmaring *ring)
{
	struct device *dev = &(ring->bcm->pci_dev->dev);

	dma_free_coherent(dev, BCM43xx_DMA_RINGMEMSIZE,
			  ring->vbase, ring->dmabase);
}

/* Reset the RX DMA channel */
int bcm43xx_dmacontroller_rx_reset(struct bcm43xx_private *bcm,
				   u16 mmio_base)
{
	int i;
	u32 value;

	bcm43xx_write32(bcm,
			mmio_base + BCM43xx_DMA_RX_CONTROL,
			0x00000000);
	for (i = 0; i < 1000; i++) {
		value = bcm43xx_read32(bcm,
				       mmio_base + BCM43xx_DMA_RX_STATUS);
		value &= BCM43xx_DMA_RXSTAT_STAT_MASK;
		if (value == BCM43xx_DMA_RXSTAT_STAT_DISABLED) {
			i = -1;
			break;
		}
		udelay(10);
	}
	if (i != -1) {
		printk(KERN_ERR PFX "Error: Wait on DMA RX status timed out.\n");
		return -ENODEV;
	}

	return 0;
}

/* Reset the RX DMA channel */
int bcm43xx_dmacontroller_tx_reset(struct bcm43xx_private *bcm,
				   u16 mmio_base)
{
	int i;
	u32 value;

	for (i = 0; i < 1000; i++) {
		value = bcm43xx_read32(bcm,
				       mmio_base + BCM43xx_DMA_TX_STATUS);
		value &= BCM43xx_DMA_TXSTAT_STAT_MASK;
		if (value == BCM43xx_DMA_TXSTAT_STAT_DISABLED ||
		    value == BCM43xx_DMA_TXSTAT_STAT_IDLEWAIT ||
		    value == BCM43xx_DMA_TXSTAT_STAT_STOPPED)
			break;
		udelay(10);
	}
	bcm43xx_write32(bcm,
			mmio_base + BCM43xx_DMA_TX_CONTROL,
			0x00000000);
	for (i = 0; i < 1000; i++) {
		value = bcm43xx_read32(bcm,
				       mmio_base + BCM43xx_DMA_TX_STATUS);
		value &= BCM43xx_DMA_TXSTAT_STAT_MASK;
		if (value == BCM43xx_DMA_TXSTAT_STAT_DISABLED) {
			i = -1;
			break;
		}
		udelay(10);
	}
	if (i != -1) {
		printk(KERN_ERR PFX "Error: Wait on DMA TX status timed out.\n");
		return -ENODEV;
	}
	/* ensure the reset is completed. */
	udelay(300);

	return 0;
}

static int setup_rx_descbuffer(struct bcm43xx_dmaring *ring,
			       struct bcm43xx_dmadesc *desc,
			       struct bcm43xx_dmadesc_meta *meta,
			       gfp_t gfp_flags)
{
	struct bcm43xx_rxhdr *rxhdr;
	dma_addr_t dmaaddr;
	u32 desc_addr;
	u32 desc_ctl;
	const int slot = (int)(desc - ring->vbase);
	struct sk_buff *skb;

	assert(slot >= 0 && slot < ring->nr_slots);
	assert(!ring->tx);

	skb = __dev_alloc_skb(ring->rx_buffersize, gfp_flags);
	if (unlikely(!skb))
		return -ENOMEM;
	dmaaddr = map_descbuffer(ring, skb->data, ring->rx_buffersize, 0);
	if (unlikely(dmaaddr + ring->rx_buffersize > BCM43xx_DMA_BUSADDRMAX)) {
		unmap_descbuffer(ring, dmaaddr, ring->rx_buffersize, 0);
		dev_kfree_skb_any(skb);
		printk(KERN_ERR PFX ">>>FATAL ERROR<<<  DMA RX SKB >1G "
				    "(0x%08x, len: %u)\n",
		       dmaaddr, ring->rx_buffersize);
		return -ENOMEM;
	}
	meta->skb = skb;
	meta->dmaaddr = dmaaddr;
	skb->dev = ring->bcm->net_dev;
	desc_addr = (u32)(dmaaddr + ring->memoffset);
	desc_ctl = (BCM43xx_DMADTOR_BYTECNT_MASK &
		    (u32)(ring->rx_buffersize - ring->frameoffset));
	if (slot == ring->nr_slots - 1)
		desc_ctl |= BCM43xx_DMADTOR_DTABLEEND;
	set_desc_addr(desc, desc_addr);
	set_desc_ctl(desc, desc_ctl);

	rxhdr = (struct bcm43xx_rxhdr *)(skb->data);
	rxhdr->frame_length = 0;
	rxhdr->flags1 = 0;

	return 0;
}

/* Allocate the initial descbuffers.
 * This is used for an RX ring only.
 */
static int alloc_initial_descbuffers(struct bcm43xx_dmaring *ring)
{
	int i, err = -ENOMEM;
	struct bcm43xx_dmadesc *desc;
	struct bcm43xx_dmadesc_meta *meta;

	for (i = 0; i < ring->nr_slots; i++) {
		desc = ring->vbase + i;
		meta = ring->meta + i;

		err = setup_rx_descbuffer(ring, desc, meta, GFP_KERNEL);
		if (err)
			goto err_unwind;
	}
	ring->used_slots = ring->nr_slots;
	err = 0;
out:
	return err;

err_unwind:
	for (i--; i >= 0; i--) {
		desc = ring->vbase + i;
		meta = ring->meta + i;

		unmap_descbuffer(ring, meta->dmaaddr, ring->rx_buffersize, 0);
		dev_kfree_skb(meta->skb);
	}
	goto out;
}

/* Do initial setup of the DMA controller.
 * Reset the controller, write the ring busaddress
 * and switch the "enable" bit on.
 */
static int dmacontroller_setup(struct bcm43xx_dmaring *ring)
{
	int err = 0;
	u32 value;

	if (ring->tx) {
		/* Set Transmit Control register to "transmit enable" */
		bcm43xx_dma_write(ring, BCM43xx_DMA_TX_CONTROL,
				  BCM43xx_DMA_TXCTRL_ENABLE);
		/* Set Transmit Descriptor ring address. */
		bcm43xx_dma_write(ring, BCM43xx_DMA_TX_DESC_RING,
				  ring->dmabase + ring->memoffset);
	} else {
		err = alloc_initial_descbuffers(ring);
		if (err)
			goto out;
		/* Set Receive Control "receive enable" and frame offset */
		value = (ring->frameoffset << BCM43xx_DMA_RXCTRL_FRAMEOFF_SHIFT);
		value |= BCM43xx_DMA_RXCTRL_ENABLE;
		bcm43xx_dma_write(ring, BCM43xx_DMA_RX_CONTROL, value);
		/* Set Receive Descriptor ring address. */
		bcm43xx_dma_write(ring, BCM43xx_DMA_RX_DESC_RING,
				  ring->dmabase + ring->memoffset);
		/* Init the descriptor pointer. */
		bcm43xx_dma_write(ring, BCM43xx_DMA_RX_DESC_INDEX, 200);
	}

out:
	return err;
}

/* Shutdown the DMA controller. */
static void dmacontroller_cleanup(struct bcm43xx_dmaring *ring)
{
	if (ring->tx) {
		bcm43xx_dmacontroller_tx_reset(ring->bcm, ring->mmio_base);
		/* Zero out Transmit Descriptor ring address. */
		bcm43xx_dma_write(ring, BCM43xx_DMA_TX_DESC_RING, 0);
	} else {
		bcm43xx_dmacontroller_rx_reset(ring->bcm, ring->mmio_base);
		/* Zero out Receive Descriptor ring address. */
		bcm43xx_dma_write(ring, BCM43xx_DMA_RX_DESC_RING, 0);
	}
}

static void free_all_descbuffers(struct bcm43xx_dmaring *ring)
{
	struct bcm43xx_dmadesc *desc;
	struct bcm43xx_dmadesc_meta *meta;
	int i;

	if (!ring->used_slots)
		return;
	for (i = 0; i < ring->nr_slots; i++) {
		desc = ring->vbase + i;
		meta = ring->meta + i;

		if (!meta->skb) {
			assert(ring->tx);
			continue;
		}
		if (ring->tx) {
			unmap_descbuffer(ring, meta->dmaaddr,
					 meta->skb->len, 1);
		} else {
			unmap_descbuffer(ring, meta->dmaaddr,
					 ring->rx_buffersize, 0);
		}
		free_descriptor_buffer(ring, desc, meta, 0);
	}
}

/* Main initialization function. */
static
struct bcm43xx_dmaring * bcm43xx_setup_dmaring(struct bcm43xx_private *bcm,
					       u16 dma_controller_base,
					       int nr_descriptor_slots,
					       int tx)
{
	struct bcm43xx_dmaring *ring;
	int err;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		goto out;

	ring->meta = kzalloc(sizeof(*ring->meta) * nr_descriptor_slots,
			     GFP_KERNEL);
	if (!ring->meta)
		goto err_kfree_ring;

	ring->memoffset = BCM43xx_DMA_DMABUSADDROFFSET;
#ifdef CONFIG_BCM947XX
	if (bcm->pci_dev->bus->number == 0)
		ring->memoffset = 0;
#endif

	ring->bcm = bcm;
	ring->nr_slots = nr_descriptor_slots;
	ring->suspend_mark = ring->nr_slots * BCM43xx_TXSUSPEND_PERCENT / 100;
	ring->resume_mark = ring->nr_slots * BCM43xx_TXRESUME_PERCENT / 100;
	assert(ring->suspend_mark < ring->resume_mark);
	ring->mmio_base = dma_controller_base;
	if (tx) {
		ring->tx = 1;
		ring->current_slot = -1;
	} else {
		switch (dma_controller_base) {
		case BCM43xx_MMIO_DMA1_BASE:
			ring->rx_buffersize = BCM43xx_DMA1_RXBUFFERSIZE;
			ring->frameoffset = BCM43xx_DMA1_RX_FRAMEOFFSET;
			break;
		case BCM43xx_MMIO_DMA4_BASE:
			ring->rx_buffersize = BCM43xx_DMA4_RXBUFFERSIZE;
			ring->frameoffset = BCM43xx_DMA4_RX_FRAMEOFFSET;
			break;
		default:
			assert(0);
		}
	}

	err = alloc_ringmemory(ring);
	if (err)
		goto err_kfree_meta;
	err = dmacontroller_setup(ring);
	if (err)
		goto err_free_ringmemory;

out:
	return ring;

err_free_ringmemory:
	free_ringmemory(ring);
err_kfree_meta:
	kfree(ring->meta);
err_kfree_ring:
	kfree(ring);
	ring = NULL;
	goto out;
}

/* Main cleanup function. */
static void bcm43xx_destroy_dmaring(struct bcm43xx_dmaring *ring)
{
	if (!ring)
		return;

	dprintk(KERN_INFO PFX "DMA 0x%04x (%s) max used slots: %d/%d\n",
		ring->mmio_base,
		(ring->tx) ? "TX" : "RX",
		ring->max_used_slots, ring->nr_slots);
	/* Device IRQs are disabled prior entering this function,
	 * so no need to take care of concurrency with rx handler stuff.
	 */
	dmacontroller_cleanup(ring);
	free_all_descbuffers(ring);
	free_ringmemory(ring);

	kfree(ring->meta);
	kfree(ring);
}

void bcm43xx_dma_free(struct bcm43xx_private *bcm)
{
	struct bcm43xx_dma *dma;

	if (bcm43xx_using_pio(bcm))
		return;
	dma = bcm43xx_current_dma(bcm);

	bcm43xx_destroy_dmaring(dma->rx_ring1);
	dma->rx_ring1 = NULL;
	bcm43xx_destroy_dmaring(dma->rx_ring0);
	dma->rx_ring0 = NULL;
	bcm43xx_destroy_dmaring(dma->tx_ring3);
	dma->tx_ring3 = NULL;
	bcm43xx_destroy_dmaring(dma->tx_ring2);
	dma->tx_ring2 = NULL;
	bcm43xx_destroy_dmaring(dma->tx_ring1);
	dma->tx_ring1 = NULL;
	bcm43xx_destroy_dmaring(dma->tx_ring0);
	dma->tx_ring0 = NULL;
}

int bcm43xx_dma_init(struct bcm43xx_private *bcm)
{
	struct bcm43xx_dma *dma = bcm43xx_current_dma(bcm);
	struct bcm43xx_dmaring *ring;
	int err = -ENOMEM;

	/* setup TX DMA channels. */
	ring = bcm43xx_setup_dmaring(bcm, BCM43xx_MMIO_DMA1_BASE,
				     BCM43xx_TXRING_SLOTS, 1);
	if (!ring)
		goto out;
	dma->tx_ring0 = ring;

	ring = bcm43xx_setup_dmaring(bcm, BCM43xx_MMIO_DMA2_BASE,
				     BCM43xx_TXRING_SLOTS, 1);
	if (!ring)
		goto err_destroy_tx0;
	dma->tx_ring1 = ring;

	ring = bcm43xx_setup_dmaring(bcm, BCM43xx_MMIO_DMA3_BASE,
				     BCM43xx_TXRING_SLOTS, 1);
	if (!ring)
		goto err_destroy_tx1;
	dma->tx_ring2 = ring;

	ring = bcm43xx_setup_dmaring(bcm, BCM43xx_MMIO_DMA4_BASE,
				     BCM43xx_TXRING_SLOTS, 1);
	if (!ring)
		goto err_destroy_tx2;
	dma->tx_ring3 = ring;

	/* setup RX DMA channels. */
	ring = bcm43xx_setup_dmaring(bcm, BCM43xx_MMIO_DMA1_BASE,
				     BCM43xx_RXRING_SLOTS, 0);
	if (!ring)
		goto err_destroy_tx3;
	dma->rx_ring0 = ring;

	if (bcm->current_core->rev < 5) {
		ring = bcm43xx_setup_dmaring(bcm, BCM43xx_MMIO_DMA4_BASE,
					     BCM43xx_RXRING_SLOTS, 0);
		if (!ring)
			goto err_destroy_rx0;
		dma->rx_ring1 = ring;
	}

	dprintk(KERN_INFO PFX "DMA initialized\n");
	err = 0;
out:
	return err;

err_destroy_rx0:
	bcm43xx_destroy_dmaring(dma->rx_ring0);
	dma->rx_ring0 = NULL;
err_destroy_tx3:
	bcm43xx_destroy_dmaring(dma->tx_ring3);
	dma->tx_ring3 = NULL;
err_destroy_tx2:
	bcm43xx_destroy_dmaring(dma->tx_ring2);
	dma->tx_ring2 = NULL;
err_destroy_tx1:
	bcm43xx_destroy_dmaring(dma->tx_ring1);
	dma->tx_ring1 = NULL;
err_destroy_tx0:
	bcm43xx_destroy_dmaring(dma->tx_ring0);
	dma->tx_ring0 = NULL;
	goto out;
}

/* Generate a cookie for the TX header. */
static u16 generate_cookie(struct bcm43xx_dmaring *ring,
			   int slot)
{
	u16 cookie = 0x0000;

	/* Use the upper 4 bits of the cookie as
	 * DMA controller ID and store the slot number
	 * in the lower 12 bits
	 */
	switch (ring->mmio_base) {
	default:
		assert(0);
	case BCM43xx_MMIO_DMA1_BASE:
		break;
	case BCM43xx_MMIO_DMA2_BASE:
		cookie = 0x1000;
		break;
	case BCM43xx_MMIO_DMA3_BASE:
		cookie = 0x2000;
		break;
	case BCM43xx_MMIO_DMA4_BASE:
		cookie = 0x3000;
		break;
	}
	assert(((u16)slot & 0xF000) == 0x0000);
	cookie |= (u16)slot;

	return cookie;
}

/* Inspect a cookie and find out to which controller/slot it belongs. */
static
struct bcm43xx_dmaring * parse_cookie(struct bcm43xx_private *bcm,
				      u16 cookie, int *slot)
{
	struct bcm43xx_dma *dma = bcm43xx_current_dma(bcm);
	struct bcm43xx_dmaring *ring = NULL;

	switch (cookie & 0xF000) {
	case 0x0000:
		ring = dma->tx_ring0;
		break;
	case 0x1000:
		ring = dma->tx_ring1;
		break;
	case 0x2000:
		ring = dma->tx_ring2;
		break;
	case 0x3000:
		ring = dma->tx_ring3;
		break;
	default:
		assert(0);
	}
	*slot = (cookie & 0x0FFF);
	assert(*slot >= 0 && *slot < ring->nr_slots);

	return ring;
}

static void dmacontroller_poke_tx(struct bcm43xx_dmaring *ring,
				  int slot)
{
	/* Everything is ready to start. Buffers are DMA mapped and
	 * associated with slots.
	 * "slot" is the last slot of the new frame we want to transmit.
	 * Close your seat belts now, please.
	 */
	wmb();
	slot = next_slot(ring, slot);
	bcm43xx_dma_write(ring, BCM43xx_DMA_TX_DESC_INDEX,
			  (u32)(slot * sizeof(struct bcm43xx_dmadesc)));
}

static int dma_tx_fragment(struct bcm43xx_dmaring *ring,
			   struct sk_buff *skb,
			   u8 cur_frag)
{
	int slot;
	struct bcm43xx_dmadesc *desc;
	struct bcm43xx_dmadesc_meta *meta;
	u32 desc_ctl;
	u32 desc_addr;

	assert(skb_shinfo(skb)->nr_frags == 0);

	slot = request_slot(ring);
	desc = ring->vbase + slot;
	meta = ring->meta + slot;

	/* Add a device specific TX header. */
	assert(skb_headroom(skb) >= sizeof(struct bcm43xx_txhdr));
	/* Reserve enough headroom for the device tx header. */
	__skb_push(skb, sizeof(struct bcm43xx_txhdr));
	/* Now calculate and add the tx header.
	 * The tx header includes the PLCP header.
	 */
	bcm43xx_generate_txhdr(ring->bcm,
			       (struct bcm43xx_txhdr *)skb->data,
			       skb->data + sizeof(struct bcm43xx_txhdr),
			       skb->len - sizeof(struct bcm43xx_txhdr),
			       (cur_frag == 0),
			       generate_cookie(ring, slot));

	meta->skb = skb;
	meta->dmaaddr = map_descbuffer(ring, skb->data, skb->len, 1);
	if (unlikely(meta->dmaaddr + skb->len > BCM43xx_DMA_BUSADDRMAX)) {
		return_slot(ring, slot);
		printk(KERN_ERR PFX ">>>FATAL ERROR<<<  DMA TX SKB >1G "
				    "(0x%08x, len: %u)\n",
		       meta->dmaaddr, skb->len);
		return -ENOMEM;
	}

	desc_addr = (u32)(meta->dmaaddr + ring->memoffset);
	desc_ctl = BCM43xx_DMADTOR_FRAMESTART | BCM43xx_DMADTOR_FRAMEEND;
	desc_ctl |= BCM43xx_DMADTOR_COMPIRQ;
	desc_ctl |= (BCM43xx_DMADTOR_BYTECNT_MASK &
		     (u32)(meta->skb->len - ring->frameoffset));
	if (slot == ring->nr_slots - 1)
		desc_ctl |= BCM43xx_DMADTOR_DTABLEEND;

	set_desc_ctl(desc, desc_ctl);
	set_desc_addr(desc, desc_addr);
	/* Now transfer the whole frame. */
	dmacontroller_poke_tx(ring, slot);

	return 0;
}

int bcm43xx_dma_tx(struct bcm43xx_private *bcm,
		   struct ieee80211_txb *txb)
{
	/* We just received a packet from the kernel network subsystem.
	 * Add headers and DMA map the memory. Poke
	 * the device to send the stuff.
	 * Note that this is called from atomic context.
	 */
	struct bcm43xx_dmaring *ring = bcm43xx_current_dma(bcm)->tx_ring1;
	u8 i;
	struct sk_buff *skb;

	assert(ring->tx);
	if (unlikely(free_slots(ring) < txb->nr_frags)) {
		/* The queue should be stopped,
		 * if we are low on free slots.
		 * If this ever triggers, we have to lower the suspend_mark.
		 */
		dprintkl(KERN_ERR PFX "Out of DMA descriptor slots!\n");
		return -ENOMEM;
	}

	for (i = 0; i < txb->nr_frags; i++) {
		skb = txb->fragments[i];
		/* Take skb from ieee80211_txb_free */
		txb->fragments[i] = NULL;
		dma_tx_fragment(ring, skb, i);
		//TODO: handle failure of dma_tx_fragment
	}
	ieee80211_txb_free(txb);

	return 0;
}

void bcm43xx_dma_handle_xmitstatus(struct bcm43xx_private *bcm,
				   struct bcm43xx_xmitstatus *status)
{
	struct bcm43xx_dmaring *ring;
	struct bcm43xx_dmadesc *desc;
	struct bcm43xx_dmadesc_meta *meta;
	int is_last_fragment;
	int slot;

	ring = parse_cookie(bcm, status->cookie, &slot);
	assert(ring);
	assert(ring->tx);
	assert(get_desc_ctl(ring->vbase + slot) & BCM43xx_DMADTOR_FRAMESTART);
	while (1) {
		assert(slot >= 0 && slot < ring->nr_slots);
		desc = ring->vbase + slot;
		meta = ring->meta + slot;

		is_last_fragment = !!(get_desc_ctl(desc) & BCM43xx_DMADTOR_FRAMEEND);
		unmap_descbuffer(ring, meta->dmaaddr, meta->skb->len, 1);
		free_descriptor_buffer(ring, desc, meta, 1);
		/* Everything belonging to the slot is unmapped
		 * and freed, so we can return it.
		 */
		return_slot(ring, slot);

		if (is_last_fragment)
			break;
		slot = next_slot(ring, slot);
	}
	bcm->stats.last_tx = jiffies;
}

static void dma_rx(struct bcm43xx_dmaring *ring,
		   int *slot)
{
	struct bcm43xx_dmadesc *desc;
	struct bcm43xx_dmadesc_meta *meta;
	struct bcm43xx_rxhdr *rxhdr;
	struct sk_buff *skb;
	u16 len;
	int err;
	dma_addr_t dmaaddr;

	desc = ring->vbase + *slot;
	meta = ring->meta + *slot;

	sync_descbuffer_for_cpu(ring, meta->dmaaddr, ring->rx_buffersize);
	skb = meta->skb;

	if (ring->mmio_base == BCM43xx_MMIO_DMA4_BASE) {
		/* We received an xmit status. */
		struct bcm43xx_hwxmitstatus *hw = (struct bcm43xx_hwxmitstatus *)skb->data;
		struct bcm43xx_xmitstatus stat;

		stat.cookie = le16_to_cpu(hw->cookie);
		stat.flags = hw->flags;
		stat.cnt1 = hw->cnt1;
		stat.cnt2 = hw->cnt2;
		stat.seq = le16_to_cpu(hw->seq);
		stat.unknown = le16_to_cpu(hw->unknown);

		bcm43xx_debugfs_log_txstat(ring->bcm, &stat);
		bcm43xx_dma_handle_xmitstatus(ring->bcm, &stat);
		/* recycle the descriptor buffer. */
		sync_descbuffer_for_device(ring, meta->dmaaddr, ring->rx_buffersize);

		return;
	}
	rxhdr = (struct bcm43xx_rxhdr *)skb->data;
	len = le16_to_cpu(rxhdr->frame_length);
	if (len == 0) {
		int i = 0;

		do {
			udelay(2);
			barrier();
			len = le16_to_cpu(rxhdr->frame_length);
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
			desc = ring->vbase + *slot;
			meta = ring->meta + *slot;
			/* recycle the descriptor buffer. */
			sync_descbuffer_for_device(ring, meta->dmaaddr,
						   ring->rx_buffersize);
			*slot = next_slot(ring, *slot);
			cnt++;
			tmp -= ring->rx_buffersize;
			if (tmp <= 0)
				break;
		}
		printkl(KERN_ERR PFX "DMA RX buffer too small "
				     "(len: %u, buffer: %u, nr-dropped: %d)\n",
		        len, ring->rx_buffersize, cnt);
		goto drop;
	}
	len -= IEEE80211_FCS_LEN;

	dmaaddr = meta->dmaaddr;
	err = setup_rx_descbuffer(ring, desc, meta, GFP_ATOMIC);
	if (unlikely(err)) {
		dprintkl(KERN_ERR PFX "DMA RX: setup_rx_descbuffer() failed\n");
		sync_descbuffer_for_device(ring, dmaaddr,
					   ring->rx_buffersize);
		goto drop;
	}

	unmap_descbuffer(ring, dmaaddr, ring->rx_buffersize, 0);
	skb_put(skb, len + ring->frameoffset);
	skb_pull(skb, ring->frameoffset);

	err = bcm43xx_rx(ring->bcm, skb, rxhdr);
	if (err) {
		dev_kfree_skb_irq(skb);
		goto drop;
	}

drop:
	return;
}

void bcm43xx_dma_rx(struct bcm43xx_dmaring *ring)
{
	u32 status;
	u16 descptr;
	int slot, current_slot;
#ifdef CONFIG_BCM43XX_DEBUG
	int used_slots = 0;
#endif

	assert(!ring->tx);
	status = bcm43xx_dma_read(ring, BCM43xx_DMA_RX_STATUS);
	descptr = (status & BCM43xx_DMA_RXSTAT_DPTR_MASK);
	current_slot = descptr / sizeof(struct bcm43xx_dmadesc);
	assert(current_slot >= 0 && current_slot < ring->nr_slots);

	slot = ring->current_slot;
	for ( ; slot != current_slot; slot = next_slot(ring, slot)) {
		dma_rx(ring, &slot);
#ifdef CONFIG_BCM43XX_DEBUG
		if (++used_slots > ring->max_used_slots)
			ring->max_used_slots = used_slots;
#endif
	}
	bcm43xx_dma_write(ring, BCM43xx_DMA_RX_DESC_INDEX,
			  (u32)(slot * sizeof(struct bcm43xx_dmadesc)));
	ring->current_slot = slot;
}

void bcm43xx_dma_tx_suspend(struct bcm43xx_dmaring *ring)
{
	assert(ring->tx);
	bcm43xx_power_saving_ctl_bits(ring->bcm, -1, 1);
	bcm43xx_dma_write(ring, BCM43xx_DMA_TX_CONTROL,
			  bcm43xx_dma_read(ring, BCM43xx_DMA_TX_CONTROL)
			  | BCM43xx_DMA_TXCTRL_SUSPEND);
}

void bcm43xx_dma_tx_resume(struct bcm43xx_dmaring *ring)
{
	assert(ring->tx);
	bcm43xx_dma_write(ring, BCM43xx_DMA_TX_CONTROL,
			  bcm43xx_dma_read(ring, BCM43xx_DMA_TX_CONTROL)
			  & ~BCM43xx_DMA_TXCTRL_SUSPEND);
	bcm43xx_power_saving_ctl_bits(ring->bcm, -1, -1);
}
