// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013-2015 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/iio/buffer_impl.h>
#include <linux/iio/buffer-dma.h>
#include <linux/dma-mapping.h>
#include <linux/sizes.h>

/*
 * For DMA buffers the storage is sub-divided into so called blocks. Each block
 * has its own memory buffer. The size of the block is the granularity at which
 * memory is exchanged between the hardware and the application. Increasing the
 * basic unit of data exchange from one sample to one block decreases the
 * management overhead that is associated with each sample. E.g. if we say the
 * management overhead for one exchange is x and the unit of exchange is one
 * sample the overhead will be x for each sample. Whereas when using a block
 * which contains n samples the overhead per sample is reduced to x/n. This
 * allows to achieve much higher samplerates than what can be sustained with
 * the one sample approach.
 *
 * Blocks are exchanged between the DMA controller and the application via the
 * means of two queues. The incoming queue and the outgoing queue. Blocks on the
 * incoming queue are waiting for the DMA controller to pick them up and fill
 * them with data. Block on the outgoing queue have been filled with data and
 * are waiting for the application to dequeue them and read the data.
 *
 * A block can be in one of the following states:
 *  * Owned by the application. In this state the application can read data from
 *    the block.
 *  * On the incoming list: Blocks on the incoming list are queued up to be
 *    processed by the DMA controller.
 *  * Owned by the DMA controller: The DMA controller is processing the block
 *    and filling it with data.
 *  * On the outgoing list: Blocks on the outgoing list have been successfully
 *    processed by the DMA controller and contain data. They can be dequeued by
 *    the application.
 *  * Dead: A block that is dead has been marked as to be freed. It might still
 *    be owned by either the application or the DMA controller at the moment.
 *    But once they are done processing it instead of going to either the
 *    incoming or outgoing queue the block will be freed.
 *
 * In addition to this blocks are reference counted and the memory associated
 * with both the block structure as well as the storage memory for the block
 * will be freed when the last reference to the block is dropped. This means a
 * block must not be accessed without holding a reference.
 *
 * The iio_dma_buffer implementation provides a generic infrastructure for
 * managing the blocks.
 *
 * A driver for a specific piece of hardware that has DMA capabilities need to
 * implement the submit() callback from the iio_dma_buffer_ops structure. This
 * callback is supposed to initiate the DMA transfer copying data from the
 * converter to the memory region of the block. Once the DMA transfer has been
 * completed the driver must call iio_dma_buffer_block_done() for the completed
 * block.
 *
 * Prior to this it must set the bytes_used field of the block contains
 * the actual number of bytes in the buffer. Typically this will be equal to the
 * size of the block, but if the DMA hardware has certain alignment requirements
 * for the transfer length it might choose to use less than the full size. In
 * either case it is expected that bytes_used is a multiple of the bytes per
 * datum, i.e. the block must not contain partial samples.
 *
 * The driver must call iio_dma_buffer_block_done() for each block it has
 * received through its submit_block() callback, even if it does not actually
 * perform a DMA transfer for the block, e.g. because the buffer was disabled
 * before the block transfer was started. In this case it should set bytes_used
 * to 0.
 *
 * In addition it is recommended that a driver implements the abort() callback.
 * It will be called when the buffer is disabled and can be used to cancel
 * pending and stop active transfers.
 *
 * The specific driver implementation should use the default callback
 * implementations provided by this module for the iio_buffer_access_funcs
 * struct. It may overload some callbacks with custom variants if the hardware
 * has special requirements that are not handled by the generic functions. If a
 * driver chooses to overload a callback it has to ensure that the generic
 * callback is called from within the custom callback.
 */

static void iio_buffer_block_release(struct kref *kref)
{
	struct iio_dma_buffer_block *block = container_of(kref,
		struct iio_dma_buffer_block, kref);

	WARN_ON(block->state != IIO_BLOCK_STATE_DEAD);

	dma_free_coherent(block->queue->dev, PAGE_ALIGN(block->size),
					block->vaddr, block->phys_addr);

	iio_buffer_put(&block->queue->buffer);
	kfree(block);
}

