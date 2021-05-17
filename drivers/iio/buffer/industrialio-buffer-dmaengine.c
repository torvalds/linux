// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2014-2015 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer_impl.h>
#include <linux/iio/buffer-dma.h>
#include <linux/iio/buffer-dmaengine.h>

/*
 * The IIO DMAengine buffer combines the generic IIO DMA buffer infrastructure
 * with the DMAengine framework. The generic IIO DMA buffer infrastructure is
 * used to manage the buffer memory and implement the IIO buffer operations
 * while the DMAengine framework is used to perform the DMA transfers. Combined
 * this results in a device independent fully functional DMA buffer
 * implementation that can be used by device drivers for peripherals which are
 * connected to a DMA controller which has a DMAengine driver implementation.
 */

struct dmaengine_buffer {
	struct iio_dma_buffer_queue queue;

	struct dma_chan *chan;
	struct list_head active;

	size_t align;
	size_t max_size;
};

static struct dmaengine_buffer *iio_buffer_to_dmaengine_buffer(
		struct iio_buffer *buffer)
{
	return container_of(buffer, struct dmaengine_buffer, queue.buffer);
}

static void iio_dmaengine_buffer_block_done(void *data,
		const struct dmaengine_result *result)
{
	struct iio_dma_buffer_block *block = data;
	unsigned long flags;

	spin_lock_irqsave(&block->queue->list_lock, flags);
	list_del(&block->head);
	spin_unlock_irqrestore(&block->queue->list_lock, flags);
	block->bytes_used -= result->residue;
	iio_dma_buffer_block_done(block);
}

static int iio_dmaengine_buffer_submit_block(struct iio_dma_buffer_queue *queue,
	struct iio_dma_buffer_block *block)
{
	struct dmaengine_buffer *dmaengine_buffer =
		iio_buffer_to_dmaengine_buffer(&queue->buffer);
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;

	block->bytes_used = min(block->size, dmaengine_buffer->max_size);
	block->bytes_used = rounddown(block->bytes_used,
			dmaengine_buffer->align);

	desc = dmaengine_prep_slave_single(dmaengine_buffer->chan,
		block->phys_addr, block->bytes_used, DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT);
	if (!desc)
		return -ENOMEM;

	desc->callback_result = iio_dmaengine_buffer_block_done;
	desc->callback_param = block;

	cookie = dmaengine_submit(desc);
	if (dma_submit_error(cookie))
		return dma_submit_error(cookie);

	spin_lock_irq(&dmaengine_buffer->queue.list_lock);
	list_add_tail(&block->head, &dmaengine_buffer->active);
	spin_unlock_irq(&dmaengine_buffer->queue.list_lock);

	dma_async_issue_pending(dmaengine_buffer->chan);

	return 0;
}

static void iio_dmaengine_buffer_abort(struct iio_dma_buffer_queue *queue)
{
	struct dmaengine_buffer *dmaengine_buffer =
		iio_buffer_to_dmaengine_buffer(&queue->buffer);

	dmaengine_terminate_sync(dmaengine_buffer->chan);
	iio_dma_buffer_block_list_abort(queue, &dmaengine_buffer->active);
}

static void iio_dmaengine_buffer_release(struct iio_buffer *buf)
{
	struct dmaengine_buffer *dmaengine_buffer =
		iio_buffer_to_dmaengine_buffer(buf);

	iio_dma_buffer_release(&dmaengine_buffer->queue);
	kfree(dmaengine_buffer);
}

static const struct iio_buffer_access_funcs iio_dmaengine_buffer_ops = {
	.read = iio_dma_buffer_read,
	.set_bytes_per_datum = iio_dma_buffer_set_bytes_per_datum,
	.set_length = iio_dma_buffer_set_length,
	.request_update = iio_dma_buffer_request_update,
	.enable = iio_dma_buffer_enable,
	.disable = iio_dma_buffer_disable,
	.data_available = iio_dma_buffer_data_available,
	.release = iio_dmaengine_buffer_release,

	.modes = INDIO_BUFFER_HARDWARE,
	.flags = INDIO_BUFFER_FLAG_FIXED_WATERMARK,
};

static const struct iio_dma_buffer_ops iio_dmaengine_default_ops = {
	.submit = iio_dmaengine_buffer_submit_block,
	.abort = iio_dmaengine_buffer_abort,
};

static ssize_t iio_dmaengine_buffer_get_length_align(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;
	struct dmaengine_buffer *dmaengine_buffer =
		iio_buffer_to_dmaengine_buffer(buffer);

	return sprintf(buf, "%zu\n", dmaengine_buffer->align);
}

static IIO_DEVICE_ATTR(length_align_bytes, 0444,
		       iio_dmaengine_buffer_get_length_align, NULL, 0);

static const struct attribute *iio_dmaengine_buffer_attrs[] = {
	&iio_dev_attr_length_align_bytes.dev_attr.attr,
	NULL,
};

