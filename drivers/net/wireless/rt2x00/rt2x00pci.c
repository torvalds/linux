/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00pci
	Abstract: rt2x00 generic pci device routines.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rt2x00.h"
#include "rt2x00pci.h"

/*
 * Beacon handlers.
 */
int rt2x00pci_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb,
			    struct ieee80211_tx_control *control)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct skb_desc *desc;
	struct data_ring *ring;
	struct data_entry *entry;

	/*
	 * Just in case mac80211 doesn't set this correctly,
	 * but we need this queue set for the descriptor
	 * initialization.
	 */
	control->queue = IEEE80211_TX_QUEUE_BEACON;
	ring = rt2x00lib_get_ring(rt2x00dev, control->queue);
	entry = rt2x00_get_data_entry(ring);

	/*
	 * Fill in skb descriptor
	 */
	desc = get_skb_desc(skb);
	desc->desc_len = ring->desc_size;
	desc->data_len = skb->len;
	desc->desc = entry->priv;
	desc->data = skb->data;
	desc->ring = ring;
	desc->entry = entry;

	memcpy(entry->data_addr, skb->data, skb->len);
	rt2x00lib_write_tx_desc(rt2x00dev, skb, control);

	/*
	 * Enable beacon generation.
	 */
	rt2x00dev->ops->lib->kick_tx_queue(rt2x00dev, control->queue);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00pci_beacon_update);

/*
 * TX data handlers.
 */
int rt2x00pci_write_tx_data(struct rt2x00_dev *rt2x00dev,
			    struct data_ring *ring, struct sk_buff *skb,
			    struct ieee80211_tx_control *control)
{
	struct data_entry *entry = rt2x00_get_data_entry(ring);
	__le32 *txd = entry->priv;
	struct skb_desc *desc;
	u32 word;

	if (rt2x00_ring_full(ring))
		return -EINVAL;

	rt2x00_desc_read(txd, 0, &word);

	if (rt2x00_get_field32(word, TXD_ENTRY_OWNER_NIC) ||
	    rt2x00_get_field32(word, TXD_ENTRY_VALID)) {
		ERROR(rt2x00dev,
		      "Arrived at non-free entry in the non-full queue %d.\n"
		      "Please file bug report to %s.\n",
		      control->queue, DRV_PROJECT);
		return -EINVAL;
	}

	/*
	 * Fill in skb descriptor
	 */
	desc = get_skb_desc(skb);
	desc->desc_len = ring->desc_size;
	desc->data_len = skb->len;
	desc->desc = entry->priv;
	desc->data = skb->data;
	desc->ring = ring;
	desc->entry = entry;

	memcpy(entry->data_addr, skb->data, skb->len);
	rt2x00lib_write_tx_desc(rt2x00dev, skb, control);

	rt2x00_ring_index_inc(ring);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00pci_write_tx_data);

/*
 * TX/RX data handlers.
 */
void rt2x00pci_rxdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring = rt2x00dev->rx;
	struct data_entry *entry;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	struct skb_desc *skbdesc;
	struct rxdata_entry_desc desc;
	int header_size;
	__le32 *rxd;
	int align;
	u32 word;

	while (1) {
		entry = rt2x00_get_data_entry(ring);
		rxd = entry->priv;
		rt2x00_desc_read(rxd, 0, &word);

		if (rt2x00_get_field32(word, RXD_ENTRY_OWNER_NIC))
			break;

		memset(&desc, 0, sizeof(desc));
		rt2x00dev->ops->lib->fill_rxdone(entry, &desc);

		hdr = (struct ieee80211_hdr *)entry->data_addr;
		header_size =
		    ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control));

		/*
		 * The data behind the ieee80211 header must be
		 * aligned on a 4 byte boundary.
		 */
		align = header_size % 4;

		/*
		 * Allocate the sk_buffer, initialize it and copy
		 * all data into it.
		 */
		skb = dev_alloc_skb(desc.size + align);
		if (!skb)
			return;

		skb_reserve(skb, align);
		memcpy(skb_put(skb, desc.size), entry->data_addr, desc.size);

		/*
		 * Fill in skb descriptor
		 */
		skbdesc = get_skb_desc(skb);
		skbdesc->desc_len = entry->ring->desc_size;
		skbdesc->data_len = skb->len;
		skbdesc->desc = entry->priv;
		skbdesc->data = skb->data;
		skbdesc->ring = ring;
		skbdesc->entry = entry;

		/*
		 * Send the frame to rt2x00lib for further processing.
		 */
		rt2x00lib_rxdone(entry, skb, &desc);

		if (test_bit(DEVICE_ENABLED_RADIO, &ring->rt2x00dev->flags)) {
			rt2x00_set_field32(&word, RXD_ENTRY_OWNER_NIC, 1);
			rt2x00_desc_write(rxd, 0, word);
		}

		rt2x00_ring_index_inc(ring);
	}
}
EXPORT_SYMBOL_GPL(rt2x00pci_rxdone);

