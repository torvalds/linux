/*
 * videobuf2-core.c - video buffer 2 core framework
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *	   Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * The vb2_thread implementation was based on code from videobuf-dvb.c:
 *	(c) 2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/kthread.h>

#include <media/videobuf2-core.h>

#include <trace/events/vb2.h>

#include "videobuf2-internal.h"

int vb2_debug;
EXPORT_SYMBOL_GPL(vb2_debug);
module_param_named(debug, vb2_debug, int, 0644);

static void __vb2_queue_cancel(struct vb2_queue *q);
static void __enqueue_in_driver(struct vb2_buffer *vb);

/**
 * __vb2_buf_mem_alloc() - allocate video memory for the given buffer
 */
static int __vb2_buf_mem_alloc(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	enum dma_data_direction dma_dir =
		q->is_output ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	void *mem_priv;
	int plane;

	/*
	 * Allocate memory for all planes in this buffer
	 * NOTE: mmapped areas should be page aligned
	 */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		unsigned long size = PAGE_ALIGN(q->plane_sizes[plane]);

		mem_priv = call_ptr_memop(vb, alloc, q->alloc_ctx[plane],
				      size, dma_dir, q->gfp_flags);
		if (IS_ERR_OR_NULL(mem_priv))
			goto free;

		/* Associate allocator private data with this plane */
		vb->planes[plane].mem_priv = mem_priv;
		vb->planes[plane].length = q->plane_sizes[plane];
	}

	return 0;
free:
	/* Free already allocated memory if one of the allocations failed */
	for (; plane > 0; --plane) {
		call_void_memop(vb, put, vb->planes[plane - 1].mem_priv);
		vb->planes[plane - 1].mem_priv = NULL;
	}

	return -ENOMEM;
}

/**
 * __vb2_buf_mem_free() - free memory of the given buffer
 */
static void __vb2_buf_mem_free(struct vb2_buffer *vb)
{
	unsigned int plane;

	for (plane = 0; plane < vb->num_planes; ++plane) {
		call_void_memop(vb, put, vb->planes[plane].mem_priv);
		vb->planes[plane].mem_priv = NULL;
		dprintk(3, "freed plane %d of buffer %d\n", plane, vb->index);
	}
}

/**
 * __vb2_buf_userptr_put() - release userspace memory associated with
 * a USERPTR buffer
 */
static void __vb2_buf_userptr_put(struct vb2_buffer *vb)
{
	unsigned int plane;

	for (plane = 0; plane < vb->num_planes; ++plane) {
		if (vb->planes[plane].mem_priv)
			call_void_memop(vb, put_userptr, vb->planes[plane].mem_priv);
		vb->planes[plane].mem_priv = NULL;
	}
}

/**
 * __vb2_plane_dmabuf_put() - release memory associated with
 * a DMABUF shared plane
 */
static void __vb2_plane_dmabuf_put(struct vb2_buffer *vb, struct vb2_plane *p)
{
	if (!p->mem_priv)
		return;

	if (p->dbuf_mapped)
		call_void_memop(vb, unmap_dmabuf, p->mem_priv);

	call_void_memop(vb, detach_dmabuf, p->mem_priv);
	dma_buf_put(p->dbuf);
	p->mem_priv = NULL;
	p->dbuf = NULL;
	p->dbuf_mapped = 0;
}

/**
 * __vb2_buf_dmabuf_put() - release memory associated with
 * a DMABUF shared buffer
 */
static void __vb2_buf_dmabuf_put(struct vb2_buffer *vb)
{
	unsigned int plane;

	for (plane = 0; plane < vb->num_planes; ++plane)
		__vb2_plane_dmabuf_put(vb, &vb->planes[plane]);
}

/**
 * __setup_lengths() - setup initial lengths for every plane in
 * every buffer on the queue
 */
static void __setup_lengths(struct vb2_queue *q, unsigned int n)
{
	unsigned int buffer, plane;
	struct vb2_buffer *vb;

	for (buffer = q->num_buffers; buffer < q->num_buffers + n; ++buffer) {
		vb = q->bufs[buffer];
		if (!vb)
			continue;

		for (plane = 0; plane < vb->num_planes; ++plane)
			vb->planes[plane].length = q->plane_sizes[plane];
	}
}

/**
 * __setup_offsets() - setup unique offsets ("cookies") for every plane in
 * every buffer on the queue
 */
static void __setup_offsets(struct vb2_queue *q, unsigned int n)
{
	unsigned int buffer, plane;
	struct vb2_buffer *vb;
	unsigned long off;

	if (q->num_buffers) {
		struct vb2_plane *p;
		vb = q->bufs[q->num_buffers - 1];
		p = &vb->planes[vb->num_planes - 1];
		off = PAGE_ALIGN(p->m.offset + p->length);
	} else {
		off = 0;
	}

	for (buffer = q->num_buffers; buffer < q->num_buffers + n; ++buffer) {
		vb = q->bufs[buffer];
		if (!vb)
			continue;

		for (plane = 0; plane < vb->num_planes; ++plane) {
			vb->planes[plane].m.offset = off;

			dprintk(3, "buffer %d, plane %d offset 0x%08lx\n",
					buffer, plane, off);

			off += vb->planes[plane].length;
			off = PAGE_ALIGN(off);
		}
	}
}

/**
 * __vb2_queue_alloc() - allocate videobuf buffer structures and (for MMAP type)
 * video buffer memory for all buffers/planes on the queue and initializes the
 * queue
 *
 * Returns the number of buffers successfully allocated.
 */
static int __vb2_queue_alloc(struct vb2_queue *q, enum vb2_memory memory,
			     unsigned int num_buffers, unsigned int num_planes)
{
	unsigned int buffer;
	struct vb2_buffer *vb;
	int ret;

	/* Ensure that q->num_buffers+num_buffers is below VB2_MAX_FRAME */
	num_buffers = min_t(unsigned int, num_buffers,
			    VB2_MAX_FRAME - q->num_buffers);

	for (buffer = 0; buffer < num_buffers; ++buffer) {
		/* Allocate videobuf buffer structures */
		vb = kzalloc(q->buf_struct_size, GFP_KERNEL);
		if (!vb) {
			dprintk(1, "memory alloc for buffer struct failed\n");
			break;
		}

		vb->state = VB2_BUF_STATE_DEQUEUED;
		vb->vb2_queue = q;
		vb->num_planes = num_planes;
		vb->index = q->num_buffers + buffer;
		vb->type = q->type;
		vb->memory = memory;

		/* Allocate video buffer memory for the MMAP type */
		if (memory == VB2_MEMORY_MMAP) {
			ret = __vb2_buf_mem_alloc(vb);
			if (ret) {
				dprintk(1, "failed allocating memory for "
						"buffer %d\n", buffer);
				kfree(vb);
				break;
			}
			/*
			 * Call the driver-provided buffer initialization
			 * callback, if given. An error in initialization
			 * results in queue setup failure.
			 */
			ret = call_vb_qop(vb, buf_init, vb);
			if (ret) {
				dprintk(1, "buffer %d %p initialization"
					" failed\n", buffer, vb);
				__vb2_buf_mem_free(vb);
				kfree(vb);
				break;
			}
		}

		q->bufs[q->num_buffers + buffer] = vb;
	}

	__setup_lengths(q, buffer);
	if (memory == VB2_MEMORY_MMAP)
		__setup_offsets(q, buffer);

	dprintk(1, "allocated %d buffers, %d plane(s) each\n",
			buffer, num_planes);

	return buffer;
}

/**
 * __vb2_free_mem() - release all video buffer memory for a given queue
 */
static void __vb2_free_mem(struct vb2_queue *q, unsigned int buffers)
{
	unsigned int buffer;
	struct vb2_buffer *vb;

	for (buffer = q->num_buffers - buffers; buffer < q->num_buffers;
	     ++buffer) {
		vb = q->bufs[buffer];
		if (!vb)
			continue;

		/* Free MMAP buffers or release USERPTR buffers */
		if (q->memory == VB2_MEMORY_MMAP)
			__vb2_buf_mem_free(vb);
		else if (q->memory == VB2_MEMORY_DMABUF)
			__vb2_buf_dmabuf_put(vb);
		else
			__vb2_buf_userptr_put(vb);
	}
}

/**
 * __vb2_queue_free() - free buffers at the end of the queue - video memory and
 * related information, if no buffers are left return the queue to an
 * uninitialized state. Might be called even if the queue has already been freed.
 */
