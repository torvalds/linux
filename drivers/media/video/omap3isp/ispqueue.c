/*
 * ispqueue.c
 *
 * TI OMAP3 ISP - Video buffers queue handling
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "ispqueue.h"

/* -----------------------------------------------------------------------------
 * Video buffers management
 */

/*
 * isp_video_buffer_cache_sync - Keep the buffers coherent between CPU and ISP
 *
 * The typical operation required here is Cache Invalidation across
 * the (user space) buffer address range. And this _must_ be done
 * at QBUF stage (and *only* at QBUF).
 *
 * We try to use optimal cache invalidation function:
 * - dmac_map_area:
 *    - used when the number of pages are _low_.
 *    - it becomes quite slow as the number of pages increase.
 *       - for 648x492 viewfinder (150 pages) it takes 1.3 ms.
 *       - for 5 Mpix buffer (2491 pages) it takes between 25-50 ms.
 *
 * - flush_cache_all:
 *    - used when the number of pages are _high_.
 *    - time taken in the range of 500-900 us.
 *    - has a higher penalty but, as whole dcache + icache is invalidated
 */
/*
 * FIXME: dmac_inv_range crashes randomly on the user space buffer
 *        address. Fall back to flush_cache_all for now.
 */
#define ISP_CACHE_FLUSH_PAGES_MAX       0

static void isp_video_buffer_cache_sync(struct isp_video_buffer *buf)
{
	if (buf->skip_cache)
		return;

	if (buf->vbuf.m.userptr == 0 || buf->npages == 0 ||
	    buf->npages > ISP_CACHE_FLUSH_PAGES_MAX)
		flush_cache_all();
	else {
		dmac_map_area((void *)buf->vbuf.m.userptr, buf->vbuf.length,
			      DMA_FROM_DEVICE);
		outer_inv_range(buf->vbuf.m.userptr,
				buf->vbuf.m.userptr + buf->vbuf.length);
	}
}

/*
 * isp_video_buffer_lock_vma - Prevent VMAs from being unmapped
 *
 * Lock the VMAs underlying the given buffer into memory. This avoids the
 * userspace buffer mapping from being swapped out, making VIPT cache handling
 * easier.
 *
 * Note that the pages will not be freed as the buffers have been locked to
 * memory using by a call to get_user_pages(), but the userspace mapping could
 * still disappear if the VMAs are not locked. This is caused by the memory
 * management code trying to be as lock-less as possible, which results in the
 * userspace mapping manager not finding out that the pages are locked under
 * some conditions.
 */
static int isp_video_buffer_lock_vma(struct isp_video_buffer *buf, int lock)
{
	struct vm_area_struct *vma;
	unsigned long start;
	unsigned long end;
	int ret = 0;

	if (buf->vbuf.memory == V4L2_MEMORY_MMAP)
		return 0;

	/* We can be called from workqueue context if the current task dies to
	 * unlock the VMAs. In that case there's no current memory management
	 * context so unlocking can't be performed, but the VMAs have been or
	 * are getting destroyed anyway so it doesn't really matter.
	 */
	if (!current || !current->mm)
		return lock ? -EINVAL : 0;

	start = buf->vbuf.m.userptr;
	end = buf->vbuf.m.userptr + buf->vbuf.length - 1;

	down_write(&current->mm->mmap_sem);
	spin_lock(&current->mm->page_table_lock);

	do {
		vma = find_vma(current->mm, start);
		if (vma == NULL) {
			ret = -EFAULT;
			goto out;
		}

		if (lock)
			vma->vm_flags |= VM_LOCKED;
		else
			vma->vm_flags &= ~VM_LOCKED;

		start = vma->vm_end + 1;
	} while (vma->vm_end < end);

	if (lock)
		buf->vm_flags |= VM_LOCKED;
	else
		buf->vm_flags &= ~VM_LOCKED;

out:
	spin_unlock(&current->mm->page_table_lock);
	up_write(&current->mm->mmap_sem);
	return ret;
}

/*
 * isp_video_buffer_sglist_kernel - Build a scatter list for a vmalloc'ed buffer
 *
 * Iterate over the vmalloc'ed area and create a scatter list entry for every
 * page.
 */
