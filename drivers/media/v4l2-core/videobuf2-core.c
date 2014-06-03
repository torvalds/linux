/*
 * videobuf2-core.c - V4L2 driver helper framework
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

#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-core.h>

static int debug;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)					      \
	do {								      \
		if (debug >= level)					      \
			pr_debug("vb2: %s: " fmt, __func__, ## arg); \
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
		(vb)->vb2_queue, (vb)->v4l2_buf.index, #op,		\
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
		(vb)->vb2_queue, (vb)->v4l2_buf.index, #op,		\
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

/* Flags that are set by the vb2 core */
#define V4L2_BUFFER_MASK_FLAGS	(V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED | \
				 V4L2_BUF_FLAG_DONE | V4L2_BUF_FLAG_ERROR | \
				 V4L2_BUF_FLAG_PREPARED | \
				 V4L2_BUF_FLAG_TIMESTAMP_MASK)
/* Output buffer flags that should be passed on to the driver */
#define V4L2_BUFFER_OUT_FLAGS	(V4L2_BUF_FLAG_PFRAME | V4L2_BUF_FLAG_BFRAME | \
				 V4L2_BUF_FLAG_KEYFRAME | V4L2_BUF_FLAG_TIMECODE)

static void __vb2_queue_cancel(struct vb2_queue *q);

/**
 * __vb2_buf_mem_alloc() - allocate video memory for the given buffer
 */