static int __vb2_queue_free(struct vb2_queue *q, unsigned int buffers)
{
	unsigned int buffer;

	/*
	 * Sanity check: when preparing a buffer the queue lock is released for
	 * a short while (see __buf_prepare for the details), which would allow
	 * a race with a reqbufs which can call this function. Removing the
	 * buffers from underneath __buf_prepare is obviously a bad idea, so we
	 * check if any of the buffers is in the state PREPARING, and if so we
	 * just return -EAGAIN.
	 */
	for (buffer = q->num_buffers - buffers; buffer < q->num_buffers;
	     ++buffer) {
		if (q->bufs[buffer] == NULL)
			continue;
		if (q->bufs[buffer]->state == VB2_BUF_STATE_PREPARING) {
			dprintk(1, "preparing buffers, cannot free\n");
			return -EAGAIN;
		}
	}

	/* Call driver-provided cleanup function for each buffer, if provided */
	for (buffer = q->num_buffers - buffers; buffer < q->num_buffers;
	     ++buffer) {
		struct vb2_buffer *vb = q->bufs[buffer];

		if (vb && vb->planes[0].mem_priv)
			call_void_vb_qop(vb, buf_cleanup, vb);
	}

	/* Release video buffer memory */
	__vb2_free_mem(q, buffers);

#ifdef CONFIG_VIDEO_ADV_DEBUG
	/*
	 * Check that all the calls were balances during the life-time of this
	 * queue. If not (or if the debug level is 1 or up), then dump the
	 * counters to the kernel log.
	 */
	if (q->num_buffers) {
		bool unbalanced = q->cnt_start_streaming != q->cnt_stop_streaming ||
				  q->cnt_wait_prepare != q->cnt_wait_finish;

		if (unbalanced || vb2_debug) {
			pr_info("vb2: counters for queue %p:%s\n", q,
				unbalanced ? " UNBALANCED!" : "");
			pr_info("vb2:     setup: %u start_streaming: %u stop_streaming: %u\n",
				q->cnt_queue_setup, q->cnt_start_streaming,
				q->cnt_stop_streaming);
			pr_info("vb2:     wait_prepare: %u wait_finish: %u\n",
				q->cnt_wait_prepare, q->cnt_wait_finish);
		}
		q->cnt_queue_setup = 0;
		q->cnt_wait_prepare = 0;
		q->cnt_wait_finish = 0;
		q->cnt_start_streaming = 0;
		q->cnt_stop_streaming = 0;
	}
	for (buffer = 0; buffer < q->num_buffers; ++buffer) {
		struct vb2_buffer *vb = q->bufs[buffer];
		bool unbalanced = vb->cnt_mem_alloc != vb->cnt_mem_put ||
				  vb->cnt_mem_prepare != vb->cnt_mem_finish ||
				  vb->cnt_mem_get_userptr != vb->cnt_mem_put_userptr ||
				  vb->cnt_mem_attach_dmabuf != vb->cnt_mem_detach_dmabuf ||
				  vb->cnt_mem_map_dmabuf != vb->cnt_mem_unmap_dmabuf ||
				  vb->cnt_buf_queue != vb->cnt_buf_done ||
				  vb->cnt_buf_prepare != vb->cnt_buf_finish ||
				  vb->cnt_buf_init != vb->cnt_buf_cleanup;

		if (unbalanced || vb2_debug) {
			pr_info("vb2:   counters for queue %p, buffer %d:%s\n",
				q, buffer, unbalanced ? " UNBALANCED!" : "");
			pr_info("vb2:     buf_init: %u buf_cleanup: %u buf_prepare: %u buf_finish: %u\n",
				vb->cnt_buf_init, vb->cnt_buf_cleanup,
				vb->cnt_buf_prepare, vb->cnt_buf_finish);
			pr_info("vb2:     buf_queue: %u buf_done: %u\n",
				vb->cnt_buf_queue, vb->cnt_buf_done);
			pr_info("vb2:     alloc: %u put: %u prepare: %u finish: %u mmap: %u\n",
				vb->cnt_mem_alloc, vb->cnt_mem_put,
				vb->cnt_mem_prepare, vb->cnt_mem_finish,
				vb->cnt_mem_mmap);
			pr_info("vb2:     get_userptr: %u put_userptr: %u\n",
				vb->cnt_mem_get_userptr, vb->cnt_mem_put_userptr);
			pr_info("vb2:     attach_dmabuf: %u detach_dmabuf: %u map_dmabuf: %u unmap_dmabuf: %u\n",
				vb->cnt_mem_attach_dmabuf, vb->cnt_mem_detach_dmabuf,
				vb->cnt_mem_map_dmabuf, vb->cnt_mem_unmap_dmabuf);
			pr_info("vb2:     get_dmabuf: %u num_users: %u vaddr: %u cookie: %u\n",
				vb->cnt_mem_get_dmabuf,
				vb->cnt_mem_num_users,
				vb->cnt_mem_vaddr,
				vb->cnt_mem_cookie);
		}
	}
#endif

	/* Free videobuf buffers */
	for (buffer = q->num_buffers - buffers; buffer < q->num_buffers;
	     ++buffer) {
		kfree(q->bufs[buffer]);
		q->bufs[buffer] = NULL;
	}

	q->num_buffers -= buffers;
	if (!q->num_buffers) {
		q->memory = 0;
		INIT_LIST_HEAD(&q->queued_list);
	}
	return 0;
}

/**
 * vb2_buffer_in_use() - return true if the buffer is in use and
 * the queue cannot be freed (by the means of REQBUFS(0)) call
 */
bool vb2_buffer_in_use(struct vb2_queue *q, struct vb2_buffer *vb)
{
	unsigned int plane;
	for (plane = 0; plane < vb->num_planes; ++plane) {
		void *mem_priv = vb->planes[plane].mem_priv;
		/*
		 * If num_users() has not been provided, call_memop
		 * will return 0, apparently nobody cares about this
		 * case anyway. If num_users() returns more than 1,
		 * we are not the only user of the plane's memory.
		 */
		if (mem_priv && call_memop(vb, num_users, mem_priv) > 1)
			return true;
	}
	return false;
}
EXPORT_SYMBOL(vb2_buffer_in_use);

/**
 * __buffers_in_use() - return true if any buffers on the queue are in use and
 * the queue cannot be freed (by the means of REQBUFS(0)) call
 */
static bool __buffers_in_use(struct vb2_queue *q)
{
	unsigned int buffer;
	for (buffer = 0; buffer < q->num_buffers; ++buffer) {
		if (vb2_buffer_in_use(q, q->bufs[buffer]))
			return true;
	}
	return false;
}

/**
 * vb2_core_querybuf() - query video buffer information
 * @q:		videobuf queue
 * @index:	id number of the buffer
 * @pb:		buffer struct passed from userspace
 *
 * Should be called from vidioc_querybuf ioctl handler in driver.
 * The passed buffer should have been verified.
 * This function fills the relevant information for the userspace.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_querybuf handler in driver.
 */
int vb2_core_querybuf(struct vb2_queue *q, unsigned int index, void *pb)
{
	return call_bufop(q, fill_user_buffer, q->bufs[index], pb);
}
EXPORT_SYMBOL_GPL(vb2_core_querybuf);

/**
 * __verify_userptr_ops() - verify that all memory operations required for
 * USERPTR queue type have been provided
 */
static int __verify_userptr_ops(struct vb2_queue *q)
{
	if (!(q->io_modes & VB2_USERPTR) || !q->mem_ops->get_userptr ||
	    !q->mem_ops->put_userptr)
		return -EINVAL;

	return 0;
}

/**
 * __verify_mmap_ops() - verify that all memory operations required for
 * MMAP queue type have been provided
 */
static int __verify_mmap_ops(struct vb2_queue *q)
{
	if (!(q->io_modes & VB2_MMAP) || !q->mem_ops->alloc ||
	    !q->mem_ops->put || !q->mem_ops->mmap)
		return -EINVAL;

	return 0;
}

/**
 * __verify_dmabuf_ops() - verify that all memory operations required for
 * DMABUF queue type have been provided
 */
static int __verify_dmabuf_ops(struct vb2_queue *q)
{
	if (!(q->io_modes & VB2_DMABUF) || !q->mem_ops->attach_dmabuf ||
	    !q->mem_ops->detach_dmabuf  || !q->mem_ops->map_dmabuf ||
	    !q->mem_ops->unmap_dmabuf)
		return -EINVAL;

	return 0;
}

/**
 * vb2_verify_memory_type() - Check whether the memory type and buffer type
 * passed to a buffer operation are compatible with the queue.
 */
