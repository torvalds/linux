/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include "cif_isp10.h"
#include "cif_isp10_regs.h"
#include "cif_isp10_version.h"
#include <linux/module.h>
#include <linux/of.h>
#include <media/v4l2-controls_rockchip.h>
#include <linux/pm_runtime.h>
#include <linux/pagemap.h>
#include <linux/slab.h>

#define CIF_ISP10_V4L2_SP_DEV_MAJOR 0
#define CIF_ISP10_V4L2_ISP_DEV_MAJOR 1
#define CIF_ISP10_V4L2_MP_DEV_MAJOR 2
#define CIF_ISP10_V4L2_DMA_DEV_MAJOR 3

#define SP_DEV 0
#define MP_DEV 1
#define DMA_DEV 2
#define ISP_DEV 3

/* One structure per open file handle */
struct cif_isp10_v4l2_fh {
	enum cif_isp10_stream_id stream_id;
	struct v4l2_fh fh;
};

/* One structure per video node */
struct cif_isp10_v4l2_node {
	struct vb2_queue buf_queue;
	/* queue lock */
	struct mutex qlock;
	struct video_device vdev;
	struct media_pad pad;
	struct cif_isp10_pipeline *pipe;
	int users;
	struct cif_isp10_v4l2_fh *owner;
};

/* One structure per device */
struct cif_isp10_v4l2_device {
	struct cif_isp10_v4l2_node node[4];
};

static struct cif_isp10_v4l2_fh *to_fh(struct file *file)
{
	if (!file || !file->private_data)
		return NULL;

	return container_of(file->private_data, struct cif_isp10_v4l2_fh, fh);
}

static struct cif_isp10_v4l2_node *to_node(struct cif_isp10_v4l2_fh *fh)
{
	struct video_device *vdev = fh ? fh->fh.vdev : NULL;

	if (!fh || !vdev)
		return NULL;

	return container_of(vdev, struct cif_isp10_v4l2_node, vdev);
}

static inline struct cif_isp10_v4l2_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct cif_isp10_v4l2_node, buf_queue);
}

static inline struct cif_isp10_buffer *to_cif_isp10_vb(
	struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct cif_isp10_buffer, vb);
}

static struct vb2_queue *to_vb2_queue(
	struct file *file)
{
	struct cif_isp10_v4l2_fh *fh = to_fh(file);
	struct video_device *vdev = fh ? fh->fh.vdev : NULL;
	struct cif_isp10_v4l2_node *node = to_node(fh);
	struct vb2_queue *q;

	if (unlikely(!vdev)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"vdev is NULL\n");
		WARN_ON(1);
	}
	q = &node->buf_queue;
	if (unlikely(!q)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"buffer queue is NULL\n");
		WARN_ON(1);
	}

	return q;
}

static enum cif_isp10_stream_id to_stream_id(
	struct file *file)
{
	struct cif_isp10_v4l2_fh *fh;

	if (unlikely(!file)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"NULL file handle\n");
		WARN_ON(1);
	}
	fh = to_fh(file);
	if (unlikely(!fh)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"fh is NULL\n");
		WARN_ON(1);
	}

	return fh->stream_id;
}

static struct cif_isp10_device *to_cif_isp10_device(
	struct vb2_queue *queue)
{
	return queue->drv_priv;
}

static enum cif_isp10_stream_id to_cif_isp10_stream_id(
	struct vb2_queue *queue)
{
	struct cif_isp10_v4l2_node *node =
		container_of(queue, struct cif_isp10_v4l2_node, buf_queue);
	struct video_device *vdev =
		&node->vdev;

	if (!strcmp(vdev->name, SP_VDEV_NAME))
		return CIF_ISP10_STREAM_SP;
	if (!strcmp(vdev->name, MP_VDEV_NAME))
		return CIF_ISP10_STREAM_MP;
	if (!strcmp(vdev->name, DMA_VDEV_NAME))
		return CIF_ISP10_STREAM_DMA;

	cif_isp10_pltfrm_pr_err(NULL,
		"unsupported/unknown device name %s\n", vdev->name);
	return -EINVAL;
}

static const char *cif_isp10_v4l2_buf_type_string(
	enum v4l2_buf_type buf_type)
{
	switch (buf_type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return "VIDEO_CAPTURE";
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return "VIDEO_OVERLAY";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return "VIDEO_OUTPUT";
	default:
		break;
	}
	return "UNKNOWN/UNSUPPORTED";
}

const char *cif_isp10_v4l2_pix_fmt_string(
	int pix_fmt)
{
	switch (pix_fmt) {
	case V4L2_PIX_FMT_RGB332:
		return "V4L2-RGB332";
	case V4L2_PIX_FMT_RGB555:
		return "V4L2-RGB555";
	case V4L2_PIX_FMT_RGB565:
		return "V4L2-RGB565";
	case V4L2_PIX_FMT_RGB555X:
		return "V4L2-RGB555X";
	case V4L2_PIX_FMT_RGB565X:
		return "V4L2-RGB565X";
	case V4L2_PIX_FMT_BGR24:
		return "V4L2-BGR24";
	case V4L2_PIX_FMT_RGB24:
		return "V4L2-RGB24";
	case V4L2_PIX_FMT_BGR32:
		return "V4L2-BGR32";
	case V4L2_PIX_FMT_RGB32:
		return "V4L2-RGB32";
	case V4L2_PIX_FMT_GREY:
		return "V4L2-GREY";
	case V4L2_PIX_FMT_YVU410:
		return "V4L2-YVU410";
	case V4L2_PIX_FMT_YVU420:
		return "V4L2-YVU420";
	case V4L2_PIX_FMT_YUYV:
		return "V4L2-YUYV";
	case V4L2_PIX_FMT_UYVY:
		return "V4L2-UYVY";
	case V4L2_PIX_FMT_YUV422P:
		return "V4L2-YUV422P";
	case V4L2_PIX_FMT_YUV411P:
		return "V4L2-YUV411P";
	case V4L2_PIX_FMT_Y41P:
		return "V4L2-Y41P";
	case V4L2_PIX_FMT_NV12:
		return "V4L2-NV12";
	case V4L2_PIX_FMT_NV21:
		return "V4L2-NV21";
	case V4L2_PIX_FMT_YUV410:
		return "V4L2-YUV410";
	case V4L2_PIX_FMT_YUV420:
		return "V4L2--YUV420";
	case V4L2_PIX_FMT_YYUV:
		return "V4L2-YYUV";
	case V4L2_PIX_FMT_HI240:
		return "V4L2-HI240";
	case V4L2_PIX_FMT_WNVA:
		return "V4L2-WNVA";
	case V4L2_PIX_FMT_NV16:
		return "V4L2-NV16";
	case V4L2_PIX_FMT_YUV444:
		return "V4L2-YUV444P";
	case V4L2_PIX_FMT_NV24:
		return "M5-YUV444SP";
	case V4L2_PIX_FMT_JPEG:
		return "V4L2-JPEG";
	case V4L2_PIX_FMT_SGRBG10:
		return "RAW-BAYER-10Bits";
	case V4L2_PIX_FMT_SGRBG8:
		return "RAW-BAYER-8Bits";
	}
	return "UNKNOWN/UNSUPPORTED";
}