static void iio_buffer_block_get(struct iio_dma_buffer_block *block)
{
	kref_get(&block->kref);
}

static void iio_buffer_block_put(struct iio_dma_buffer_block *block)
{
	kref_put(&block->kref, iio_buffer_block_release);
}

/*
 * dma_free_coherent can sleep, hence we need to take some special care to be
 * able to drop a reference from an atomic context.
 */
static LIST_HEAD(iio_dma_buffer_dead_blocks);
static DEFINE_SPINLOCK(iio_dma_buffer_dead_blocks_lock);

static void iio_dma_buffer_cleanup_worker(struct work_struct *work)
{
	struct iio_dma_buffer_block *block, *_block;
	LIST_HEAD(block_list);

	spin_lock_irq(&iio_dma_buffer_dead_blocks_lock);
	list_splice_tail_init(&iio_dma_buffer_dead_blocks, &block_list);
	spin_unlock_irq(&iio_dma_buffer_dead_blocks_lock);

	list_for_each_entry_safe(block, _block, &block_list, head)
		iio_buffer_block_release(&block->kref);
}
static DECLARE_WORK(iio_dma_buffer_cleanup_work, iio_dma_buffer_cleanup_worker);

static void iio_buffer_block_release_atomic(struct kref *kref)
{
	struct iio_dma_buffer_block *block;
	unsigned long flags;

	block = container_of(kref, struct iio_dma_buffer_block, kref);

	spin_lock_irqsave(&iio_dma_buffer_dead_blocks_lock, flags);
	list_add_tail(&block->head, &iio_dma_buffer_dead_blocks);
	spin_unlock_irqrestore(&iio_dma_buffer_dead_blocks_lock, flags);

	schedule_work(&iio_dma_buffer_cleanup_work);
}

/*
 * Version of iio_buffer_block_put() that can be called from atomic context
 */
static void iio_buffer_block_put_atomic(struct iio_dma_buffer_block *block)
{
	kref_put(&block->kref, iio_buffer_block_release_atomic);
}

static struct iio_dma_buffer_queue *iio_buffer_to_queue(struct iio_buffer *buf)
{
	return container_of(buf, struct iio_dma_buffer_queue, buffer);
}

static struct iio_dma_buffer_block *iio_dma_buffer_alloc_block(
	struct iio_dma_buffer_queue *queue, size_t size)
{
	struct iio_dma_buffer_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return NULL;

	block->vaddr = dma_alloc_coherent(queue->dev, PAGE_ALIGN(size),
		&block->phys_addr, GFP_KERNEL);
	if (!block->vaddr) {
		kfree(block);
		return NULL;
	}

	block->size = size;
	block->state = IIO_BLOCK_STATE_DONE;
	block->queue = queue;
	INIT_LIST_HEAD(&block->head);
	kref_init(&block->kref);

	iio_buffer_get(&queue->buffer);

	return block;
}

static void _iio_dma_buffer_block_done(struct iio_dma_buffer_block *block)
{
	if (block->state != IIO_BLOCK_STATE_DEAD)
		block->state = IIO_BLOCK_STATE_DONE;
}

static void iio_dma_buffer_queue_wake(struct iio_dma_buffer_queue *queue)
{
	__poll_t flags;

	if (queue->buffer.direction == IIO_BUFFER_DIRECTION_IN)
		flags = EPOLLIN | EPOLLRDNORM;
	else
		flags = EPOLLOUT | EPOLLWRNORM;

	wake_up_interruptible_poll(&queue->buffer.pollq, flags);
}

/**
 * iio_dma_buffer_block_done() - Indicate that a block has been completed
 * @block: The completed block
 *
 * Should be called when the DMA controller has finished handling the block to
 * pass back ownership of the block to the queue.
 */
void iio_dma_buffer_block_done(struct iio_dma_buffer_block *block)
{
	struct iio_dma_buffer_queue *queue = block->queue;
	unsigned long flags;

	spin_lock_irqsave(&queue->list_lock, flags);
	_iio_dma_buffer_block_done(block);
	spin_unlock_irqrestore(&queue->list_lock, flags);

	iio_buffer_block_put_atomic(block);
	iio_dma_buffer_queue_wake(queue);
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_block_done);

