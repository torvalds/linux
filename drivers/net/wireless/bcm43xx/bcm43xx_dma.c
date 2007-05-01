/*

  Broadcom BCM43xx wireless driver

  DMA ringbuffer and descriptor allocation/management

  Copyright (c) 2005, 2006 Michael Buesch <mbuesch@freenet.de>

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

u16 bcm43xx_dmacontroller_base(int dma64bit, int controller_idx)
{
	static const u16 map64[] = {
		BCM43xx_MMIO_DMA64_BASE0,
		BCM43xx_MMIO_DMA64_BASE1,
		BCM43xx_MMIO_DMA64_BASE2,
		BCM43xx_MMIO_DMA64_BASE3,
		BCM43xx_MMIO_DMA64_BASE4,
		BCM43xx_MMIO_DMA64_BASE5,
	};
	static const u16 map32[] = {
		BCM43xx_MMIO_DMA32_BASE0,
		BCM43xx_MMIO_DMA32_BASE1,
		BCM43xx_MMIO_DMA32_BASE2,
		BCM43xx_MMIO_DMA32_BASE3,
		BCM43xx_MMIO_DMA32_BASE4,
		BCM43xx_MMIO_DMA32_BASE5,
	};

	if (dma64bit) {
		assert(controller_idx >= 0 &&
		       controller_idx < ARRAY_SIZE(map64));
		return map64[controller_idx];
	}
	assert(controller_idx >= 0 &&
	       controller_idx < ARRAY_SIZE(map32));
	return map32[controller_idx];
}

static inline
dma_addr_t map_descbuffer(struct bcm43xx_dmaring *ring,
			  unsigned char *buf,
			  size_t len,
			  int tx)
{
	dma_addr_t dmaaddr;
	int direction = PCI_DMA_FROMDEVICE;

	if (tx)
		direction = PCI_DMA_TODEVICE;

	dmaaddr = pci_map_single(ring->bcm->pci_dev,
					 buf, len,
					 direction);

	return dmaaddr;
}

static inline
void unmap_descbuffer(struct bcm43xx_dmaring *ring,
		      dma_addr_t addr,
		      size_t len,
		      int tx)
{
	if (tx) {
		pci_unmap_single(ring->bcm->pci_dev,
				 addr, len,
				 PCI_DMA_TODEVICE);
	} else {
		pci_unmap_single(ring->bcm->pci_dev,
				 addr, len,
				 PCI_DMA_FROMDEVICE);
	}
}

static inline
void sync_descbuffer_for_cpu(struct bcm43xx_dmaring *ring,
			     dma_addr_t addr,
			     size_t len)
{
	assert(!ring->tx);

	pci_dma_sync_single_for_cpu(ring->bcm->pci_dev,
				    addr, len, PCI_DMA_FROMDEVICE);
}

static inline
void sync_descbuffer_for_device(struct bcm43xx_dmaring *ring,
				dma_addr_t addr,
				size_t len)
{
	assert(!ring->tx);

	pci_dma_sync_single_for_cpu(ring->bcm->pci_dev,
				    addr, len, PCI_DMA_TODEVICE);
}

/* Unmap and free a descriptor buffer. */
static inline
void free_descriptor_buffer(struct bcm43xx_dmaring *ring,
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
	ring->descbase = pci_alloc_consistent(ring->bcm->pci_dev, BCM43xx_DMA_RINGMEMSIZE,
					    &(ring->dmabase));
	if (!ring->descbase) {
		/* Allocation may have failed due to pci_alloc_consistent
		   insisting on use of GFP_DMA, which is more restrictive
		   than necessary...  */
		struct dma_desc *rx_ring;
		dma_addr_t rx_ring_dma;

		rx_ring = kzalloc(BCM43xx_DMA_RINGMEMSIZE, GFP_KERNEL);
		if (!rx_ring)
			goto out_err;

		rx_ring_dma = pci_map_single(ring->bcm->pci_dev, rx_ring,
					     BCM43xx_DMA_RINGMEMSIZE,
					     PCI_DMA_BIDIRECTIONAL);

		if (pci_dma_mapping_error(rx_ring_dma) ||
		    rx_ring_dma + BCM43xx_DMA_RINGMEMSIZE > ring->bcm->dma_mask) {
			/* Sigh... */
			if (!pci_dma_mapping_error(rx_ring_dma))
				pci_unmap_single(ring->bcm->pci_dev,
						 rx_ring_dma, BCM43xx_DMA_RINGMEMSIZE,
						 PCI_DMA_BIDIRECTIONAL);
			rx_ring_dma = pci_map_single(ring->bcm->pci_dev,
						 rx_ring, BCM43xx_DMA_RINGMEMSIZE,
						 PCI_DMA_BIDIRECTIONAL);
			if (pci_dma_mapping_error(rx_ring_dma) ||
			    rx_ring_dma + BCM43xx_DMA_RINGMEMSIZE > ring->bcm->dma_mask) {
				assert(0);
				if (!pci_dma_mapping_error(rx_ring_dma))
					pci_unmap_single(ring->bcm->pci_dev,
							 rx_ring_dma, BCM43xx_DMA_RINGMEMSIZE,
							 PCI_DMA_BIDIRECTIONAL);
				goto out_err;
			}
                }

                ring->descbase = rx_ring;
                ring->dmabase = rx_ring_dma;
	}
	memset(ring->descbase, 0, BCM43xx_DMA_RINGMEMSIZE);

	return 0;
out_err:
	printk(KERN_ERR PFX "DMA ringmemory allocation failed\n");
	return -ENOMEM;
}