static int __vb2_buf_mem_alloc(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	int plane;

	/*
	 * Allocate memory for all planes in this buffer
	 * NOTE: mmapped areas should be page aligned
	 */
	for (plane = 0; plane < vb->num_planes; ++plane) {
		unsigned long size = PAGE_ALIGN(q->plane_sizes[plane]);

		mem_priv = call_ptr_memop(vb, alloc, q->alloc_ctx[plane],
				      size, q->gfp_flags);
		if (IS_ERR_OR_NULL(mem_priv))
			goto free;

		/* Associate allocator private data with this plane */
		vb->planes[plane].mem_priv = mem_priv;
		vb->v4l2_planes[plane].length = q->plane_sizes[plane];
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
		dprintk(3, "freed plane %d of buffer %d\n", plane,
			vb->v4l2_buf.index);
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
	memset(p, 0, sizeof(*p));
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
			vb->v4l2_planes[plane].length = q->plane_sizes[plane];
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
		struct v4l2_plane *p;
		vb = q->bufs[q->num_buffers - 1];
		p = &vb->v4l2_planes[vb->num_planes - 1];
		off = PAGE_ALIGN(p->m.mem_offset + p->length);
	} else {
		off = 0;
	}

	for (buffer = q->num_buffers; buffer < q->num_buffers + n; ++buffer) {
		vb = q->bufs[buffer];
		if (!vb)
			continue;

		for (plane = 0; plane < vb->num_planes; ++plane) {
			vb->v4l2_planes[plane].m.mem_offset = off;

			dprintk(3, "buffer %d, plane %d offset 0x%08lx\n",
					buffer, plane, off);

			off += vb->v4l2_planes[plane].length;
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
static int __vb2_queue_alloc(struct vb2_queue *q, enum v4l2_memory memory,
			     unsigned int num_buffers, unsigned int num_planes)
{
	unsigned int buffer;
	struct vb2_buffer *vb;
	int ret;

	for (buffer = 0; buffer < num_buffers; ++buffer) {
		/* Allocate videobuf buffer structures */
		vb = kzalloc(q->buf_struct_size, GFP_KERNEL);
		if (!vb) {
			dprintk(1, "memory alloc for buffer struct failed\n");
			break;
		}

		/* Length stores number of planes for multiplanar buffers */
		if (V4L2_TYPE_IS_MULTIPLANAR(q->type))
			vb->v4l2_buf.length = num_planes;

		vb->state = VB2_BUF_STATE_DEQUEUED;
		vb->vb2_queue = q;
		vb->num_planes = num_planes;
		vb->v4l2_buf.index = q->num_buffers + buffer;
		vb->v4l2_buf.type = q->type;
		vb->v4l2_buf.memory = memory;

		/* Allocate video buffer memory for the MMAP type */
		if (memory == V4L2_MEMORY_MMAP) {
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
	if (memory == V4L2_MEMORY_MMAP)
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
		if (q->memory == V4L2_MEMORY_MMAP)
			__vb2_buf_mem_free(vb);
		else if (q->memory == V4L2_MEMORY_DMABUF)
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

/**
 * __verify_planes_array() - verify that the planes array passed in struct
 * v4l2_buffer from userspace can be safely used
 */
static int __verify_planes_array(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type))
		return 0;

	/* Is memory for copying plane information present? */
	if (NULL == b->m.planes) {
		dprintk(1, "multi-planar buffer passed but "
			   "planes array not provided\n");
		return -EINVAL;
	}

	if (b->length < vb->num_planes || b->length > VIDEO_MAX_PLANES) {
		dprintk(1, "incorrect planes array length, "
			   "expected %d, got %d\n", vb->num_planes, b->length);
		return -EINVAL;
	}

	return 0;
}

/**
 * __verify_length() - Verify that the bytesused value for each plane fits in
 * the plane length and that the data offset doesn't exceed the bytesused value.
 */
static int __verify_length(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	unsigned int length;
	unsigned int plane;

	if (!V4L2_TYPE_IS_OUTPUT(b->type))
		return 0;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		for (plane = 0; plane < vb->num_planes; ++plane) {
			length = (b->memory == V4L2_MEMORY_USERPTR)
			       ? b->m.planes[plane].length
			       : vb->v4l2_planes[plane].length;

			if (b->m.planes[plane].bytesused > length)
				return -EINVAL;

			if (b->m.planes[plane].data_offset > 0 &&
			    b->m.planes[plane].data_offset >=
			    b->m.planes[plane].bytesused)
				return -EINVAL;
		}
	} else {
		length = (b->memory == V4L2_MEMORY_USERPTR)
		       ? b->length : vb->v4l2_planes[0].length;

		if (b->bytesused > length)
			return -EINVAL;
	}

	return 0;
}

/**
 * __buffer_in_use() - return true if the buffer is in use and
 * the queue cannot be freed (by the means of REQBUFS(0)) call
 */
static bool __buffer_in_use(struct vb2_queue *q, struct vb2_buffer *vb)
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

/**
 * __buffers_in_use() - return true if any buffers on the queue are in use and
 * the queue cannot be freed (by the means of REQBUFS(0)) call
 */
static bool __buffers_in_use(struct vb2_queue *q)
{
	unsigned int buffer;
	for (buffer = 0; buffer < q->num_buffers; ++buffer) {
		if (__buffer_in_use(q, q->bufs[buffer]))
			return true;
	}
	return false;
}

/**
 * __fill_v4l2_buffer() - fill in a struct v4l2_buffer with information to be
 * returned to userspace
 */
static void __fill_v4l2_buffer(struct vb2_buffer *vb, struct v4l2_buffer *b)
{
	struct vb2_queue *q = vb->vb2_queue;

	/* Copy back data such as timestamp, flags, etc. */
	memcpy(b, &vb->v4l2_buf, offsetof(struct v4l2_buffer, m));
	b->reserved2 = vb->v4l2_buf.reserved2;
	b->reserved = vb->v4l2_buf.reserved;

	if (V4L2_TYPE_IS_MULTIPLANAR(q->type)) {
		/*
		 * Fill in plane-related data if userspace provided an array
		 * for it. The caller has already verified memory and size.
		 */
		b->length = vb->num_planes;
		memcpy(b->m.planes, vb->v4l2_planes,
			b->length * sizeof(struct v4l2_plane));
	} else {
		/*
		 * We use length and offset in v4l2_planes array even for
		 * single-planar buffers, but userspace does not.
		 */
		b->length = vb->v4l2_planes[0].length;
		b->bytesused = vb->v4l2_planes[0].bytesused;
		if (q->memory == V4L2_MEMORY_MMAP)
			b->m.offset = vb->v4l2_planes[0].m.mem_offset;
		else if (q->memory == V4L2_MEMORY_USERPTR)
			b->m.userptr = vb->v4l2_planes[0].m.userptr;
		else if (q->memory == V4L2_MEMORY_DMABUF)
			b->m.fd = vb->v4l2_planes[0].m.fd;
	}

	/*
	 * Clear any buffer state related flags.
	 */
	b->flags &= ~V4L2_BUFFER_MASK_FLAGS;
	b->flags |= q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
	if ((q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) !=
	    V4L2_BUF_FLAG_TIMESTAMP_COPY) {
		/*
		 * For non-COPY timestamps, drop timestamp source bits
		 * and obtain the timestamp source from the queue.
		 */
		b->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		b->flags |= q->timestamp_flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	}

	switch (vb->state) {
	case VB2_BUF_STATE_QUEUED:
	case VB2_BUF_STATE_ACTIVE:
		b->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case VB2_BUF_STATE_ERROR:
		b->flags |= V4L2_BUF_FLAG_ERROR;
		/* fall through */
	case VB2_BUF_STATE_DONE:
		b->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case VB2_BUF_STATE_PREPARED:
		b->flags |= V4L2_BUF_FLAG_PREPARED;
		break;
	case VB2_BUF_STATE_PREPARING:
	case VB2_BUF_STATE_DEQUEUED:
		/* nothing */
		break;
	}

	if (__buffer_in_use(q, vb))
		b->flags |= V4L2_BUF_FLAG_MAPPED;
}

/**
 * vb2_querybuf() - query video buffer information
 * @q:		videobuf queue
 * @b:		buffer struct passed from userspace to vidioc_querybuf handler
 *		in driver
 *
 * Should be called from vidioc_querybuf ioctl handler in driver.
 * This function will verify the passed v4l2_buffer structure and fill the
 * relevant information for the userspace.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_querybuf handler in driver.
 */
int vb2_querybuf(struct vb2_queue *q, struct v4l2_buffer *b)
{
	struct vb2_buffer *vb;
	int ret;

	if (b->type != q->type) {
		dprintk(1, "wrong buffer type\n");
		return -EINVAL;
	}

	if (b->index >= q->num_buffers) {
		dprintk(1, "buffer index out of range\n");
		return -EINVAL;
	}
	vb = q->bufs[b->index];
	ret = __verify_planes_array(vb, b);
	if (!ret)
		__fill_v4l2_buffer(vb, b);
	return ret;
}
EXPORT_SYMBOL(vb2_querybuf);

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
 * __verify_memory_type() - Check whether the memory type and buffer type
 * passed to a buffer operation are compatible with the queue.
 */
static int __verify_memory_type(struct vb2_queue *q,
		enum v4l2_memory memory, enum v4l2_buf_type type)
{
	if (memory != V4L2_MEMORY_MMAP && memory != V4L2_MEMORY_USERPTR &&
	    memory != V4L2_MEMORY_DMABUF) {
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
	if (memory == V4L2_MEMORY_MMAP && __verify_mmap_ops(q)) {
		dprintk(1, "MMAP for current setup unsupported\n");
		return -EINVAL;
	}

	if (memory == V4L2_MEMORY_USERPTR && __verify_userptr_ops(q)) {
		dprintk(1, "USERPTR for current setup unsupported\n");
		return -EINVAL;
	}

	if (memory == V4L2_MEMORY_DMABUF && __verify_dmabuf_ops(q)) {
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

/**
 * __reqbufs() - Initiate streaming
 * @q:		videobuf2 queue
 * @req:	struct passed from userspace to vidioc_reqbufs handler in driver
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
static int __reqbufs(struct vb2_queue *q, struct v4l2_requestbuffers *req)
{
	unsigned int num_buffers, allocated_buffers, num_planes = 0;
	int ret;

	if (q->streaming) {
		dprintk(1, "streaming active\n");
		return -EBUSY;
	}

	if (req->count == 0 || q->num_buffers != 0 || q->memory != req->memory) {
		/*
		 * We already have buffers allocated, so first check if they
		 * are not in use and can be freed.
		 */
		if (q->memory == V4L2_MEMORY_MMAP && __buffers_in_use(q)) {
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
		if (ret)
			return ret;

		/*
		 * In case of REQBUFS(0) return immediately without calling
		 * driver's queue_setup() callback and allocating resources.
		 */
		if (req->count == 0)
			return 0;
	}

	/*
	 * Make sure the requested values and current defaults are sane.
	 */
	num_buffers = min_t(unsigned int, req->count, VIDEO_MAX_FRAME);
	num_buffers = max_t(unsigned int, num_buffers, q->min_buffers_needed);
	memset(q->plane_sizes, 0, sizeof(q->plane_sizes));
	memset(q->alloc_ctx, 0, sizeof(q->alloc_ctx));
	q->memory = req->memory;

	/*
	 * Ask the driver how many buffers and planes per buffer it requires.
	 * Driver also sets the size and allocator context for each plane.
	 */
	ret = call_qop(q, queue_setup, q, NULL, &num_buffers, &num_planes,
		       q->plane_sizes, q->alloc_ctx);
	if (ret)
		return ret;

	/* Finally, allocate buffers and video memory */
	allocated_buffers = __vb2_queue_alloc(q, req->memory, num_buffers, num_planes);
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

	q->num_buffers = allocated_buffers;

	if (ret < 0) {
		/*
		 * Note: __vb2_queue_free() will subtract 'allocated_buffers'
		 * from q->num_buffers.
		 */
		__vb2_queue_free(q, allocated_buffers);
		return ret;
	}

	/*
	 * Return the number of successfully allocated buffers
	 * to the userspace.
	 */
	req->count = allocated_buffers;

	return 0;
}

/**
 * vb2_reqbufs() - Wrapper for __reqbufs() that also verifies the memory and
 * type values.
 * @q:		videobuf2 queue
 * @req:	struct passed from userspace to vidioc_reqbufs handler in driver
 */
int vb2_reqbufs(struct vb2_queue *q, struct v4l2_requestbuffers *req)
{
	int ret = __verify_memory_type(q, req->memory, req->type);

	return ret ? ret : __reqbufs(q, req);
}
EXPORT_SYMBOL_GPL(vb2_reqbufs);

/**
 * __create_bufs() - Allocate buffers and any required auxiliary structs
 * @q:		videobuf2 queue
 * @create:	creation parameters, passed from userspace to vidioc_create_bufs
 *		handler in driver
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
static int __create_bufs(struct vb2_queue *q, struct v4l2_create_buffers *create)
{
	unsigned int num_planes = 0, num_buffers, allocated_buffers;
	int ret;

	if (q->num_buffers == VIDEO_MAX_FRAME) {
		dprintk(1, "maximum number of buffers already allocated\n");
		return -ENOBUFS;
	}

	if (!q->num_buffers) {
		memset(q->plane_sizes, 0, sizeof(q->plane_sizes));
		memset(q->alloc_ctx, 0, sizeof(q->alloc_ctx));
		q->memory = create->memory;
	}

	num_buffers = min(create->count, VIDEO_MAX_FRAME - q->num_buffers);

	/*
	 * Ask the driver, whether the requested number of buffers, planes per
	 * buffer and their sizes are acceptable
	 */
	ret = call_qop(q, queue_setup, q, &create->format, &num_buffers,
		       &num_planes, q->plane_sizes, q->alloc_ctx);
	if (ret)
		return ret;

	/* Finally, allocate buffers and video memory */
	allocated_buffers = __vb2_queue_alloc(q, create->memory, num_buffers,
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
		ret = call_qop(q, queue_setup, q, &create->format, &num_buffers,
			       &num_planes, q->plane_sizes, q->alloc_ctx);

		if (!ret && allocated_buffers < num_buffers)
			ret = -ENOMEM;

		/*
		 * Either the driver has accepted a smaller number of buffers,
		 * or .queue_setup() returned an error
		 */
	}

	q->num_buffers += allocated_buffers;

	if (ret < 0) {
		/*
		 * Note: __vb2_queue_free() will subtract 'allocated_buffers'
		 * from q->num_buffers.
		 */
		__vb2_queue_free(q, allocated_buffers);
		return -ENOMEM;
	}

	/*
	 * Return the number of successfully allocated buffers
	 * to the userspace.
	 */
	create->count = allocated_buffers;

	return 0;
}

/**
 * vb2_create_bufs() - Wrapper for __create_bufs() that also verifies the
 * memory and type values.
 * @q:		videobuf2 queue
 * @create:	creation parameters, passed from userspace to vidioc_create_bufs
 *		handler in driver
 */
int vb2_create_bufs(struct vb2_queue *q, struct v4l2_create_buffers *create)
{
	int ret = __verify_memory_type(q, create->memory, create->format.type);

	create->index = q->num_buffers;
	if (create->count == 0)
		return ret != -EBUSY ? ret : 0;
	return ret ? ret : __create_bufs(q, create);
}
EXPORT_SYMBOL_GPL(vb2_create_bufs);

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
	if (plane_no > vb->num_planes || !vb->planes[plane_no].mem_priv)
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
	if (plane_no > vb->num_planes || !vb->planes[plane_no].mem_priv)
		return NULL;

	return call_ptr_memop(vb, cookie, vb->planes[plane_no].mem_priv);
}
EXPORT_SYMBOL_GPL(vb2_plane_cookie);

/**
 * vb2_buffer_done() - inform videobuf that an operation on a buffer is finished
 * @vb:		vb2_buffer returned from the driver
 * @state:	either VB2_BUF_STATE_DONE if the operation finished successfully
 *		or VB2_BUF_STATE_ERROR if the operation finished with an error.
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

	if (!q->start_streaming_called) {
		if (WARN_ON(state != VB2_BUF_STATE_QUEUED))
			state = VB2_BUF_STATE_QUEUED;
	} else if (WARN_ON(state != VB2_BUF_STATE_DONE &&
			   state != VB2_BUF_STATE_ERROR)) {
			state = VB2_BUF_STATE_ERROR;
	}

#ifdef CONFIG_VIDEO_ADV_DEBUG
	/*
	 * Although this is not a callback, it still does have to balance
	 * with the buf_queue op. So update this counter manually.
	 */
	vb->cnt_buf_done++;
#endif
	dprintk(4, "done processing on buffer %d, state: %d\n",
			vb->v4l2_buf.index, state);

	/* sync buffers */
	for (plane = 0; plane < vb->num_planes; ++plane)
		call_void_memop(vb, finish, vb->planes[plane].mem_priv);

	/* Add the buffer to the done buffers list */
	spin_lock_irqsave(&q->done_lock, flags);
	vb->state = state;
	if (state != VB2_BUF_STATE_QUEUED)
		list_add_tail(&vb->done_entry, &q->done_list);
	atomic_dec(&q->owned_by_drv_count);
	spin_unlock_irqrestore(&q->done_lock, flags);

	if (state == VB2_BUF_STATE_QUEUED)
		return;

	/* Inform any processes that may be waiting for buffers */
	wake_up(&q->done_wq);
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
 * __fill_vb2_buffer() - fill a vb2_buffer with information provided in a
 * v4l2_buffer by the userspace. The caller has already verified that struct
 * v4l2_buffer has a valid number of planes.
 */
static void __fill_vb2_buffer(struct vb2_buffer *vb, const struct v4l2_buffer *b,
				struct v4l2_plane *v4l2_planes)
{
	unsigned int plane;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		/* Fill in driver-provided information for OUTPUT types */
		if (V4L2_TYPE_IS_OUTPUT(b->type)) {
			bool bytesused_is_used;

			/* Check if bytesused == 0 for all planes */
			for (plane = 0; plane < vb->num_planes; ++plane)
				if (b->m.planes[plane].bytesused)
					break;
			bytesused_is_used = plane < vb->num_planes;

			/*
			 * Will have to go up to b->length when API starts
			 * accepting variable number of planes.
			 *
			 * If bytesused_is_used is false, then fall back to the
			 * full buffer size. In that case userspace clearly
			 * never bothered to set it and it's a safe assumption
			 * that they really meant to use the full plane sizes.
			 */
			for (plane = 0; plane < vb->num_planes; ++plane) {
				struct v4l2_plane *pdst = &v4l2_planes[plane];
				struct v4l2_plane *psrc = &b->m.planes[plane];

				pdst->bytesused = bytesused_is_used ?
					psrc->bytesused : psrc->length;
				pdst->data_offset = psrc->data_offset;
			}
		}

		if (b->memory == V4L2_MEMORY_USERPTR) {
			for (plane = 0; plane < vb->num_planes; ++plane) {
				v4l2_planes[plane].m.userptr =
					b->m.planes[plane].m.userptr;
				v4l2_planes[plane].length =
					b->m.planes[plane].length;
			}
		}
		if (b->memory == V4L2_MEMORY_DMABUF) {
			for (plane = 0; plane < vb->num_planes; ++plane) {
				v4l2_planes[plane].m.fd =
					b->m.planes[plane].m.fd;
				v4l2_planes[plane].length =
					b->m.planes[plane].length;
			}
		}
	} else {
		/*
		 * Single-planar buffers do not use planes array,
		 * so fill in relevant v4l2_buffer struct fields instead.
		 * In videobuf we use our internal V4l2_planes struct for
		 * single-planar buffers as well, for simplicity.
		 *
		 * If bytesused == 0, then fall back to the full buffer size
		 * as that's a sensible default.
		 */
		if (V4L2_TYPE_IS_OUTPUT(b->type))
			v4l2_planes[0].bytesused =
				b->bytesused ? b->bytesused : b->length;
		else
			v4l2_planes[0].bytesused = 0;

		if (b->memory == V4L2_MEMORY_USERPTR) {
			v4l2_planes[0].m.userptr = b->m.userptr;
			v4l2_planes[0].length = b->length;
		}

		if (b->memory == V4L2_MEMORY_DMABUF) {
			v4l2_planes[0].m.fd = b->m.fd;
			v4l2_planes[0].length = b->length;
		}
	}

	/* Zero flags that the vb2 core handles */
	vb->v4l2_buf.flags = b->flags & ~V4L2_BUFFER_MASK_FLAGS;
	if ((vb->vb2_queue->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) !=
	    V4L2_BUF_FLAG_TIMESTAMP_COPY || !V4L2_TYPE_IS_OUTPUT(b->type)) {
		/*
		 * Non-COPY timestamps and non-OUTPUT queues will get
		 * their timestamp and timestamp source flags from the
		 * queue.
		 */
		vb->v4l2_buf.flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	}

	if (V4L2_TYPE_IS_OUTPUT(b->type)) {
		/*
		 * For output buffers mask out the timecode flag:
		 * this will be handled later in vb2_internal_qbuf().
		 * The 'field' is valid metadata for this output buffer
		 * and so that needs to be copied here.
		 */
		vb->v4l2_buf.flags &= ~V4L2_BUF_FLAG_TIMECODE;
		vb->v4l2_buf.field = b->field;
	} else {
		/* Zero any output buffer flags as this is a capture buffer */
		vb->v4l2_buf.flags &= ~V4L2_BUFFER_OUT_FLAGS;
	}
}

/**
 * __qbuf_mmap() - handle qbuf of an MMAP buffer
 */
static int __qbuf_mmap(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	__fill_vb2_buffer(vb, b, vb->v4l2_planes);
	return call_vb_qop(vb, buf_prepare, vb);
}

/**
 * __qbuf_userptr() - handle qbuf of a USERPTR buffer
 */
static int __qbuf_userptr(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	unsigned int plane;
	int ret;
	int write = !V4L2_TYPE_IS_OUTPUT(q->type);
	bool reacquired = vb->planes[0].mem_priv == NULL;

	memset(planes, 0, sizeof(planes[0]) * vb->num_planes);
	/* Copy relevant information provided by the userspace */
	__fill_vb2_buffer(vb, b, planes);

	for (plane = 0; plane < vb->num_planes; ++plane) {
		/* Skip the plane if already verified */
		if (vb->v4l2_planes[plane].m.userptr &&
		    vb->v4l2_planes[plane].m.userptr == planes[plane].m.userptr
		    && vb->v4l2_planes[plane].length == planes[plane].length)
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
		memset(&vb->v4l2_planes[plane], 0, sizeof(struct v4l2_plane));

		/* Acquire each plane's memory */
		mem_priv = call_ptr_memop(vb, get_userptr, q->alloc_ctx[plane],
				      planes[plane].m.userptr,
				      planes[plane].length, write);
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
	for (plane = 0; plane < vb->num_planes; ++plane)
		vb->v4l2_planes[plane] = planes[plane];

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
			call_void_memop(vb, put_userptr, vb->planes[plane].mem_priv);
		vb->planes[plane].mem_priv = NULL;
		vb->v4l2_planes[plane].m.userptr = 0;
		vb->v4l2_planes[plane].length = 0;
	}

	return ret;
}

/**
 * __qbuf_dmabuf() - handle qbuf of a DMABUF buffer
 */
static int __qbuf_dmabuf(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	struct vb2_queue *q = vb->vb2_queue;
	void *mem_priv;
	unsigned int plane;
	int ret;
	int write = !V4L2_TYPE_IS_OUTPUT(q->type);
	bool reacquired = vb->planes[0].mem_priv == NULL;

	memset(planes, 0, sizeof(planes[0]) * vb->num_planes);
	/* Copy relevant information provided by the userspace */
	__fill_vb2_buffer(vb, b, planes);

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
		    vb->v4l2_planes[plane].length == planes[plane].length) {
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
		memset(&vb->v4l2_planes[plane], 0, sizeof(struct v4l2_plane));

		/* Acquire each plane's memory */
		mem_priv = call_ptr_memop(vb, attach_dmabuf, q->alloc_ctx[plane],
			dbuf, planes[plane].length, write);
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
	for (plane = 0; plane < vb->num_planes; ++plane)
		vb->v4l2_planes[plane] = planes[plane];

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

	/* sync buffers */
	for (plane = 0; plane < vb->num_planes; ++plane)
		call_void_memop(vb, prepare, vb->planes[plane].mem_priv);

	call_void_vb_qop(vb, buf_queue, vb);
}

static int __buf_prepare(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	struct vb2_queue *q = vb->vb2_queue;
	struct rw_semaphore *mmap_sem;
	int ret;

	ret = __verify_length(vb, b);
	if (ret < 0) {
		dprintk(1, "plane parameters verification failed: %d\n", ret);
		return ret;
	}
	if (b->field == V4L2_FIELD_ALTERNATE && V4L2_TYPE_IS_OUTPUT(q->type)) {
		/*
		 * If the format's field is ALTERNATE, then the buffer's field
		 * should be either TOP or BOTTOM, not ALTERNATE since that
		 * makes no sense. The driver has to know whether the
		 * buffer represents a top or a bottom field in order to
		 * program any DMA correctly. Using ALTERNATE is wrong, since
		 * that just says that it is either a top or a bottom field,
		 * but not which of the two it is.
		 */
		dprintk(1, "the field is incorrectly set to ALTERNATE for an output buffer\n");
		return -EINVAL;
	}

	if (q->error) {
		dprintk(1, "fatal error occurred on queue\n");
		return -EIO;
	}

	vb->state = VB2_BUF_STATE_PREPARING;
	vb->v4l2_buf.timestamp.tv_sec = 0;
	vb->v4l2_buf.timestamp.tv_usec = 0;
	vb->v4l2_buf.sequence = 0;

	switch (q->memory) {
	case V4L2_MEMORY_MMAP:
		ret = __qbuf_mmap(vb, b);
		break;
	case V4L2_MEMORY_USERPTR:
		/*
		 * In case of user pointer buffers vb2 allocators need to get
		 * direct access to userspace pages. This requires getting
		 * the mmap semaphore for read access in the current process
		 * structure. The same semaphore is taken before calling mmap
		 * operation, while both qbuf/prepare_buf and mmap are called
		 * by the driver or v4l2 core with the driver's lock held.
		 * To avoid an AB-BA deadlock (mmap_sem then driver's lock in
		 * mmap and driver's lock then mmap_sem in qbuf/prepare_buf),
		 * the videobuf2 core releases the driver's lock, takes
		 * mmap_sem and then takes the driver's lock again.
		 */
		mmap_sem = &current->mm->mmap_sem;
		call_void_qop(q, wait_prepare, q);
		down_read(mmap_sem);
		call_void_qop(q, wait_finish, q);

		ret = __qbuf_userptr(vb, b);

		up_read(mmap_sem);
		break;
	case V4L2_MEMORY_DMABUF:
		ret = __qbuf_dmabuf(vb, b);
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

static int vb2_queue_or_prepare_buf(struct vb2_queue *q, struct v4l2_buffer *b,
				    const char *opname)
{
	if (b->type != q->type) {
		dprintk(1, "%s: invalid buffer type\n", opname);
		return -EINVAL;
	}

	if (b->index >= q->num_buffers) {
		dprintk(1, "%s: buffer index out of range\n", opname);
		return -EINVAL;
	}

	if (q->bufs[b->index] == NULL) {
		/* Should never happen */
		dprintk(1, "%s: buffer is NULL\n", opname);
		return -EINVAL;
	}

	if (b->memory != q->memory) {
		dprintk(1, "%s: invalid memory type\n", opname);
		return -EINVAL;
	}

	return __verify_planes_array(q->bufs[b->index], b);
}

/**
 * vb2_prepare_buf() - Pass ownership of a buffer from userspace to the kernel
 * @q:		videobuf2 queue
 * @b:		buffer structure passed from userspace to vidioc_prepare_buf
 *		handler in driver
 *
 * Should be called from vidioc_prepare_buf ioctl handler of a driver.
 * This function:
 * 1) verifies the passed buffer,
 * 2) calls buf_prepare callback in the driver (if provided), in which
 *    driver-specific buffer initialization can be performed,
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_prepare_buf handler in driver.
 */
int vb2_prepare_buf(struct vb2_queue *q, struct v4l2_buffer *b)
{
	struct vb2_buffer *vb;
	int ret;

	if (vb2_fileio_is_active(q)) {
		dprintk(1, "file io in progress\n");
		return -EBUSY;
	}

	ret = vb2_queue_or_prepare_buf(q, b, "prepare_buf");
	if (ret)
		return ret;

	vb = q->bufs[b->index];
	if (vb->state != VB2_BUF_STATE_DEQUEUED) {
		dprintk(1, "invalid buffer state %d\n",
			vb->state);
		return -EINVAL;
	}

	ret = __buf_prepare(vb, b);
	if (!ret) {
		/* Fill buffer information for the userspace */
		__fill_v4l2_buffer(vb, b);

		dprintk(1, "prepare of buffer %d succeeded\n", vb->v4l2_buf.index);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(vb2_prepare_buf);

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
	return ret;
}

static int vb2_internal_qbuf(struct vb2_queue *q, struct v4l2_buffer *b)
{
	int ret = vb2_queue_or_prepare_buf(q, b, "qbuf");
	struct vb2_buffer *vb;

	if (ret)
		return ret;

	vb = q->bufs[b->index];

	switch (vb->state) {
	case VB2_BUF_STATE_DEQUEUED:
		ret = __buf_prepare(vb, b);
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
	vb->state = VB2_BUF_STATE_QUEUED;
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		/*
		 * For output buffers copy the timestamp if needed,
		 * and the timecode field and flag if needed.
		 */
		if ((q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
		    V4L2_BUF_FLAG_TIMESTAMP_COPY)
			vb->v4l2_buf.timestamp = b->timestamp;
		vb->v4l2_buf.flags |= b->flags & V4L2_BUF_FLAG_TIMECODE;
		if (b->flags & V4L2_BUF_FLAG_TIMECODE)
			vb->v4l2_buf.timecode = b->timecode;
	}

	/*
	 * If already streaming, give the buffer to driver for processing.
	 * If not, the buffer will be given to driver on next streamon.
	 */
	if (q->start_streaming_called)
		__enqueue_in_driver(vb);

	/* Fill buffer information for the userspace */
	__fill_v4l2_buffer(vb, b);

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

	dprintk(1, "qbuf of buffer %d succeeded\n", vb->v4l2_buf.index);
	return 0;
}

/**
 * vb2_qbuf() - Queue a buffer from userspace
 * @q:		videobuf2 queue
 * @b:		buffer structure passed from userspace to vidioc_qbuf handler
 *		in driver
 *
 * Should be called from vidioc_qbuf ioctl handler of a driver.
 * This function:
 * 1) verifies the passed buffer,
 * 2) if necessary, calls buf_prepare callback in the driver (if provided), in
 *    which driver-specific buffer initialization can be performed,
 * 3) if streaming is on, queues the buffer in driver by the means of buf_queue
 *    callback for processing.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_qbuf handler in driver.
 */
int vb2_qbuf(struct vb2_queue *q, struct v4l2_buffer *b)
{
	if (vb2_fileio_is_active(q)) {
		dprintk(1, "file io in progress\n");
		return -EBUSY;
	}

	return vb2_internal_qbuf(q, b);
}
EXPORT_SYMBOL_GPL(vb2_qbuf);

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
				struct v4l2_buffer *b, int nonblocking)
{
	unsigned long flags;
	int ret;

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
	 * Only remove the buffer from done_list if v4l2_buffer can handle all
	 * the planes.
	 */
	ret = __verify_planes_array(*vb, b);
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
	if (q->memory == V4L2_MEMORY_DMABUF)
		for (i = 0; i < vb->num_planes; ++i) {
			if (!vb->planes[i].dbuf_mapped)
				continue;
			call_void_memop(vb, unmap_dmabuf, vb->planes[i].mem_priv);
			vb->planes[i].dbuf_mapped = 0;
		}
}

static int vb2_internal_dqbuf(struct vb2_queue *q, struct v4l2_buffer *b, bool nonblocking)
{
	struct vb2_buffer *vb = NULL;
	int ret;

	if (b->type != q->type) {
		dprintk(1, "invalid buffer type\n");
		return -EINVAL;
	}
	ret = __vb2_get_done_vb(q, &vb, b, nonblocking);
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
	__fill_v4l2_buffer(vb, b);
	/* Remove from videobuf queue */
	list_del(&vb->queued_entry);
	q->queued_count--;
	/* go back to dequeued state */
	__vb2_dqbuf(vb);

	dprintk(1, "dqbuf of buffer %d, with state %d\n",
			vb->v4l2_buf.index, vb->state);

	return 0;
}

/**
 * vb2_dqbuf() - Dequeue a buffer to the userspace
 * @q:		videobuf2 queue
 * @b:		buffer structure passed from userspace to vidioc_dqbuf handler
 *		in driver
 * @nonblocking: if true, this call will not sleep waiting for a buffer if no
 *		 buffers ready for dequeuing are present. Normally the driver
 *		 would be passing (file->f_flags & O_NONBLOCK) here
 *
 * Should be called from vidioc_dqbuf ioctl handler of a driver.
 * This function:
 * 1) verifies the passed buffer,
 * 2) calls buf_finish callback in the driver (if provided), in which
 *    driver can perform any additional operations that may be required before
 *    returning the buffer to userspace, such as cache sync,
 * 3) the buffer struct members are filled with relevant information for
 *    the userspace.
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_dqbuf handler in driver.
 */
int vb2_dqbuf(struct vb2_queue *q, struct v4l2_buffer *b, bool nonblocking)
{
	if (vb2_fileio_is_active(q)) {
		dprintk(1, "file io in progress\n");
		return -EBUSY;
	}
	return vb2_internal_dqbuf(q, b, nonblocking);
}
EXPORT_SYMBOL_GPL(vb2_dqbuf);

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

static int vb2_internal_streamon(struct vb2_queue *q, enum v4l2_buf_type type)
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

/**
 * vb2_streamon - start streaming
 * @q:		videobuf2 queue
 * @type:	type argument passed from userspace to vidioc_streamon handler
 *
 * Should be called from vidioc_streamon handler of a driver.
 * This function:
 * 1) verifies current state
 * 2) passes any previously queued buffers to the driver and starts streaming
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_streamon handler in the driver.
 */
int vb2_streamon(struct vb2_queue *q, enum v4l2_buf_type type)
{
	if (vb2_fileio_is_active(q)) {
		dprintk(1, "file io in progress\n");
		return -EBUSY;
	}
	return vb2_internal_streamon(q, type);
}
EXPORT_SYMBOL_GPL(vb2_streamon);

static int vb2_internal_streamoff(struct vb2_queue *q, enum v4l2_buf_type type)
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

	dprintk(3, "successful\n");
	return 0;
}

/**
 * vb2_streamoff - stop streaming
 * @q:		videobuf2 queue
 * @type:	type argument passed from userspace to vidioc_streamoff handler
 *
 * Should be called from vidioc_streamoff handler of a driver.
 * This function:
 * 1) verifies current state,
 * 2) stop streaming and dequeues any queued buffers, including those previously
 *    passed to the driver (after waiting for the driver to finish).
 *
 * This call can be used for pausing playback.
 * The return values from this function are intended to be directly returned
 * from vidioc_streamoff handler in the driver
 */
int vb2_streamoff(struct vb2_queue *q, enum v4l2_buf_type type)
{
	if (vb2_fileio_is_active(q)) {
		dprintk(1, "file io in progress\n");
		return -EBUSY;
	}
	return vb2_internal_streamoff(q, type);
}
EXPORT_SYMBOL_GPL(vb2_streamoff);

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
			if (vb->v4l2_planes[plane].m.mem_offset == off) {
				*_buffer = buffer;
				*_plane = plane;
				return 0;
			}
		}
	}

	return -EINVAL;
}

/**
 * vb2_expbuf() - Export a buffer as a file descriptor
 * @q:		videobuf2 queue
 * @eb:		export buffer structure passed from userspace to vidioc_expbuf
 *		handler in driver
 *
 * The return values from this function are intended to be directly returned
 * from vidioc_expbuf handler in driver.
 */
int vb2_expbuf(struct vb2_queue *q, struct v4l2_exportbuffer *eb)
{
	struct vb2_buffer *vb = NULL;
	struct vb2_plane *vb_plane;
	int ret;
	struct dma_buf *dbuf;

	if (q->memory != V4L2_MEMORY_MMAP) {
		dprintk(1, "queue is not currently set up for mmap\n");
		return -EINVAL;
	}

	if (!q->mem_ops->get_dmabuf) {
		dprintk(1, "queue does not support DMA buffer exporting\n");
		return -EINVAL;
	}

	if (eb->flags & ~(O_CLOEXEC | O_ACCMODE)) {
		dprintk(1, "queue does support only O_CLOEXEC and access mode flags\n");
		return -EINVAL;
	}

	if (eb->type != q->type) {
		dprintk(1, "invalid buffer type\n");
		return -EINVAL;
	}

	if (eb->index >= q->num_buffers) {
		dprintk(1, "buffer index out of range\n");
		return -EINVAL;
	}

	vb = q->bufs[eb->index];

	if (eb->plane >= vb->num_planes) {
		dprintk(1, "buffer plane out of range\n");
		return -EINVAL;
	}

	if (vb2_fileio_is_active(q)) {
		dprintk(1, "expbuf: file io in progress\n");
		return -EBUSY;
	}

	vb_plane = &vb->planes[eb->plane];

	dbuf = call_ptr_memop(vb, get_dmabuf, vb_plane->mem_priv, eb->flags & O_ACCMODE);
	if (IS_ERR_OR_NULL(dbuf)) {
		dprintk(1, "failed to export buffer %d, plane %d\n",
			eb->index, eb->plane);
		return -EINVAL;
	}

	ret = dma_buf_fd(dbuf, eb->flags & ~O_ACCMODE);
	if (ret < 0) {
		dprintk(3, "buffer %d, plane %d failed to export (%d)\n",
			eb->index, eb->plane, ret);
		dma_buf_put(dbuf);
		return ret;
	}

	dprintk(3, "buffer %d, plane %d exported as %d descriptor\n",
		eb->index, eb->plane, ret);
	eb->fd = ret;

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_expbuf);

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

	if (q->memory != V4L2_MEMORY_MMAP) {
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
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
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
	length = PAGE_ALIGN(vb->v4l2_planes[plane].length);
	if (length < (vma->vm_end - vma->vm_start)) {
		dprintk(1,
			"MMAP invalid, as it would overflow buffer length\n");
		return -EINVAL;
	}

	ret = call_memop(vb, mmap, vb->planes[plane].mem_priv, vma);
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
	int ret;

	if (q->memory != V4L2_MEMORY_MMAP) {
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

	return (unsigned long)vb2_plane_vaddr(vb, plane);
}
EXPORT_SYMBOL_GPL(vb2_get_unmapped_area);
#endif

static int __vb2_init_fileio(struct vb2_queue *q, int read);
static int __vb2_cleanup_fileio(struct vb2_queue *q);

/**
 * vb2_poll() - implements poll userspace operation
 * @q:		videobuf2 queue
 * @file:	file argument passed to the poll file operation handler
 * @wait:	wait argument passed to the poll file operation handler
 *
 * This function implements poll file operation handler for a driver.
 * For CAPTURE queues, if a buffer is ready to be dequeued, the userspace will
 * be informed that the file descriptor of a video device is available for
 * reading.
 * For OUTPUT queues, if a buffer is ready to be dequeued, the file descriptor
 * will be reported as available for writing.
 *
 * If the driver uses struct v4l2_fh, then vb2_poll() will also check for any
 * pending events.
 *
 * The return values from this function are intended to be directly returned
 * from poll handler in driver.
 */
unsigned int vb2_poll(struct vb2_queue *q, struct file *file, poll_table *wait)
{
	struct video_device *vfd = video_devdata(file);
	unsigned long req_events = poll_requested_events(wait);
	struct vb2_buffer *vb = NULL;
	unsigned int res = 0;
	unsigned long flags;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags)) {
		struct v4l2_fh *fh = file->private_data;

		if (v4l2_event_pending(fh))
			res = POLLPRI;
		else if (req_events & POLLPRI)
			poll_wait(file, &fh->wait, wait);
	}

	if (!V4L2_TYPE_IS_OUTPUT(q->type) && !(req_events & (POLLIN | POLLRDNORM)))
		return res;
	if (V4L2_TYPE_IS_OUTPUT(q->type) && !(req_events & (POLLOUT | POLLWRNORM)))
		return res;

	/*
	 * Start file I/O emulator only if streaming API has not been used yet.
	 */
	if (q->num_buffers == 0 && !vb2_fileio_is_active(q)) {
		if (!V4L2_TYPE_IS_OUTPUT(q->type) && (q->io_modes & VB2_READ) &&
				(req_events & (POLLIN | POLLRDNORM))) {
			if (__vb2_init_fileio(q, 1))
				return res | POLLERR;
		}
		if (V4L2_TYPE_IS_OUTPUT(q->type) && (q->io_modes & VB2_WRITE) &&
				(req_events & (POLLOUT | POLLWRNORM))) {
			if (__vb2_init_fileio(q, 0))
				return res | POLLERR;
			/*
			 * Write to OUTPUT queue can be done immediately.
			 */
			return res | POLLOUT | POLLWRNORM;
		}
	}

	/*
	 * There is nothing to wait for if no buffer has been queued and the
	 * queue isn't streaming, or if the error flag is set.
	 */
	if ((list_empty(&q->queued_list) && !vb2_is_streaming(q)) || q->error)
		return res | POLLERR;

	if (list_empty(&q->done_list))
		poll_wait(file, &q->done_wq, wait);

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
		return (V4L2_TYPE_IS_OUTPUT(q->type)) ?
				res | POLLOUT | POLLWRNORM :
				res | POLLIN | POLLRDNORM;
	}
	return res;
}
EXPORT_SYMBOL_GPL(vb2_poll);