/**
 * iio_dma_buffer_block_list_abort() - Indicate that a list block has been
 *   aborted
 * @queue: Queue for which to complete blocks.
 * @list: List of aborted blocks. All blocks in this list must be from @queue.
 *
 * Typically called from the abort() callback after the DMA controller has been
 * stopped. This will set bytes_used to 0 for each block in the list and then
 * hand the blocks back to the queue.
 */
void iio_dma_buffer_block_list_abort(struct iio_dma_buffer_queue *queue,
	struct list_head *list)
{
	struct iio_dma_buffer_block *block, *_block;
	unsigned long flags;

	spin_lock_irqsave(&queue->list_lock, flags);
	list_for_each_entry_safe(block, _block, list, head) {
		list_del(&block->head);
		block->bytes_used = 0;
		_iio_dma_buffer_block_done(block);
		iio_buffer_block_put_atomic(block);
	}
	spin_unlock_irqrestore(&queue->list_lock, flags);

	iio_dma_buffer_queue_wake(queue);
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_block_list_abort);

static bool iio_dma_block_reusable(struct iio_dma_buffer_block *block)
{
	/*
	 * If the core owns the block it can be re-used. This should be the
	 * default case when enabling the buffer, unless the DMA controller does
	 * not support abort and has not given back the block yet.
	 */
	switch (block->state) {
	case IIO_BLOCK_STATE_QUEUED:
	case IIO_BLOCK_STATE_DONE:
		return true;
	default:
		return false;
	}
}

/**
 * iio_dma_buffer_request_update() - DMA buffer request_update callback
 * @buffer: The buffer which to request an update
 *
 * Should be used as the iio_dma_buffer_request_update() callback for
 * iio_buffer_access_ops struct for DMA buffers.
 */
