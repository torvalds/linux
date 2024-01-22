// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2023
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "dcmipp-common.h"

#define DCMIPP_PRSR		0x1f8
#define DCMIPP_CMIER		0x3f0
#define DCMIPP_CMIER_P0FRAMEIE	BIT(9)
#define DCMIPP_CMIER_P0VSYNCIE	BIT(10)
#define DCMIPP_CMIER_P0OVRIE	BIT(15)
#define DCMIPP_CMIER_P0ALL	(DCMIPP_CMIER_P0VSYNCIE |\
				 DCMIPP_CMIER_P0FRAMEIE |\
				 DCMIPP_CMIER_P0OVRIE)
#define DCMIPP_CMSR1		0x3f4
#define DCMIPP_CMSR2		0x3f8
#define DCMIPP_CMSR2_P0FRAMEF	BIT(9)
#define DCMIPP_CMSR2_P0VSYNCF	BIT(10)
#define DCMIPP_CMSR2_P0OVRF	BIT(15)
#define DCMIPP_CMFCR		0x3fc
#define DCMIPP_P0FSCR		0x404
#define DCMIPP_P0FSCR_PIPEN	BIT(31)
#define DCMIPP_P0FCTCR		0x500
#define DCMIPP_P0FCTCR_CPTREQ	BIT(3)
#define DCMIPP_P0DCCNTR		0x5b0
#define DCMIPP_P0DCLMTR		0x5b4
#define DCMIPP_P0DCLMTR_ENABLE	BIT(31)
#define DCMIPP_P0DCLMTR_LIMIT_MASK	GENMASK(23, 0)
#define DCMIPP_P0PPM0AR1	0x5c4
#define DCMIPP_P0SR		0x5f8
#define DCMIPP_P0SR_CPTACT	BIT(23)

struct dcmipp_bytecap_pix_map {
	unsigned int code;
	u32 pixelformat;
};

#define PIXMAP_MBUS_PFMT(mbus, fmt)			\
	{						\
		.code = MEDIA_BUS_FMT_##mbus,		\
		.pixelformat = V4L2_PIX_FMT_##fmt	\
	}

static const struct dcmipp_bytecap_pix_map dcmipp_bytecap_pix_map_list[] = {
	PIXMAP_MBUS_PFMT(RGB565_2X8_LE, RGB565),
	PIXMAP_MBUS_PFMT(YUYV8_2X8, YUYV),
	PIXMAP_MBUS_PFMT(YVYU8_2X8, YVYU),
	PIXMAP_MBUS_PFMT(UYVY8_2X8, UYVY),
	PIXMAP_MBUS_PFMT(VYUY8_2X8, VYUY),
	PIXMAP_MBUS_PFMT(Y8_1X8, GREY),
	PIXMAP_MBUS_PFMT(SBGGR8_1X8, SBGGR8),
	PIXMAP_MBUS_PFMT(SGBRG8_1X8, SGBRG8),
	PIXMAP_MBUS_PFMT(SGRBG8_1X8, SGRBG8),
	PIXMAP_MBUS_PFMT(SRGGB8_1X8, SRGGB8),
	PIXMAP_MBUS_PFMT(JPEG_1X8, JPEG),
};

static const struct dcmipp_bytecap_pix_map *
dcmipp_bytecap_pix_map_by_pixelformat(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dcmipp_bytecap_pix_map_list); i++) {
		if (dcmipp_bytecap_pix_map_list[i].pixelformat == pixelformat)
			return &dcmipp_bytecap_pix_map_list[i];
	}

	return NULL;
}

struct dcmipp_buf {
	struct vb2_v4l2_buffer	vb;
	bool			prepared;
	dma_addr_t		addr;
	size_t			size;
	struct list_head	list;
};

enum dcmipp_state {
	DCMIPP_STOPPED = 0,
	DCMIPP_WAIT_FOR_BUFFER,
	DCMIPP_RUNNING,
};

