// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/dma-iommu.h>
#include <linux/rk-camera-module.h>
#include "dev.h"
#include "regs.h"

static int mpfbc_get_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = mpfbc_dev->ispdev;

	if (!fmt)
		return -EINVAL;

	/* get isp out format */
	fmt->pad = RKISP_ISP_PAD_SOURCE_PATH;
	fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
	return v4l2_subdev_call(&dev->isp_sdev.sd, pad, get_fmt, NULL, fmt);
}

static int mpfbc_set_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_isp_subdev *isp_sd = &mpfbc_dev->ispdev->isp_sdev;
	u32 src_w = isp_sd->out_crop.width;
	u32 src_h = isp_sd->out_crop.height;
	struct v4l2_rect *crop;

	if (!sel)
		return -EINVAL;
	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	crop = &sel->r;
	crop->left = clamp_t(u32, crop->left, 0, src_w);
	crop->top = clamp_t(u32, crop->top, 0, src_h);
	crop->width = clamp_t(u32, crop->width,
		CIF_ISP_OUTPUT_W_MIN, src_w - crop->left);
	crop->height = clamp_t(u32, crop->height,
		CIF_ISP_OUTPUT_H_MIN, src_h - crop->top);

	mpfbc_dev->crop = *crop;
	return 0;
}

static int mpfbc_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_isp_subdev *isp_sd = &mpfbc_dev->ispdev->isp_sdev;
	struct v4l2_rect *crop;

	if (!sel)
		return -EINVAL;

	crop = &sel->r;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		*crop = isp_sd->out_crop;
		break;
	case V4L2_SEL_TGT_CROP:
		*crop = mpfbc_dev->crop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void mpfbc_crop_on(struct rkisp_mpfbc_device *mpfbc_dev)
{
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	void __iomem *base = dev->base_addr;
	u32 src_w = dev->isp_sdev.out_crop.width;
	u32 src_h = dev->isp_sdev.out_crop.height;
	u32 dest_w = mpfbc_dev->crop.width;
	u32 dest_h = mpfbc_dev->crop.height;
	u32 left = mpfbc_dev->crop.left;
	u32 top = mpfbc_dev->crop.top;
	u32 ctrl;

	if (src_w == dest_w && src_h == dest_h)
		return;

	writel(left, base + CIF_DUAL_CROP_M_H_OFFS);
	writel(top, base + CIF_DUAL_CROP_M_V_OFFS);
	writel(dest_w, base + CIF_DUAL_CROP_M_H_SIZE);
	writel(dest_h, base + CIF_DUAL_CROP_M_V_SIZE);
	ctrl = readl(base + CIF_DUAL_CROP_CTRL);
	ctrl |= CIF_DUAL_CROP_MP_MODE_YUV | CIF_DUAL_CROP_CFG_UPD;
	writel(ctrl, base + CIF_DUAL_CROP_CTRL);
}

static void mpfbc_crop_off(struct rkisp_mpfbc_device *mpfbc_dev)
{
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	void __iomem *base = dev->base_addr;
	u32 src_w = dev->isp_sdev.out_crop.width;
	u32 src_h = dev->isp_sdev.out_crop.height;
	u32 dest_w = mpfbc_dev->crop.width;
	u32 dest_h = mpfbc_dev->crop.height;
	u32 ctrl;

	if (src_w == dest_w && src_h == dest_h)
		return;

	ctrl = readl(base + CIF_DUAL_CROP_CTRL);
	ctrl &= ~(CIF_DUAL_CROP_MP_MODE_YUV |
		  CIF_DUAL_CROP_MP_MODE_RAW);
	ctrl |= CIF_DUAL_CROP_GEN_CFG_UPD;
	writel(ctrl, base + CIF_DUAL_CROP_CTRL);
}

static void free_dma_buf(struct rkisp_dma_buf *buf)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;

	if (!buf)
		return;

	ops->unmap_dmabuf(buf->mem_priv);
	ops->detach_dmabuf(buf->mem_priv);
	dma_buf_put(buf->dbuf);
	kfree(buf);
}

