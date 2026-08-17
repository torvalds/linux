// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-vmalloc.h>

#include "isp4_interface.h"
#include "isp4_subdev.h"
#include "isp4_video.h"

#define ISP4VID_ISP_DRV_NAME "amd_isp_capture"
#define ISP4VID_MAX_PREVIEW_FPS 30
#define ISP4VID_DEFAULT_FMT V4L2_PIX_FMT_NV12

#define ISP4VID_PAD_VIDEO_OUTPUT 0

/* time perframe default */
#define ISP4VID_ISP_TPF_DEFAULT isp4vid_tpfs[0]

static const char *const isp4vid_video_dev_name = "Preview";

/* Sizes must be in increasing order */
static const struct v4l2_frmsize_discrete isp4vid_frmsize[] = {
	{640, 360},
	{640, 480},
	{1280, 720},
	{1280, 960},
	{1920, 1080},
	{1920, 1440},
	{2560, 1440},
	{2880, 1620},
	{2880, 1624},
	{2888, 1808},
};

static const u32 isp4vid_formats[] = {
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_YUYV
};

/* time perframe list */
static const struct v4l2_fract isp4vid_tpfs[] = {
	{ 1, ISP4VID_MAX_PREVIEW_FPS }
};

void isp4vid_handle_frame_done(struct isp4vid_dev *isp_vdev,
			       const struct isp4if_img_buf_info *img_buf)
{
	struct isp4vid_capture_buffer *isp4vid_buf;
	void *vbuf;

	scoped_guard(mutex, &isp_vdev->buf_list_lock) {
		isp4vid_buf = list_first_entry_or_null(&isp_vdev->buf_list,
						       typeof(*isp4vid_buf),
						       list);
		if (!isp4vid_buf)
			return;

		vbuf = vb2_plane_vaddr(&isp4vid_buf->vb2.vb2_buf, 0);

		if (vbuf != img_buf->planes[0].sys_addr) {
			dev_err(isp_vdev->dev, "Invalid vbuf\n");
			return;
		}

		list_del(&isp4vid_buf->list);
	}

	/* Fill the buffer */
	isp4vid_buf->vb2.vb2_buf.timestamp = ktime_get_ns();
	isp4vid_buf->vb2.sequence = isp_vdev->sequence++;
	isp4vid_buf->vb2.field = V4L2_FIELD_ANY;

	vb2_set_plane_payload(&isp4vid_buf->vb2.vb2_buf,
			      0, isp_vdev->format.sizeimage);

	vb2_buffer_done(&isp4vid_buf->vb2.vb2_buf, VB2_BUF_STATE_DONE);

	dev_dbg(isp_vdev->dev, "call vb2_buffer_done(size=%u)\n",
		isp_vdev->format.sizeimage);
}

static const struct v4l2_pix_format isp4vid_fmt_default = {
	.width = 1920,
	.height = 1080,
	.pixelformat = ISP4VID_DEFAULT_FMT,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
};

static void isp4vid_capture_return_all_buffers(struct isp4vid_dev *isp_vdev,
					       enum vb2_buffer_state state)
{
	struct isp4vid_capture_buffer *vbuf, *node;

	scoped_guard(mutex, &isp_vdev->buf_list_lock) {
		list_for_each_entry_safe(vbuf, node, &isp_vdev->buf_list, list)
			vb2_buffer_done(&vbuf->vb2.vb2_buf, state);
		INIT_LIST_HEAD(&isp_vdev->buf_list);
	}

	dev_dbg(isp_vdev->dev, "call vb2_buffer_done(%d)\n", state);
}

static int isp4vid_vdev_link_validate(struct media_link *link)
{
	return 0;
}

static const struct media_entity_operations isp4vid_vdev_ent_ops = {
	.link_validate = isp4vid_vdev_link_validate,
};

static const struct v4l2_file_operations isp4vid_vdev_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

static int isp4vid_ioctl_querycap(struct file *file, void *fh,
				  struct v4l2_capability *cap)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);

	strscpy(cap->driver, ISP4VID_ISP_DRV_NAME, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "%s", ISP4VID_ISP_DRV_NAME);
	cap->capabilities |= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;

	dev_dbg(isp_vdev->dev, "%s|capabilities=0x%X\n", isp_vdev->vdev.name,
		cap->capabilities);

	return 0;
}