static int cif_isp10_v4l2_cid2cif_isp10_cid(u32 v4l2_cid)
{
	switch (v4l2_cid) {
	case V4L2_CID_FLASH_LED_MODE:
		return CIF_ISP10_CID_FLASH_MODE;
	case V4L2_CID_AUTOGAIN:
		return CIF_ISP10_CID_AUTO_GAIN;
	case V4L2_EXPOSURE_AUTO:
		return CIF_ISP10_CID_AUTO_EXPOSURE;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return CIF_ISP10_CID_AUTO_WHITE_BALANCE;
	case V4L2_CID_BLACK_LEVEL:
		return CIF_ISP10_CID_BLACK_LEVEL;
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		return CIF_ISP10_CID_WB_TEMPERATURE;
	case V4L2_CID_EXPOSURE:
		return CIF_ISP10_CID_EXPOSURE_TIME;
	case V4L2_CID_GAIN:
		return CIF_ISP10_CID_ANALOG_GAIN;
	case V4L2_CID_FOCUS_ABSOLUTE:
		return CIF_ISP10_CID_FOCUS_ABSOLUTE;
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		return CIF_ISP10_CID_AUTO_N_PRESET_WHITE_BALANCE;
	case V4L2_CID_SCENE_MODE:
		return CIF_ISP10_CID_SCENE_MODE;
	case V4L2_CID_COLORFX:
		return CIF_ISP10_CID_IMAGE_EFFECT;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		return CIF_ISP10_CID_JPEG_QUALITY;
	case V4L2_CID_HFLIP:
		return CIF_ISP10_CID_HFLIP;
	case V4L2_CID_VFLIP:
		return CIF_ISP10_CID_VFLIP;
	case V4L2_CID_ISO_SENSITIVITY:
		return CIF_ISP10_CID_ISO_SENSITIVITY;
	case RK_V4L2_CID_AUTO_FPS:
		return CIF_ISP10_CID_AUTO_FPS;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		return CIF_ISP10_CID_MIN_BUFFER_FOR_CAPTURE;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupported V4L2 CID 0x%x\n",
			v4l2_cid);
		break;
	}
	return -EINVAL;
}

static enum cif_isp10_image_effect cif_isp10_v4l2_colorfx2cif_isp10_ie(
	u32 v4l2_colorfx)
{
	switch (v4l2_colorfx) {
	case V4L2_COLORFX_SEPIA:
		return CIF_ISP10_IE_SEPIA;
	case V4L2_COLORFX_BW:
		return CIF_ISP10_IE_BW;
	case V4L2_COLORFX_NEGATIVE:
		return CIF_ISP10_IE_NEGATIVE;
	case V4L2_COLORFX_EMBOSS:
		return CIF_ISP10_IE_EMBOSS;
	case V4L2_COLORFX_SKETCH:
		return CIF_ISP10_IE_SKETCH;
	case V4L2_COLORFX_NONE:
		return CIF_ISP10_IE_NONE;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupported V4L2 COLORFX %d\n",
			v4l2_colorfx);
		break;
	}
	return -EINVAL;
}

static enum cif_isp10_pix_fmt cif_isp10_v4l2_pix_fmt2cif_isp10_pix_fmt(
	u32 v4l2_pix_fmt, struct vb2_queue *queue)
{
/*struct cif_isp10_v4l2_node *node =
 *	container_of(queue, struct cif_isp10_v4l2_node, buf_queue);
 *	struct video_device *vdev =
 *	&node->vdev;
 */

	switch (v4l2_pix_fmt) {
	case V4L2_PIX_FMT_GREY:
		return CIF_YUV400;
	case V4L2_PIX_FMT_YUV420:
		return CIF_YUV420P;
	case V4L2_PIX_FMT_YVU420:
		return CIF_YVU420P;
	case V4L2_PIX_FMT_NV12:
		return CIF_YUV420SP;
	case V4L2_PIX_FMT_NV21:
		return CIF_YVU420SP;
	case V4L2_PIX_FMT_YUYV:
		return CIF_YUV422I;
	case V4L2_PIX_FMT_YVYU:
		return CIF_YVU422I;
	case V4L2_PIX_FMT_UYVY:
		return CIF_UYV422I;
	case V4L2_PIX_FMT_YUV422P:
		return CIF_YUV422P;
	case V4L2_PIX_FMT_NV16:
		return CIF_YUV422SP;
	case V4L2_PIX_FMT_YUV444:
		return CIF_YUV444P;
	case V4L2_PIX_FMT_NV24:
		return CIF_YUV444SP;
	case V4L2_PIX_FMT_RGB565:
		return CIF_RGB565;
	case V4L2_PIX_FMT_RGB24:
		return CIF_RGB888;
	case V4L2_PIX_FMT_SBGGR8:
		return CIF_BAYER_SBGGR8;
	case V4L2_PIX_FMT_SGBRG8:
		return CIF_BAYER_SGBRG8;
	case V4L2_PIX_FMT_SGRBG8:
		return CIF_BAYER_SGRBG8;
	case V4L2_PIX_FMT_SRGGB8:
		return CIF_BAYER_SRGGB8;
	case V4L2_PIX_FMT_SBGGR10:
		return CIF_BAYER_SBGGR10;
	case V4L2_PIX_FMT_SGBRG10:
		return CIF_BAYER_SGBRG10;
	case V4L2_PIX_FMT_SGRBG10:
		return CIF_BAYER_SGRBG10;
	case V4L2_PIX_FMT_SRGGB10:
		return CIF_BAYER_SRGGB10;
	case V4L2_PIX_FMT_SBGGR12:
		return CIF_BAYER_SBGGR12;
	case V4L2_PIX_FMT_SGBRG12:
		return CIF_BAYER_SGBRG12;
	case V4L2_PIX_FMT_SGRBG12:
		return CIF_BAYER_SGRBG12;
	case V4L2_PIX_FMT_SRGGB12:
		return CIF_BAYER_SRGGB12;
	case V4L2_PIX_FMT_JPEG:
		return CIF_JPEG;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown or unsupported V4L2 pixel format %c%c%c%c\n",
			(u8)(v4l2_pix_fmt & 0xff),
			(u8)((v4l2_pix_fmt >> 8) & 0xff),
			(u8)((v4l2_pix_fmt >> 16) & 0xff),
			(u8)((v4l2_pix_fmt >> 24) & 0xff));
		return CIF_UNKNOWN_FORMAT;
	}
}

static u32 cif_isp10_pix_fmt2v4l2_pix_fmt(
		enum cif_isp10_pix_fmt pix_fmt, struct vb2_queue *queue)
{
/*struct cif_isp10_v4l2_node *node =
 *	container_of(queue, struct cif_isp10_v4l2_node, buf_queue);
 *	struct video_device *vdev =
 *	&node->vdev;
 */

	switch (pix_fmt) {
	case CIF_YUV400:
		return V4L2_PIX_FMT_GREY;
	case CIF_YUV420P:
		return V4L2_PIX_FMT_YUV420;
	case CIF_YVU420P:
		return V4L2_PIX_FMT_YVU420;
	case CIF_YUV420SP:
		return V4L2_PIX_FMT_NV12;
	case CIF_YVU420SP:
		return V4L2_PIX_FMT_NV21;
	case CIF_YUV422I:
		return V4L2_PIX_FMT_YUYV;
	case CIF_UYV422I:
		return V4L2_PIX_FMT_UYVY;
	case CIF_YUV422P:
		return V4L2_PIX_FMT_YUV422P;
	case CIF_YUV422SP:
		return V4L2_PIX_FMT_NV16;
	case CIF_YUV444P:
		return V4L2_PIX_FMT_YUV444;
	case CIF_YUV444SP:
		return V4L2_PIX_FMT_NV24;
	case CIF_RGB565:
		return V4L2_PIX_FMT_RGB565;
	case CIF_RGB888:
		return V4L2_PIX_FMT_RGB24;
	case CIF_BAYER_SBGGR8:
		return V4L2_PIX_FMT_SBGGR8;
	case CIF_BAYER_SGBRG8:
		return V4L2_PIX_FMT_SGBRG8;
	case CIF_BAYER_SGRBG8:
		return V4L2_PIX_FMT_SGRBG8;
	case CIF_BAYER_SRGGB8:
		return V4L2_PIX_FMT_SRGGB8;
	case CIF_BAYER_SBGGR10:
		return V4L2_PIX_FMT_SBGGR10;
	case CIF_BAYER_SGBRG10:
		return V4L2_PIX_FMT_SGBRG10;
	case CIF_BAYER_SGRBG10:
		return V4L2_PIX_FMT_SGRBG10;
	case CIF_BAYER_SRGGB10:
		return V4L2_PIX_FMT_SRGGB10;
	case CIF_BAYER_SBGGR12:
		return V4L2_PIX_FMT_SBGGR12;
	case CIF_BAYER_SGBRG12:
		return V4L2_PIX_FMT_SGBRG12;
	case CIF_BAYER_SGRBG12:
		return V4L2_PIX_FMT_SGRBG12;
	case CIF_BAYER_SRGGB12:
		return V4L2_PIX_FMT_SRGGB12;
	case CIF_JPEG:
		return V4L2_PIX_FMT_JPEG;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown or unsupported V4L2 pixel format %x\n",
			pix_fmt);
		return 0;
	}
}