static void mpfbc_free_dma_buf(struct rkisp_mpfbc_device *dev)
{
	free_dma_buf(dev->pic_cur);
	dev->pic_cur = NULL;
	free_dma_buf(dev->pic_nxt);
	dev->pic_nxt = NULL;
	free_dma_buf(dev->gain_cur);
	dev->gain_cur = NULL;
	free_dma_buf(dev->gain_nxt);
	dev->gain_nxt = NULL;
}

static int mpfbc_s_rx_buffer(struct v4l2_subdev *sd,
			     void *dbuf, unsigned int *size)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	void __iomem *base = dev->base_addr;
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;
	struct rkisp_dma_buf *buf;
	dma_addr_t dma_addr;
	u32 w = ALIGN(mpfbc_dev->crop.width, 16);
	u32 h = ALIGN(mpfbc_dev->crop.height, 16);
	u32 sizes = (w * h >> 4) + w * h * 2;
	u32 w_tmp;
	int ret = 0;

	if (!dbuf || !size)
		return -EINVAL;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err;
	}

	buf->dbuf = dbuf;
	buf->mem_priv = ops->attach_dmabuf(dev->dev, dbuf,
				*size, DMA_BIDIRECTIONAL);
	if (IS_ERR(buf->mem_priv)) {
		ret = PTR_ERR(buf->mem_priv);
		goto err;
	}

	ret = ops->map_dmabuf(buf->mem_priv);
	if (ret) {
		ops->detach_dmabuf(buf->mem_priv);
		goto err;
	}

	dma_addr = *((dma_addr_t *)ops->cookie(buf->mem_priv));

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s buf:0x%x size:%d\n",
		 __func__, (u32)dma_addr, *size);

	/* picture or gain buffer */
	if (*size == sizes) {
		if (mpfbc_dev->pic_cur) {
			mpfbc_dev->pic_nxt = buf;
			writel(dma_addr, base + ISP_MPFBC_HEAD_PTR2);
			writel(dma_addr + (w * h >> 4),
			       base + ISP_MPFBC_PAYL_PTR2);
			mpfbc_dev->pingpong = true;
		} else {
			mpfbc_dev->pic_cur = buf;
			writel(dma_addr, base + ISP_MPFBC_HEAD_PTR);
			writel(dma_addr + (w * h >> 4),
			       base + ISP_MPFBC_PAYL_PTR);
			mpfbc_dev->pingpong = false;
		}
	} else {
		if (mpfbc_dev->gain_cur) {
			mpfbc_dev->gain_nxt = buf;
			writel(dma_addr, base + MI_GAIN_WR_BASE2);
			mi_wr_ctrl2(base, SW_GAIN_WR_PINGPONG);
		} else {
			mpfbc_dev->gain_cur = buf;
			writel(dma_addr, base + MI_GAIN_WR_BASE);
			isp_clear_bits(base + MI_WR_CTRL2,
					SW_GAIN_WR_PINGPONG);
		}
		writel(*size, base + MI_GAIN_WR_SIZE);

		w_tmp = (mpfbc_dev->crop.width + 3) / 4;
		w_tmp = ALIGN(w_tmp, 16);
		writel(w_tmp, base + MI_GAIN_WR_LENGTH);
		mi_wr_ctrl2(base, SW_GAIN_WR_AUTOUPD);
	}

	return 0;
err:
	kfree(buf);
	dma_buf_put(dbuf);
	mpfbc_free_dma_buf(mpfbc_dev);
	return ret;
}

