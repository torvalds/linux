/*
 * videobuf2-v4l2.c - V4L2 driver helper framework
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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/freezer.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>

#include <media/videobuf2-v4l2.h>

static int debug;
module_param(debug, int, 0644);

#define dprintk(q, level, fmt, arg...)					      \
	do {								      \
		if (debug >= level)					      \
			pr_info("vb2-v4l2: [%p] %s: " fmt,		      \
				(q)->name, __func__, ## arg);		      \
	} while (0)

/* Flags that are set by us */
#define V4L2_BUFFER_MASK_FLAGS	(V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED | \
				 V4L2_BUF_FLAG_DONE | V4L2_BUF_FLAG_ERROR | \
				 V4L2_BUF_FLAG_PREPARED | \
				 V4L2_BUF_FLAG_IN_REQUEST | \
				 V4L2_BUF_FLAG_REQUEST_FD | \
				 V4L2_BUF_FLAG_TIMESTAMP_MASK)
/* Output buffer flags that should be passed on to the driver */
#define V4L2_BUFFER_OUT_FLAGS	(V4L2_BUF_FLAG_PFRAME | \
				 V4L2_BUF_FLAG_BFRAME | \
				 V4L2_BUF_FLAG_KEYFRAME | \
				 V4L2_BUF_FLAG_TIMECODE | \
				 V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF)

/*
 * __verify_planes_array() - verify that the planes array passed in struct
 * v4l2_buffer from userspace can be safely used
 */
static int __verify_planes_array(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type))
		return 0;

	/* Is memory for copying plane information present? */
	if (b->m.planes == NULL) {
		dprintk(vb->vb2_queue, 1,
			"multi-planar buffer passed but planes array not provided\n");
		return -EINVAL;
	}

	if (b->length < vb->num_planes || b->length > VB2_MAX_PLANES) {
		dprintk(vb->vb2_queue, 1,
			"incorrect planes array length, expected %d, got %d\n",
			vb->num_planes, b->length);
		return -EINVAL;
	}

	return 0;
}

static int __verify_planes_array_core(struct vb2_buffer *vb, const void *pb)
{
	return __verify_planes_array(vb, pb);
}

/*
 * __verify_length() - Verify that the bytesused value for each plane fits in
 * the plane length and that the data offset doesn't exceed the bytesused value.
 */
static int __verify_length(struct vb2_buffer *vb, const struct v4l2_buffer *b)
{
	unsigned int length;
	unsigned int bytesused;
	unsigned int plane;

	if (V4L2_TYPE_IS_CAPTURE(b->type))
		return 0;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		for (plane = 0; plane < vb->num_planes; ++plane) {
			length = (b->memory == VB2_MEMORY_USERPTR ||
				  b->memory == VB2_MEMORY_DMABUF)
			       ? b->m.planes[plane].length
				: vb->planes[plane].length;
			bytesused = b->m.planes[plane].bytesused
				  ? b->m.planes[plane].bytesused : length;

			if (b->m.planes[plane].bytesused > length)
				return -EINVAL;

			if (b->m.planes[plane].data_offset > 0 &&
			    b->m.planes[plane].data_offset >= bytesused)
				return -EINVAL;
		}
	} else {
		length = (b->memory == VB2_MEMORY_USERPTR)
			? b->length : vb->planes[0].length;

		if (b->bytesused > length)
			return -EINVAL;
	}

	return 0;
}

/*
 * __init_vb2_v4l2_buffer() - initialize the vb2_v4l2_buffer struct
 */
static void __init_vb2_v4l2_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->request_fd = -1;
}

static void __copy_timestamp(struct vb2_buffer *vb, const void *pb)
{
	const struct v4l2_buffer *b = pb;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *q = vb->vb2_queue;

	if (q->is_output) {
		/*
		 * For output buffers copy the timestamp if needed,
		 * and the timecode field and flag if needed.
		 */
		if (q->copy_timestamp)
			vb->timestamp = v4l2_buffer_get_timestamp(b);
		vbuf->flags |= b->flags & V4L2_BUF_FLAG_TIMECODE;
		if (b->flags & V4L2_BUF_FLAG_TIMECODE)
			vbuf->timecode = b->timecode;
	}
};

static void vb2_warn_zero_bytesused(struct vb2_buffer *vb)
{
	static bool check_once;

	if (check_once)
		return;

	check_once = true;

	pr_warn("use of bytesused == 0 is deprecated and will be removed in the future,\n");
	if (vb->vb2_queue->allow_zero_bytesused)
		pr_warn("use VIDIOC_DECODER_CMD(V4L2_DEC_CMD_STOP) instead.\n");
	else
		pr_warn("use the actual size instead.\n");
}