static int isp4vid_g_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);

	f->fmt.pix = isp_vdev->format;

	return 0;
}

static int isp4vid_fill_buffer_size(struct v4l2_pix_format *fmt)
{
	int ret = 0;

	switch (fmt->pixelformat) {
	case V4L2_PIX_FMT_NV12:
		fmt->bytesperline = fmt->width;
		fmt->sizeimage = fmt->bytesperline * fmt->height * 3 / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
		fmt->bytesperline = fmt->width * 2;
		fmt->sizeimage = fmt->bytesperline * fmt->height;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int isp4vid_try_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);
	struct v4l2_pix_format *format = &f->fmt.pix;
	const struct v4l2_frmsize_discrete *fsz;
	size_t i;

	/*
	 * Check if the hardware supports the requested format, use the default
	 * format otherwise.
	 */
	for (i = 0; i < ARRAY_SIZE(isp4vid_formats); i++)
		if (isp4vid_formats[i] == format->pixelformat)
			break;

	if (i == ARRAY_SIZE(isp4vid_formats))
		format->pixelformat = ISP4VID_DEFAULT_FMT;

	switch (format->pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUYV:
		fsz = v4l2_find_nearest_size(isp4vid_frmsize,
					     ARRAY_SIZE(isp4vid_frmsize),
					     width, height, format->width,
					     format->height);
		format->width = fsz->width;
		format->height = fsz->height;
		break;
	default:
		dev_err(isp_vdev->dev, "%s|unsupported fmt=%u\n",
			isp_vdev->vdev.name,
			format->pixelformat);
		return -EINVAL;
	}

	/*
	 * There is no need to check the return value, as failure will never
	 * happen here
	 */
	isp4vid_fill_buffer_size(format);

	if (format->field == V4L2_FIELD_ANY)
		format->field = isp4vid_fmt_default.field;

	if (format->colorspace == V4L2_COLORSPACE_DEFAULT)
		format->colorspace = isp4vid_fmt_default.colorspace;

	return 0;
}

static int isp4vid_set_fmt_2_isp(struct v4l2_subdev *sdev,
				 struct v4l2_pix_format *pix_fmt)
{
	struct v4l2_subdev_format fmt = {};

	switch (pix_fmt->pixelformat) {
	case V4L2_PIX_FMT_NV12:
		fmt.format.code = MEDIA_BUS_FMT_YUYV8_1_5X8;
		break;
	case V4L2_PIX_FMT_YUYV:
		fmt.format.code = MEDIA_BUS_FMT_YUYV8_1X16;
		break;
	default:
		return -EINVAL;
	}
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.pad = ISP4VID_PAD_VIDEO_OUTPUT;
	fmt.format.width = pix_fmt->width;
	fmt.format.height = pix_fmt->height;
	return v4l2_subdev_call(sdev, pad, set_fmt, NULL, &fmt);
}

static int isp4vid_s_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);
	int ret;

	/* Do not change the format while stream is on */
	if (vb2_is_busy(&isp_vdev->vbq))
		return -EBUSY;

	ret = isp4vid_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	dev_dbg(isp_vdev->dev, "%s|width height:%ux%u->%ux%u\n",
		isp_vdev->vdev.name,
		isp_vdev->format.width, isp_vdev->format.height,
		f->fmt.pix.width, f->fmt.pix.height);
	dev_dbg(isp_vdev->dev, "%s|pixelformat:0x%x-0x%x\n",
		isp_vdev->vdev.name, isp_vdev->format.pixelformat,
		f->fmt.pix.pixelformat);
	dev_dbg(isp_vdev->dev, "%s|bytesperline:%u->%u\n",
		isp_vdev->vdev.name, isp_vdev->format.bytesperline,
		f->fmt.pix.bytesperline);
	dev_dbg(isp_vdev->dev, "%s|sizeimage:%u->%u\n",
		isp_vdev->vdev.name, isp_vdev->format.sizeimage,
		f->fmt.pix.sizeimage);

	isp_vdev->format = f->fmt.pix;
	ret = isp4vid_set_fmt_2_isp(isp_vdev->isp_sdev, &isp_vdev->format);

	return ret;
}