static int cif_isp10_v4l2_register_video_device(
	struct cif_isp10_device *dev,
	struct video_device *vdev,
	const char *name,
	int qtype,
	int major,
	const struct v4l2_file_operations *fops,
	const struct v4l2_ioctl_ops *ioctl_ops)
{
	int ret;

	vdev->release = video_device_release;
	strlcpy(vdev->name, name, sizeof(vdev->name));
	vdev->vfl_type = qtype;
	vdev->fops = fops;
	video_set_drvdata(vdev, dev);
	vdev->minor = -1;
	vdev->ioctl_ops = ioctl_ops;
	vdev->v4l2_dev = &dev->v4l2_dev;
	if (qtype == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		vdev->vfl_dir = VFL_DIR_TX;
	else
		vdev->vfl_dir = VFL_DIR_RX;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, major);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"video_register_device failed with error %d\n", ret);
		goto err;
	}

	cif_isp10_pltfrm_pr_info(NULL,
		"video device video%d.%d (%s) successfully registered\n",
		major, vdev->minor, name);

	return 0;
err:
	video_device_release(vdev);
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with err %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_streamon(
	struct file *file,
	void *priv,
	enum v4l2_buf_type buf_type)
{
	int ret;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	static u32 streamon_cnt_sp;
	static u32 streamon_cnt_mp;
	static u32 streamon_cnt_dma;
	u32 stream_ids = to_stream_id(file);

	cif_isp10_pltfrm_pr_dbg(dev->dev, "%s(%d)\n",
		cif_isp10_v4l2_buf_type_string(queue->type),
		(stream_ids & CIF_ISP10_STREAM_MP) ? ++streamon_cnt_mp :
		((stream_ids & CIF_ISP10_STREAM_SP) ? ++streamon_cnt_sp :
		++streamon_cnt_dma));

	ret = vb2_streamon(queue, buf_type);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"videobuf_streamon failed\n");
		goto err;
	}

	ret = cif_isp10_streamon(dev, stream_ids);
	if (IS_ERR_VALUE(ret)) {
		goto err;
	}

	return 0;
err:
	(void)vb2_queue_release(queue);
	cif_isp10_pltfrm_pr_err(dev->dev, "failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_do_streamoff(
	struct file *file)
{
	int ret = 0;
	int err;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	u32 stream_ids = to_stream_id(file);

	cif_isp10_pltfrm_pr_dbg(dev->dev, "%s\n",
		cif_isp10_v4l2_buf_type_string(queue->type));

	err = cif_isp10_streamoff(dev, stream_ids);
	if (IS_ERR_VALUE(err))
		ret = -EFAULT;
	err = vb2_streamoff(queue, queue->type);
	if (IS_ERR_VALUE(err)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"videobuf_streamoff failed with error %d\n", err);
		ret = -EFAULT;
	}

	vb2_queue_release(queue);

	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_err(dev->dev,
			"failed with error %d\n", ret);

	return ret;
}

static int cif_isp10_v4l2_streamoff(
	struct file *file,
	void *priv,
	enum v4l2_buf_type buf_type)
{
	int ret = cif_isp10_v4l2_do_streamoff(file);

	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_err(NULL,
			"failed with error %d\n", ret);

	return ret;
}

static int cif_isp10_v4l2_qbuf(
	struct file *file,
	void *priv,
	struct v4l2_buffer *buf)
{
	int ret;

	ret = vb2_ioctl_qbuf(file, priv, buf);
	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_err(NULL,
			"videobuf_qbuf failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_dqbuf(
	struct file *file,
	void *priv,
	struct v4l2_buffer *buf)
{
	int ret;

	ret = vb2_ioctl_dqbuf(file, priv, buf);
	if (IS_ERR_VALUE(ret) && (ret != -EAGAIN))
		cif_isp10_pltfrm_pr_err(NULL,
			"videobuf_dqbuf failed with error %d\n", ret);
	else
		cif_isp10_pltfrm_pr_dbg(NULL,
			"dequeued buffer %d, size %d\n",
			buf->index, buf->length);
	return ret;
}

static int cif_isp10_v4l2_reqbufs(
	struct file *file,
	void *priv,
	struct v4l2_requestbuffers *req)
{
	int ret;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_stream_id strm = to_cif_isp10_stream_id(queue);

	ret = vb2_ioctl_reqbufs(file, priv, req);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"videobuf_reqbufs failed with error %d\n", ret);
	}
	cif_isp10_reqbufs(dev, strm, req);
	return ret;
}

static int cif_isp10_v4l2_querybuf(
	struct file *file,
	void *priv,
	struct v4l2_buffer *buf)
{
	int ret;

	ret = vb2_ioctl_querybuf(file, priv, buf);
	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_err(NULL,
			"videobuf_querybuf failed with error %d\n", ret);

	return ret;
}

static int cif_isp10_v4l2_s_ctrl(
	struct file *file,
	void *priv,
	struct v4l2_control *vc)
{
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_cid id =
		cif_isp10_v4l2_cid2cif_isp10_cid(vc->id);
	int val = vc->value;

	if (IS_ERR_VALUE(id))
		return id;

	switch (vc->id) {
	case V4L2_CID_COLORFX:
		val = cif_isp10_v4l2_colorfx2cif_isp10_ie(val);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		if (vc->value == V4L2_FLASH_LED_MODE_NONE)
			val = CIF_ISP10_FLASH_MODE_OFF;
		else if (vc->value == V4L2_FLASH_LED_MODE_FLASH)
			val = CIF_ISP10_FLASH_MODE_FLASH;
		else if (vc->value == V4L2_FLASH_LED_MODE_TORCH)
			val = CIF_ISP10_FLASH_MODE_TORCH;
		else
			val = -EINVAL;
		break;
	default:
		break;
	}

	return cif_isp10_s_ctrl(dev, id, val);
}

static int cif_isp10_v4l2_s_fmt(
	struct file *file,
	void *priv,
	struct v4l2_format *f)
{
	int ret;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	struct cif_isp10_v4l2_fh *fh = to_fh(file);
	struct cif_isp10_v4l2_node *node = to_node(fh);
	struct cif_isp10_strm_fmt strm_fmt;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"%s\n",
		cif_isp10_v4l2_buf_type_string(queue->type));

	if (node->owner && node->owner != fh)
		return -EBUSY;

	strm_fmt.frm_fmt.pix_fmt =
		cif_isp10_v4l2_pix_fmt2cif_isp10_pix_fmt(
			f->fmt.pix.pixelformat, queue);
	strm_fmt.frm_fmt.width = f->fmt.pix.width;
	strm_fmt.frm_fmt.height = f->fmt.pix.height;
/* strm_fmt.frm_fmt.quantization = f->fmt.pix.quantization; */
	strm_fmt.frm_fmt.quantization = 0;
	ret = cif_isp10_s_fmt(dev,
		to_stream_id(file),
		&strm_fmt,
		f->fmt.pix.bytesperline);

	//TODO:: check s_fmt format field and size;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	cif_isp10_calc_min_out_buff_size(dev,
		to_stream_id(file), &f->fmt.pix.sizeimage, false);
	//f->fmt.pix.sizeimage = PAGE_ALIGN(f->fmt.pix.sizeimage);

	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

/* existence of this function is checked by V4L2 */
static int cif_isp10_v4l2_g_fmt(
	struct file *file,
	void *priv,
	struct v4l2_format *f)
{
	enum cif_isp10_pix_fmt pix_fmt;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_stream_id stream_id = to_cif_isp10_stream_id(queue);