static int vb2_fill_vb2_v4l2_buffer(struct vb2_buffer *vb, struct v4l2_buffer *b)
{
	struct vb2_queue *q = vb->vb2_queue;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_plane *planes = vbuf->planes;
	unsigned int plane;
	int ret;

	ret = __verify_length(vb, b);
	if (ret < 0) {
		dprintk(q, 1, "plane parameters verification failed: %d\n", ret);
		return ret;
	}
	if (b->field == V4L2_FIELD_ALTERNATE && q->is_output) {
		/*
		 * If the format's field is ALTERNATE, then the buffer's field
		 * should be either TOP or BOTTOM, not ALTERNATE since that
		 * makes no sense. The driver has to know whether the
		 * buffer represents a top or a bottom field in order to
		 * program any DMA correctly. Using ALTERNATE is wrong, since
		 * that just says that it is either a top or a bottom field,
		 * but not which of the two it is.
		 */
		dprintk(q, 1, "the field is incorrectly set to ALTERNATE for an output buffer\n");
		return -EINVAL;
	}
	vbuf->sequence = 0;
	vbuf->request_fd = -1;
	vbuf->is_held = false;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		switch (b->memory) {
		case VB2_MEMORY_USERPTR:
			for (plane = 0; plane < vb->num_planes; ++plane) {
				planes[plane].m.userptr =
					b->m.planes[plane].m.userptr;
				planes[plane].length =
					b->m.planes[plane].length;
			}
			break;
		case VB2_MEMORY_DMABUF:
			for (plane = 0; plane < vb->num_planes; ++plane) {
				planes[plane].m.fd =
					b->m.planes[plane].m.fd;
				planes[plane].length =
					b->m.planes[plane].length;
			}
			break;
		default:
			for (plane = 0; plane < vb->num_planes; ++plane) {
				planes[plane].m.offset =
					vb->planes[plane].m.offset;
				planes[plane].length =
					vb->planes[plane].length;
			}
			break;
		}

		/* Fill in driver-provided information for OUTPUT types */
		if (V4L2_TYPE_IS_OUTPUT(b->type)) {
			/*
			 * Will have to go up to b->length when API starts
			 * accepting variable number of planes.
			 *
			 * If bytesused == 0 for the output buffer, then fall
			 * back to the full buffer size. In that case
			 * userspace clearly never bothered to set it and
			 * it's a safe assumption that they really meant to
			 * use the full plane sizes.
			 *
			 * Some drivers, e.g. old codec drivers, use bytesused == 0
			 * as a way to indicate that streaming is finished.
			 * In that case, the driver should use the
			 * allow_zero_bytesused flag to keep old userspace
			 * applications working.
			 */
			for (plane = 0; plane < vb->num_planes; ++plane) {
				struct vb2_plane *pdst = &planes[plane];
				struct v4l2_plane *psrc = &b->m.planes[plane];

				if (psrc->bytesused == 0)
					vb2_warn_zero_bytesused(vb);

				if (vb->vb2_queue->allow_zero_bytesused)
					pdst->bytesused = psrc->bytesused;
				else
					pdst->bytesused = psrc->bytesused ?
						psrc->bytesused : pdst->length;
				pdst->data_offset = psrc->data_offset;
			}
		}
	} else {
		/*
		 * Single-planar buffers do not use planes array,
		 * so fill in relevant v4l2_buffer struct fields instead.
		 * In videobuf we use our internal V4l2_planes struct for
		 * single-planar buffers as well, for simplicity.
		 *
		 * If bytesused == 0 for the output buffer, then fall back
		 * to the full buffer size as that's a sensible default.
		 *
		 * Some drivers, e.g. old codec drivers, use bytesused == 0 as
		 * a way to indicate that streaming is finished. In that case,
		 * the driver should use the allow_zero_bytesused flag to keep
		 * old userspace applications working.
		 */
		switch (b->memory) {
		case VB2_MEMORY_USERPTR:
			planes[0].m.userptr = b->m.userptr;
			planes[0].length = b->length;
			break;
		case VB2_MEMORY_DMABUF:
			planes[0].m.fd = b->m.fd;
			planes[0].length = b->length;
			break;
		default:
			planes[0].m.offset = vb->planes[0].m.offset;
			planes[0].length = vb->planes[0].length;
			break;
		}

		planes[0].data_offset = 0;
		if (V4L2_TYPE_IS_OUTPUT(b->type)) {
			if (b->bytesused == 0)
				vb2_warn_zero_bytesused(vb);

			if (vb->vb2_queue->allow_zero_bytesused)
				planes[0].bytesused = b->bytesused;
			else
				planes[0].bytesused = b->bytesused ?
					b->bytesused : planes[0].length;
		} else
			planes[0].bytesused = 0;

	}

	/* Zero flags that we handle */
	vbuf->flags = b->flags & ~V4L2_BUFFER_MASK_FLAGS;
	if (!vb->vb2_queue->copy_timestamp || V4L2_TYPE_IS_CAPTURE(b->type)) {
		/*
		 * Non-COPY timestamps and non-OUTPUT queues will get
		 * their timestamp and timestamp source flags from the
		 * queue.
		 */
		vbuf->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	}

	if (V4L2_TYPE_IS_OUTPUT(b->type)) {
		/*
		 * For output buffers mask out the timecode flag:
		 * this will be handled later in vb2_qbuf().
		 * The 'field' is valid metadata for this output buffer
		 * and so that needs to be copied here.
		 */
		vbuf->flags &= ~V4L2_BUF_FLAG_TIMECODE;
		vbuf->field = b->field;
		if (!(q->subsystem_flags & VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF))
			vbuf->flags &= ~V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF;
	} else {
		/* Zero any output buffer flags as this is a capture buffer */
		vbuf->flags &= ~V4L2_BUFFER_OUT_FLAGS;
		/* Zero last flag, this is a signal from driver to userspace */
		vbuf->flags &= ~V4L2_BUF_FLAG_LAST;
	}

	return 0;
}

