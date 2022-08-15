// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
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

static void free_bridge_buf(struct rkisp_bridge_device *dev)
{
	struct rkisp_hw_dev *hw = dev->ispdev->hw_dev;
	struct rkisp_bridge_buf *buf;
	struct rkisp_ispp_buf *dbufs;
	unsigned long lock_flags = 0;
	int i, j;

	spin_lock_irqsave(&hw->buf_lock, lock_flags);
	if (--hw->buf_init_cnt > 0) {
		spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
		return;
	}

	v4l2_dbg(1, rkisp_debug, &dev->ispdev->v4l2_dev,
		 "%s\n", __func__);

	if (hw->cur_buf) {
		list_add_tail(&hw->cur_buf->list, &hw->list);
		if (hw->cur_buf == hw->nxt_buf)
			hw->nxt_buf = NULL;
		hw->cur_buf = NULL;
	}

	if (hw->nxt_buf) {
		list_add_tail(&hw->nxt_buf->list, &hw->list);
		hw->nxt_buf = NULL;
	}

	if (dev->ispdev->cur_fbcgain) {
		list_add_tail(&dev->ispdev->cur_fbcgain->list, &hw->list);
		dev->ispdev->cur_fbcgain = NULL;
	}

	while (!list_empty(&hw->rpt_list)) {
		dbufs = list_first_entry(&hw->rpt_list,
				struct rkisp_ispp_buf, list);
		list_del(&dbufs->list);
		list_add_tail(&dbufs->list, &hw->list);
	}

	while (!list_empty(&hw->list)) {
		dbufs = list_first_entry(&hw->list,
				struct rkisp_ispp_buf, list);
		list_del(&dbufs->list);
	}

	hw->is_buf_init = false;
	spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
	for (i = 0; i < BRIDGE_BUF_MAX; i++) {
		buf = &hw->bufs[i];
		for (j = 0; j < GROUP_BUF_MAX; j++)
			rkisp_free_buffer(dev->ispdev, &buf->dummy[j]);
	}

	rkisp_free_common_dummy_buf(dev->ispdev);
}

static int init_buf(struct rkisp_bridge_device *dev, u32 pic_size, u32 gain_size)
{
	struct v4l2_subdev *sd = v4l2_get_subdev_hostdata(&dev->sd);
	struct rkisp_hw_dev *hw = dev->ispdev->hw_dev;
	struct rkisp_bridge_buf *buf;
	struct rkisp_dummy_buffer *dummy;
	int i, j, val, ret = 0;
	unsigned long lock_flags = 0;
	bool is_direct = (hw->isp_ver == ISP_V20) ? true : false;

	spin_lock_irqsave(&hw->buf_lock, lock_flags);
	if (++hw->buf_init_cnt > 1) {
		spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
		return 0;
	}
	spin_unlock_irqrestore(&hw->buf_lock, lock_flags);

	v4l2_dbg(1, rkisp_debug, &dev->ispdev->v4l2_dev,
		 "%s pic size:%d gain size:%d\n",
		 __func__, pic_size, gain_size);

	INIT_LIST_HEAD(&hw->list);
	for (i = 0; i < dev->buf_num; i++) {
		buf = &hw->bufs[i];
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			if (j && hw->isp_ver == ISP_V30)
				continue;
			dummy = &buf->dummy[j];
			dummy->is_need_vaddr = true;
			dummy->is_need_dbuf = true;
			dummy->size = PAGE_ALIGN(!j ? pic_size : gain_size);
			ret = rkisp_alloc_buffer(dev->ispdev, dummy);
			if (ret)
				goto err;
			buf->dbufs.dbuf[j] = dummy->dbuf;
			buf->dbufs.didx[j] = i * GROUP_BUF_MAX + j;
			buf->dbufs.gain_size = PAGE_ALIGN(gain_size);
			buf->dbufs.mfbc_size = PAGE_ALIGN(pic_size);
		}
		list_add_tail(&buf->dbufs.list, &hw->list);
		ret = v4l2_subdev_call(sd, video, s_rx_buffer, &buf->dbufs, NULL);
		if (ret)
			goto err;
	}

	for (i = 0; i < hw->dev_num; i++) {
		struct rkisp_device *isp = hw->isp[i];

		if (!isp ||
		    (isp && !(isp->isp_inp & INP_CSI)))
			continue;
		ret = rkisp_alloc_common_dummy_buf(isp);
		if (ret < 0)
			goto err;
		else
			break;
	}

	hw->cur_buf = list_first_entry(&hw->list, struct rkisp_ispp_buf, list);
	list_del(&hw->cur_buf->list);
	buf = to_bridge_buf(hw->cur_buf);
	val = buf->dummy[GROUP_BUF_PIC].dma_addr;
	rkisp_write(dev->ispdev, dev->cfg->reg.y0_base, val, is_direct);
	val += dev->cfg->offset;
	rkisp_write(dev->ispdev, dev->cfg->reg.uv0_base, val, is_direct);
	if (hw->isp_ver == ISP_V20) {
		val = buf->dummy[GROUP_BUF_GAIN].dma_addr;
		rkisp_write(dev->ispdev, dev->cfg->reg.g0_base, val, is_direct);
	}

	if (!list_empty(&hw->list)) {
		hw->nxt_buf = list_first_entry(&hw->list,
				struct rkisp_ispp_buf, list);
		list_del(&hw->nxt_buf->list);
	}
	if (hw->nxt_buf && (dev->work_mode & ISP_ISPP_QUICK)) {
		buf = to_bridge_buf(hw->nxt_buf);
		val = buf->dummy[GROUP_BUF_PIC].dma_addr;
		rkisp_write(dev->ispdev, dev->cfg->reg.y1_base, val, true);
		val += dev->cfg->offset;
		rkisp_write(dev->ispdev, dev->cfg->reg.uv1_base, val, true);
		val = buf->dummy[GROUP_BUF_GAIN].dma_addr;
		rkisp_write(dev->ispdev, dev->cfg->reg.g1_base, val, true);
		rkisp_set_bits(dev->ispdev, MI_WR_CTRL2,
			       0, SW_GAIN_WR_PINGPONG, true);
	}

	rkisp_set_bits(dev->ispdev, CIF_VI_DPCL, 0,
		       CIF_VI_DPCL_CHAN_MODE_MP |
		       CIF_VI_DPCL_MP_MUX_MRSZ_MI, true);
	rkisp_set_bits(dev->ispdev, MI_WR_CTRL, 0,
		       CIF_MI_CTRL_INIT_BASE_EN |
		       CIF_MI_CTRL_INIT_OFFSET_EN, true);
	rkisp_set_bits(dev->ispdev, MI_IMSC, 0,
		       dev->cfg->frame_end_id, true);

	spin_lock_irqsave(&hw->buf_lock, lock_flags);
	hw->is_buf_init = true;
	spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
	return 0;