static int isp4vid_enum_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);

	switch (f->index) {
	case 0:
		f->pixelformat = V4L2_PIX_FMT_NV12;
		break;
	case 1:
		f->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(isp_vdev->dev, "%s|index=%d, pixelformat=0x%X\n",
		isp_vdev->vdev.name, f->index, f->pixelformat);

	return 0;
}

static int isp4vid_enum_framesizes(struct file *file, void *fh,
				   struct v4l2_frmsizeenum *fsize)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isp4vid_formats); i++) {
		if (isp4vid_formats[i] == fsize->pixel_format)
			break;
	}

	if (i == ARRAY_SIZE(isp4vid_formats))
		return -EINVAL;

	if (fsize->index < ARRAY_SIZE(isp4vid_frmsize)) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete = isp4vid_frmsize[fsize->index];
		dev_dbg(isp_vdev->dev, "%s|size[%d]=%dx%d\n",
			isp_vdev->vdev.name, fsize->index,
			fsize->discrete.width, fsize->discrete.height);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int isp4vid_ioctl_enum_frameintervals(struct file *file, void *priv,
					     struct v4l2_frmivalenum *fival)
{
	struct isp4vid_dev *isp_vdev = video_drvdata(file);
	size_t i;

	if (fival->index >= ARRAY_SIZE(isp4vid_tpfs))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(isp4vid_formats); i++)
		if (isp4vid_formats[i] == fival->pixel_format)
			break;

	if (i == ARRAY_SIZE(isp4vid_formats))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(isp4vid_frmsize); i++)
		if (isp4vid_frmsize[i].width == fival->width &&
		    isp4vid_frmsize[i].height == fival->height)
			break;

	if (i == ARRAY_SIZE(isp4vid_frmsize))
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = isp4vid_tpfs[fival->index];
	v4l2_simplify_fraction(&fival->discrete.numerator,
			       &fival->discrete.denominator, 8, 333);

	dev_dbg(isp_vdev->dev, "%s|interval[%d]=%d/%d\n",
		isp_vdev->vdev.name, fival->index,
		fival->discrete.numerator,
		fival->discrete.denominator);

	return 0;
}

static int isp4vid_ioctl_g_param(struct file *file, void *priv,
				 struct v4l2_streamparm *param)
{
	struct v4l2_captureparm *capture = &param->parm.capture;
	struct isp4vid_dev *isp_vdev = video_drvdata(file);

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	capture->capability   = V4L2_CAP_TIMEPERFRAME;
	capture->timeperframe = isp_vdev->timeperframe;
	capture->readbuffers  = 0;

	dev_dbg(isp_vdev->dev, "%s|timeperframe=%d/%d\n", isp_vdev->vdev.name,
		capture->timeperframe.numerator,
		capture->timeperframe.denominator);

	return 0;
}

static const struct v4l2_ioctl_ops isp4vid_vdev_ioctl_ops = {
	.vidioc_querycap            = isp4vid_ioctl_querycap,
	.vidioc_enum_fmt_vid_cap    = isp4vid_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap       = isp4vid_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap       = isp4vid_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap     = isp4vid_try_fmt_vid_cap,
	.vidioc_reqbufs             = vb2_ioctl_reqbufs,
	.vidioc_querybuf            = vb2_ioctl_querybuf,
	.vidioc_qbuf                = vb2_ioctl_qbuf,
	.vidioc_expbuf              = vb2_ioctl_expbuf,
	.vidioc_dqbuf               = vb2_ioctl_dqbuf,
	.vidioc_create_bufs         = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf         = vb2_ioctl_prepare_buf,
	.vidioc_streamon            = vb2_ioctl_streamon,
	.vidioc_streamoff           = vb2_ioctl_streamoff,
	.vidioc_g_parm              = isp4vid_ioctl_g_param,
	.vidioc_s_parm              = isp4vid_ioctl_g_param,
	.vidioc_enum_framesizes     = isp4vid_enum_framesizes,
	.vidioc_enum_frameintervals = isp4vid_ioctl_enum_frameintervals,
};

static unsigned int isp4vid_get_image_size(struct v4l2_pix_format *fmt)
{
	switch (fmt->pixelformat) {
	case V4L2_PIX_FMT_NV12:
		return fmt->width * fmt->height * 3 / 2;
	case V4L2_PIX_FMT_YUYV:
		return fmt->width * fmt->height * 2;
	default:
		return 0;
	}
}