static int mpfbc_start(struct rkisp_mpfbc_device *mpfbc_dev)
{
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	void __iomem *base = dev->base_addr;
	u32 h = ALIGN(mpfbc_dev->crop.height, 16);

	mpfbc_crop_on(mpfbc_dev);
	writel(mpfbc_dev->pingpong << 4 | SW_MPFBC_YUV_MODE(1) |
		SW_MPFBC_MAINISP_MODE | SW_MPFBC_EN,
		base + ISP_MPFBC_BASE);
	writel(0, base + ISP_MPFBC_VIR_WIDTH);
	writel(h, base + ISP_MPFBC_VIR_HEIGHT);
	isp_set_bits(base + CTRL_SWS_CFG, 0, SW_ISP2PP_PIPE_EN);
	isp_set_bits(base + MI_IMSC, 0, MI_MPFBC_FRAME);
	isp_set_bits(base + MI_WR_CTRL, 0, CIF_MI_CTRL_INIT_BASE_EN);
	mp_set_data_path(base);
	force_cfg_update(base);
	mpfbc_dev->en = true;
	return 0;
}

static int mpfbc_stop(struct rkisp_mpfbc_device *mpfbc_dev)
{
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	void __iomem *base = dev->base_addr;
	int ret;

	mpfbc_dev->stopping = true;
	isp_clear_bits(base + ISP_MPFBC_BASE, SW_MPFBC_EN);
	hdr_stop_dmatx(dev);
	ret = wait_event_timeout(mpfbc_dev->done,
				 !mpfbc_dev->en,
				 msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&mpfbc_dev->sd,
			  "waiting on event return error %d\n", ret);
	mpfbc_crop_off(mpfbc_dev);
	mpfbc_dev->stopping = false;
	isp_clear_bits(base + MI_IMSC, MI_MPFBC_FRAME);
	mpfbc_dev->en = false;
	return 0;
}

static int mpfbc_start_stream(struct v4l2_subdev *sd)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	int ret;

	if (!mpfbc_dev->linked || !dev->isp_inp) {
		v4l2_err(&dev->v4l2_dev,
			 "mpfbc no linked or isp inval input\n");
		return -EINVAL;
	}

	if (WARN_ON(mpfbc_dev->en))
		return -EBUSY;

	if (dev->isp_inp & INP_CSI ||
	    dev->isp_inp & INP_DVP) {
		/* Always update sensor info in case media topology changed */
		ret = rkisp_update_sensor_info(dev);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "update sensor info failed %d\n",
				 ret);
			return -EBUSY;
		}
	}

	/* enable clocks/power-domains */
	ret = dev->pipe.open(&dev->pipe, &sd->entity, true);
	if (ret < 0)
		return ret;

	hdr_config_dmatx(dev);
	ret = mpfbc_start(mpfbc_dev);
	if (ret < 0)
		goto close_pipe;
	hdr_update_dmatx_buf(dev);

	/* start sub-devices */
	ret = dev->pipe.set_stream(&dev->pipe, true);
	if (ret < 0)
		goto stop_mpfbc;

	ret = media_pipeline_start(&sd->entity, &dev->pipe.pipe);
	if (ret < 0)
		goto pipe_stream_off;

	return 0;
pipe_stream_off:
	dev->pipe.set_stream(&dev->pipe, false);
stop_mpfbc:
	mpfbc_stop(mpfbc_dev);
	hdr_destroy_buf(dev);
close_pipe:
	dev->pipe.close(&dev->pipe);
	return ret;
}

static void mpfbc_destroy_buf(struct rkisp_device *dev)
{
	mpfbc_free_dma_buf(&dev->mpfbc_dev);
	hdr_destroy_buf(dev);
}

static int mpfbc_stop_stream(struct v4l2_subdev *sd)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = mpfbc_dev->ispdev;

	mpfbc_stop(mpfbc_dev);
	media_pipeline_stop(&sd->entity);
	dev->pipe.set_stream(&dev->pipe, false);
	dev->pipe.close(&dev->pipe);
	mpfbc_destroy_buf(dev);
	return 0;
}

static int mpfbc_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, on);

	if (on) {
		atomic_inc(&dev->cap_dev.refcnt);
		ret = mpfbc_start_stream(sd);
	} else if (mpfbc_dev->en) {
		ret = mpfbc_stop_stream(sd);
	}

	if (!on)
		atomic_dec(&dev->cap_dev.refcnt);
	return ret;
}

