// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/rkisp1-config.h>

#include "dev.h"
#include "regs.h"

static void set_y_addr(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_y_base, val);
}

static void set_uv_addr(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_uv_base, val);
}


static void update_mi(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_dummy_buffer *dummy_buf;
	u32 val;

	if (stream->curr_buf) {
		val = stream->curr_buf->buff_addr[RKISPP_PLANE_Y];
		set_y_addr(stream, val);
		val = stream->curr_buf->buff_addr[RKISPP_PLANE_UV];
		set_uv_addr(stream, val);
	}

	if (stream->type == STREAM_OUTPUT && !stream->curr_buf) {
		dummy_buf = &dev->hw_dev->dummy_buf;
		set_y_addr(stream, dummy_buf->dma_addr);
		set_uv_addr(stream, dummy_buf->dma_addr);
	}

	v4l2_dbg(2, rkispp_debug, &stream->isppdev->v4l2_dev,
		 "%s stream:%d Y:0x%x UV:0x%x\n",
		 __func__, stream->id,
		 rkispp_read(dev, stream->config->reg.cur_y_base),
		 rkispp_read(dev, stream->config->reg.cur_uv_base));
}

static int config_fec(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	struct rkispp_fec_head *fec_data;
	struct rkispp_hw_dev *hw = dev->hw_dev;
	u32 fmt, mult = 1, mesh_size;
	u32 in_width, in_height;
	u32 addr_offs, max_w, max_h;
	u32 addryf, addrxf, addryi, addrxi;

	vdev = &dev->stream_vdev;
	vdev->fec.is_end = true;
	if (!(vdev->module_ens & ISPP_MODULE_FEC))
		return 0;

	if (dev->inp == INP_DDR) {
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = FMT_YUV422;
	}

	in_width = dev->ispp_sdev.in_fmt.width;
	in_height = dev->ispp_sdev.in_fmt.height;
	max_w = hw->max_in.w ? hw->max_in.w : in_width;
	max_h = hw->max_in.h ? hw->max_in.h : in_height;
	addr_offs =  max_w * max_h;
	vdev->fec.uv_offset = addr_offs;

	if (stream) {
		stream->config->frame_end_id = FEC_INT;
		stream->config->reg.cur_y_base = RKISPP_FEC_RD_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_FEC_RD_UV_BASE;
		stream->config->reg.cur_y_base_shd = RKISPP_FEC_RD_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_FEC_RD_UV_BASE_SHD;
	}


	if (fmt & FMT_YUYV)
		mult = 2;
	rkispp_set_bits(dev, RKISPP_FEC_CTRL, FMT_RD_MASK, fmt);

	rkispp_write(dev, RKISPP_FEC_RD_VIR_STRIDE, ALIGN(in_width * mult, 16) >> 2);
	rkispp_write(dev, RKISPP_FEC_SRC_SIZE, in_height << 16 | in_width);

	fec_data = (struct rkispp_fec_head *)dev->params_vdev.buf_fec[0].vaddr;
	if (fec_data) {
		rkispp_prepare_buffer(dev, &dev->params_vdev.buf_fec[0]);
		addrxf =
			dev->params_vdev.buf_fec[0].dma_addr + fec_data->meshxf_oft;
		addryf =
			dev->params_vdev.buf_fec[0].dma_addr + fec_data->meshyf_oft;
		addrxi =
			dev->params_vdev.buf_fec[0].dma_addr + fec_data->meshxi_oft;
		addryi =
			dev->params_vdev.buf_fec[0].dma_addr + fec_data->meshyi_oft;
		rkispp_write(dev, RKISPP_FEC_MESH_XFRA_BASE, addrxf);
		rkispp_write(dev, RKISPP_FEC_MESH_YFRA_BASE, addryf);
		rkispp_write(dev, RKISPP_FEC_MESH_XINT_BASE, addrxi);
		rkispp_write(dev, RKISPP_FEC_MESH_YINT_BASE, addryi);

		stream = &vdev->stream[STREAM_MB];
		if (stream->out_fmt.width > 1920) {
			mesh_size = cal_fec_mesh(stream->out_fmt.width, stream->out_fmt.height, 1);
			rkispp_set_bits(dev, RKISPP_FEC_CORE_CTRL, 0x20, SW_MESH_DENSITY);
		} else {
			mesh_size = cal_fec_mesh(stream->out_fmt.width, stream->out_fmt.height, 0);
			rkispp_set_bits(dev, RKISPP_FEC_CORE_CTRL, 0x20, 0);
		}
		rkispp_write(dev, RKISPP_FEC_MESH_SIZE, mesh_size);
	}

	stream = &vdev->stream[STREAM_MB];
	if (!stream->streaming) {
		rkispp_write(dev, RKISPP_FEC_WR_Y_BASE, hw->dummy_buf.dma_addr);
		rkispp_write(dev, RKISPP_FEC_WR_UV_BASE, hw->dummy_buf.dma_addr);
	}

	if (vdev->monitor.is_en) {
		init_completion(&vdev->monitor.fec.cmpl);
		schedule_work(&vdev->monitor.fec.work);
	}
	rkispp_set_clk_rate(dev->hw_dev->clks[0], dev->hw_dev->core_clk_max);
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, in_width, in_height,
		 rkispp_read(dev, RKISPP_FEC_CTRL),
		 rkispp_read(dev, RKISPP_FEC_CORE_CTRL));
	return 0;
}