int vb2_verify_memory_type(struct vb2_queue *q,
		enum vb2_memory memory, unsigned int type)
{
	if (memory != VB2_MEMORY_MMAP && memory != VB2_MEMORY_USERPTR &&
	    memory != VB2_MEMORY_DMABUF) {
		dprintk(1, "unsupported memory type\n");
		return -EINVAL;
	}

	if (type != q->type) {
		dprintk(1, "requested type is incorrect\n");
		return -EINVAL;
	}

	/*
	 * Make sure all the required memory ops for given memory type
	 * are available.
	 */
	if (memory == VB2_MEMORY_MMAP && __verify_mmap_ops(q)) {
		dprintk(1, "MMAP for current setup unsupported\n");
		return -EINVAL;
	}

	if (memory == VB2_MEMORY_USERPTR && __verify_userptr_ops(q)) {
		dprintk(1, "USERPTR for current setup unsupported\n");
		return -EINVAL;
	}

	if (memory == VB2_MEMORY_DMABUF && __verify_dmabuf_ops(q)) {
		dprintk(1, "DMABUF for current setup unsupported\n");
		return -EINVAL;
	}

	/*
	 * Place the busy tests at the end: -EBUSY can be ignored when
	 * create_bufs is called with count == 0, but count == 0 should still
	 * do the memory and type validation.
	 */
	if (vb2_fileio_is_active(q)) {
		dprintk(1, "file io in progress\n");
		return -EBUSY;
	}
	return 0;
}
EXPORT_SYMBOL(vb2_verify_memory_type);

/**
 * vb2_core_reqbufs() - Initiate streaming
 * @q:		videobuf2 queue
 * @memory: memory type
 * @count: requested buffer count
 *
 * Should be called from vidioc_reqbufs ioctl handler of a driver.
 * This function:
 * 1) verifies streaming parameters passed from the userspace,
 * 2) sets up the queue,
 * 3) negotiates number of buffers and planes per buffer with the driver
 *    to be used during streaming,
 * 4) allocates internal buffer structures (struct vb2_buffer), according to
 *    the agreed parameters,
 * 5) for MMAP memory type, allocates actual video memory, using the
 *    memory handling/allocation routines provided during queue initialization
 *
 * If req->count is 0, all the memory will be freed instead.
 * If the queue has been allocated previously (by a previous vb2_reqbufs) call
 * and the queue is not busy, memory will be reallocated.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_reqbufs handler in driver.
 */
int vb2_core_reqbufs(struct vb2_queue *q, enum vb2_memory memory,
		unsigned int *count)
{
	unsigned int num_buffers, allocated_buffers, num_planes = 0;
	int ret;

	if (q->streaming) {
		dprintk(1, "streaming active\n");
		return -EBUSY;
	}

	if (*count == 0 || q->num_buffers != 0 || q->memory != memory) {
		/*
		 * We already have buffers allocated, so first check if they
		 * are not in use and can be freed.
		 */
		mutex_lock(&q->mmap_lock);
		if (q->memory == VB2_MEMORY_MMAP && __buffers_in_use(q)) {
			mutex_unlock(&q->mmap_lock);
			dprintk(1, "memory in use, cannot free\n");
			return -EBUSY;
		}

		/*
		 * Call queue_cancel to clean up any buffers in the PREPARED or
		 * QUEUED state which is possible if buffers were prepared or
		 * queued without ever calling STREAMON.
		 */
		__vb2_queue_cancel(q);
		ret = __vb2_queue_free(q, q->num_buffers);
		mutex_unlock(&q->mmap_lock);
		if (ret)
			return ret;

		/*
		 * In case of REQBUFS(0) return immediately without calling
		 * driver's queue_setup() callback and allocating resources.
		 */
		if (*count == 0)
			return 0;
	}

	/*
	 * Make sure the requested values and current defaults are sane.
	 */
	num_buffers = min_t(unsigned int, *count, VB2_MAX_FRAME);
	num_buffers = max_t(unsigned int, num_buffers, q->min_buffers_needed);
	memset(q->plane_sizes, 0, sizeof(q->plane_sizes));
	memset(q->alloc_ctx, 0, sizeof(q->alloc_ctx));
	q->memory = memory;

	/*
	 * Ask the driver how many buffers and planes per buffer it requires.
	 * Driver also sets the size and allocator context for each plane.
	 */
	ret = call_qop(q, queue_setup, q, NULL, &num_buffers, &num_planes,
		       q->plane_sizes, q->alloc_ctx);
	if (ret)
		return ret;

	/* Finally, allocate buffers and video memory */
	allocated_buffers =
		__vb2_queue_alloc(q, memory, num_buffers, num_planes);
	if (allocated_buffers == 0) {
		dprintk(1, "memory allocation failed\n");
		return -ENOMEM;
	}

	/*
	 * There is no point in continuing if we can't allocate the minimum
	 * number of buffers needed by this vb2_queue.
	 */
	if (allocated_buffers < q->min_buffers_needed)
		ret = -ENOMEM;

	/*
	 * Check if driver can handle the allocated number of buffers.
	 */
	if (!ret && allocated_buffers < num_buffers) {
		num_buffers = allocated_buffers;

		ret = call_qop(q, queue_setup, q, NULL, &num_buffers,
			       &num_planes, q->plane_sizes, q->alloc_ctx);

		if (!ret && allocated_buffers < num_buffers)
			ret = -ENOMEM;

		/*
		 * Either the driver has accepted a smaller number of buffers,
		 * or .queue_setup() returned an error
		 */
	}

	mutex_lock(&q->mmap_lock);
	q->num_buffers = allocated_buffers;

	if (ret < 0) {
		/*
		 * Note: __vb2_queue_free() will subtract 'allocated_buffers'
		 * from q->num_buffers.
		 */
		__vb2_queue_free(q, allocated_buffers);
		mutex_unlock(&q->mmap_lock);
		return ret;
	}
	mutex_unlock(&q->mmap_lock);

	/*
	 * Return the number of successfully allocated buffers
	 * to the userspace.
	 */
	*count = allocated_buffers;
	q->waiting_for_buffers = !q->is_output;

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_reqbufs);

/**
 * vb2_core_create_bufs() - Allocate buffers and any required auxiliary structs
 * @q:		videobuf2 queue
 * @memory: memory type
 * @count: requested buffer count
 * @parg: parameter passed to device driver
 *
 * Should be called from vidioc_create_bufs ioctl handler of a driver.
 * This function:
 * 1) verifies parameter sanity
 * 2) calls the .queue_setup() queue operation
 * 3) performs any necessary memory allocations
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_create_bufs handler in driver.
 */
int vb2_core_create_bufs(struct vb2_queue *q, enum vb2_memory memory,
		unsigned int *count, const void *parg)
{
	unsigned int num_planes = 0, num_buffers, allocated_buffers;
	int ret;

	if (q->num_buffers == VB2_MAX_FRAME) {
		dprintk(1, "maximum number of buffers already allocated\n");
		return -ENOBUFS;
	}

	if (!q->num_buffers) {
		memset(q->plane_sizes, 0, sizeof(q->plane_sizes));
		memset(q->alloc_ctx, 0, sizeof(q->alloc_ctx));
		q->memory = memory;
		q->waiting_for_buffers = !q->is_output;
	}

	num_buffers = min(*count, VB2_MAX_FRAME - q->num_buffers);

	/*
	 * Ask the driver, whether the requested number of buffers, planes per
	 * buffer and their sizes are acceptable
	 */
	ret = call_qop(q, queue_setup, q, parg, &num_buffers,
		       &num_planes, q->plane_sizes, q->alloc_ctx);
	if (ret)
		return ret;

	/* Finally, allocate buffers and video memory */
	allocated_buffers = __vb2_queue_alloc(q, memory, num_buffers,
				num_planes);
	if (allocated_buffers == 0) {
		dprintk(1, "memory allocation failed\n");
		return -ENOMEM;
	}

	/*
	 * Check if driver can handle the so far allocated number of buffers.
	 */
	if (allocated_buffers < num_buffers) {
		num_buffers = allocated_buffers;

		/*
		 * q->num_buffers contains the total number of buffers, that the
		 * queue driver has set up
		 */
		ret = call_qop(q, queue_setup, q, parg, &num_buffers,
			       &num_planes, q->plane_sizes, q->alloc_ctx);

		if (!ret && allocated_buffers < num_buffers)
			ret = -ENOMEM;

		/*
		 * Either the driver has accepted a smaller number of buffers,
		 * or .queue_setup() returned an error
		 */
	}

	mutex_lock(&q->mmap_lock);
	q->num_buffers += allocated_buffers;

	if (ret < 0) {
		/*
		 * Note: __vb2_queue_free() will subtract 'allocated_buffers'
		 * from q->num_buffers.
		 */
		__vb2_queue_free(q, allocated_buffers);
		mutex_unlock(&q->mmap_lock);
		return -ENOMEM;
	}
	mutex_unlock(&q->mmap_lock);

	/*
	 * Return the number of successfully allocated buffers
	 * to the userspace.
	 */
	*count = allocated_buffers;

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_create_bufs);