	switch (stream_id) {
	case CIF_ISP10_STREAM_SP:
		pix_fmt = dev->config.mi_config.sp.output.pix_fmt;
		f->fmt.pix.width =
			dev->config.mi_config.sp.output.width;
		f->fmt.pix.height =
			dev->config.mi_config.sp.output.height;
		f->fmt.pix.pixelformat = cif_isp10_pix_fmt2v4l2_pix_fmt(pix_fmt, queue);
		break;
	case CIF_ISP10_STREAM_MP:
		pix_fmt = dev->config.mi_config.mp.output.pix_fmt;
		f->fmt.pix.width =
			dev->config.mi_config.mp.output.width;
		f->fmt.pix.height =
			dev->config.mi_config.mp.output.height;
		f->fmt.pix.pixelformat = cif_isp10_pix_fmt2v4l2_pix_fmt(pix_fmt, queue);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cif_isp10_v4l2_g_input(
	struct file *file,
	void *priv,
	unsigned int *i)
{
	int ret;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);

	ret = cif_isp10_g_input(dev, i);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_s_input(
	struct file *file,
	void *priv,
	unsigned int i)
{
	int ret;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);

	cif_isp10_pltfrm_pr_dbg(dev->dev, "setting input to %d\n", i);

	ret = cif_isp10_s_input(dev, i);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_enum_framesizes(
	struct file *file,
	void *priv,
	struct v4l2_frmsizeenum *fsize)
{
	/* THIS FUNCTION IS UNDER CONSTRUCTION */
	int ret;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);

	if (IS_ERR_OR_NULL(dev->img_src)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"input has not yet been selected, cannot enumerate formats\n");
		ret = -ENODEV;
		goto err;
	}

	return -EINVAL;
err:
	cif_isp10_pltfrm_pr_err(NULL, "failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_vb2_queue_setup(struct vb2_queue *queue,
			const void *parg,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	int ret;
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_stream_id strm = to_cif_isp10_stream_id(queue);

	cif_isp10_pltfrm_pr_dbg(NULL,
		"%s request planes %d count %d, size %d\n",
		cif_isp10_v4l2_buf_type_string(queue->type),
		*num_planes,
		*num_buffers, sizes[0]);

	if (*num_planes == 0)
		*num_planes = 1;

	if (*num_buffers == 0)
		*num_buffers = 4;

	alloc_ctxs[0] = dev->alloc_ctx;

	ret = cif_isp10_calc_min_out_buff_size(dev, strm, &sizes[0], false);
	//sizes[0] = PAGE_ALIGN(sizes[0] );
	if (ret)
		return -EINVAL;

	cif_isp10_pltfrm_pr_dbg(NULL, "%s count %d, size %d\n",
		cif_isp10_v4l2_buf_type_string(queue->type),
		*num_buffers, sizes[0]);

	return 0;
}

static void cif_isp10_v4l2_vb2_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cif_isp10_buffer *ispbuf = to_cif_isp10_vb(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_stream_id strm = to_cif_isp10_stream_id(queue);
	struct cif_isp10_stream *stream = to_stream_by_id(dev, strm);
	u32 size;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"buffer type %s\n",
		cif_isp10_v4l2_buf_type_string(queue->type));

	list_add_tail(&ispbuf->queue, &stream->buf_queue);

	cif_isp10_calc_min_out_buff_size(dev, strm, &size, false);
	//size = PAGE_ALIGN(size);
	vb2_set_plane_payload(vb, 0, size);
}

static void cif_isp10_v4l2_vb2_stop_streaming(struct vb2_queue *queue)
{
	struct cif_isp10_v4l2_node *node;
	enum cif_isp10_stream_id strm = to_cif_isp10_stream_id(queue);
	struct cif_isp10_stream *stream = NULL;
	struct cif_isp10_device *dev;
	struct cif_isp10_buffer *buf, *buf_tmp;
	unsigned long lock_flags = 0;

	node = queue_to_node(queue);

	dev = video_get_drvdata(&node->vdev);

	stream = to_stream_by_id(dev, strm);

	spin_lock_irqsave(&dev->vbq_lock, lock_flags);

	if (stream->curr_buf) {
		vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
			VB2_BUF_STATE_ERROR);
		stream->curr_buf = NULL;
	}
	if (stream->next_buf) {
		vb2_buffer_done(&stream->next_buf->vb.vb2_buf,
			VB2_BUF_STATE_ERROR);
		stream->next_buf = NULL;
	}

	list_for_each_entry_safe(buf, buf_tmp, &stream->buf_queue, queue) {
		list_del(&buf->queue);
		if (buf->vb.vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(&buf->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->vbq_lock, lock_flags);
}

static struct vb2_ops cif_isp10_v4l2_vb2_ops = {
	.queue_setup	= cif_isp10_v4l2_vb2_queue_setup,
	.buf_queue	= cif_isp10_v4l2_vb2_queue,
	//.buf_cleanup	= cif_isp10_v4l2_vb2_release,
	//.buf_init	= cif_isp10_v4l2_vb2_init,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
	.stop_streaming	= cif_isp10_v4l2_vb2_stop_streaming,
};

static int cif_isp10_init_vb2_queue(struct vb2_queue *q,
	struct cif_isp10_device *dev,
	enum v4l2_buf_type buf_type)
{
	struct cif_isp10_v4l2_node *node;

	memset(q, 0, sizeof(*q));
	node = queue_to_node(q);
	mutex_init(&node->qlock);

	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = dev;
	q->ops = &cif_isp10_v4l2_vb2_ops;
	q->buf_struct_size = sizeof(struct cif_isp10_buffer);
	q->min_buffers_needed	= 4;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->qlock;

#ifdef CIF_ISP10_MODE_DMA_CONTIG
	q->mem_ops = &vb2_dma_contig_memops;
	dev->alloc_ctx = vb2_dma_contig_init_ctx(dev->dev);
#endif

#ifdef CIF_ISP10_MODE_DMA_SG
	q->mem_ops = &vb2_dma_sg_memops;
	dev->alloc_ctx = vb2_dma_sg_init_ctx(dev->dev);
#endif
	return vb2_queue_init(q);
}

static int cif_isp10_v4l2_open(
	struct file *file)
{
	int ret;
	struct video_device *vdev = video_devdata(file);
	struct cif_isp10_device *dev = video_get_drvdata(vdev);
	struct cif_isp10_v4l2_fh *fh;
	struct cif_isp10_v4l2_node *node;
	enum v4l2_buf_type buf_type;
	enum cif_isp10_stream_id stream_id;
	struct cif_isp10_v4l2_device *cif_isp10_v4l2_dev =
		(struct cif_isp10_v4l2_device *)dev->nodes;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"video device video%d.%d (%s)\n",
		vdev->num, vdev->minor, vdev->name);

	if (vdev->minor == cif_isp10_v4l2_dev->node[SP_DEV].vdev.minor) {
		buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		stream_id = CIF_ISP10_STREAM_SP;
	} else if (vdev->minor == cif_isp10_v4l2_dev->node[MP_DEV].vdev.minor) {
		buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		stream_id = CIF_ISP10_STREAM_MP;
	} else if (vdev->minor ==
				cif_isp10_v4l2_dev->node[DMA_DEV].vdev.minor) {
		buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		stream_id = CIF_ISP10_STREAM_DMA;
	} else {
		cif_isp10_pltfrm_pr_err(NULL,
			"invalid video device video%d.%d (%s)\n",
			vdev->num, vdev->minor, vdev->name);
		ret = -EINVAL;
		goto err;
	}

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh) {
		cif_isp10_pltfrm_pr_err(NULL,
			"memory allocation failed\n");
		ret = -ENOMEM;
		goto err;
	}
	fh->stream_id = stream_id;

	file->private_data = &fh->fh;
	v4l2_fh_init(&fh->fh, vdev);
	v4l2_fh_add(&fh->fh);

	node = to_node(fh);
	if (++node->users > 1)
		return 0;

	/* First open of the device, so initialize everything */
	node->owner = NULL;

	cif_isp10_init_vb2_queue(to_vb2_queue(file), dev, buf_type);
	vdev->queue = to_vb2_queue(file);

	ret = cif_isp10_init(dev, to_stream_id(file));
	if (IS_ERR_VALUE(ret)) {
		v4l2_fh_del(&fh->fh);
		v4l2_fh_exit(&fh->fh);
		kfree(fh);
		node->users--;
		goto err;
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_release(struct file *file)
{
	int ret = 0;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	struct cif_isp10_v4l2_fh *fh = to_fh(file);
	struct cif_isp10_v4l2_node *node = to_node(fh);
	enum cif_isp10_stream_id stream_id = to_stream_id(file);

	cif_isp10_pltfrm_pr_dbg(dev->dev, "%s\n",
		cif_isp10_v4l2_buf_type_string(queue->type));

	if (node->users) {
		--node->users;
	} else {
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"number of users for this device is already 0\n");
		return 0;
	}

	if (!node->users) {
		if (queue->streaming)
			if (IS_ERR_VALUE(cif_isp10_v4l2_do_streamoff(file)))
				cif_isp10_pltfrm_pr_warn(dev->dev,
					"streamoff failed\n");

		/* Last close, so uninitialize hardware */
		ret = cif_isp10_release(dev, stream_id);
	}

	if (node->owner == fh)
		node->owner = NULL;

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	kfree(fh);

	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_err(dev->dev,
			"failed with error %d\n", ret);
	return ret;
}

