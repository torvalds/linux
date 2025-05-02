/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-sg.h>
#include "fthd_drv.h"
#include "fthd_hw.h"
#include "fthd_isp.h"
#include "fthd_ringbuf.h"
#include "fthd_buffer.h"

#define FTHD_MAX_WIDTH 1280
#define FTHD_MAX_HEIGHT 720
#define FTHD_MIN_WIDTH 320
#define FTHD_MIN_HEIGHT 240
#define FTHD_NUM_FORMATS 2 /* NV16 is disabled for now */

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
# define VFL_TYPE_VIDEO VFL_TYPE_GRABBER
#endif

static int fthd_buffer_queue_setup(
    struct vb2_queue *vq,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
    const struct v4l2_format *fmt,
#endif
#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(4,5,0))
    const void *parg,
#endif
    unsigned int *nbuffers,
    unsigned int *nplanes,
    unsigned int sizes[],
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)
    struct device *alloc_devs[]
#else
    void *alloc_ctxs[]
#endif
) {

	struct fthd_private *dev_priv = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *cur_fmt = &dev_priv->fmt.fmt;
	int i, total_size = 0;

	if (*nplanes)
		return sizes[0] < (cur_fmt->bytesperline * cur_fmt->height) ? -EINVAL : 0;

	*nplanes = dev_priv->fmt.planes;

	if (!*nplanes)
		return -EINVAL;

	/* FIXME: We assume single plane format here but not below */
	for (i = 0; i < *nplanes; i++) {
		sizes[i] = cur_fmt->sizeimage;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)
		alloc_devs[i] = &dev_priv->pdev->dev;
#else
		alloc_ctxs[i] = dev_priv->alloc_ctx;
#endif
		total_size += sizes[i];
	}

	*nbuffers = (4096 * 4096) / total_size;
	if (*nbuffers > 4)
		*nbuffers = 4;
	if (*nbuffers <= 1)
		return -ENOMEM;
	pr_debug("using %d buffers\n", *nbuffers);

	return 0;
}

static void fthd_buffer_cleanup(struct vb2_buffer *vb)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct h2t_buf_ctx *ctx = NULL;
	int i;

	pr_debug("%p\n", vb);
	for(i = 0; i < FTHD_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].vb == vb) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		};
	}
	if (!ctx || ctx->state == BUF_FREE)
		return;

	ctx->state = BUF_FREE;
	ctx->vb = NULL;
	isp_mem_destroy(ctx->dma_desc_obj);
	for(i = 0; i < dev_priv->fmt.planes; i++) {
		iommu_free(dev_priv, ctx->plane[i]);
		ctx->plane[i] = NULL;
	}
	ctx->dma_desc_obj = NULL;
}

static int fthd_send_h2t_buffer(struct fthd_private *dev_priv, struct h2t_buf_ctx *ctx)
{
	u32 entry;
	int ret;

	pr_debug("sending buffer %p size %ld, ctx %p\n", ctx->vb, sizeof(ctx->dma_desc_list), ctx);
	FTHD_S2_MEMCPY_TOIO(ctx->dma_desc_obj->offset, &ctx->dma_desc_list, sizeof(ctx->dma_desc_list));
	ret = fthd_channel_ringbuf_send(dev_priv, dev_priv->channel_buf_h2t,
					ctx->dma_desc_obj->offset, 0x180, 0x30000000, &entry);

	if (ret) {
		pr_err("%s: fthd_channel_ringbuf_send: %d\n", __FUNCTION__, ret);
		return ret;
	}
	return fthd_channel_wait_ready(dev_priv, dev_priv->channel_buf_h2t, entry, 2000);
}

static void fthd_buffer_queue(struct vb2_buffer *vb)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct dma_descriptor_list *list;
	struct h2t_buf_ctx *ctx = NULL;

	int i;
	pr_debug("vb = %p\n", vb);
	for(i = 0; i < FTHD_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].vb == vb) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		};
	}

	if (!ctx)
		return;

	if (ctx->state != BUF_ALLOC)
		return;

	if (!vb->vb2_queue->streaming) {
		ctx->state = BUF_DRV_QUEUED;
	} else {
		list = &ctx->dma_desc_list;
		list->field0 = 1;
		ctx->state = BUF_HW_QUEUED;
		wmb();
		pr_debug("%d: field0: %d, count %d, pool %d, addr0 0x%08x, addr1 0x%08x tag 0x%08llx vb = %p\n", i, list->field0,
			 list->desc[i].count, list->desc[i].pool, list->desc[i].addr0, list->desc[i].addr1, list->desc[i].tag, ctx->vb);

		if (fthd_send_h2t_buffer(dev_priv, ctx)) {
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
			ctx->state = BUF_ALLOC;
		}
	}
	return;
}