/**
 * vb2_plane_vaddr() - Return a kernel virtual address of a given plane
 * @vb:		vb2_buffer to which the plane in question belongs to
 * @plane_no:	plane number for which the address is to be returned
 *
 * This function returns a kernel virtual address of a given plane if
 * such a mapping exist, NULL otherwise.
 */
void *vb2_plane_vaddr(struct vb2_buffer *vb, unsigned int plane_no)
{
	if (plane_no >= vb->num_planes || !vb->planes[plane_no].mem_priv)
		return NULL;

	return call_ptr_memop(vb, vaddr, vb->planes[plane_no].mem_priv);

}
EXPORT_SYMBOL_GPL(vb2_plane_vaddr);

/**
 * vb2_plane_cookie() - Return allocator specific cookie for the given plane
 * @vb:		vb2_buffer to which the plane in question belongs to
 * @plane_no:	plane number for which the cookie is to be returned
 *
 * This function returns an allocator specific cookie for a given plane if
 * available, NULL otherwise. The allocator should provide some simple static
 * inline function, which would convert this cookie to the allocator specific
 * type that can be used directly by the driver to access the buffer. This can
 * be for example physical address, pointer to scatter list or IOMMU mapping.
 */
void *vb2_plane_cookie(struct vb2_buffer *vb, unsigned int plane_no)
{
	if (plane_no >= vb->num_planes || !vb->planes[plane_no].mem_priv)
		return NULL;

	return call_ptr_memop(vb, cookie, vb->planes[plane_no].mem_priv);
}
EXPORT_SYMBOL_GPL(vb2_plane_cookie);

/**
 * vb2_buffer_done() - inform videobuf that an operation on a buffer is finished
 * @vb:		vb2_buffer returned from the driver
 * @state:	either VB2_BUF_STATE_DONE if the operation finished successfully,
 *		VB2_BUF_STATE_ERROR if the operation finished with an error or
 *		VB2_BUF_STATE_QUEUED if the driver wants to requeue buffers.
 *		If start_streaming fails then it should return buffers with state
 *		VB2_BUF_STATE_QUEUED to put them back into the queue.
 *
 * This function should be called by the driver after a hardware operation on
 * a buffer is finished and the buffer may be returned to userspace. The driver
 * cannot use this buffer anymore until it is queued back to it by videobuf
 * by the means of buf_queue callback. Only buffers previously queued to the
 * driver by buf_queue can be passed to this function.
 *
 * While streaming a buffer can only be returned in state DONE or ERROR.
 * The start_streaming op can also return them in case the DMA engine cannot
 * be started for some reason. In that case the buffers should be returned with
 * state QUEUED.
 */
void vb2_buffer_done(struct vb2_buffer *vb, enum vb2_buffer_state state)
{
	struct vb2_queue *q = vb->vb2_queue;
	unsigned long flags;
	unsigned int plane;

	if (WARN_ON(vb->state != VB2_BUF_STATE_ACTIVE))
		return;

	if (WARN_ON(state != VB2_BUF_STATE_DONE &&
		    state != VB2_BUF_STATE_ERROR &&
		    state != VB2_BUF_STATE_QUEUED &&
		    state != VB2_BUF_STATE_REQUEUEING))
		state = VB2_BUF_STATE_ERROR;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	/*
	 * Although this is not a callback, it still does have to balance
	 * with the buf_queue op. So update this counter manually.
	 */
	vb->cnt_buf_done++;
#endif
	dprintk(4, "done processing on buffer %d, state: %d\n",
			vb->index, state);

	/* sync buffers */
	for (plane = 0; plane < vb->num_planes; ++plane)
		call_void_memop(vb, finish, vb->planes[plane].mem_priv);

	spin_lock_irqsave(&q->done_lock, flags);
	if (state == VB2_BUF_STATE_QUEUED ||
	    state == VB2_BUF_STATE_REQUEUEING) {
		vb->state = VB2_BUF_STATE_QUEUED;
	} else {
		/* Add the buffer to the done buffers list */
		list_add_tail(&vb->done_entry, &q->done_list);
		vb->state = state;
	}
	atomic_dec(&q->owned_by_drv_count);
	spin_unlock_irqrestore(&q->done_lock, flags);

	trace_vb2_buf_done(q, vb);

	switch (state) {
	case VB2_BUF_STATE_QUEUED:
		return;
	case VB2_BUF_STATE_REQUEUEING:
		if (q->start_streaming_called)
			__enqueue_in_driver(vb);
		return;
	default:
		/* Inform any processes that may be waiting for buffers */
		wake_up(&q->done_wq);
		break;
	}
}
EXPORT_SYMBOL_GPL(vb2_buffer_done);

/**
 * vb2_discard_done() - discard all buffers marked as DONE
 * @q:		videobuf2 queue
 *
 * This function is intended to be used with suspend/resume operations. It
 * discards all 'done' buffers as they would be too old to be requested after
 * resume.
 *
 * Drivers must stop the hardware and synchronize with interrupt handlers and/or
 * delayed works before calling this function to make sure no buffer will be
 * touched by the driver and/or hardware.
 */
void vb2_discard_done(struct vb2_queue *q)
{
	struct vb2_buffer *vb;
	unsigned long flags;

	spin_lock_irqsave(&q->done_lock, flags);
	list_for_each_entry(vb, &q->done_list, done_entry)
		vb->state = VB2_BUF_STATE_ERROR;
	spin_unlock_irqrestore(&q->done_lock, flags);
}
EXPORT_SYMBOL_GPL(vb2_discard_done);

/**
 * __qbuf_mmap() - handle qbuf of an MMAP buffer
 */
static int __qbuf_mmap(struct vb2_buffer *vb, const void *pb)
{
	int ret = call_bufop(vb->vb2_queue, fill_vb2_buffer,
			vb, pb, vb->planes);
	return ret ? ret : call_vb_qop(vb, buf_prepare, vb);
}

/**
 * __qbuf_userptr() - handle qbuf of a USERPTR buffer
 */
static int __qbuf_userptr(struct vb2_buffer *vb, const void *pb)
{
	struct vb2_plane planes[VB2_MAX_PLANES];
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	unsigned int plane;
	int ret;
	enum dma_data_direction dma_dir =
		q->is_output ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	bool reacquired = vb->planes[0].mem_priv == NULL;

	memset(planes, 0, sizeof(planes[0]) * vb->num_planes);
	/* Copy relevant information provided by the userspace */
	ret = call_bufop(vb->vb2_queue, fill_vb2_buffer, vb, pb, planes);
	if (ret)
		return ret;

	for (plane = 0; plane < vb->num_planes; ++plane) {
		/* Skip the plane if already verified */
		if (vb->planes[plane].m.userptr &&
			vb->planes[plane].m.userptr == planes[plane].m.userptr
			&& vb->planes[plane].length == planes[plane].length)
			continue;

		dprintk(3, "userspace address for plane %d changed, "
				"reacquiring memory\n", plane);

		/* Check if the provided plane buffer is large enough */
		if (planes[plane].length < q->plane_sizes[plane]) {
			dprintk(1, "provided buffer size %u is less than "
						"setup size %u for plane %d\n",
						planes[plane].length,
						q->plane_sizes[plane], plane);
			ret = -EINVAL;
			goto err;
		}

		/* Release previously acquired memory if present */
		if (vb->planes[plane].mem_priv) {
			if (!reacquired) {
				reacquired = true;
				call_void_vb_qop(vb, buf_cleanup, vb);
			}
			call_void_memop(vb, put_userptr, vb->planes[plane].mem_priv);
		}

		vb->planes[plane].mem_priv = NULL;
		vb->planes[plane].bytesused = 0;
		vb->planes[plane].length = 0;
		vb->planes[plane].m.userptr = 0;
		vb->planes[plane].data_offset = 0;

		/* Acquire each plane's memory */
		mem_priv = call_ptr_memop(vb, get_userptr, q->alloc_ctx[plane],
				      planes[plane].m.userptr,
				      planes[plane].length, dma_dir);
		if (IS_ERR_OR_NULL(mem_priv)) {
			dprintk(1, "failed acquiring userspace "
						"memory for plane %d\n", plane);
			ret = mem_priv ? PTR_ERR(mem_priv) : -EINVAL;
			goto err;
		}
		vb->planes[plane].mem_priv = mem_priv;
	}

	/*
	 * Now that everything is in order, copy relevant information
	 * provided by userspace.
	 */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		vb->planes[plane].bytesused = planes[plane].bytesused;
		vb->planes[plane].length = planes[plane].length;
		vb->planes[plane].m.userptr = planes[plane].m.userptr;
		vb->planes[plane].data_offset = planes[plane].data_offset;
	}

	if (reacquired) {
		/*
		 * One or more planes changed, so we must call buf_init to do
		 * the driver-specific initialization on the newly acquired
		 * buffer, if provided.
		 */
		ret = call_vb_qop(vb, buf_init, vb);
		if (ret) {
			dprintk(1, "buffer initialization failed\n");
			goto err;
		}
	}

	ret = call_vb_qop(vb, buf_prepare, vb);
	if (ret) {
		dprintk(1, "buffer preparation failed\n");
		call_void_vb_qop(vb, buf_cleanup, vb);
		goto err;
	}

	return 0;