static int isp_video_buffer_sglist_kernel(struct isp_video_buffer *buf)
{
	struct scatterlist *sglist;
	unsigned int npages;
	unsigned int i;
	void *addr;

	addr = buf->vaddr;
	npages = PAGE_ALIGN(buf->vbuf.length) >> PAGE_SHIFT;

	sglist = vmalloc(npages * sizeof(*sglist));
	if (sglist == NULL)
		return -ENOMEM;

	sg_init_table(sglist, npages);

	for (i = 0; i < npages; ++i, addr += PAGE_SIZE) {
		struct page *page = vmalloc_to_page(addr);

		if (page == NULL || PageHighMem(page)) {
			vfree(sglist);
			return -EINVAL;
		}

		sg_set_page(&sglist[i], page, PAGE_SIZE, 0);
	}

	buf->sglen = npages;
	buf->sglist = sglist;

	return 0;
}

/*
 * isp_video_buffer_sglist_user - Build a scatter list for a userspace buffer
 *
 * Walk the buffer pages list and create a 1:1 mapping to a scatter list.
 */
static int isp_video_buffer_sglist_user(struct isp_video_buffer *buf)
{
	struct scatterlist *sglist;
	unsigned int offset = buf->offset;
	unsigned int i;

	sglist = vmalloc(buf->npages * sizeof(*sglist));
	if (sglist == NULL)
		return -ENOMEM;

	sg_init_table(sglist, buf->npages);

	for (i = 0; i < buf->npages; ++i) {
		if (PageHighMem(buf->pages[i])) {
			vfree(sglist);
			return -EINVAL;
		}

		sg_set_page(&sglist[i], buf->pages[i], PAGE_SIZE - offset,
			    offset);
		offset = 0;
	}

	buf->sglen = buf->npages;
	buf->sglist = sglist;

	return 0;
}

/*
 * isp_video_buffer_sglist_pfnmap - Build a scatter list for a VM_PFNMAP buffer
 *
 * Create a scatter list of physically contiguous pages starting at the buffer
 * memory physical address.
 */
static int isp_video_buffer_sglist_pfnmap(struct isp_video_buffer *buf)
{
	struct scatterlist *sglist;
	unsigned int offset = buf->offset;
	unsigned long pfn = buf->paddr >> PAGE_SHIFT;
	unsigned int i;

	sglist = vmalloc(buf->npages * sizeof(*sglist));
	if (sglist == NULL)
		return -ENOMEM;

	sg_init_table(sglist, buf->npages);

	for (i = 0; i < buf->npages; ++i, ++pfn) {
		sg_set_page(&sglist[i], pfn_to_page(pfn), PAGE_SIZE - offset,
			    offset);
		/* PFNMAP buffers will not get DMA-mapped, set the DMA address
		 * manually.
		 */
		sg_dma_address(&sglist[i]) = (pfn << PAGE_SHIFT) + offset;
		offset = 0;
	}

	buf->sglen = buf->npages;
	buf->sglist = sglist;

	return 0;
}

/*
 * isp_video_buffer_cleanup - Release pages for a userspace VMA.
 *
 * Release pages locked by a call isp_video_buffer_prepare_user and free the
 * pages table.
 */
static void isp_video_buffer_cleanup(struct isp_video_buffer *buf)
{
	enum dma_data_direction direction;
	unsigned int i;

	if (buf->queue->ops->buffer_cleanup)
		buf->queue->ops->buffer_cleanup(buf);

	if (!(buf->vm_flags & VM_PFNMAP)) {
		direction = buf->vbuf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE
			  ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		dma_unmap_sg(buf->queue->dev, buf->sglist, buf->sglen,
			     direction);
	}

	vfree(buf->sglist);
	buf->sglist = NULL;
	buf->sglen = 0;

	if (buf->pages != NULL) {
		isp_video_buffer_lock_vma(buf, 0);

		for (i = 0; i < buf->npages; ++i)
			page_cache_release(buf->pages[i]);

		vfree(buf->pages);
		buf->pages = NULL;
	}

	buf->npages = 0;
	buf->skip_cache = false;
}