/**
 * vb2_queue_init() - initialize a videobuf2 queue
 * @q:		videobuf2 queue; this structure should be allocated in driver
 *
 * The vb2_queue structure should be allocated by the driver. The driver is
 * responsible of clearing it's content and setting initial values for some
 * required entries before calling this function.
 * q->ops, q->mem_ops, q->type and q->io_modes are mandatory. Please refer
 * to the struct vb2_queue description in include/media/videobuf2-core.h
 * for more information.
 */
int vb2_queue_init(struct vb2_queue *q)
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
	    WARN_ON(!q->ops->buf_queue)   ||
	    WARN_ON(q->timestamp_flags &
		    ~(V4L2_BUF_FLAG_TIMESTAMP_MASK |
		      V4L2_BUF_FLAG_TSTAMP_SRC_MASK)))
		return -EINVAL;

	/* Warn that the driver should choose an appropriate timestamp type */
	WARN_ON((q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
		V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN);

	INIT_LIST_HEAD(&q->queued_list);
	INIT_LIST_HEAD(&q->done_list);
	spin_lock_init(&q->done_lock);
	init_waitqueue_head(&q->done_wq);

	if (q->buf_struct_size == 0)
		q->buf_struct_size = sizeof(struct vb2_buffer);

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_queue_init);

