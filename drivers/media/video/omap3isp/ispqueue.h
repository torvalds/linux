/*
 * ispqueue.h
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

#ifndef OMAP3_ISP_QUEUE_H
#define OMAP3_ISP_QUEUE_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

struct isp_video_queue;
struct page;
struct scatterlist;

#define ISP_VIDEO_MAX_BUFFERS		16

/**
 * enum isp_video_buffer_state - ISP video buffer state
 * @ISP_BUF_STATE_IDLE:	The buffer is under userspace control (dequeued
 *	or not queued yet).
 * @ISP_BUF_STATE_QUEUED: The buffer has been queued but isn't used by the
 *	device yet.
 * @ISP_BUF_STATE_ACTIVE: The buffer is in use for an active video transfer.
 * @ISP_BUF_STATE_ERROR: The device is done with the buffer and an error
 *	occurred. For capture device the buffer likely contains corrupted data or
 *	no data at all.
 * @ISP_BUF_STATE_DONE: The device is done with the buffer and no error occurred.
 *	For capture devices the buffer contains valid data.
 */
enum isp_video_buffer_state {
	ISP_BUF_STATE_IDLE,
	ISP_BUF_STATE_QUEUED,
	ISP_BUF_STATE_ACTIVE,
	ISP_BUF_STATE_ERROR,
	ISP_BUF_STATE_DONE,
};

/**
 * struct isp_video_buffer - ISP video buffer
 * @vma_use_count: Number of times the buffer is mmap'ed to userspace
 * @stream: List head for insertion into main queue
 * @queue: ISP buffers queue this buffer belongs to
 * @prepared: Whether the buffer has been prepared
 * @skip_cache: Whether to skip cache management operations for this buffer
 * @vaddr: Memory virtual address (for kernel buffers)
 * @vm_flags: Buffer VMA flags (for userspace buffers)
 * @offset: Offset inside the first page (for userspace buffers)
 * @npages: Number of pages (for userspace buffers)
 * @pages: Pages table (for userspace non-VM_PFNMAP buffers)
 * @paddr: Memory physical address (for userspace VM_PFNMAP buffers)
 * @sglen: Number of elements in the scatter list (for non-VM_PFNMAP buffers)
 * @sglist: Scatter list (for non-VM_PFNMAP buffers)
 * @vbuf: V4L2 buffer
 * @irqlist: List head for insertion into IRQ queue
 * @state: Current buffer state
 * @wait: Wait queue to signal buffer completion
 */
struct isp_video_buffer {
	unsigned long vma_use_count;
	struct list_head stream;
	struct isp_video_queue *queue;
	unsigned int prepared:1;
	bool skip_cache;

	/* For kernel buffers. */
	void *vaddr;

	/* For userspace buffers. */
	vm_flags_t vm_flags;
	unsigned long offset;
	unsigned int npages;
	struct page **pages;
	dma_addr_t paddr;

	/* For all buffers except VM_PFNMAP. */
	unsigned int sglen;
	struct scatterlist *sglist;

	/* Touched by the interrupt handler. */
	struct v4l2_buffer vbuf;
	struct list_head irqlist;
	enum isp_video_buffer_state state;
	wait_queue_head_t wait;
};

#define to_isp_video_buffer(vb)	container_of(vb, struct isp_video_buffer, vb)

/**
 * struct isp_video_queue_operations - Driver-specific operations
 * @queue_prepare: Called before allocating buffers. Drivers should clamp the
 *	number of buffers according to their requirements, and must return the
 *	buffer size in bytes.
 * @buffer_prepare: Called the first time a buffer is queued, or after changing
 *	the userspace memory address for a USERPTR buffer, with the queue lock
 *	held. Drivers should perform device-specific buffer preparation (such as
 *	mapping the buffer memory in an IOMMU). This operation is optional.
 * @buffer_queue: Called when a buffer is being added to the queue with the
 *	queue irqlock spinlock held.
 * @buffer_cleanup: Called before freeing buffers, or before changing the
 *	userspace memory address for a USERPTR buffer, with the queue lock held.
 *	Drivers must perform cleanup operations required to undo the
 *	buffer_prepare call. This operation is optional.
 */
struct isp_video_queue_operations {
	void (*queue_prepare)(struct isp_video_queue *queue,
			      unsigned int *nbuffers, unsigned int *size);
	int  (*buffer_prepare)(struct isp_video_buffer *buf);
	void (*buffer_queue)(struct isp_video_buffer *buf);
	void (*buffer_cleanup)(struct isp_video_buffer *buf);
};

/**
 * struct isp_video_queue - ISP video buffers queue
 * @type: Type of video buffers handled by this queue
 * @ops: Queue operations
 * @dev: Device used for DMA operations
 * @bufsize: Size of a driver-specific buffer object
 * @count: Number of currently allocated buffers
 * @buffers: ISP video buffers
 * @lock: Mutex to protect access to the buffers, main queue and state
 * @irqlock: Spinlock to protect access to the IRQ queue
 * @streaming: Queue state, indicates whether the queue is streaming
 * @queue: List of all queued buffers
 */
struct isp_video_queue {
	enum v4l2_buf_type type;
	const struct isp_video_queue_operations *ops;
	struct device *dev;
	unsigned int bufsize;

	unsigned int count;
	struct isp_video_buffer *buffers[ISP_VIDEO_MAX_BUFFERS];
	struct mutex lock;
	spinlock_t irqlock;

	unsigned int streaming:1;

	struct list_head queue;
};

int omap3isp_video_queue_cleanup(struct isp_video_queue *queue);
int omap3isp_video_queue_init(struct isp_video_queue *queue,
			      enum v4l2_buf_type type,
			      const struct isp_video_queue_operations *ops,
			      struct device *dev, unsigned int bufsize);

int omap3isp_video_queue_reqbufs(struct isp_video_queue *queue,
				 struct v4l2_requestbuffers *rb);
int omap3isp_video_queue_querybuf(struct isp_video_queue *queue,
				  struct v4l2_buffer *vbuf);
int omap3isp_video_queue_qbuf(struct isp_video_queue *queue,
			      struct v4l2_buffer *vbuf);
int omap3isp_video_queue_dqbuf(struct isp_video_queue *queue,
			       struct v4l2_buffer *vbuf, int nonblocking);
int omap3isp_video_queue_streamon(struct isp_video_queue *queue);
void omap3isp_video_queue_streamoff(struct isp_video_queue *queue);
void omap3isp_video_queue_discard_done(struct isp_video_queue *queue);
int omap3isp_video_queue_mmap(struct isp_video_queue *queue,
			      struct vm_area_struct *vma);
unsigned int omap3isp_video_queue_poll(struct isp_video_queue *queue,
				       struct file *file, poll_table *wait);

#endif /* OMAP3_ISP_QUEUE_H */