/*
 * isp_video_buffer_prepare_user - Pin userspace VMA pages to memory.
 *
 * This function creates a list of pages for a userspace VMA. The number of
 * pages is first computed based on the buffer size, and pages are then
 * retrieved by a call to get_user_pages.
 *
 * Pages are pinned to memory by get_user_pages, making them available for DMA
 * transfers. However, due to memory management optimization, it seems the
 * get_user_pages doesn't guarantee that the pinned pages will not be written
 * to swap and removed from the userspace mapping(s). When this happens, a page
 * fault can be generated when accessing those unmapped pages.
 *
 * If the fault is triggered by a page table walk caused by VIPT cache
 * management operations, the page fault handler might oops if the MM semaphore
 * is held, as it can't handle kernel page faults in that case. To fix that, a
 * fixup entry needs to be added to the cache management code, or the userspace
 * VMA must be locked to avoid removing pages from the userspace mapping in the
 * first place.
 *
 * If the number of pages retrieved is smaller than the number required by the
 * buffer size, the function returns -EFAULT.
 */
static int isp_video_buffer_prepare_user(struct isp_video_buffer *buf)
{
	unsigned long data;
	unsigned int first;
	unsigned int last;
	int ret;

	data = buf->vbuf.m.userptr;
	first = (data & PAGE_MASK) >> PAGE_SHIFT;
	last = ((data + buf->vbuf.length - 1) & PAGE_MASK) >> PAGE_SHIFT;

	buf->offset = data & ~PAGE_MASK;
	buf->npages = last - first + 1;
	buf->pages = vmalloc(buf->npages * sizeof(buf->pages[0]));
	if (buf->pages == NULL)
		return -ENOMEM;

	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(current, current->mm, data & PAGE_MASK,
			     buf->npages,
			     buf->vbuf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE, 0,
			     buf->pages, NULL);
	up_read(&current->mm->mmap_sem);

	if (ret != buf->npages) {
		buf->npages = ret < 0 ? 0 : ret;
		isp_video_buffer_cleanup(buf);
		return -EFAULT;
	}

	ret = isp_video_buffer_lock_vma(buf, 1);
	if (ret < 0)
		isp_video_buffer_cleanup(buf);

	return ret;
}

/*
 * isp_video_buffer_prepare_pfnmap - Validate a VM_PFNMAP userspace buffer
 *
 * Userspace VM_PFNMAP buffers are supported only if they are contiguous in
 * memory and if they span a single VMA.
 *
 * Return 0 if the buffer is valid, or -EFAULT otherwise.
 */
static int isp_video_buffer_prepare_pfnmap(struct isp_video_buffer *buf)
{
	struct vm_area_struct *vma;
	unsigned long prev_pfn;
	unsigned long this_pfn;
	unsigned long start;
	unsigned long end;
	dma_addr_t pa;
	int ret = -EFAULT;

	start = buf->vbuf.m.userptr;
	end = buf->vbuf.m.userptr + buf->vbuf.length - 1;

	buf->offset = start & ~PAGE_MASK;
	buf->npages = (end >> PAGE_SHIFT) - (start >> PAGE_SHIFT) + 1;
	buf->pages = NULL;

	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, start);
	if (vma == NULL || vma->vm_end < end)
		goto done;

	for (prev_pfn = 0; start <= end; start += PAGE_SIZE) {
		ret = follow_pfn(vma, start, &this_pfn);
		if (ret)
			goto done;

		if (prev_pfn == 0)
			pa = this_pfn << PAGE_SHIFT;
		else if (this_pfn != prev_pfn + 1) {
			ret = -EFAULT;
			goto done;
		}

		prev_pfn = this_pfn;
	}

	buf->paddr = pa + buf->offset;
	ret = 0;

done:
	up_read(&current->mm->mmap_sem);
	return ret;
}

/*
 * isp_video_buffer_prepare_vm_flags - Get VMA flags for a userspace address
 *
 * This function locates the VMAs for the buffer's userspace address and checks
 * that their flags match. The only flag that we need to care for at the moment
 * is VM_PFNMAP.
 *
 * The buffer vm_flags field is set to the first VMA flags.
 *
 * Return -EFAULT if no VMA can be found for part of the buffer, or if the VMAs
 * have incompatible flags.
 */