int iio_dma_buffer_request_update(struct iio_buffer *buffer)
{
	struct iio_dma_buffer_queue *queue = iio_buffer_to_queue(buffer);
	struct iio_dma_buffer_block *block;
	bool try_reuse = false;
	size_t size;
	int ret = 0;
	int i;

	/*
	 * Split the buffer into two even parts. This is used as a double
	 * buffering scheme with usually one block at a time being used by the
	 * DMA and the other one by the application.
	 */
	size = DIV_ROUND_UP(queue->buffer.bytes_per_datum *
		queue->buffer.length, 2);

	mutex_lock(&queue->lock);

	/* Allocations are page aligned */
	if (PAGE_ALIGN(queue->fileio.block_size) == PAGE_ALIGN(size))
		try_reuse = true;

	queue->fileio.block_size = size;
	queue->fileio.active_block = NULL;

	spin_lock_irq(&queue->list_lock);
	for (i = 0; i < ARRAY_SIZE(queue->fileio.blocks); i++) {
		block = queue->fileio.blocks[i];

		/* If we can't re-use it free it */
		if (block && (!iio_dma_block_reusable(block) || !try_reuse))
			block->state = IIO_BLOCK_STATE_DEAD;
	}

	/*
	 * At this point all blocks are either owned by the core or marked as
	 * dead. This means we can reset the lists without having to fear
	 * corrution.
	 */
	spin_unlock_irq(&queue->list_lock);

	INIT_LIST_HEAD(&queue->incoming);

	for (i = 0; i < ARRAY_SIZE(queue->fileio.blocks); i++) {
		if (queue->fileio.blocks[i]) {
			block = queue->fileio.blocks[i];
			if (block->state == IIO_BLOCK_STATE_DEAD) {
				/* Could not reuse it */
				iio_buffer_block_put(block);
				block = NULL;
			} else {
				block->size = size;
			}
		} else {
			block = NULL;
		}

		if (!block) {
			block = iio_dma_buffer_alloc_block(queue, size);
			if (!block) {
				ret = -ENOMEM;
				goto out_unlock;
			}
			queue->fileio.blocks[i] = block;
		}

		/*
		 * block->bytes_used may have been modified previously, e.g. by
		 * iio_dma_buffer_block_list_abort(). Reset it here to the
		 * block's so that iio_dma_buffer_io() will work.
		 */
		block->bytes_used = block->size;

		/*
		 * If it's an input buffer, mark the block as queued, and
		 * iio_dma_buffer_enable() will submit it. Otherwise mark it as
		 * done, which means it's ready to be dequeued.
		 */
		if (queue->buffer.direction == IIO_BUFFER_DIRECTION_IN) {
			block->state = IIO_BLOCK_STATE_QUEUED;
			list_add_tail(&block->head, &queue->incoming);
		} else {
			block->state = IIO_BLOCK_STATE_DONE;
		}
	}

out_unlock:
	mutex_unlock(&queue->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_request_update);

static void iio_dma_buffer_fileio_free(struct iio_dma_buffer_queue *queue)
{
	unsigned int i;

	spin_lock_irq(&queue->list_lock);
	for (i = 0; i < ARRAY_SIZE(queue->fileio.blocks); i++) {
		if (!queue->fileio.blocks[i])
			continue;
		queue->fileio.blocks[i]->state = IIO_BLOCK_STATE_DEAD;
	}
	spin_unlock_irq(&queue->list_lock);

	INIT_LIST_HEAD(&queue->incoming);

	for (i = 0; i < ARRAY_SIZE(queue->fileio.blocks); i++) {
		if (!queue->fileio.blocks[i])
			continue;
		iio_buffer_block_put(queue->fileio.blocks[i]);
		queue->fileio.blocks[i] = NULL;
	}
	queue->fileio.active_block = NULL;
}

static void iio_dma_buffer_submit_block(struct iio_dma_buffer_queue *queue,
	struct iio_dma_buffer_block *block)
{
	int ret;

	/*
	 * If the hardware has already been removed we put the block into
	 * limbo. It will neither be on the incoming nor outgoing list, nor will
	 * it ever complete. It will just wait to be freed eventually.
	 */
	if (!queue->ops)
		return;

	block->state = IIO_BLOCK_STATE_ACTIVE;
	iio_buffer_block_get(block);
	ret = queue->ops->submit(queue, block);
	if (ret) {
		/*
		 * This is a bit of a problem and there is not much we can do
		 * other then wait for the buffer to be disabled and re-enabled
		 * and try again. But it should not really happen unless we run
		 * out of memory or something similar.
		 *
		 * TODO: Implement support in the IIO core to allow buffers to
		 * notify consumers that something went wrong and the buffer
		 * should be disabled.
		 */
		iio_buffer_block_put(block);
	}
}

/**
 * iio_dma_buffer_enable() - Enable DMA buffer
 * @buffer: IIO buffer to enable
 * @indio_dev: IIO device the buffer is attached to
 *
 * Needs to be called when the device that the buffer is attached to starts
 * sampling. Typically should be the iio_buffer_access_ops enable callback.
 *
 * This will allocate the DMA buffers and start the DMA transfers.
 */
int iio_dma_buffer_enable(struct iio_buffer *buffer,
	struct iio_dev *indio_dev)
{
	struct iio_dma_buffer_queue *queue = iio_buffer_to_queue(buffer);
	struct iio_dma_buffer_block *block, *_block;

	mutex_lock(&queue->lock);
	queue->active = true;
	list_for_each_entry_safe(block, _block, &queue->incoming, head) {
		list_del(&block->head);
		iio_dma_buffer_submit_block(queue, block);
	}
	mutex_unlock(&queue->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_enable);

/**
 * iio_dma_buffer_disable() - Disable DMA buffer
 * @buffer: IIO DMA buffer to disable
 * @indio_dev: IIO device the buffer is attached to
 *
 * Needs to be called when the device that the buffer is attached to stops
 * sampling. Typically should be the iio_buffer_access_ops disable callback.
 */
int iio_dma_buffer_disable(struct iio_buffer *buffer,
	struct iio_dev *indio_dev)
{
	struct iio_dma_buffer_queue *queue = iio_buffer_to_queue(buffer);

	mutex_lock(&queue->lock);
	queue->active = false;

	if (queue->ops && queue->ops->abort)
		queue->ops->abort(queue);
	mutex_unlock(&queue->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_disable);

static void iio_dma_buffer_enqueue(struct iio_dma_buffer_queue *queue,
	struct iio_dma_buffer_block *block)
{
	if (block->state == IIO_BLOCK_STATE_DEAD) {
		iio_buffer_block_put(block);
	} else if (queue->active) {
		iio_dma_buffer_submit_block(queue, block);
	} else {
		block->state = IIO_BLOCK_STATE_QUEUED;
		list_add_tail(&block->head, &queue->incoming);
	}
}

static struct iio_dma_buffer_block *iio_dma_buffer_dequeue(
	struct iio_dma_buffer_queue *queue)
{
	struct iio_dma_buffer_block *block;
	unsigned int idx;

	spin_lock_irq(&queue->list_lock);

	idx = queue->fileio.next_dequeue;
	block = queue->fileio.blocks[idx];

	if (block->state == IIO_BLOCK_STATE_DONE) {
		idx = (idx + 1) % ARRAY_SIZE(queue->fileio.blocks);
		queue->fileio.next_dequeue = idx;
	} else {
		block = NULL;
	}

	spin_unlock_irq(&queue->list_lock);

	return block;
}

static int iio_dma_buffer_io(struct iio_buffer *buffer, size_t n,
			     char __user *user_buffer, bool is_from_user)
{
	struct iio_dma_buffer_queue *queue = iio_buffer_to_queue(buffer);
	struct iio_dma_buffer_block *block;
	void *addr;
	int ret;

	if (n < buffer->bytes_per_datum)
		return -EINVAL;

	mutex_lock(&queue->lock);

	if (!queue->fileio.active_block) {
		block = iio_dma_buffer_dequeue(queue);
		if (block == NULL) {
			ret = 0;
			goto out_unlock;
		}
		queue->fileio.pos = 0;
		queue->fileio.active_block = block;
	} else {
		block = queue->fileio.active_block;
	}

	n = rounddown(n, buffer->bytes_per_datum);
	if (n > block->bytes_used - queue->fileio.pos)
		n = block->bytes_used - queue->fileio.pos;
	addr = block->vaddr + queue->fileio.pos;

	if (is_from_user)
		ret = copy_from_user(addr, user_buffer, n);
	else
		ret = copy_to_user(user_buffer, addr, n);
	if (ret) {
		ret = -EFAULT;
		goto out_unlock;
	}

	queue->fileio.pos += n;

	if (queue->fileio.pos == block->bytes_used) {
		queue->fileio.active_block = NULL;
		iio_dma_buffer_enqueue(queue, block);
	}

	ret = n;

out_unlock:
	mutex_unlock(&queue->lock);

	return ret;
}

/**
 * iio_dma_buffer_read() - DMA buffer read callback
 * @buffer: Buffer to read form
 * @n: Number of bytes to read
 * @user_buffer: Userspace buffer to copy the data to
 *
 * Should be used as the read callback for iio_buffer_access_ops
 * struct for DMA buffers.
 */
int iio_dma_buffer_read(struct iio_buffer *buffer, size_t n,
			char __user *user_buffer)
{
	return iio_dma_buffer_io(buffer, n, user_buffer, false);
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_read);

/**
 * iio_dma_buffer_write() - DMA buffer write callback
 * @buffer: Buffer to read form
 * @n: Number of bytes to read
 * @user_buffer: Userspace buffer to copy the data from
 *
 * Should be used as the write callback for iio_buffer_access_ops
 * struct for DMA buffers.
 */
int iio_dma_buffer_write(struct iio_buffer *buffer, size_t n,
			 const char __user *user_buffer)
{
	return iio_dma_buffer_io(buffer, n,
				 (__force __user char *)user_buffer, true);
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_write);

/**
 * iio_dma_buffer_usage() - DMA buffer data_available and
 * space_available callback
 * @buf: Buffer to check for data availability
 *
 * Should be used as the data_available and space_available callbacks for
 * iio_buffer_access_ops struct for DMA buffers.
 */
size_t iio_dma_buffer_usage(struct iio_buffer *buf)
{
	struct iio_dma_buffer_queue *queue = iio_buffer_to_queue(buf);
	struct iio_dma_buffer_block *block;
	size_t data_available = 0;
	unsigned int i;

	/*
	 * For counting the available bytes we'll use the size of the block not
	 * the number of actual bytes available in the block. Otherwise it is
	 * possible that we end up with a value that is lower than the watermark
	 * but won't increase since all blocks are in use.
	 */

	mutex_lock(&queue->lock);
	if (queue->fileio.active_block)
		data_available += queue->fileio.active_block->size;

	spin_lock_irq(&queue->list_lock);

	for (i = 0; i < ARRAY_SIZE(queue->fileio.blocks); i++) {
		block = queue->fileio.blocks[i];

		if (block != queue->fileio.active_block
		    && block->state == IIO_BLOCK_STATE_DONE)
			data_available += block->size;
	}

	spin_unlock_irq(&queue->list_lock);
	mutex_unlock(&queue->lock);

	return data_available;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_usage);

/**
 * iio_dma_buffer_set_bytes_per_datum() - DMA buffer set_bytes_per_datum callback
 * @buffer: Buffer to set the bytes-per-datum for
 * @bpd: The new bytes-per-datum value
 *
 * Should be used as the set_bytes_per_datum callback for iio_buffer_access_ops
 * struct for DMA buffers.
 */
int iio_dma_buffer_set_bytes_per_datum(struct iio_buffer *buffer, size_t bpd)
{
	buffer->bytes_per_datum = bpd;

	return 0;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_set_bytes_per_datum);

/**
 * iio_dma_buffer_set_length - DMA buffer set_length callback
 * @buffer: Buffer to set the length for
 * @length: The new buffer length
 *
 * Should be used as the set_length callback for iio_buffer_access_ops
 * struct for DMA buffers.
 */
int iio_dma_buffer_set_length(struct iio_buffer *buffer, unsigned int length)
{
	/* Avoid an invalid state */
	if (length < 2)
		length = 2;
	buffer->length = length;
	buffer->watermark = length / 2;

	return 0;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_set_length);

/**
 * iio_dma_buffer_init() - Initialize DMA buffer queue
 * @queue: Buffer to initialize
 * @dev: DMA device
 * @ops: DMA buffer queue callback operations
 *
 * The DMA device will be used by the queue to do DMA memory allocations. So it
 * should refer to the device that will perform the DMA to ensure that
 * allocations are done from a memory region that can be accessed by the device.
 */
int iio_dma_buffer_init(struct iio_dma_buffer_queue *queue,
	struct device *dev, const struct iio_dma_buffer_ops *ops)
{
	iio_buffer_init(&queue->buffer);
	queue->buffer.length = PAGE_SIZE;
	queue->buffer.watermark = queue->buffer.length / 2;
	queue->dev = dev;
	queue->ops = ops;

	INIT_LIST_HEAD(&queue->incoming);

	mutex_init(&queue->lock);
	spin_lock_init(&queue->list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_init);

/**
 * iio_dma_buffer_exit() - Cleanup DMA buffer queue
 * @queue: Buffer to cleanup
 *
 * After this function has completed it is safe to free any resources that are
 * associated with the buffer and are accessed inside the callback operations.
 */
void iio_dma_buffer_exit(struct iio_dma_buffer_queue *queue)
{
	mutex_lock(&queue->lock);

	iio_dma_buffer_fileio_free(queue);
	queue->ops = NULL;

	mutex_unlock(&queue->lock);
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_exit);

/**
 * iio_dma_buffer_release() - Release final buffer resources
 * @queue: Buffer to release
 *
 * Frees resources that can't yet be freed in iio_dma_buffer_exit(). Should be
 * called in the buffers release callback implementation right before freeing
 * the memory associated with the buffer.
 */
void iio_dma_buffer_release(struct iio_dma_buffer_queue *queue)
{
	mutex_destroy(&queue->lock);
}
EXPORT_SYMBOL_GPL(iio_dma_buffer_release);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("DMA buffer for the IIO framework");
MODULE_LICENSE("GPL v2");
