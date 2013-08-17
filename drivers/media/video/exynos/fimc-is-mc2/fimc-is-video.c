/*
* Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/videonode.h>
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include <media/videobuf2-core.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/exynos_mc.h>

#include "fimc-is-time.h"
#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"

#define SPARE_PLANE 1
#define SPARE_SIZE (16 * 1024)

struct fimc_is_fmt fimc_is_formats[] = {
	 {
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.num_planes	= 1 + SPARE_PLANE,
		.mbus_code	= V4L2_MBUS_FMT_YUYV8_2X8,
	}, {
		.name		= "YUV 4:2:2 packed, CbYCrY",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.num_planes	= 1 + SPARE_PLANE,
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
	}, {
		.name		= "YUV 4:2:2 planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 planar, YCbCr",
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 planar, YCbCr",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.num_planes	= 2 + SPARE_PLANE,
	}, {
		.name		= "YVU 4:2:0 non-contiguous 2-planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21M,
		.num_planes	= 2 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.num_planes	= 3 + SPARE_PLANE,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cr/Cb",
		.pixelformat	= V4L2_PIX_FMT_YVU420M,
		.num_planes	= 3 + SPARE_PLANE,
	}, {
		.name		= "BAYER 10 bit",
		.pixelformat	= V4L2_PIX_FMT_SBGGR10,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "BAYER 12 bit",
		.pixelformat	= V4L2_PIX_FMT_SBGGR12,
		.num_planes	= 1 + SPARE_PLANE,
	}, {
		.name		= "BAYER 16 bit",
		.pixelformat	= V4L2_PIX_FMT_SBGGR16,
		.num_planes	= 1 + SPARE_PLANE,
	},
};

struct fimc_is_fmt *fimc_is_find_format(u32 *pixelformat,
	u32 *mbus_code, int index)
{
	struct fimc_is_fmt *fmt, *def_fmt = NULL;
	unsigned int i;

	if (index >= ARRAY_SIZE(fimc_is_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(fimc_is_formats); ++i) {
		fmt = &fimc_is_formats[i];
		if (pixelformat && fmt->pixelformat == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == i)
			def_fmt = fmt;
	}
	return def_fmt;

}

void fimc_is_set_plane_size(struct fimc_is_frame_cfg *frame, unsigned int sizes[])
{
	u32 plane;
	u32 width[FIMC_IS_MAX_PLANES];

	for (plane = 0; plane < FIMC_IS_MAX_PLANES; ++plane)
		width[plane] = frame->width + frame->width_stride[plane];

	switch (frame->format.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		dbg("V4L2_PIX_FMT_YUYV(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = width[0]*frame->height*2;
		sizes[1] = SPARE_SIZE;
		break;
	case V4L2_PIX_FMT_NV12M:
		dbg("V4L2_PIX_FMT_NV12M(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = width[0]*frame->height;
		sizes[1] = width[1]*frame->height/2;
		sizes[2] = SPARE_SIZE;
		break;
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV21:
		dbg("V4L2_PIX_FMT_NV21(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = width[0]*frame->height;
		sizes[1] = width[1]*frame->height/2;
		sizes[2] = SPARE_SIZE;
		break;
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
		dbg("V4L2_PIX_FMT_YVU420M(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = width[0]*frame->height;
		sizes[1] = width[1]*frame->height/4;
		sizes[2] = width[2]*frame->height/4;
		sizes[3] = SPARE_SIZE;
		break;
	case V4L2_PIX_FMT_SBGGR10:
		dbg("V4L2_PIX_FMT_SBGGR10(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = frame->width*frame->height*2;
		sizes[1] = SPARE_SIZE;
		break;
	case V4L2_PIX_FMT_SBGGR16:
		dbg("V4L2_PIX_FMT_SBGGR16(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = frame->width*frame->height*2;
		sizes[1] = SPARE_SIZE;
		break;
	case V4L2_PIX_FMT_SBGGR12:
		dbg("V4L2_PIX_FMT_SBGGR12(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] = frame->width*frame->height*2;
		sizes[1] = SPARE_SIZE;
		break;
	default:
		err("unknown pixelformat\n");
		break;
	}
}

struct fimc_is_core * fimc_is_video_ctx_2_core(struct fimc_is_video_ctx *vctx)
{
	return (struct fimc_is_core *)vctx->video->core;
}

static inline void vref_init(struct fimc_is_video *video)
{
	atomic_set(&video->refcount, 0);
}

static inline int vref_get(struct fimc_is_video *video)
{
	return atomic_inc_return(&video->refcount) - 1;
}

static inline int vref_put(struct fimc_is_video *video,
	void (*release)(struct fimc_is_video *video))
{
	int ret = 0;

	ret = atomic_sub_and_test(1, &video->refcount);
	if (ret)
		pr_debug("closed all instacne");

	return atomic_read(&video->refcount);
}

static int queue_init(void *priv, struct vb2_queue *vbq_src,
	struct vb2_queue *vbq_dst)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = priv;

	BUG_ON(!vctx);

	if (vctx->type == FIMC_IS_VIDEO_TYPE_OUTPUT) {
		BUG_ON(!vbq_src);

		vbq_src->type		= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		vbq_src->io_modes	= VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		vbq_src->drv_priv	= vctx;
		vbq_src->ops		= vctx->vb2_ops;
		vbq_src->mem_ops	= vctx->mem_ops;
		vb2_queue_init(vbq_src);
		vctx->q_src.vbq = vbq_src;
	} else if (vctx->type == FIMC_IS_VIDEO_TYPE_CAPTURE) {
		BUG_ON(!vbq_dst);

		vbq_dst->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		vbq_dst->io_modes	= VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		vbq_dst->drv_priv	= vctx;
		vbq_dst->ops		= vctx->vb2_ops;
		vbq_dst->mem_ops	= vctx->mem_ops;
		vb2_queue_init(vbq_dst);
		vctx->q_dst.vbq = vbq_dst;
	} else if (vctx->type == FIMC_IS_VIDEO_TYPE_M2M) {
		BUG_ON(!vbq_src);
		BUG_ON(!vbq_dst);

		vbq_src->type		= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		vbq_src->io_modes	= VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		vbq_src->drv_priv	= vctx;
		vbq_src->ops		= vctx->vb2_ops;
		vbq_src->mem_ops	= vctx->mem_ops;
		vb2_queue_init(vbq_src);
		vctx->q_src.vbq = vbq_src;

		vbq_dst->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		vbq_dst->io_modes	= VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		vbq_dst->drv_priv	= vctx;
		vbq_dst->ops		= vctx->vb2_ops;
		vbq_dst->mem_ops	= vctx->mem_ops;
		vb2_queue_init(vbq_dst);
		vctx->q_dst.vbq = vbq_dst;
	} else {
		merr("video type is invalid(%d)", vctx, vctx->type);
		ret = -EINVAL;
	}

	return ret;
}

int open_vctx(struct file *file,
	struct fimc_is_video *video,
	struct fimc_is_video_ctx **vctx,
	u32 id_src, u32 id_dst)
{
	int ret = 0;

	BUG_ON(!file);
	BUG_ON(!video);

	if (atomic_read(&video->refcount) > FIMC_IS_MAX_NODES) {
		err("can't open vctx, refcount is invalid");
		ret = -EINVAL;
		goto exit;
	}

	*vctx = kzalloc(sizeof(struct fimc_is_video_ctx), GFP_KERNEL);
	if (*vctx == NULL) {
		err("kzalloc is fail");
		ret = -ENOMEM;
		*vctx = NULL;
		goto exit;
	}

	(*vctx)->instance = vref_get(video);
	(*vctx)->q_src.id = id_src;
	(*vctx)->q_src.instance = (*vctx)->instance;
	(*vctx)->q_dst.id = id_dst;
	(*vctx)->q_dst.instance = (*vctx)->instance;

	file->private_data = *vctx;

exit:
	return ret;
}

int close_vctx(struct file *file,
	struct fimc_is_video *video,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;

	kfree(vctx);
	file->private_data = NULL;
	ret = vref_put(video, NULL);

	return ret;
}

/*
 * =============================================================================
 * Queue Opertation
 * =============================================================================
 */

