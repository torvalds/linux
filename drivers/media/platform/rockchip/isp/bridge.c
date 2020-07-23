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

static inline
struct rkisp_bridge_buf *to_bridge_buf(struct rkisp_ispp_buf *dbufs)
{
	return container_of(dbufs, struct rkisp_bridge_buf, dbufs);
}

static void update_mi(struct rkisp_bridge_device *dev)
{
	void __iomem *base = dev->ispdev->base_addr;
	struct rkisp_bridge_buf *buf;
	u32 val;

	if (dev->nxt_buf) {
		buf = to_bridge_buf(dev->nxt_buf);
		val = buf->dummy[GROUP_BUF_PIC].dma_addr;
		writel(val, base + dev->cfg->reg.y0_base);
		val += dev->cfg->offset;
		writel(val, base + dev->cfg->reg.uv0_base);
		val = buf->dummy[GROUP_BUF_GAIN].dma_addr;
		writel(val, base + dev->cfg->reg.g0_base);
	}

	v4l2_dbg(3, rkisp_debug, &dev->sd,
		 "%s pic(shd:0x%x base:0x%x) gain(shd:0x%x base:0x%x)\n",
		 __func__,
		 readl(base + dev->cfg->reg.y0_base_shd),
		 readl(base + dev->cfg->reg.y0_base),
		 readl(base + dev->cfg->reg.g0_base_shd),
		 readl(base + dev->cfg->reg.g0_base));
}

static int frame_end(struct rkisp_bridge_device *dev)
{
	struct v4l2_subdev *sd = v4l2_get_subdev_hostdata(&dev->sd);
	unsigned long lock_flags = 0;
	u64 ns = 0;

	if (dev->cur_buf && dev->nxt_buf) {
		rkisp_dmarx_get_frame(dev->ispdev,
				      &dev->cur_buf->frame_id, &ns, false);
		dev->cur_buf->frame_id++;
		if (!ns)
			ns = ktime_get_ns();
		dev->cur_buf->frame_timestamp = ns;
		v4l2_subdev_call(sd, video, s_rx_buffer, dev->cur_buf, NULL);
		dev->cur_buf = NULL;
	}

	if (dev->nxt_buf) {
		dev->cur_buf = dev->nxt_buf;
		dev->nxt_buf = NULL;
	}

	spin_lock_irqsave(&dev->buf_lock, lock_flags);
	if (!list_empty(&dev->list)) {
		dev->nxt_buf = list_first_entry(&dev->list,
				struct rkisp_ispp_buf, list);
		list_del(&dev->nxt_buf->list);
	}
	spin_unlock_irqrestore(&dev->buf_lock, lock_flags);

	update_mi(dev);

	return 0;
}

static int config_gain(struct rkisp_bridge_device *dev)
{
	void __iomem *base = dev->ispdev->base_addr;
	struct rkisp_bridge_buf *buf;
	u32 val;

	dev->cur_buf = list_first_entry(&dev->list,
			struct rkisp_ispp_buf, list);
	list_del(&dev->cur_buf->list);
	if (!list_empty(&dev->list)) {
		dev->nxt_buf = list_first_entry(&dev->list,
				struct rkisp_ispp_buf, list);
		list_del(&dev->nxt_buf->list);
	}

	if (dev->nxt_buf && (dev->work_mode & ISP_ISPP_QUICK)) {
		buf = to_bridge_buf(dev->nxt_buf);
		val = buf->dummy[GROUP_BUF_GAIN].dma_addr;
		writel(val, base + dev->cfg->reg.g1_base);
		mi_wr_ctrl2(base, SW_GAIN_WR_PINGPONG);
	}

	buf = to_bridge_buf(dev->cur_buf);
	val = buf->dummy[GROUP_BUF_GAIN].dma_addr;
	writel(val, base + dev->cfg->reg.g0_base);

	val = dev->cur_buf->dbuf[GROUP_BUF_GAIN]->size;
	writel(val, base + MI_GAIN_WR_SIZE);
	val = ALIGN((dev->crop.width + 3) >> 2, 16);
	writel(val, base + MI_GAIN_WR_LENGTH);
	mi_wr_ctrl2(base, SW_GAIN_WR_AUTOUPD);

	return 0;
}

