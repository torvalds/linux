/*
 * drivers/video/tegra/host/nvhost_cdma.c
 *
 * Tegra Graphics Host Command DMA
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "nvhost_cdma.h"
#include "dev.h"
#include <asm/cacheflush.h>

/*
 * TODO:
 *   stats
 *     - for figuring out what to optimize further
 *   resizable push buffer & sync queue
 *     - some channels hardly need any, some channels (3d) could use more
 */

#define cdma_to_channel(cdma) container_of(cdma, struct nvhost_channel, cdma)
#define cdma_to_dev(cdma) ((cdma_to_channel(cdma))->dev)
#define cdma_to_nvmap(cdma) ((cdma_to_dev(cdma))->nvmap)
#define pb_to_cdma(pb) container_of(pb, struct nvhost_cdma, push_buffer)

/*
 * push_buffer
 *
 * The push buffer is a circular array of words to be fetched by command DMA.
 * Note that it works slightly differently to the sync queue; fence == cur
 * means that the push buffer is full, not empty.
 */

// 8 bytes per slot. (This number does not include the final RESTART.)
#define PUSH_BUFFER_SIZE (NVHOST_GATHER_QUEUE_SIZE * 8)

static void destroy_push_buffer(struct push_buffer *pb);

/**
 * Reset to empty push buffer
 */
static void reset_push_buffer(struct push_buffer *pb)
{
	pb->fence = PUSH_BUFFER_SIZE - 8;
	pb->cur = 0;
}

/**
 * Init push buffer resources
 */
static int init_push_buffer(struct push_buffer *pb)
{
	struct nvhost_cdma *cdma = pb_to_cdma(pb);
	struct nvmap_client *nvmap = cdma_to_nvmap(cdma);
	pb->mem = NULL;
	pb->mapped = NULL;
	pb->phys = 0;
	reset_push_buffer(pb);

	/* allocate and map pushbuffer memory */
	pb->mem = nvmap_alloc(nvmap, PUSH_BUFFER_SIZE + 4, 32,
			      NVMAP_HANDLE_WRITE_COMBINE);
	if (IS_ERR_OR_NULL(pb->mem)) {
		pb->mem = NULL;
		goto fail;
	}
	pb->mapped = nvmap_mmap(pb->mem);
	if (pb->mapped == NULL)
		goto fail;

	/* pin pushbuffer and get physical address */
	pb->phys = nvmap_pin(nvmap, pb->mem);
	if (pb->phys >= 0xfffff000) {
		pb->phys = 0;
		goto fail;
	}

	/* put the restart at the end of pushbuffer memory */
	*(pb->mapped + (PUSH_BUFFER_SIZE >> 2)) = nvhost_opcode_restart(pb->phys);

	return 0;

fail:
	destroy_push_buffer(pb);
	return -ENOMEM;
}

/**
 * Clean up push buffer resources
 */
static void destroy_push_buffer(struct push_buffer *pb)
{
	struct nvhost_cdma *cdma = pb_to_cdma(pb);
	struct nvmap_client *nvmap = cdma_to_nvmap(cdma);
	if (pb->mapped)
		nvmap_munmap(pb->mem, pb->mapped);

	if (pb->phys != 0)
		nvmap_unpin(nvmap, pb->mem);

	if (pb->mem)
		nvmap_free(nvmap, pb->mem);

	pb->mem = NULL;
	pb->mapped = NULL;
	pb->phys = 0;
}

/**
 * Push two words to the push buffer
 * Caller must ensure push buffer is not full
 */
static void push_to_push_buffer(struct push_buffer *pb, u32 op1, u32 op2)
{
	u32 cur = pb->cur;
	u32 *p = (u32*)((u32)pb->mapped + cur);
	BUG_ON(cur == pb->fence);
	*(p++) = op1;
	*(p++) = op2;
	pb->cur = (cur + 8) & (PUSH_BUFFER_SIZE - 1);
	/* printk("push_to_push_buffer: op1=%08x; op2=%08x; cur=%x\n", op1, op2, pb->cur); */
}

/**
 * Pop a number of two word slots from the push buffer
 * Caller must ensure push buffer is not empty
 */