static int fthd_buffer_prepare(struct vb2_buffer *vb)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct sg_table *sgtable;
	struct h2t_buf_ctx *ctx = NULL;
	struct dma_descriptor_list *dma_list;
	int i;

	pr_debug("%p\n", vb);
	for(i = 0; i < FTHD_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].state == BUF_FREE ||
		    (dev_priv->h2t_bufs[i].state == BUF_ALLOC && dev_priv->h2t_bufs[i].vb == vb)) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		}
	}

	if (!ctx)
		return -ENOBUFS;

	if (ctx->state == BUF_FREE) {
		pr_debug("allocating new entry\n");
		ctx->dma_desc_obj = isp_mem_create(dev_priv, FTHD_MEM_BUFFER, 0x180);
		if (!ctx->dma_desc_obj)
			return -ENOMEM;

		ctx->vb = vb;
		ctx->state = BUF_ALLOC;

		for(i = 0; i < dev_priv->fmt.planes; i++) {
		  sgtable = vb2_dma_sg_plane_desc(vb, i);
		  ctx->plane[i] = iommu_allocate_sgtable(dev_priv, sgtable);
		  if(!ctx->plane[i])
			  return -ENOMEM;
		}
	}

	vb2_set_plane_payload(vb, 0, dev_priv->fmt.fmt.sizeimage);

	dma_list = &ctx->dma_desc_list;
	memset(dma_list, 0, 0x180);

	dma_list->field0 = 1;
	dma_list->count = 1;
	dma_list->desc[0].count = 1;
	dma_list->desc[0].pool = 0x02;
	dma_list->desc[0].addr0 = (ctx->plane[0]->offset << 12) | 0xc0000000;

	if (dev_priv->fmt.planes >= 2)
		dma_list->desc[0].addr1 = (ctx->plane[1]->offset << 12) | 0xc0000000;
	if (dev_priv->fmt.planes >= 3)
		dma_list->desc[0].addr2 = (ctx->plane[2]->offset << 12) | 0xc0000000;

	dma_list->desc[0].tag = (u64)ctx;
	init_waitqueue_head(&ctx->wq);
	return 0;
}

void fthd_buffer_return_handler(struct fthd_private *dev_priv, u32 offset, int size)
{
	struct dma_descriptor_list list;
	struct h2t_buf_ctx *ctx;
	int i;

	FTHD_S2_MEMCPY_FROMIO(&list, offset, sizeof(list));

	for(i = 0; i < list.count; i++) {
		ctx = (struct h2t_buf_ctx *)list.desc[i].tag;
		pr_debug("%d: field0: %d, count %d, pool %d, addr0 0x%08x, addr1 0x%08x tag 0x%08llx vb = %p, ctx = %p\n", i, list.field0,
			 list.desc[i].count, list.desc[i].pool, list.desc[i].addr0, list.desc[i].addr1, list.desc[i].tag, ctx->vb, ctx);

		if (ctx->state == BUF_HW_QUEUED || ctx->state == BUF_DRV_QUEUED) {
			ctx->state = BUF_ALLOC;
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_DONE);
		}

	}
}

static int fthd_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vq);
	struct h2t_buf_ctx *ctx;
	int i, ret;

	pr_debug("count = %d\n", count);
	ret = fthd_start_channel(dev_priv, 0);
	if (ret)
		return ret;

	for(i = 0; i < FTHD_BUFFERS && count; i++, count--) {
		ctx = dev_priv->h2t_bufs + i;
		if (ctx->state != BUF_DRV_QUEUED)
			continue;

		if (fthd_send_h2t_buffer(dev_priv, ctx)) {
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_ERROR);
			ctx->state = BUF_ALLOC;
		}
			ctx->state = BUF_HW_QUEUED;
	}
	return 0;
}