/**
 * iio_dmaengine_buffer_alloc() - Allocate new buffer which uses DMAengine
 * @dev: Parent device for the buffer
 * @channel: DMA channel name, typically "rx".
 *
 * This allocates a new IIO buffer which internally uses the DMAengine framework
 * to perform its transfers. The parent device will be used to request the DMA
 * channel.
 *
 * Once done using the buffer iio_dmaengine_buffer_free() should be used to
 * release it.
 */
static struct iio_buffer *iio_dmaengine_buffer_alloc(struct device *dev,
	const char *channel)
{
	struct dmaengine_buffer *dmaengine_buffer;
	unsigned int width, src_width, dest_width;
	struct dma_slave_caps caps;
	struct dma_chan *chan;
	int ret;

	dmaengine_buffer = kzalloc(sizeof(*dmaengine_buffer), GFP_KERNEL);
	if (!dmaengine_buffer)
		return ERR_PTR(-ENOMEM);

	chan = dma_request_chan(dev, channel);
	if (IS_ERR(chan)) {
		ret = PTR_ERR(chan);
		goto err_free;
	}

	ret = dma_get_slave_caps(chan, &caps);
	if (ret < 0)
		goto err_free;

	/* Needs to be aligned to the maximum of the minimums */
	if (caps.src_addr_widths)
		src_width = __ffs(caps.src_addr_widths);
	else
		src_width = 1;
	if (caps.dst_addr_widths)
		dest_width = __ffs(caps.dst_addr_widths);
	else
		dest_width = 1;
	width = max(src_width, dest_width);

	INIT_LIST_HEAD(&dmaengine_buffer->active);
	dmaengine_buffer->chan = chan;
	dmaengine_buffer->align = width;
	dmaengine_buffer->max_size = dma_get_max_seg_size(chan->device->dev);

	iio_dma_buffer_init(&dmaengine_buffer->queue, chan->device->dev,
		&iio_dmaengine_default_ops);

	dmaengine_buffer->queue.buffer.attrs = iio_dmaengine_buffer_attrs;
	dmaengine_buffer->queue.buffer.access = &iio_dmaengine_buffer_ops;

	return &dmaengine_buffer->queue.buffer;

err_free:
	kfree(dmaengine_buffer);
	return ERR_PTR(ret);
}

/**
 * iio_dmaengine_buffer_free() - Free dmaengine buffer
 * @buffer: Buffer to free
 *
 * Frees a buffer previously allocated with iio_dmaengine_buffer_alloc().
 */
static void iio_dmaengine_buffer_free(struct iio_buffer *buffer)
{
	struct dmaengine_buffer *dmaengine_buffer =
		iio_buffer_to_dmaengine_buffer(buffer);

	iio_dma_buffer_exit(&dmaengine_buffer->queue);
	dma_release_channel(dmaengine_buffer->chan);

	iio_buffer_put(buffer);
}

static void __devm_iio_dmaengine_buffer_free(struct device *dev, void *res)
{
	iio_dmaengine_buffer_free(*(struct iio_buffer **)res);
}

/**
 * devm_iio_dmaengine_buffer_alloc() - Resource-managed iio_dmaengine_buffer_alloc()
 * @dev: Parent device for the buffer
 * @channel: DMA channel name, typically "rx".
 *
 * This allocates a new IIO buffer which internally uses the DMAengine framework
 * to perform its transfers. The parent device will be used to request the DMA
 * channel.
 *
 * The buffer will be automatically de-allocated once the device gets destroyed.
 */
static struct iio_buffer *devm_iio_dmaengine_buffer_alloc(struct device *dev,
	const char *channel)
{
	struct iio_buffer **bufferp, *buffer;

	bufferp = devres_alloc(__devm_iio_dmaengine_buffer_free,
			       sizeof(*bufferp), GFP_KERNEL);
	if (!bufferp)
		return ERR_PTR(-ENOMEM);

	buffer = iio_dmaengine_buffer_alloc(dev, channel);
	if (IS_ERR(buffer)) {
		devres_free(bufferp);
		return buffer;
	}

	*bufferp = buffer;
	devres_add(dev, bufferp);

	return buffer;
}

/**
 * devm_iio_dmaengine_buffer_setup() - Setup a DMA buffer for an IIO device
 * @dev: Parent device for the buffer
 * @indio_dev: IIO device to which to attach this buffer.
 * @channel: DMA channel name, typically "rx".
 *
 * This allocates a new IIO buffer with devm_iio_dmaengine_buffer_alloc()
 * and attaches it to an IIO device with iio_device_attach_buffer().
 * It also appends the INDIO_BUFFER_HARDWARE mode to the supported modes of the
 * IIO device.
 */
int devm_iio_dmaengine_buffer_setup(struct device *dev,
				    struct iio_dev *indio_dev,
				    const char *channel)
{
	struct iio_buffer *buffer;

	buffer = devm_iio_dmaengine_buffer_alloc(indio_dev->dev.parent,
						 channel);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	indio_dev->modes |= INDIO_BUFFER_HARDWARE;

	return iio_device_attach_buffer(indio_dev, buffer);
}
EXPORT_SYMBOL_GPL(devm_iio_dmaengine_buffer_setup);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("DMA buffer for the IIO framework");
MODULE_LICENSE("GPL");