static void pop_from_push_buffer(struct push_buffer *pb, unsigned int slots)
{
	pb->fence = (pb->fence + slots * 8) & (PUSH_BUFFER_SIZE - 1);
}

/**
 * Return the number of two word slots free in the push buffer
 */
static u32 push_buffer_space(struct push_buffer *pb)
{
	return ((pb->fence - pb->cur) & (PUSH_BUFFER_SIZE - 1)) / 8;
}

static u32 push_buffer_putptr(struct push_buffer *pb)
{
	return pb->phys + pb->cur;
}


/* Sync Queue
 *
 * The sync queue is a circular buffer of u32s interpreted as:
 *   0: SyncPointID
 *   1: SyncPointValue
 *   2: NumSlots (how many pushbuffer slots to free)
 *   3: NumHandles
 *   4: nvmap client which pinned the handles
 *   5..: NumHandles * nvmemhandle to unpin
 *
 * There's always one word unused, so (accounting for wrap):
 *   - Write == Read => queue empty
 *   - Write + 1 == Read => queue full
 * The queue must not be left with less than SYNC_QUEUE_MIN_ENTRY words
 * of space at the end of the array.
 *
 * We want to pass contiguous arrays of handles to NrRmMemUnpin, so arrays
 * that would wrap at the end of the buffer will be split into two (or more)
 * entries.
 */

/* Number of words needed to store an entry containing one handle */
#define SYNC_QUEUE_MIN_ENTRY (4 + (2 * sizeof(void *) / sizeof(u32)))

/**
 * Reset to empty queue.
 */
static void reset_sync_queue(struct sync_queue *queue)
{
	queue->read = 0;
	queue->write = 0;
}

/**
 *  Find the number of handles that can be stashed in the sync queue without
 *  waiting.
 *  0 -> queue is full, must update to wait for some entries to be freed.
 */
static unsigned int sync_queue_space(struct sync_queue *queue)
{
	unsigned int read = queue->read;
	unsigned int write = queue->write;
	u32 size;

	BUG_ON(read  > (NVHOST_SYNC_QUEUE_SIZE - SYNC_QUEUE_MIN_ENTRY));
	BUG_ON(write > (NVHOST_SYNC_QUEUE_SIZE - SYNC_QUEUE_MIN_ENTRY));

	/*
	 * We can use all of the space up to the end of the buffer, unless the
	 * read position is within that space (the read position may advance
	 * asynchronously, but that can't take space away once we've seen it).
	 */
	if (read > write) {
		size = (read - 1) - write;
	} else {
		size = NVHOST_SYNC_QUEUE_SIZE - write;

		/*
		 * If the read position is zero, it gets complicated. We can't
		 * use the last word in the buffer, because that would leave
		 * the queue empty.
		 * But also if we use too much we would not leave enough space
		 * for a single handle packet, and would have to wrap in
		 * add_to_sync_queue - also leaving write == read == 0,
		 * an empty queue.
		 */
		if (read == 0)
			size -= SYNC_QUEUE_MIN_ENTRY;
	}

	/*
	 * There must be room for an entry header and at least one handle,
	 * otherwise we report a full queue.
	 */
	if (size < SYNC_QUEUE_MIN_ENTRY)
		return 0;
	/* Minimum entry stores one handle */
	return (size - SYNC_QUEUE_MIN_ENTRY) + 1;
}

/**
 * Add an entry to the sync queue.
 */
#define entry_size(_cnt)	((1 + _cnt)*sizeof(void *)/sizeof(u32))

static void add_to_sync_queue(struct sync_queue *queue,
			      u32 sync_point_id, u32 sync_point_value,
			      u32 nr_slots, struct nvmap_client *user_nvmap,
			      struct nvmap_handle **handles, u32 nr_handles)
{
	u32 write = queue->write;
	u32 *p = queue->buffer + write;
	u32 size = 4 + (entry_size(nr_handles));

	BUG_ON(sync_point_id == NVSYNCPT_INVALID);
	BUG_ON(sync_queue_space(queue) < nr_handles);

	write += size;
	BUG_ON(write > NVHOST_SYNC_QUEUE_SIZE);

	*p++ = sync_point_id;
	*p++ = sync_point_value;
	*p++ = nr_slots;
	*p++ = nr_handles;
	BUG_ON(!user_nvmap);
	*(struct nvmap_client **)p = nvmap_client_get(user_nvmap);