err:
	/* In case of errors, release planes that were already acquired */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		if (vb->planes[plane].mem_priv)
			call_void_memop(vb, put_userptr,
				vb->planes[plane].mem_priv);
		vb->planes[plane].mem_priv = NULL;
		vb->planes[plane].m.userptr = 0;
		vb->planes[plane].length = 0;
	}

	return ret;
}

/**
 * __qbuf_dmabuf() - handle qbuf of a DMABUF buffer
 */
static int __qbuf_dmabuf(struct vb2_buffer *vb, const void *pb)
{
	struct vb2_plane planes[VB2_MAX_PLANES];
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	unsigned int plane;
	int ret;
	enum dma_data_direction dma_dir =
		q->is_output ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	bool reacquired = vb->planes[0].mem_priv == NULL;

	memset(planes, 0, sizeof(planes[0]) * vb->num_planes);
	/* Copy relevant information provided by the userspace */
	ret = call_bufop(vb->vb2_queue, fill_vb2_buffer, vb, pb, planes);
	if (ret)
		return ret;

	for (plane = 0; plane < vb->num_planes; ++plane) {
		struct dma_buf *dbuf = dma_buf_get(planes[plane].m.fd);

		if (IS_ERR_OR_NULL(dbuf)) {
			dprintk(1, "invalid dmabuf fd for plane %d\n",
				plane);
			ret = -EINVAL;
			goto err;
		}

		/* use DMABUF size if length is not provided */
		if (planes[plane].length == 0)
			planes[plane].length = dbuf->size;

		if (planes[plane].length < q->plane_sizes[plane]) {
			dprintk(1, "invalid dmabuf length for plane %d\n",
				plane);
			ret = -EINVAL;
			goto err;
		}

		/* Skip the plane if already verified */
		if (dbuf == vb->planes[plane].dbuf &&
			vb->planes[plane].length == planes[plane].length) {
			dma_buf_put(dbuf);
			continue;
		}

		dprintk(1, "buffer for plane %d changed\n", plane);

		if (!reacquired) {
			reacquired = true;
			call_void_vb_qop(vb, buf_cleanup, vb);
		}

		/* Release previously acquired memory if present */
		__vb2_plane_dmabuf_put(vb, &vb->planes[plane]);
		vb->planes[plane].bytesused = 0;
		vb->planes[plane].length = 0;
		vb->planes[plane].m.fd = 0;
		vb->planes[plane].data_offset = 0;

		/* Acquire each plane's memory */
		mem_priv = call_ptr_memop(vb, attach_dmabuf,
			q->alloc_ctx[plane], dbuf, planes[plane].length,
			dma_dir);
		if (IS_ERR(mem_priv)) {
			dprintk(1, "failed to attach dmabuf\n");
			ret = PTR_ERR(mem_priv);
			dma_buf_put(dbuf);
			goto err;
		}

		vb->planes[plane].dbuf = dbuf;
		vb->planes[plane].mem_priv = mem_priv;
	}

	/* TODO: This pins the buffer(s) with  dma_buf_map_attachment()).. but
	 * really we want to do this just before the DMA, not while queueing
	 * the buffer(s)..
	 */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		ret = call_memop(vb, map_dmabuf, vb->planes[plane].mem_priv);
		if (ret) {
			dprintk(1, "failed to map dmabuf for plane %d\n",
				plane);
			goto err;
		}
		vb->planes[plane].dbuf_mapped = 1;
	}

	/*
	 * Now that everything is in order, copy relevant information
	 * provided by userspace.
	 */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		vb->planes[plane].bytesused = planes[plane].bytesused;
		vb->planes[plane].length = planes[plane].length;
		vb->planes[plane].m.fd = planes[plane].m.fd;
		vb->planes[plane].data_offset = planes[plane].data_offset;
	}

	if (reacquired) {
		/*
		 * Call driver-specific initialization on the newly acquired buffer,
		 * if provided.
		 */
		ret = call_vb_qop(vb, buf_init, vb);
		if (ret) {
			dprintk(1, "buffer initialization failed\n");
			goto err;
		}
	}

	ret = call_vb_qop(vb, buf_prepare, vb);
	if (ret) {
		dprintk(1, "buffer preparation failed\n");
		call_void_vb_qop(vb, buf_cleanup, vb);
		goto err;
	}

	return 0;
err:
	/* In case of errors, release planes that were already acquired */
	__vb2_buf_dmabuf_put(vb);

	return ret;
}

/**
 * __enqueue_in_driver() - enqueue a vb2_buffer in driver for processing
 */
static void __enqueue_in_driver(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	unsigned int plane;

	vb->state = VB2_BUF_STATE_ACTIVE;
	atomic_inc(&q->owned_by_drv_count);

	trace_vb2_buf_queue(q, vb);

	/* sync buffers */
	for (plane = 0; plane < vb->num_planes; ++plane)
		call_void_memop(vb, prepare, vb->planes[plane].mem_priv);

	call_void_vb_qop(vb, buf_queue, vb);
}

static int __buf_prepare(struct vb2_buffer *vb, const void *pb)
{
	struct vb2_queue *q = vb->vb2_queue;
	int ret;

	if (q->error) {
		dprintk(1, "fatal error occurred on queue\n");
		return -EIO;
	}

	vb->state = VB2_BUF_STATE_PREPARING;

	switch (q->memory) {
	case VB2_MEMORY_MMAP:
		ret = __qbuf_mmap(vb, pb);
		break;
	case VB2_MEMORY_USERPTR:
		ret = __qbuf_userptr(vb, pb);
		break;
	case VB2_MEMORY_DMABUF:
		ret = __qbuf_dmabuf(vb, pb);
		break;
	default:
		WARN(1, "Invalid queue type\n");
		ret = -EINVAL;
	}

	if (ret)
		dprintk(1, "buffer preparation failed: %d\n", ret);
	vb->state = ret ? VB2_BUF_STATE_DEQUEUED : VB2_BUF_STATE_PREPARED;

	return ret;
}

/**
 * vb2_core_prepare_buf() - Pass ownership of a buffer from userspace
 *			to the kernel
 * @q:		videobuf2 queue
 * @index:	id number of the buffer
 * @pb:		buffer structure passed from userspace to vidioc_prepare_buf
 *		handler in driver
 *
 * Should be called from vidioc_prepare_buf ioctl handler of a driver.
 * The passed buffer should have been verified.
 * This function calls buf_prepare callback in the driver (if provided),
 * in which driver-specific buffer initialization can be performed,
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_prepare_buf handler in driver.
 */
int vb2_core_prepare_buf(struct vb2_queue *q, unsigned int index, void *pb)
{
	struct vb2_buffer *vb;
	int ret;

	vb = q->bufs[index];
	if (vb->state != VB2_BUF_STATE_DEQUEUED) {
		dprintk(1, "invalid buffer state %d\n",
			vb->state);
		return -EINVAL;
	}

	ret = __buf_prepare(vb, pb);
	if (ret)
		return ret;

	/* Fill buffer information for the userspace */
	ret = call_bufop(q, fill_user_buffer, vb, pb);
	if (ret)
		return ret;

	dprintk(1, "prepare of buffer %d succeeded\n", vb->index);

	return ret;
}
EXPORT_SYMBOL_GPL(vb2_core_prepare_buf);

/**
 * vb2_start_streaming() - Attempt to start streaming.
 * @q:		videobuf2 queue
 *
 * Attempt to start streaming. When this function is called there must be
 * at least q->min_buffers_needed buffers queued up (i.e. the minimum
 * number of buffers required for the DMA engine to function). If the
 * @start_streaming op fails it is supposed to return all the driver-owned
 * buffers back to vb2 in state QUEUED. Check if that happened and if
 * not warn and reclaim them forcefully.
 */