err:
	free_bridge_buf(dev);
	v4l2_err(&dev->sd, "%s fail:%d\n", __func__, ret);
	return ret;
}

static int config_mode(struct rkisp_bridge_device *dev)
{
	struct rkisp_hw_dev *hw = dev->ispdev->hw_dev;
	u32 w = hw->max_in.w ? hw->max_in.w : dev->crop.width;
	u32 h = hw->max_in.h ? hw->max_in.h : dev->crop.height;
	u32 offs = w * h;
	u32 pic_size = 0, gain_size = 0;

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

	if (hw->isp_ver == ISP_V20) {
		gain_size = ALIGN(w, 64) * ALIGN(h, 128) >> 4;
		rkisp_bridge_init_ops_v20(dev);
	} else {
		dev->work_mode &= ~(ISP_ISPP_FBC | ISP_ISPP_QUICK);
		rkisp_bridge_init_ops_v30(dev);
	}

	if (dev->work_mode & ISP_ISPP_FBC) {
		w = ALIGN(w, 16);
		h = ALIGN(h, 16);
		offs = w * h >> 4;
		pic_size = offs;
	}
	if (dev->work_mode & ISP_ISPP_422)
		pic_size += w * h * 2;
	else
		pic_size += w * h * 3 >> 1;
	dev->cfg->offset = offs;

	if (hw->isp_ver == ISP_V20) {
		pic_size += RKISP_MOTION_DECT_TS_SIZE;
		gain_size += RKISP_MOTION_DECT_TS_SIZE;
	}
	return init_buf(dev, pic_size, gain_size);
}

static int bridge_start_stream(struct v4l2_subdev *sd)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	int ret = -EINVAL;

	if (WARN_ON(dev->en))
		return -EBUSY;

	if (dev->ispdev->isp_sdev.out_fmt.fmt_type == FMT_BAYER) {
		v4l2_err(sd, "no support raw from isp to ispp\n");
		goto free_buf;
	}

	if (dev->ispdev->isp_inp & INP_CSI ||
	    dev->ispdev->isp_inp & INP_DVP ||
	    dev->ispdev->isp_inp & INP_LVDS ||
	    dev->ispdev->isp_inp & INP_CIF) {
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

	ret = dev->ops->start(dev);
	if (ret)
		goto close_pipe;

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
	dev->ops->stop(dev);
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

	dev->ops->stop(dev);
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
	struct rkisp_hw_dev *hw = dev->ispdev->hw_dev;
	struct rkisp_ispp_buf *dbufs = buf;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(&hw->buf_lock, lock_flags);
	/* size isn't using now */
	if (!dbufs || !hw->buf_init_cnt) {
		spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
		return -EINVAL;
	}
	list_add_tail(&dbufs->list, &hw->list);
	spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
	return 0;
}