/**
 * vb2_queue_release() - stop streaming, release the queue and free memory
 * @q:		videobuf2 queue
 *
 * This function stops streaming and performs necessary clean ups, including
 * freeing video buffer memory. The driver is responsible for freeing
 * the vb2_queue structure itself.
 */
void vb2_queue_release(struct vb2_queue *q)
{
	__vb2_cleanup_fileio(q);
	__vb2_queue_cancel(q);
	__vb2_queue_free(q, q->num_buffers);
}
EXPORT_SYMBOL_GPL(vb2_queue_release);

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
	struct v4l2_requestbuffers req;
	struct v4l2_plane p;
	struct v4l2_buffer b;
	struct vb2_fileio_buf bufs[VIDEO_MAX_FRAME];
	unsigned int cur_index;
	unsigned int initial_index;
	unsigned int q_count;
	unsigned int dq_count;
	unsigned int flags;
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

	dprintk(3, "setting up file io: mode %s, count %d, flags %08x\n",
		(read) ? "read" : "write", count, q->io_flags);

	fileio = kzalloc(sizeof(struct vb2_fileio_data), GFP_KERNEL);
	if (fileio == NULL)
		return -ENOMEM;

	fileio->flags = q->io_flags;

	/*
	 * Request buffers and use MMAP type to force driver
	 * to allocate buffers by itself.
	 */
	fileio->req.count = count;
	fileio->req.memory = V4L2_MEMORY_MMAP;
	fileio->req.type = q->type;
	q->fileio = fileio;
	ret = __reqbufs(q, &fileio->req);
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
		bool is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(q->type);

		/*
		 * Queue all buffers.
		 */
		for (i = 0; i < q->num_buffers; i++) {
			struct v4l2_buffer *b = &fileio->b;

			memset(b, 0, sizeof(*b));
			b->type = q->type;
			if (is_multiplanar) {
				memset(&fileio->p, 0, sizeof(fileio->p));
				b->m.planes = &fileio->p;
				b->length = 1;
			}
			b->memory = q->memory;
			b->index = i;
			ret = vb2_internal_qbuf(q, b);
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
	ret = vb2_internal_streamon(q, q->type);
	if (ret)
		goto err_reqbufs;

	return ret;

err_reqbufs:
	fileio->req.count = 0;
	__reqbufs(q, &fileio->req);

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
		vb2_internal_streamoff(q, q->type);
		q->fileio = NULL;
		fileio->req.count = 0;
		vb2_reqbufs(q, &fileio->req);
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
	bool is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(q->type);
	/*
	 * When using write() to write data to an output video node the vb2 core
	 * should set timestamps if V4L2_BUF_FLAG_TIMESTAMP_COPY is set. Nobody
	 * else is able to provide this information with the write() operation.
	 */
	bool set_timestamp = !read &&
		(q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
		V4L2_BUF_FLAG_TIMESTAMP_COPY;
	int ret, index;

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
		/*
		 * Call vb2_dqbuf to get buffer back.
		 */
		memset(&fileio->b, 0, sizeof(fileio->b));
		fileio->b.type = q->type;
		fileio->b.memory = q->memory;
		if (is_multiplanar) {
			memset(&fileio->p, 0, sizeof(fileio->p));
			fileio->b.m.planes = &fileio->p;
			fileio->b.length = 1;
		}
		ret = vb2_internal_dqbuf(q, &fileio->b, nonblock);
		dprintk(5, "vb2_dqbuf result: %d\n", ret);
		if (ret)
			return ret;
		fileio->dq_count += 1;

		fileio->cur_index = index = fileio->b.index;
		buf = &fileio->bufs[index];

		/*
		 * Get number of bytes filled by the driver
		 */
		buf->pos = 0;
		buf->queued = 0;
		buf->size = read ? vb2_get_plane_payload(q->bufs[index], 0)
				 : vb2_plane_size(q->bufs[index], 0);
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
	if (buf->pos == buf->size ||
	   (!read && (fileio->flags & VB2_FILEIO_WRITE_IMMEDIATELY))) {
		/*
		 * Check if this is the last buffer to read.
		 */
		if (read && (fileio->flags & VB2_FILEIO_READ_ONCE) &&
		    fileio->dq_count == 1) {
			dprintk(3, "read limit reached\n");
			return __vb2_cleanup_fileio(q);
		}

		/*
		 * Call vb2_qbuf and give buffer to the driver.
		 */
		memset(&fileio->b, 0, sizeof(fileio->b));
		fileio->b.type = q->type;
		fileio->b.memory = q->memory;
		fileio->b.index = index;
		fileio->b.bytesused = buf->pos;
		if (is_multiplanar) {
			memset(&fileio->p, 0, sizeof(fileio->p));
			fileio->p.bytesused = buf->pos;
			fileio->b.m.planes = &fileio->p;
			fileio->b.length = 1;
		}
		if (set_timestamp)
			v4l2_get_timestamp(&fileio->b.timestamp);
		ret = vb2_internal_qbuf(q, &fileio->b);
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
	struct vb2_fileio_data *fileio = q->fileio;
	bool set_timestamp = false;
	int prequeue = 0;
	int index = 0;
	int ret = 0;

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		prequeue = q->num_buffers;
		set_timestamp =
			(q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
			V4L2_BUF_FLAG_TIMESTAMP_COPY;
	}

	set_freezable();

	for (;;) {
		struct vb2_buffer *vb;

		/*
		 * Call vb2_dqbuf to get buffer back.
		 */
		memset(&fileio->b, 0, sizeof(fileio->b));
		fileio->b.type = q->type;
		fileio->b.memory = q->memory;
		if (prequeue) {
			fileio->b.index = index++;
			prequeue--;
		} else {
			call_void_qop(q, wait_finish, q);
			ret = vb2_internal_dqbuf(q, &fileio->b, 0);
			call_void_qop(q, wait_prepare, q);
			dprintk(5, "file io: vb2_dqbuf result: %d\n", ret);
		}
		if (threadio->stop)
			break;
		if (ret)
			break;
		try_to_freeze();

		vb = q->bufs[fileio->b.index];
		if (!(fileio->b.flags & V4L2_BUF_FLAG_ERROR))
			ret = threadio->fnc(vb, threadio->priv);
		if (ret)
			break;
		call_void_qop(q, wait_finish, q);
		if (set_timestamp)
			v4l2_get_timestamp(&fileio->b.timestamp);
		ret = vb2_internal_qbuf(q, &fileio->b);
		call_void_qop(q, wait_prepare, q);
		if (ret)
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

	ret = __vb2_init_fileio(q, !V4L2_TYPE_IS_OUTPUT(q->type));
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
	struct vb2_fileio_data *fileio = q->fileio;
	int err;

	if (threadio == NULL)
		return 0;
	call_void_qop(q, wait_finish, q);
	threadio->stop = true;
	vb2_internal_streamoff(q, q->type);
	call_void_qop(q, wait_prepare, q);
	q->fileio = NULL;
	fileio->req.count = 0;
	vb2_reqbufs(q, &fileio->req);
	kfree(fileio);
	err = kthread_stop(threadio->thread);
	threadio->thread = NULL;
	kfree(threadio);
	q->fileio = NULL;
	q->threadio = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(vb2_thread_stop);

/*
 * The following functions are not part of the vb2 core API, but are helper
 * functions that plug into struct v4l2_ioctl_ops, struct v4l2_file_operations
 * and struct vb2_ops.
 * They contain boilerplate code that most if not all drivers have to do
 * and so they simplify the driver code.
 */

/* The queue is busy if there is a owner and you are not that owner. */
static inline bool vb2_queue_is_busy(struct video_device *vdev, struct file *file)
{
	return vdev->queue->owner && vdev->queue->owner != file->private_data;
}

/* vb2 ioctl helpers */

int vb2_ioctl_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct video_device *vdev = video_devdata(file);
	int res = __verify_memory_type(vdev->queue, p->memory, p->type);

	if (res)
		return res;
	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	res = __reqbufs(vdev->queue, p);
	/* If count == 0, then the owner has released all buffers and he
	   is no longer owner of the queue. Otherwise we have a new owner. */
	if (res == 0)
		vdev->queue->owner = p->count ? file->private_data : NULL;
	return res;
}
EXPORT_SYMBOL_GPL(vb2_ioctl_reqbufs);

int vb2_ioctl_create_bufs(struct file *file, void *priv,
			  struct v4l2_create_buffers *p)
{
	struct video_device *vdev = video_devdata(file);
	int res = __verify_memory_type(vdev->queue, p->memory, p->format.type);

	p->index = vdev->queue->num_buffers;
	/* If count == 0, then just check if memory and type are valid.
	   Any -EBUSY result from __verify_memory_type can be mapped to 0. */
	if (p->count == 0)
		return res != -EBUSY ? res : 0;
	if (res)
		return res;
	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	res = __create_bufs(vdev->queue, p);
	if (res == 0)
		vdev->queue->owner = file->private_data;
	return res;
}
EXPORT_SYMBOL_GPL(vb2_ioctl_create_bufs);

int vb2_ioctl_prepare_buf(struct file *file, void *priv,
			  struct v4l2_buffer *p)
{
	struct video_device *vdev = video_devdata(file);

	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	return vb2_prepare_buf(vdev->queue, p);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_prepare_buf);

int vb2_ioctl_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct video_device *vdev = video_devdata(file);

	/* No need to call vb2_queue_is_busy(), anyone can query buffers. */
	return vb2_querybuf(vdev->queue, p);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_querybuf);

int vb2_ioctl_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct video_device *vdev = video_devdata(file);

	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	return vb2_qbuf(vdev->queue, p);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_qbuf);

int vb2_ioctl_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct video_device *vdev = video_devdata(file);

	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	return vb2_dqbuf(vdev->queue, p, file->f_flags & O_NONBLOCK);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_dqbuf);