static int isp4vid_qops_queue_setup(struct vb2_queue *vq,
				    unsigned int *nbuffers,
				    unsigned int *nplanes, unsigned int sizes[],
				    struct device *alloc_devs[])
{
	struct isp4vid_dev *isp_vdev = vb2_get_drv_priv(vq);
	unsigned int q_num_bufs = vb2_get_num_buffers(vq);

	if (*nplanes > 1) {
		dev_err(isp_vdev->dev,
			"fail to setup queue, no mplane supported %u\n",
			*nplanes);
		return -EINVAL;
	}

	if (*nplanes == 1) {
		unsigned int size;

		size = isp4vid_get_image_size(&isp_vdev->format);
		if (sizes[0] < size) {
			dev_err(isp_vdev->dev,
				"fail for small plane size %u, %u expected\n",
				sizes[0], size);
			return -EINVAL;
		}
	}

	if (q_num_bufs + *nbuffers < ISP4IF_MAX_STREAM_BUF_COUNT)
		*nbuffers = ISP4IF_MAX_STREAM_BUF_COUNT - q_num_bufs;

	switch (isp_vdev->format.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUYV: {
		*nplanes = 1;
		sizes[0] = max(sizes[0], isp_vdev->format.sizeimage);
		isp_vdev->format.sizeimage = sizes[0];
	}
	break;
	default:
		dev_err(isp_vdev->dev, "%s|unsupported fmt=%u\n",
			isp_vdev->vdev.name, isp_vdev->format.pixelformat);
		return -EINVAL;
	}

	dev_dbg(isp_vdev->dev, "%s|*nbuffers=%u *nplanes=%u sizes[0]=%u\n",
		isp_vdev->vdev.name,
		*nbuffers, *nplanes, sizes[0]);

	return 0;
}

static void isp4vid_qops_buffer_queue(struct vb2_buffer *vb)
{
	struct isp4vid_capture_buffer *buf =
		container_of(vb, struct isp4vid_capture_buffer, vb2.vb2_buf);
	struct isp4vid_dev *isp_vdev = vb2_get_drv_priv(vb->vb2_queue);
	struct isp4if_img_buf_info *img_buf = &buf->img_buf;
	void *vaddr = vb2_plane_vaddr(vb, 0);

	dev_dbg(isp_vdev->dev, "queue buf, vaddr %p, gpuva 0x%llx, size %u\n",
		vaddr, buf->gpu_addr, vb->planes[0].length);

	switch (isp_vdev->format.pixelformat) {
	case V4L2_PIX_FMT_NV12: {
		u32 y_size = isp_vdev->format.sizeimage / 3 * 2;
		u32 uv_size = isp_vdev->format.sizeimage / 3;

		img_buf->planes[0].len = y_size;
		img_buf->planes[0].sys_addr = vaddr;
		img_buf->planes[0].mc_addr = buf->gpu_addr;

		dev_dbg(isp_vdev->dev, "img_buf[0]: mc=0x%llx size=%u\n",
			img_buf->planes[0].mc_addr,
			img_buf->planes[0].len);

		img_buf->planes[1].len = uv_size;
		img_buf->planes[1].sys_addr = vaddr + y_size;
		img_buf->planes[1].mc_addr = buf->gpu_addr + y_size;

		dev_dbg(isp_vdev->dev, "img_buf[1]: mc=0x%llx size=%u\n",
			img_buf->planes[1].mc_addr,
			img_buf->planes[1].len);

		img_buf->planes[2].len = 0;
	}
	break;
	case V4L2_PIX_FMT_YUYV: {
		img_buf->planes[0].len = isp_vdev->format.sizeimage;
		img_buf->planes[0].sys_addr = vaddr;
		img_buf->planes[0].mc_addr = buf->gpu_addr;

		dev_dbg(isp_vdev->dev, "img_buf[0]: mc=0x%llx size=%u\n",
			img_buf->planes[0].mc_addr,
			img_buf->planes[0].len);

		img_buf->planes[1].len = 0;
		img_buf->planes[2].len = 0;
	}
	break;
	default:
		dev_err(isp_vdev->dev, "%s|unsupported fmt=%u\n",
			isp_vdev->vdev.name, isp_vdev->format.pixelformat);
		return;
	}

	if (isp_vdev->stream_started)
		isp4sd_ioc_send_img_buf(isp_vdev->isp_sdev, img_buf);

	scoped_guard(mutex, &isp_vdev->buf_list_lock)
		list_add_tail(&buf->list, &isp_vdev->buf_list);
}