/*
static unsigned int cif_isp10_v4l2_poll(
	struct file *file,
	struct poll_table_struct *wait)
{
	struct cif_isp10_v4l2_fh *fh = to_fh(file);
	int ret = 0;
	struct videobuf_queue *queue = to_videobuf_queue(file);
	unsigned long req_events = poll_requested_events(wait);

	cif_isp10_pltfrm_pr_dbg(NULL, "%s\n",
		cif_isp10_v4l2_buf_type_string(queue->type));

	if (v4l2_event_pending(&fh->fh))
		ret = POLLPRI;
	else if (req_events & POLLPRI)
		poll_wait(file, &fh->fh.wait, wait);

	if (!(req_events & (POLLIN | POLLOUT | POLLRDNORM)))
		return ret;

	ret |= videobuf_poll_stream(file, queue, wait);
	if (ret & POLLERR) {
		cif_isp10_pltfrm_pr_err(NULL,
			"videobuf_poll_stream failed with error 0x%x\n", ret);
	}
	return ret;
}
*/

/*
 * VMA operations.
 */
/*
static void cif_isp10_v4l2_vm_open(struct vm_area_struct *vma)
{
	struct cif_isp10_metadata_s *metadata =
		(struct cif_isp10_metadata_s *)vma->vm_private_data;

	metadata->vmas++;
}

static void cif_isp10_v4l2_vm_close(struct vm_area_struct *vma)
{
	struct cif_isp10_metadata_s *metadata =
		(struct cif_isp10_metadata_s *)vma->vm_private_data;

	metadata->vmas--;
}

static const struct vm_operations_struct cif_isp10_vm_ops = {
	.open		= cif_isp10_v4l2_vm_open,
	.close		= cif_isp10_v4l2_vm_close,
};

int cif_isp10_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct videobuf_queue *queue = to_videobuf_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_stream_id strm = to_stream_id(file);
	int retval;

	retval = cif_isp10_mmap(dev, strm, vma);
	if (retval < 0)
		goto done;

	vma->vm_ops          = &cif_isp10_vm_ops;
	vma->vm_flags       |= VM_DONTEXPAND | VM_DONTDUMP;
	cif_isp10_v4l2_vm_open(vma);

done:
	return retval;
}
*/

const struct v4l2_file_operations cif_isp10_v4l2_fops = {
	.open = cif_isp10_v4l2_open,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = video_ioctl2,
#endif
	.release = cif_isp10_v4l2_release,
	//.poll = cif_isp10_v4l2_poll,
	//.mmap = cif_isp10_v4l2_mmap,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

/*TBD: clean up code below this line******************************************/

static int v4l2_querycap(struct file *file,
			 void *priv, struct v4l2_capability *cap)
{
	struct vb2_queue *queue = to_vb2_queue(file);
	struct video_device *vdev = video_devdata(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	u32 stream_ids = to_stream_id(file);

	strcpy(cap->driver, DRIVER_NAME);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		"platform:" DRIVER_NAME "-%03i",
		dev->dev_id);

	if (stream_ids == CIF_ISP10_STREAM_SP) {
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_STREAMING;
		cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_STREAMING;
	} else if (stream_ids == CIF_ISP10_STREAM_MP) {
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_STREAMING;
		cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_STREAMING;
	}
	else if (stream_ids == CIF_ISP10_STREAM_DMA)
		cap->capabilities = V4L2_CAP_VIDEO_M2M_MPLANE |
			V4L2_CAP_VIDEO_M2M;
	cap->capabilities |= V4L2_CAP_DEVICE_CAPS;
	cap->device_caps |= V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int cif_isp10_v4l2_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;
	return v4l2_event_subscribe(fh, sub, 16, NULL);
}

static int cif_isp10_v4l2_unsubscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

static void cif_isp10_v4l2_event(
	struct cif_isp10_device *dev,
	__u32 frame_sequence)
{
	struct cif_isp10_v4l2_device *cif_isp10_v4l2_dev =
		(struct cif_isp10_v4l2_device *)dev->nodes;
	struct v4l2_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = V4L2_EVENT_FRAME_SYNC;
	ev.u.frame_sync.frame_sequence = frame_sequence;
	v4l2_event_queue(&cif_isp10_v4l2_dev->node[SP_DEV].vdev, &ev);
}

static void cif_isp10_v4l2_requeue_bufs(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id)
{
	struct cif_isp10_buffer *ispbuf;
	struct vb2_buffer *buf;
	struct vb2_queue *q = NULL;
	struct cif_isp10_v4l2_device *cif_isp10_v4l2_dev =
		(struct cif_isp10_v4l2_device *)dev->nodes;

	if (stream_id == CIF_ISP10_STREAM_SP)
		q = &cif_isp10_v4l2_dev->node[SP_DEV].buf_queue;
	else if (stream_id == CIF_ISP10_STREAM_MP)
		q = &cif_isp10_v4l2_dev->node[MP_DEV].buf_queue;
	else if (stream_id == CIF_ISP10_STREAM_DMA)
		q = &cif_isp10_v4l2_dev->node[DMA_DEV].buf_queue;
	else
		WARN_ON(1);

	dev = to_cif_isp10_device(q);

	list_for_each_entry(buf, &q->queued_list, queued_entry) {
		ispbuf = to_cif_isp10_vb(to_vb2_v4l2_buffer(buf));
		if (!IS_ERR_VALUE(cif_isp10_qbuf(
			to_cif_isp10_device(q), stream_id, ispbuf))) {
			spin_lock(&dev->vbreq_lock);
			if ((buf->state == VB2_BUF_STATE_QUEUED) ||
			    (buf->state == VB2_BUF_STATE_ACTIVE))
				buf->state = VB2_BUF_STATE_ACTIVE;
			else
				cif_isp10_pltfrm_pr_err(NULL,
					"skip state change for buf: %d, state: %d\n",
					buf->index, buf->state);
			spin_unlock(&dev->vbreq_lock);
		} else {
			cif_isp10_pltfrm_pr_err(NULL,
				"failed for buffer %d\n", buf->index);
		}
	}
}

static long v4l2_default_ioctl(struct file *file, void *fh,
			       bool valid_prio, unsigned int cmd, void *arg)
{
	int ret = -EINVAL;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);

	if (!arg) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"NULL Pointer Violation from IOCTL arg:0x%lx\n",
			(unsigned long)arg);
		return ret;
	}

	if (cmd == RK_VIDIOC_SENSOR_MODE_DATA) {
		struct isp_supplemental_sensor_mode_data *p_mode_data =
		(struct isp_supplemental_sensor_mode_data *)arg;

		ret = (int)cif_isp10_img_src_ioctl(dev->img_src,
			RK_VIDIOC_SENSOR_MODE_DATA, p_mode_data);

		if (ret < 0) {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"failed to get sensor mode data\n");
			return ret;
		}

		p_mode_data->isp_input_width =
			dev->config.isp_config.input->defrect.width;
		p_mode_data->isp_input_height =
			dev->config.isp_config.input->defrect.height;
		p_mode_data->isp_input_horizontal_start =
			dev->config.isp_config.input->defrect.left;
		p_mode_data->isp_input_vertical_start =
			dev->config.isp_config.input->defrect.top;

		p_mode_data->isp_output_width =
			dev->config.isp_config.output.width;
		p_mode_data->isp_output_height =
			dev->config.isp_config.output.height;
	} else if (cmd == RK_VIDIOC_CAMERA_MODULEINFO) {
		struct camera_module_info_s *p_camera_module =
		(struct camera_module_info_s *)arg;

		ret = (int)cif_isp10_img_src_ioctl(dev->img_src,
			RK_VIDIOC_CAMERA_MODULEINFO, p_camera_module);

		if (ret < 0) {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"failed to get camera module information\n");
			return ret;
		}
	}

	return ret;
}