static void set_buffer_cache_hints(struct vb2_queue *q,
				   struct vb2_buffer *vb,
				   struct v4l2_buffer *b)
{
	if (!vb2_queue_allows_cache_hints(q)) {
		/*
		 * Clear buffer cache flags if queue does not support user
		 * space hints. That's to indicate to userspace that these
		 * flags won't work.
		 */
		b->flags &= ~V4L2_BUF_FLAG_NO_CACHE_INVALIDATE;
		b->flags &= ~V4L2_BUF_FLAG_NO_CACHE_CLEAN;
		return;
	}

	if (b->flags & V4L2_BUF_FLAG_NO_CACHE_INVALIDATE)
		vb->skip_cache_sync_on_finish = 1;

	if (b->flags & V4L2_BUF_FLAG_NO_CACHE_CLEAN)
		vb->skip_cache_sync_on_prepare = 1;
}

static int vb2_queue_or_prepare_buf(struct vb2_queue *q, struct media_device *mdev,
				    struct v4l2_buffer *b, bool is_prepare,
				    struct media_request **p_req)
{
	const char *opname = is_prepare ? "prepare_buf" : "qbuf";
	struct media_request *req;
	struct vb2_v4l2_buffer *vbuf;
	struct vb2_buffer *vb;
	int ret;

	if (b->type != q->type) {
		dprintk(q, 1, "%s: invalid buffer type\n", opname);
		return -EINVAL;
	}

	if (b->index >= q->num_buffers) {
		dprintk(q, 1, "%s: buffer index out of range\n", opname);
		return -EINVAL;
	}

	if (q->bufs[b->index] == NULL) {
		/* Should never happen */
		dprintk(q, 1, "%s: buffer is NULL\n", opname);
		return -EINVAL;
	}

	if (b->memory != q->memory) {
		dprintk(q, 1, "%s: invalid memory type\n", opname);
		return -EINVAL;
	}

	vb = q->bufs[b->index];
	vbuf = to_vb2_v4l2_buffer(vb);
	ret = __verify_planes_array(vb, b);
	if (ret)
		return ret;

	if (!is_prepare && (b->flags & V4L2_BUF_FLAG_REQUEST_FD) &&
	    vb->state != VB2_BUF_STATE_DEQUEUED) {
		dprintk(q, 1, "%s: buffer is not in dequeued state\n", opname);
		return -EINVAL;
	}

	if (!vb->prepared) {
		set_buffer_cache_hints(q, vb, b);
		/* Copy relevant information provided by the userspace */
		memset(vbuf->planes, 0,
		       sizeof(vbuf->planes[0]) * vb->num_planes);
		ret = vb2_fill_vb2_v4l2_buffer(vb, b);
		if (ret)
			return ret;
	}

	if (is_prepare)
		return 0;

	if (!(b->flags & V4L2_BUF_FLAG_REQUEST_FD)) {
		if (q->requires_requests) {
			dprintk(q, 1, "%s: queue requires requests\n", opname);
			return -EBADR;
		}
		if (q->uses_requests) {
			dprintk(q, 1, "%s: queue uses requests\n", opname);
			return -EBUSY;
		}
		return 0;
	} else if (!q->supports_requests) {
		dprintk(q, 1, "%s: queue does not support requests\n", opname);
		return -EBADR;
	} else if (q->uses_qbuf) {
		dprintk(q, 1, "%s: queue does not use requests\n", opname);
		return -EBUSY;
	}