static int isp4vid_qops_start_streaming(struct vb2_queue *vq,
					unsigned int count)
{
	struct isp4vid_dev *isp_vdev = vb2_get_drv_priv(vq);
	struct isp4vid_capture_buffer *isp4vid_buf;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	int ret = 0;

	isp_vdev->sequence = 0;

	ret = isp4sd_pwron_and_init(isp_vdev->isp_sdev);
	if (ret) {
		dev_err(isp_vdev->dev, "power up isp fail %d\n", ret);
		goto release_buffers;
	}

	entity = &isp_vdev->vdev.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_pad_remote_pad_first(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			dev_dbg(isp_vdev->dev, "fail start streaming: %s %d\n",
				subdev->name, ret);
			goto release_buffers;
		}
	}

	list_for_each_entry(isp4vid_buf, &isp_vdev->buf_list, list)
		isp4sd_ioc_send_img_buf(isp_vdev->isp_sdev,
					&isp4vid_buf->img_buf);

	isp_vdev->stream_started = true;

	return 0;

release_buffers:
	isp4vid_capture_return_all_buffers(isp_vdev, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void isp4vid_qops_stop_streaming(struct vb2_queue *vq)
{
	struct isp4vid_dev *isp_vdev = vb2_get_drv_priv(vq);
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	int ret;

	entity = &isp_vdev->vdev.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_pad_remote_pad_first(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 0);

		if (ret < 0 && ret != -ENOIOCTLCMD)
			dev_dbg(isp_vdev->dev, "fail stop streaming: %s %d\n",
				subdev->name, ret);
	}

	isp_vdev->stream_started = false;
	isp4sd_pwroff_and_deinit(isp_vdev->isp_sdev);

	/* Release all active buffers */
	isp4vid_capture_return_all_buffers(isp_vdev, VB2_BUF_STATE_ERROR);
}

static int isp4vid_qops_buf_init(struct vb2_buffer *vb)
{
	struct isp4vid_capture_buffer *buf =
		container_of(vb, struct isp4vid_capture_buffer, vb2.vb2_buf);
	struct isp4vid_dev *isp_vdev = vb2_get_drv_priv(vb->vb2_queue);
	void *mem_priv = vb->planes[0].mem_priv;
	struct device *dev = isp_vdev->dev;
	u64 gpu_addr;
	void *bo;
	int ret;

	if (vb->planes[0].dbuf) {
		buf->dbuf = vb->planes[0].dbuf;
	} else {
		/*
		 * HAS_DMA is a Kconfig dependency so CONFIG_HAS_DMA is always
		 * defined when this driver is compiled. The #else branch is
		 * kept as a safeguard in case the dependency is ever removed.
		 */
#ifdef CONFIG_HAS_DMA
		buf->dbuf = vb2_vmalloc_memops.get_dmabuf(vb, mem_priv, 0);
		if (IS_ERR_OR_NULL(buf->dbuf)) {
			dev_err(dev, "fail to get dma buf\n");
			return -EINVAL;
		}
#else
		dev_err(dev, "get dmabuf fail -- CONFIG_HAS_DMA not defined\n");
		buf->dbuf = NULL;
		return -EINVAL;
#endif
	}

	/* create isp user BO and obtain gpu_addr */
	ret = isp_user_buffer_alloc(dev, buf->dbuf, &bo, &gpu_addr);
	if (ret) {
		dev_err(dev, "fail to create isp user BO\n");
		if (!vb->planes[0].dbuf) {
			dma_buf_put(buf->dbuf);
			buf->dbuf = NULL;
		}

		return ret;
	}

	buf->bo = bo;
	buf->gpu_addr = gpu_addr;
	return 0;
}