static void fec_free_buf(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct list_head *list = &vdev->fec.list_rd;
	struct rkisp_ispp_buf *dbufs;

	if (vdev->fec.cur_rd)
		vdev->fec.cur_rd = NULL;
	while (!list_empty(list)) {
		dbufs = get_list_buf(list, true);
		if (dbufs->is_isp)
			v4l2_subdev_call(dev->ispp_sdev.remote_sd,
					 video, s_rx_buffer, dbufs, NULL);
		else
			get_list_buf(list, false);
	}
}

static int config_modules(struct rkispp_device *dev)
{
	int ret;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "stream module ens:0x%x\n", dev->stream_vdev.module_ens);
	dev->stream_vdev.monitor.monitoring_module = 0;
	dev->stream_vdev.monitor.restart_module = 0;
	dev->stream_vdev.monitor.is_restart = false;
	dev->stream_vdev.monitor.retry = 0;
	init_completion(&dev->stream_vdev.monitor.cmpl);

	ret = config_fec(dev);
	if (ret < 0)
		goto free_fec;

	/* config default params */
	dev->params_vdev.params_ops->rkispp_params_cfg(&dev->params_vdev, 0);

	return 0;
free_fec:
	fec_free_buf(dev);
	return ret;
}

static void fec_work_event(struct rkispp_device *dev,
			   void *buff_rd,
			   bool is_isr, bool is_quick)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_monitor *monitor = &vdev->monitor;
	struct list_head *list = &vdev->fec.list_rd;
	void __iomem *base = dev->hw_dev->base_addr;
	struct rkispp_stream *stream = &vdev->stream[STREAM_II];
	unsigned long lock_flags = 0, lock_flags1 = 0;
	bool is_start = false;
	struct rkisp_ispp_reg *reg_buf = NULL;
	struct rkispp_buffer *inbuf;
	struct v4l2_subdev *sd = NULL;
	u32 val;
	struct rkisp_ispp_buf *buf_rd = buff_rd;

	if (!(vdev->module_ens & ISPP_MODULE_FEC))
		return;
	if (dev->inp == INP_ISP)
		sd = dev->ispp_sdev.remote_sd;
	spin_lock_irqsave(&vdev->fec.buf_lock, lock_flags);
	/* event from fec frame end */
	if (!buf_rd && is_isr) {
		vdev->fec.is_end = true;
		if (vdev->fec.cur_rd) {
			if (sd) {
				v4l2_subdev_call(sd, video, s_rx_buffer, vdev->fec.cur_rd, NULL);
			} else if (stream->streaming && vdev->fec.cur_rd->priv) {
				inbuf = vdev->fec.cur_rd->priv;
				vb2_buffer_done(&inbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			}
			vdev->fec.cur_rd = NULL;
		}
	}
	spin_lock_irqsave(&monitor->lock, lock_flags1);
	if (monitor->is_restart && buf_rd) {
		list_add_tail(&buf_rd->list, list);
		goto restart_unlock;
	}

	if (buf_rd && vdev->fec.is_end && list_empty(list)) {
		/* fec read buf from nr */
		vdev->fec.cur_rd = buf_rd;
	} else if (vdev->fec.is_end && !list_empty(list)) {
		/* fec read buf from list
		 * fec processing slow than nr
		 * new read buf from nr into list
		 */
		vdev->fec.cur_rd = get_list_buf(list, true);
		if (buf_rd)
			list_add_tail(&buf_rd->list, list);
	} else if (!vdev->fec.is_end && buf_rd) {
		/* fec no idle
		 * new read buf from nr into list
		 */
		list_add_tail(&buf_rd->list, list);
	}

	if (vdev->fec.cur_rd && vdev->fec.is_end) {
		if (vdev->fec.cur_rd->priv) {
			inbuf = vdev->fec.cur_rd->priv;
			val = inbuf->buff_addr[RKISPP_PLANE_Y];
			rkispp_write(dev, RKISPP_FEC_RD_Y_BASE, val);
			val = inbuf->buff_addr[RKISPP_PLANE_UV];
			rkispp_write(dev, RKISPP_FEC_RD_UV_BASE, val);
		} else {
			struct rkispp_isp_buf_pool *buf;

			buf = get_pool_buf(dev, vdev->fec.cur_rd);
			val = buf->dma[GROUP_BUF_PIC];
			rkispp_write(dev, RKISPP_FEC_RD_Y_BASE, val);
			val += vdev->fec.uv_offset;
			rkispp_write(dev, RKISPP_FEC_RD_UV_BASE, val);
		}
		is_start = true;
	}

	if (is_start) {
		u32 seq = 0;
		u64 timestamp = 0;

		if (vdev->fec.cur_rd) {
			seq = vdev->fec.cur_rd->frame_id;
			timestamp = vdev->fec.cur_rd->frame_timestamp;
			dev->ispp_sdev.frm_sync_seq = seq;
			dev->ispp_sdev.frame_timestamp = timestamp;
			rkispp_set_bits(dev, RKISPP_FEC_CORE_CTRL, 0x00, SW_FEC_EN);
		}

		stream = &vdev->stream[STREAM_MB];
		if (stream->streaming && !stream->is_cfg)
			secure_config_mb(stream);

		if (!dev->hw_dev->is_single)
			rkispp_update_regs(dev, RKISPP_CTRL, RKISPP_FEC_SRC_SIZE);
		writel(FEC_FORCE_UPD, base + RKISPP_CTRL_UPDATE);

		v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
			 "FEC start seq:%d | Y_SHD rd:0x%x\n",
			 seq, readl(base + RKISPP_FEC_RD_Y_BASE_SHD));
		v4l2_dbg(2, rkispp_debug, &stream->isppdev->v4l2_dev,
			"%s stream:%d Y:0x%x UV:0x%x\n",
			__func__, stream->id,
			rkispp_read(dev, stream->config->reg.cur_y_base),
			rkispp_read(dev, stream->config->reg.cur_uv_base));
		vdev->fec.dbg.id = seq;
		vdev->fec.dbg.timestamp = ktime_get_ns();
		if (monitor->is_en) {
			monitor->fec.time = vdev->fec.dbg.interval / 1000 / 1000;
			monitor->monitoring_module |= MONITOR_FEC;
			if (!completion_done(&monitor->fec.cmpl))
				complete(&monitor->fec.cmpl);
		}

		if (stream->is_reg_withstream)
			rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id, seq);

		if (!dev->hw_dev->is_shutdown)
			writel(FEC_ST, base + RKISPP_CTRL_STRT);

		vdev->fec.is_end = false;
	}