	/*
	 * For proper locking when queueing a request you need to be able
	 * to lock access to the vb2 queue, so check that there is a lock
	 * that we can use. In addition p_req must be non-NULL.
	 */
	if (WARN_ON(!q->lock || !p_req))
		return -EINVAL;

	/*
	 * Make sure this op is implemented by the driver. It's easy to forget
	 * this callback, but is it important when canceling a buffer in a
	 * queued request.
	 */
	if (WARN_ON(!q->ops->buf_request_complete))
		return -EINVAL;
	/*
	 * Make sure this op is implemented by the driver for the output queue.
	 * It's easy to forget this callback, but is it important to correctly
	 * validate the 'field' value at QBUF time.
	 */
	if (WARN_ON((q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
		     q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
		    !q->ops->buf_out_validate))
		return -EINVAL;

	req = media_request_get_by_fd(mdev, b->request_fd);
	if (IS_ERR(req)) {
		dprintk(q, 1, "%s: invalid request_fd\n", opname);
		return PTR_ERR(req);
	}

	/*
	 * Early sanity check. This is checked again when the buffer
	 * is bound to the request in vb2_core_qbuf().
	 */
	if (req->state != MEDIA_REQUEST_STATE_IDLE &&
	    req->state != MEDIA_REQUEST_STATE_UPDATING) {
		dprintk(q, 1, "%s: request is not idle\n", opname);
		media_request_put(req);
		return -EBUSY;
	}

	*p_req = req;
	vbuf->request_fd = b->request_fd;

	return 0;
}

/*
 * __fill_v4l2_buffer() - fill in a struct v4l2_buffer with information to be
 * returned to userspace
 */
static void __fill_v4l2_buffer(struct vb2_buffer *vb, void *pb)
{
	struct v4l2_buffer *b = pb;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *q = vb->vb2_queue;
	unsigned int plane;

	/* Copy back data such as timestamp, flags, etc. */
	b->index = vb->index;
	b->type = vb->type;
	b->memory = vb->memory;
	b->bytesused = 0;

	b->flags = vbuf->flags;
	b->field = vbuf->field;
	v4l2_buffer_set_timestamp(b, vb->timestamp);
	b->timecode = vbuf->timecode;
	b->sequence = vbuf->sequence;
	b->reserved2 = 0;
	b->request_fd = 0;

	if (q->is_multiplanar) {
		/*
		 * Fill in plane-related data if userspace provided an array
		 * for it. The caller has already verified memory and size.
		 */
		b->length = vb->num_planes;
		for (plane = 0; plane < vb->num_planes; ++plane) {
			struct v4l2_plane *pdst = &b->m.planes[plane];
			struct vb2_plane *psrc = &vb->planes[plane];

			pdst->bytesused = psrc->bytesused;
			pdst->length = psrc->length;
			if (q->memory == VB2_MEMORY_MMAP)
				pdst->m.mem_offset = psrc->m.offset;
			else if (q->memory == VB2_MEMORY_USERPTR)
				pdst->m.userptr = psrc->m.userptr;
			else if (q->memory == VB2_MEMORY_DMABUF)
				pdst->m.fd = psrc->m.fd;
			pdst->data_offset = psrc->data_offset;
			memset(pdst->reserved, 0, sizeof(pdst->reserved));
		}
	} else {
		/*
		 * We use length and offset in v4l2_planes array even for
		 * single-planar buffers, but userspace does not.
		 */
		b->length = vb->planes[0].length;
		b->bytesused = vb->planes[0].bytesused;
		if (q->memory == VB2_MEMORY_MMAP)
			b->m.offset = vb->planes[0].m.offset;
		else if (q->memory == VB2_MEMORY_USERPTR)
			b->m.userptr = vb->planes[0].m.userptr;
		else if (q->memory == VB2_MEMORY_DMABUF)
			b->m.fd = vb->planes[0].m.fd;
	}

	/*
	 * Clear any buffer state related flags.
	 */
	b->flags &= ~V4L2_BUFFER_MASK_FLAGS;
	b->flags |= q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
	if (!q->copy_timestamp) {
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
	case VB2_BUF_STATE_IN_REQUEST:
		b->flags |= V4L2_BUF_FLAG_IN_REQUEST;
		break;
	case VB2_BUF_STATE_ERROR:
		b->flags |= V4L2_BUF_FLAG_ERROR;
		fallthrough;
	case VB2_BUF_STATE_DONE:
		b->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case VB2_BUF_STATE_PREPARING:
	case VB2_BUF_STATE_DEQUEUED:
		/* nothing */
		break;
	}

	if ((vb->state == VB2_BUF_STATE_DEQUEUED ||
	     vb->state == VB2_BUF_STATE_IN_REQUEST) &&
	    vb->synced && vb->prepared)
		b->flags |= V4L2_BUF_FLAG_PREPARED;

	if (vb2_buffer_in_use(q, vb))
		b->flags |= V4L2_BUF_FLAG_MAPPED;
	if (vbuf->request_fd >= 0) {
		b->flags |= V4L2_BUF_FLAG_REQUEST_FD;
		b->request_fd = vbuf->request_fd;
	}
}

/*
 * __fill_vb2_buffer() - fill a vb2_buffer with information provided in a
 * v4l2_buffer by the userspace. It also verifies that struct
 * v4l2_buffer has a valid number of planes.
 */
static int __fill_vb2_buffer(struct vb2_buffer *vb, struct vb2_plane *planes)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	unsigned int plane;