static int isp_video_buffer_prepare_vm_flags(struct isp_video_buffer *buf)
{
	struct vm_area_struct *vma;
	pgprot_t vm_page_prot;
	unsigned long start;
	unsigned long end;
	int ret = -EFAULT;

	start = buf->vbuf.m.userptr;
	end = buf->vbuf.m.userptr + buf->vbuf.length - 1;

	down_read(&current->mm->mmap_sem);

	do {
		vma = find_vma(current->mm, start);
		if (vma == NULL)
			goto done;

		if (start == buf->vbuf.m.userptr) {
			buf->vm_flags = vma->vm_flags;
			vm_page_prot = vma->vm_page_prot;
		}

		if ((buf->vm_flags ^ vma->vm_flags) & VM_PFNMAP)
			goto done;

		if (vm_page_prot != vma->vm_page_prot)
			goto done;

		start = vma->vm_end + 1;
	} while (vma->vm_end < end);

	/* Skip cache management to enhance performances for non-cached or
	 * write-combining buffers.
	 */
	if (vm_page_prot == pgprot_noncached(vm_page_prot) ||
	    vm_page_prot == pgprot_writecombine(vm_page_prot))
		buf->skip_cache = true;

	ret = 0;

done:
	up_read(&current->mm->mmap_sem);
	return ret;
}

/*
 * isp_video_buffer_prepare - Make a buffer ready for operation
 *
 * Preparing a buffer involves:
 *
 * - validating VMAs (userspace buffers only)
 * - locking pages and VMAs into memory (userspace buffers only)
 * - building page and scatter-gather lists
 * - mapping buffers for DMA operation
 * - performing driver-specific preparation
 *
 * The function must be called in userspace context with a valid mm context
 * (this excludes cleanup paths such as sys_close when the userspace process
 * segfaults).
 */
static int isp_video_buffer_prepare(struct isp_video_buffer *buf)
{
	enum dma_data_direction direction;
	int ret;

	switch (buf->vbuf.memory) {
	case V4L2_MEMORY_MMAP:
		ret = isp_video_buffer_sglist_kernel(buf);
		break;

	case V4L2_MEMORY_USERPTR:
		ret = isp_video_buffer_prepare_vm_flags(buf);
		if (ret < 0)
			return ret;

		if (buf->vm_flags & VM_PFNMAP) {
			ret = isp_video_buffer_prepare_pfnmap(buf);
			if (ret < 0)
				return ret;

			ret = isp_video_buffer_sglist_pfnmap(buf);
		} else {
			ret = isp_video_buffer_prepare_user(buf);
			if (ret < 0)
				return ret;

			ret = isp_video_buffer_sglist_user(buf);
		}
		break;

	default:
		return -EINVAL;
	}

	if (ret < 0)
		goto done;

	if (!(buf->vm_flags & VM_PFNMAP)) {
		direction = buf->vbuf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE
			  ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		ret = dma_map_sg(buf->queue->dev, buf->sglist, buf->sglen,
				 direction);
		if (ret != buf->sglen) {
			ret = -EFAULT;
			goto done;
		}
	}

	if (buf->queue->ops->buffer_prepare)
		ret = buf->queue->ops->buffer_prepare(buf);

done:
	if (ret < 0) {
		isp_video_buffer_cleanup(buf);
		return ret;
	}

	return ret;
}

/*
 * isp_video_queue_query - Query the status of a given buffer
 *
 * Locking: must be called with the queue lock held.
 */
static void isp_video_buffer_query(struct isp_video_buffer *buf,
				   struct v4l2_buffer *vbuf)
{
	memcpy(vbuf, &buf->vbuf, sizeof(*vbuf));

	if (buf->vma_use_count)
		vbuf->flags |= V4L2_BUF_FLAG_MAPPED;

	switch (buf->state) {
	case ISP_BUF_STATE_ERROR:
		vbuf->flags |= V4L2_BUF_FLAG_ERROR;
	case ISP_BUF_STATE_DONE:
		vbuf->flags |= V4L2_BUF_FLAG_DONE;
	case ISP_BUF_STATE_QUEUED:
	case ISP_BUF_STATE_ACTIVE:
		vbuf->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case ISP_BUF_STATE_IDLE:
	default:
		break;
	}
}

/*
 * isp_video_buffer_wait - Wait for a buffer to be ready
 *
 * In non-blocking mode, return immediately with 0 if the buffer is ready or
 * -EAGAIN if the buffer is in the QUEUED or ACTIVE state.
 *
 * In blocking mode, wait (interruptibly but with no timeout) on the buffer wait
 * queue using the same condition.
 */