restart_unlock:
	spin_unlock_irqrestore(&monitor->lock, lock_flags1);
	spin_unlock_irqrestore(&vdev->fec.buf_lock, lock_flags);

}

static void rkispp_module_work_event(struct rkispp_device *dev,
				     void *buf_rd, void *buf_wr,
				     u32 module, bool is_isr)
{

	if (dev->hw_dev->is_shutdown)
		return;

	if (dev->ispp_sdev.state != ISPP_STOP)
		fec_work_event(dev, buf_rd, is_isr, false);

	/* cur frame (tnr->nr->fec) done for next frame
	 * fec start at nr end if fec enable, and fec can async with
	 * tnr different frames for single device.
	 * tnr->nr->fec frame0
	 *       |->tnr->nr->fec frame1
	 */
	if (is_isr && !buf_rd && !buf_wr) {
		dev->stream_vdev.monitor.retry = 0;
		rkispp_event_handle(dev, CMD_QUEUE_DMABUF, NULL);

	}

	if (dev->ispp_sdev.state == ISPP_STOP) {
		if ((module & ISPP_MODULE_FEC) && buf_rd) {
			struct rkisp_ispp_buf *buf = buf_rd;

			if (buf->is_isp)
				v4l2_subdev_call(dev->ispp_sdev.remote_sd,
						 video, s_rx_buffer, buf, NULL);
		}
		if (!dev->hw_dev->is_idle)
			dev->hw_dev->is_idle = true;
	}
}

