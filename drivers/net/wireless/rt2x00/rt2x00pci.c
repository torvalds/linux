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
int rt2x00pci_write_tx_data(struct queue_entry *entry)
{
	struct queue_entry_priv_pci *entry_priv = entry->priv_data;
	struct skb_frame_desc *skbdesc;
	u32 word;

	rt2x00_desc_read(entry_priv->desc, 0, &word);

	/*
	 * This should not happen, we already checked the entry
	 * was ours. When the hardware disagrees there has been
	 * a queue corruption!
	 */
	if (unlikely(rt2x00_get_field32(word, TXD_ENTRY_OWNER_NIC) ||
		     rt2x00_get_field32(word, TXD_ENTRY_VALID))) {
		ERROR(entry->queue->rt2x00dev,
		      "Corrupt queue %d, accessing entry which is not ours.\n"
		      "Please file bug report to %s.\n",
		      entry->queue->qid, DRV_PROJECT);
		return -EINVAL;
	}

	/*
	 * Fill in skb descriptor
	 */
	skbdesc = get_skb_frame_desc(entry->skb);
	skbdesc->desc = entry_priv->desc;
	skbdesc->desc_len = entry->queue->desc_size;

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
	struct queue_entry_priv_pci *entry_priv;
	struct skb_frame_desc *skbdesc;
	u32 word;

	while (1) {
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		entry_priv = entry->priv_data;
		rt2x00_desc_read(entry_priv->desc, 0, &word);

		if (rt2x00_get_field32(word, RXD_ENTRY_OWNER_NIC))
			break;

		/*
		 * Fill in desc fields of the skb descriptor
		 */
		skbdesc = get_skb_frame_desc(entry->skb);
		skbdesc->desc = entry_priv->desc;
		skbdesc->desc_len = entry->queue->desc_size;

		/*
		 * Send the frame to rt2x00lib for further processing.
		 */
		rt2x00lib_rxdone(rt2x00dev, entry);
	}
}
EXPORT_SYMBOL_GPL(rt2x00pci_rxdone);

/*
 * Device initialization handlers.
 */
static int rt2x00pci_alloc_queue_dma(struct rt2x00_dev *rt2x00dev,
				     struct data_queue *queue)
{
	struct queue_entry_priv_pci *entry_priv;
	void *addr;
	dma_addr_t dma;
	unsigned int i;

	/*
	 * Allocate DMA memory for descriptor and buffer.
	 */
	addr = dma_alloc_coherent(rt2x00dev->dev,
				  queue->limit * queue->desc_size,
				  &dma, GFP_KERNEL | GFP_DMA);
	if (!addr)
		return -ENOMEM;

	memset(addr, 0, queue->limit * queue->desc_size);

	/*
	 * Initialize all queue entries to contain valid addresses.
	 */
	for (i = 0; i < queue->limit; i++) {
		entry_priv = queue->entries[i].priv_data;
		entry_priv->desc = addr + i * queue->desc_size;
		entry_priv->desc_dma = dma + i * queue->desc_size;
	}

	return 0;
}

static void rt2x00pci_free_queue_dma(struct rt2x00_dev *rt2x00dev,
				     struct data_queue *queue)
{
	struct queue_entry_priv_pci *entry_priv =
	    queue->entries[0].priv_data;

	if (entry_priv->desc)
		dma_free_coherent(rt2x00dev->dev,
				  queue->limit * queue->desc_size,
				  entry_priv->desc, entry_priv->desc_dma);
	entry_priv->desc = NULL;
}

int rt2x00pci_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct pci_dev *pci_dev = to_pci_dev(rt2x00dev->dev);
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
	free_irq(to_pci_dev(rt2x00dev->dev)->irq, rt2x00dev);

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
	struct pci_dev *pci_dev = to_pci_dev(rt2x00dev->dev);

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

	if (dma_set_mask(&pci_dev->dev, DMA_32BIT_MASK)) {
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
	rt2x00dev->dev = &pci_dev->dev;
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