	p = (u32 *)((void *)p + sizeof(struct nvmap_client *));

	if (nr_handles)
		memcpy(p, handles, nr_handles * sizeof(struct nvmap_handle *));

	/* If there's not enough room for another entry, wrap to the start. */
	if ((write + SYNC_QUEUE_MIN_ENTRY) > NVHOST_SYNC_QUEUE_SIZE) {
		/*
		 * It's an error for the read position to be zero, as that
		 * would mean we emptied the queue while adding something.
		 */
		BUG_ON(queue->read == 0);
		write = 0;
	}

	queue->write = write;
}

/**
 * Get a pointer to the next entry in the queue, or NULL if the queue is empty.
 * Doesn't consume the entry.
 */
static u32 *sync_queue_head(struct sync_queue *queue)
{
	u32 read = queue->read;
	u32 write = queue->write;

	BUG_ON(read  > (NVHOST_SYNC_QUEUE_SIZE - SYNC_QUEUE_MIN_ENTRY));
	BUG_ON(write > (NVHOST_SYNC_QUEUE_SIZE - SYNC_QUEUE_MIN_ENTRY));

	if (read == write)
		return NULL;
	return queue->buffer + read;
}

/**
 * Advances to the next queue entry, if you want to consume it.
 */
static void
dequeue_sync_queue_head(struct sync_queue *queue)
{
	u32 read = queue->read;
	u32 size;

	BUG_ON(read == queue->write);

	size = 4 + entry_size(queue->buffer[read + 3]);

	read += size;
	BUG_ON(read > NVHOST_SYNC_QUEUE_SIZE);

	/* If there's not enough room for another entry, wrap to the start. */
	if ((read + SYNC_QUEUE_MIN_ENTRY) > NVHOST_SYNC_QUEUE_SIZE)
		read = 0;

	queue->read = read;
}


/*** Cdma internal stuff ***/

/**
 * Kick channel DMA into action by writing its PUT offset (if it has changed)
 */
static void kick_cdma(struct nvhost_cdma *cdma)
{
	u32 put = push_buffer_putptr(&cdma->push_buffer);
	if (put != cdma->last_put) {
		void __iomem *chan_regs = cdma_to_channel(cdma)->aperture;
		wmb();
		writel(put, chan_regs + HOST1X_CHANNEL_DMAPUT);
		cdma->last_put = put;
	}
}

/**
 * Return the status of the cdma's sync queue or push buffer for the given event
 *  - sq empty: returns 1 for empty, 0 for not empty (as in "1 empty queue" :-)
 *  - sq space: returns the number of handles that can be stored in the queue
 *  - pb space: returns the number of free slots in the channel's push buffer
 * Must be called with the cdma lock held.
 */
static unsigned int cdma_status(struct nvhost_cdma *cdma, enum cdma_event event)
{
	switch (event) {
	case CDMA_EVENT_SYNC_QUEUE_EMPTY:
		return sync_queue_head(&cdma->sync_queue) ? 0 : 1;
	case CDMA_EVENT_SYNC_QUEUE_SPACE:
		return sync_queue_space(&cdma->sync_queue);
	case CDMA_EVENT_PUSH_BUFFER_SPACE:
		return push_buffer_space(&cdma->push_buffer);
	default:
		return 0;
	}
}

/**
 * Sleep (if necessary) until the requested event happens
 *   - CDMA_EVENT_SYNC_QUEUE_EMPTY : sync queue is completely empty.
 *     - Returns 1
 *   - CDMA_EVENT_SYNC_QUEUE_SPACE : there is space in the sync queue.
 *   - CDMA_EVENT_PUSH_BUFFER_SPACE : there is space in the push buffer
 *     - Return the amount of space (> 0)
 * Must be called with the cdma lock held.
 */
static unsigned int wait_cdma(struct nvhost_cdma *cdma, enum cdma_event event)
{
	for (;;) {
		unsigned int space = cdma_status(cdma, event);
		if (space)
			return space;

		BUG_ON(cdma->event != CDMA_EVENT_NONE);
		cdma->event = event;

		mutex_unlock(&cdma->lock);
		down(&cdma->sem);
		mutex_lock(&cdma->lock);
	}
}