static int isp_video_buffer_wait(struct isp_video_buffer *buf, int nonblocking)
{
	if (nonblocking) {
		return (buf->state != ISP_BUF_STATE_QUEUED &&
			buf->state != ISP_BUF_STATE_ACTIVE)
			? 0 : -EAGAIN;
	}

	return wait_event_interruptible(buf->wait,
		buf->state != ISP_BUF_STATE_QUEUED &&
		buf->state != ISP_BUF_STATE_ACTIVE);
}

/* -----------------------------------------------------------------------------
 * Queue management
 */

/*
 * isp_video_queue_free - Free video buffers memory
 *
 * Buffers can only be freed if the queue isn't streaming and if no buffer is
 * mapped to userspace. Return -EBUSY if those conditions aren't statisfied.
 *
 * This function must be called with the queue lock held.
 */
static int isp_video_queue_free(struct isp_video_queue *queue)
{
	unsigned int i;

	if (queue->streaming)
		return -EBUSY;

	for (i = 0; i < queue->count; ++i) {
		if (queue->buffers[i]->vma_use_count != 0)
			return -EBUSY;
	}

	for (i = 0; i < queue->count; ++i) {
		struct isp_video_buffer *buf = queue->buffers[i];

		isp_video_buffer_cleanup(buf);

		vfree(buf->vaddr);
		buf->vaddr = NULL;

		kfree(buf);
		queue->buffers[i] = NULL;
	}

	INIT_LIST_HEAD(&queue->queue);
	queue->count = 0;
	return 0;
}

/*
 * isp_video_queue_alloc - Allocate video buffers memory
 *
 * This function must be called with the queue lock held.
 */
static int isp_video_queue_alloc(struct isp_video_queue *queue,
				 unsigned int nbuffers,
				 unsigned int size, enum v4l2_memory memory)
{
	struct isp_video_buffer *buf;
	unsigned int i;
	void *mem;
	int ret;

	/* Start by freeing the buffers. */
	ret = isp_video_queue_free(queue);
	if (ret < 0)
		return ret;

	/* Bail out of no buffers should be allocated. */
	if (nbuffers == 0)
		return 0;

	/* Initialize the allocated buffers. */
	for (i = 0; i < nbuffers; ++i) {
		buf = kzalloc(queue->bufsize, GFP_KERNEL);
		if (buf == NULL)
			break;

		if (memory == V4L2_MEMORY_MMAP) {
			/* Allocate video buffers memory for mmap mode. Align
			 * the size to the page size.
			 */
			mem = vmalloc_32_user(PAGE_ALIGN(size));
			if (mem == NULL) {
				kfree(buf);
				break;
			}

			buf->vbuf.m.offset = i * PAGE_ALIGN(size);
			buf->vaddr = mem;
		}

		buf->vbuf.index = i;
		buf->vbuf.length = size;
		buf->vbuf.type = queue->type;
		buf->vbuf.field = V4L2_FIELD_NONE;
		buf->vbuf.memory = memory;

		buf->queue = queue;
		init_waitqueue_head(&buf->wait);

		queue->buffers[i] = buf;
	}

	if (i == 0)
		return -ENOMEM;

	queue->count = i;
	return nbuffers;
}

/**
 * omap3isp_video_queue_cleanup - Clean up the video buffers queue
 * @queue: Video buffers queue
 *
 * Free all allocated resources and clean up the video buffers queue. The queue
 * must not be busy (no ongoing video stream) and buffers must have been
 * unmapped.
 *
 * Return 0 on success or -EBUSY if the queue is busy or buffers haven't been
 * unmapped.
 */
int omap3isp_video_queue_cleanup(struct isp_video_queue *queue)
{
	return isp_video_queue_free(queue);
}

/**
 * omap3isp_video_queue_init - Initialize the video buffers queue
 * @queue: Video buffers queue
 * @type: V4L2 buffer type (capture or output)
 * @ops: Driver-specific queue operations
 * @dev: Device used for DMA operations
 * @bufsize: Size of the driver-specific buffer structure
 *
 * Initialize the video buffers queue with the supplied parameters.
 *
 * The queue type must be one of V4L2_BUF_TYPE_VIDEO_CAPTURE or
 * V4L2_BUF_TYPE_VIDEO_OUTPUT. Other buffer types are not supported yet.
 *
 * Buffer objects will be allocated using the given buffer size to allow room
 * for driver-specific fields. Driver-specific buffer structures must start
 * with a struct isp_video_buffer field. Drivers with no driver-specific buffer
 * structure must pass the size of the isp_video_buffer structure in the bufsize
 * parameter.
 *
 * Return 0 on success.
 */