static int fimc_is_queue_open(struct fimc_is_queue *queue,
	u32 rdycount)
{
	int ret = 0;

	queue->buf_maxcount = 0;
	queue->buf_refcount = 0;
	queue->buf_rdycount = rdycount;
	clear_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);
	clear_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
	clear_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state);
	memset(&queue->framecfg, 0, sizeof(struct fimc_is_frame_cfg));

	return ret;
}

static int fimc_is_queue_close(struct fimc_is_queue *queue)
{
	int ret = 0;

	queue->buf_maxcount = 0;
	queue->buf_refcount = 0;
	clear_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);
	clear_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
	clear_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state);
	fimc_is_frame_close(&queue->framemgr);

	return ret;
}

static int fimc_is_queue_set_format_mplane(struct fimc_is_queue *queue,
	struct v4l2_format *format)
{
	int ret = 0;
	u32 plane;
	struct v4l2_pix_format_mplane *pix;
	struct fimc_is_fmt *fmt;

	pix = &format->fmt.pix_mp;
	fmt = fimc_is_find_format(&pix->pixelformat, NULL, 0);
	if (!fmt) {
		err("pixel format is not found\n");
		ret = -EINVAL;
		goto p_err;
	}

	queue->framecfg.format.pixelformat	= fmt->pixelformat;
	queue->framecfg.format.mbus_code	= fmt->mbus_code;
	queue->framecfg.format.num_planes	= fmt->num_planes;
	queue->framecfg.width			= pix->width;
	queue->framecfg.height			= pix->height;

