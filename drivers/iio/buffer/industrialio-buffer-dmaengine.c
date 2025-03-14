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
	enum dma_transfer_direction dma_dir;
	struct scatterlist *sgl;
	struct dma_vec *vecs;
	size_t max_size;
	dma_cookie_t cookie;
	size_t len_total;
	unsigned int i;
	int nents;

	max_size = min(block->size, dmaengine_buffer->max_size);
	max_size = round_down(max_size, dmaengine_buffer->align);

	if (queue->buffer.direction == IIO_BUFFER_DIRECTION_IN)
		dma_dir = DMA_DEV_TO_MEM;
	else
		dma_dir = DMA_MEM_TO_DEV;

	if (block->sg_table) {
		sgl = block->sg_table->sgl;
		nents = sg_nents_for_len(sgl, block->bytes_used);
		if (nents < 0)
			return nents;

		vecs = kmalloc_array(nents, sizeof(*vecs), GFP_ATOMIC);
		if (!vecs)
			return -ENOMEM;

		len_total = block->bytes_used;

		for (i = 0; i < nents; i++) {
			vecs[i].addr = sg_dma_address(sgl);
			vecs[i].len = min(sg_dma_len(sgl), len_total);
			len_total -= vecs[i].len;

			sgl = sg_next(sgl);
		}

		desc = dmaengine_prep_peripheral_dma_vec(dmaengine_buffer->chan,
							 vecs, nents, dma_dir,
							 DMA_PREP_INTERRUPT);
		kfree(vecs);
	} else {
		max_size = min(block->size, dmaengine_buffer->max_size);
		max_size = round_down(max_size, dmaengine_buffer->align);

		if (queue->buffer.direction == IIO_BUFFER_DIRECTION_IN)
			block->bytes_used = max_size;

		if (!block->bytes_used || block->bytes_used > max_size)
			return -EINVAL;

		desc = dmaengine_prep_slave_single(dmaengine_buffer->chan,
						   block->phys_addr,
						   block->bytes_used,
						   dma_dir,
						   DMA_PREP_INTERRUPT);
	}
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
	.write = iio_dma_buffer_write,
	.set_bytes_per_datum = iio_dma_buffer_set_bytes_per_datum,
	.set_length = iio_dma_buffer_set_length,
	.request_update = iio_dma_buffer_request_update,
	.enable = iio_dma_buffer_enable,
	.disable = iio_dma_buffer_disable,
	.data_available = iio_dma_buffer_usage,
	.space_available = iio_dma_buffer_usage,
	.release = iio_dmaengine_buffer_release,

	.enqueue_dmabuf = iio_dma_buffer_enqueue_dmabuf,
	.attach_dmabuf = iio_dma_buffer_attach_dmabuf,
	.detach_dmabuf = iio_dma_buffer_detach_dmabuf,

	.lock_queue = iio_dma_buffer_lock_queue,
	.unlock_queue = iio_dma_buffer_unlock_queue,

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

	return sysfs_emit(buf, "%zu\n", dmaengine_buffer->align);
}

static IIO_DEVICE_ATTR(length_align_bytes, 0444,
		       iio_dmaengine_buffer_get_length_align, NULL, 0);

static const struct iio_dev_attr *iio_dmaengine_buffer_attrs[] = {
	&iio_dev_attr_length_align_bytes,
	NULL,
};

/**
 * iio_dmaengine_buffer_alloc() - Allocate new buffer which uses DMAengine
 * @chan: DMA channel.
 *
 * This allocates a new IIO buffer which internally uses the DMAengine framework
 * to perform its transfers.
 *
 * Once done using the buffer iio_dmaengine_buffer_free() should be used to
 * release it.
 */
static struct iio_buffer *iio_dmaengine_buffer_alloc(struct dma_chan *chan)
{
	struct dmaengine_buffer *dmaengine_buffer;
	unsigned int width, src_width, dest_width;
	struct dma_slave_caps caps;
	int ret;

	ret = dma_get_slave_caps(chan, &caps);
	if (ret < 0)
		return ERR_PTR(ret);

	dmaengine_buffer = kzalloc(sizeof(*dmaengine_buffer), GFP_KERNEL);
	if (!dmaengine_buffer)
		return ERR_PTR(-ENOMEM);

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
	iio_buffer_put(buffer);
}

/**
 * iio_dmaengine_buffer_teardown() - Releases DMA channel and frees buffer
 * @buffer: Buffer to free
 *
 * Releases the DMA channel and frees the buffer previously setup with
 * iio_dmaengine_buffer_setup_ext().
 */
void iio_dmaengine_buffer_teardown(struct iio_buffer *buffer)
{
	struct dmaengine_buffer *dmaengine_buffer =
		iio_buffer_to_dmaengine_buffer(buffer);
	struct dma_chan *chan = dmaengine_buffer->chan;

	iio_dmaengine_buffer_free(buffer);
	dma_release_channel(chan);
}
EXPORT_SYMBOL_NS_GPL(iio_dmaengine_buffer_teardown, "IIO_DMAENGINE_BUFFER");