static int vb2_start_streaming(struct vb2_queue *q)
{
	struct vb2_buffer *vb;
	int ret;

	/*
	 * If any buffers were queued before streamon,
	 * we can now pass them to driver for processing.
	 */
	list_for_each_entry(vb, &q->queued_list, queued_entry)
		__enqueue_in_driver(vb);

	/* Tell the driver to start streaming */
	q->start_streaming_called = 1;
	ret = call_qop(q, start_streaming, q,
		       atomic_read(&q->owned_by_drv_count));
	if (!ret)
		return 0;

	q->start_streaming_called = 0;

	dprintk(1, "driver refused to start streaming\n");
	/*
	 * If you see this warning, then the driver isn't cleaning up properly
	 * after a failed start_streaming(). See the start_streaming()
	 * documentation in videobuf2-core.h for more information how buffers
	 * should be returned to vb2 in start_streaming().
	 */
	if (WARN_ON(atomic_read(&q->owned_by_drv_count))) {
		unsigned i;

		/*
		 * Forcefully reclaim buffers if the driver did not
		 * correctly return them to vb2.
		 */
		for (i = 0; i < q->num_buffers; ++i) {
			vb = q->bufs[i];
			if (vb->state == VB2_BUF_STATE_ACTIVE)
				vb2_buffer_done(vb, VB2_BUF_STATE_QUEUED);
		}
		/* Must be zero now */
		WARN_ON(atomic_read(&q->owned_by_drv_count));
	}
	/*
	 * If done_list is not empty, then start_streaming() didn't call
	 * vb2_buffer_done(vb, VB2_BUF_STATE_QUEUED) but STATE_ERROR or
	 * STATE_DONE.
	 */
	WARN_ON(!list_empty(&q->done_list));
	return ret;
}

/**
 * vb2_core_qbuf() - Queue a buffer from userspace
 * @q:		videobuf2 queue
 * @index:	id number of the buffer
 * @pb:		buffer structure passed from userspace to vidioc_qbuf handler
 *		in driver
 *
 * Should be called from vidioc_qbuf ioctl handler of a driver.
 * The passed buffer should have been verified.
 * This function:
 * 1) if necessary, calls buf_prepare callback in the driver (if provided), in
 *    which driver-specific buffer initialization can be performed,
 * 2) if streaming is on, queues the buffer in driver by the means of buf_queue
 *    callback for processing.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_qbuf handler in driver.
 */
int vb2_core_qbuf(struct vb2_queue *q, unsigned int index, void *pb)
{
	struct vb2_buffer *vb;
	int ret;

	vb = q->bufs[index];

	switch (vb->state) {
	case VB2_BUF_STATE_DEQUEUED:
		ret = __buf_prepare(vb, pb);
		if (ret)
			return ret;
		break;
	case VB2_BUF_STATE_PREPARED:
		break;
	case VB2_BUF_STATE_PREPARING:
		dprintk(1, "buffer still being prepared\n");
		return -EINVAL;
	default:
		dprintk(1, "invalid buffer state %d\n", vb->state);
		return -EINVAL;
	}

	/*
	 * Add to the queued buffers list, a buffer will stay on it until
	 * dequeued in dqbuf.
	 */
	list_add_tail(&vb->queued_entry, &q->queued_list);
	q->queued_count++;
	q->waiting_for_buffers = false;
	vb->state = VB2_BUF_STATE_QUEUED;

	call_bufop(q, set_timestamp, vb, pb);

	trace_vb2_qbuf(q, vb);

	/*
	 * If already streaming, give the buffer to driver for processing.
	 * If not, the buffer will be given to driver on next streamon.
	 */
	if (q->start_streaming_called)
		__enqueue_in_driver(vb);

	/* Fill buffer information for the userspace */
	ret = call_bufop(q, fill_user_buffer, vb, pb);
	if (ret)
		return ret;

	/*
	 * If streamon has been called, and we haven't yet called
	 * start_streaming() since not enough buffers were queued, and
	 * we now have reached the minimum number of queued buffers,
	 * then we can finally call start_streaming().
	 */
	if (q->streaming && !q->start_streaming_called &&
	    q->queued_count >= q->min_buffers_needed) {
		ret = vb2_start_streaming(q);
		if (ret)
			return ret;
	}

	dprintk(1, "qbuf of buffer %d succeeded\n", vb->index);
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_qbuf);

/**
 * __vb2_wait_for_done_vb() - wait for a buffer to become available
 * for dequeuing
 *
 * Will sleep if required for nonblocking == false.
 */
static int __vb2_wait_for_done_vb(struct vb2_queue *q, int nonblocking)
{
	/*
	 * All operations on vb_done_list are performed under done_lock
	 * spinlock protection. However, buffers may be removed from
	 * it and returned to userspace only while holding both driver's
	 * lock and the done_lock spinlock. Thus we can be sure that as
	 * long as we hold the driver's lock, the list will remain not
	 * empty if list_empty() check succeeds.
	 */

	for (;;) {
		int ret;

		if (!q->streaming) {
			dprintk(1, "streaming off, will not wait for buffers\n");
			return -EINVAL;
		}

		if (q->error) {
			dprintk(1, "Queue in error state, will not wait for buffers\n");
			return -EIO;
		}

		if (q->last_buffer_dequeued) {
			dprintk(3, "last buffer dequeued already, will not wait for buffers\n");
			return -EPIPE;
		}

		if (!list_empty(&q->done_list)) {
			/*
			 * Found a buffer that we were waiting for.
			 */
			break;
		}

		if (nonblocking) {
			dprintk(1, "nonblocking and no buffers to dequeue, "
								"will not wait\n");
			return -EAGAIN;
		}

		/*
		 * We are streaming and blocking, wait for another buffer to
		 * become ready or for streamoff. Driver's lock is released to
		 * allow streamoff or qbuf to be called while waiting.
		 */
		call_void_qop(q, wait_prepare, q);

		/*
		 * All locks have been released, it is safe to sleep now.
		 */
		dprintk(3, "will sleep waiting for buffers\n");
		ret = wait_event_interruptible(q->done_wq,
				!list_empty(&q->done_list) || !q->streaming ||
				q->error);

		/*
		 * We need to reevaluate both conditions again after reacquiring
		 * the locks or return an error if one occurred.
		 */
		call_void_qop(q, wait_finish, q);
		if (ret) {
			dprintk(1, "sleep was interrupted\n");
			return ret;
		}
	}
	return 0;
}

/**
 * __vb2_get_done_vb() - get a buffer ready for dequeuing
 *
 * Will sleep if required for nonblocking == false.
 */
static int __vb2_get_done_vb(struct vb2_queue *q, struct vb2_buffer **vb,
			     void *pb, int nonblocking)
{
	unsigned long flags;
	int ret = 0;

	/*
	 * Wait for at least one buffer to become available on the done_list.
	 */
	ret = __vb2_wait_for_done_vb(q, nonblocking);
	if (ret)
		return ret;

	/*
	 * Driver's lock has been held since we last verified that done_list
	 * is not empty, so no need for another list_empty(done_list) check.
	 */
	spin_lock_irqsave(&q->done_lock, flags);
	*vb = list_first_entry(&q->done_list, struct vb2_buffer, done_entry);
	/*
	 * Only remove the buffer from done_list if all planes can be
	 * handled. Some cases such as V4L2 file I/O and DVB have pb
	 * == NULL; skip the check then as there's nothing to verify.
	 */
	if (pb)
		ret = call_bufop(q, verify_planes_array, *vb, pb);
	if (!ret)
		list_del(&(*vb)->done_entry);
	spin_unlock_irqrestore(&q->done_lock, flags);

	return ret;
}

/**
 * vb2_wait_for_all_buffers() - wait until all buffers are given back to vb2
 * @q:		videobuf2 queue
 *
 * This function will wait until all buffers that have been given to the driver
 * by buf_queue() are given back to vb2 with vb2_buffer_done(). It doesn't call
 * wait_prepare, wait_finish pair. It is intended to be called with all locks
 * taken, for example from stop_streaming() callback.
 */
int vb2_wait_for_all_buffers(struct vb2_queue *q)
{
	if (!q->streaming) {
		dprintk(1, "streaming off, will not wait for buffers\n");
		return -EINVAL;
	}

	if (q->start_streaming_called)
		wait_event(q->done_wq, !atomic_read(&q->owned_by_drv_count));
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_wait_for_all_buffers);