	for (plane = 0; plane < fmt->num_planes; ++plane) {
		if (pix->plane_fmt[plane].bytesperline)
			queue->framecfg.width_stride[plane] =
				pix->plane_fmt[plane].bytesperline - pix->width;
		else
			queue->framecfg.width_stride[plane] = 0;
	}

p_err:
	return ret;
}

int fimc_is_queue_setup(struct fimc_is_queue *queue,
	void *alloc_ctx,
	unsigned int *num_planes,
	unsigned int sizes[],
	void *allocators[])
{
	u32 ret = 0;
	u32 plane;

	*num_planes = (unsigned int)(queue->framecfg.format.num_planes);
	fimc_is_set_plane_size(&queue->framecfg, sizes);

	for (plane = 0; plane < *num_planes; plane++) {
		allocators[plane] = alloc_ctx;
		queue->framecfg.size[plane] = sizes[plane];
		mdbgv_vid("queue[%d] size : %d\n", queue, plane, sizes[plane]);
	}

	return ret;
}

int fimc_is_queue_buffer_queue(struct fimc_is_queue *queue,
	const struct fimc_is_vb2 *vb2,
	struct vb2_buffer *vb)
{
	u32 ret = 0, i;
	u32 index;
	u32 ext_size;
	u32 spare;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	index = vb->v4l2_buf.index;
	framemgr = &queue->framemgr;

	BUG_ON(framemgr->id == FRAMEMGR_ID_INVALID);

	/* plane address is updated for checking everytime */
	if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_YVU420M) {
		queue->buf_dva[index][0] = vb2->plane_addr(vb, 0);
		queue->buf_dva[index][1] = vb2->plane_addr(vb, 2);
		queue->buf_dva[index][2] = vb2->plane_addr(vb, 1);
		queue->buf_dva[index][3] = vb2->plane_addr(vb, 3);
	} else {
		for (i = 0; i < vb->num_planes; i++)
			queue->buf_dva[index][i] = vb2->plane_addr(vb, i);
	}

	frame = &framemgr->frame[index];

	/* uninitialized frame need to get info */
	if (frame->init == FRAME_UNI_MEM)
		goto set_info;

	/* plane count check */
	if (frame->planes != vb->num_planes) {
		err("plane count is changed(%08X != %08X)",
			frame->planes, vb->num_planes);
		ret = -EINVAL;
		goto exit;
	}

	/* plane address check */
	for (i = 0; i < frame->planes; i++) {
		if (frame->dvaddr_buffer[i] != queue->buf_dva[index][i]) {
			err("buffer %d plane %d is changed(%08X != %08X)",
				index, i,
				frame->dvaddr_buffer[i],
				queue->buf_dva[index][i]);
			ret = -EINVAL;
			goto exit;
		}
	}

	goto exit;