static int v4l2_g_parm(
		struct file *file,
		void *priv,
		struct v4l2_streamparm *a)
{
	return 0;
}

static int v4l2_s_parm(
	struct file *file,
	void *priv,
	struct v4l2_streamparm *a)
{
	return 0;
}

static int v4l2_enum_input(struct file *file, void *priv,
			   struct v4l2_input *input)
{
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	const char *inp_name;

	if ((queue->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
		(queue->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"wrong buffer queue %d\n", queue->type);
		return -EINVAL;
	}

	inp_name = cif_isp10_g_input_name(dev, input->index);
	if (IS_ERR_OR_NULL(inp_name))
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = V4L2_STD_UNKNOWN;
	strncpy(input->name, inp_name, sizeof(input->name)-1);

	return 0;
}

/* ================================================================= */

static int mainpath_g_ctrl(
	struct file *file,
	void *priv,
	struct v4l2_control *vc)
{
	int ret = -EINVAL;

	switch (vc->id) {
	default:
		return -EINVAL;
	}
	return ret;
}

#ifdef NOT_YET
static int mainpath_try_fmt_cap(struct v4l2_format *f)
{
	int ifmt = 0;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	for (ifmt = 0; ifmt < get_cif_isp10_output_format_size(); ifmt++) {
		if (pix->pixelformat ==
		get_cif_isp10_output_format(ifmt)->fourcc)
			break;
	}

	if (ifmt == get_cif_isp10_output_format_size())
		ifmt = 0;

	pix->bytesperline = pix->width *
		get_cif_isp10_output_format(ifmt)->depth / 8;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_NV24:
	case V4L2_PIX_FMT_JPEG:
		pix->colorspace = V4L2_COLORSPACE_JPEG;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_SGRBG10:
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	default:
		WARN_ON(1);
		break;
	}

	return 0;
}
#endif

static int v4l2_enum_fmt_cap(struct file *file, void *fh,
			     struct v4l2_fmtdesc *f)
{
	int ret = 0;
	int xgold_num_format = 0;

	xgold_num_format = get_cif_isp10_output_format_desc_size();
	if ((f->index >= xgold_num_format) ||
	(get_cif_isp10_output_format_desc(f->index)->pixelformat == 0)) {
		cif_isp10_pltfrm_pr_err(NULL, "index %d\n", f->index);
		return -EINVAL;
	}
	strlcpy(f->description,
		get_cif_isp10_output_format_desc(f->index)->description,
			sizeof(f->description));
	f->pixelformat =
	get_cif_isp10_output_format_desc(f->index)->pixelformat;
	f->flags = get_cif_isp10_output_format_desc(f->index)->flags;

	return ret;
}

static int v4l2_g_ctrl(struct file *file, void *priv,
	struct v4l2_control *vc)
{
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	enum cif_isp10_cid id =
		cif_isp10_v4l2_cid2cif_isp10_cid(vc->id);

	if (id == CIF_ISP10_CID_MIN_BUFFER_FOR_CAPTURE) {
		/* Three buffers needed at least.
		 * one for MI_MP_Y_BASE_AD_INIT, one for MI_MP_Y_BASE_AD_SHD
		 * the other one stay in the waiting queue.
		 */
		vc->value = 3;
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"V4L2_CID_MIN_BUFFERS_FOR_CAPTURE %d\n",
			vc->value);
		return 0;
	}

	return cif_isp10_img_src_g_ctrl(dev->img_src,
		id, &vc->value);
}

static int v4l2_s_ext_ctrls(struct file *file, void *priv,
	struct v4l2_ext_controls *vc_ext)
{
	struct cif_isp10_img_src_ctrl *ctrls;
	struct cif_isp10_img_src_ext_ctrl *ctrl;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	int ret = -EINVAL;
	unsigned int i;

	/* The only use-case is gain and exposure to sensor. Thus no check if
	 * this shall go to img_src or not as of now.
	 */
	cif_isp10_pltfrm_pr_dbg(dev->dev, "count %d\n",
		vc_ext->count);

	if (vc_ext->count == 0)
		return ret;

	ctrl = kmalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrls = kmalloc(vc_ext->count *
		sizeof(struct cif_isp10_img_src_ctrl), GFP_KERNEL);
	if (!ctrls) {
		kfree(ctrl);
		return -ENOMEM;
	}

	ctrl->cnt = vc_ext->count;
	/*current kernel version don't define
	 *this member for struct v4l2_ext_control.
	 */
	/*ctrl->class = vc_ext->ctrl_class;*/
	ctrl->ctrls = ctrls;

	for (i = 0; i < vc_ext->count; i++) {
		ctrls[i].id = vc_ext->controls[i].id;
		ctrls[i].val = vc_ext->controls[i].value;
	}

	ret = cif_isp10_s_exp(dev, ctrl);
	return ret;
}

int cif_isp10_v4l2_cropcap(
	struct file *file,
	void *fh,
	struct v4l2_cropcap *a)
{
	int ret = 0;
	struct vb2_queue *queue = to_vb2_queue(file);
	struct cif_isp10_device *dev = to_cif_isp10_device(queue);
	u32 target_width, target_height;
	u32 h_offs, v_offs;

	if ((dev->config.input_sel == CIF_ISP10_INP_DMA) ||
		(dev->config.input_sel == CIF_ISP10_INP_DMA_IE)) {
		/* calculate cropping for aspect ratio */
		ret = cif_isp10_calc_isp_cropping(dev,
			&dev->isp_dev.input_width, &dev->isp_dev.input_height,
			&h_offs, &v_offs);

		/* Get output size */
		ret = cif_isp10_get_target_frm_size(dev,
			&target_width, &target_height);
		if (ret < 0) {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"failed to get target frame size\n");
			return ret;
		}

		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"CIF_IN_W=%d, CIF_IN_H=%d, ISP_IN_W=%d, ISP_IN_H=%d, target_width=%d, target_height=%d\n",
			dev->config.isp_config.input->width,
			dev->config.isp_config.input->height,
			dev->isp_dev.input_width,
			dev->isp_dev.input_height,
			target_width,
			target_height);

		/* This is the input to Bayer after input formatter cropping */
		a->defrect.top = 0;
		a->defrect.left = 0;
		a->defrect.width = dev->isp_dev.input_width;
		a->defrect.height = dev->isp_dev.input_height;
		/* This is the minimum cropping window for the IS module */
		a->bounds.width = 2;
		a->bounds.height = 2;
		a->bounds.top = (a->defrect.height - a->bounds.height) / 2;
		a->bounds.left = (a->defrect.width - a->bounds.width) / 2;

		a->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	} else if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
		/* calculate cropping for aspect ratio */
		ret = cif_isp10_calc_isp_cropping(dev,
			&dev->isp_dev.input_width, &dev->isp_dev.input_height,
			&h_offs, &v_offs);

		/* Get output size */
		ret = cif_isp10_get_target_frm_size(dev,
			&target_width, &target_height);
		if (ret < 0) {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"failed to get target frame size\n");
			return ret;
		}

		/* This is the input to Bayer after input formatter cropping */
		a->defrect.top =
			v_offs + dev->config.isp_config.input->defrect.top;
		a->defrect.left =
			h_offs + dev->config.isp_config.input->defrect.left;
		a->defrect.width = dev->isp_dev.input_width;
		a->defrect.height = dev->isp_dev.input_height;

		a->bounds.top = 0;
		a->bounds.left = 0;
		a->bounds.width = dev->config.isp_config.input->width;
		a->bounds.height = dev->config.isp_config.input->height;
		a->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"invalid input\n");
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"v4l2_cropcap: defrect(%d,%d,%d,%d) bounds(%d,%d,%d,%d)\n",
		a->defrect.width,
		a->defrect.height,
		a->defrect.left,
		a->defrect.top,
		a->bounds.width,
		a->bounds.height,
		a->bounds.left,
		a->bounds.top);

	return ret;
}