/**
 * For all sync queue entries that have already finished according to the
 * current sync point registers:
 *  - unpin & unref their mems
 *  - pop their push buffer slots
 *  - remove them from the sync queue
 * This is normally called from the host code's worker thread, but can be
 * called manually if necessary.
 * Must be called with the cdma lock held.
 */
static void update_cdma(struct nvhost_cdma *cdma)
{
	bool signal = false;
	struct nvhost_master *dev = cdma_to_dev(cdma);

	BUG_ON(!cdma->running);

	/*
	 * Walk the sync queue, reading the sync point registers as necessary,
	 * to consume as many sync queue entries as possible without blocking
	 */
	for (;;) {
		u32 syncpt_id, syncpt_val;
		unsigned int nr_slots, nr_handles;
		struct nvmap_handle **handles;
		struct nvmap_client *nvmap;
		u32 *sync;

		sync = sync_queue_head(&cdma->sync_queue);
		if (!sync) {
			if (cdma->event == CDMA_EVENT_SYNC_QUEUE_EMPTY)
				signal = true;
			break;
		}

		syncpt_id = *sync++;
		syncpt_val = *sync++;

		BUG_ON(syncpt_id == NVSYNCPT_INVALID);

		/* Check whether this syncpt has completed, and bail if not */
		if (!nvhost_syncpt_min_cmp(&dev->syncpt, syncpt_id, syncpt_val))
			break;

		nr_slots = *sync++;
		nr_handles = *sync++;
		nvmap = *(struct nvmap_client **)sync;
		sync = ((void *)sync + sizeof(struct nvmap_client *));
		handles = (struct nvmap_handle **)sync;

		BUG_ON(!nvmap);

		/* Unpin the memory */
		nvmap_unpin_handles(nvmap, handles, nr_handles);

		nvmap_client_put(nvmap);

		/* Pop push buffer slots */
		if (nr_slots) {
			pop_from_push_buffer(&cdma->push_buffer, nr_slots);
			if (cdma->event == CDMA_EVENT_PUSH_BUFFER_SPACE)
				signal = true;
		}

		dequeue_sync_queue_head(&cdma->sync_queue);
		if (cdma->event == CDMA_EVENT_SYNC_QUEUE_SPACE)
			signal = true;
	}

	/* Wake up CdmaWait() if the requested event happened */
	if (signal) {
		cdma->event = CDMA_EVENT_NONE;
		up(&cdma->sem);
	}
}

/**
 * Create a cdma
 */
int nvhost_cdma_init(struct nvhost_cdma *cdma)
{
	int err;

	mutex_init(&cdma->lock);
	sema_init(&cdma->sem, 0);
	cdma->event = CDMA_EVENT_NONE;
	cdma->running = false;
	err = init_push_buffer(&cdma->push_buffer);
	if (err)
		return err;
	reset_sync_queue(&cdma->sync_queue);
	return 0;
}

/**
 * Destroy a cdma
 */
void nvhost_cdma_deinit(struct nvhost_cdma *cdma)
{
	BUG_ON(cdma->running);
	destroy_push_buffer(&cdma->push_buffer);
}

static void start_cdma(struct nvhost_cdma *cdma)
{
	void __iomem *chan_regs = cdma_to_channel(cdma)->aperture;

	if (cdma->running)
		return;

	cdma->last_put = push_buffer_putptr(&cdma->push_buffer);

	writel(nvhost_channel_dmactrl(true, false, false),
		chan_regs + HOST1X_CHANNEL_DMACTRL);

	/* set base, put, end pointer (all of memory) */
	writel(0, chan_regs + HOST1X_CHANNEL_DMASTART);
	writel(cdma->last_put, chan_regs + HOST1X_CHANNEL_DMAPUT);
	writel(0xFFFFFFFF, chan_regs + HOST1X_CHANNEL_DMAEND);

	/* reset GET */
	writel(nvhost_channel_dmactrl(true, true, true),
		chan_regs + HOST1X_CHANNEL_DMACTRL);

	/* start the command DMA */
	writel(nvhost_channel_dmactrl(false, false, false),
		chan_regs + HOST1X_CHANNEL_DMACTRL);

	cdma->running = true;

}