static void fthd_stop_streaming(struct vb2_queue *vq)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vq);
	struct h2t_buf_ctx *ctx;
	int ret, i;

	ret = fthd_stop_channel(dev_priv, 0);
	if (!ret) {
		pr_debug("waiting for buffers...\n");
		vb2_wait_for_all_buffers(vq);
		pr_debug("done\n");
	} else {
	    /* Firmware doesn't respond. */
	    for(i = 0; i < FTHD_BUFFERS;i++) {
		    ctx = dev_priv->h2t_bufs + i;
		    if (ctx->state == BUF_DRV_QUEUED || ctx->state == BUF_HW_QUEUED) {
			    vb2_buffer_done(ctx->vb, VB2_BUF_STATE_DONE);
			    ctx->vb = NULL;
			    ctx->state = BUF_ALLOC;
		}
	    }
	}
}

static struct vb2_ops vb2_queue_ops = {
	.queue_setup            = fthd_buffer_queue_setup,
	.buf_prepare            = fthd_buffer_prepare,
	.buf_cleanup            = fthd_buffer_cleanup,
	.start_streaming        = fthd_start_streaming,
	.stop_streaming         = fthd_stop_streaming,
	.buf_queue              = fthd_buffer_queue,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static struct v4l2_file_operations fthd_vdev_fops = {
	.owner          = THIS_MODULE,
	.open           = v4l2_fh_open,

	.read		= vb2_fop_read,
	.release        = vb2_fop_release,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2
};

static int fthd_v4l2_ioctl_enum_input(struct file *filp, void *priv,
				      struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	memset(input, 0, sizeof(*input));
	strcpy(input->name, "Camera");
	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = 0;

	return 0;
}

static int fthd_v4l2_ioctl_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int fthd_v4l2_ioctl_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int fthd_v4l2_ioctl_querycap(struct file *filp, void *priv,
				    struct v4l2_capability *cap)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	strcpy(cap->driver, "facetimehd");
	strcpy(cap->card, "Apple Facetime HD");
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(dev_priv->pdev));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			   V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int fthd_v4l2_ioctl_enum_fmt_vid_cap(struct file *filp, void *priv,
				   struct v4l2_fmtdesc *fmt)
{
	char *desc = NULL;

	switch (fmt->index) {
	case 0:
		fmt->pixelformat = V4L2_PIX_FMT_YUYV;
		desc = "YUYV";
		break;
	case 1:
		fmt->pixelformat = V4L2_PIX_FMT_YVYU;
		desc = "YVYU";
		break;
	/* We don't support the mplane yet
	case 2:
		fmt->pixelformat = V4L2_PIX_FMT_NV16;
		desc = "NV16";
		break;
	*/
	default:
		return -EINVAL;
	}

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	strncpy(fmt->description, desc, sizeof(fmt->description));

	return 0;
}

static int fthd_v4l2_adjust_format(struct fthd_private *dev_priv,
				   struct v4l2_pix_format *pix)
{

	if (pix->pixelformat != V4L2_PIX_FMT_YUYV &&
	    pix->pixelformat != V4L2_PIX_FMT_YVYU)
		pix->pixelformat = V4L2_PIX_FMT_YUYV;

	if (pix->width < FTHD_MIN_WIDTH)
		pix->width = FTHD_MIN_WIDTH;
	if (pix->width > FTHD_MAX_WIDTH)
		pix->width = FTHD_MAX_WIDTH;
	if (pix->height < FTHD_MIN_HEIGHT)
		pix->height = FTHD_MIN_HEIGHT;
	if (pix->height > FTHD_MAX_HEIGHT)
		pix->height = FTHD_MAX_HEIGHT;

	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->field = V4L2_FIELD_NONE;
	pix->width = ALIGN(pix->width, 7);

	switch (pix->pixelformat) {
/*
	case V4L2_PIX_FMT_NV16:
		pix->sizeimage = pix->width * pix->height;
		pix->bytesperline = pix->width;
		break;
*/
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	default:
		pix->bytesperline = pix->width * 2;
		pix->sizeimage = pix->bytesperline * pix->height;
		break;
	}

	return 0;
}

static int fthd_v4l2_ioctl_try_fmt_vid_cap(struct file *filp, void *_priv,
					   struct v4l2_format *fmt)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	pr_debug("%s: %dx%d\n", __FUNCTION__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return fthd_v4l2_adjust_format(dev_priv, &fmt->fmt.pix);
}

static int fthd_v4l2_ioctl_g_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	pr_debug("%s\n", __FUNCTION__);
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt->fmt.pix = dev_priv->fmt.fmt;

	return 0;
}