/**
 * __vb2_dqbuf() - bring back the buffer to the DEQUEUED state
 */
static void __vb2_dqbuf(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	unsigned int i;

	/* nothing to do if the buffer is already dequeued */
	if (vb->state == VB2_BUF_STATE_DEQUEUED)
		return;

	vb->state = VB2_BUF_STATE_DEQUEUED;

	/* unmap DMABUF buffer */
	if (q->memory == VB2_MEMORY_DMABUF)
		for (i = 0; i < vb->num_planes; ++i) {
			if (!vb->planes[i].dbuf_mapped)
				continue;
			call_void_memop(vb, unmap_dmabuf, vb->planes[i].mem_priv);
			vb->planes[i].dbuf_mapped = 0;
		}
}

/**
 * vb2_dqbuf() - Dequeue a buffer to the userspace
 * @q:		videobuf2 queue
 * @pb:		buffer structure passed from userspace to vidioc_dqbuf handler
 *		in driver
 * @nonblocking: if true, this call will not sleep waiting for a buffer if no
 *		 buffers ready for dequeuing are present. Normally the driver
 *		 would be passing (file->f_flags & O_NONBLOCK) here
 *
 * Should be called from vidioc_dqbuf ioctl handler of a driver.
 * The passed buffer should have been verified.
 * This function:
 * 1) calls buf_finish callback in the driver (if provided), in which
 *    driver can perform any additional operations that may be required before
 *    returning the buffer to userspace, such as cache sync,
 * 2) the buffer struct members are filled with relevant information for
 *    the userspace.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_dqbuf handler in driver.
 */
int vb2_core_dqbuf(struct vb2_queue *q, void *pb, bool nonblocking)
{
	struct vb2_buffer *vb = NULL;
	int ret;

	ret = __vb2_get_done_vb(q, &vb, pb, nonblocking);
	if (ret < 0)
		return ret;

	switch (vb->state) {
	case VB2_BUF_STATE_DONE:
		dprintk(3, "returning done buffer\n");
		break;
	case VB2_BUF_STATE_ERROR:
		dprintk(3, "returning done buffer with errors\n");
		break;
	default:
		dprintk(1, "invalid buffer state\n");
		return -EINVAL;
	}

	call_void_vb_qop(vb, buf_finish, vb);

	/* Fill buffer information for the userspace */
	ret = call_bufop(q, fill_user_buffer, vb, pb);
	if (ret)
		return ret;

	/* Remove from videobuf queue */
	list_del(&vb->queued_entry);
	q->queued_count--;

	trace_vb2_dqbuf(q, vb);

	/* go back to dequeued state */
	__vb2_dqbuf(vb);

	dprintk(1, "dqbuf of buffer %d, with state %d\n",
			vb->index, vb->state);

	return 0;

}
EXPORT_SYMBOL_GPL(vb2_core_dqbuf);

/**
 * __vb2_queue_cancel() - cancel and stop (pause) streaming
 *
 * Removes all queued buffers from driver's queue and all buffers queued by
 * userspace from videobuf's queue. Returns to state after reqbufs.
 */
static void __vb2_queue_cancel(struct vb2_queue *q)
{
	unsigned int i;

	/*
	 * Tell driver to stop all transactions and release all queued
	 * buffers.
	 */
	if (q->start_streaming_called)
		call_void_qop(q, stop_streaming, q);

	/*
	 * If you see this warning, then the driver isn't cleaning up properly
	 * in stop_streaming(). See the stop_streaming() documentation in
	 * videobuf2-core.h for more information how buffers should be returned
	 * to vb2 in stop_streaming().
	 */
	if (WARN_ON(atomic_read(&q->owned_by_drv_count))) {
		for (i = 0; i < q->num_buffers; ++i)
			if (q->bufs[i]->state == VB2_BUF_STATE_ACTIVE)
				vb2_buffer_done(q->bufs[i], VB2_BUF_STATE_ERROR);
		/* Must be zero now */
		WARN_ON(atomic_read(&q->owned_by_drv_count));
	}

	q->streaming = 0;
	q->start_streaming_called = 0;
	q->queued_count = 0;
	q->error = 0;

	/*
	 * Remove all buffers from videobuf's list...
	 */
	INIT_LIST_HEAD(&q->queued_list);
	/*
	 * ...and done list; userspace will not receive any buffers it
	 * has not already dequeued before initiating cancel.
	 */
	INIT_LIST_HEAD(&q->done_list);
	atomic_set(&q->owned_by_drv_count, 0);
	wake_up_all(&q->done_wq);

	/*
	 * Reinitialize all buffers for next use.
	 * Make sure to call buf_finish for any queued buffers. Normally
	 * that's done in dqbuf, but that's not going to happen when we
	 * cancel the whole queue. Note: this code belongs here, not in
	 * __vb2_dqbuf() since in vb2_internal_dqbuf() there is a critical
	 * call to __fill_v4l2_buffer() after buf_finish(). That order can't
	 * be changed, so we can't move the buf_finish() to __vb2_dqbuf().
	 */
	for (i = 0; i < q->num_buffers; ++i) {
		struct vb2_buffer *vb = q->bufs[i];

		if (vb->state != VB2_BUF_STATE_DEQUEUED) {
			vb->state = VB2_BUF_STATE_PREPARED;
			call_void_vb_qop(vb, buf_finish, vb);
		}
		__vb2_dqbuf(vb);
	}
}

int vb2_core_streamon(struct vb2_queue *q, unsigned int type)
{
	int ret;

	if (type != q->type) {
		dprintk(1, "invalid stream type\n");
		return -EINVAL;
	}

	if (q->streaming) {
		dprintk(3, "already streaming\n");
		return 0;
	}

	if (!q->num_buffers) {
		dprintk(1, "no buffers have been allocated\n");
		return -EINVAL;
	}

	if (q->num_buffers < q->min_buffers_needed) {
		dprintk(1, "need at least %u allocated buffers\n",
				q->min_buffers_needed);
		return -EINVAL;
	}

	/*
	 * Tell driver to start streaming provided sufficient buffers
	 * are available.
	 */
	if (q->queued_count >= q->min_buffers_needed) {
		ret = vb2_start_streaming(q);
		if (ret) {
			__vb2_queue_cancel(q);
			return ret;
		}
	}

	q->streaming = 1;

	dprintk(3, "successful\n");
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_streamon);

/**
 * vb2_queue_error() - signal a fatal error on the queue
 * @q:		videobuf2 queue
 *
 * Flag that a fatal unrecoverable error has occurred and wake up all processes
 * waiting on the queue. Polling will now set POLLERR and queuing and dequeuing
 * buffers will return -EIO.
 *
 * The error flag will be cleared when cancelling the queue, either from
 * vb2_streamoff or vb2_queue_release. Drivers should thus not call this
 * function before starting the stream, otherwise the error flag will remain set
 * until the queue is released when closing the device node.
 */
void vb2_queue_error(struct vb2_queue *q)
{
	q->error = 1;

	wake_up_all(&q->done_wq);
}
EXPORT_SYMBOL_GPL(vb2_queue_error);

int vb2_core_streamoff(struct vb2_queue *q, unsigned int type)
{
	if (type != q->type) {
		dprintk(1, "invalid stream type\n");
		return -EINVAL;
	}

	/*
	 * Cancel will pause streaming and remove all buffers from the driver
	 * and videobuf, effectively returning control over them to userspace.
	 *
	 * Note that we do this even if q->streaming == 0: if you prepare or
	 * queue buffers, and then call streamoff without ever having called
	 * streamon, you would still expect those buffers to be returned to
	 * their normal dequeued state.
	 */
	__vb2_queue_cancel(q);
	q->waiting_for_buffers = !q->is_output;
	q->last_buffer_dequeued = false;

	dprintk(3, "successful\n");
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_streamoff);

/**
 * __find_plane_by_offset() - find plane associated with the given offset off
 */
static int __find_plane_by_offset(struct vb2_queue *q, unsigned long off,
			unsigned int *_buffer, unsigned int *_plane)
{
	struct vb2_buffer *vb;
	unsigned int buffer, plane;

	/*
	 * Go over all buffers and their planes, comparing the given offset
	 * with an offset assigned to each plane. If a match is found,
	 * return its buffer and plane numbers.
	 */
	for (buffer = 0; buffer < q->num_buffers; ++buffer) {
		vb = q->bufs[buffer];

		for (plane = 0; plane < vb->num_planes; ++plane) {
			if (vb->planes[plane].m.offset == off) {
				*_buffer = buffer;
				*_plane = plane;
				return 0;
			}
		}
	}

	return -EINVAL;
}