set_info:
	if (test_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state)) {
		err("already prepared but new index(%d) is came", index);
		ret = -EINVAL;
		goto exit;
	}

	frame->vb = vb;
	frame->planes = vb->num_planes;
	spare = frame->planes - 1;

	for (i = 0; i < frame->planes; i++) {
		frame->dvaddr_buffer[i] = queue->buf_dva[index][i];
#ifdef PRINT_BUFADDR
		pr_info("%04X %d.%d %08X\n", framemgr->id,
			index, i, frame->dvaddr_buffer[i]);
#endif
	}

	if (framemgr->id & FRAMEMGR_ID_SHOT) {
		ext_size = sizeof(struct camera2_shot_ext) -
			sizeof(struct camera2_shot);

		/* Create Kvaddr for Metadata */
		queue->buf_kva[index][spare] = vb2->plane_kvaddr(vb, spare);
		if (!queue->buf_kva[index][spare]) {
			err("plane_kvaddr is fail(%08X)", framemgr->id);
			ret = -EINVAL;
			goto exit;
		}

		frame->dvaddr_shot = queue->buf_dva[index][spare] + ext_size;
		frame->kvaddr_shot = queue->buf_kva[index][spare] + ext_size;
		frame->cookie_shot = (u32)vb2_plane_cookie(vb, spare);
		frame->shot = (struct camera2_shot *)frame->kvaddr_shot;
		frame->shot_ext = (struct camera2_shot_ext *)
			queue->buf_kva[index][spare];
		frame->shot_size = queue->framecfg.size[spare];
#ifdef MEASURE_TIME
		frame->tzone = (struct timeval *)frame->shot_ext->timeZone;
#endif
	} else {
		/* Create Kvaddr for frame sync */
		queue->buf_kva[index][spare] = vb2->plane_kvaddr(vb, spare);
		if (!queue->buf_kva[index][spare]) {
			err("plane_kvaddr is fail(%08X)", framemgr->id);
			ret = -EINVAL;
			goto exit;
		}

		frame->stream = (struct camera2_stream *)
			queue->buf_kva[index][spare];
		frame->stream->address = queue->buf_kva[index][spare];
		frame->stream_size = queue->framecfg.size[spare];
	}

	frame->init = FRAME_INI_MEM;

	queue->buf_refcount++;

	if (queue->buf_rdycount == queue->buf_refcount)
		set_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);

	if (queue->buf_maxcount == queue->buf_refcount)
		set_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);

exit:
	return ret;
}

inline void fimc_is_queue_wait_prepare(struct vb2_queue *vbq)
{
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_video *video;

	BUG_ON(!vbq);

	vctx = vbq->drv_priv;
	if (!vctx) {
		err("vctx is NULL");
		return;
	}

	video = vctx->video;
	mutex_unlock(&video->lock);
}

inline void fimc_is_queue_wait_finish(struct vb2_queue *vbq)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_video *video;

	BUG_ON(!vbq);

	vctx = vbq->drv_priv;
	if (!vctx) {
		err("vctx is NULL");
		return;
	}

	video = vctx->video;
	ret = mutex_lock_interruptible(&video->lock);
	if (ret)
		err("mutex_lock_interruptible is fail(%d)", ret);
}