void rt2x00pci_txdone(struct rt2x00_dev *rt2x00dev, struct data_entry *entry,
		      const int tx_status, const int retry)
{
	u32 word;

	rt2x00lib_txdone(entry, tx_status, retry);

	/*
	 * Make this entry available for reuse.
	 */
	entry->flags = 0;

	rt2x00_desc_read(entry->priv, 0, &word);
	rt2x00_set_field32(&word, TXD_ENTRY_OWNER_NIC, 0);
	rt2x00_set_field32(&word, TXD_ENTRY_VALID, 0);
	rt2x00_desc_write(entry->priv, 0, word);

	rt2x00_ring_index_done_inc(entry->ring);

	/*
	 * If the data ring was full before the txdone handler
	 * we must make sure the packet queue in the mac80211 stack
	 * is reenabled when the txdone handler has finished.
	 */
	if (!rt2x00_ring_full(entry->ring))
		ieee80211_wake_queue(rt2x00dev->hw,
				     entry->tx_status.control.queue);

}
EXPORT_SYMBOL_GPL(rt2x00pci_txdone);

/*
 * Device initialization handlers.
 */
#define priv_offset(__ring, __i)				\
({								\
	ring->data_addr + (i * ring->desc_size);		\
})

#define data_addr_offset(__ring, __i)				\
({								\
	(__ring)->data_addr +					\
	    ((__ring)->stats.limit * (__ring)->desc_size) +	\
	    ((__i) * (__ring)->data_size);			\
})

#define data_dma_offset(__ring, __i)				\
({								\
	(__ring)->data_dma +					\
	    ((__ring)->stats.limit * (__ring)->desc_size) +	\
	    ((__i) * (__ring)->data_size);			\
})

static int rt2x00pci_alloc_dma(struct rt2x00_dev *rt2x00dev,
			       struct data_ring *ring)
{
	unsigned int i;

	/*
	 * Allocate DMA memory for descriptor and buffer.
	 */
	ring->data_addr = pci_alloc_consistent(rt2x00dev_pci(rt2x00dev),
					       rt2x00_get_ring_size(ring),
					       &ring->data_dma);
	if (!ring->data_addr)
		return -ENOMEM;

	/*
	 * Initialize all ring entries to contain valid
	 * addresses.
	 */
	for (i = 0; i < ring->stats.limit; i++) {
		ring->entry[i].priv = priv_offset(ring, i);
		ring->entry[i].data_addr = data_addr_offset(ring, i);
		ring->entry[i].data_dma = data_dma_offset(ring, i);
	}

	return 0;
}

static void rt2x00pci_free_dma(struct rt2x00_dev *rt2x00dev,
			       struct data_ring *ring)
{
	if (ring->data_addr)
		pci_free_consistent(rt2x00dev_pci(rt2x00dev),
				    rt2x00_get_ring_size(ring),
				    ring->data_addr, ring->data_dma);
	ring->data_addr = NULL;
}

int rt2x00pci_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct pci_dev *pci_dev = rt2x00dev_pci(rt2x00dev);
	struct data_ring *ring;
	int status;

	/*
	 * Allocate DMA
	 */
	ring_for_each(rt2x00dev, ring) {
		status = rt2x00pci_alloc_dma(rt2x00dev, ring);
		if (status)
			goto exit;
	}

	/*
	 * Register interrupt handler.
	 */
	status = request_irq(pci_dev->irq, rt2x00dev->ops->lib->irq_handler,
			     IRQF_SHARED, pci_name(pci_dev), rt2x00dev);
	if (status) {
		ERROR(rt2x00dev, "IRQ %d allocation failed (error %d).\n",
		      pci_dev->irq, status);
		return status;
	}

	return 0;

exit:
	rt2x00pci_uninitialize(rt2x00dev);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00pci_initialize);

void rt2x00pci_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring;

	/*
	 * Free irq line.
	 */
	free_irq(rt2x00dev_pci(rt2x00dev)->irq, rt2x00dev);

	/*
	 * Free DMA
	 */
	ring_for_each(rt2x00dev, ring)
		rt2x00pci_free_dma(rt2x00dev, ring);
}
EXPORT_SYMBOL_GPL(rt2x00pci_uninitialize);

/*
 * PCI driver handlers.
 */