static void rkispp_destroy_buf(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;

	if (atomic_read(&vdev->refcnt) == 1) {
		vdev->irq_ends = 0;
		fec_free_buf(dev);
		rkispp_event_handle(dev, CMD_FREE_POOL, NULL);
	}
}

static int start_isp(struct rkispp_device *dev)
{
	struct rkispp_subdev *ispp_sdev = &dev->ispp_sdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream;
	struct rkisp_ispp_mode mode;
	int ret;

	if (dev->inp != INP_ISP || ispp_sdev->state)
		return 0;

	if (dev->stream_sync) {
		stream = &vdev->stream[STREAM_MB];
		if (stream->linked && !stream->streaming)
			return 0;
	} else if (atomic_read(&vdev->refcnt) > 1) {
		return 0;
	}

	rkispp_start_3a_run(dev);

	mutex_lock(&dev->hw_dev->dev_lock);

	mode.work_mode = ISP_ISPP_422;
	mode.buf_num = 1;
	mode.buf_num += RKISP_BUF_MAX + 2 * (dev->hw_dev->dev_num - 1);

	ret = v4l2_subdev_call(ispp_sdev->remote_sd, core, ioctl,
			       RKISP_ISPP_CMD_SET_MODE, &mode);
	if (ret)
		goto err;

	ret = config_modules(dev);
	if (ret) {
		rkispp_event_handle(dev, CMD_FREE_POOL, NULL);
		mode.work_mode = ISP_ISPP_INIT_FAIL;
		v4l2_subdev_call(ispp_sdev->remote_sd, core, ioctl,
				 RKISP_ISPP_CMD_SET_MODE, &mode);
		goto err;
	}

	if (dev->hw_dev->is_single)
		writel(ALL_FORCE_UPD, dev->hw_dev->base_addr + RKISPP_CTRL_UPDATE);
	stream = &vdev->stream[STREAM_MB];
	if (stream->streaming)
		stream->is_upd = true;
	if (dev->isp_mode & ISP_ISPP_QUICK)
		rkispp_set_bits(dev, RKISPP_CTRL_QUICK, 0, GLB_QUICK_EN);

	dev->isr_cnt = 0;
	dev->isr_err_cnt = 0;
	ret = v4l2_subdev_call(&ispp_sdev->sd, video, s_stream, true);
err:
	mutex_unlock(&dev->hw_dev->dev_lock);
	return ret;
}

static void check_to_force_update(struct rkispp_device *dev, u32 mis_val)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream;
	u32  mask = FEC_INT;

	vdev->irq_ends |= (mis_val & mask);
	v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
		 "irq_ends:0x%x mask:0x%x\n",
		 vdev->irq_ends, mask);
	if (vdev->irq_ends != mask)
		return;
	vdev->irq_ends = 0;
	if (mis_val & FEC_INT)
		rkispp_module_work_event(dev, NULL, NULL,
					 ISPP_MODULE_FEC, true);

	stream = &vdev->stream[STREAM_MB];
	if (stream->streaming)
		stream->is_upd = true;

}

static struct rkispp_stream_ops rkispp_stream_ops = {
	.config_modules = config_modules,
	.destroy_buf = rkispp_destroy_buf,
	.fec_work_event = fec_work_event,
	.start_isp = start_isp,
	.check_to_force_update = check_to_force_update,
	.update_mi = update_mi,
	.rkispp_module_work_event = rkispp_module_work_event,
};

void rkispp_stream_init_ops_v20(struct rkispp_stream_vdev *stream_vdev)
{
	stream_vdev->stream_ops = &rkispp_stream_ops;
}