int vb2_ioctl_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct video_device *vdev = video_devdata(file);

	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	return vb2_streamon(vdev->queue, i);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_streamon);

int vb2_ioctl_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct video_device *vdev = video_devdata(file);

	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	return vb2_streamoff(vdev->queue, i);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_streamoff);

int vb2_ioctl_expbuf(struct file *file, void *priv, struct v4l2_exportbuffer *p)
{
	struct video_device *vdev = video_devdata(file);

	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	return vb2_expbuf(vdev->queue, p);
}
EXPORT_SYMBOL_GPL(vb2_ioctl_expbuf);

/* v4l2_file_operations helpers */

int vb2_fop_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct mutex *lock = vdev->queue->lock ? vdev->queue->lock : vdev->lock;
	int err;

	if (lock && mutex_lock_interruptible(lock))
		return -ERESTARTSYS;
	err = vb2_mmap(vdev->queue, vma);
	if (lock)
		mutex_unlock(lock);
	return err;
}
EXPORT_SYMBOL_GPL(vb2_fop_mmap);

int _vb2_fop_release(struct file *file, struct mutex *lock)
{
	struct video_device *vdev = video_devdata(file);

	if (file->private_data == vdev->queue->owner) {
		if (lock)
			mutex_lock(lock);
		vb2_queue_release(vdev->queue);
		vdev->queue->owner = NULL;
		if (lock)
			mutex_unlock(lock);
	}
	return v4l2_fh_release(file);
}
EXPORT_SYMBOL_GPL(_vb2_fop_release);