void nvhost_cdma_stop(struct nvhost_cdma *cdma)
{
	void __iomem *chan_regs = cdma_to_channel(cdma)->aperture;

	mutex_lock(&cdma->lock);
	if (cdma->running) {
		wait_cdma(cdma, CDMA_EVENT_SYNC_QUEUE_EMPTY);
		writel(nvhost_channel_dmactrl(true, false, false),
			chan_regs + HOST1X_CHANNEL_DMACTRL);
		cdma->running = false;
	}
	mutex_unlock(&cdma->lock);
}

/**
 * Begin a cdma submit
 */
void nvhost_cdma_begin(struct nvhost_cdma *cdma)
{
	mutex_lock(&cdma->lock);
	if (!cdma->running)
		start_cdma(cdma);
	cdma->slots_free = 0;
	cdma->slots_used = 0;
}

/**
 * Push two words into a push buffer slot
 * Blocks as necessary if the push buffer is full.
 */
void nvhost_cdma_push(struct nvhost_cdma *cdma, u32 op1, u32 op2)
{
	u32 slots_free = cdma->slots_free;
	if (slots_free == 0) {
		kick_cdma(cdma);
		slots_free = wait_cdma(cdma, CDMA_EVENT_PUSH_BUFFER_SPACE);
	}
	cdma->slots_free = slots_free - 1;
	cdma->slots_used++;
	push_to_push_buffer(&cdma->push_buffer, op1, op2);
}

/**
 * End a cdma submit
 * Kick off DMA, add a contiguous block of memory handles to the sync queue,
 * and a number of slots to be freed from the pushbuffer.
 * Blocks as necessary if the sync queue is full.
 * The handles for a submit must all be pinned at the same time, but they
 * can be unpinned in smaller chunks.
 */
void nvhost_cdma_end(struct nvmap_client *user_nvmap, struct nvhost_cdma *cdma,
		     u32 sync_point_id, u32 sync_point_value,
		     struct nvmap_handle **handles, unsigned int nr_handles)
{
	kick_cdma(cdma);

	while (nr_handles || cdma->slots_used) {
		unsigned int count;
		/*
		 * Wait until there's enough room in the
		 * sync queue to write something.
		 */
		count = wait_cdma(cdma, CDMA_EVENT_SYNC_QUEUE_SPACE);

		/*
		 * Add reloc entries to sync queue (as many as will fit)
		 * and unlock it
		 */
		if (count > nr_handles)
			count = nr_handles;
		add_to_sync_queue(&cdma->sync_queue, sync_point_id,
				  sync_point_value, cdma->slots_used,
				  user_nvmap, handles, count);
		/* NumSlots only goes in the first packet */
		cdma->slots_used = 0;
		handles += count;
		nr_handles -= count;
	}

	mutex_unlock(&cdma->lock);
}

/**
 * Update cdma state according to current sync point values
 */
void nvhost_cdma_update(struct nvhost_cdma *cdma)
{
	mutex_lock(&cdma->lock);
	update_cdma(cdma);
	mutex_unlock(&cdma->lock);
}

/**
 * Manually spin until all CDMA has finished. Used if an async update
 * cannot be scheduled for any reason.
 */
void nvhost_cdma_flush(struct nvhost_cdma *cdma)
{
	mutex_lock(&cdma->lock);
	while (sync_queue_head(&cdma->sync_queue)) {
		update_cdma(cdma);
		mutex_unlock(&cdma->lock);
		schedule();
		mutex_lock(&cdma->lock);
	}
	mutex_unlock(&cdma->lock);
}

/**
 * Find the currently executing gather in the push buffer and return
 * its physical address and size.
 */
void nvhost_cdma_find_gather(struct nvhost_cdma *cdma, u32 dmaget, u32 *addr, u32 *size)
{
	u32 offset = dmaget - cdma->push_buffer.phys;

	*addr = *size = 0;

	if (offset >= 8 && offset < cdma->push_buffer.cur) {
		u32 *p = cdma->push_buffer.mapped + (offset - 8) / 4;

		/* Make sure we have a gather */
		if ((p[0] >> 28) == 6) {
			*addr = p[1];
			*size = p[0] & 0x3fff;
		}
	}
}
