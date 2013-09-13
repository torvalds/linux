/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
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
	Module: rt2x00mmio
	Abstract: rt2x00 generic mmio device routines.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "rt2x00.h"
#include "rt2x00mmio.h"

/*
 * Register access.
 */
int rt2x00mmio_regbusy_read(struct rt2x00_dev *rt2x00dev,
			    const unsigned int offset,
			    const struct rt2x00_field32 field,
			    u32 *reg)
{
	unsigned int i;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return 0;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00mmio_register_read(rt2x00dev, offset, reg);
		if (!rt2x00_get_field32(*reg, field))
			return 1;
		udelay(REGISTER_BUSY_DELAY);
	}

	printk_once(KERN_ERR "%s() Indirect register access failed: "
	      "offset=0x%.08x, value=0x%.08x\n", __func__, offset, *reg);
	*reg = ~0;

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mmio_regbusy_read);

bool rt2x00mmio_rxdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue = rt2x00dev->rx;
	struct queue_entry *entry;
	struct queue_entry_priv_mmio *entry_priv;
	struct skb_frame_desc *skbdesc;
	int max_rx = 16;

	while (--max_rx) {
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		entry_priv = entry->priv_data;

		if (rt2x00dev->ops->lib->get_entry_state(entry))
			break;

		/*
		 * Fill in desc fields of the skb descriptor
		 */
		skbdesc = get_skb_frame_desc(entry->skb);
		skbdesc->desc = entry_priv->desc;
		skbdesc->desc_len = entry->queue->desc_size;

		/*
		 * DMA is already done, notify rt2x00lib that
		 * it finished successfully.
		 */
		rt2x00lib_dmastart(entry);
		rt2x00lib_dmadone(entry);

		/*
		 * Send the frame to rt2x00lib for further processing.
		 */
		rt2x00lib_rxdone(entry, GFP_ATOMIC);
	}

	return !max_rx;
}
EXPORT_SYMBOL_GPL(rt2x00mmio_rxdone);

void rt2x00mmio_flush_queue(struct data_queue *queue, bool drop)
{
	unsigned int i;

	for (i = 0; !rt2x00queue_empty(queue) && i < 10; i++)
		msleep(10);
}
EXPORT_SYMBOL_GPL(rt2x00mmio_flush_queue);

/*
 * Device initialization handlers.
 */
static int rt2x00mmio_alloc_queue_dma(struct rt2x00_dev *rt2x00dev,
				      struct data_queue *queue)
{
	struct queue_entry_priv_mmio *entry_priv;
	void *addr;
	dma_addr_t dma;
	unsigned int i;

	/*
	 * Allocate DMA memory for descriptor and buffer.
	 */
	addr = dma_alloc_coherent(rt2x00dev->dev,
				  queue->limit * queue->desc_size,
				  &dma, GFP_KERNEL);
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

static void rt2x00mmio_free_queue_dma(struct rt2x00_dev *rt2x00dev,
				      struct data_queue *queue)
{
	struct queue_entry_priv_mmio *entry_priv =
	    queue->entries[0].priv_data;

	if (entry_priv->desc)
		dma_free_coherent(rt2x00dev->dev,
				  queue->limit * queue->desc_size,
				  entry_priv->desc, entry_priv->desc_dma);
	entry_priv->desc = NULL;
}

int rt2x00mmio_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	int status;

	/*
	 * Allocate DMA
	 */
	queue_for_each(rt2x00dev, queue) {
		status = rt2x00mmio_alloc_queue_dma(rt2x00dev, queue);
		if (status)
			goto exit;
	}

	/*
	 * Register interrupt handler.
	 */
	status = request_irq(rt2x00dev->irq,
			     rt2x00dev->ops->lib->irq_handler,
			     IRQF_SHARED, rt2x00dev->name, rt2x00dev);
	if (status) {
		rt2x00_err(rt2x00dev, "IRQ %d allocation failed (error %d)\n",
			   rt2x00dev->irq, status);
		goto exit;
	}

	return 0;

exit:
	queue_for_each(rt2x00dev, queue)
		rt2x00mmio_free_queue_dma(rt2x00dev, queue);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00mmio_initialize);

void rt2x00mmio_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	/*
	 * Free irq line.
	 */
	free_irq(rt2x00dev->irq, rt2x00dev);

	/*
	 * Free DMA
	 */
	queue_for_each(rt2x00dev, queue)
		rt2x00mmio_free_queue_dma(rt2x00dev, queue);
}
EXPORT_SYMBOL_GPL(rt2x00mmio_uninitialize);

/*
 * rt2x00mmio module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 mmio library");
MODULE_LICENSE("GPL");