	if (!vb->vb2_queue->copy_timestamp)
		vb->timestamp = 0;

	for (plane = 0; plane < vb->num_planes; ++plane) {
		if (vb->vb2_queue->memory != VB2_MEMORY_MMAP) {
			planes[plane].m = vbuf->planes[plane].m;
			planes[plane].length = vbuf->planes[plane].length;
		}
		planes[plane].bytesused = vbuf->planes[plane].bytesused;
		planes[plane].data_offset = vbuf->planes[plane].data_offset;
	}
	return 0;
}

static const struct vb2_buf_ops v4l2_buf_ops = {
	.verify_planes_array	= __verify_planes_array_core,
	.init_buffer		= __init_vb2_v4l2_buffer,
	.fill_user_buffer	= __fill_v4l2_buffer,
	.fill_vb2_buffer	= __fill_vb2_buffer,
	.copy_timestamp		= __copy_timestamp,
};

int vb2_find_timestamp(const struct vb2_queue *q, u64 timestamp,
		       unsigned int start_idx)
{
	unsigned int i;

	for (i = start_idx; i < q->num_buffers; i++)
		if (q->bufs[i]->copied_timestamp &&
		    q->bufs[i]->timestamp == timestamp)
			return i;
	return -1;
}
EXPORT_SYMBOL_GPL(vb2_find_timestamp);

/*
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
		dprintk(q, 1, "wrong buffer type\n");
		return -EINVAL;
	}

	if (b->index >= q->num_buffers) {
		dprintk(q, 1, "buffer index out of range\n");
		return -EINVAL;
	}
	vb = q->bufs[b->index];
	ret = __verify_planes_array(vb, b);
	if (!ret)
		vb2_core_querybuf(q, b->index, b);
	return ret;
}
EXPORT_SYMBOL(vb2_querybuf);

static void fill_buf_caps(struct vb2_queue *q, u32 *caps)
{
	*caps = V4L2_BUF_CAP_SUPPORTS_ORPHANED_BUFS;
	if (q->io_modes & VB2_MMAP)
		*caps |= V4L2_BUF_CAP_SUPPORTS_MMAP;
	if (q->io_modes & VB2_USERPTR)
		*caps |= V4L2_BUF_CAP_SUPPORTS_USERPTR;
	if (q->io_modes & VB2_DMABUF)
		*caps |= V4L2_BUF_CAP_SUPPORTS_DMABUF;
	if (q->subsystem_flags & VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF)
		*caps |= V4L2_BUF_CAP_SUPPORTS_M2M_HOLD_CAPTURE_BUF;
	if (q->allow_cache_hints && q->io_modes & VB2_MMAP)
		*caps |= V4L2_BUF_CAP_SUPPORTS_MMAP_CACHE_HINTS;
#ifdef CONFIG_MEDIA_CONTROLLER_REQUEST_API
	if (q->supports_requests)
		*caps |= V4L2_BUF_CAP_SUPPORTS_REQUESTS;
#endif
}

static void validate_memory_flags(struct vb2_queue *q,
				  int memory,
				  u32 *flags)
{
	if (!q->allow_cache_hints || memory != V4L2_MEMORY_MMAP) {
		/*
		 * This needs to clear V4L2_MEMORY_FLAG_NON_COHERENT only,
		 * but in order to avoid bugs we zero out all bits.
		 */
		*flags = 0;
	} else {
		/* Clear all unknown flags. */
		*flags &= V4L2_MEMORY_FLAG_NON_COHERENT;
	}
}

int vb2_reqbufs(struct vb2_queue *q, struct v4l2_requestbuffers *req)
{
	int ret = vb2_verify_memory_type(q, req->memory, req->type);
	u32 flags = req->flags;

	fill_buf_caps(q, &req->capabilities);
	validate_memory_flags(q, req->memory, &flags);
	req->flags = flags;
	return ret ? ret : vb2_core_reqbufs(q, req->memory,
					    req->flags, &req->count);
}
EXPORT_SYMBOL_GPL(vb2_reqbufs);