static int config_mpfbc(struct rkisp_bridge_device *dev)
{
	void __iomem *base = dev->ispdev->base_addr;
	struct rkisp_bridge_buf *buf;
	u32 h = ALIGN(dev->crop.height, 16);
	u32 val, ctrl = 0;

	if (dev->work_mode & ISP_ISPP_QUICK) {
		isp_set_bits(base + CTRL_SWS_CFG,
			     0, SW_ISP2PP_PIPE_EN);
		ctrl = SW_MPFBC_MAINISP_MODE;
		if (dev->nxt_buf) {
			ctrl |= SW_MPFBC_PINGPONG_EN;
			buf = to_bridge_buf(dev->nxt_buf);
			val = buf->dummy[GROUP_BUF_PIC].dma_addr;
			writel(val, base + dev->cfg->reg.y1_base);
			val += dev->cfg->offset;
			writel(val, base + dev->cfg->reg.uv1_base);
		}
	}

	buf = to_bridge_buf(dev->cur_buf);
	val = buf->dummy[GROUP_BUF_PIC].dma_addr;
	writel(val, base + dev->cfg->reg.y0_base);
	val += dev->cfg->offset;
	writel(val, base + dev->cfg->reg.uv0_base);

	writel(0, base + ISP_MPFBC_VIR_WIDTH);
	writel(h, base + ISP_MPFBC_VIR_HEIGHT);

	mp_set_data_path(base);
	isp_set_bits(base + MI_WR_CTRL, 0,
		     CIF_MI_CTRL_INIT_BASE_EN |
		     CIF_MI_CTRL_INIT_OFFSET_EN);
	isp_set_bits(base + MI_IMSC, 0,
		     dev->cfg->frame_end_id);

	ctrl |= (dev->work_mode & ISP_ISPP_422) | SW_MPFBC_EN;
	writel(ctrl, base + ISP_MPFBC_BASE);
	return 0;
}

static void disable_mpfbc(void __iomem *base)
{
	isp_clear_bits(base + ISP_MPFBC_BASE, SW_MPFBC_EN);
}

static struct rkisp_bridge_ops mpfbc_ops = {
	.config = config_mpfbc,
	.disable = disable_mpfbc,
	.is_stopped = is_mpfbc_stopped,
};

static struct rkisp_bridge_config mpfbc_cfg = {
	.frame_end_id = MI_MPFBC_FRAME,
	.reg = {
		.y0_base = ISP_MPFBC_HEAD_PTR,
		.uv0_base = ISP_MPFBC_PAYL_PTR,
		.y1_base = ISP_MPFBC_HEAD_PTR2,
		.uv1_base = ISP_MPFBC_PAYL_PTR2,
		.g0_base = MI_GAIN_WR_BASE,
		.g1_base = MI_GAIN_WR_BASE2,

		.y0_base_shd = ISP_MPFBC_HEAD_PTR,
		.uv0_base_shd = ISP_MPFBC_PAYL_PTR,
		.g0_base_shd = MI_GAIN_WR_BASE_SHD,
	},
};

static int config_mp(struct rkisp_bridge_device *dev)
{
	void __iomem *base = dev->ispdev->base_addr;
	struct rkisp_bridge_buf *buf;
	u32 val;

	if (dev->work_mode & ISP_ISPP_QUICK) {
		isp_set_bits(base + CTRL_SWS_CFG, 0,
			     SW_ISP2PP_PIPE_EN);
		if (dev->nxt_buf) {
			buf = to_bridge_buf(dev->nxt_buf);
			val = buf->dummy[GROUP_BUF_PIC].dma_addr;
			writel(val, base + dev->cfg->reg.y1_base);
			val += dev->cfg->offset;
			writel(val, base + dev->cfg->reg.uv1_base);
			isp_set_bits(base + CIF_MI_CTRL, 0,
				     CIF_MI_MP_PINGPONG_ENABLE);
		}
	}

	buf = to_bridge_buf(dev->cur_buf);
	val = buf->dummy[GROUP_BUF_PIC].dma_addr;
	writel(val, base + dev->cfg->reg.y0_base);
	val += dev->cfg->offset;
	writel(val, base + dev->cfg->reg.uv0_base);
	writel(dev->cfg->offset, base + CIF_MI_MP_Y_SIZE_INIT);
	val = dev->cur_buf->dbuf[GROUP_BUF_PIC]->size - dev->cfg->offset;
	writel(val, base + CIF_MI_MP_CB_SIZE_INIT);
	writel(0, base + CIF_MI_MP_CR_SIZE_INIT);
	writel(0, base + CIF_MI_MP_Y_OFFS_CNT_INIT);
	writel(0, base + CIF_MI_MP_CB_OFFS_CNT_INIT);
	writel(0, base + CIF_MI_MP_CR_OFFS_CNT_INIT);

	mp_set_data_path(base);
	mp_mi_ctrl_set_format(base, MI_CTRL_MP_WRITE_YUV_SPLA);
	writel(dev->work_mode & ISP_ISPP_422, base + ISP_MPFBC_BASE);
	isp_set_bits(base + MI_WR_CTRL, 0,
		     CIF_MI_CTRL_INIT_BASE_EN |
		     CIF_MI_CTRL_INIT_OFFSET_EN);
	isp_set_bits(base + MI_IMSC, 0,
		     dev->cfg->frame_end_id);
	mi_ctrl_mpyuv_enable(base);
	mp_mi_ctrl_autoupdate_en(base);
	return 0;
}