static int bridge_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	struct rkisp_hw_dev *hw = dev->ispdev->hw_dev;
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, sd,
		 "%s %d\n", __func__, on);

	mutex_lock(&hw->dev_lock);
	if (on) {
		memset(&dev->dbg, 0, sizeof(dev->dbg));
		atomic_inc(&dev->ispdev->cap_dev.refcnt);
		ret = bridge_start_stream(sd);
	} else {
		if (dev->en)
			ret = bridge_stop_stream(sd);
		atomic_dec(&dev->ispdev->cap_dev.refcnt);
	}
	mutex_unlock(&hw->dev_lock);

	return ret;
}

static int bridge_s_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, sd,
		 "%s %d\n", __func__, on);

	if (on)
		ret = v4l2_pipeline_pm_get(&sd->entity);
	else
		v4l2_pipeline_pm_put(&sd->entity);

	return ret;
}

static long bridge_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rkisp_bridge_device *dev = v4l2_get_subdevdata(sd);
	struct rkisp_ispp_mode *mode;
	struct max_input *max_in;
	long ret = 0;

	switch (cmd) {
	case RKISP_ISPP_CMD_SET_FMT:
		max_in = arg;
		dev->ispdev->hw_dev->max_in = *max_in;
		break;
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

void rkisp_bridge_update_mi(struct rkisp_device *dev, u32 isp_mis)
{
	struct rkisp_bridge_device *br = &dev->br_dev;
	struct rkisp_hw_dev *hw = dev->hw_dev;
	unsigned long lock_flags = 0;

	if ((dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V30) ||
	    !br->en || br->work_mode & ISP_ISPP_QUICK ||
	    isp_mis & CIF_ISP_FRAME)
		return;

	br->fs_ns = ktime_get_ns();
	spin_lock_irqsave(&hw->buf_lock, lock_flags);
	if (!hw->nxt_buf && !list_empty(&hw->list)) {
		hw->nxt_buf = list_first_entry(&hw->list,
				struct rkisp_ispp_buf, list);
		list_del(&hw->nxt_buf->list);
	}
	spin_unlock_irqrestore(&hw->buf_lock, lock_flags);

	br->ops->update_mi(br);
}

void rkisp_bridge_isr(u32 *mis_val, struct rkisp_device *dev)
{
	struct rkisp_bridge_device *bridge = &dev->br_dev;
	void __iomem *base = dev->base_addr;
	u32 irq;

	if (!bridge->en)
		return;

	if (!bridge->cfg ||
	    (bridge->cfg &&
	     !(*mis_val & bridge->cfg->frame_end_id)))
		return;

	irq = bridge->cfg->frame_end_id;
	*mis_val &= ~irq;
	writel(irq, base + CIF_MI_ICR);

	irq = (irq == MI_MPFBC_FRAME) ? ISP_FRAME_MPFBC : ISP_FRAME_MP;
	bridge->ops->frame_end(bridge, FRAME_IRQ);

	rkisp_check_idle(dev, irq);
}

static int check_remote_node(struct rkisp_device *ispdev)
{
	struct device *dev = ispdev->dev;
	struct device_node *parent = dev->of_node;
	struct device_node *remote = NULL;
	int i, j;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 2; j++) {
			remote = of_graph_get_remote_node(parent, i, j);
			if (!remote)
				continue;
			of_node_put(remote);
			if (strstr(of_node_full_name(remote), "ispp"))
				return 0;
		}
	}

	return -ENODEV;
}

int rkisp_register_bridge_subdev(struct rkisp_device *dev,
				 struct v4l2_device *v4l2_dev)
{
	struct rkisp_bridge_device *bridge = &dev->br_dev;
	struct v4l2_subdev *sd;
	struct media_entity *source, *sink;
	int ret;

	memset(bridge, 0, sizeof(*bridge));
	if ((dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V30) ||
	    check_remote_node(dev) < 0)
		return 0;

	bridge->ispdev = dev;
	sd = &bridge->sd;
	v4l2_subdev_init(sd, &bridge_v4l2_ops);
	//sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.obj_type = 0;
	snprintf(sd->name, sizeof(sd->name), "%s", BRIDGE_DEV_NAME);
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
	bridge->wq = alloc_workqueue("rkisp bridge workqueue",
				     WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	hrtimer_init(&bridge->frame_qst, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	return ret;

free_media:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void rkisp_unregister_bridge_subdev(struct rkisp_device *dev)
{
	struct v4l2_subdev *sd = &dev->br_dev.sd;

	if ((dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V30) ||
	    check_remote_node(dev) < 0)
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