static void free_ringmemory(struct bcm43xx_dmaring *ring)
{
	struct device *dev = &(ring->bcm->pci_dev->dev);

	dma_free_coherent(dev, BCM43xx_DMA_RINGMEMSIZE,
			  ring->descbase, ring->dmabase);
}

/* Reset the RX DMA channel */
int bcm43xx_dmacontroller_rx_reset(struct bcm43xx_private *bcm,
				   u16 mmio_base, int dma64)
{
	int i;
	u32 value;
	u16 offset;

	offset = dma64 ? BCM43xx_DMA64_RXCTL : BCM43xx_DMA32_RXCTL;
	bcm43xx_write32(bcm, mmio_base + offset, 0);
	for (i = 0; i < 1000; i++) {
		offset = dma64 ? BCM43xx_DMA64_RXSTATUS : BCM43xx_DMA32_RXSTATUS;
		value = bcm43xx_read32(bcm, mmio_base + offset);
		if (dma64) {
			value &= BCM43xx_DMA64_RXSTAT;
			if (value == BCM43xx_DMA64_RXSTAT_DISABLED) {
				i = -1;
				break;
			}
		} else {
			value &= BCM43xx_DMA32_RXSTATE;
			if (value == BCM43xx_DMA32_RXSTAT_DISABLED) {
				i = -1;
				break;
			}
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
				   u16 mmio_base, int dma64)
{
	int i;
	u32 value;
	u16 offset;

	for (i = 0; i < 1000; i++) {
		offset = dma64 ? BCM43xx_DMA64_TXSTATUS : BCM43xx_DMA32_TXSTATUS;
		value = bcm43xx_read32(bcm, mmio_base + offset);
		if (dma64) {
			value &= BCM43xx_DMA64_TXSTAT;
			if (value == BCM43xx_DMA64_TXSTAT_DISABLED ||
			    value == BCM43xx_DMA64_TXSTAT_IDLEWAIT ||
			    value == BCM43xx_DMA64_TXSTAT_STOPPED)
				break;
		} else {
			value &= BCM43xx_DMA32_TXSTATE;
			if (value == BCM43xx_DMA32_TXSTAT_DISABLED ||
			    value == BCM43xx_DMA32_TXSTAT_IDLEWAIT ||
			    value == BCM43xx_DMA32_TXSTAT_STOPPED)
				break;
		}
		udelay(10);
	}
	offset = dma64 ? BCM43xx_DMA64_TXCTL : BCM43xx_DMA32_TXCTL;
	bcm43xx_write32(bcm, mmio_base + offset, 0);
	for (i = 0; i < 1000; i++) {
		offset = dma64 ? BCM43xx_DMA64_TXSTATUS : BCM43xx_DMA32_TXSTATUS;
		value = bcm43xx_read32(bcm, mmio_base + offset);
		if (dma64) {
			value &= BCM43xx_DMA64_TXSTAT;
			if (value == BCM43xx_DMA64_TXSTAT_DISABLED) {
				i = -1;
				break;
			}
		} else {
			value &= BCM43xx_DMA32_TXSTATE;
			if (value == BCM43xx_DMA32_TXSTAT_DISABLED) {
				i = -1;
				break;
			}
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

static void fill_descriptor(struct bcm43xx_dmaring *ring,
			    struct bcm43xx_dmadesc_generic *desc,
			    dma_addr_t dmaaddr,
			    u16 bufsize,
			    int start, int end, int irq)
{
	int slot;

	slot = bcm43xx_dma_desc2idx(ring, desc);
	assert(slot >= 0 && slot < ring->nr_slots);

	if (ring->dma64) {
		u32 ctl0 = 0, ctl1 = 0;
		u32 addrlo, addrhi;
		u32 addrext;

		addrlo = (u32)(dmaaddr & 0xFFFFFFFF);
		addrhi = (((u64)dmaaddr >> 32) & ~BCM43xx_DMA64_ROUTING);
		addrext = (((u64)dmaaddr >> 32) >> BCM43xx_DMA64_ROUTING_SHIFT);
		addrhi |= ring->routing;
		if (slot == ring->nr_slots - 1)
			ctl0 |= BCM43xx_DMA64_DCTL0_DTABLEEND;
		if (start)
			ctl0 |= BCM43xx_DMA64_DCTL0_FRAMESTART;
		if (end)
			ctl0 |= BCM43xx_DMA64_DCTL0_FRAMEEND;
		if (irq)
			ctl0 |= BCM43xx_DMA64_DCTL0_IRQ;
		ctl1 |= (bufsize - ring->frameoffset)
			& BCM43xx_DMA64_DCTL1_BYTECNT;
		ctl1 |= (addrext << BCM43xx_DMA64_DCTL1_ADDREXT_SHIFT)
			& BCM43xx_DMA64_DCTL1_ADDREXT_MASK;

		desc->dma64.control0 = cpu_to_le32(ctl0);
		desc->dma64.control1 = cpu_to_le32(ctl1);
		desc->dma64.address_low = cpu_to_le32(addrlo);
		desc->dma64.address_high = cpu_to_le32(addrhi);
	} else {
		u32 ctl;
		u32 addr;
		u32 addrext;

		addr = (u32)(dmaaddr & ~BCM43xx_DMA32_ROUTING);
		addrext = (u32)(dmaaddr & BCM43xx_DMA32_ROUTING)
			   >> BCM43xx_DMA32_ROUTING_SHIFT;
		addr |= ring->routing;
		ctl = (bufsize - ring->frameoffset)
		      & BCM43xx_DMA32_DCTL_BYTECNT;
		if (slot == ring->nr_slots - 1)
			ctl |= BCM43xx_DMA32_DCTL_DTABLEEND;
		if (start)
			ctl |= BCM43xx_DMA32_DCTL_FRAMESTART;
		if (end)
			ctl |= BCM43xx_DMA32_DCTL_FRAMEEND;
		if (irq)
			ctl |= BCM43xx_DMA32_DCTL_IRQ;
		ctl |= (addrext << BCM43xx_DMA32_DCTL_ADDREXT_SHIFT)
		       & BCM43xx_DMA32_DCTL_ADDREXT_MASK;

		desc->dma32.control = cpu_to_le32(ctl);
		desc->dma32.address = cpu_to_le32(addr);
	}
}

static int setup_rx_descbuffer(struct bcm43xx_dmaring *ring,
			       struct bcm43xx_dmadesc_generic *desc,
			       struct bcm43xx_dmadesc_meta *meta,
			       gfp_t gfp_flags)
{
	struct bcm43xx_rxhdr *rxhdr;
	struct bcm43xx_hwxmitstatus *xmitstat;
	dma_addr_t dmaaddr;
	struct sk_buff *skb;

	assert(!ring->tx);

	skb = __dev_alloc_skb(ring->rx_buffersize, gfp_flags);
	if (unlikely(!skb))
		return -ENOMEM;
	dmaaddr = map_descbuffer(ring, skb->data, ring->rx_buffersize, 0);
	/* This hardware bug work-around adapted from the b44 driver.
	   The chip may be unable to do PCI DMA to/from anything above 1GB */
	if (pci_dma_mapping_error(dmaaddr) ||
	    dmaaddr + ring->rx_buffersize > ring->bcm->dma_mask) {
		/* This one has 30-bit addressing... */
		if (!pci_dma_mapping_error(dmaaddr))
			pci_unmap_single(ring->bcm->pci_dev,
					 dmaaddr, ring->rx_buffersize,
					 PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(skb);
		skb = __dev_alloc_skb(ring->rx_buffersize,GFP_DMA);
		if (skb == NULL)
			return -ENOMEM;
		dmaaddr = pci_map_single(ring->bcm->pci_dev,
					 skb->data, ring->rx_buffersize,
					 PCI_DMA_FROMDEVICE);
		if (pci_dma_mapping_error(dmaaddr) ||
		    dmaaddr + ring->rx_buffersize > ring->bcm->dma_mask) {
			assert(0);
			dev_kfree_skb_any(skb);
			return -ENOMEM;
		}
	}
	meta->skb = skb;
	meta->dmaaddr = dmaaddr;
	skb->dev = ring->bcm->net_dev;

	fill_descriptor(ring, desc, dmaaddr,
			ring->rx_buffersize, 0, 0, 0);

	rxhdr = (struct bcm43xx_rxhdr *)(skb->data);
	rxhdr->frame_length = 0;
	rxhdr->flags1 = 0;
	xmitstat = (struct bcm43xx_hwxmitstatus *)(skb->data);
	xmitstat->cookie = 0;

	return 0;
}

/* Allocate the initial descbuffers.
 * This is used for an RX ring only.
 */
static int alloc_initial_descbuffers(struct bcm43xx_dmaring *ring)
{
	int i, err = -ENOMEM;
	struct bcm43xx_dmadesc_generic *desc;
	struct bcm43xx_dmadesc_meta *meta;

	for (i = 0; i < ring->nr_slots; i++) {
		desc = bcm43xx_dma_idx2desc(ring, i, &meta);

		err = setup_rx_descbuffer(ring, desc, meta, GFP_KERNEL);
		if (err)
			goto err_unwind;
	}
	mb();
	ring->used_slots = ring->nr_slots;
	err = 0;
out:
	return err;

err_unwind:
	for (i--; i >= 0; i--) {
		desc = bcm43xx_dma_idx2desc(ring, i, &meta);

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
	u32 addrext;

	if (ring->tx) {
		if (ring->dma64) {
			u64 ringbase = (u64)(ring->dmabase);

			addrext = ((ringbase >> 32) >> BCM43xx_DMA64_ROUTING_SHIFT);
			value = BCM43xx_DMA64_TXENABLE;
			value |= (addrext << BCM43xx_DMA64_TXADDREXT_SHIFT)
				& BCM43xx_DMA64_TXADDREXT_MASK;
			bcm43xx_dma_write(ring, BCM43xx_DMA64_TXCTL, value);
			bcm43xx_dma_write(ring, BCM43xx_DMA64_TXRINGLO,
					(ringbase & 0xFFFFFFFF));
			bcm43xx_dma_write(ring, BCM43xx_DMA64_TXRINGHI,
					((ringbase >> 32) & ~BCM43xx_DMA64_ROUTING)
					| ring->routing);
		} else {
			u32 ringbase = (u32)(ring->dmabase);

			addrext = (ringbase >> BCM43xx_DMA32_ROUTING_SHIFT);
			value = BCM43xx_DMA32_TXENABLE;
			value |= (addrext << BCM43xx_DMA32_TXADDREXT_SHIFT)
				& BCM43xx_DMA32_TXADDREXT_MASK;
			bcm43xx_dma_write(ring, BCM43xx_DMA32_TXCTL, value);
			bcm43xx_dma_write(ring, BCM43xx_DMA32_TXRING,
					(ringbase & ~BCM43xx_DMA32_ROUTING)
					| ring->routing);
		}
	} else {
		err = alloc_initial_descbuffers(ring);
		if (err)
			goto out;
		if (ring->dma64) {
			u64 ringbase = (u64)(ring->dmabase);

			addrext = ((ringbase >> 32) >> BCM43xx_DMA64_ROUTING_SHIFT);
			value = (ring->frameoffset << BCM43xx_DMA64_RXFROFF_SHIFT);
			value |= BCM43xx_DMA64_RXENABLE;
			value |= (addrext << BCM43xx_DMA64_RXADDREXT_SHIFT)
				& BCM43xx_DMA64_RXADDREXT_MASK;
			bcm43xx_dma_write(ring, BCM43xx_DMA64_RXCTL, value);
			bcm43xx_dma_write(ring, BCM43xx_DMA64_RXRINGLO,
					(ringbase & 0xFFFFFFFF));
			bcm43xx_dma_write(ring, BCM43xx_DMA64_RXRINGHI,
					((ringbase >> 32) & ~BCM43xx_DMA64_ROUTING)
					| ring->routing);
			bcm43xx_dma_write(ring, BCM43xx_DMA64_RXINDEX, 200);
		} else {
			u32 ringbase = (u32)(ring->dmabase);

			addrext = (ringbase >> BCM43xx_DMA32_ROUTING_SHIFT);
			value = (ring->frameoffset << BCM43xx_DMA32_RXFROFF_SHIFT);
			value |= BCM43xx_DMA32_RXENABLE;
			value |= (addrext << BCM43xx_DMA32_RXADDREXT_SHIFT)
				& BCM43xx_DMA32_RXADDREXT_MASK;
			bcm43xx_dma_write(ring, BCM43xx_DMA32_RXCTL, value);
			bcm43xx_dma_write(ring, BCM43xx_DMA32_RXRING,
					(ringbase & ~BCM43xx_DMA32_ROUTING)
					| ring->routing);
			bcm43xx_dma_write(ring, BCM43xx_DMA32_RXINDEX, 200);
		}
	}

out:
	return err;
}

/* Shutdown the DMA controller. */
static void dmacontroller_cleanup(struct bcm43xx_dmaring *ring)
{
	if (ring->tx) {
		bcm43xx_dmacontroller_tx_reset(ring->bcm, ring->mmio_base, ring->dma64);
		if (ring->dma64) {
			bcm43xx_dma_write(ring, BCM43xx_DMA64_TXRINGLO, 0);
			bcm43xx_dma_write(ring, BCM43xx_DMA64_TXRINGHI, 0);
		} else
			bcm43xx_dma_write(ring, BCM43xx_DMA32_TXRING, 0);
	} else {
		bcm43xx_dmacontroller_rx_reset(ring->bcm, ring->mmio_base, ring->dma64);
		if (ring->dma64) {
			bcm43xx_dma_write(ring, BCM43xx_DMA64_RXRINGLO, 0);
			bcm43xx_dma_write(ring, BCM43xx_DMA64_RXRINGHI, 0);
		} else
			bcm43xx_dma_write(ring, BCM43xx_DMA32_RXRING, 0);
	}
}

static void free_all_descbuffers(struct bcm43xx_dmaring *ring)
{
	struct bcm43xx_dmadesc_generic *desc;
	struct bcm43xx_dmadesc_meta *meta;
	int i;

	if (!ring->used_slots)
		return;
	for (i = 0; i < ring->nr_slots; i++) {
		desc = bcm43xx_dma_idx2desc(ring, i, &meta);

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
		free_descriptor_buffer(ring, meta, 0);
	}
}

/* Main initialization function. */
static
struct bcm43xx_dmaring * bcm43xx_setup_dmaring(struct bcm43xx_private *bcm,
					       int controller_index,
					       int for_tx,
					       int dma64)
{
	struct bcm43xx_dmaring *ring;
	int err;
	int nr_slots;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		goto out;

	nr_slots = BCM43xx_RXRING_SLOTS;
	if (for_tx)
		nr_slots = BCM43xx_TXRING_SLOTS;

	ring->meta = kcalloc(nr_slots, sizeof(struct bcm43xx_dmadesc_meta),
			     GFP_KERNEL);
	if (!ring->meta)
		goto err_kfree_ring;

	ring->routing = BCM43xx_DMA32_CLIENTTRANS;
	if (dma64)
		ring->routing = BCM43xx_DMA64_CLIENTTRANS;
#ifdef CONFIG_BCM947XX
	if (bcm->pci_dev->bus->number == 0)
		ring->routing = dma64 ? BCM43xx_DMA64_NOTRANS : BCM43xx_DMA32_NOTRANS;
#endif

	ring->bcm = bcm;
	ring->nr_slots = nr_slots;
	ring->suspend_mark = ring->nr_slots * BCM43xx_TXSUSPEND_PERCENT / 100;
	ring->resume_mark = ring->nr_slots * BCM43xx_TXRESUME_PERCENT / 100;
	assert(ring->suspend_mark < ring->resume_mark);
	ring->mmio_base = bcm43xx_dmacontroller_base(dma64, controller_index);
	ring->index = controller_index;
	ring->dma64 = !!dma64;
	if (for_tx) {
		ring->tx = 1;
		ring->current_slot = -1;
	} else {
		if (ring->index == 0) {
			ring->rx_buffersize = BCM43xx_DMA0_RX_BUFFERSIZE;
			ring->frameoffset = BCM43xx_DMA0_RX_FRAMEOFFSET;
		} else if (ring->index == 3) {
			ring->rx_buffersize = BCM43xx_DMA3_RX_BUFFERSIZE;
			ring->frameoffset = BCM43xx_DMA3_RX_FRAMEOFFSET;
		} else
			assert(0);
	}

	err = alloc_ringmemory(ring);
	if (err)
		goto err_kfree_meta;
	err = dmacontroller_setup(ring);
	if (err)
		goto err_free_ringmemory;
	return ring;

out:
	printk(KERN_ERR PFX "Error in bcm43xx_setup_dmaring\n");
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

	dprintk(KERN_INFO PFX "DMA-%s 0x%04X (%s) max used slots: %d/%d\n",
		(ring->dma64) ? "64" : "32",
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

	bcm43xx_destroy_dmaring(dma->rx_ring3);
	dma->rx_ring3 = NULL;
	bcm43xx_destroy_dmaring(dma->rx_ring0);
	dma->rx_ring0 = NULL;

	bcm43xx_destroy_dmaring(dma->tx_ring5);
	dma->tx_ring5 = NULL;
	bcm43xx_destroy_dmaring(dma->tx_ring4);
	dma->tx_ring4 = NULL;
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
	int dma64 = 0;

	bcm->dma_mask = bcm43xx_get_supported_dma_mask(bcm);
	if (bcm->dma_mask == DMA_64BIT_MASK)
		dma64 = 1;
	err = pci_set_dma_mask(bcm->pci_dev, bcm->dma_mask);
	if (err)
		goto no_dma;
	err = pci_set_consistent_dma_mask(bcm->pci_dev, bcm->dma_mask);
	if (err)
		goto no_dma;

	/* setup TX DMA channels. */
	ring = bcm43xx_setup_dmaring(bcm, 0, 1, dma64);
	if (!ring)
		goto out;
	dma->tx_ring0 = ring;

	ring = bcm43xx_setup_dmaring(bcm, 1, 1, dma64);
	if (!ring)
		goto err_destroy_tx0;
	dma->tx_ring1 = ring;

	ring = bcm43xx_setup_dmaring(bcm, 2, 1, dma64);
	if (!ring)
		goto err_destroy_tx1;
	dma->tx_ring2 = ring;

	ring = bcm43xx_setup_dmaring(bcm, 3, 1, dma64);
	if (!ring)
		goto err_destroy_tx2;
	dma->tx_ring3 = ring;

	ring = bcm43xx_setup_dmaring(bcm, 4, 1, dma64);
	if (!ring)
		goto err_destroy_tx3;
	dma->tx_ring4 = ring;

	ring = bcm43xx_setup_dmaring(bcm, 5, 1, dma64);
	if (!ring)
		goto err_destroy_tx4;
	dma->tx_ring5 = ring;

	/* setup RX DMA channels. */
	ring = bcm43xx_setup_dmaring(bcm, 0, 0, dma64);
	if (!ring)
		goto err_destroy_tx5;
	dma->rx_ring0 = ring;

	if (bcm->current_core->rev < 5) {
		ring = bcm43xx_setup_dmaring(bcm, 3, 0, dma64);
		if (!ring)
			goto err_destroy_rx0;
		dma->rx_ring3 = ring;
	}

	dprintk(KERN_INFO PFX "%d-bit DMA initialized\n",
		(bcm->dma_mask == DMA_64BIT_MASK) ? 64 :
		(bcm->dma_mask == DMA_32BIT_MASK) ? 32 : 30);
	err = 0;
out:
	return err;

err_destroy_rx0:
	bcm43xx_destroy_dmaring(dma->rx_ring0);
	dma->rx_ring0 = NULL;
err_destroy_tx5:
	bcm43xx_destroy_dmaring(dma->tx_ring5);
	dma->tx_ring5 = NULL;
err_destroy_tx4:
	bcm43xx_destroy_dmaring(dma->tx_ring4);
	dma->tx_ring4 = NULL;
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
no_dma:
#ifdef CONFIG_BCM43XX_PIO
	printk(KERN_WARNING PFX "DMA not supported on this device."
				" Falling back to PIO.\n");
	bcm->__using_pio = 1;
	return -ENOSYS;
#else
	printk(KERN_ERR PFX "FATAL: DMA not supported and PIO not configured. "
			    "Please recompile the driver with PIO support.\n");
	return -ENODEV;
#endif /* CONFIG_BCM43XX_PIO */
}

/* Generate a cookie for the TX header. */
static u16 generate_cookie(struct bcm43xx_dmaring *ring,
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
		assert(0);
	}
	*slot = (cookie & 0x0FFF);
	assert(*slot >= 0 && *slot < ring->nr_slots);

	return ring;
}

static void dmacontroller_poke_tx(struct bcm43xx_dmaring *ring,
				  int slot)
{
	u16 offset;
	int descsize;

	/* Everything is ready to start. Buffers are DMA mapped and
	 * associated with slots.
	 * "slot" is the last slot of the new frame we want to transmit.
	 * Close your seat belts now, please.
	 */
	wmb();
	slot = next_slot(ring, slot);
	offset = (ring->dma64) ? BCM43xx_DMA64_TXINDEX : BCM43xx_DMA32_TXINDEX;
	descsize = (ring->dma64) ? sizeof(struct bcm43xx_dmadesc64)
		: sizeof(struct bcm43xx_dmadesc32);
	bcm43xx_dma_write(ring, offset,
			(u32)(slot * descsize));
}

static void dma_tx_fragment(struct bcm43xx_dmaring *ring,
			    struct sk_buff *skb,
			    u8 cur_frag)
{
	int slot;
	struct bcm43xx_dmadesc_generic *desc;
	struct bcm43xx_dmadesc_meta *meta;
	dma_addr_t dmaaddr;
	struct sk_buff *bounce_skb;

	assert(skb_shinfo(skb)->nr_frags == 0);

	slot = request_slot(ring);
	desc = bcm43xx_dma_idx2desc(ring, slot, &meta);

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
	dmaaddr = map_descbuffer(ring, skb->data, skb->len, 1);
	if (dma_mapping_error(dmaaddr) || dmaaddr + skb->len > ring->bcm->dma_mask) {
		/* chip cannot handle DMA to/from > 1GB, use bounce buffer (copied from b44 driver) */
		if (!dma_mapping_error(dmaaddr))
			unmap_descbuffer(ring, dmaaddr, skb->len, 1);
		bounce_skb = __dev_alloc_skb(skb->len, GFP_ATOMIC|GFP_DMA);
		if (!bounce_skb)
			return;
		dmaaddr = map_descbuffer(ring, bounce_skb->data, bounce_skb->len, 1);
		if (dma_mapping_error(dmaaddr) || dmaaddr + skb->len > ring->bcm->dma_mask) {
			if (!dma_mapping_error(dmaaddr))
				unmap_descbuffer(ring, dmaaddr, skb->len, 1);
			dev_kfree_skb_any(bounce_skb);
			assert(0);
			return;
		}
		skb_copy_from_linear_data(skb, skb_put(bounce_skb, skb->len),
					  skb->len);
		dev_kfree_skb_any(skb);
		skb = bounce_skb;
	}

	meta->skb = skb;
	meta->dmaaddr = dmaaddr;

	fill_descriptor(ring, desc, dmaaddr,
			skb->len, 1, 1, 1);

	/* Now transfer the whole frame. */
	dmacontroller_poke_tx(ring, slot);
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
	}
	ieee80211_txb_free(txb);

	return 0;
}

void bcm43xx_dma_handle_xmitstatus(struct bcm43xx_private *bcm,
				   struct bcm43xx_xmitstatus *status)
{
	struct bcm43xx_dmaring *ring;
	struct bcm43xx_dmadesc_generic *desc;
	struct bcm43xx_dmadesc_meta *meta;
	int is_last_fragment;
	int slot;
	u32 tmp;

	ring = parse_cookie(bcm, status->cookie, &slot);
	assert(ring);
	assert(ring->tx);
	while (1) {
		assert(slot >= 0 && slot < ring->nr_slots);
		desc = bcm43xx_dma_idx2desc(ring, slot, &meta);

		if (ring->dma64) {
			tmp = le32_to_cpu(desc->dma64.control0);
			is_last_fragment = !!(tmp & BCM43xx_DMA64_DCTL0_FRAMEEND);
		} else {
			tmp = le32_to_cpu(desc->dma32.control);
			is_last_fragment = !!(tmp & BCM43xx_DMA32_DCTL_FRAMEEND);
		}
		unmap_descbuffer(ring, meta->dmaaddr, meta->skb->len, 1);
		free_descriptor_buffer(ring, meta, 1);
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
	struct bcm43xx_dmadesc_generic *desc;
	struct bcm43xx_dmadesc_meta *meta;
	struct bcm43xx_rxhdr *rxhdr;
	struct sk_buff *skb;
	u16 len;
	int err;
	dma_addr_t dmaaddr;

	desc = bcm43xx_dma_idx2desc(ring, *slot, &meta);

	sync_descbuffer_for_cpu(ring, meta->dmaaddr, ring->rx_buffersize);
	skb = meta->skb;

	if (ring->index == 3) {
		/* We received an xmit status. */
		struct bcm43xx_hwxmitstatus *hw = (struct bcm43xx_hwxmitstatus *)skb->data;
		struct bcm43xx_xmitstatus stat;
		int i = 0;

		stat.cookie = le16_to_cpu(hw->cookie);
		while (stat.cookie == 0) {
			if (unlikely(++i >= 10000)) {
				assert(0);
				break;
			}
			udelay(2);
			barrier();
			stat.cookie = le16_to_cpu(hw->cookie);
		}
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
			desc = bcm43xx_dma_idx2desc(ring, *slot, &meta);
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
	if (ring->dma64) {
		status = bcm43xx_dma_read(ring, BCM43xx_DMA64_RXSTATUS);
		descptr = (status & BCM43xx_DMA64_RXSTATDPTR);
		current_slot = descptr / sizeof(struct bcm43xx_dmadesc64);
	} else {
		status = bcm43xx_dma_read(ring, BCM43xx_DMA32_RXSTATUS);
		descptr = (status & BCM43xx_DMA32_RXDPTR);
		current_slot = descptr / sizeof(struct bcm43xx_dmadesc32);
	}
	assert(current_slot >= 0 && current_slot < ring->nr_slots);

	slot = ring->current_slot;
	for ( ; slot != current_slot; slot = next_slot(ring, slot)) {
		dma_rx(ring, &slot);
#ifdef CONFIG_BCM43XX_DEBUG
		if (++used_slots > ring->max_used_slots)
			ring->max_used_slots = used_slots;
#endif
	}
	if (ring->dma64) {
		bcm43xx_dma_write(ring, BCM43xx_DMA64_RXINDEX,
				(u32)(slot * sizeof(struct bcm43xx_dmadesc64)));
	} else {
		bcm43xx_dma_write(ring, BCM43xx_DMA32_RXINDEX,
				(u32)(slot * sizeof(struct bcm43xx_dmadesc32)));
	}
	ring->current_slot = slot;
}

void bcm43xx_dma_tx_suspend(struct bcm43xx_dmaring *ring)
{
	assert(ring->tx);
	bcm43xx_power_saving_ctl_bits(ring->bcm, -1, 1);
	if (ring->dma64) {
		bcm43xx_dma_write(ring, BCM43xx_DMA64_TXCTL,
				bcm43xx_dma_read(ring, BCM43xx_DMA64_TXCTL)
				| BCM43xx_DMA64_TXSUSPEND);
	} else {
		bcm43xx_dma_write(ring, BCM43xx_DMA32_TXCTL,
				bcm43xx_dma_read(ring, BCM43xx_DMA32_TXCTL)
				| BCM43xx_DMA32_TXSUSPEND);
	}
}

void bcm43xx_dma_tx_resume(struct bcm43xx_dmaring *ring)
{
	assert(ring->tx);
	if (ring->dma64) {
		bcm43xx_dma_write(ring, BCM43xx_DMA64_TXCTL,
				bcm43xx_dma_read(ring, BCM43xx_DMA64_TXCTL)
				& ~BCM43xx_DMA64_TXSUSPEND);
	} else {
		bcm43xx_dma_write(ring, BCM43xx_DMA32_TXCTL,
				bcm43xx_dma_read(ring, BCM43xx_DMA32_TXCTL)
				& ~BCM43xx_DMA32_TXSUSPEND);
	}
	bcm43xx_power_saving_ctl_bits(ring->bcm, -1, -1);
}
