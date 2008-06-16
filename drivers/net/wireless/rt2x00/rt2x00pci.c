/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
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
 * TX data handlers.
 */
int rt2x00pci_write_tx_data(struct rt2x00_dev *rt2x00dev,
			    struct data_queue *queue, struct sk_buff *skb,
			    struct ieee80211_tx_control *control)
{
	struct queue_entry *entry = rt2x00queue_get_entry(queue, Q_INDEX);
	struct queue_entry_priv_pci_tx *priv_tx = entry->priv_data;
	struct skb_frame_desc *skbdesc;
	u32 word;

	if (rt2x00queue_full(queue))
		return -EINVAL;

	rt2x00_desc_read(priv_tx->desc, 0, &word);

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
	skbdesc = get_skb_frame_desc(skb);
	skbdesc->data = skb->data;
	skbdesc->data_len = skb->len;
	skbdesc->desc = priv_tx->desc;
	skbdesc->desc_len = queue->desc_size;
	skbdesc->entry = entry;

	memcpy(&priv_tx->control, control, sizeof(priv_tx->control));
	memcpy(priv_tx->data, skb->data, skb->len);
	rt2x00lib_write_tx_desc(rt2x00dev, skb, control);

	rt2x00queue_index_inc(queue, Q_INDEX);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00pci_write_tx_data);

/*
 * TX/RX data handlers.
 */
void rt2x00pci_rxdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue = rt2x00dev->rx;
	struct queue_entry *entry;
	struct queue_entry_priv_pci_rx *priv_rx;
	struct ieee80211_hdr *hdr;
	struct skb_frame_desc *skbdesc;
	struct rxdone_entry_desc rxdesc;
	int header_size;
	int align;
	u32 word;

	while (1) {
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		priv_rx = entry->priv_data;
		rt2x00_desc_read(priv_rx->desc, 0, &word);

		if (rt2x00_get_field32(word, RXD_ENTRY_OWNER_NIC))
			break;

		memset(&rxdesc, 0, sizeof(rxdesc));
		rt2x00dev->ops->lib->fill_rxdone(entry, &rxdesc);

		hdr = (struct ieee80211_hdr *)priv_rx->data;
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
		entry->skb = dev_alloc_skb(rxdesc.size + align);
		if (!entry->skb)
			return;

		skb_reserve(entry->skb, align);
		memcpy(skb_put(entry->skb, rxdesc.size),
		       priv_rx->data, rxdesc.size);

		/*
		 * Fill in skb descriptor
		 */
		skbdesc = get_skb_frame_desc(entry->skb);
		memset(skbdesc, 0, sizeof(*skbdesc));
		skbdesc->data = entry->skb->data;
		skbdesc->data_len = entry->skb->len;
		skbdesc->desc = priv_rx->desc;
		skbdesc->desc_len = queue->desc_size;
		skbdesc->entry = entry;

		/*
		 * Send the frame to rt2x00lib for further processing.
		 */
		rt2x00lib_rxdone(entry, &rxdesc);

		if (test_bit(DEVICE_ENABLED_RADIO, &queue->rt2x00dev->flags)) {
			rt2x00_set_field32(&word, RXD_ENTRY_OWNER_NIC, 1);
			rt2x00_desc_write(priv_rx->desc, 0, word);
		}

		rt2x00queue_index_inc(queue, Q_INDEX);
	}
}
EXPORT_SYMBOL_GPL(rt2x00pci_rxdone);

void rt2x00pci_txdone(struct rt2x00_dev *rt2x00dev, struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc)
{
	struct queue_entry_priv_pci_tx *priv_tx = entry->priv_data;
	u32 word;

	txdesc->control = &priv_tx->control;
	rt2x00lib_txdone(entry, txdesc);

	/*
	 * Make this entry available for reuse.
	 */
	entry->flags = 0;

	rt2x00_desc_read(priv_tx->desc, 0, &word);
	rt2x00_set_field32(&word, TXD_ENTRY_OWNER_NIC, 0);
	rt2x00_set_field32(&word, TXD_ENTRY_VALID, 0);
	rt2x00_desc_write(priv_tx->desc, 0, word);

	rt2x00queue_index_inc(entry->queue, Q_INDEX_DONE);

	/*
	 * If the data queue was full before the txdone handler
	 * we must make sure the packet queue in the mac80211 stack
	 * is reenabled when the txdone handler has finished.
	 */
	if (!rt2x00queue_full(entry->queue))
		ieee80211_wake_queue(rt2x00dev->hw, priv_tx->control.queue);

}
EXPORT_SYMBOL_GPL(rt2x00pci_txdone);

/*
 * Device initialization handlers.
 */