int vb2_prepare_buf(struct vb2_queue *q, struct media_device *mdev,
		    struct v4l2_buffer *b)
{
	int ret;

	if (vb2_fileio_is_active(q)) {
		dprintk(q, 1, "file io in progress\n");
		return -EBUSY;
	}

	if (b->flags & V4L2_BUF_FLAG_REQUEST_FD)
		return -EINVAL;

	ret = vb2_queue_or_prepare_buf(q, mdev, b, true, NULL);

	return ret ? ret : vb2_core_prepare_buf(q, b->index, b);
}
EXPORT_SYMBOL_GPL(vb2_prepare_buf);

int vb2_create_bufs(struct vb2_queue *q, struct v4l2_create_buffers *create)
{
	unsigned requested_planes = 1;
	unsigned requested_sizes[VIDEO_MAX_PLANES];
	struct v4l2_format *f = &create->format;
	int ret = vb2_verify_memory_type(q, create->memory, f->type);
	unsigned i;

	fill_buf_caps(q, &create->capabilities);
	validate_memory_flags(q, create->memory, &create->flags);
	create->index = q->num_buffers;
	if (create->count == 0)
		return ret != -EBUSY ? ret : 0;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		requested_planes = f->fmt.pix_mp.num_planes;
		if (requested_planes == 0 ||
		    requested_planes > VIDEO_MAX_PLANES)
			return -EINVAL;
		for (i = 0; i < requested_planes; i++)
			requested_sizes[i] =
				f->fmt.pix_mp.plane_fmt[i].sizeimage;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		requested_sizes[0] = f->fmt.pix.sizeimage;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		requested_sizes[0] = f->fmt.vbi.samples_per_line *
			(f->fmt.vbi.count[0] + f->fmt.vbi.count[1]);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		requested_sizes[0] = f->fmt.sliced.io_size;
		break;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		requested_sizes[0] = f->fmt.sdr.buffersize;
		break;
	case V4L2_BUF_TYPE_META_CAPTURE:
	case V4L2_BUF_TYPE_META_OUTPUT:
		requested_sizes[0] = f->fmt.meta.buffersize;
		break;
	default:
		return -EINVAL;
	}
	for (i = 0; i < requested_planes; i++)
		if (requested_sizes[i] == 0)
			return -EINVAL;
	return ret ? ret : vb2_core_create_bufs(q, create->memory,
						create->flags,
						&create->count,
						requested_planes,
						requested_sizes);
}
EXPORT_SYMBOL_GPL(vb2_create_bufs);

int vb2_qbuf(struct vb2_queue *q, struct media_device *mdev,
	     struct v4l2_buffer *b)
{
	struct media_request *req = NULL;
	int ret;

	if (vb2_fileio_is_active(q)) {
		dprintk(q, 1, "file io in progress\n");
		return -EBUSY;
	}

	ret = vb2_queue_or_prepare_buf(q, mdev, b, false, &req);
	if (ret)
		return ret;
	ret = vb2_core_qbuf(q, b->index, b, req);
	if (req)
		media_request_put(req);
	return ret;
}
EXPORT_SYMBOL_GPL(vb2_qbuf);

int vb2_dqbuf(struct vb2_queue *q, struct v4l2_buffer *b, bool nonblocking)
{
	int ret;

	if (vb2_fileio_is_active(q)) {
		dprintk(q, 1, "file io in progress\n");
		return -EBUSY;
	}

	if (b->type != q->type) {
		dprintk(q, 1, "invalid buffer type\n");
		return -EINVAL;
	}

	ret = vb2_core_dqbuf(q, NULL, b, nonblocking);

	if (!q->is_output &&
	    b->flags & V4L2_BUF_FLAG_DONE &&
	    b->flags & V4L2_BUF_FLAG_LAST)
		q->last_buffer_dequeued = true;

	/*
	 *  After calling the VIDIOC_DQBUF V4L2_BUF_FLAG_DONE must be
	 *  cleared.
	 */
	b->flags &= ~V4L2_BUF_FLAG_DONE;

	return ret;
}
EXPORT_SYMBOL_GPL(vb2_dqbuf);

int vb2_streamon(struct vb2_queue *q, enum v4l2_buf_type type)
{
	if (vb2_fileio_is_active(q)) {
		dprintk(q, 1, "file io in progress\n");
		return -EBUSY;
	}
	return vb2_core_streamon(q, type);
}
EXPORT_SYMBOL_GPL(vb2_streamon);