struct dcmipp_bytecap_device {
	struct dcmipp_ent_device ved;
	struct video_device vdev;
	struct device *dev;
	struct v4l2_pix_format format;
	struct vb2_queue queue;
	struct list_head buffers;
	/*
	 * Protects concurrent calls of buf queue / irq handler
	 * and buffer handling related variables / lists
	 */
	spinlock_t irqlock;
	/* mutex used as vdev and queue lock */
	struct mutex lock;
	u32 sequence;
	struct media_pipeline pipe;
	struct v4l2_subdev *s_subdev;

	enum dcmipp_state state;

	/*
	 * DCMIPP driver is handling 2 buffers
	 * active: buffer into which DCMIPP is currently writing into
	 * next: buffer given to the DCMIPP and which will become
	 *       automatically active on next VSYNC
	 */
	struct dcmipp_buf *active, *next;

	void __iomem *regs;

	u32 cmier;
	u32 cmsr2;

	struct {
		u32 errors;
		u32 limit;
		u32 overrun;
		u32 buffers;
		u32 vsync;
		u32 frame;
		u32 it;
		u32 underrun;
		u32 nactive;
	} count;
};

static const struct v4l2_pix_format fmt_default = {
	.width = DCMIPP_FMT_WIDTH_DEFAULT,
	.height = DCMIPP_FMT_HEIGHT_DEFAULT,
	.pixelformat = V4L2_PIX_FMT_RGB565,
	.field = V4L2_FIELD_NONE,
	.bytesperline = DCMIPP_FMT_WIDTH_DEFAULT * 2,
	.sizeimage = DCMIPP_FMT_WIDTH_DEFAULT * DCMIPP_FMT_HEIGHT_DEFAULT * 2,
	.colorspace = DCMIPP_COLORSPACE_DEFAULT,
	.ycbcr_enc = DCMIPP_YCBCR_ENC_DEFAULT,
	.quantization = DCMIPP_QUANTIZATION_DEFAULT,
	.xfer_func = DCMIPP_XFER_FUNC_DEFAULT,
};

static int dcmipp_bytecap_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	strscpy(cap->driver, DCMIPP_PDEV_NAME, sizeof(cap->driver));
	strscpy(cap->card, KBUILD_MODNAME, sizeof(cap->card));

	return 0;
}

static int dcmipp_bytecap_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct dcmipp_bytecap_device *vcap = video_drvdata(file);

	f->fmt.pix = vcap->format;

	return 0;
}

static int dcmipp_bytecap_try_fmt_vid_cap(struct file *file, void *priv,
					  struct v4l2_format *f)
{
	struct dcmipp_bytecap_device *vcap = video_drvdata(file);
	struct v4l2_pix_format *format = &f->fmt.pix;
	const struct dcmipp_bytecap_pix_map *vpix;
	u32 in_w, in_h;

	/* Don't accept a pixelformat that is not on the table */
	vpix = dcmipp_bytecap_pix_map_by_pixelformat(format->pixelformat);
	if (!vpix)
		format->pixelformat = fmt_default.pixelformat;

	/* Adjust width & height */
	in_w = format->width;
	in_h = format->height;
	v4l_bound_align_image(&format->width, DCMIPP_FRAME_MIN_WIDTH,
			      DCMIPP_FRAME_MAX_WIDTH, 0, &format->height,
			      DCMIPP_FRAME_MIN_HEIGHT, DCMIPP_FRAME_MAX_HEIGHT,
			      0, 0);
	if (format->width != in_w || format->height != in_h)
		dev_dbg(vcap->dev, "resolution updated: %dx%d -> %dx%d\n",
			in_w, in_h, format->width, format->height);

	if (format->pixelformat == V4L2_PIX_FMT_JPEG) {
		format->bytesperline = format->width;
		format->sizeimage = format->bytesperline * format->height;
	} else {
		v4l2_fill_pixfmt(format, format->pixelformat,
				 format->width, format->height);
	}

	if (format->field == V4L2_FIELD_ANY)
		format->field = fmt_default.field;

	dcmipp_colorimetry_clamp(format);

	return 0;
}