int omap3isp_video_queue_init(struct isp_video_queue *queue,
			      enum v4l2_buf_type type,
			      const struct isp_video_queue_operations *ops,
			      struct device *dev, unsigned int bufsize)
{
	INIT_LIST_HEAD(&queue->queue);
	mutex_init(&queue->lock);
	spin_lock_init(&queue->irqlock);

	queue->type = type;
	queue->ops = ops;
	queue->dev = dev;
	queue->bufsize = bufsize;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 operations
 */

/**
 * omap3isp_video_queue_reqbufs - Allocate video buffers memory
 *
 * This function is intended to be used as a VIDIOC_REQBUFS ioctl handler. It
 * allocated video buffer objects and, for MMAP buffers, buffer memory.
 *
 * If the number of buffers is 0, all buffers are freed and the function returns
 * without performing any allocation.
 *
 * If the number of buffers is not 0, currently allocated buffers (if any) are
 * freed and the requested number of buffers are allocated. Depending on
 * driver-specific requirements and on memory availability, a number of buffer
 * smaller or bigger than requested can be allocated. This isn't considered as
 * an error.
 *
 * Return 0 on success or one of the following error codes:
 *
 * -EINVAL if the buffer type or index are invalid
 * -EBUSY if the queue is busy (streaming or buffers mapped)
 * -ENOMEM if the buffers can't be allocated due to an out-of-memory condition
 */
int omap3isp_video_queue_reqbufs(struct isp_video_queue *queue,
				 struct v4l2_requestbuffers *rb)
{
	unsigned int nbuffers = rb->count;
	unsigned int size;
	int ret;

	if (rb->type != queue->type)
		return -EINVAL;

	queue->ops->queue_prepare(queue, &nbuffers, &size);
	if (size == 0)
		return -EINVAL;

	nbuffers = min_t(unsigned int, nbuffers, ISP_VIDEO_MAX_BUFFERS);

	mutex_lock(&queue->lock);

	ret = isp_video_queue_alloc(queue, nbuffers, size, rb->memory);
	if (ret < 0)
		goto done;

	rb->count = ret;
	ret = 0;

done:
	mutex_unlock(&queue->lock);
	return ret;
}

/**
 * omap3isp_video_queue_querybuf - Query the status of a buffer in a queue
 *
 * This function is intended to be used as a VIDIOC_QUERYBUF ioctl handler. It
 * returns the status of a given video buffer.
 *
 * Return 0 on success or -EINVAL if the buffer type or index are invalid.
 */
int omap3isp_video_queue_querybuf(struct isp_video_queue *queue,
				  struct v4l2_buffer *vbuf)
{
	struct isp_video_buffer *buf;
	int ret = 0;

	if (vbuf->type != queue->type)
		return -EINVAL;

	mutex_lock(&queue->lock);

	if (vbuf->index >= queue->count) {
		ret = -EINVAL;
		goto done;
	}

	buf = queue->buffers[vbuf->index];
	isp_video_buffer_query(buf, vbuf);

done:
	mutex_unlock(&queue->lock);
	return ret;
}

/**
 * omap3isp_video_queue_qbuf - Queue a buffer
 *
 * This function is intended to be used as a VIDIOC_QBUF ioctl handler.
 *
 * The v4l2_buffer structure passed from userspace is first sanity tested. If
 * sane, the buffer is then processed and added to the main queue and, if the
 * queue is streaming, to the IRQ queue.
 *
 * Before being enqueued, USERPTR buffers are checked for address changes. If
 * the buffer has a different userspace address, the old memory area is unlocked
 * and the new memory area is locked.
 */
int omap3isp_video_queue_qbuf(struct isp_video_queue *queue,
			      struct v4l2_buffer *vbuf)
{
	struct isp_video_buffer *buf;
	unsigned long flags;
	int ret = -EINVAL;

	if (vbuf->type != queue->type)
		goto done;

	mutex_lock(&queue->lock);

	if (vbuf->index >= queue->count)
		goto done;