int cif_isp10_v4l2_g_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	return 0;
}

/*
 * This is a write only function, so the upper layer
 * will ignore the changes to 'a'. So don't use 'a' to pass
 * the actual cropping parameters, the upper layer
 * should call g_crop to get the actual window.
 */
int cif_isp10_v4l2_s_crop(
	struct file *file,
	void *fh,
	const struct v4l2_crop *a)
{
	return 0;
}

int cif_isp10_v4l2_try_fmt(struct file *file, void *fh,
					struct v4l2_format *f)
{
	return 0;
}

const struct v4l2_ioctl_ops cif_isp10_v4l2_sp_ioctlops = {
	.vidioc_reqbufs = cif_isp10_v4l2_reqbufs,
	.vidioc_querybuf = cif_isp10_v4l2_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = cif_isp10_v4l2_qbuf,
	.vidioc_dqbuf = cif_isp10_v4l2_dqbuf,
	.vidioc_streamon = cif_isp10_v4l2_streamon,
	.vidioc_streamoff = cif_isp10_v4l2_streamoff,
	.vidioc_g_input = cif_isp10_v4l2_g_input,
	.vidioc_s_input = cif_isp10_v4l2_s_input,
	.vidioc_enum_input = v4l2_enum_input,
	.vidioc_g_ctrl = v4l2_g_ctrl,
	.vidioc_s_ctrl = cif_isp10_v4l2_s_ctrl,
	.vidioc_s_fmt_vid_cap = cif_isp10_v4l2_s_fmt,
	.vidioc_g_fmt_vid_cap = cif_isp10_v4l2_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane = cif_isp10_v4l2_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane = cif_isp10_v4l2_g_fmt,
	.vidioc_s_fmt_vid_overlay = cif_isp10_v4l2_s_fmt,
	.vidioc_g_fmt_vid_overlay = cif_isp10_v4l2_g_fmt,
	.vidioc_s_ext_ctrls = v4l2_s_ext_ctrls,
	.vidioc_enum_fmt_vid_cap = v4l2_enum_fmt_cap,
	.vidioc_enum_framesizes = cif_isp10_v4l2_enum_framesizes,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_querycap = v4l2_querycap,
	.vidioc_cropcap = cif_isp10_v4l2_cropcap,
	.vidioc_s_crop = cif_isp10_v4l2_s_crop,
	.vidioc_g_crop = cif_isp10_v4l2_g_crop,
	.vidioc_subscribe_event = cif_isp10_v4l2_subscribe_event,
	.vidioc_unsubscribe_event = cif_isp10_v4l2_unsubscribe_event,
	.vidioc_default = v4l2_default_ioctl,
	.vidioc_try_fmt_vid_cap = cif_isp10_v4l2_try_fmt,
	.vidioc_s_parm = v4l2_s_parm,
	.vidioc_g_parm = v4l2_g_parm,
};

const struct v4l2_ioctl_ops cif_isp10_v4l2_mp_ioctlops = {
	.vidioc_reqbufs = cif_isp10_v4l2_reqbufs,
	.vidioc_querybuf = cif_isp10_v4l2_querybuf,
	.vidioc_qbuf = cif_isp10_v4l2_qbuf,
	.vidioc_dqbuf = cif_isp10_v4l2_dqbuf,
	.vidioc_streamon = cif_isp10_v4l2_streamon,
	.vidioc_streamoff = cif_isp10_v4l2_streamoff,
	.vidioc_g_input = cif_isp10_v4l2_g_input,
	.vidioc_s_input = cif_isp10_v4l2_s_input,
	.vidioc_enum_input = v4l2_enum_input,
	.vidioc_g_ctrl = mainpath_g_ctrl,
	.vidioc_s_ctrl = cif_isp10_v4l2_s_ctrl,
	.vidioc_s_fmt_vid_cap = cif_isp10_v4l2_s_fmt,
	.vidioc_g_fmt_vid_cap = cif_isp10_v4l2_g_fmt,
	.vidioc_enum_fmt_vid_cap = v4l2_enum_fmt_cap,
	.vidioc_enum_framesizes = cif_isp10_v4l2_enum_framesizes,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_s_parm = v4l2_s_parm,
	.vidioc_querycap = v4l2_querycap,
	.vidioc_cropcap = cif_isp10_v4l2_cropcap,
	.vidioc_s_crop = cif_isp10_v4l2_s_crop,
	.vidioc_g_crop = cif_isp10_v4l2_g_crop,
	.vidioc_default = v4l2_default_ioctl,
	.vidioc_try_fmt_vid_cap = cif_isp10_v4l2_try_fmt,
	.vidioc_g_parm = v4l2_g_parm,
};

const struct v4l2_ioctl_ops cif_isp10_v4l2_dma_ioctlops = {
	.vidioc_reqbufs = cif_isp10_v4l2_reqbufs,
	.vidioc_querybuf = cif_isp10_v4l2_querybuf,
	.vidioc_qbuf = cif_isp10_v4l2_qbuf,
	.vidioc_dqbuf = cif_isp10_v4l2_dqbuf,
	.vidioc_streamon = cif_isp10_v4l2_streamon,
	.vidioc_streamoff = cif_isp10_v4l2_streamoff,
	.vidioc_s_fmt_vid_out = cif_isp10_v4l2_s_fmt,
	.vidioc_g_fmt_vid_out = cif_isp10_v4l2_g_fmt,
	.vidioc_cropcap = cif_isp10_v4l2_cropcap,
	.vidioc_s_crop = cif_isp10_v4l2_s_crop,
	.vidioc_g_crop = cif_isp10_v4l2_g_crop,
	.vidioc_querycap = v4l2_querycap,
};

static struct pltfrm_soc_cfg rk3288_cfg = {
	.name = CIF_ISP10_SOC_RK3288,
	.soc_cfg = pltfrm_rk3288_cfg,
};

static struct pltfrm_soc_cfg rk3399_cfg = {
	.name = CIF_ISP10_SOC_RK3399,
	.soc_cfg = pltfrm_rk3399_cfg,
};

static const struct of_device_id cif_isp10_v4l2_of_match[] = {
	{.compatible = "rockchip,rk3288-cif-isp",
	.data = (void *)&rk3288_cfg},
	{.compatible = "rockchip,rk3399-cif-isp",
	.data = (void *)&rk3399_cfg},
	{},
};