static void isp4vid_qops_buf_cleanup(struct vb2_buffer *vb)
{
	struct isp4vid_capture_buffer *buf =
		container_of(vb, struct isp4vid_capture_buffer, vb2.vb2_buf);

	if (buf->bo) {
		isp_user_buffer_free(buf->bo);
		buf->bo = NULL;
	}

	/*
	 * Only put dmabufs we obtained ourselves via get_dmabuf, not ones
	 * provided by the framework for DMABUF import
	 */
	if (buf->dbuf && buf->dbuf != vb->planes[0].dbuf)
		dma_buf_put(buf->dbuf);

	buf->dbuf = NULL;
}

static const struct vb2_ops isp4vid_qops = {
	.queue_setup = isp4vid_qops_queue_setup,
	.buf_init = isp4vid_qops_buf_init,
	.buf_cleanup = isp4vid_qops_buf_cleanup,
	.start_streaming = isp4vid_qops_start_streaming,
	.stop_streaming = isp4vid_qops_stop_streaming,
	.buf_queue = isp4vid_qops_buffer_queue,
};

int isp4vid_dev_init(struct isp4vid_dev *isp_vdev, struct v4l2_subdev *isp_sd)
{
	const char *vdev_name = isp4vid_video_dev_name;
	struct v4l2_device *v4l2_dev;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	if (!isp_vdev || !isp_sd || !isp_sd->v4l2_dev)
		return -EINVAL;

	v4l2_dev = isp_sd->v4l2_dev;
	vdev = &isp_vdev->vdev;

	isp_vdev->isp_sdev = isp_sd;
	isp_vdev->dev = v4l2_dev->dev;

	/* Initialize the vb2_queue struct */
	mutex_init(&isp_vdev->vbq_lock);
	q = &isp_vdev->vbq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->buf_struct_size = sizeof(struct isp4vid_capture_buffer);
	q->min_queued_buffers = 2;
	q->ops = &isp4vid_qops;
	q->drv_priv = isp_vdev;
	q->mem_ops = &vb2_vmalloc_memops;
	q->lock = &isp_vdev->vbq_lock;
	q->dev = v4l2_dev->dev;
	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(v4l2_dev->dev, "vb2_queue_init error:%d\n", ret);
		return ret;
	}

	/* Initialize buffer list and its lock */
	mutex_init(&isp_vdev->buf_list_lock);
	INIT_LIST_HEAD(&isp_vdev->buf_list);

	/* Set default frame format */
	isp_vdev->format = isp4vid_fmt_default;
	isp_vdev->timeperframe = ISP4VID_ISP_TPF_DEFAULT;
	v4l2_simplify_fraction(&isp_vdev->timeperframe.numerator,
			       &isp_vdev->timeperframe.denominator, 8, 333);

	ret = isp4vid_fill_buffer_size(&isp_vdev->format);
	if (ret) {
		dev_err(v4l2_dev->dev, "fail to fill buffer size: %d\n", ret);
		goto err_release_vb2_queue;
	}

	ret = isp4vid_set_fmt_2_isp(isp_sd, &isp_vdev->format);
	if (ret) {
		dev_err(v4l2_dev->dev, "fail init format :%d\n", ret);
		goto err_release_vb2_queue;
	}

	/* Initialize the video_device struct */
	isp_vdev->vdev.entity.name = vdev_name;
	isp_vdev->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
	isp_vdev->vdev_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&isp_vdev->vdev.entity, 1,
				     &isp_vdev->vdev_pad);

	if (ret) {
		dev_err(v4l2_dev->dev, "init media entity pad fail:%d\n", ret);
		goto err_release_vb2_queue;
	}

	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_STREAMING | V4L2_CAP_IO_MC;
	vdev->entity.ops = &isp4vid_vdev_ent_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &isp4vid_vdev_fops;
	vdev->ioctl_ops = &isp4vid_vdev_ioctl_ops;
	vdev->lock = NULL;
	vdev->queue = q;
	vdev->v4l2_dev = v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	strscpy(vdev->name, vdev_name, sizeof(vdev->name));
	video_set_drvdata(vdev, isp_vdev);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(v4l2_dev->dev, "register video device fail:%d\n", ret);
		goto err_entity_cleanup;
	}

	return 0;

err_entity_cleanup:
	media_entity_cleanup(&isp_vdev->vdev.entity);
err_release_vb2_queue:
	vb2_queue_release(q);
	return ret;
}

void isp4vid_dev_deinit(struct isp4vid_dev *isp_vdev)
{
	vb2_video_unregister_device(&isp_vdev->vdev);
}