int vb2_fop_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct mutex *lock = vdev->queue->lock ? vdev->queue->lock : vdev->lock;

	return _vb2_fop_release(file, lock);
}
EXPORT_SYMBOL_GPL(vb2_fop_release);

ssize_t vb2_fop_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct video_device *vdev = video_devdata(file);
	struct mutex *lock = vdev->queue->lock ? vdev->queue->lock : vdev->lock;
	int err = -EBUSY;

	if (lock && mutex_lock_interruptible(lock))
		return -ERESTARTSYS;
	if (vb2_queue_is_busy(vdev, file))
		goto exit;
	err = vb2_write(vdev->queue, buf, count, ppos,
		       file->f_flags & O_NONBLOCK);
	if (vdev->queue->fileio)
		vdev->queue->owner = file->private_data;
exit:
	if (lock)
		mutex_unlock(lock);
	return err;
}
EXPORT_SYMBOL_GPL(vb2_fop_write);

ssize_t vb2_fop_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct video_device *vdev = video_devdata(file);
	struct mutex *lock = vdev->queue->lock ? vdev->queue->lock : vdev->lock;
	int err = -EBUSY;

	if (lock && mutex_lock_interruptible(lock))
		return -ERESTARTSYS;
	if (vb2_queue_is_busy(vdev, file))
		goto exit;
	err = vb2_read(vdev->queue, buf, count, ppos,
		       file->f_flags & O_NONBLOCK);
	if (vdev->queue->fileio)
		vdev->queue->owner = file->private_data;