int fimc_is_queue_start_streaming(struct fimc_is_queue *queue,
	struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;

	BUG_ON(!queue);
	BUG_ON(!device);
	BUG_ON(!subdev);
	BUG_ON(!vctx);

	if (test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		merr("already stream on(%ld)", vctx, queue->state);
		ret = -EINVAL;
		goto p_err;
	}

	if (!test_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state)) {
		merr("buffer state is not ready(%ld)", vctx, queue->state);
		ret = -EINVAL;
		goto p_err;
	}

	ret = CALL_QOPS(queue, start_streaming, device, subdev, queue);
	if (ret) {
		merr("start_streaming is fail(%d)", vctx, ret);
		ret = -EINVAL;
		goto p_err;
	}

	set_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state);

p_err:
	return ret;
}

int fimc_is_queue_stop_streaming(struct fimc_is_queue *queue,
	struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;

	BUG_ON(!queue);
	BUG_ON(!device);
	BUG_ON(!subdev);
	BUG_ON(!vctx);

	if (!test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		merr("already stream off(%ld)", vctx, queue->state);
		ret = -EINVAL;
		goto p_err;
	}

	ret = CALL_QOPS(queue, stop_streaming, device, subdev, queue);
	if (ret) {
		merr("stop_streaming is fail(%d)", vctx, ret);
		ret = -EINVAL;
		goto p_err;
	}

	clear_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state);
	clear_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
	clear_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);

p_err:
	return ret;
}