static int dcmipp_bytecap_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct dcmipp_bytecap_device *vcap = video_drvdata(file);
	int ret;

	/* Do not change the format while stream is on */
	if (vb2_is_busy(&vcap->queue))
		return -EBUSY;

	ret = dcmipp_bytecap_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	dev_dbg(vcap->dev, "%s: format update: old:%ux%u (0x%p4cc, %u, %u, %u, %u) new:%ux%d (0x%p4cc, %u, %u, %u, %u)\n",
		vcap->vdev.name,
		/* old */
		vcap->format.width, vcap->format.height,
		&vcap->format.pixelformat, vcap->format.colorspace,
		vcap->format.quantization, vcap->format.xfer_func,
		vcap->format.ycbcr_enc,
		/* new */
		f->fmt.pix.width, f->fmt.pix.height,
		&f->fmt.pix.pixelformat, f->fmt.pix.colorspace,
		f->fmt.pix.quantization, f->fmt.pix.xfer_func,
		f->fmt.pix.ycbcr_enc);

	vcap->format = f->fmt.pix;

	return 0;
}

static int dcmipp_bytecap_enum_fmt_vid_cap(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	const struct dcmipp_bytecap_pix_map *vpix;
	unsigned int index = f->index;
	unsigned int i;

	if (f->mbus_code) {
		/*
		 * If a media bus code is specified, only enumerate formats
		 * compatible with it.
		 */
		for (i = 0; i < ARRAY_SIZE(dcmipp_bytecap_pix_map_list); i++) {
			vpix = &dcmipp_bytecap_pix_map_list[i];
			if (vpix->code != f->mbus_code)
				continue;

			if (index == 0)
				break;

			index--;
		}

		if (i == ARRAY_SIZE(dcmipp_bytecap_pix_map_list))
			return -EINVAL;
	} else {
		/* Otherwise, enumerate all formats. */
		if (f->index >= ARRAY_SIZE(dcmipp_bytecap_pix_map_list))
			return -EINVAL;

		vpix = &dcmipp_bytecap_pix_map_list[f->index];
	}

	f->pixelformat = vpix->pixelformat;

	return 0;
}

static int dcmipp_bytecap_enum_framesizes(struct file *file, void *fh,
					  struct v4l2_frmsizeenum *fsize)
{
	const struct dcmipp_bytecap_pix_map *vpix;

	if (fsize->index)
		return -EINVAL;

