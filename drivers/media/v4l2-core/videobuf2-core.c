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
#include <media/v4l2-mc.h>

#include <trace/events/vb2.h>

static int debug;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)					      \
	do {								      \
		if (debug >= level)					      \
			pr_info("vb2-core: %s: " fmt, __func__, ## arg); \
	} while (0)

#ifdef CONFIG_VIDEO_ADV_DEBUG

/*
 * If advanced debugging is on, then count how often each op is called
 * successfully, which can either be per-buffer or per-queue.
 *
 * This makes it easy to check that the 'init' and 'cleanup'
 * (and variations thereof) stay balanced.
 */

#define log_memop(vb, op)						\
	dprintk(2, "call_memop(%p, %d, %s)%s\n",			\
		(vb)->vb2_queue, (vb)->index, #op,			\
		(vb)->vb2_queue->mem_ops->op ? "" : " (nop)")

#define call_memop(vb, op, args...)					\
({									\
	struct vb2_queue *_q = (vb)->vb2_queue;				\
	int err;							\
									\
	log_memop(vb, op);						\
	err = _q->mem_ops->op ? _q->mem_ops->op(args) : 0;		\
	if (!err)							\
		(vb)->cnt_mem_ ## op++;					\
	err;								\
})

#define call_ptr_memop(vb, op, args...)					\
({									\
	struct vb2_queue *_q = (vb)->vb2_queue;				\
	void *ptr;							\
									\
	log_memop(vb, op);						\
	ptr = _q->mem_ops->op ? _q->mem_ops->op(args) : NULL;		\
	if (!IS_ERR_OR_NULL(ptr))					\
		(vb)->cnt_mem_ ## op++;					\
	ptr;								\
})

#define call_void_memop(vb, op, args...)				\
({									\
	struct vb2_queue *_q = (vb)->vb2_queue;				\
									\
	log_memop(vb, op);						\
	if (_q->mem_ops->op)						\
		_q->mem_ops->op(args);					\
	(vb)->cnt_mem_ ## op++;						\
})

#define log_qop(q, op)							\
	dprintk(2, "call_qop(%p, %s)%s\n", q, #op,			\
		(q)->ops->op ? "" : " (nop)")

#define call_qop(q, op, args...)					\
({									\
	int err;							\
									\
	log_qop(q, op);							\
	err = (q)->ops->op ? (q)->ops->op(args) : 0;			\
	if (!err)							\
		(q)->cnt_ ## op++;					\
	err;								\
})

#define call_void_qop(q, op, args...)					\
({									\
	log_qop(q, op);							\
	if ((q)->ops->op)						\
		(q)->ops->op(args);					\
	(q)->cnt_ ## op++;						\
})

#define log_vb_qop(vb, op, args...)					\
	dprintk(2, "call_vb_qop(%p, %d, %s)%s\n",			\
		(vb)->vb2_queue, (vb)->index, #op,			\
		(vb)->vb2_queue->ops->op ? "" : " (nop)")

#define call_vb_qop(vb, op, args...)					\
({									\
	int err;							\
									\
	log_vb_qop(vb, op);						\
	err = (vb)->vb2_queue->ops->op ?				\
		(vb)->vb2_queue->ops->op(args) : 0;			\
	if (!err)							\
		(vb)->cnt_ ## op++;					\
	err;								\
})

#define call_void_vb_qop(vb, op, args...)				\
({									\
	log_vb_qop(vb, op);						\
	if ((vb)->vb2_queue->ops->op)					\
		(vb)->vb2_queue->ops->op(args);				\
	(vb)->cnt_ ## op++;						\
})

#else

#define call_memop(vb, op, args...)					\
	((vb)->vb2_queue->mem_ops->op ?					\
		(vb)->vb2_queue->mem_ops->op(args) : 0)

#define call_ptr_memop(vb, op, args...)					\
	((vb)->vb2_queue->mem_ops->op ?					\
		(vb)->vb2_queue->mem_ops->op(args) : NULL)

#define call_void_memop(vb, op, args...)				\
	do {								\
		if ((vb)->vb2_queue->mem_ops->op)			\
			(vb)->vb2_queue->mem_ops->op(args);		\
	} while (0)

#define call_qop(q, op, args...)					\
	((q)->ops->op ? (q)->ops->op(args) : 0)

#define call_void_qop(q, op, args...)					\
	do {								\
		if ((q)->ops->op)					\
			(q)->ops->op(args);				\
	} while (0)

#define call_vb_qop(vb, op, args...)					\
	((vb)->vb2_queue->ops->op ? (vb)->vb2_queue->ops->op(args) : 0)

#define call_void_vb_qop(vb, op, args...)				\
	do {								\
		if ((vb)->vb2_queue->ops->op)				\
			(vb)->vb2_queue->ops->op(args);			\
	} while (0)

#endif

#define call_bufop(q, op, args...)					\
({									\
	int ret = 0;							\
	if (q && q->buf_ops && q->buf_ops->op)				\
		ret = q->buf_ops->op(args);				\
	ret;								\
})

#define call_void_bufop(q, op, args...)					\
({									\
	if (q && q->buf_ops && q->buf_ops->op)				\
		q->buf_ops->op(args);					\
})

static void __vb2_queue_cancel(struct vb2_queue *q);
static void __enqueue_in_driver(struct vb2_buffer *vb);

/**
 * __vb2_buf_mem_alloc() - allocate video memory for the given buffer
 */
static int __vb2_buf_mem_alloc(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	int plane;
	int ret = -ENOMEM;

	/*
	 * Allocate memory for all planes in this buffer
	 * NOTE: mmapped areas should be page aligned
	 */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		unsigned long size = PAGE_ALIGN(vb->planes[plane].length);

		mem_priv = call_ptr_memop(vb, alloc,
				q->alloc_devs[plane] ? : q->dev,
				q->dma_attrs, size, q->dma_dir, q->gfp_flags);
		if (IS_ERR_OR_NULL(mem_priv)) {
			if (mem_priv)
				ret = PTR_ERR(mem_priv);
			goto free;
		}

		/* Associate allocator private data with this plane */
		vb->planes[plane].mem_priv = mem_priv;
	}

	return 0;
free:
	/* Free already allocated memory if one of the allocations failed */
	for (; plane > 0; --plane) {
		call_void_memop(vb, put, vb->planes[plane - 1].mem_priv);
		vb->planes[plane - 1].mem_priv = NULL;
	}

	return ret;
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
 * __setup_offsets() - setup unique offsets ("cookies") for every plane in
 * the buffer.
 */
static void __setup_offsets(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	unsigned int plane;
	unsigned long off = 0;

	if (vb->index) {
		struct vb2_buffer *prev = q->bufs[vb->index - 1];
		struct vb2_plane *p = &prev->planes[prev->num_planes - 1];

		off = PAGE_ALIGN(p->m.offset + p->length);
	}

	for (plane = 0; plane < vb->num_planes; ++plane) {
		vb->planes[plane].m.offset = off;

		dprintk(3, "buffer %d, plane %d offset 0x%08lx\n",
				vb->index, plane, off);

		off += vb->planes[plane].length;
		off = PAGE_ALIGN(off);
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
			     unsigned int num_buffers, unsigned int num_planes,
			     const unsigned plane_sizes[VB2_MAX_PLANES])
{
	unsigned int buffer, plane;
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
		for (plane = 0; plane < num_planes; ++plane) {
			vb->planes[plane].length = plane_sizes[plane];
			vb->planes[plane].min_length = plane_sizes[plane];
		}
		q->bufs[vb->index] = vb;

		/* Allocate video buffer memory for the MMAP type */
		if (memory == VB2_MEMORY_MMAP) {
			ret = __vb2_buf_mem_alloc(vb);
			if (ret) {
				dprintk(1, "failed allocating memory for buffer %d\n",
					buffer);
				q->bufs[vb->index] = NULL;
				kfree(vb);
				break;
			}
			__setup_offsets(vb);
			/*
			 * Call the driver-provided buffer initialization
			 * callback, if given. An error in initialization
			 * results in queue setup failure.
			 */
			ret = call_vb_qop(vb, buf_init, vb);
			if (ret) {
				dprintk(1, "buffer %d %p initialization failed\n",
					buffer, vb);
				__vb2_buf_mem_free(vb);
				q->bufs[vb->index] = NULL;
				kfree(vb);
				break;
			}
		}
	}

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

		if (unbalanced || debug) {
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

		if (unbalanced || debug) {
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

void vb2_core_querybuf(struct vb2_queue *q, unsigned int index, void *pb)
{
	call_void_bufop(q, fill_user_buffer, q->bufs[index], pb);
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

int vb2_core_reqbufs(struct vb2_queue *q, enum vb2_memory memory,
		unsigned int *count)
{
	unsigned int num_buffers, allocated_buffers, num_planes = 0;
	unsigned plane_sizes[VB2_MAX_PLANES] = { };
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
	memset(q->alloc_devs, 0, sizeof(q->alloc_devs));
	q->memory = memory;

	/*
	 * Ask the driver how many buffers and planes per buffer it requires.
	 * Driver also sets the size and allocator context for each plane.
	 */
	ret = call_qop(q, queue_setup, q, &num_buffers, &num_planes,
		       plane_sizes, q->alloc_devs);
	if (ret)
		return ret;

	/* Finally, allocate buffers and video memory */
	allocated_buffers =
		__vb2_queue_alloc(q, memory, num_buffers, num_planes, plane_sizes);
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
		/*
		 * num_planes is set by the previous queue_setup(), but since it
		 * signals to queue_setup() whether it is called from create_bufs()
		 * vs reqbufs() we zero it here to signal that queue_setup() is
		 * called for the reqbufs() case.
		 */
		num_planes = 0;

		ret = call_qop(q, queue_setup, q, &num_buffers,
			       &num_planes, plane_sizes, q->alloc_devs);

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

int vb2_core_create_bufs(struct vb2_queue *q, enum vb2_memory memory,
		unsigned int *count, unsigned requested_planes,
		const unsigned requested_sizes[])
{
	unsigned int num_planes = 0, num_buffers, allocated_buffers;
	unsigned plane_sizes[VB2_MAX_PLANES] = { };
	int ret;

	if (q->num_buffers == VB2_MAX_FRAME) {
		dprintk(1, "maximum number of buffers already allocated\n");
		return -ENOBUFS;
	}

	if (!q->num_buffers) {
		memset(q->alloc_devs, 0, sizeof(q->alloc_devs));
		q->memory = memory;
		q->waiting_for_buffers = !q->is_output;
	}

	num_buffers = min(*count, VB2_MAX_FRAME - q->num_buffers);

	if (requested_planes && requested_sizes) {
		num_planes = requested_planes;
		memcpy(plane_sizes, requested_sizes, sizeof(plane_sizes));
	}

	/*
	 * Ask the driver, whether the requested number of buffers, planes per
	 * buffer and their sizes are acceptable
	 */
	ret = call_qop(q, queue_setup, q, &num_buffers,
		       &num_planes, plane_sizes, q->alloc_devs);
	if (ret)
		return ret;

	/* Finally, allocate buffers and video memory */
	allocated_buffers = __vb2_queue_alloc(q, memory, num_buffers,
				num_planes, plane_sizes);
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
		ret = call_qop(q, queue_setup, q, &num_buffers,
			       &num_planes, plane_sizes, q->alloc_devs);

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

void *vb2_plane_vaddr(struct vb2_buffer *vb, unsigned int plane_no)
{
	if (plane_no >= vb->num_planes || !vb->planes[plane_no].mem_priv)
		return NULL;

	return call_ptr_memop(vb, vaddr, vb->planes[plane_no].mem_priv);

}
EXPORT_SYMBOL_GPL(vb2_plane_vaddr);

void *vb2_plane_cookie(struct vb2_buffer *vb, unsigned int plane_no)
{
	if (plane_no >= vb->num_planes || !vb->planes[plane_no].mem_priv)
		return NULL;

	return call_ptr_memop(vb, cookie, vb->planes[plane_no].mem_priv);
}
EXPORT_SYMBOL_GPL(vb2_plane_cookie);

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

	if (state != VB2_BUF_STATE_QUEUED &&
	    state != VB2_BUF_STATE_REQUEUEING) {
		/* sync buffers */
		for (plane = 0; plane < vb->num_planes; ++plane)
			call_void_memop(vb, finish, vb->planes[plane].mem_priv);
	}

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
 * __prepare_mmap() - prepare an MMAP buffer
 */
static int __prepare_mmap(struct vb2_buffer *vb, const void *pb)
{
	int ret = 0;

	if (pb)
		ret = call_bufop(vb->vb2_queue, fill_vb2_buffer,
				 vb, pb, vb->planes);
	return ret ? ret : call_vb_qop(vb, buf_prepare, vb);
}

/**
 * __prepare_userptr() - prepare a USERPTR buffer
 */
static int __prepare_userptr(struct vb2_buffer *vb, const void *pb)
{
	struct vb2_plane planes[VB2_MAX_PLANES];
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	unsigned int plane;
	int ret = 0;
	bool reacquired = vb->planes[0].mem_priv == NULL;

	memset(planes, 0, sizeof(planes[0]) * vb->num_planes);
	/* Copy relevant information provided by the userspace */
	if (pb) {
		ret = call_bufop(vb->vb2_queue, fill_vb2_buffer,
				 vb, pb, planes);
		if (ret)
			return ret;
	}

	for (plane = 0; plane < vb->num_planes; ++plane) {
		/* Skip the plane if already verified */
		if (vb->planes[plane].m.userptr &&
			vb->planes[plane].m.userptr == planes[plane].m.userptr
			&& vb->planes[plane].length == planes[plane].length)
			continue;

		dprintk(3, "userspace address for plane %d changed, reacquiring memory\n",
			plane);

		/* Check if the provided plane buffer is large enough */
		if (planes[plane].length < vb->planes[plane].min_length) {
			dprintk(1, "provided buffer size %u is less than setup size %u for plane %d\n",
						planes[plane].length,
						vb->planes[plane].min_length,
						plane);
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
		mem_priv = call_ptr_memop(vb, get_userptr,
				q->alloc_devs[plane] ? : q->dev,
				planes[plane].m.userptr,
				planes[plane].length, q->dma_dir);
		if (IS_ERR(mem_priv)) {
			dprintk(1, "failed acquiring userspace memory for plane %d\n",
				plane);
			ret = PTR_ERR(mem_priv);
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
 * __prepare_dmabuf() - prepare a DMABUF buffer
 */
static int __prepare_dmabuf(struct vb2_buffer *vb, const void *pb)
{
	struct vb2_plane planes[VB2_MAX_PLANES];
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	unsigned int plane;
	int ret = 0;
	bool reacquired = vb->planes[0].mem_priv == NULL;

	memset(planes, 0, sizeof(planes[0]) * vb->num_planes);
	/* Copy relevant information provided by the userspace */
	if (pb) {
		ret = call_bufop(vb->vb2_queue, fill_vb2_buffer,
				 vb, pb, planes);
		if (ret)
			return ret;
	}

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

		if (planes[plane].length < vb->planes[plane].min_length) {
			dprintk(1, "invalid dmabuf length %u for plane %d, minimum length %u\n",
				planes[plane].length, plane,
				vb->planes[plane].min_length);
			dma_buf_put(dbuf);
			ret = -EINVAL;
			goto err;
		}

		/* Skip the plane if already verified */
		if (dbuf == vb->planes[plane].dbuf &&
			vb->planes[plane].length == planes[plane].length) {
			dma_buf_put(dbuf);
			continue;
		}

		dprintk(3, "buffer for plane %d changed\n", plane);

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
				q->alloc_devs[plane] ? : q->dev,
				dbuf, planes[plane].length, q->dma_dir);
		if (IS_ERR(mem_priv)) {
			dprintk(1, "failed to attach dmabuf\n");
			ret = PTR_ERR(mem_priv);
			dma_buf_put(dbuf);
			goto err;
		}

		vb->planes[plane].dbuf = dbuf;
		vb->planes[plane].mem_priv = mem_priv;
	}

	/*
	 * This pins the buffer(s) with dma_buf_map_attachment()). It's done
	 * here instead just before the DMA, while queueing the buffer(s) so
	 * userspace knows sooner rather than later if the dma-buf map fails.
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

	vb->state = VB2_BUF_STATE_ACTIVE;
	atomic_inc(&q->owned_by_drv_count);

	trace_vb2_buf_queue(q, vb);

	call_void_vb_qop(vb, buf_queue, vb);
}

static int __buf_prepare(struct vb2_buffer *vb, const void *pb)
{
	struct vb2_queue *q = vb->vb2_queue;
	unsigned int plane;
	int ret;

	if (q->error) {
		dprintk(1, "fatal error occurred on queue\n");
		return -EIO;
	}

	vb->state = VB2_BUF_STATE_PREPARING;

	switch (q->memory) {
	case VB2_MEMORY_MMAP:
		ret = __prepare_mmap(vb, pb);
		break;
	case VB2_MEMORY_USERPTR:
		ret = __prepare_userptr(vb, pb);
		break;
	case VB2_MEMORY_DMABUF:
		ret = __prepare_dmabuf(vb, pb);
		break;
	default:
		WARN(1, "Invalid queue type\n");
		ret = -EINVAL;
	}

	if (ret) {
		dprintk(1, "buffer preparation failed: %d\n", ret);
		vb->state = VB2_BUF_STATE_DEQUEUED;
		return ret;
	}

	/* sync buffers */
	for (plane = 0; plane < vb->num_planes; ++plane)
		call_void_memop(vb, prepare, vb->planes[plane].mem_priv);

	vb->state = VB2_BUF_STATE_PREPARED;

	return 0;
}

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
	call_void_bufop(q, fill_user_buffer, vb, pb);

	dprintk(2, "prepare of buffer %d succeeded\n", vb->index);

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

	if (pb)
		call_void_bufop(q, copy_timestamp, vb, pb);

	trace_vb2_qbuf(q, vb);

	/*
	 * If already streaming, give the buffer to driver for processing.
	 * If not, the buffer will be given to driver on next streamon.
	 */
	if (q->start_streaming_called)
		__enqueue_in_driver(vb);

	/* Fill buffer information for the userspace */
	if (pb)
		call_void_bufop(q, fill_user_buffer, vb, pb);

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

	dprintk(2, "qbuf of buffer %d succeeded\n", vb->index);
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
			dprintk(3, "nonblocking and no buffers to dequeue, will not wait\n");
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

int vb2_core_dqbuf(struct vb2_queue *q, unsigned int *pindex, void *pb,
		   bool nonblocking)
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

	if (pindex)
		*pindex = vb->index;

	/* Fill buffer information for the userspace */
	if (pb)
		call_void_bufop(q, fill_user_buffer, vb, pb);

	/* Remove from videobuf queue */
	list_del(&vb->queued_entry);
	q->queued_count--;

	trace_vb2_dqbuf(q, vb);

	/* go back to dequeued state */
	__vb2_dqbuf(vb);

	dprintk(2, "dqbuf of buffer %d, with state %d\n",
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
	 * __vb2_dqbuf() since in vb2_core_dqbuf() there is a critical
	 * call to __fill_user_buffer() after buf_finish(). That order can't
	 * be changed, so we can't move the buf_finish() to __vb2_dqbuf().
	 */
	for (i = 0; i < q->num_buffers; ++i) {
		struct vb2_buffer *vb = q->bufs[i];

		if (vb->state == VB2_BUF_STATE_PREPARED ||
		    vb->state == VB2_BUF_STATE_QUEUED) {
			unsigned int plane;

			for (plane = 0; plane < vb->num_planes; ++plane)
				call_void_memop(vb, finish,
						vb->planes[plane].mem_priv);
		}

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
		ret = v4l_vb2q_enable_media_source(q);
		if (ret)
			return ret;
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

	if (q->bidirectional)
		q->dma_dir = DMA_BIDIRECTIONAL;
	else
		q->dma_dir = q->is_output ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_queue_init);

static int __vb2_init_fileio(struct vb2_queue *q, int read);
static int __vb2_cleanup_fileio(struct vb2_queue *q);
void vb2_core_queue_release(struct vb2_queue *q)
{
	__vb2_cleanup_fileio(q);
	__vb2_queue_cancel(q);
	mutex_lock(&q->mmap_lock);
	__vb2_queue_free(q, q->num_buffers);
	mutex_unlock(&q->mmap_lock);
}
EXPORT_SYMBOL_GPL(vb2_core_queue_release);

unsigned int vb2_core_poll(struct vb2_queue *q, struct file *file,
		poll_table *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct vb2_buffer *vb = NULL;
	unsigned long flags;

	if (!q->is_output && !(req_events & (POLLIN | POLLRDNORM)))
		return 0;
	if (q->is_output && !(req_events & (POLLOUT | POLLWRNORM)))
		return 0;

	/*
	 * Start file I/O emulator only if streaming API has not been used yet.
	 */
	if (q->num_buffers == 0 && !vb2_fileio_is_active(q)) {
		if (!q->is_output && (q->io_modes & VB2_READ) &&
				(req_events & (POLLIN | POLLRDNORM))) {
			if (__vb2_init_fileio(q, 1))
				return POLLERR;
		}
		if (q->is_output && (q->io_modes & VB2_WRITE) &&
				(req_events & (POLLOUT | POLLWRNORM))) {
			if (__vb2_init_fileio(q, 0))
				return POLLERR;
			/*
			 * Write to OUTPUT queue can be done immediately.
			 */
			return POLLOUT | POLLWRNORM;
		}
	}

	/*
	 * There is nothing to wait for if the queue isn't streaming, or if the
	 * error flag is set.
	 */
	if (!vb2_is_streaming(q) || q->error)
		return POLLERR;

	/*
	 * If this quirk is set and QBUF hasn't been called yet then
	 * return POLLERR as well. This only affects capture queues, output
	 * queues will always initialize waiting_for_buffers to false.
	 * This quirk is set by V4L2 for backwards compatibility reasons.
	 */
	if (q->quirk_poll_must_check_waiting_for_buffers &&
	    q->waiting_for_buffers && (req_events & (POLLIN | POLLRDNORM)))
		return POLLERR;

	/*
	 * For output streams you can call write() as long as there are fewer
	 * buffers queued than there are buffers available.
	 */
	if (q->is_output && q->fileio && q->queued_count < q->num_buffers)
		return POLLOUT | POLLWRNORM;

	if (list_empty(&q->done_list)) {
		/*
		 * If the last buffer was dequeued from a capture queue,
		 * return immediately. DQBUF will return -EPIPE.
		 */
		if (q->last_buffer_dequeued)
			return POLLIN | POLLRDNORM;

		poll_wait(file, &q->done_wq, wait);
	}

	/*
	 * Take first buffer available for dequeuing.
	 */
	spin_lock_irqsave(&q->done_lock, flags);
	if (!list_empty(&q->done_list))
		vb = list_first_entry(&q->done_list, struct vb2_buffer,
					done_entry);
	spin_unlock_irqrestore(&q->done_lock, flags);

	if (vb && (vb->state == VB2_BUF_STATE_DONE
			|| vb->state == VB2_BUF_STATE_ERROR)) {
		return (q->is_output) ?
				POLLOUT | POLLWRNORM :
				POLLIN | POLLRDNORM;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_core_poll);

/**
 * struct vb2_fileio_buf - buffer context used by file io emulator
 *
 * vb2 provides a compatibility layer and emulator of file io (read and
 * write) calls on top of streaming API. This structure is used for
 * tracking context related to the buffers.
 */
struct vb2_fileio_buf {
	void *vaddr;
	unsigned int size;
	unsigned int pos;
	unsigned int queued:1;
};

/**
 * struct vb2_fileio_data - queue context used by file io emulator
 *
 * @cur_index:	the index of the buffer currently being read from or
 *		written to. If equal to q->num_buffers then a new buffer
 *		must be dequeued.
 * @initial_index: in the read() case all buffers are queued up immediately
 *		in __vb2_init_fileio() and __vb2_perform_fileio() just cycles
 *		buffers. However, in the write() case no buffers are initially
 *		queued, instead whenever a buffer is full it is queued up by
 *		__vb2_perform_fileio(). Only once all available buffers have
 *		been queued up will __vb2_perform_fileio() start to dequeue
 *		buffers. This means that initially __vb2_perform_fileio()
 *		needs to know what buffer index to use when it is queuing up
 *		the buffers for the first time. That initial index is stored
 *		in this field. Once it is equal to q->num_buffers all
 *		available buffers have been queued and __vb2_perform_fileio()
 *		should start the normal dequeue/queue cycle.
 *
 * vb2 provides a compatibility layer and emulator of file io (read and
 * write) calls on top of streaming API. For proper operation it required
 * this structure to save the driver state between each call of the read
 * or write function.
 */
struct vb2_fileio_data {
	unsigned int count;
	unsigned int type;
	unsigned int memory;
	struct vb2_fileio_buf bufs[VB2_MAX_FRAME];
	unsigned int cur_index;
	unsigned int initial_index;
	unsigned int q_count;
	unsigned int dq_count;
	unsigned read_once:1;
	unsigned write_immediately:1;
};

/**
 * __vb2_init_fileio() - initialize file io emulator
 * @q:		videobuf2 queue
 * @read:	mode selector (1 means read, 0 means write)
 */
static int __vb2_init_fileio(struct vb2_queue *q, int read)
{
	struct vb2_fileio_data *fileio;
	int i, ret;
	unsigned int count = 0;

	/*
	 * Sanity check
	 */
	if (WARN_ON((read && !(q->io_modes & VB2_READ)) ||
		    (!read && !(q->io_modes & VB2_WRITE))))
		return -EINVAL;

	/*
	 * Check if device supports mapping buffers to kernel virtual space.
	 */
	if (!q->mem_ops->vaddr)
		return -EBUSY;

	/*
	 * Check if streaming api has not been already activated.
	 */
	if (q->streaming || q->num_buffers > 0)
		return -EBUSY;

	/*
	 * Start with count 1, driver can increase it in queue_setup()
	 */
	count = 1;

	dprintk(3, "setting up file io: mode %s, count %d, read_once %d, write_immediately %d\n",
		(read) ? "read" : "write", count, q->fileio_read_once,
		q->fileio_write_immediately);

	fileio = kzalloc(sizeof(*fileio), GFP_KERNEL);
	if (fileio == NULL)
		return -ENOMEM;

	fileio->read_once = q->fileio_read_once;
	fileio->write_immediately = q->fileio_write_immediately;

	/*
	 * Request buffers and use MMAP type to force driver
	 * to allocate buffers by itself.
	 */
	fileio->count = count;
	fileio->memory = VB2_MEMORY_MMAP;
	fileio->type = q->type;
	q->fileio = fileio;
	ret = vb2_core_reqbufs(q, fileio->memory, &fileio->count);
	if (ret)
		goto err_kfree;

	/*
	 * Check if plane_count is correct
	 * (multiplane buffers are not supported).
	 */
	if (q->bufs[0]->num_planes != 1) {
		ret = -EBUSY;
		goto err_reqbufs;
	}

	/*
	 * Get kernel address of each buffer.
	 */
	for (i = 0; i < q->num_buffers; i++) {
		fileio->bufs[i].vaddr = vb2_plane_vaddr(q->bufs[i], 0);
		if (fileio->bufs[i].vaddr == NULL) {
			ret = -EINVAL;
			goto err_reqbufs;
		}
		fileio->bufs[i].size = vb2_plane_size(q->bufs[i], 0);
	}

	/*
	 * Read mode requires pre queuing of all buffers.
	 */
	if (read) {
		/*
		 * Queue all buffers.
		 */
		for (i = 0; i < q->num_buffers; i++) {
			ret = vb2_core_qbuf(q, i, NULL);
			if (ret)
				goto err_reqbufs;
			fileio->bufs[i].queued = 1;
		}
		/*
		 * All buffers have been queued, so mark that by setting
		 * initial_index to q->num_buffers
		 */
		fileio->initial_index = q->num_buffers;
		fileio->cur_index = q->num_buffers;
	}

	/*
	 * Start streaming.
	 */
	ret = vb2_core_streamon(q, q->type);
	if (ret)
		goto err_reqbufs;

	return ret;

err_reqbufs:
	fileio->count = 0;
	vb2_core_reqbufs(q, fileio->memory, &fileio->count);

err_kfree:
	q->fileio = NULL;
	kfree(fileio);
	return ret;
}

/**
 * __vb2_cleanup_fileio() - free resourced used by file io emulator
 * @q:		videobuf2 queue
 */
static int __vb2_cleanup_fileio(struct vb2_queue *q)
{
	struct vb2_fileio_data *fileio = q->fileio;

	if (fileio) {
		vb2_core_streamoff(q, q->type);
		q->fileio = NULL;
		fileio->count = 0;
		vb2_core_reqbufs(q, fileio->memory, &fileio->count);
		kfree(fileio);
		dprintk(3, "file io emulator closed\n");
	}
	return 0;
}

/**
 * __vb2_perform_fileio() - perform a single file io (read or write) operation
 * @q:		videobuf2 queue
 * @data:	pointed to target userspace buffer
 * @count:	number of bytes to read or write
 * @ppos:	file handle position tracking pointer
 * @nonblock:	mode selector (1 means blocking calls, 0 means nonblocking)
 * @read:	access mode selector (1 means read, 0 means write)
 */
static size_t __vb2_perform_fileio(struct vb2_queue *q, char __user *data, size_t count,
		loff_t *ppos, int nonblock, int read)
{
	struct vb2_fileio_data *fileio;
	struct vb2_fileio_buf *buf;
	bool is_multiplanar = q->is_multiplanar;
	/*
	 * When using write() to write data to an output video node the vb2 core
	 * should copy timestamps if V4L2_BUF_FLAG_TIMESTAMP_COPY is set. Nobody
	 * else is able to provide this information with the write() operation.
	 */
	bool copy_timestamp = !read && q->copy_timestamp;
	unsigned index;
	int ret;

	dprintk(3, "mode %s, offset %ld, count %zd, %sblocking\n",
		read ? "read" : "write", (long)*ppos, count,
		nonblock ? "non" : "");

	if (!data)
		return -EINVAL;

	/*
	 * Initialize emulator on first call.
	 */
	if (!vb2_fileio_is_active(q)) {
		ret = __vb2_init_fileio(q, read);
		dprintk(3, "vb2_init_fileio result: %d\n", ret);
		if (ret)
			return ret;
	}
	fileio = q->fileio;

	/*
	 * Check if we need to dequeue the buffer.
	 */
	index = fileio->cur_index;
	if (index >= q->num_buffers) {
		struct vb2_buffer *b;

		/*
		 * Call vb2_dqbuf to get buffer back.
		 */
		ret = vb2_core_dqbuf(q, &index, NULL, nonblock);
		dprintk(5, "vb2_dqbuf result: %d\n", ret);
		if (ret)
			return ret;
		fileio->dq_count += 1;

		fileio->cur_index = index;
		buf = &fileio->bufs[index];
		b = q->bufs[index];

		/*
		 * Get number of bytes filled by the driver
		 */
		buf->pos = 0;
		buf->queued = 0;
		buf->size = read ? vb2_get_plane_payload(q->bufs[index], 0)
				 : vb2_plane_size(q->bufs[index], 0);
		/* Compensate for data_offset on read in the multiplanar case. */
		if (is_multiplanar && read &&
				b->planes[0].data_offset < buf->size) {
			buf->pos = b->planes[0].data_offset;
			buf->size -= buf->pos;
		}
	} else {
		buf = &fileio->bufs[index];
	}

	/*
	 * Limit count on last few bytes of the buffer.
	 */
	if (buf->pos + count > buf->size) {
		count = buf->size - buf->pos;
		dprintk(5, "reducing read count: %zd\n", count);
	}

	/*
	 * Transfer data to userspace.
	 */
	dprintk(3, "copying %zd bytes - buffer %d, offset %u\n",
		count, index, buf->pos);
	if (read)
		ret = copy_to_user(data, buf->vaddr + buf->pos, count);
	else
		ret = copy_from_user(buf->vaddr + buf->pos, data, count);
	if (ret) {
		dprintk(3, "error copying data\n");
		return -EFAULT;
	}

	/*
	 * Update counters.
	 */
	buf->pos += count;
	*ppos += count;

	/*
	 * Queue next buffer if required.
	 */
	if (buf->pos == buf->size || (!read && fileio->write_immediately)) {
		struct vb2_buffer *b = q->bufs[index];

		/*
		 * Check if this is the last buffer to read.
		 */
		if (read && fileio->read_once && fileio->dq_count == 1) {
			dprintk(3, "read limit reached\n");
			return __vb2_cleanup_fileio(q);
		}

		/*
		 * Call vb2_qbuf and give buffer to the driver.
		 */
		b->planes[0].bytesused = buf->pos;

		if (copy_timestamp)
			b->timestamp = ktime_get_ns();
		ret = vb2_core_qbuf(q, index, NULL);
		dprintk(5, "vb2_dbuf result: %d\n", ret);
		if (ret)
			return ret;

		/*
		 * Buffer has been queued, update the status
		 */
		buf->pos = 0;
		buf->queued = 1;
		buf->size = vb2_plane_size(q->bufs[index], 0);
		fileio->q_count += 1;
		/*
		 * If we are queuing up buffers for the first time, then
		 * increase initial_index by one.
		 */
		if (fileio->initial_index < q->num_buffers)
			fileio->initial_index++;
		/*
		 * The next buffer to use is either a buffer that's going to be
		 * queued for the first time (initial_index < q->num_buffers)
		 * or it is equal to q->num_buffers, meaning that the next
		 * time we need to dequeue a buffer since we've now queued up
		 * all the 'first time' buffers.
		 */
		fileio->cur_index = fileio->initial_index;
	}

	/*
	 * Return proper number of bytes processed.
	 */
	if (ret == 0)
		ret = count;
	return ret;
}

size_t vb2_read(struct vb2_queue *q, char __user *data, size_t count,
		loff_t *ppos, int nonblocking)
{
	return __vb2_perform_fileio(q, data, count, ppos, nonblocking, 1);
}
EXPORT_SYMBOL_GPL(vb2_read);

size_t vb2_write(struct vb2_queue *q, const char __user *data, size_t count,
		loff_t *ppos, int nonblocking)
{
	return __vb2_perform_fileio(q, (char __user *) data, count,
							ppos, nonblocking, 0);
}
EXPORT_SYMBOL_GPL(vb2_write);

struct vb2_threadio_data {
	struct task_struct *thread;
	vb2_thread_fnc fnc;
	void *priv;
	bool stop;
};

static int vb2_thread(void *data)
{
	struct vb2_queue *q = data;
	struct vb2_threadio_data *threadio = q->threadio;
	bool copy_timestamp = false;
	unsigned prequeue = 0;
	unsigned index = 0;
	int ret = 0;

	if (q->is_output) {
		prequeue = q->num_buffers;
		copy_timestamp = q->copy_timestamp;
	}

	set_freezable();

	for (;;) {
		struct vb2_buffer *vb;

		/*
		 * Call vb2_dqbuf to get buffer back.
		 */
		if (prequeue) {
			vb = q->bufs[index++];
			prequeue--;
		} else {
			call_void_qop(q, wait_finish, q);
			if (!threadio->stop)
				ret = vb2_core_dqbuf(q, &index, NULL, 0);
			call_void_qop(q, wait_prepare, q);
			dprintk(5, "file io: vb2_dqbuf result: %d\n", ret);
			if (!ret)
				vb = q->bufs[index];
		}
		if (ret || threadio->stop)
			break;
		try_to_freeze();

		if (vb->state != VB2_BUF_STATE_ERROR)
			if (threadio->fnc(vb, threadio->priv))
				break;
		call_void_qop(q, wait_finish, q);
		if (copy_timestamp)
			vb->timestamp = ktime_get_ns();;
		if (!threadio->stop)
			ret = vb2_core_qbuf(q, vb->index, NULL);
		call_void_qop(q, wait_prepare, q);
		if (ret || threadio->stop)
			break;
	}

	/* Hmm, linux becomes *very* unhappy without this ... */
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return 0;
}

/*
 * This function should not be used for anything else but the videobuf2-dvb
 * support. If you think you have another good use-case for this, then please
 * contact the linux-media mailinglist first.
 */
int vb2_thread_start(struct vb2_queue *q, vb2_thread_fnc fnc, void *priv,
		     const char *thread_name)
{
	struct vb2_threadio_data *threadio;
	int ret = 0;

	if (q->threadio)
		return -EBUSY;
	if (vb2_is_busy(q))
		return -EBUSY;
	if (WARN_ON(q->fileio))
		return -EBUSY;

	threadio = kzalloc(sizeof(*threadio), GFP_KERNEL);
	if (threadio == NULL)
		return -ENOMEM;
	threadio->fnc = fnc;
	threadio->priv = priv;

	ret = __vb2_init_fileio(q, !q->is_output);
	dprintk(3, "file io: vb2_init_fileio result: %d\n", ret);
	if (ret)
		goto nomem;
	q->threadio = threadio;
	threadio->thread = kthread_run(vb2_thread, q, "vb2-%s", thread_name);
	if (IS_ERR(threadio->thread)) {
		ret = PTR_ERR(threadio->thread);
		threadio->thread = NULL;
		goto nothread;
	}
	return 0;

nothread:
	__vb2_cleanup_fileio(q);
nomem:
	kfree(threadio);
	return ret;
}
EXPORT_SYMBOL_GPL(vb2_thread_start);

int vb2_thread_stop(struct vb2_queue *q)
{
	struct vb2_threadio_data *threadio = q->threadio;
	int err;

	if (threadio == NULL)
		return 0;
	threadio->stop = true;
	/* Wake up all pending sleeps in the thread */
	vb2_queue_error(q);
	err = kthread_stop(threadio->thread);
	__vb2_cleanup_fileio(q);
	threadio->thread = NULL;
	kfree(threadio);
	q->threadio = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(vb2_thread_stop);

MODULE_DESCRIPTION("Media buffer core framework");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>, Marek Szyprowski");
MODULE_LICENSE("GPL");
