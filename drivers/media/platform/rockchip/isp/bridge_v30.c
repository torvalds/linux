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

static void crop_on(struct rkisp_bridge_device *dev)
{
	struct rkisp_device *ispdev = dev->ispdev;
	u32 src_w = ispdev->isp_sdev.out_crop.width;
	u32 src_h = ispdev->isp_sdev.out_crop.height;
	u32 dest_w = dev->crop.width;
	u32 dest_h = dev->crop.height;
	u32 left = dev->crop.left;
	u32 top = dev->crop.top;
	u32 ctrl = CIF_DUAL_CROP_CFG_UPD;

	rkisp_write(ispdev, CIF_DUAL_CROP_M_H_OFFS, left, false);
	rkisp_write(ispdev, CIF_DUAL_CROP_M_V_OFFS, top, false);
	rkisp_write(ispdev, CIF_DUAL_CROP_M_H_SIZE, dest_w, false);
	rkisp_write(ispdev, CIF_DUAL_CROP_M_V_SIZE, dest_h, false);
	ctrl |= rkisp_read(ispdev, CIF_DUAL_CROP_CTRL, true);
	if (src_w == dest_w && src_h == dest_h)
		ctrl &= ~(CIF_DUAL_CROP_MP_MODE_YUV | CIF_DUAL_CROP_MP_MODE_RAW);
	else
		ctrl |= CIF_DUAL_CROP_MP_MODE_YUV;
	rkisp_write(ispdev, CIF_DUAL_CROP_CTRL, ctrl, false);
}

static void crop_off(struct rkisp_bridge_device *dev)
{
	struct rkisp_device *ispdev = dev->ispdev;
	u32 ctrl = CIF_DUAL_CROP_GEN_CFG_UPD;

	ctrl = rkisp_read(ispdev, CIF_DUAL_CROP_CTRL, true);
	ctrl &= ~(CIF_DUAL_CROP_MP_MODE_YUV | CIF_DUAL_CROP_MP_MODE_RAW);
	rkisp_write(ispdev, CIF_DUAL_CROP_CTRL, ctrl, false);
}

static int bridge_start(struct rkisp_bridge_device *dev)
{
	crop_on(dev);
	dev->ops->config(dev);

	rkisp_stats_first_ddr_config(&dev->ispdev->stats_vdev);
	if (!dev->ispdev->hw_dev->is_mi_update)
		force_cfg_update(dev->ispdev);

	dev->ispdev->skip_frame = 0;
	dev->en = true;
	return 0;
}

static int bridge_stop(struct rkisp_bridge_device *dev)
{
	int ret;

	dev->stopping = true;
	if (dev->ispdev->hw_dev->is_single)
		dev->ops->disable(dev);
	ret = wait_event_timeout(dev->done, !dev->en,
				 msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->sd,
			  "%s timeout ret:%d\n", __func__, ret);
	crop_off(dev);
	dev->stopping = false;
	dev->en = false;
	dev->ops->disable(dev);
	dev->ispdev->irq_ends_mask &= ~ISP_FRAME_MP;
	return 0;
}

static void bridge_update_mi(struct rkisp_bridge_device *br)
{
	struct rkisp_device *dev = br->ispdev;
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_bridge_buf *buf;
	u32 val;

	if (hw->nxt_buf) {
		buf = to_bridge_buf(hw->nxt_buf);
		val = buf->dummy[GROUP_BUF_PIC].dma_addr;
		rkisp_write(dev, br->cfg->reg.y0_base, val, true);
		val += br->cfg->offset;
		rkisp_write(dev, br->cfg->reg.uv0_base, val, true);
	}

	v4l2_dbg(2, rkisp_debug, &br->sd,
		 "update pic(shd:0x%x base:0x%x)\n",
		 rkisp_read(dev, br->cfg->reg.y0_base_shd, true),
		 rkisp_read(dev, br->cfg->reg.y0_base, true));
}