static unsigned int cif_isp10_v4l2_dev_cnt;
static int cif_isp10_v4l2_drv_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct cif_isp10_device *dev = NULL;
	struct cif_isp10_v4l2_device *cif_isp10_v4l2_dev;
	int ret;

	cif_isp10_pltfrm_pr_info(NULL, "probing...\n");

	cif_isp10_v4l2_dev = devm_kzalloc(
				&pdev->dev,
				sizeof(struct cif_isp10_v4l2_device),
				GFP_KERNEL);
	if (IS_ERR_OR_NULL(cif_isp10_v4l2_dev)) {
		ret = -ENOMEM;
		goto err;
	}

	match = of_match_node(cif_isp10_v4l2_of_match, node);
	dev = cif_isp10_create(&pdev->dev,
		cif_isp10_v4l2_event,
		cif_isp10_v4l2_requeue_bufs,
		(struct pltfrm_soc_cfg *)match->data);
	if (IS_ERR_OR_NULL(dev)) {
		ret = -ENODEV;
		goto err;
	}

	dev->dev_id = cif_isp10_v4l2_dev_cnt;
	dev->isp_dev.dev_id = &dev->dev_id;
	dev->nodes = (void *)cif_isp10_v4l2_dev;
	dev->isp_state = CIF_ISP10_STATE_IDLE;
	spin_lock_init(&dev->vbq_lock);
	spin_lock_init(&dev->vbreq_lock);
	spin_lock_init(&dev->iowrite32_verify_lock);
	spin_lock_init(&dev->isp_state_lock);
	init_waitqueue_head(&dev->isp_stop_wait);

	ret = v4l2_device_register(dev->dev, &dev->v4l2_dev);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"V4L2 device registration failed\n");
		goto err;
	}

	ret = cif_isp10_v4l2_register_video_device(
		dev,
		&cif_isp10_v4l2_dev->node[SP_DEV].vdev,
		SP_VDEV_NAME,
		V4L2_CAP_VIDEO_OVERLAY,
		CIF_ISP10_V4L2_SP_DEV_MAJOR,
		&cif_isp10_v4l2_fops,
		&cif_isp10_v4l2_sp_ioctlops);
	if (ret)
		goto err;

	ret = register_cifisp_device(&dev->isp_dev,
		&cif_isp10_v4l2_dev->node[ISP_DEV].vdev,
		&dev->v4l2_dev,
		dev->config.base_addr);
	if (ret)
		goto err;

	ret = cif_isp10_v4l2_register_video_device(
		dev,
		&cif_isp10_v4l2_dev->node[MP_DEV].vdev,
		MP_VDEV_NAME,
		V4L2_CAP_VIDEO_CAPTURE,
		CIF_ISP10_V4L2_MP_DEV_MAJOR,
		&cif_isp10_v4l2_fops,
		&cif_isp10_v4l2_mp_ioctlops);
	if (ret)
		goto err;

	ret = cif_isp10_v4l2_register_video_device(
		dev,
		&cif_isp10_v4l2_dev->node[DMA_DEV].vdev,
		DMA_VDEV_NAME,
		V4L2_CAP_VIDEO_OUTPUT,
		CIF_ISP10_V4L2_DMA_DEV_MAJOR,
		&cif_isp10_v4l2_fops,
		&cif_isp10_v4l2_dma_ioctlops);
	if (ret)
		goto err;

	pm_runtime_enable(&pdev->dev);

	cif_isp10_v4l2_dev_cnt++;
	return 0;
err:
	cif_isp10_destroy(dev);
	return ret;
}

/* ======================================================================== */

static int cif_isp10_v4l2_drv_remove(struct platform_device *pdev)
{
	struct cif_isp10_device *cif_isp10_dev =
		(struct cif_isp10_device *)platform_get_drvdata(pdev);
	struct cif_isp10_v4l2_device *cif_isp10_v4l2_dev =
		(struct cif_isp10_v4l2_device *)cif_isp10_dev->nodes;

	if (IS_ERR_VALUE(cif_isp10_release(cif_isp10_dev,
		CIF_ISP10_ALL_STREAMS)))
		cif_isp10_pltfrm_pr_warn(cif_isp10_dev->dev,
			"CIF power off failed\n");

	video_unregister_device(&cif_isp10_v4l2_dev->node[SP_DEV].vdev);
	video_unregister_device(&cif_isp10_v4l2_dev->node[MP_DEV].vdev);
	video_unregister_device(&cif_isp10_v4l2_dev->node[DMA_DEV].vdev);
	unregister_cifisp_device(&cif_isp10_v4l2_dev->node[ISP_DEV].vdev);
	v4l2_device_unregister(&cif_isp10_dev->v4l2_dev);
	cif_isp10_pltfrm_dev_release(&pdev->dev, cif_isp10_dev);
	cif_isp10_destroy(cif_isp10_dev);

	cif_isp10_v4l2_dev_cnt--;
	return 0;
}

static int cif_isp10_v4l2_drv_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	int ret = 0;
	struct cif_isp10_device *cif_isp10_dev =
		(struct cif_isp10_device *)platform_get_drvdata(pdev);

	cif_isp10_pltfrm_pr_dbg(cif_isp10_dev->dev, "\n");

	ret = cif_isp10_suspend(cif_isp10_dev);
	if (IS_ERR_VALUE(ret))
		goto err;

	cif_isp10_pltfrm_pinctrl_set_state(&pdev->dev,
		CIF_ISP10_PINCTRL_STATE_SLEEP);

	return 0;
err:
	cif_isp10_pltfrm_pr_err(cif_isp10_dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_v4l2_drv_resume(struct platform_device *pdev)
{
	int ret = 0;
	struct cif_isp10_device *cif_isp10_dev =
		(struct cif_isp10_device *)platform_get_drvdata(pdev);

	cif_isp10_pltfrm_pr_dbg(cif_isp10_dev->dev, "\n");

	if (!cif_isp10_dev->img_src) {
		cif_isp10_pltfrm_pr_err(
			cif_isp10_dev->dev,
			"cif_isp10_dev img_src is null!\n");
		goto err;
	}

	ret = cif_isp10_resume(cif_isp10_dev);
	if (IS_ERR_VALUE(ret))
		goto err;

	cif_isp10_pltfrm_pinctrl_set_state(&pdev->dev,
		CIF_ISP10_PINCTRL_STATE_DEFAULT);

	return 0;
err:
	cif_isp10_pltfrm_pr_err(cif_isp10_dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_runtime_suspend(struct device *dev)
{
	cif_isp10_pltfrm_pr_dbg(dev, "\n");
	return cif_isp10_pltfrm_pm_set_state(dev, CIF_ISP10_PM_STATE_SUSPENDED);
}

static int cif_isp10_runtime_resume(struct device *dev)
{
	cif_isp10_pltfrm_pr_dbg(dev, "\n");
	return cif_isp10_pltfrm_pm_set_state(dev, CIF_ISP10_PM_STATE_SW_STNDBY);
}

static const struct dev_pm_ops cif_isp10_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(cif_isp10_runtime_suspend,
			   cif_isp10_runtime_resume, NULL)
};

static struct platform_driver cif_isp10_v4l2_plat_drv = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(cif_isp10_v4l2_of_match),
		.pm = &cif_isp10_dev_pm_ops,
		   },
	.probe = cif_isp10_v4l2_drv_probe,
	.remove = cif_isp10_v4l2_drv_remove,
	.suspend = cif_isp10_v4l2_drv_suspend,
	.resume = cif_isp10_v4l2_drv_resume,
};

/* ======================================================================== */
static int cif_isp10_v4l2_init(void)
{
	int ret;

	ret = platform_driver_register(&cif_isp10_v4l2_plat_drv);
	if (ret) {
		cif_isp10_pltfrm_pr_err(NULL,
			"cannot register platform driver, failed with %d\n",
			ret);
		return -ENODEV;
	}

	return ret;
}

/* ======================================================================== */
static void __exit cif_isp10_v4l2_exit(void)
{
	platform_driver_unregister(&cif_isp10_v4l2_plat_drv);
}

device_initcall_sync(cif_isp10_v4l2_init);
module_exit(cif_isp10_v4l2_exit);

MODULE_DESCRIPTION("V4L2 interface for CIF ISP10 driver");
MODULE_AUTHOR("George");
MODULE_LICENSE("GPL");