static int mpfbc_s_power(struct v4l2_subdev *sd, int on)
{
	struct rkisp_mpfbc_device *mpfbc_dev = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = mpfbc_dev->ispdev;
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, on);

	if (on)
		ret = v4l2_pipeline_pm_use(&sd->entity, 1);
	else
		ret = v4l2_pipeline_pm_use(&sd->entity, 0);

	return ret;
}

void rkisp_mpfbc_isr(u32 mis_val, struct rkisp_device *dev)
{
	struct rkisp_mpfbc_device *mpfbc_dev = &dev->mpfbc_dev;
	void __iomem *base = dev->base_addr;

	writel(MI_MPFBC_FRAME, base + CIF_MI_ICR);

	if (mpfbc_dev->stopping) {
		if (is_mpfbc_stopped(base)) {
			mpfbc_dev->en = false;
			mpfbc_dev->stopping = false;
			wake_up(&mpfbc_dev->done);
		}
	}
}

static const struct v4l2_subdev_pad_ops mpfbc_pad_ops = {
	.set_fmt = mpfbc_get_set_fmt,
	.get_fmt = mpfbc_get_set_fmt,
	.get_selection = mpfbc_get_selection,
	.set_selection = mpfbc_set_selection,
};

static const struct v4l2_subdev_video_ops mpfbc_video_ops = {
	.s_rx_buffer = mpfbc_s_rx_buffer,
	.s_stream = mpfbc_s_stream,
};

static const struct v4l2_subdev_core_ops mpfbc_core_ops = {
	.s_power = mpfbc_s_power,
};

static struct v4l2_subdev_ops mpfbc_v4l2_ops = {
	.core = &mpfbc_core_ops,
	.video = &mpfbc_video_ops,
	.pad = &mpfbc_pad_ops,
};

int rkisp_register_mpfbc_subdev(struct rkisp_device *dev,
			       struct v4l2_device *v4l2_dev)
{
	struct rkisp_mpfbc_device *mpfbc_dev = &dev->mpfbc_dev;
	struct v4l2_subdev *sd;
	struct media_entity *source, *sink;
	int ret;

	memset(mpfbc_dev, 0, sizeof(*mpfbc_dev));
	if (dev->isp_ver != ISP_V20)
		return 0;

	mpfbc_dev->ispdev = dev;
	sd = &mpfbc_dev->sd;

	v4l2_subdev_init(sd, &mpfbc_v4l2_ops);
	//sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.obj_type = 0;
	snprintf(sd->name, sizeof(sd->name), MPFBC_DEV_NAME);

	mpfbc_dev->pad.flags = MEDIA_PAD_FL_SINK;

	ret = media_entity_pads_init(&sd->entity, 1, &mpfbc_dev->pad);
	if (ret < 0)
		return ret;
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, mpfbc_dev);
	sd->grp_id = GRP_ID_ISP_MPFBC;
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register mpfbc subdev\n");
		goto free_media;
	}
	mpfbc_dev->crop = dev->isp_sdev.out_crop;
	/* mpfbc links */
	mpfbc_dev->linked = true;
	source = &dev->isp_sdev.sd.entity;
	sink = &sd->entity;
	ret = media_create_pad_link(source, RKISP_ISP_PAD_SOURCE_PATH,
				    sink, 0, mpfbc_dev->linked);

	init_waitqueue_head(&mpfbc_dev->done);
	return ret;

free_media:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void rkisp_unregister_mpfbc_subdev(struct rkisp_device *dev)
{
	struct v4l2_subdev *sd = &dev->mpfbc_dev.sd;

	if (dev->isp_ver != ISP_V20)
		return;
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

void rkisp_get_mpfbc_sd(struct platform_device *dev,
			 struct v4l2_subdev **sd)
{
	struct rkisp_device *isp_dev = platform_get_drvdata(dev);

	if (isp_dev)
		*sd = &isp_dev->mpfbc_dev.sd;
	else
		*sd = NULL;
}