	buf = queue->buffers[vbuf->index];

	if (vbuf->memory != buf->vbuf.memory)
		goto done;

	if (buf->state != ISP_BUF_STATE_IDLE)
		goto done;

	if (vbuf->memory == V4L2_MEMORY_USERPTR &&
	    vbuf->m.userptr != buf->vbuf.m.userptr) {
		isp_video_buffer_cleanup(buf);
		buf->vbuf.m.userptr = vbuf->m.userptr;
		buf->prepared = 0;
	}

	if (!buf->prepared) {
		ret = isp_video_buffer_prepare(buf);
		if (ret < 0)
			goto done;
		buf->prepared = 1;
	}

	isp_video_buffer_cache_sync(buf);

	buf->state = ISP_BUF_STATE_QUEUED;
	list_add_tail(&buf->stream, &queue->queue);

	if (queue->streaming) {
		spin_lock_irqsave(&queue->irqlock, flags);
		queue->ops->buffer_queue(buf);
		spin_unlock_irqrestore(&queue->irqlock, flags);
	}

	ret = 0;

done:
	mutex_unlock(&queue->lock);
	return ret;
}

/**
 * omap3isp_video_queue_dqbuf - Dequeue a buffer
 *
 * This function is intended to be used as a VIDIOC_DQBUF ioctl handler.
 *
 * The v4l2_buffer structure passed from userspace is first sanity tested. If
 * sane, the buffer is then processed and added to the main queue and, if the
 * queue is streaming, to the IRQ queue.
 *
 * Before being enqueued, USERPTR buffers are checked for address changes. If
 * the buffer has a different userspace address, the old memory area is unlocked
 * and the new memory area is locked.
 */
int omap3isp_video_queue_dqbuf(struct isp_video_queue *queue,
			       struct v4l2_buffer *vbuf, int nonblocking)
{
	struct isp_video_buffer *buf;
	int ret;

	if (vbuf->type != queue->type)
		return -EINVAL;

	mutex_lock(&queue->lock);

	if (list_empty(&queue->queue)) {
		ret = -EINVAL;
		goto done;
	}

	buf = list_first_entry(&queue->queue, struct isp_video_buffer, stream);
	ret = isp_video_buffer_wait(buf, nonblocking);
	if (ret < 0)
		goto done;

	list_del(&buf->stream);