static int fthd_v4l2_ioctl_s_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	int ret;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* FIXME: Check if hardware is busy */

	ret = fthd_v4l2_adjust_format(dev_priv, &fmt->fmt.pix);
	if (ret)
		return ret;

	pr_debug("%c%c%c%c\n", fmt->fmt.pix.pixelformat, fmt->fmt.pix.pixelformat >> 8,
		 fmt->fmt.pix.pixelformat >> 16, fmt->fmt.pix.pixelformat >> 24);

	dev_priv->fmt.fmt = fmt->fmt.pix;

	switch (fmt->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_NV16:
		dev_priv->fmt.planes = 2;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		dev_priv->fmt.planes = 1;
		break;
	}

	return 0;
}


static int fthd_v4l2_ioctl_g_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
        struct fthd_private *priv_dev = video_drvdata(filp);
	struct v4l2_fract timeperframe = {
		.numerator = priv_dev->frametime,
		.denominator = 1000,
	};

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	parm->parm.capture.readbuffers = FTHD_BUFFERS;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = timeperframe;
	return 0;
}

static int fthd_v4l2_ioctl_s_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{

        struct fthd_private *dev_priv = video_drvdata(filp);
	struct v4l2_fract *timeperframe;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	timeperframe = &parm->parm.capture.timeperframe;

	if(timeperframe->denominator == 0) {
		timeperframe->numerator = 20;
		timeperframe->denominator = 1000;
	}

	dev_priv->frametime = clamp_t(unsigned int, timeperframe->numerator * 1000 /
				timeperframe->denominator, 20, 500);

	return fthd_v4l2_ioctl_g_parm(filp, priv, parm);
}

static int fthd_v4l2_ioctl_enum_framesizes(struct file *filp, void *priv,
		struct v4l2_frmsizeenum *sizes)
{
	if (sizes->index)
		return -EINVAL;

	if (sizes->pixel_format != V4L2_PIX_FMT_YUYV &&
	    sizes->pixel_format != V4L2_PIX_FMT_YVYU)
		return -EINVAL;

	sizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	sizes->discrete.width = FTHD_MAX_WIDTH;
	sizes->discrete.height = FTHD_MAX_HEIGHT;

	return 0;
}

static int fthd_v4l2_ioctl_enum_frameintervals(struct file *filp, void *priv,
		struct v4l2_frmivalenum *interval)
{
	pr_debug("%s\n", __FUNCTION__);

	if (interval->index)
		return -EINVAL;

	if (interval->pixel_format != V4L2_PIX_FMT_YUYV &&
	    interval->pixel_format != V4L2_PIX_FMT_YVYU &&
	    interval->pixel_format != V4L2_PIX_FMT_NV16)
		return -EINVAL;

	if (interval->width & 7
	    || interval->width > FTHD_MAX_WIDTH
	    || interval->height > FTHD_MAX_HEIGHT)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete.numerator = 1;
	interval->discrete.denominator = 30;

	return 0;
}

static int fthd_v4l2_ioctl_subscribe_event(struct v4l2_fh *fh,
		const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}

	return -EINVAL;
}

static struct v4l2_ioctl_ops fthd_ioctl_ops = {
	.vidioc_enum_input      = fthd_v4l2_ioctl_enum_input,
	.vidioc_g_input         = fthd_v4l2_ioctl_g_input,
	.vidioc_s_input         = fthd_v4l2_ioctl_s_input,
	.vidioc_enum_fmt_vid_cap = fthd_v4l2_ioctl_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = fthd_v4l2_ioctl_try_fmt_vid_cap,

	.vidioc_g_fmt_vid_cap   = fthd_v4l2_ioctl_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap   = fthd_v4l2_ioctl_s_fmt_vid_cap,
	.vidioc_querycap        = fthd_v4l2_ioctl_querycap,


        .vidioc_reqbufs         = vb2_ioctl_reqbufs,
	.vidioc_create_bufs     = vb2_ioctl_create_bufs,
	.vidioc_querybuf        = vb2_ioctl_querybuf,
	.vidioc_qbuf            = vb2_ioctl_qbuf,
	.vidioc_dqbuf           = vb2_ioctl_dqbuf,
	.vidioc_expbuf          = vb2_ioctl_expbuf,
	.vidioc_streamon        = vb2_ioctl_streamon,
	.vidioc_streamoff       = vb2_ioctl_streamoff,