static void disable_mp(void __iomem *base)
{
	mi_ctrl_mp_disable(base);
}

static struct rkisp_bridge_ops mp_ops = {
	.config = config_mp,
	.disable = disable_mp,
	.is_stopped = mp_is_stream_stopped,
};

static struct rkisp_bridge_config mp_cfg = {
	.frame_end_id = MI_MP_FRAME,
	.reg = {
		.y0_base = MI_MP_WR_Y_BASE,
		.uv0_base = MI_MP_WR_CB_BASE,
		.y1_base = MI_MP_WR_Y_BASE2,
		.uv1_base = MI_MP_WR_CB_BASE2,
		.g0_base = MI_GAIN_WR_BASE,
		.g1_base = MI_GAIN_WR_BASE2,

		.y0_base_shd = MI_MP_WR_Y_BASE_SHD,
		.uv0_base_shd = MI_MP_WR_CB_BASE_SHD,
		.g0_base_shd = MI_GAIN_WR_BASE_SHD,
	},
};

static void free_bridge_buf(struct rkisp_bridge_device *dev)
{
	struct rkisp_bridge_buf *buf;
	struct rkisp_ispp_buf *dbufs;
	int i, j;

	if (dev->cur_buf) {
		list_add_tail(&dev->cur_buf->list, &dev->list);
		if (dev->cur_buf == dev->nxt_buf)
			dev->nxt_buf = NULL;
		dev->cur_buf = NULL;
	}

	if (dev->nxt_buf) {
		list_add_tail(&dev->nxt_buf->list, &dev->list);
		dev->nxt_buf = NULL;
	}

	while (!list_empty(&dev->list)) {
		dbufs = list_first_entry(&dev->list,
				struct rkisp_ispp_buf, list);
		list_del(&dbufs->list);
	}

	for (i = 0; i < BRIDGE_BUF_MAX; i++) {
		buf = &dev->bufs[i];
		for (j = 0; j < GROUP_BUF_MAX; j++)
			rkisp_free_buffer(dev->ispdev->dev, &buf->dummy[j]);
	}
}

static int init_buf(struct rkisp_bridge_device *dev)
{
	struct v4l2_subdev *sd = v4l2_get_subdev_hostdata(&dev->sd);
	struct rkisp_bridge_buf *buf;
	struct rkisp_dummy_buffer *dummy;
	u32 width = dev->crop.width;
	u32 height = dev->crop.height;
	u32 offset = width * height;
	u32 pic_size = 0, gain_size;
	int i, j, ret = 0;

	gain_size = ALIGN(width, 64) * ALIGN(height, 128) >> 4;
	if (dev->work_mode & ISP_ISPP_FBC) {
		width = ALIGN(width, 16);
		height = ALIGN(height, 16);
		offset = width * height >> 4;
		pic_size = offset;
	}
	if (dev->work_mode & ISP_ISPP_422)
		pic_size += width * height * 2;
	else
		pic_size += width * height * 3 >> 1;
	dev->cfg->offset = offset;

	for (i = 0; i < dev->buf_num; i++) {
		buf = &dev->bufs[i];
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			dummy = &buf->dummy[j];
			dummy->is_need_dbuf = true;
			dummy->size = !j ? pic_size : gain_size;
			ret = rkisp_alloc_buffer(dev->ispdev->dev, dummy);
			if (ret)
				goto err;
			buf->dbufs.dbuf[j] = dummy->dbuf;
		}
		list_add_tail(&buf->dbufs.list, &dev->list);
		ret = v4l2_subdev_call(sd, video, s_rx_buffer, &buf->dbufs, NULL);
		if (ret)
			goto err;
	}

	return 0;