	/* Only accept code in the pix map table */
	vpix = dcmipp_bytecap_pix_map_by_pixelformat(fsize->pixel_format);
	if (!vpix)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = DCMIPP_FRAME_MIN_WIDTH;
	fsize->stepwise.max_width = DCMIPP_FRAME_MAX_WIDTH;
	fsize->stepwise.min_height = DCMIPP_FRAME_MIN_HEIGHT;
	fsize->stepwise.max_height = DCMIPP_FRAME_MAX_HEIGHT;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static const struct v4l2_file_operations dcmipp_bytecap_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops dcmipp_bytecap_ioctl_ops = {
	.vidioc_querycap = dcmipp_bytecap_querycap,

	.vidioc_g_fmt_vid_cap = dcmipp_bytecap_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = dcmipp_bytecap_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = dcmipp_bytecap_try_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = dcmipp_bytecap_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = dcmipp_bytecap_enum_framesizes,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static int dcmipp_pipeline_s_stream(struct dcmipp_bytecap_device *vcap,
				    int state)
{
	struct media_pad *pad;
	int ret;

	/*
	 * Get source subdev - since link is IMMUTABLE, pointer is cached
	 * within the dcmipp_bytecap_device structure
	 */
	if (!vcap->s_subdev) {
		pad = media_pad_remote_pad_first(&vcap->vdev.entity.pads[0]);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			return -EINVAL;
		vcap->s_subdev = media_entity_to_v4l2_subdev(pad->entity);
	}

	ret = v4l2_subdev_call(vcap->s_subdev, video, s_stream, state);
	if (ret < 0) {
		dev_err(vcap->dev, "failed to %s streaming (%d)\n",
			state ? "start" : "stop", ret);
		return ret;
	}

	return 0;
}

static void dcmipp_start_capture(struct dcmipp_bytecap_device *vcap,
				 struct dcmipp_buf *buf)
{
	/* Set buffer address */
	reg_write(vcap, DCMIPP_P0PPM0AR1, buf->addr);

	/* Set buffer size */
	reg_write(vcap, DCMIPP_P0DCLMTR, DCMIPP_P0DCLMTR_ENABLE |
		  ((buf->size / 4) & DCMIPP_P0DCLMTR_LIMIT_MASK));

	/* Capture request */
	reg_set(vcap, DCMIPP_P0FCTCR, DCMIPP_P0FCTCR_CPTREQ);
}

static void dcmipp_bytecap_all_buffers_done(struct dcmipp_bytecap_device *vcap,
					    enum vb2_buffer_state state)
{
	struct dcmipp_buf *buf, *node;

	list_for_each_entry_safe(buf, node, &vcap->buffers, list) {
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static int dcmipp_bytecap_start_streaming(struct vb2_queue *vq,
					  unsigned int count)
{
	struct dcmipp_bytecap_device *vcap = vb2_get_drv_priv(vq);
	struct media_entity *entity = &vcap->vdev.entity;
	struct dcmipp_buf *buf;
	int ret;

	vcap->sequence = 0;
	memset(&vcap->count, 0, sizeof(vcap->count));

	ret = pm_runtime_resume_and_get(vcap->dev);
	if (ret < 0) {
		dev_err(vcap->dev, "%s: Failed to start streaming, cannot get sync (%d)\n",
			__func__, ret);
		goto err_buffer_done;
	}

	ret = media_pipeline_start(entity->pads, &vcap->pipe);
	if (ret) {
		dev_dbg(vcap->dev, "%s: Failed to start streaming, media pipeline start error (%d)\n",
			__func__, ret);
		goto err_pm_put;
	}

	ret = dcmipp_pipeline_s_stream(vcap, 1);
	if (ret)
		goto err_media_pipeline_stop;

	spin_lock_irq(&vcap->irqlock);

	/* Enable pipe at the end of programming */
	reg_set(vcap, DCMIPP_P0FSCR, DCMIPP_P0FSCR_PIPEN);

	/*
	 * vb2 framework guarantee that we have at least 'min_queued_buffers'
	 * buffers in the list at this moment
	 */
	vcap->next = list_first_entry(&vcap->buffers, typeof(*buf), list);
	dev_dbg(vcap->dev, "Start with next [%d] %p phy=%pad\n",
		vcap->next->vb.vb2_buf.index, vcap->next, &vcap->next->addr);

	dcmipp_start_capture(vcap, vcap->next);

	/* Enable interruptions */
	vcap->cmier |= DCMIPP_CMIER_P0ALL;
	reg_set(vcap, DCMIPP_CMIER, vcap->cmier);

	vcap->state = DCMIPP_RUNNING;

	spin_unlock_irq(&vcap->irqlock);

	return 0;

err_media_pipeline_stop:
	media_pipeline_stop(entity->pads);
err_pm_put:
	pm_runtime_put(vcap->dev);
err_buffer_done:
	spin_lock_irq(&vcap->irqlock);
	/*
	 * Return all buffers to vb2 in QUEUED state.
	 * This will give ownership back to userspace
	 */
	dcmipp_bytecap_all_buffers_done(vcap, VB2_BUF_STATE_QUEUED);
	vcap->active = NULL;
	spin_unlock_irq(&vcap->irqlock);

	return ret;
}

static void dcmipp_dump_status(struct dcmipp_bytecap_device *vcap)
{
	struct device *dev = vcap->dev;

	dev_dbg(dev, "[DCMIPP_PRSR]  =%#10.8x\n", reg_read(vcap, DCMIPP_PRSR));
	dev_dbg(dev, "[DCMIPP_P0SR] =%#10.8x\n", reg_read(vcap, DCMIPP_P0SR));
	dev_dbg(dev, "[DCMIPP_P0DCCNTR]=%#10.8x\n",
		reg_read(vcap, DCMIPP_P0DCCNTR));
	dev_dbg(dev, "[DCMIPP_CMSR1] =%#10.8x\n", reg_read(vcap, DCMIPP_CMSR1));
	dev_dbg(dev, "[DCMIPP_CMSR2] =%#10.8x\n", reg_read(vcap, DCMIPP_CMSR2));
}

/*
 * Stop the stream engine. Any remaining buffers in the stream queue are
 * dequeued and passed on to the vb2 framework marked as STATE_ERROR.
 */
static void dcmipp_bytecap_stop_streaming(struct vb2_queue *vq)
{
	struct dcmipp_bytecap_device *vcap = vb2_get_drv_priv(vq);
	int ret;
	u32 status;

	dcmipp_pipeline_s_stream(vcap, 0);

	/* Stop the media pipeline */
	media_pipeline_stop(vcap->vdev.entity.pads);

	/* Disable interruptions */
	reg_clear(vcap, DCMIPP_CMIER, vcap->cmier);

	/* Stop capture */
	reg_clear(vcap, DCMIPP_P0FCTCR, DCMIPP_P0FCTCR_CPTREQ);

	/* Wait until CPTACT become 0 */
	ret = readl_relaxed_poll_timeout(vcap->regs + DCMIPP_P0SR, status,
					 !(status & DCMIPP_P0SR_CPTACT),
					 20 * USEC_PER_MSEC,
					 1000 * USEC_PER_MSEC);
	if (ret)
		dev_warn(vcap->dev, "Timeout when stopping\n");

	/* Disable pipe */
	reg_clear(vcap, DCMIPP_P0FSCR, DCMIPP_P0FSCR_PIPEN);

	spin_lock_irq(&vcap->irqlock);

	/* Return all queued buffers to vb2 in ERROR state */
	dcmipp_bytecap_all_buffers_done(vcap, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&vcap->buffers);

	vcap->active = NULL;
	vcap->state = DCMIPP_STOPPED;

	spin_unlock_irq(&vcap->irqlock);

	dcmipp_dump_status(vcap);

	pm_runtime_put(vcap->dev);

	if (vcap->count.errors)
		dev_warn(vcap->dev, "Some errors found while streaming: errors=%d (overrun=%d, limit=%d, nactive=%d), underrun=%d, buffers=%d\n",
			 vcap->count.errors, vcap->count.overrun,
			 vcap->count.limit, vcap->count.nactive,
			 vcap->count.underrun, vcap->count.buffers);
}

static int dcmipp_bytecap_buf_prepare(struct vb2_buffer *vb)
{
	struct dcmipp_bytecap_device *vcap =  vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmipp_buf *buf = container_of(vbuf, struct dcmipp_buf, vb);
	unsigned long size;

	size = vcap->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(vcap->dev, "%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	if (!buf->prepared) {
		/* Get memory addresses */
		buf->addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
		buf->size = vb2_plane_size(&buf->vb.vb2_buf, 0);
		buf->prepared = true;

		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->size);

		dev_dbg(vcap->dev, "Setup [%d] phy=%pad size=%zu\n",
			vb->index, &buf->addr, buf->size);
	}

	return 0;
}

static void dcmipp_bytecap_buf_queue(struct vb2_buffer *vb2_buf)
{
	struct dcmipp_bytecap_device *vcap =
		vb2_get_drv_priv(vb2_buf->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2_buf);
	struct dcmipp_buf *buf = container_of(vbuf, struct dcmipp_buf, vb);

	dev_dbg(vcap->dev, "Queue [%d] %p phy=%pad\n", buf->vb.vb2_buf.index,
		buf, &buf->addr);

	spin_lock_irq(&vcap->irqlock);
	list_add_tail(&buf->list, &vcap->buffers);

	if (vcap->state == DCMIPP_WAIT_FOR_BUFFER) {
		vcap->next = buf;
		dev_dbg(vcap->dev, "Restart with next [%d] %p phy=%pad\n",
			buf->vb.vb2_buf.index, buf, &buf->addr);

		dcmipp_start_capture(vcap, buf);

		vcap->state = DCMIPP_RUNNING;
	}

	spin_unlock_irq(&vcap->irqlock);
}

static int dcmipp_bytecap_queue_setup(struct vb2_queue *vq,
				      unsigned int *nbuffers,
				      unsigned int *nplanes,
				      unsigned int sizes[],
				      struct device *alloc_devs[])
{
	struct dcmipp_bytecap_device *vcap = vb2_get_drv_priv(vq);
	unsigned int size;

	size = vcap->format.sizeimage;

	/* Make sure the image size is large enough */
	if (*nplanes)
		return sizes[0] < vcap->format.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = vcap->format.sizeimage;

	dev_dbg(vcap->dev, "Setup queue, count=%d, size=%d\n",
		*nbuffers, size);

	return 0;
}

static int dcmipp_bytecap_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmipp_buf *buf = container_of(vbuf, struct dcmipp_buf, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static const struct vb2_ops dcmipp_bytecap_qops = {
	.start_streaming	= dcmipp_bytecap_start_streaming,
	.stop_streaming		= dcmipp_bytecap_stop_streaming,
	.buf_init		= dcmipp_bytecap_buf_init,
	.buf_prepare		= dcmipp_bytecap_buf_prepare,
	.buf_queue		= dcmipp_bytecap_buf_queue,
	.queue_setup		= dcmipp_bytecap_queue_setup,
	/*
	 * Since q->lock is set we can use the standard
	 * vb2_ops_wait_prepare/finish helper functions.
	 */
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static void dcmipp_bytecap_release(struct video_device *vdev)
{
	struct dcmipp_bytecap_device *vcap =
		container_of(vdev, struct dcmipp_bytecap_device, vdev);

	dcmipp_pads_cleanup(vcap->ved.pads);
	mutex_destroy(&vcap->lock);

	kfree(vcap);
}

void dcmipp_bytecap_ent_release(struct dcmipp_ent_device *ved)
{
	struct dcmipp_bytecap_device *vcap =
		container_of(ved, struct dcmipp_bytecap_device, ved);

	media_entity_cleanup(ved->ent);
	vb2_video_unregister_device(&vcap->vdev);
}

static void dcmipp_buffer_done(struct dcmipp_bytecap_device *vcap,
			       struct dcmipp_buf *buf,
			       size_t bytesused,
			       int err)
{
	struct vb2_v4l2_buffer *vbuf;

	list_del_init(&buf->list);

	vbuf = &buf->vb;

	vbuf->sequence = vcap->sequence++;
	vbuf->field = V4L2_FIELD_NONE;
	vbuf->vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&vbuf->vb2_buf, 0, bytesused);
	vb2_buffer_done(&vbuf->vb2_buf,
			err ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	dev_dbg(vcap->dev, "Done  [%d] %p phy=%pad\n", buf->vb.vb2_buf.index,
		buf, &buf->addr);
	vcap->count.buffers++;
}

/* irqlock must be held */
static void
dcmipp_bytecap_set_next_frame_or_stop(struct dcmipp_bytecap_device *vcap)
{
	if (!vcap->next && list_is_singular(&vcap->buffers)) {
		/*
		 * If there is no available buffer (none or a single one in the
		 * list while two are expected), stop the capture (effective
		 * for next frame). On-going frame capture will continue until
		 * FRAME END but no further capture will be done.
		 */
		reg_clear(vcap, DCMIPP_P0FCTCR, DCMIPP_P0FCTCR_CPTREQ);

		dev_dbg(vcap->dev, "Capture restart is deferred to next buffer queueing\n");
		vcap->next = NULL;
		vcap->state = DCMIPP_WAIT_FOR_BUFFER;
		return;
	}

	/* If we don't have buffer yet, pick the one after active */
	if (!vcap->next)
		vcap->next = list_next_entry(vcap->active, list);

	/*
	 * Set buffer address
	 * This register is shadowed and will be taken into
	 * account on next VSYNC (start of next frame)
	 */
	reg_write(vcap, DCMIPP_P0PPM0AR1, vcap->next->addr);
	dev_dbg(vcap->dev, "Write [%d] %p phy=%pad\n",
		vcap->next->vb.vb2_buf.index, vcap->next, &vcap->next->addr);
}

/* irqlock must be held */
static void dcmipp_bytecap_process_frame(struct dcmipp_bytecap_device *vcap,
					 size_t bytesused)
{
	int err = 0;
	struct dcmipp_buf *buf = vcap->active;

	if (!buf) {
		vcap->count.nactive++;
		vcap->count.errors++;
		return;
	}

	if (bytesused > buf->size) {
		dev_dbg(vcap->dev, "frame larger than expected (%zu > %zu)\n",
			bytesused, buf->size);
		/* Clip to buffer size and return buffer to V4L2 in error */
		bytesused = buf->size;
		vcap->count.limit++;
		vcap->count.errors++;
		err = -EOVERFLOW;
	}

	dcmipp_buffer_done(vcap, buf, bytesused, err);
	vcap->active = NULL;
}

static irqreturn_t dcmipp_bytecap_irq_thread(int irq, void *arg)
{
	struct dcmipp_bytecap_device *vcap =
			container_of(arg, struct dcmipp_bytecap_device, ved);
	size_t bytesused = 0;
	u32 cmsr2;

	spin_lock_irq(&vcap->irqlock);

	cmsr2 = vcap->cmsr2 & vcap->cmier;

	/*
	 * If we have an overrun, a frame-end will probably not be generated,
	 * in that case the active buffer will be recycled as next buffer by
	 * the VSYNC handler
	 */
	if (cmsr2 & DCMIPP_CMSR2_P0OVRF) {
		vcap->count.errors++;
		vcap->count.overrun++;
	}

	if (cmsr2 & DCMIPP_CMSR2_P0FRAMEF) {
		vcap->count.frame++;

		/* Read captured buffer size */
		bytesused = reg_read(vcap, DCMIPP_P0DCCNTR);
		dcmipp_bytecap_process_frame(vcap, bytesused);
	}

	if (cmsr2 & DCMIPP_CMSR2_P0VSYNCF) {
		vcap->count.vsync++;
		if (vcap->state == DCMIPP_WAIT_FOR_BUFFER) {
			vcap->count.underrun++;
			goto out;
		}

		/*
		 * On VSYNC, the previously set next buffer is going to become
		 * active thanks to the shadowing mechanism of the DCMIPP. In
		 * most of the cases, since a FRAMEEND has already come,
		 * pointer next is NULL since active is reset during the
		 * FRAMEEND handling. However, in case of framerate adjustment,
		 * there are more VSYNC than FRAMEEND. Thus we recycle the
		 * active (but not used) buffer and put it back into next.
		 */
		swap(vcap->active, vcap->next);
		dcmipp_bytecap_set_next_frame_or_stop(vcap);
	}

out:
	spin_unlock_irq(&vcap->irqlock);
	return IRQ_HANDLED;
}

static irqreturn_t dcmipp_bytecap_irq_callback(int irq, void *arg)
{
	struct dcmipp_bytecap_device *vcap =
			container_of(arg, struct dcmipp_bytecap_device, ved);

	/* Store interrupt status register */
	vcap->cmsr2 = reg_read(vcap, DCMIPP_CMSR2) & vcap->cmier;
	vcap->count.it++;

	/* Clear interrupt */
	reg_write(vcap, DCMIPP_CMFCR, vcap->cmsr2);

	return IRQ_WAKE_THREAD;
}

static int dcmipp_bytecap_link_validate(struct media_link *link)
{
	struct media_entity *entity = link->sink->entity;
	struct video_device *vd = media_entity_to_video_device(entity);
	struct dcmipp_bytecap_device *vcap = container_of(vd,
					struct dcmipp_bytecap_device, vdev);
	struct v4l2_subdev *source_sd =
		media_entity_to_v4l2_subdev(link->source->entity);
	struct v4l2_subdev_format source_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = link->source->index,
	};
	const struct dcmipp_bytecap_pix_map *vpix;
	int ret;

	ret = v4l2_subdev_call(source_sd, pad, get_fmt, NULL, &source_fmt);
	if (ret < 0)
		return 0;

	if (source_fmt.format.width != vcap->format.width ||
	    source_fmt.format.height != vcap->format.height) {
		dev_err(vcap->dev, "Wrong width or height %ux%u (%ux%u expected)\n",
			vcap->format.width, vcap->format.height,
			source_fmt.format.width, source_fmt.format.height);
		return -EINVAL;
	}

	vpix = dcmipp_bytecap_pix_map_by_pixelformat(vcap->format.pixelformat);
	if (source_fmt.format.code != vpix->code) {
		dev_err(vcap->dev, "Wrong mbus_code 0x%x, (0x%x expected)\n",
			vpix->code, source_fmt.format.code);
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations dcmipp_bytecap_entity_ops = {
	.link_validate = dcmipp_bytecap_link_validate,
};

struct dcmipp_ent_device *dcmipp_bytecap_ent_init(struct device *dev,
						  const char *entity_name,
						  struct v4l2_device *v4l2_dev,
						  void __iomem *regs)
{
	struct dcmipp_bytecap_device *vcap;
	struct video_device *vdev;
	struct vb2_queue *q;
	const unsigned long pad_flag = MEDIA_PAD_FL_SINK;
	int ret = 0;

	/* Allocate the dcmipp_bytecap_device struct */
	vcap = kzalloc(sizeof(*vcap), GFP_KERNEL);
	if (!vcap)
		return ERR_PTR(-ENOMEM);

	/* Allocate the pads */
	vcap->ved.pads = dcmipp_pads_init(1, &pad_flag);
	if (IS_ERR(vcap->ved.pads)) {
		ret = PTR_ERR(vcap->ved.pads);
		goto err_free_vcap;
	}

	/* Initialize the media entity */
	vcap->vdev.entity.name = entity_name;
	vcap->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
	vcap->vdev.entity.ops = &dcmipp_bytecap_entity_ops;
	ret = media_entity_pads_init(&vcap->vdev.entity, 1, vcap->ved.pads);
	if (ret)
		goto err_clean_pads;

	/* Initialize the lock */
	mutex_init(&vcap->lock);

	/* Initialize the vb2 queue */
	q = &vcap->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->lock = &vcap->lock;
	q->drv_priv = vcap;
	q->buf_struct_size = sizeof(struct dcmipp_buf);
	q->ops = &dcmipp_bytecap_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 1;
	q->dev = dev;

	/* DCMIPP requires 16 bytes aligned buffers */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32) & ~0x0f);
	if (ret) {
		dev_err(dev, "Failed to set DMA mask\n");
		goto err_mutex_destroy;
	}

	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(dev, "%s: vb2 queue init failed (err=%d)\n",
			entity_name, ret);
		goto err_clean_m_ent;
	}

	/* Initialize buffer list and its lock */
	INIT_LIST_HEAD(&vcap->buffers);
	spin_lock_init(&vcap->irqlock);

	/* Set default frame format */
	vcap->format = fmt_default;

	/* Fill the dcmipp_ent_device struct */
	vcap->ved.ent = &vcap->vdev.entity;
	vcap->ved.handler = dcmipp_bytecap_irq_callback;
	vcap->ved.thread_fn = dcmipp_bytecap_irq_thread;
	vcap->dev = dev;
	vcap->regs = regs;

	/* Initialize the video_device struct */
	vdev = &vcap->vdev;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_IO_MC;
	vdev->release = dcmipp_bytecap_release;
	vdev->fops = &dcmipp_bytecap_fops;
	vdev->ioctl_ops = &dcmipp_bytecap_ioctl_ops;
	vdev->lock = &vcap->lock;
	vdev->queue = q;
	vdev->v4l2_dev = v4l2_dev;
	strscpy(vdev->name, entity_name, sizeof(vdev->name));
	video_set_drvdata(vdev, &vcap->ved);

	/* Register the video_device with the v4l2 and the media framework */
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "%s: video register failed (err=%d)\n",
			vcap->vdev.name, ret);
		goto err_clean_m_ent;
	}

	return &vcap->ved;

err_clean_m_ent:
	media_entity_cleanup(&vcap->vdev.entity);
err_mutex_destroy:
	mutex_destroy(&vcap->lock);
err_clean_pads:
	dcmipp_pads_cleanup(vcap->ved.pads);
err_free_vcap:
	kfree(vcap);

	return ERR_PTR(ret);
}