	.vidioc_g_parm          = fthd_v4l2_ioctl_g_parm,
	.vidioc_s_parm          = fthd_v4l2_ioctl_s_parm,
	.vidioc_enum_framesizes = fthd_v4l2_ioctl_enum_framesizes,
	.vidioc_enum_frameintervals = fthd_v4l2_ioctl_enum_frameintervals,

	.vidioc_subscribe_event	= fthd_v4l2_ioctl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int fthd_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	pr_debug("id = %x\n", ctrl->id);
	return -EINVAL;
}

static int fthd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fthd_private *dev_priv = container_of(ctrl->handler, struct fthd_private, v4l2_ctrl_handler);
	int ret = -EINVAL;

	pr_info("id = %x, val = %d\n", ctrl->id, ctrl->val);

	switch(ctrl->id) {
	case V4L2_CID_CONTRAST:
		ret = fthd_isp_cmd_channel_contrast_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_BRIGHTNESS:
		ret = fthd_isp_cmd_channel_brightness_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = fthd_isp_cmd_channel_saturation_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_HUE:
		ret = fthd_isp_cmd_channel_hue_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = fthd_isp_cmd_channel_awb(dev_priv, 0, ctrl->val);

	default:
		break;

	}
	pr_debug("ret = %d\n", ret);
	return ret;
}

static const struct v4l2_ctrl_ops fthd_ctrl_ops = {
	.g_volatile_ctrl = fthd_g_volatile_ctrl,
	.s_ctrl = fthd_s_ctrl,
};

int fthd_v4l2_register(struct fthd_private *dev_priv)
{
	struct v4l2_device *v4l2_dev = &dev_priv->v4l2_dev;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	ret = v4l2_device_register(&dev_priv->pdev->dev, v4l2_dev);
	if (ret) {
		pr_err("v4l2_device_register: %d\n", ret);
		return ret;
	}

	vdev = video_device_alloc();
	if (!vdev) {
		ret = -ENOMEM;
		goto fail;
	}
	dev_priv->videodev = vdev;

	q = &dev_priv->vb2_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev_priv;
	q->ops = &vb2_queue_ops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->buf_struct_size = 0;//sizeof(struct vpif_cap_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
	q->min_buffers_needed = 1;
#else
	q->min_queued_buffers = 1;
#endif
	q->lock = &dev_priv->vb2_queue_lock;

	ret = vb2_queue_init(q);
	if (ret)
		goto fail;

	v4l2_ctrl_handler_init(&dev_priv->v4l2_ctrl_handler, 4);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_HUE, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);

	if (dev_priv->v4l2_ctrl_handler.error) {
		pr_err("failed to setup control handlers\n");
		v4l2_ctrl_handler_free(&dev_priv->v4l2_ctrl_handler);
		goto fail;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	dev_priv->alloc_ctx = vb2_dma_sg_init_ctx(&dev_priv->pdev->dev);
#endif
	vdev->v4l2_dev = v4l2_dev;
	strcpy(vdev->name, "Apple Facetime HD"); // XXX: Length?
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->fops = &fthd_vdev_fops;
	vdev->ioctl_ops = &fthd_ioctl_ops;
	vdev->queue = q;
	vdev->release = video_device_release;
	vdev->ctrl_handler = &dev_priv->v4l2_ctrl_handler;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			    V4L2_CAP_STREAMING;
#endif
	video_set_drvdata(vdev, dev_priv);
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		video_device_release(vdev);
		goto fail_vdev;
	}
	dev_priv->fmt.fmt.sizeimage = 1280 * 720 * 2;
	dev_priv->fmt.fmt.pixelformat = V4L2_PIX_FMT_YUYV;
	dev_priv->fmt.fmt.width = 1280;
	dev_priv->fmt.fmt.height = 720;
	dev_priv->fmt.planes = 1;

	fthd_v4l2_adjust_format(dev_priv, &dev_priv->fmt.fmt);

	return 0;
fail_vdev:
	v4l2_ctrl_handler_free(&dev_priv->v4l2_ctrl_handler);
fail:
	v4l2_device_unregister(&dev_priv->v4l2_dev);
	return ret;
}

void fthd_v4l2_unregister(struct fthd_private *dev_priv)
{

	v4l2_ctrl_handler_free(&dev_priv->v4l2_ctrl_handler);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	vb2_dma_sg_cleanup_ctx(dev_priv->alloc_ctx);
#endif
	video_unregister_device(dev_priv->videodev);
	v4l2_device_unregister(&dev_priv->v4l2_dev);
}