err:
	free_bridge_buf(dev);
	v4l2_err(&dev->sd, "%s fail:%d\n", __func__, ret);
	return ret;
}

static int config_mode(struct rkisp_bridge_device *dev)
{
	if (dev->work_mode == ISP_ISPP_INIT_FAIL) {
		free_bridge_buf(dev);
		return 0;
	}

	if (!dev->linked || !dev->ispdev->isp_inp) {
		v4l2_err(&dev->sd,
			 "invalid: link:%d or isp input:0x%x\n",
			 dev->linked,
			 dev->ispdev->isp_inp);
		return -EINVAL;
	}

	v4l2_dbg(1, rkisp_debug, &dev->sd,
		 "work mode:0x%x buf num:%d\n",
		 dev->work_mode, dev->buf_num);

	if (dev->work_mode & ISP_ISPP_FBC) {
		dev->ops = &mpfbc_ops;
		dev->cfg = &mpfbc_cfg;
	} else {
		dev->ops = &mp_ops;
		dev->cfg = &mp_cfg;
	}

	return init_buf(dev);
}

static void crop_on(struct rkisp_bridge_device *dev)
{
	struct rkisp_device *ispdev = dev->ispdev;
	void __iomem *base = ispdev->base_addr;
	u32 src_w = ispdev->isp_sdev.out_crop.width;
	u32 src_h = ispdev->isp_sdev.out_crop.height;
	u32 dest_w = dev->crop.width;
	u32 dest_h = dev->crop.height;
	u32 left = dev->crop.left;
	u32 top = dev->crop.top;
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

static void crop_off(struct rkisp_bridge_device *dev)
{
	struct rkisp_device *ispdev = dev->ispdev;
	void __iomem *base = ispdev->base_addr;
	u32 src_w = ispdev->isp_sdev.out_crop.width;
	u32 src_h = ispdev->isp_sdev.out_crop.height;
	u32 dest_w = dev->crop.width;
	u32 dest_h = dev->crop.height;
	u32 ctrl;

	if (src_w == dest_w && src_h == dest_h)
		return;

	ctrl = readl(base + CIF_DUAL_CROP_CTRL);
	ctrl &= ~(CIF_DUAL_CROP_MP_MODE_YUV |
		  CIF_DUAL_CROP_MP_MODE_RAW);
	ctrl |= CIF_DUAL_CROP_GEN_CFG_UPD;
	writel(ctrl, base + CIF_DUAL_CROP_CTRL);
}

static int bridge_start(struct rkisp_bridge_device *dev)
{
	void __iomem *base = dev->ispdev->base_addr;

	crop_on(dev);
	config_gain(dev);
	dev->ops->config(dev);

	force_cfg_update(base);

	if (!(dev->work_mode & ISP_ISPP_QUICK))
		update_mi(dev);

	dev->en = true;
	return 0;
}

static int bridge_stop(struct rkisp_bridge_device *dev)
{
	void __iomem *base = dev->ispdev->base_addr;
	int ret;

	dev->stopping = true;
	dev->ops->disable(base);
	hdr_stop_dmatx(dev->ispdev);
	ret = wait_event_timeout(dev->done, !dev->en,
				 msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->sd,
			  "%s timeout ret:%d\n", __func__, ret);
	crop_off(dev);
	isp_clear_bits(base + MI_IMSC, dev->cfg->frame_end_id);
	dev->stopping = false;
	dev->en = false;

	/* make sure ispp last frame done */
	if (dev->work_mode & ISP_ISPP_QUICK)
		usleep_range(20000, 25000);
	return 0;
}

static int bridge_start_stream(struct v4l2_subdev *sd)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	int ret;

	if (WARN_ON(dev->en))
		return -EBUSY;

	if (dev->ispdev->isp_inp & INP_CSI ||
	    dev->ispdev->isp_inp & INP_DVP) {
		/* Always update sensor info in case media topology changed */
		ret = rkisp_update_sensor_info(dev->ispdev);
		if (ret < 0) {
			v4l2_err(sd, "update sensor info failed %d\n", ret);
			goto free_buf;
		}
	}

	/* enable clocks/power-domains */
	ret = dev->ispdev->pipe.open(&dev->ispdev->pipe, &sd->entity, true);
	if (ret < 0)
		goto free_buf;

	hdr_config_dmatx(dev->ispdev);
	ret = bridge_start(dev);
	if (ret)
		goto close_pipe;
	hdr_update_dmatx_buf(dev->ispdev);

	/* start sub-devices */
	ret = dev->ispdev->pipe.set_stream(&dev->ispdev->pipe, true);
	if (ret < 0)
		goto stop_bridge;

	ret = media_pipeline_start(&sd->entity, &dev->ispdev->pipe.pipe);
	if (ret < 0)
		goto pipe_stream_off;

	return 0;
pipe_stream_off:
	dev->ispdev->pipe.set_stream(&dev->ispdev->pipe, false);
stop_bridge:
	bridge_stop(dev);
close_pipe:
	dev->ispdev->pipe.close(&dev->ispdev->pipe);
	hdr_destroy_buf(dev->ispdev);
free_buf:
	free_bridge_buf(dev);
	v4l2_err(&dev->sd, "%s fail:%d\n", __func__, ret);
	return ret;
}