int vb2_streamoff(struct vb2_queue *q, enum v4l2_buf_type type)
{
	if (vb2_fileio_is_active(q)) {
		dprintk(q, 1, "file io in progress\n");
		return -EBUSY;
	}
	return vb2_core_streamoff(q, type);
}
EXPORT_SYMBOL_GPL(vb2_streamoff);

int vb2_expbuf(struct vb2_queue *q, struct v4l2_exportbuffer *eb)
{
	return vb2_core_expbuf(q, &eb->fd, eb->type, eb->index,
				eb->plane, eb->flags);
}
EXPORT_SYMBOL_GPL(vb2_expbuf);

int vb2_queue_init_name(struct vb2_queue *q, const char *name)
{
	/*
	 * Sanity check
	 */
	if (WARN_ON(!q)			  ||
	    WARN_ON(q->timestamp_flags &
		    ~(V4L2_BUF_FLAG_TIMESTAMP_MASK |
		      V4L2_BUF_FLAG_TSTAMP_SRC_MASK)))
		return -EINVAL;

	/* Warn that the driver should choose an appropriate timestamp type */
	WARN_ON((q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
		V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN);

	/* Warn that vb2_memory should match with v4l2_memory */
	if (WARN_ON(VB2_MEMORY_MMAP != (int)V4L2_MEMORY_MMAP)
		|| WARN_ON(VB2_MEMORY_USERPTR != (int)V4L2_MEMORY_USERPTR)
		|| WARN_ON(VB2_MEMORY_DMABUF != (int)V4L2_MEMORY_DMABUF))
		return -EINVAL;

	if (q->buf_struct_size == 0)
		q->buf_struct_size = sizeof(struct vb2_v4l2_buffer);

	q->buf_ops = &v4l2_buf_ops;
	q->is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(q->type);
	q->is_output = V4L2_TYPE_IS_OUTPUT(q->type);
	q->copy_timestamp = (q->timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK)
			== V4L2_BUF_FLAG_TIMESTAMP_COPY;
	/*
	 * For compatibility with vb1: if QBUF hasn't been called yet, then
	 * return EPOLLERR as well. This only affects capture queues, output
	 * queues will always initialize waiting_for_buffers to false.
	 */
	q->quirk_poll_must_check_waiting_for_buffers = true;

	if (name)
		strscpy(q->name, name, sizeof(q->name));
	else
		q->name[0] = '\0';

	return vb2_core_queue_init(q);
}
EXPORT_SYMBOL_GPL(vb2_queue_init_name);

int vb2_queue_init(struct vb2_queue *q)
{
	return vb2_queue_init_name(q, NULL);
}
EXPORT_SYMBOL_GPL(vb2_queue_init);

void vb2_queue_release(struct vb2_queue *q)
{
	vb2_core_queue_release(q);
}
EXPORT_SYMBOL_GPL(vb2_queue_release);

int vb2_queue_change_type(struct vb2_queue *q, unsigned int type)
{
	if (type == q->type)
		return 0;

	if (vb2_is_busy(q))
		return -EBUSY;

	q->type = type;

	return 0;
}
EXPORT_SYMBOL_GPL(vb2_queue_change_type);

__poll_t vb2_poll(struct vb2_queue *q, struct file *file, poll_table *wait)
{
	struct video_device *vfd = video_devdata(file);
	__poll_t res;

	res = vb2_core_poll(q, file, wait);

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags)) {
		struct v4l2_fh *fh = file->private_data;

		poll_wait(file, &fh->wait, wait);
		if (v4l2_event_pending(fh))
			res |= EPOLLPRI;
	}

	return res;
}
EXPORT_SYMBOL_GPL(vb2_poll);

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
	int res = vb2_verify_memory_type(vdev->queue, p->memory, p->type);
	u32 flags = p->flags;

	fill_buf_caps(vdev->queue, &p->capabilities);
	validate_memory_flags(vdev->queue, p->memory, &flags);
	p->flags = flags;
	if (res)
		return res;
	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;
	res = vb2_core_reqbufs(vdev->queue, p->memory, p->flags, &p->count);
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
	int res = vb2_verify_memory_type(vdev->queue, p->memory,
			p->format.type);

	p->index = vdev->queue->num_buffers;
	fill_buf_caps(vdev->queue, &p->capabilities);
	validate_memory_flags(vdev->queue, p->memory, &p->flags);
	/*
	 * If count == 0, then just check if memory and type are valid.
	 * Any -EBUSY result from vb2_verify_memory_type can be mapped to 0.
	 */
	if (p->count == 0)
		return res != -EBUSY ? res : 0;
	if (res)
		return res;
	if (vb2_queue_is_busy(vdev, file))
		return -EBUSY;

	res = vb2_create_bufs(vdev->queue, p);
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
	return vb2_prepare_buf(vdev->queue, vdev->v4l2_dev->mdev, p);
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
	return vb2_qbuf(vdev->queue, vdev->v4l2_dev->mdev, p);
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

	return vb2_mmap(vdev->queue, vma);
}
EXPORT_SYMBOL_GPL(vb2_fop_mmap);