#define desc_size(__queue)			\
({						\
	 ((__queue)->limit * (__queue)->desc_size);\
})

#define data_size(__queue)			\
({						\
	 ((__queue)->limit * (__queue)->data_size);\
})

#define dma_size(__queue)			\
({						\
	data_size(__queue) + desc_size(__queue);\
})

#define desc_offset(__queue, __base, __i)	\
({						\
	(__base) + data_size(__queue) + 	\
	    ((__i) * (__queue)->desc_size);	\
})

#define data_offset(__queue, __base, __i)	\
({						\
	(__base) +				\
	    ((__i) * (__queue)->data_size);	\
})

static int rt2x00pci_alloc_queue_dma(struct rt2x00_dev *rt2x00dev,
				     struct data_queue *queue)
{
	struct pci_dev *pci_dev = rt2x00dev_pci(rt2x00dev);
	struct queue_entry_priv_pci_rx *priv_rx;
	struct queue_entry_priv_pci_tx *priv_tx;
	void *addr;
	dma_addr_t dma;
	void *desc_addr;
	dma_addr_t desc_dma;
	void *data_addr;
	dma_addr_t data_dma;
	unsigned int i;

	/*
	 * Allocate DMA memory for descriptor and buffer.
	 */
	addr = pci_alloc_consistent(pci_dev, dma_size(queue), &dma);
	if (!addr)
		return -ENOMEM;

	memset(addr, 0, dma_size(queue));

	/*
	 * Initialize all queue entries to contain valid addresses.
	 */
	for (i = 0; i < queue->limit; i++) {
		desc_addr = desc_offset(queue, addr, i);
		desc_dma = desc_offset(queue, dma, i);
		data_addr = data_offset(queue, addr, i);
		data_dma = data_offset(queue, dma, i);

		if (queue->qid == QID_RX) {
			priv_rx = queue->entries[i].priv_data;
			priv_rx->desc = desc_addr;
			priv_rx->desc_dma = desc_dma;
			priv_rx->data = data_addr;
			priv_rx->data_dma = data_dma;
		} else {
			priv_tx = queue->entries[i].priv_data;
			priv_tx->desc = desc_addr;
			priv_tx->desc_dma = desc_dma;
			priv_tx->data = data_addr;
			priv_tx->data_dma = data_dma;
		}
	}

	return 0;
}

static void rt2x00pci_free_queue_dma(struct rt2x00_dev *rt2x00dev,
				     struct data_queue *queue)
{
	struct pci_dev *pci_dev = rt2x00dev_pci(rt2x00dev);
	struct queue_entry_priv_pci_rx *priv_rx;
	struct queue_entry_priv_pci_tx *priv_tx;
	void *data_addr;
	dma_addr_t data_dma;

	if (queue->qid == QID_RX) {
		priv_rx = queue->entries[0].priv_data;
		data_addr = priv_rx->data;
		data_dma = priv_rx->data_dma;

		priv_rx->data = NULL;
	} else {
		priv_tx = queue->entries[0].priv_data;
		data_addr = priv_tx->data;
		data_dma = priv_tx->data_dma;

		priv_tx->data = NULL;
	}

	if (data_addr)
		pci_free_consistent(pci_dev, dma_size(queue),
				    data_addr, data_dma);
}

int rt2x00pci_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct pci_dev *pci_dev = rt2x00dev_pci(rt2x00dev);
	struct data_queue *queue;
	int status;

	/*
	 * Allocate DMA
	 */
	queue_for_each(rt2x00dev, queue) {
		status = rt2x00pci_alloc_queue_dma(rt2x00dev, queue);
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
		goto exit;
	}

	return 0;

exit:
	queue_for_each(rt2x00dev, queue)
		rt2x00pci_free_queue_dma(rt2x00dev, queue);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00pci_initialize);

void rt2x00pci_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	/*
	 * Free irq line.
	 */
	free_irq(rt2x00dev_pci(rt2x00dev)->irq, rt2x00dev);

	/*
	 * Free DMA
	 */
	queue_for_each(rt2x00dev, queue)
		rt2x00pci_free_queue_dma(rt2x00dev, queue);
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

	if (rt2x00dev->csr.base) {
		iounmap(rt2x00dev->csr.base);
		rt2x00dev->csr.base = NULL;
	}
}

static int rt2x00pci_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	struct pci_dev *pci_dev = rt2x00dev_pci(rt2x00dev);

	rt2x00dev->csr.base = ioremap(pci_resource_start(pci_dev, 0),
				      pci_resource_len(pci_dev, 0));
	if (!rt2x00dev->csr.base)
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
MODULE_DESCRIPTION("rt2x00 pci library");
MODULE_LICENSE("GPL");