static void bridge_destroy_buf(struct rkisp_bridge_device *dev)
{
	free_bridge_buf(dev);
	hdr_destroy_buf(dev->ispdev);
}

static int bridge_stop_stream(struct v4l2_subdev *sd)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);

	bridge_stop(dev);
	media_pipeline_stop(&sd->entity);
	dev->ispdev->pipe.set_stream(&dev->ispdev->pipe, false);
	dev->ispdev->pipe.close(&dev->ispdev->pipe);
	bridge_destroy_buf(dev);
	return 0;
}

static int bridge_get_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);

	if (!fmt)
		return -EINVAL;

	/* get isp out format */
	fmt->pad = RKISP_ISP_PAD_SOURCE_PATH;
	fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
	return v4l2_subdev_call(&dev->ispdev->isp_sdev.sd,
				pad, get_fmt, NULL, fmt);
}

static int bridge_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	struct rkisp_isp_subdev *isp_sd = &dev->ispdev->isp_sdev;
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

	dev->crop = *crop;
	return 0;
}

static int bridge_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	struct rkisp_isp_subdev *isp_sd = &dev->ispdev->isp_sdev;
	struct v4l2_rect *crop;

	if (!sel)
		return -EINVAL;

	crop = &sel->r;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		*crop = isp_sd->out_crop;
		break;
	case V4L2_SEL_TGT_CROP:
		*crop = dev->crop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bridge_s_rx_buffer(struct v4l2_subdev *sd,
			      void *buf, unsigned int *size)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	struct rkisp_ispp_buf *dbufs = buf;
	unsigned long lock_flags = 0;

	/* size isn't using now */
	if (!dbufs)
		return -EINVAL;

	spin_lock_irqsave(&dev->buf_lock, lock_flags);
	list_add_tail(&dbufs->list, &dev->list);
	spin_unlock_irqrestore(&dev->buf_lock, lock_flags);
	return 0;
}

static int bridge_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, sd,
		 "%s %d\n", __func__, on);

	if (on) {
		atomic_inc(&dev->ispdev->cap_dev.refcnt);
		ret = bridge_start_stream(sd);
	} else if (dev->en) {
		ret = bridge_stop_stream(sd);
	}

	if (!on)
		atomic_dec(&dev->ispdev->cap_dev.refcnt);
	return ret;
}

static int bridge_s_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, sd,
		 "%s %d\n", __func__, on);

	if (on)
		ret = v4l2_pipeline_pm_use(&sd->entity, 1);
	else
		ret = v4l2_pipeline_pm_use(&sd->entity, 0);

	return ret;
}