/**
 * vb2_core_expbuf() - Export a buffer as a file descriptor
 * @q:		videobuf2 queue
 * @fd:		file descriptor associated with DMABUF (set by driver) *
 * @type:	buffer type
 * @index:	id number of the buffer
 * @plane:	index of the plane to be exported, 0 for single plane queues
 * @flags:	flags for newly created file, currently only O_CLOEXEC is
 *		supported, refer to manual of open syscall for more details
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_expbuf handler in driver.
 */
int vb2_core_expbuf(struct vb2_queue *q, int *fd, unsigned int type,
		unsigned int index, unsigned int plane, unsigned int flags)
{
	struct vb2_buffer *vb = NULL;
	struct vb2_plane *vb_plane;
	int ret;
	struct dma_buf *dbuf;

	if (q->memory != VB2_MEMORY_MMAP) {
		dprintk(1, "queue is not currently set up for mmap\n");
		return -EINVAL;
	}

	if (!q->mem_ops->get_dmabuf) {
		dprintk(1, "queue does not support DMA buffer exporting\n");
		return -EINVAL;
	}

	if (flags & ~(O_CLOEXEC | O_ACCMODE)) {
		dprintk(1, "queue does support only O_CLOEXEC and access mode flags\n");
		return -EINVAL;
	}

	if (type != q->type) {
		dprintk(1, "invalid buffer type\n");
		return -EINVAL;
	}

	if (index >= q->num_buffers) {
		dprintk(1, "buffer index out of range\n");
		return -EINVAL;
	}

	vb = q->bufs[index];

	if (plane >= vb->num_planes) {
		dprintk(1, "buffer plane out of range\n");
		return -EINVAL;
	}

	if (vb2_fileio_is_active(q)) {
		dprintk(1, "expbuf: file io in progress\n");
		return -EBUSY;
	}

	vb_plane = &vb->planes[plane];

	dbuf = call_ptr_memop(vb, get_dmabuf, vb_plane->mem_priv,
				flags & O_ACCMODE);
	if (IS_ERR_OR_NULL(dbuf)) {
		dprintk(1, "failed to export buffer %d, plane %d\n",
			index, plane);
		return -EINVAL;
	}

	ret = dma_buf_fd(dbuf, flags & ~O_ACCMODE);
	if (ret < 0) {
		dprintk(3, "buffer %d, plane %d failed to export (%d)\n",
			index, plane, ret);
		dma_buf_put(dbuf);
		return ret;
	}

	dprintk(3, "buffer %d, plane %d exported as %d descriptor\n",
		index, plane, ret);
	*fd = ret;

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_expbuf);

/**
 * vb2_mmap() - map video buffers into application address space
 * @q:		videobuf2 queue
 * @vma:	vma passed to the mmap file operation handler in the driver
 *
 * Should be called from mmap file operation handler of a driver.
 * This function maps one plane of one of the available video buffers to
 * userspace. To map whole video memory allocated on reqbufs, this function
 * has to be called once per each plane per each buffer previously allocated.
 *
 * When the userspace application calls mmap, it passes to it an offset returned
 * to it earlier by the means of vidioc_querybuf handler. That offset acts as
 * a "cookie", which is then used to identify the plane to be mapped.
 * This function finds a plane with a matching offset and a mapping is performed
 * by the means of a provided memory operation.
 *
 * The return values from this function are intended to be directly returned
 * from the mmap handler in driver.
 */
int vb2_mmap(struct vb2_queue *q, struct vm_area_struct *vma)
{
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	struct vb2_buffer *vb;
	unsigned int buffer = 0, plane = 0;
	int ret;
	unsigned long length;

	if (q->memory != VB2_MEMORY_MMAP) {
		dprintk(1, "queue is not currently set up for mmap\n");
		return -EINVAL;
	}

	/*
	 * Check memory area access mode.
	 */
	if (!(vma->vm_flags & VM_SHARED)) {
		dprintk(1, "invalid vma flags, VM_SHARED needed\n");
		return -EINVAL;
	}
	if (q->is_output) {
		if (!(vma->vm_flags & VM_WRITE)) {
			dprintk(1, "invalid vma flags, VM_WRITE needed\n");
			return -EINVAL;
		}
	} else {
		if (!(vma->vm_flags & VM_READ)) {
			dprintk(1, "invalid vma flags, VM_READ needed\n");
			return -EINVAL;
		}
	}
	if (vb2_fileio_is_active(q)) {
		dprintk(1, "mmap: file io in progress\n");
		return -EBUSY;
	}

	/*
	 * Find the plane corresponding to the offset passed by userspace.
	 */
	ret = __find_plane_by_offset(q, off, &buffer, &plane);
	if (ret)
		return ret;

	vb = q->bufs[buffer];

	/*
	 * MMAP requires page_aligned buffers.
	 * The buffer length was page_aligned at __vb2_buf_mem_alloc(),
	 * so, we need to do the same here.
	 */
	length = PAGE_ALIGN(vb->planes[plane].length);
	if (length < (vma->vm_end - vma->vm_start)) {
		dprintk(1,
			"MMAP invalid, as it would overflow buffer length\n");
		return -EINVAL;
	}

	mutex_lock(&q->mmap_lock);
	ret = call_memop(vb, mmap, vb->planes[plane].mem_priv, vma);
	mutex_unlock(&q->mmap_lock);
	if (ret)
		return ret;

	dprintk(3, "buffer %d, plane %d successfully mapped\n", buffer, plane);
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_mmap);

#ifndef CONFIG_MMU
unsigned long vb2_get_unmapped_area(struct vb2_queue *q,
				    unsigned long addr,
				    unsigned long len,
				    unsigned long pgoff,
				    unsigned long flags)
{
	unsigned long off = pgoff << PAGE_SHIFT;
	struct vb2_buffer *vb;
	unsigned int buffer, plane;
	void *vaddr;
	int ret;

	if (q->memory != VB2_MEMORY_MMAP) {
		dprintk(1, "queue is not currently set up for mmap\n");
		return -EINVAL;
	}

	/*
	 * Find the plane corresponding to the offset passed by userspace.
	 */
	ret = __find_plane_by_offset(q, off, &buffer, &plane);
	if (ret)
		return ret;

	vb = q->bufs[buffer];

	vaddr = vb2_plane_vaddr(vb, plane);
	return vaddr ? (unsigned long)vaddr : -EINVAL;
}
EXPORT_SYMBOL_GPL(vb2_get_unmapped_area);
#endif

/**
 * vb2_core_queue_init() - initialize a videobuf2 queue
 * @q:		videobuf2 queue; this structure should be allocated in driver
 *
 * The vb2_queue structure should be allocated by the driver. The driver is
 * responsible of clearing it's content and setting initial values for some
 * required entries before calling this function.
 * q->ops, q->mem_ops, q->type and q->io_modes are mandatory. Please refer
 * to the struct vb2_queue description in include/media/videobuf2-core.h
 * for more information.
 */
int vb2_core_queue_init(struct vb2_queue *q)
{
	/*
	 * Sanity check
	 */
	if (WARN_ON(!q)			  ||
	    WARN_ON(!q->ops)		  ||
	    WARN_ON(!q->mem_ops)	  ||
	    WARN_ON(!q->type)		  ||
	    WARN_ON(!q->io_modes)	  ||
	    WARN_ON(!q->ops->queue_setup) ||
	    WARN_ON(!q->ops->buf_queue))
		return -EINVAL;

	INIT_LIST_HEAD(&q->queued_list);
	INIT_LIST_HEAD(&q->done_list);
	spin_lock_init(&q->done_lock);
	mutex_init(&q->mmap_lock);
	init_waitqueue_head(&q->done_wq);

	if (q->buf_struct_size == 0)
		q->buf_struct_size = sizeof(struct vb2_buffer);

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_queue_init);

/**
 * vb2_core_queue_release() - stop streaming, release the queue and free memory
 * @q:		videobuf2 queue
 *
 * This function stops streaming and performs necessary clean ups, including
 * freeing video buffer memory. The driver is responsible for freeing
 * the vb2_queue structure itself.
 */
void vb2_core_queue_release(struct vb2_queue *q)
{
	__vb2_queue_cancel(q);
	mutex_lock(&q->mmap_lock);
	__vb2_queue_free(q, q->num_buffers);
	mutex_unlock(&q->mmap_lock);
}
EXPORT_SYMBOL_GPL(vb2_core_queue_release);

MODULE_DESCRIPTION("Driver helper framework for Video for Linux 2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>, Marek Szyprowski");
MODULE_LICENSE("GPL");