int _vb2_fop_release(struct file *file, struct mutex *lock)
{
	struct video_device *vdev = video_devdata(file);

	if (lock)
		mutex_lock(lock);
	if (file->private_data == vdev->queue->owner) {
		vb2_queue_release(vdev->queue);
		vdev->queue->owner = NULL;
	}
	if (lock)
		mutex_unlock(lock);
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

	if (!(vdev->queue->io_modes & VB2_WRITE))
		return -EINVAL;
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

	if (!(vdev->queue->io_modes & VB2_READ))
		return -EINVAL;
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

__poll_t vb2_fop_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct vb2_queue *q = vdev->queue;
	struct mutex *lock = q->lock ? q->lock : vdev->lock;
	__poll_t res;
	void *fileio;

	/*
	 * If this helper doesn't know how to lock, then you shouldn't be using
	 * it but you should write your own.
	 */
	WARN_ON(!lock);

	if (lock && mutex_lock_interruptible(lock))
		return EPOLLERR;

	fileio = q->fileio;

	res = vb2_poll(vdev->queue, file, wait);

	/* If fileio was started, then we have a new queue owner. */
	if (!fileio && q->fileio)
		q->owner = file->private_data;
	if (lock)
		mutex_unlock(lock);
	return res;
}
EXPORT_SYMBOL_GPL(vb2_fop_poll);

#ifndef CONFIG_MMU
unsigned long vb2_fop_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct video_device *vdev = video_devdata(file);

	return vb2_get_unmapped_area(vdev->queue, addr, len, pgoff, flags);
}
EXPORT_SYMBOL_GPL(vb2_fop_get_unmapped_area);
#endif

void vb2_video_unregister_device(struct video_device *vdev)
{
	/* Check if vdev was ever registered at all */
	if (!vdev || !video_is_registered(vdev))
		return;

	/*
	 * Calling this function only makes sense if vdev->queue is set.
	 * If it is NULL, then just call video_unregister_device() instead.
	 */
	WARN_ON(!vdev->queue);

	/*
	 * Take a reference to the device since video_unregister_device()
	 * calls device_unregister(), but we don't want that to release
	 * the device since we want to clean up the queue first.
	 */
	get_device(&vdev->dev);
	video_unregister_device(vdev);
	if (vdev->queue && vdev->queue->owner) {
		struct mutex *lock = vdev->queue->lock ?
			vdev->queue->lock : vdev->lock;

		if (lock)
			mutex_lock(lock);
		vb2_queue_release(vdev->queue);
		vdev->queue->owner = NULL;
		if (lock)
			mutex_unlock(lock);
	}
	/*
	 * Now we put the device, and in most cases this will release
	 * everything.
	 */
	put_device(&vdev->dev);
}
EXPORT_SYMBOL_GPL(vb2_video_unregister_device);

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

/*
 * Note that this function is called during validation time and
 * thus the req_queue_mutex is held to ensure no request objects
 * can be added or deleted while validating. So there is no need
 * to protect the objects list.
 */
int vb2_request_validate(struct media_request *req)
{
	struct media_request_object *obj;
	int ret = 0;

	if (!vb2_request_buffer_cnt(req))
		return -ENOENT;

	list_for_each_entry(obj, &req->objects, list) {
		if (!obj->ops->prepare)
			continue;

		ret = obj->ops->prepare(obj);
		if (ret)
			break;
	}

	if (ret) {
		list_for_each_entry_continue_reverse(obj, &req->objects, list)
			if (obj->ops->unprepare)
				obj->ops->unprepare(obj);
		return ret;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_request_validate);

void vb2_request_queue(struct media_request *req)
{
	struct media_request_object *obj, *obj_safe;

	/*
	 * Queue all objects. Note that buffer objects are at the end of the
	 * objects list, after all other object types. Once buffer objects
	 * are queued, the driver might delete them immediately (if the driver
	 * processes the buffer at once), so we have to use
	 * list_for_each_entry_safe() to handle the case where the object we
	 * queue is deleted.
	 */
	list_for_each_entry_safe(obj, obj_safe, &req->objects, list)
		if (obj->ops->queue)
			obj->ops->queue(obj);
}
EXPORT_SYMBOL_GPL(vb2_request_queue);

MODULE_DESCRIPTION("Driver helper framework for Video for Linux 2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>, Marek Szyprowski");
MODULE_LICENSE("GPL");