static struct iio_buffer
*__iio_dmaengine_buffer_setup_ext(struct iio_dev *indio_dev,
				  struct dma_chan *chan,
				  enum iio_buffer_direction dir)
{
	struct iio_buffer *buffer;
	int ret;

	buffer = iio_dmaengine_buffer_alloc(chan);
	if (IS_ERR(buffer))
		return ERR_CAST(buffer);

	indio_dev->modes |= INDIO_BUFFER_HARDWARE;

	buffer->direction = dir;

	ret = iio_device_attach_buffer(indio_dev, buffer);
	if (ret) {
		iio_dmaengine_buffer_free(buffer);
		return ERR_PTR(ret);
	}

	return buffer;
}

/**
 * iio_dmaengine_buffer_setup_ext() - Setup a DMA buffer for an IIO device
 * @dev: DMA channel consumer device
 * @indio_dev: IIO device to which to attach this buffer.
 * @channel: DMA channel name, typically "rx".
 * @dir: Direction of buffer (in or out)
 *
 * This allocates a new IIO buffer with devm_iio_dmaengine_buffer_alloc()
 * and attaches it to an IIO device with iio_device_attach_buffer().
 * It also appends the INDIO_BUFFER_HARDWARE mode to the supported modes of the
 * IIO device.
 *
 * Once done using the buffer iio_dmaengine_buffer_teardown() should be used to
 * release it.
 */
struct iio_buffer *iio_dmaengine_buffer_setup_ext(struct device *dev,
						  struct iio_dev *indio_dev,
						  const char *channel,
						  enum iio_buffer_direction dir)
{
	struct dma_chan *chan;
	struct iio_buffer *buffer;

	chan = dma_request_chan(dev, channel);
	if (IS_ERR(chan))
		return ERR_CAST(chan);

	buffer = __iio_dmaengine_buffer_setup_ext(indio_dev, chan, dir);
	if (IS_ERR(buffer))
		dma_release_channel(chan);

	return buffer;
}
EXPORT_SYMBOL_NS_GPL(iio_dmaengine_buffer_setup_ext, "IIO_DMAENGINE_BUFFER");

static void devm_iio_dmaengine_buffer_teardown(void *buffer)
{
	iio_dmaengine_buffer_teardown(buffer);
}

/**
 * devm_iio_dmaengine_buffer_setup_ext() - Setup a DMA buffer for an IIO device
 * @dev: Device for devm ownership and DMA channel consumer device
 * @indio_dev: IIO device to which to attach this buffer.
 * @channel: DMA channel name, typically "rx".
 * @dir: Direction of buffer (in or out)
 *
 * This allocates a new IIO buffer with devm_iio_dmaengine_buffer_alloc()
 * and attaches it to an IIO device with iio_device_attach_buffer().
 * It also appends the INDIO_BUFFER_HARDWARE mode to the supported modes of the
 * IIO device.
 */
int devm_iio_dmaengine_buffer_setup_ext(struct device *dev,
					struct iio_dev *indio_dev,
					const char *channel,
					enum iio_buffer_direction dir)
{
	struct iio_buffer *buffer;

	buffer = iio_dmaengine_buffer_setup_ext(dev, indio_dev, channel, dir);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	return devm_add_action_or_reset(dev, devm_iio_dmaengine_buffer_teardown,
					buffer);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_dmaengine_buffer_setup_ext, "IIO_DMAENGINE_BUFFER");

static void devm_iio_dmaengine_buffer_free(void *buffer)
{
	iio_dmaengine_buffer_free(buffer);
}

/**
 * devm_iio_dmaengine_buffer_setup_with_handle() - Setup a DMA buffer for an
 *						   IIO device
 * @dev: Device for devm ownership
 * @indio_dev: IIO device to which to attach this buffer.
 * @chan: DMA channel
 * @dir: Direction of buffer (in or out)
 *
 * This allocates a new IIO buffer with devm_iio_dmaengine_buffer_alloc()
 * and attaches it to an IIO device with iio_device_attach_buffer().
 * It also appends the INDIO_BUFFER_HARDWARE mode to the supported modes of the
 * IIO device.
 *
 * This is the same as devm_iio_dmaengine_buffer_setup_ext() except that the
 * caller manages requesting and releasing the DMA channel handle.
 */
int devm_iio_dmaengine_buffer_setup_with_handle(struct device *dev,
						struct iio_dev *indio_dev,
						struct dma_chan *chan,
						enum iio_buffer_direction dir)
{
	struct iio_buffer *buffer;

	buffer = __iio_dmaengine_buffer_setup_ext(indio_dev, chan, dir);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	return devm_add_action_or_reset(dev, devm_iio_dmaengine_buffer_free,
					buffer);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_dmaengine_buffer_setup_with_handle,
		     "IIO_DMAENGINE_BUFFER");

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("DMA buffer for the IIO framework");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMA_BUFFER");