static int bridge_frame_end(struct rkisp_bridge_device *dev, u32 state)
{
	struct rkisp_device *ispdev = dev->ispdev;
	struct rkisp_hw_dev *hw = ispdev->hw_dev;
	struct v4l2_subdev *sd = v4l2_get_subdev_hostdata(&dev->sd);
	unsigned long lock_flags = 0;
	u64 ns = ktime_get_ns();

	if (dev->stopping) {
		if (!hw->is_single) {
			dev->en = false;
			dev->stopping = false;
			dev->ops->disable(dev);
			wake_up(&dev->done);
		} else if (dev->ops->is_stopped(dev)) {
			dev->en = false;
			dev->stopping = false;
			wake_up(&dev->done);
		}
	}

	if (!dev->en) {
		ispdev->irq_ends_mask &= ~ISP_FRAME_MP;
		return 0;
	}

	rkisp_dmarx_get_frame(ispdev, &dev->dbg.id, NULL, NULL, true);
	dev->dbg.interval = ns - dev->dbg.timestamp;
	dev->dbg.timestamp = ns;
	if (hw->cur_buf && hw->nxt_buf) {
		if (ispdev->skip_frame > 0) {
			ispdev->skip_frame--;
			spin_lock_irqsave(&hw->buf_lock, lock_flags);
			list_add_tail(&hw->cur_buf->list, &hw->list);
			spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
		} else {
			ns = 0;
			rkisp_dmarx_get_frame(ispdev, &hw->cur_buf->frame_id,
					      NULL, &ns, true);
			if (!ns)
				ns = ktime_get_ns();
			hw->cur_buf->frame_timestamp = ns;
			hw->cur_buf->index = ispdev->dev_id;
			v4l2_subdev_call(sd, video, s_rx_buffer, hw->cur_buf, NULL);
		}
		hw->cur_buf = NULL;
	} else {
		v4l2_dbg(1, rkisp_debug, &dev->sd, "lost frm_id %d\n", dev->dbg.id);
		dev->dbg.frameloss++;
	}

	if (hw->nxt_buf) {
		hw->cur_buf = hw->nxt_buf;
		hw->nxt_buf = NULL;
	}

	return 0;
}

static int config_mp(struct rkisp_bridge_device *dev)
{
	u32 w = dev->crop.width;
	u32 h = dev->crop.height;
	u32 val;

	val = w * h;
	rkisp_write(dev->ispdev, CIF_MI_MP_Y_SIZE_INIT, val, false);
	val = (dev->work_mode & ISP_ISPP_422) ? val : val / 2;
	rkisp_write(dev->ispdev, CIF_MI_MP_CB_SIZE_INIT, val, false);
	rkisp_write(dev->ispdev, CIF_MI_MP_CR_SIZE_INIT, 0, false);
	rkisp_write(dev->ispdev, CIF_MI_MP_Y_OFFS_CNT_INIT, 0, false);
	rkisp_write(dev->ispdev, CIF_MI_MP_CB_OFFS_CNT_INIT, 0, false);
	rkisp_write(dev->ispdev, CIF_MI_MP_CR_OFFS_CNT_INIT, 0, false);

	rkisp_write(dev->ispdev, ISP_MPFBC_BASE,
		    dev->work_mode & ISP_ISPP_422, false);
	rkisp_set_bits(dev->ispdev, CIF_MI_CTRL, MI_CTRL_MP_FMT_MASK,
		       MI_CTRL_MP_WRITE_YUV_SPLA | CIF_MI_CTRL_MP_ENABLE |
		       CIF_MI_MP_AUTOUPDATE_ENABLE, false);
	dev->ispdev->irq_ends_mask |= ISP_FRAME_MP;
	return 0;
}

static void disable_mp(struct rkisp_bridge_device *dev)
{
	rkisp_clear_bits(dev->ispdev, CIF_MI_CTRL,
		CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE, false);
}

static bool is_stopped_mp(struct rkisp_bridge_device *dev)
{
	bool en = true;

	if (dev->ispdev->hw_dev->is_single)
		en = mp_is_stream_stopped(dev->ispdev->base_addr);
	return en;
}

static struct rkisp_bridge_ops mp_ops = {
	.config = config_mp,
	.disable = disable_mp,
	.is_stopped = is_stopped_mp,
	.frame_end = bridge_frame_end,
	.update_mi = bridge_update_mi,
	.start = bridge_start,
	.stop = bridge_stop,
};

static struct rkisp_bridge_config mp_cfg = {
	.frame_end_id = MI_MP_FRAME,
	.reg = {
		.y0_base = MI_MP_WR_Y_BASE,
		.uv0_base = MI_MP_WR_CB_BASE,
		.y1_base = MI_MP_WR_Y_BASE2,
		.uv1_base = MI_MP_WR_CB_BASE2,

		.y0_base_shd = MI_MP_WR_Y_BASE_SHD,
		.uv0_base_shd = MI_MP_WR_CB_BASE_SHD,
	},
};

void rkisp_bridge_init_ops_v30(struct rkisp_bridge_device *dev)
{
	dev->ops = &mp_ops;
	dev->cfg = &mp_cfg;
}