int fimc_is_video_probe(struct fimc_is_video *video,
	void *core_data,
	char *video_name,
	u32 video_number,
	struct mutex *lock,
	const struct v4l2_file_operations *fops,
	const struct v4l2_ioctl_ops *ioctl_ops)
{
	int ret = 0;
	struct fimc_is_core *core = core_data;

	vref_init(video);

	mutex_init(&video->lock);
	snprintf(video->vd.name, sizeof(video->vd.name),
		"%s", video_name);

	video->id		= video_number;
	video->core		= core;
	video->vb2		= core->mem.vb2;
	video->vd.fops		= fops;
	video->vd.ioctl_ops	= ioctl_ops;
	video->vd.v4l2_dev	= &core->mdev->v4l2_dev;
	video->vd.minor		= -1;
	video->vd.release	= video_device_release;
	video->vd.lock		= lock;
	video_set_drvdata(&video->vd, core);

	ret = video_register_device(&video->vd,
				VFL_TYPE_GRABBER,
				(EXYNOS_VIDEONODE_FIMC_IS + video_number));
	if (ret) {
		err("Failed to register video device");
		goto p_err;
	}

	video->pads.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_init(&video->vd.entity, 1, &video->pads, 0);
	if (ret) {
		err("Failed to media_entity_init ScalerP video device\n");
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_video_open(struct fimc_is_video_ctx *vctx,
	void *device,
	u32 buf_rdycount,
	struct fimc_is_video *video,
	u32 video_type,
	const struct vb2_ops *vb2_ops,
	const struct fimc_is_queue_ops *src_qops,
	const struct fimc_is_queue_ops *dst_qops,
	const struct vb2_mem_ops *mem_ops)
{
	int ret = 0;
	struct fimc_is_queue *q_src, *q_dst;

	BUG_ON(!video);
	BUG_ON(!vb2_ops);
	BUG_ON(!mem_ops);

	q_src = &vctx->q_src;
	q_dst = &vctx->q_dst;
	q_src->vbq = NULL;
	q_dst->vbq = NULL;
	q_src->qops = src_qops;
	q_dst->qops = dst_qops;

	vctx->type		= video_type;
	vctx->device		= device;
	vctx->video		= video;
	vctx->vb2_ops		= vb2_ops;
	vctx->mem_ops		= mem_ops;
	mutex_init(&vctx->lock);

	switch (video_type) {
	case FIMC_IS_VIDEO_TYPE_OUTPUT:
		fimc_is_queue_open(q_src, buf_rdycount);

		q_src->vbq = kzalloc(sizeof(struct vb2_queue), GFP_KERNEL);
		queue_init(vctx, q_src->vbq, NULL);
		break;
	case FIMC_IS_VIDEO_TYPE_CAPTURE:
		fimc_is_queue_open(q_dst, buf_rdycount);

		q_dst->vbq = kzalloc(sizeof(struct vb2_queue), GFP_KERNEL);
		queue_init(vctx, NULL, q_dst->vbq);
		break;
	case FIMC_IS_VIDEO_TYPE_M2M:
		fimc_is_queue_open(q_src, buf_rdycount);
		fimc_is_queue_open(q_dst, buf_rdycount);

		q_src->vbq = kzalloc(sizeof(struct vb2_queue), GFP_KERNEL);
		q_dst->vbq = kzalloc(sizeof(struct vb2_queue), GFP_KERNEL);
		queue_init(vctx, q_src->vbq, q_dst->vbq);
		break;
	default:
		merr("invalid type(%d)", vctx, video_type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

int fimc_is_video_close(struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	u32 video_type = vctx->type;
	struct fimc_is_queue *q_src, *q_dst;

	BUG_ON(!vctx);

	q_src = &vctx->q_src;
	q_dst = &vctx->q_dst;

	switch (video_type) {
	case FIMC_IS_VIDEO_TYPE_OUTPUT:
		BUG_ON(!q_src->vbq);
		fimc_is_queue_close(q_src);
		vb2_queue_release(q_src->vbq);
		kfree(q_src->vbq);
		break;
	case FIMC_IS_VIDEO_TYPE_CAPTURE:
		BUG_ON(!q_dst->vbq);
		fimc_is_queue_close(q_dst);
		vb2_queue_release(q_dst->vbq);
		kfree(q_dst->vbq);
		break;
	case FIMC_IS_VIDEO_TYPE_M2M:
		BUG_ON(!q_src->vbq);
		BUG_ON(!q_dst->vbq);
		fimc_is_queue_close(q_src);
		fimc_is_queue_close(q_dst);
		kfree(q_src->vbq);
		kfree(q_dst->vbq);
		break;
	default:
		merr("invalid type(%d)", vctx, video_type);
		ret = -EINVAL;
		break;
	}

	/*
	 * vb2 release can call stop callback
	 * not if video node is not stream off
	 */
	vctx->device = NULL;

	return ret;
}

u32 fimc_is_video_poll(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	u32 video_type = vctx->type;

	switch (video_type) {
	case FIMC_IS_VIDEO_TYPE_OUTPUT:
		ret = vb2_poll(vctx->q_src.vbq, file, wait);
		break;
	case FIMC_IS_VIDEO_TYPE_CAPTURE:
		ret = vb2_poll(vctx->q_dst.vbq, file, wait);
		break;
	case FIMC_IS_VIDEO_TYPE_M2M:
		merr("video poll is not supported", vctx);
		ret = -EINVAL;
		break;
	default:
		merr("invalid type(%d)", vctx, video_type);
		break;
	}

	return ret;
}

int fimc_is_video_mmap(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct vm_area_struct *vma)
{
	u32 ret = 0;
	u32 video_type = vctx->type;

	switch (video_type) {
	case FIMC_IS_VIDEO_TYPE_OUTPUT:
		ret = vb2_mmap(vctx->q_src.vbq, vma);
		break;
	case FIMC_IS_VIDEO_TYPE_CAPTURE:
		ret = vb2_mmap(vctx->q_dst.vbq, vma);
		break;
	case FIMC_IS_VIDEO_TYPE_M2M:
		merr("video mmap is not supported", vctx);
		ret = -EINVAL;
		break;
	default:
		merr("invalid type(%d)", vctx, video_type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

int fimc_is_video_reqbufs(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct v4l2_requestbuffers *request)
{
	int ret = 0;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);
	BUG_ON(!request);

	queue = GET_VCTX_QUEUE(vctx, request);

	if (test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		err("video is stream on, not applied");
		ret = -EINVAL;
		goto p_err;
	}

	ret = vb2_reqbufs(queue->vbq, request);
	if (ret) {
		err("vb2_reqbufs is fail(%d)", ret);
		goto p_err;
	}

	framemgr = &queue->framemgr;
	queue->buf_maxcount = request->count;
	if (queue->buf_maxcount == 0) {
		queue->buf_refcount = 0;
		clear_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
		clear_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);
		fimc_is_frame_close(framemgr);
	} else {
		if (queue->buf_maxcount < queue->buf_rdycount) {
			err("buffer count is not invalid(%d < %d)",
				queue->buf_maxcount, queue->buf_rdycount);
			ret = -EINVAL;
			goto p_err;
		}

		if (!queue->buf_rdycount)
			set_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
	}

	fimc_is_frame_open(framemgr, queue->id, queue->buf_maxcount);

p_err:
	return ret;
}

int fimc_is_video_querybuf(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_queue *queue;

	queue = GET_VCTX_QUEUE(vctx, buf);

	ret = vb2_querybuf(queue->vbq, buf);

	return ret;
}

int fimc_is_video_set_format_mplane(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct v4l2_format *format)
{
	int ret = 0;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);
	BUG_ON(!format);

	queue = GET_VCTX_QUEUE(vctx, format);

	ret = fimc_is_queue_set_format_mplane(queue, format);

	mdbgv_vid("set_format(%d x %d)\n", vctx,
		queue->framecfg.width,
		queue->framecfg.height);

	return ret;
}

int fimc_is_video_qbuf(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_queue *queue;
	struct vb2_queue *vbq;
	struct vb2_buffer *vb;

	BUG_ON(!file);
	BUG_ON(!vctx);
	BUG_ON(!buf);

	buf->flags &= ~V4L2_BUF_FLAG_USE_SYNC;
	queue = GET_VCTX_QUEUE(vctx, buf);
	vbq = queue->vbq;

	if (!vbq) {
		merr("vbq is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	if (vbq->fileio) {
		merr("file io in progress", vctx);
		ret = -EBUSY;
		goto p_err;
	}

	if (buf->type != queue->vbq->type) {
		merr("buf type is invalid(%d != %d)", vctx,
			buf->type, queue->vbq->type);
		ret = -EINVAL;
		goto p_err;
	}

	if (buf->index >= vbq->num_buffers) {
		merr("buffer index%d out of range", vctx, buf->index);
		ret = -EINVAL;
		goto p_err;
	}

	if (buf->memory != vbq->memory) {
		merr("invalid memory type%d", vctx, buf->memory);
		ret = -EINVAL;
		goto p_err;
	}

	vb = vbq->bufs[buf->index];
	if (!vb) {
		merr("vb is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	ret = vb2_qbuf(queue->vbq, buf);

p_err:
	return ret;
}

int fimc_is_video_dqbuf(struct file *file,
	struct fimc_is_video_ctx *vctx,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	u32 qcount;
	bool blocking;
	struct fimc_is_queue *queue;
	struct fimc_is_framemgr *framemgr;

	BUG_ON(!file);
	BUG_ON(!vctx);
	BUG_ON(!buf);

	blocking = file->f_flags & O_NONBLOCK;
	queue = GET_VCTX_QUEUE(vctx, buf);
	framemgr = &queue->framemgr;

	if (!queue->vbq) {
		merr("vbq is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	if (buf->type != queue->vbq->type) {
		merr("buf type is invalid(%d != %d)", vctx,
			buf->type, queue->vbq->type);
		ret = -EINVAL;
		goto p_err;
	}

	if (!test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		merr("queue is not streamon(%ld)", vctx, queue->state);
		ret = -EINVAL;
		goto p_err;
	}

	framemgr_e_barrier_irq(framemgr, 0);
	qcount = framemgr->frame_req_cnt +
		framemgr->frame_pro_cnt +
		framemgr->frame_com_cnt;
	framemgr_x_barrier_irq(framemgr, 0);

	if (qcount <= 0) {
		merr("dqbuf can not be executed without qbuf(%d)", vctx, qcount);
		ret = -EINVAL;
		goto p_err;
	}

	ret = vb2_dqbuf(queue->vbq, buf, blocking);

p_err:
	return ret;
}

int fimc_is_video_streamon(struct file *file,
	struct fimc_is_video_ctx *vctx,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_queue *queue;
	struct vb2_queue *vbq;

	BUG_ON(!file);
	BUG_ON(!vctx);

	queue = GET_QUEUE(vctx, type);
	vbq = queue->vbq;
	if (!vbq) {
		merr("vbq is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	if (vbq->type != type) {
		merr("invalid stream type(%d != %d)", vctx, vbq->type, type);
		ret = -EINVAL;
		goto p_err;
	}

	if (vbq->streaming) {
		merr("streamon: already streaming", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	ret = vb2_streamon(vbq, type);

p_err:
	return ret;
}

int fimc_is_video_streamoff(struct file *file,
	struct fimc_is_video_ctx *vctx,
	enum v4l2_buf_type type)
{
	int ret = 0;
	u32 qcount;
	struct fimc_is_queue *queue;
	struct vb2_queue *vbq;
	struct fimc_is_framemgr *framemgr;

	BUG_ON(!file);
	BUG_ON(!vctx);

	queue = GET_QUEUE(vctx, type);
	framemgr = &queue->framemgr;
	vbq = queue->vbq;
	if (!vbq) {
		merr("vbq is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	framemgr_e_barrier_irq(framemgr, 0);
	qcount = framemgr->frame_req_cnt +
		framemgr->frame_pro_cnt +
		framemgr->frame_com_cnt;
	framemgr_x_barrier_irq(framemgr, 0);

	if (qcount > 0)
		mwarn("video%d qbuf is not empty(%d)", vctx,
			vctx->video->id, qcount);

	if (vbq->type != type) {
		merr("invalid stream type(%d != %d)", vctx, vbq->type, type);
		ret = -EINVAL;
		goto p_err;
	}

	if (!vbq->streaming) {
		merr("streamoff: not streaming", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	ret = vb2_streamoff(vbq, type);

p_err:
	return ret;
}

int queue_done(struct fimc_is_video_ctx *vctx,
	struct fimc_is_queue *queue,
	u32 index, u32 state)
{
	int ret = 0;
	struct vb2_buffer *vb;

	BUG_ON(!vctx);
	BUG_ON(!vctx->video);
	BUG_ON(!queue);
	BUG_ON(!queue->vbq);
	BUG_ON(index >= FRAMEMGR_MAX_REQUEST);

	vb = queue->vbq->bufs[index];

	if (!test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		warn("%d video queue is not stream on", vctx->video->id);
		ret = -EINVAL;
		goto p_err;
	}

	if (vb->state != VB2_BUF_STATE_ACTIVE) {
		err("vb buffer[%d] state is not active(%d)", index, vb->state);
		ret = -EINVAL;
		goto p_err;
	}

	vb2_buffer_done(vb, state);

p_err:
	return ret;
}

int buffer_done(struct fimc_is_video_ctx *vctx, u32 index)
{
	int ret = 0;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);
	BUG_ON(!vctx->video);
	BUG_ON(index >= FRAMEMGR_MAX_REQUEST);
	BUG_ON(vctx->type == FIMC_IS_VIDEO_TYPE_M2M);

	switch (vctx->type) {
	case FIMC_IS_VIDEO_TYPE_OUTPUT:
		queue = GET_SRC_QUEUE(vctx);
		queue_done(vctx, queue, index, VB2_BUF_STATE_DONE);
		break;
	case FIMC_IS_VIDEO_TYPE_CAPTURE:
		queue = GET_DST_QUEUE(vctx);
		queue_done(vctx, queue, index, VB2_BUF_STATE_DONE);
		break;
	default:
		merr("invalid type(%d)", vctx, vctx->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

long video_ioctl3(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct fimc_is_video_ctx *video_ctx = file->private_data;

	if (mutex_lock_interruptible(&video_ctx->lock)) {
		err("mutex_lock_interruptible is fail");
		ret = -ERESTARTSYS;
		goto p_err;
	}

	ret = video_ioctl2(file, cmd, arg);

p_err:
	mutex_unlock(&video_ctx->lock);
	return ret;
}