exit:
	if (lock)
		mutex_unlock(lock);
	return err;
}
EXPORT_SYMBOL_GPL(vb2_fop_read);

unsigned int vb2_fop_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct vb2_queue *q = vdev->queue;
	struct mutex *lock = q->lock ? q->lock : vdev->lock;
	unsigned long req_events = poll_requested_events(wait);
	unsigned res;
	void *fileio;
	bool must_lock = false;

	/* Try to be smart: only lock if polling might start fileio,
	   otherwise locking will only introduce unwanted delays. */
	if (q->num_buffers == 0 && !vb2_fileio_is_active(q)) {
		if (!V4L2_TYPE_IS_OUTPUT(q->type) && (q->io_modes & VB2_READ) &&
				(req_events & (POLLIN | POLLRDNORM)))
			must_lock = true;
		else if (V4L2_TYPE_IS_OUTPUT(q->type) && (q->io_modes & VB2_WRITE) &&
				(req_events & (POLLOUT | POLLWRNORM)))
			must_lock = true;
	}

	/* If locking is needed, but this helper doesn't know how, then you
	   shouldn't be using this helper but you should write your own. */
	WARN_ON(must_lock && !lock);

	if (must_lock && lock && mutex_lock_interruptible(lock))
		return POLLERR;

	fileio = q->fileio;

	res = vb2_poll(vdev->queue, file, wait);

	/* If fileio was started, then we have a new queue owner. */
	if (must_lock && !fileio && q->fileio)
		q->owner = file->private_data;
	if (must_lock && lock)
		mutex_unlock(lock);
	return res;
}
EXPORT_SYMBOL_GPL(vb2_fop_poll);

#ifndef CONFIG_MMU
unsigned long vb2_fop_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct video_device *vdev = video_devdata(file);
	struct mutex *lock = vdev->queue->lock ? vdev->queue->lock : vdev->lock;
	int ret;

	if (lock && mutex_lock_interruptible(lock))
		return -ERESTARTSYS;
	ret = vb2_get_unmapped_area(vdev->queue, addr, len, pgoff, flags);
	if (lock)
		mutex_unlock(lock);
	return ret;
}
EXPORT_SYMBOL_GPL(vb2_fop_get_unmapped_area);
#endif

/* vb2_ops helpers. Only use if vq->lock is non-NULL. */

void vb2_ops_wait_prepare(struct vb2_queue *vq)
{
	mutex_unlock(vq->lock);
}
EXPORT_SYMBOL_GPL(vb2_ops_wait_prepare);

void vb2_ops_wait_finish(struct vb2_queue *vq)
{
	mutex_lock(vq->lock);
}
EXPORT_SYMBOL_GPL(vb2_ops_wait_finish);

MODULE_DESCRIPTION("Driver helper framework for Video for Linux 2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>, Marek Szyprowski");
MODULE_LICENSE("GPL");