static long bridge_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	struct rkisp_ispp_mode *mode;
	long ret = 0;

	switch (cmd) {
	case RKISP_ISPP_CMD_SET_MODE:
		mode = arg;
		dev->work_mode = mode->work_mode;
		dev->buf_num = mode->buf_num;
		ret = config_mode(dev);
		rkisp_chk_tb_over(dev->ispdev);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static const struct v4l2_subdev_pad_ops bridge_pad_ops = {
	.set_fmt = bridge_get_set_fmt,
	.get_fmt = bridge_get_set_fmt,
	.get_selection = bridge_get_selection,
	.set_selection = bridge_set_selection,
};

static const struct v4l2_subdev_video_ops bridge_video_ops = {
	.s_rx_buffer = bridge_s_rx_buffer,
	.s_stream = bridge_s_stream,
};

static const struct v4l2_subdev_core_ops bridge_core_ops = {
	.s_power = bridge_s_power,
	.ioctl = bridge_ioctl,
};

static struct v4l2_subdev_ops bridge_v4l2_ops = {
	.core = &bridge_core_ops,
	.video = &bridge_video_ops,
	.pad = &bridge_pad_ops,
};

void rkisp_bridge_isr(u32 *mis_val, struct rkisp_device *dev)
{
	struct rkisp_bridge_device *bridge = &dev->br_dev;
	void __iomem *base = dev->base_addr;
	u32 val = 0;

	/* dmarx isr is unreliable, MI frame end to replace it */
	if (*mis_val & (MI_MP_FRAME | MI_MPFBC_FRAME) &&
	    IS_HDR_RDBK(dev->hdr.op_mode)) {
		switch (dev->hdr.op_mode) {
		case HDR_RDBK_FRAME3://for rd1 rd0 rd2
			val |= RAW1_RD_FRAME;
			/* FALLTHROUGH */
		case HDR_RDBK_FRAME2://for rd0 rd2
			val |= RAW0_RD_FRAME;
			/* FALLTHROUGH */
		default:// for rd2
			val |= RAW2_RD_FRAME;
			/* FALLTHROUGH */
		}
		rkisp2_rawrd_isr(val, dev);
		if (dev->dmarx_dev.trigger == T_MANUAL)
			rkisp_csi_trigger_event(&dev->csi_dev, NULL);
	}

	if (!bridge->en || !bridge->cfg ||
	    (bridge->cfg &&
	     !(*mis_val & bridge->cfg->frame_end_id)))
		return;

	*mis_val &= ~bridge->cfg->frame_end_id;
	writel(bridge->cfg->frame_end_id, base + CIF_MI_ICR);

	if (bridge->stopping) {
		if (bridge->ops->is_stopped(base)) {
			bridge->en = false;
			bridge->stopping = false;
			wake_up(&bridge->done);
		}
	} else if (!(bridge->work_mode & ISP_ISPP_QUICK)) {
		frame_end(bridge);
	}
}

int rkisp_register_bridge_subdev(struct rkisp_device *dev,
				 struct v4l2_device *v4l2_dev)
{
	struct rkisp_bridge_device *bridge = &dev->br_dev;
	struct v4l2_subdev *sd;
	struct media_entity *source, *sink;
	int ret;

	memset(bridge, 0, sizeof(*bridge));
	if (dev->isp_ver != ISP_V20)
		return 0;

	bridge->ispdev = dev;
	sd = &bridge->sd;
	v4l2_subdev_init(sd, &bridge_v4l2_ops);
	//sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.obj_type = 0;
	snprintf(sd->name, sizeof(sd->name), BRIDGE_DEV_NAME);
	bridge->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&sd->entity, 1, &bridge->pad);
	if (ret < 0)
		return ret;
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, bridge);
	sd->grp_id = GRP_ID_ISP_BRIDGE;
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		v4l2_err(sd, "Failed to register subdev\n");
		goto free_media;
	}
	bridge->crop = dev->isp_sdev.out_crop;
	/* bridge links */
	bridge->linked = true;
	source = &dev->isp_sdev.sd.entity;
	sink = &sd->entity;
	ret = media_create_pad_link(source, RKISP_ISP_PAD_SOURCE_PATH,
				    sink, 0, bridge->linked);
	init_waitqueue_head(&bridge->done);
	spin_lock_init(&bridge->buf_lock);
	INIT_LIST_HEAD(&bridge->list);
	return ret;

free_media:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void rkisp_unregister_bridge_subdev(struct rkisp_device *dev)
{
	struct v4l2_subdev *sd = &dev->br_dev.sd;

	if (dev->isp_ver != ISP_V20)
		return;
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

void rkisp_get_bridge_sd(struct platform_device *dev,
			 struct v4l2_subdev **sd)
{
	struct rkisp_device *isp_dev = platform_get_drvdata(dev);

	if (isp_dev)
		*sd = &isp_dev->br_dev.sd;
	else
		*sd = NULL;
}
EXPORT_SYMBOL(rkisp_get_bridge_sd);