static void rt2x00pci_free_reg(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rf);
	rt2x00dev->rf = NULL;

	kfree(rt2x00dev->eeprom);
	rt2x00dev->eeprom = NULL;

	if (rt2x00dev->csr_addr) {
		iounmap(rt2x00dev->csr_addr);
		rt2x00dev->csr_addr = NULL;
	}
}

static int rt2x00pci_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	struct pci_dev *pci_dev = rt2x00dev_pci(rt2x00dev);

	rt2x00dev->csr_addr = ioremap(pci_resource_start(pci_dev, 0),
				      pci_resource_len(pci_dev, 0));
	if (!rt2x00dev->csr_addr)
		goto exit;

	rt2x00dev->eeprom = kzalloc(rt2x00dev->ops->eeprom_size, GFP_KERNEL);
	if (!rt2x00dev->eeprom)
		goto exit;

	rt2x00dev->rf = kzalloc(rt2x00dev->ops->rf_size, GFP_KERNEL);
	if (!rt2x00dev->rf)
		goto exit;

	return 0;

exit:
	ERROR_PROBE("Failed to allocate registers.\n");

	rt2x00pci_free_reg(rt2x00dev);

	return -ENOMEM;
}

int rt2x00pci_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	struct rt2x00_ops *ops = (struct rt2x00_ops *)id->driver_data;
	struct ieee80211_hw *hw;
	struct rt2x00_dev *rt2x00dev;
	int retval;

	retval = pci_request_regions(pci_dev, pci_name(pci_dev));
	if (retval) {
		ERROR_PROBE("PCI request regions failed.\n");
		return retval;
	}

	retval = pci_enable_device(pci_dev);
	if (retval) {
		ERROR_PROBE("Enable device failed.\n");
		goto exit_release_regions;
	}

	pci_set_master(pci_dev);

	if (pci_set_mwi(pci_dev))
		ERROR_PROBE("MWI not available.\n");

	if (pci_set_dma_mask(pci_dev, DMA_64BIT_MASK) &&
	    pci_set_dma_mask(pci_dev, DMA_32BIT_MASK)) {
		ERROR_PROBE("PCI DMA not supported.\n");
		retval = -EIO;
		goto exit_disable_device;
	}

	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		ERROR_PROBE("Failed to allocate hardware.\n");
		retval = -ENOMEM;
		goto exit_disable_device;
	}

	pci_set_drvdata(pci_dev, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = pci_dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;

	retval = rt2x00pci_alloc_reg(rt2x00dev);
	if (retval)
		goto exit_free_device;

	retval = rt2x00lib_probe_dev(rt2x00dev);
	if (retval)
		goto exit_free_reg;

	return 0;

exit_free_reg:
	rt2x00pci_free_reg(rt2x00dev);

exit_free_device:
	ieee80211_free_hw(hw);

exit_disable_device:
	if (retval != -EBUSY)
		pci_disable_device(pci_dev);

exit_release_regions:
	pci_release_regions(pci_dev);

	pci_set_drvdata(pci_dev, NULL);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00pci_probe);

void rt2x00pci_remove(struct pci_dev *pci_dev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Free all allocated data.
	 */
	rt2x00lib_remove_dev(rt2x00dev);
	rt2x00pci_free_reg(rt2x00dev);
	ieee80211_free_hw(hw);

	/*
	 * Free the PCI device data.
	 */
	pci_set_drvdata(pci_dev, NULL);
	pci_disable_device(pci_dev);
	pci_release_regions(pci_dev);
}
EXPORT_SYMBOL_GPL(rt2x00pci_remove);

#ifdef CONFIG_PM
int rt2x00pci_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int retval;

	retval = rt2x00lib_suspend(rt2x00dev, state);
	if (retval)
		return retval;

	rt2x00pci_free_reg(rt2x00dev);

	pci_save_state(pci_dev);
	pci_disable_device(pci_dev);
	return pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));
}
EXPORT_SYMBOL_GPL(rt2x00pci_suspend);

int rt2x00pci_resume(struct pci_dev *pci_dev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int retval;

	if (pci_set_power_state(pci_dev, PCI_D0) ||
	    pci_enable_device(pci_dev) ||
	    pci_restore_state(pci_dev)) {
		ERROR(rt2x00dev, "Failed to resume device.\n");
		return -EIO;
	}

	retval = rt2x00pci_alloc_reg(rt2x00dev);
	if (retval)
		return retval;

	retval = rt2x00lib_resume(rt2x00dev);
	if (retval)
		goto exit_free_reg;

	return 0;

exit_free_reg:
	rt2x00pci_free_reg(rt2x00dev);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00pci_resume);
#endif /* CONFIG_PM */

/*
 * rt2x00pci module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 library");
MODULE_LICENSE("GPL");