	isp_video_buffer_query(buf, vbuf);
	buf->state = ISP_BUF_STATE_IDLE;
	vbuf->flags &= ~V4L2_BUF_FLAG_QUEUED;

done:
	mutex_unlock(&queue->lock);
	return ret;
}

/**
 * omap3isp_video_queue_streamon - Start streaming
 *
 * This function is intended to be used as a VIDIOC_STREAMON ioctl handler. It
 * starts streaming on the queue and calls the buffer_queue operation for all
 * queued buffers.
 *
 * Return 0 on success.
 */
int omap3isp_video_queue_streamon(struct isp_video_queue *queue)
{
	struct isp_video_buffer *buf;
	unsigned long flags;

	mutex_lock(&queue->lock);

	if (queue->streaming)
		goto done;

	queue->streaming = 1;

	spin_lock_irqsave(&queue->irqlock, flags);
	list_for_each_entry(buf, &queue->queue, stream)
		queue->ops->buffer_queue(buf);
	spin_unlock_irqrestore(&queue->irqlock, flags);

done:
	mutex_unlock(&queue->lock);
	return 0;
}

/**
 * omap3isp_video_queue_streamoff - Stop streaming
 *
 * This function is intended to be used as a VIDIOC_STREAMOFF ioctl handler. It
 * stops streaming on the queue and wakes up all the buffers.
 *
 * Drivers must stop the hardware and synchronize with interrupt handlers and/or
 * delayed works before calling this function to make sure no buffer will be
 * touched by the driver and/or hardware.
 */
void omap3isp_video_queue_streamoff(struct isp_video_queue *queue)
{
	struct isp_video_buffer *buf;
	unsigned long flags;
	unsigned int i;

	mutex_lock(&queue->lock);

	if (!queue->streaming)
		goto done;

	queue->streaming = 0;

	spin_lock_irqsave(&queue->irqlock, flags);
	for (i = 0; i < queue->count; ++i) {
		buf = queue->buffers[i];

		if (buf->state == ISP_BUF_STATE_ACTIVE)
			wake_up(&buf->wait);

		buf->state = ISP_BUF_STATE_IDLE;
	}
	spin_unlock_irqrestore(&queue->irqlock, flags);

	INIT_LIST_HEAD(&queue->queue);

done:
	mutex_unlock(&queue->lock);
}

/**
 * omap3isp_video_queue_discard_done - Discard all buffers marked as DONE
 *
 * This function is intended to be used with suspend/resume operations. It
 * discards all 'done' buffers as they would be too old to be requested after
 * resume.
 *
 * Drivers must stop the hardware and synchronize with interrupt handlers and/or
 * delayed works before calling this function to make sure no buffer will be
 * touched by the driver and/or hardware.
 */
void omap3isp_video_queue_discard_done(struct isp_video_queue *queue)
{
	struct isp_video_buffer *buf;
	unsigned int i;

	mutex_lock(&queue->lock);

	if (!queue->streaming)
		goto done;

	for (i = 0; i < queue->count; ++i) {
		buf = queue->buffers[i];

		if (buf->state == ISP_BUF_STATE_DONE)
			buf->state = ISP_BUF_STATE_ERROR;
	}

done:
	mutex_unlock(&queue->lock);
}

static void isp_video_queue_vm_open(struct vm_area_struct *vma)
{
	struct isp_video_buffer *buf = vma->vm_private_data;

	buf->vma_use_count++;
}

static void isp_video_queue_vm_close(struct vm_area_struct *vma)
{
	struct isp_video_buffer *buf = vma->vm_private_data;

	buf->vma_use_count--;
}

static const struct vm_operations_struct isp_video_queue_vm_ops = {
	.open = isp_video_queue_vm_open,
	.close = isp_video_queue_vm_close,
};

/**
 * omap3isp_video_queue_mmap - Map buffers to userspace
 *
 * This function is intended to be used as an mmap() file operation handler. It
 * maps a buffer to userspace based on the VMA offset.
 *
 * Only buffers of memory type MMAP are supported.
 */
int omap3isp_video_queue_mmap(struct isp_video_queue *queue,
			 struct vm_area_struct *vma)
{
	struct isp_video_buffer *uninitialized_var(buf);
	unsigned long size;
	unsigned int i;
	int ret = 0;

	mutex_lock(&queue->lock);

	for (i = 0; i < queue->count; ++i) {
		buf = queue->buffers[i];
		if ((buf->vbuf.m.offset >> PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}

	if (i == queue->count) {
		ret = -EINVAL;
		goto done;
	}

	size = vma->vm_end - vma->vm_start;

	if (buf->vbuf.memory != V4L2_MEMORY_MMAP ||
	    size != PAGE_ALIGN(buf->vbuf.length)) {
		ret = -EINVAL;
		goto done;
	}

	ret = remap_vmalloc_range(vma, buf->vaddr, 0);
	if (ret < 0)
		goto done;

	vma->vm_ops = &isp_video_queue_vm_ops;
	vma->vm_private_data = buf;
	isp_video_queue_vm_open(vma);

done:
	mutex_unlock(&queue->lock);
	return ret;
}

/**
 * omap3isp_video_queue_poll - Poll video queue state
 *
 * This function is intended to be used as a poll() file operation handler. It
 * polls the state of the video buffer at the front of the queue and returns an
 * events mask.
 *
 * If no buffer is present at the front of the queue, POLLERR is returned.
 */
unsigned int omap3isp_video_queue_poll(struct isp_video_queue *queue,
				       struct file *file, poll_table *wait)
{
	struct isp_video_buffer *buf;
	unsigned int mask = 0;

	mutex_lock(&queue->lock);
	if (list_empty(&queue->queue)) {
		mask |= POLLERR;
		goto done;
	}
	buf = list_first_entry(&queue->queue, struct isp_video_buffer, stream);

	poll_wait(file, &buf->wait, wait);
	if (buf->state == ISP_BUF_STATE_DONE ||
	    buf->state == ISP_BUF_STATE_ERROR) {
		if (queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			mask |= POLLIN | POLLRDNORM;
		else
			mask |= POLLOUT | POLLWRNORM;
	}

done:
	mutex_unlock(&queue->lock);
	return mask;
}
