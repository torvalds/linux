// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

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
#include <uapi/linux/rk-video-format.h>

#include "dev.h"
#include "regs.h"


/*
 * DDR->|                                 |->MB------->DDR
 *      |->TNR->DDR->NR->SHARP->DDR->FEC->|->SCL0----->DDR
 * ISP->|                                 |->SCL1----->DDR
 *                                        |->SCL2----->DDR
 */

static void rkispp_module_work_event(struct rkispp_device *dev,
			      void *buf_rd, void *buf_wr,
			      u32 module, bool is_isr);

static void set_y_addr(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_y_base, val);
}

static void set_uv_addr(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_uv_base, val);
}

static enum hrtimer_restart rkispp_frame_done_early(struct hrtimer *timer)
{
	struct rkispp_stream_vdev *vdev =
		container_of(timer, struct rkispp_stream_vdev, frame_qst);
	struct rkispp_stream *stream = &vdev->stream[0];
	struct rkispp_device *dev = stream->isppdev;
	void __iomem *base = dev->hw_dev->base_addr;
	bool is_fec_en = (vdev->module_ens & ISPP_MODULE_FEC);
	enum hrtimer_restart ret = HRTIMER_NORESTART;
	u32 threshold = vdev->wait_line / 128;
	u32 tile, tile_mask, working, work_mask;
	u32 i, seq, ycnt, shift, time, max_time;
	u64 t, ns = ktime_get_ns();

	working = readl(base + RKISPP_CTRL_SYS_STATUS);
	tile = readl(base + RKISPP_CTRL_SYS_CTL_STA0);
	if (is_fec_en) {
		shift = 16;
		work_mask = FEC_WORKING;
		tile_mask = FEC_TILE_LINE_CNT_MASK;
		t = vdev->fec.dbg.timestamp;
		seq = vdev->fec.dbg.id;
		max_time = 6000000;
	} else {
		shift = 8;
		work_mask = NR_WORKING;
		tile_mask = NR_TILE_LINE_CNT_MASK;
		t = vdev->nr.dbg.timestamp;
		seq = vdev->nr.dbg.id;
		max_time = 2000000;
	}
	working &= work_mask;
	tile &= tile_mask;
	ycnt = tile >> shift;
	time = (u32)(ns - t);
	if (dev->ispp_sdev.state == ISPP_STOP) {
		vdev->is_done_early = false;
		goto end;
	} else if (working && ycnt < threshold) {
		if (!ycnt)
			ns = max_time;
		else
			ns = time * (threshold - ycnt) / ycnt + 100 * 1000;
		if (ns > max_time)
			ns = max_time;
		hrtimer_forward(timer, timer->base->get_time(), ns_to_ktime(ns));
		ret = HRTIMER_RESTART;
	} else {
		v4l2_dbg(3, rkispp_debug, &stream->isppdev->v4l2_dev,
			 "%s seq:%d line:%d ycnt:%d time:%dus\n",
			 __func__, seq, vdev->wait_line, ycnt * 128, time / 1000);
		for (i = 0; i < dev->stream_max; i++) {
			stream = &vdev->stream[i];
			if (!stream->streaming || !stream->is_cfg || stream->stopping)
				continue;
			rkispp_frame_end(stream, FRAME_WORK);
		}
	}
end:
	return ret;
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

static bool is_en_done_early(struct rkispp_device *dev)
{
	u32 height = dev->ispp_sdev.out_fmt.height;
	u32 line = dev->stream_vdev.wait_line;
	bool en =  false;

	if (line) {
		if (line > height - 128)
			dev->stream_vdev.wait_line = height - 128;
		en = true;
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "wait %d line to wake up frame\n", line);
	}

	return en;
}

static void rkispp_tnr_complete(struct rkispp_device *dev, struct rkispp_tnr_inf *inf)
{
	struct rkispp_subdev *ispp_sdev = &dev->ispp_sdev;
	struct v4l2_event ev = {
		.type = RKISPP_V4L2_EVENT_TNR_COMPLETE,
	};
	struct rkispp_tnr_inf *tnr_inf;

	tnr_inf = (struct rkispp_tnr_inf *)ev.u.data;
	memcpy(tnr_inf, inf, sizeof(*tnr_inf));

	v4l2_subdev_notify_event(&ispp_sdev->sd, &ev);
}

static void tnr_free_buf(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkisp_ispp_buf *dbufs;
	struct list_head *list;
	int i;

	list = &vdev->tnr.list_rd;
	if (vdev->tnr.cur_rd) {
		list_add_tail(&vdev->tnr.cur_rd->list, list);
		if (vdev->tnr.nxt_rd == vdev->tnr.cur_rd)
			vdev->tnr.nxt_rd = NULL;
		vdev->tnr.cur_rd = NULL;
	}
	if (vdev->tnr.nxt_rd) {
		list_add_tail(&vdev->tnr.nxt_rd->list, list);
		vdev->tnr.nxt_rd = NULL;
	}
	while (!list_empty(list)) {
		dbufs = get_list_buf(list, true);
		v4l2_subdev_call(dev->ispp_sdev.remote_sd,
				 video, s_rx_buffer, dbufs, NULL);
	}

	list = &vdev->tnr.list_wr;
	if (vdev->tnr.cur_wr) {
		list_add_tail(&vdev->tnr.cur_wr->list, list);
		vdev->tnr.cur_wr = NULL;
	}
	while (!list_empty(list)) {
		dbufs = get_list_buf(list, true);
		kfree(dbufs);
	}
	list = &vdev->tnr.list_rpt;
	while (!list_empty(list)) {
		dbufs = get_list_buf(list, true);
		kfree(dbufs);
	}

	for (i = 0; i < sizeof(vdev->tnr.buf) /
	     sizeof(struct rkispp_dummy_buffer); i++)
		rkispp_free_buffer(dev, &vdev->tnr.buf.iir + i);

	vdev->tnr.is_but_init = false;
	vdev->tnr.is_trigger = false;
}

static int tnr_init_buf(struct rkispp_device *dev,
			u32 pic_size, u32 gain_size)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkisp_ispp_buf *dbufs;
	struct rkispp_dummy_buffer *buf;
	int i, j, ret, cnt = RKISPP_BUF_MAX;
	u32 buf_idx = 0;

	if (dev->inp == INP_ISP && dev->isp_mode & ISP_ISPP_QUICK)
		cnt = 1;
	for (i = 0; i < cnt; i++) {
		dbufs = kzalloc(sizeof(*dbufs), GFP_KERNEL);
		if (!dbufs) {
			ret = -ENOMEM;
			goto err;
		}
		dbufs->is_isp = false;
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			buf = &vdev->tnr.buf.wr[i][j];
			buf->is_need_dbuf = true;
			buf->is_need_dmafd = false;
			buf->is_need_vaddr = true;
			buf->size = !j ? pic_size : PAGE_ALIGN(gain_size);
			buf->index = buf_idx++;
			ret = rkispp_allow_buffer(dev, buf);
			if (ret) {
				kfree(dbufs);
				goto err;
			}
			dbufs->dbuf[j] = buf->dbuf;
			dbufs->didx[j] = buf->index;
		}
		list_add_tail(&dbufs->list, &vdev->tnr.list_wr);
	}

	if (dev->inp == INP_ISP && dev->isp_mode & ISP_ISPP_QUICK) {
		buf = &vdev->tnr.buf.iir;
		buf->size = pic_size;
		ret = rkispp_allow_buffer(dev, buf);
		if (ret < 0)
			goto err;
	}

	buf = &vdev->tnr.buf.gain_kg;
	buf->is_need_vaddr = true;
	buf->is_need_dbuf = true;
	buf->is_need_dmafd = false;
	buf->size = PAGE_ALIGN(gain_size * 4);
	buf->index = buf_idx++;
	ret = rkispp_allow_buffer(dev, buf);
	if (ret < 0)
		goto err;

	vdev->tnr.is_but_init = true;
	return 0;
err:
	tnr_free_buf(dev);
	v4l2_err(&dev->v4l2_dev, "%s failed\n", __func__);
	return ret;
}

static int config_tnr(struct rkispp_device *dev)
{
	struct rkispp_hw_dev *hw = dev->hw_dev;
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	int ret, mult = 1;
	u32 width, height, fmt;
	u32 pic_size, gain_size;
	u32 addr_offs, w, h, val;
	u32 max_w, max_h;

	vdev = &dev->stream_vdev;
	vdev->tnr.is_end = true;
	vdev->tnr.is_3to1 =
		((vdev->module_ens & ISPP_MODULE_TNR_3TO1) ==
		 ISPP_MODULE_TNR_3TO1);
	if (!(vdev->module_ens & ISPP_MODULE_TNR))
		return 0;

	if (dev->inp == INP_DDR) {
		vdev->tnr.is_3to1 = false;
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = dev->isp_mode & (FMT_YUV422 | FMT_FBC);
	}

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;
	max_w = hw->max_in.w ? hw->max_in.w : width;
	max_h = hw->max_in.h ? hw->max_in.h : height;
	w = (fmt & FMT_FBC) ? ALIGN(max_w, 16) : max_w;
	h = (fmt & FMT_FBC) ? ALIGN(max_h, 16) : max_h;
	addr_offs = (fmt & FMT_FBC) ? w * h >> 4 : w * h;
	pic_size = (fmt & FMT_YUV422) ? w * h * 2 : w * h * 3 >> 1;
	vdev->tnr.uv_offset = addr_offs;
	if (fmt & FMT_FBC)
		pic_size += w * h >> 4;

	gain_size = ALIGN(width, 64) * ALIGN(height, 128) >> 4;
	if (fmt & FMT_YUYV)
		mult = 2;

	if (vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)) {
		ret = tnr_init_buf(dev, pic_size, gain_size);
		if (ret)
			return ret;
		if (dev->inp == INP_ISP &&
		    dev->isp_mode & ISP_ISPP_QUICK) {
			rkispp_set_bits(dev, RKISPP_CTRL_QUICK,
					GLB_QUICK_MODE_MASK,
					GLB_QUICK_MODE(0));

			val = hw->pool[0].dma[GROUP_BUF_PIC];
			rkispp_write(dev, RKISPP_TNR_CUR_Y_BASE, val);
			rkispp_write(dev, RKISPP_TNR_CUR_UV_BASE, val + addr_offs);

			val = hw->pool[0].dma[GROUP_BUF_GAIN];
			rkispp_write(dev, RKISPP_TNR_GAIN_CUR_Y_BASE, val);

			if (vdev->tnr.is_3to1) {
				val = hw->pool[1].dma[GROUP_BUF_PIC];
				rkispp_write(dev, RKISPP_TNR_NXT_Y_BASE, val);
				rkispp_write(dev, RKISPP_TNR_NXT_UV_BASE, val + addr_offs);
				val = hw->pool[1].dma[GROUP_BUF_GAIN];
				rkispp_write(dev, RKISPP_TNR_GAIN_NXT_Y_BASE, val);
			}
		}

		val = vdev->tnr.buf.gain_kg.dma_addr;
		rkispp_write(dev, RKISPP_TNR_GAIN_KG_Y_BASE, val);

		val = vdev->tnr.buf.wr[0][GROUP_BUF_PIC].dma_addr;
		rkispp_write(dev, RKISPP_TNR_WR_Y_BASE, val);
		rkispp_write(dev, RKISPP_TNR_WR_UV_BASE, val + addr_offs);
		if (vdev->tnr.buf.iir.mem_priv)
			val = vdev->tnr.buf.iir.dma_addr;
		rkispp_write(dev, RKISPP_TNR_IIR_Y_BASE, val);
		rkispp_write(dev, RKISPP_TNR_IIR_UV_BASE, val + addr_offs);

		val = vdev->tnr.buf.wr[0][GROUP_BUF_GAIN].dma_addr;
		rkispp_write(dev, RKISPP_TNR_GAIN_WR_Y_BASE, val);

		rkispp_write(dev, RKISPP_TNR_WR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
		rkispp_set_bits(dev, RKISPP_TNR_CTRL, FMT_WR_MASK, fmt << 4 | SW_TNR_1ST_FRM);
	}

	if (stream) {
		stream->config->frame_end_id = TNR_INT;
		stream->config->reg.cur_y_base = RKISPP_TNR_CUR_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_TNR_CUR_UV_BASE;
		stream->config->reg.cur_y_base_shd = RKISPP_TNR_CUR_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_TNR_CUR_UV_BASE_SHD;
	}

	rkispp_set_bits(dev, RKISPP_TNR_CTRL, FMT_RD_MASK, fmt);
	if (fmt & FMT_FBC) {
		rkispp_write(dev, RKISPP_TNR_CUR_VIR_STRIDE, 0);
		rkispp_write(dev, RKISPP_TNR_IIR_VIR_STRIDE, 0);
		rkispp_write(dev, RKISPP_TNR_NXT_VIR_STRIDE, 0);
	} else {
		rkispp_write(dev, RKISPP_TNR_CUR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
		rkispp_write(dev, RKISPP_TNR_IIR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
		rkispp_write(dev, RKISPP_TNR_NXT_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
	}
	rkispp_set_bits(dev, RKISPP_TNR_CORE_CTRL, SW_TNR_MODE,
			vdev->tnr.is_3to1 ? SW_TNR_MODE : 0);
	rkispp_write(dev, RKISPP_TNR_GAIN_CUR_VIR_STRIDE, ALIGN(width, 64) >> 4);
	rkispp_write(dev, RKISPP_TNR_GAIN_NXT_VIR_STRIDE, ALIGN(width, 64) >> 4);
	rkispp_write(dev, RKISPP_TNR_GAIN_KG_VIR_STRIDE, ALIGN(width, 16) * 6);
	rkispp_write(dev, RKISPP_TNR_GAIN_WR_VIR_STRIDE, ALIGN(width, 64) >> 4);
	rkispp_write(dev, RKISPP_CTRL_TNR_SIZE, height << 16 | width);

	if (vdev->monitor.is_en) {
		init_completion(&vdev->monitor.tnr.cmpl);
		schedule_work(&vdev->monitor.tnr.work);
	}
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, width, height,
		 rkispp_read(dev, RKISPP_TNR_CTRL),
		 rkispp_read(dev, RKISPP_TNR_CORE_CTRL));
	return 0;
}

static void nr_free_buf(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkisp_ispp_buf *dbufs;
	struct list_head *list;
	int i;

	list = &vdev->nr.list_rd;
	if (vdev->nr.cur_rd) {
		list_add_tail(&vdev->nr.cur_rd->list, list);
		vdev->nr.cur_rd = NULL;
	}
	while (!list_empty(list)) {
		dbufs = get_list_buf(list, true);
		if (dbufs->is_isp)
			v4l2_subdev_call(dev->ispp_sdev.remote_sd,
					 video, s_rx_buffer, dbufs, NULL);
		else
			kfree(dbufs);
	}

	list = &vdev->nr.list_wr;
	if (vdev->nr.cur_wr)
		vdev->nr.cur_wr = NULL;
	while (!list_empty(list))
		get_list_buf(list, false);

	for (i = 0; i < sizeof(vdev->nr.buf) /
	     sizeof(struct rkispp_dummy_buffer); i++)
		rkispp_free_buffer(dev, &vdev->nr.buf.tmp_yuv + i);
}

static int nr_init_buf(struct rkispp_device *dev, u32 size)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_dummy_buffer *buf;
	int i, ret, cnt = 0;

	if (vdev->module_ens & ISPP_MODULE_FEC)
		cnt = vdev->is_done_early ? 1 : RKISPP_BUF_MAX;

	for (i = 0; i < cnt; i++) {
		buf = &vdev->nr.buf.wr[i];
		buf->size = size;
		ret = rkispp_allow_buffer(dev, buf);
		if (ret)
			goto err;
		list_add_tail(&buf->list, &vdev->nr.list_wr);
	}

	buf = &vdev->nr.buf.tmp_yuv;
	cnt = DIV_ROUND_UP(dev->ispp_sdev.out_fmt.width, 32);
	buf->size = PAGE_ALIGN(cnt * 42 * 32);
	ret = rkispp_allow_buffer(dev, buf);
	if (ret)
		goto err;
	return 0;
err:
	nr_free_buf(dev);
	v4l2_err(&dev->v4l2_dev, "%s failed\n", __func__);
	return ret;
}

static int config_nr_shp(struct rkispp_device *dev)
{
	struct rkispp_hw_dev *hw = dev->hw_dev;
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	u32 width, height, fmt;
	u32 pic_size, addr_offs;
	u32 w, h, val;
	u32 max_w, max_h;
	int ret, mult = 1;

	vdev = &dev->stream_vdev;
	vdev->nr.is_end = true;
	if (!(vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)))
		return 0;

	vdev->is_done_early = is_en_done_early(dev);

	if (dev->inp == INP_DDR) {
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = dev->isp_mode & (FMT_YUV422 | FMT_FBC);
	}

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;
	w = width;
	h = height;
	max_w = hw->max_in.w ? hw->max_in.w : w;
	max_h = hw->max_in.h ? hw->max_in.h : h;
	if (fmt & FMT_FBC) {
		max_w = ALIGN(max_w, 16);
		max_h = ALIGN(max_h, 16);
		w = ALIGN(w, 16);
		h = ALIGN(h, 16);
	}
	addr_offs = (fmt & FMT_FBC) ? max_w * max_h >> 4 : max_w * max_h;
	pic_size = (fmt & FMT_YUV422) ? w * h * 2 : w * h * 3 >> 1;
	vdev->nr.uv_offset = addr_offs;

	if (fmt & FMT_YUYV)
		mult = 2;

	ret = nr_init_buf(dev, pic_size);
	if (ret)
		return ret;

	if (vdev->module_ens & ISPP_MODULE_TNR) {
		rkispp_write(dev, RKISPP_NR_ADDR_BASE_Y,
			     rkispp_read(dev, RKISPP_TNR_WR_Y_BASE));
		rkispp_write(dev, RKISPP_NR_ADDR_BASE_UV,
			     rkispp_read(dev, RKISPP_TNR_WR_UV_BASE));
		rkispp_write(dev, RKISPP_NR_ADDR_BASE_GAIN,
			     rkispp_read(dev, RKISPP_TNR_GAIN_WR_Y_BASE));
		rkispp_set_bits(dev, RKISPP_CTRL_QUICK, 0, GLB_NR_SD32_TNR);
	} else {
		/* tnr need to set same format with nr in the fbc mode */
		rkispp_set_bits(dev, RKISPP_TNR_CTRL, FMT_RD_MASK, fmt);
		rkispp_write(dev, RKISPP_CTRL_TNR_SIZE, height << 16 | width);
		if (dev->inp == INP_ISP) {
			if (dev->isp_mode & ISP_ISPP_QUICK)
				rkispp_set_bits(dev, RKISPP_CTRL_QUICK,
						GLB_QUICK_MODE_MASK,
						GLB_QUICK_MODE(2));
			else
				rkispp_set_bits(dev, RKISPP_NR_UVNR_CTRL_PARA,
						0, SW_UVNR_SD32_SELF_EN);

			val = hw->pool[0].dma[GROUP_BUF_PIC];
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_Y, val);
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_UV, val + addr_offs);
			val = hw->pool[0].dma[GROUP_BUF_GAIN];
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_GAIN, val);
			rkispp_clear_bits(dev, RKISPP_CTRL_QUICK, GLB_NR_SD32_TNR);
		} else if (stream) {
			stream->config->frame_end_id = NR_INT;
			stream->config->reg.cur_y_base = RKISPP_NR_ADDR_BASE_Y;
			stream->config->reg.cur_uv_base = RKISPP_NR_ADDR_BASE_UV;
			stream->config->reg.cur_y_base_shd = RKISPP_NR_ADDR_BASE_Y_SHD;
			stream->config->reg.cur_uv_base_shd = RKISPP_NR_ADDR_BASE_UV_SHD;
		}
	}

	rkispp_clear_bits(dev, RKISPP_CTRL_QUICK, GLB_FEC2SCL_EN);
	if (vdev->module_ens & ISPP_MODULE_FEC) {
		addr_offs = width * height;
		vdev->fec.uv_offset = addr_offs;
		val = vdev->nr.buf.wr[0].dma_addr;
		rkispp_write(dev, RKISPP_SHARP_WR_Y_BASE, val);
		rkispp_write(dev, RKISPP_SHARP_WR_UV_BASE, val + addr_offs);
		rkispp_write(dev, RKISPP_SHARP_WR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
		rkispp_set_bits(dev, RKISPP_SHARP_CTRL, SW_SHP_WR_FORMAT_MASK, fmt & (~FMT_FBC));

		rkispp_write(dev, RKISPP_FEC_RD_Y_BASE, val);
		rkispp_write(dev, RKISPP_FEC_RD_UV_BASE, val + addr_offs);
	} else {
		stream = &vdev->stream[STREAM_MB];
		if (!stream->streaming) {
			val = hw->dummy_buf.dma_addr;
			rkispp_write(dev, RKISPP_SHARP_WR_Y_BASE, val);
			rkispp_write(dev, RKISPP_SHARP_WR_UV_BASE, val);
			rkispp_write(dev, RKISPP_SHARP_WR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
			if (dev->inp == INP_ISP)
				rkispp_set_bits(dev, RKISPP_SHARP_CTRL,
						SW_SHP_WR_FORMAT_MASK, FMT_FBC);
		}
	}

	val = vdev->nr.buf.tmp_yuv.dma_addr;
	rkispp_write(dev, RKISPP_SHARP_TMP_YUV_BASE, val);

	/* fix to use new nr algorithm */
	rkispp_set_bits(dev, RKISPP_NR_CTRL, NR_NEW_ALGO, NR_NEW_ALGO);
	rkispp_set_bits(dev, RKISPP_NR_CTRL, FMT_RD_MASK, fmt);
	if (fmt & FMT_FBC) {
		rkispp_write(dev, RKISPP_NR_VIR_STRIDE, 0);
		rkispp_write(dev, RKISPP_FBC_VIR_HEIGHT, max_h);
	} else {
		rkispp_write(dev, RKISPP_NR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
	}
	rkispp_write(dev, RKISPP_NR_VIR_STRIDE_GAIN, ALIGN(width, 64) >> 4);
	rkispp_write(dev, RKISPP_CTRL_SIZE, height << 16 | width);

	if (vdev->monitor.is_en) {
		init_completion(&vdev->monitor.nr.cmpl);
		schedule_work(&vdev->monitor.nr.work);
	}
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d\n"
		 "nr ctrl:0x%x ctrl_para:0x%x\n"
		 "shp ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, width, height,
		 rkispp_read(dev, RKISPP_NR_CTRL),
		 rkispp_read(dev, RKISPP_NR_UVNR_CTRL_PARA),
		 rkispp_read(dev, RKISPP_SHARP_CTRL),
		 rkispp_read(dev, RKISPP_SHARP_CORE_CTRL));
	return 0;
}

static void fec_free_buf(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct list_head *list = &vdev->fec.list_rd;

	if (vdev->fec.cur_rd)
		vdev->fec.cur_rd = NULL;
	while (!list_empty(list))
		get_list_buf(list, false);
}

static int config_fec(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	u32 width, height, fmt, mult = 1;

	vdev = &dev->stream_vdev;
	vdev->fec.is_end = true;
	if (!(vdev->module_ens & ISPP_MODULE_FEC))
		return 0;

	if (dev->inp == INP_DDR) {
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = dev->isp_mode & FMT_YUV422;
	}

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;

	if (vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)) {
		rkispp_write(dev, RKISPP_FEC_RD_Y_BASE,
			     rkispp_read(dev, RKISPP_SHARP_WR_Y_BASE));
		rkispp_write(dev, RKISPP_FEC_RD_UV_BASE,
			     rkispp_read(dev, RKISPP_SHARP_WR_UV_BASE));
	} else if (stream) {
		stream->config->frame_end_id = FEC_INT;
		stream->config->reg.cur_y_base = RKISPP_FEC_RD_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_FEC_RD_UV_BASE;
		stream->config->reg.cur_y_base_shd = RKISPP_FEC_RD_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_FEC_RD_UV_BASE_SHD;
	}

	if (fmt & FMT_YUYV)
		mult = 2;
	rkispp_set_bits(dev, RKISPP_FEC_CTRL, FMT_RD_MASK, fmt);
	rkispp_write(dev, RKISPP_FEC_RD_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
	rkispp_write(dev, RKISPP_FEC_DST_SIZE, height << 16 | width);
	rkispp_set_bits(dev, RKISPP_CTRL_QUICK, 0, GLB_FEC2SCL_EN);

	if (vdev->monitor.is_en) {
		init_completion(&vdev->monitor.fec.cmpl);
		schedule_work(&vdev->monitor.fec.work);
	}
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, width, height,
		 rkispp_read(dev, RKISPP_FEC_CTRL),
		 rkispp_read(dev, RKISPP_FEC_CORE_CTRL));
	return 0;
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
	dev->stream_vdev.monitor.is_en = rkispp_monitor;
	init_completion(&dev->stream_vdev.monitor.cmpl);

	ret = config_tnr(dev);
	if (ret < 0)
		return ret;

	ret = config_nr_shp(dev);
	if (ret < 0)
		goto free_tnr;

	ret = config_fec(dev);
	if (ret < 0)
		goto free_nr;

	/* config default params */
	dev->params_vdev.params_ops->rkispp_params_cfg(&dev->params_vdev, 0);
	return 0;
free_nr:
	nr_free_buf(dev);
free_tnr:
	tnr_free_buf(dev);
	return ret;
}

static void rkispp_destroy_buf(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;

	if (atomic_read(&vdev->refcnt) == 1) {
		vdev->irq_ends = 0;
		tnr_free_buf(dev);
		nr_free_buf(dev);
		fec_free_buf(dev);
		rkispp_event_handle(dev, CMD_FREE_POOL, NULL);
	}
}


static void nr_work_event(struct rkispp_device *dev,
			  struct rkisp_ispp_buf *buf_rd,
			  struct rkispp_dummy_buffer *buf_wr,
			  bool is_isr)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream = &vdev->stream[STREAM_II];
	struct rkispp_monitor *monitor = &vdev->monitor;
	void __iomem *base = dev->hw_dev->base_addr;
	struct rkispp_dummy_buffer *buf_to_fec = NULL;
	struct rkispp_dummy_buffer *dummy;
	struct rkispp_buffer *inbuf;
	struct v4l2_subdev *sd = NULL;
	struct list_head *list;
	struct dma_buf *dbuf;
	unsigned long lock_flags = 0, lock_flags1 = 0;
	bool is_start = false, is_quick = false;
	bool is_fec_en = (vdev->module_ens & ISPP_MODULE_FEC);
	struct rkisp_ispp_reg *reg_buf = NULL;
	u32 val;

	if (!(vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)))
		return;

	if (dev->inp == INP_ISP) {
		if (dev->isp_mode & ISP_ISPP_QUICK)
			is_quick = true;
		else
			sd = dev->ispp_sdev.remote_sd;
	}

	spin_lock_irqsave(&vdev->nr.buf_lock, lock_flags);

	/* event from nr frame end */
	if (!buf_rd && !buf_wr && is_isr) {
		vdev->nr.is_end = true;

		if (vdev->nr.cur_rd) {
			/* nr read buf return to isp or tnr */
			if (vdev->nr.cur_rd->is_isp && sd) {
				v4l2_subdev_call(sd, video, s_rx_buffer, vdev->nr.cur_rd, NULL);
			} else if (!vdev->nr.cur_rd->priv) {
				rkispp_module_work_event(dev, NULL, vdev->nr.cur_rd,
							 ISPP_MODULE_TNR, is_isr);
			} else if (stream->streaming && vdev->nr.cur_rd->priv) {
				inbuf = vdev->nr.cur_rd->priv;
				vb2_buffer_done(&inbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			}
			vdev->nr.cur_rd = NULL;
		}

		if (vdev->nr.cur_wr) {
			/* nr write buf to fec */
			buf_to_fec = vdev->nr.cur_wr;
			vdev->nr.cur_wr = NULL;

			if (vdev->is_done_early && !dev->hw_dev->is_first)
				buf_to_fec = NULL;
		}
	}

	if (!vdev->fec.is_end) {
		if (buf_rd)
			list_add_tail(&buf_rd->list, &vdev->nr.list_rd);
		goto end;
	}

	spin_lock_irqsave(&monitor->lock, lock_flags1);
	if (monitor->is_restart) {
		if (buf_rd)
			list_add_tail(&buf_rd->list, &vdev->nr.list_rd);
		if (buf_wr)
			list_add_tail(&buf_wr->list, &vdev->nr.list_wr);
		goto restart_unlock;
	}

	list = &vdev->nr.list_rd;
	if (buf_rd && vdev->nr.is_end && list_empty(list)) {
		/* nr read buf from isp or tnr */
		vdev->nr.cur_rd = buf_rd;
	} else if (vdev->nr.is_end && !list_empty(list)) {
		/* nr read buf from list
		 * nr processing slow than isp or tnr
		 * new read buf from isp or tnr into list
		 */
		vdev->nr.cur_rd = get_list_buf(list, true);
		if (buf_rd)
			list_add_tail(&buf_rd->list, list);
	} else if (!vdev->nr.is_end && buf_rd) {
		/* nr no idle
		 * new read buf from isp or tnr into list
		 */
		list_add_tail(&buf_rd->list, list);
	}

	list = &vdev->nr.list_wr;
	if (vdev->nr.is_end && !vdev->nr.cur_wr) {
		/* nr idle, get new write buf */
		vdev->nr.cur_wr = buf_wr ? buf_wr :
				get_list_buf(list, false);
	} else if (buf_wr) {
		/* tnr no idle, write buf from nr into list */
		list_add_tail(&buf_wr->list, list);
	}

	if (vdev->nr.cur_rd && vdev->nr.is_end) {
		if (vdev->nr.cur_rd->priv) {
			inbuf = vdev->nr.cur_rd->priv;
			val = inbuf->buff_addr[RKISPP_PLANE_Y];
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_Y, val);
			val = inbuf->buff_addr[RKISPP_PLANE_UV];
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_UV, val);
		} else if (!vdev->nr.cur_rd->is_isp) {
			u32 size = sizeof(vdev->tnr.buf) / sizeof(*dummy);

			dbuf = vdev->nr.cur_rd->dbuf[GROUP_BUF_PIC];
			dummy = dbuf_to_dummy(dbuf, &vdev->tnr.buf.iir, size);
			val = dummy->dma_addr;
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_Y, val);
			val += vdev->nr.uv_offset;
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_UV, val);

			dbuf = vdev->nr.cur_rd->dbuf[GROUP_BUF_GAIN];
			dummy = dbuf_to_dummy(dbuf, &vdev->tnr.buf.iir, size);
			val = dummy->dma_addr;
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_GAIN, val);
		} else {
			struct rkispp_isp_buf_pool *buf;

			buf = get_pool_buf(dev, vdev->nr.cur_rd);
			val = buf->dma[GROUP_BUF_PIC];
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_Y, val);
			val += vdev->nr.uv_offset;
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_UV, val);

			val = buf->dma[GROUP_BUF_GAIN];
			rkispp_write(dev, RKISPP_NR_ADDR_BASE_GAIN, val);
		}
		is_start = true;
	}

	if (vdev->nr.is_end && is_quick)
		is_start = true;

	if (vdev->nr.cur_wr && is_start) {
		dummy = vdev->nr.cur_wr;
		val = dummy->dma_addr;
		rkispp_write(dev, RKISPP_SHARP_WR_Y_BASE, val);
		val += vdev->fec.uv_offset;
		rkispp_write(dev, RKISPP_SHARP_WR_UV_BASE, val);
	}

	if (is_start) {
		u32 seq = 0;
		u64 timestamp = 0;

		if (vdev->nr.cur_rd) {
			seq = vdev->nr.cur_rd->frame_id;
			timestamp = vdev->nr.cur_rd->frame_timestamp;
			if (vdev->nr.cur_wr) {
				vdev->nr.cur_wr->id = seq;
				vdev->nr.cur_wr->timestamp = timestamp;
			} else {
				vdev->nr.buf.wr[0].id = seq;
				vdev->nr.buf.wr[0].timestamp = timestamp;
			}
			if (!is_fec_en && !is_quick) {
				dev->ispp_sdev.frame_timestamp = timestamp;
				dev->ispp_sdev.frm_sync_seq = seq;
			}
		}

		/* check MB config and output buf beforce start, when MB connect to SHARP
		 * MB update by OTHER_FORCE_UPD
		 */
		stream = &vdev->stream[STREAM_MB];
		if (!is_fec_en && stream->streaming) {
			if (!stream->is_cfg) {
				secure_config_mb(stream);
			} else if (!stream->curr_buf) {
				get_stream_buf(stream);
				if (stream->curr_buf)
					vdev->stream_ops->update_mi(stream);
			}
		}

		/* check SCL output buf beforce start
		 * SCL update by OTHER_FORCE_UPD
		 */
		for (val = STREAM_S0; val <= STREAM_S2; val++) {
			stream = &vdev->stream[val];
			if (!stream->streaming || !stream->is_cfg || stream->curr_buf)
				continue;
			get_stream_buf(stream);
			if (stream->curr_buf) {
				vdev->stream_ops->update_mi(stream);
				rkispp_set_bits(dev, stream->config->reg.ctrl, 0, SW_SCL_ENABLE);
			} else {
				rkispp_clear_bits(dev, stream->config->reg.ctrl, SW_SCL_ENABLE);
			}
		}

		if (!dev->hw_dev->is_single) {
			if (vdev->nr.cur_rd &&
			    (vdev->nr.cur_rd->is_isp || vdev->nr.cur_rd->priv)) {
				rkispp_update_regs(dev, RKISPP_CTRL, RKISPP_TNR_CTRL);
				writel(TNR_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
			}
			rkispp_update_regs(dev, RKISPP_NR, RKISPP_ORB_MAX_FEATURE);
		}

		writel(OTHER_FORCE_UPD, base + RKISPP_CTRL_UPDATE);

		val = readl(base + RKISPP_SHARP_CORE_CTRL);
		if (!(val & SW_SHP_EN) && !is_fec_en && !stream->streaming)
			writel(val | SW_SHP_DMA_DIS, base + RKISPP_SHARP_CORE_CTRL);
		else if (val & SW_SHP_EN)
			writel(val & ~SW_SHP_DMA_DIS, base + RKISPP_SHARP_CORE_CTRL);

		v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
			 "NR start seq:%d | Y_SHD rd:0x%x wr:0x%x\n",
			 seq, readl(base + RKISPP_NR_ADDR_BASE_Y_SHD),
			 readl(base + RKISPP_SHARP_WR_Y_BASE_SHD));

		for (val = STREAM_S0; val <= STREAM_S2 && !is_fec_en; val++) {
			stream = &vdev->stream[val];
			/* check scale stream stop state */
			if (stream->streaming && stream->stopping) {
				if (stream->ops->is_stopped(stream)) {
					stream->stopping = false;
					stream->streaming = false;
					wake_up(&stream->done);
				} else {
					stream->ops->stop(stream);
				}
			}
		}

		vdev->nr.dbg.id = seq;
		vdev->nr.dbg.timestamp = ktime_get_ns();
		if (monitor->is_en) {
			monitor->nr.time = vdev->nr.dbg.interval / 1000 / 1000;
			monitor->monitoring_module |= MONITOR_NR;
			monitor->nr.is_err = false;
			if (!completion_done(&monitor->nr.cmpl))
				complete(&monitor->nr.cmpl);
		}

		if (rkispp_is_reg_withstream_global())
			rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id, seq);
		if (reg_buf && (rkispp_debug_reg & ISPP_MODULE_NR)) {
			u32 offset, size;

			offset = reg_buf->reg_size;
			size = 4 + RKISPP_NR_BUFFER_READY - RKISPP_NR_CTRL;
			reg_buf->ispp_size[ISPP_ID_NR] = size;
			reg_buf->ispp_offset[ISPP_ID_NR] = offset;
			memcpy_fromio(&reg_buf->reg[offset], base + RKISPP_NR_CTRL, size);

			offset += size;
			reg_buf->reg_size = offset;
		}
		if (reg_buf && (rkispp_debug_reg & ISPP_MODULE_SHP)) {
			u32 offset, size;

			offset = reg_buf->reg_size;
			size = 4 + RKISPP_SHARP_GRAD_RATIO - RKISPP_SHARP_CTRL;
			reg_buf->ispp_size[ISPP_ID_SHP] = size;
			reg_buf->ispp_offset[ISPP_ID_SHP] = offset;
			memcpy_fromio(&reg_buf->reg[offset], base + RKISPP_SHARP_CTRL, size);

			offset += size;
			reg_buf->reg_size = offset;
		}
		if (reg_buf && (rkispp_debug_reg & ISPP_MODULE_ORB)) {
			u32 offset, size;

			offset = reg_buf->reg_size;
			size = 4 + RKISPP_ORB_MAX_FEATURE - RKISPP_ORB_WR_BASE;
			reg_buf->ispp_size[ISPP_ID_ORB] = size;
			reg_buf->ispp_offset[ISPP_ID_ORB] = offset;
			memcpy_fromio(&reg_buf->reg[offset], base + RKISPP_ORB_WR_BASE, size);

			offset += size;
			reg_buf->reg_size = offset;
		}

		if (!is_quick && !dev->hw_dev->is_shutdown) {
			writel(NR_SHP_ST, base + RKISPP_CTRL_STRT);

			if (!is_fec_en && vdev->is_done_early)
				hrtimer_start(&vdev->frame_qst,
					      ns_to_ktime(1000000),
					      HRTIMER_MODE_REL);
		}
		vdev->nr.is_end = false;
	}
restart_unlock:
	spin_unlock_irqrestore(&monitor->lock, lock_flags1);
end:
	/* nr_shp->fec->scl
	 * fec start working should after nr
	 * for scl will update by OTHER_FORCE_UPD
	 */
	if (buf_to_fec)
		rkispp_module_work_event(dev, buf_to_fec, NULL,
					 ISPP_MODULE_FEC, is_isr);
	spin_unlock_irqrestore(&vdev->nr.buf_lock, lock_flags);

	if (is_fec_en && vdev->is_done_early &&
	    is_start && !dev->hw_dev->is_first)
		hrtimer_start(&vdev->fec_qst,
			      ns_to_ktime(1000000),
			      HRTIMER_MODE_REL);
}

static void tnr_work_event(struct rkispp_device *dev,
			   struct rkisp_ispp_buf *buf_rd,
			   struct rkisp_ispp_buf *buf_wr,
			   bool is_isr)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream = &vdev->stream[STREAM_II];
	struct rkispp_monitor *monitor = &vdev->monitor;
	void __iomem *base = dev->hw_dev->base_addr;
	struct rkispp_dummy_buffer *dummy;
	struct rkispp_buffer *inbuf;
	struct v4l2_subdev *sd = NULL;
	struct list_head *list;
	struct dma_buf *dbuf;
	unsigned long lock_flags = 0, lock_flags1 = 0;
	u32 val, size = sizeof(vdev->tnr.buf) / sizeof(*dummy);
	bool is_3to1 = vdev->tnr.is_3to1, is_start = false;
	bool is_en = rkispp_read(dev, RKISPP_TNR_CORE_CTRL) & SW_TNR_EN;
	struct rkisp_ispp_reg *reg_buf = NULL;

	if (!(vdev->module_ens & ISPP_MODULE_TNR) ||
	    (dev->inp == INP_ISP && dev->isp_mode & ISP_ISPP_QUICK))
		return;

	if (dev->inp == INP_ISP)
		sd = dev->ispp_sdev.remote_sd;

	spin_lock_irqsave(&vdev->tnr.buf_lock, lock_flags);

	/* event from tnr frame end */
	if (!buf_rd && !buf_wr && is_isr) {
		vdev->tnr.is_end = true;

		if (vdev->tnr.cur_rd) {
			/* tnr read buf return to isp */
			if (sd) {
				v4l2_subdev_call(sd, video, s_rx_buffer, vdev->tnr.cur_rd, NULL);
			} else if (stream->streaming && vdev->tnr.cur_rd->priv) {
				inbuf = vdev->tnr.cur_rd->priv;
				vb2_buffer_done(&inbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			}
			if (vdev->tnr.cur_rd == vdev->tnr.nxt_rd)
				vdev->tnr.nxt_rd = NULL;
			vdev->tnr.cur_rd = NULL;
		}

		if (vdev->tnr.cur_wr) {
			struct rkispp_tnr_inf tnr_inf;

			if (!vdev->tnr.cur_wr->is_move_judge || !vdev->tnr.is_trigger) {
				/* tnr write buf to nr */
				rkispp_module_work_event(dev, vdev->tnr.cur_wr, NULL,
							 ISPP_MODULE_NR, is_isr);
			} else {
				tnr_inf.dev_id = dev->dev_id;
				tnr_inf.frame_id = vdev->tnr.cur_wr->frame_id;
				tnr_inf.gainkg_idx = vdev->tnr.buf.gain_kg.index;
				tnr_inf.gainwr_idx = vdev->tnr.cur_wr->didx[GROUP_BUF_GAIN];
				tnr_inf.gainkg_size = vdev->tnr.buf.gain_kg.size;
				dbuf = vdev->tnr.cur_wr->dbuf[GROUP_BUF_GAIN];
				dummy = dbuf_to_dummy(dbuf, &vdev->tnr.buf.iir, size);
				tnr_inf.gainwr_size = dummy->size;
				rkispp_finish_buffer(dev, dummy);
				rkispp_finish_buffer(dev, &vdev->tnr.buf.gain_kg);
				rkispp_tnr_complete(dev, &tnr_inf);
				list_add_tail(&vdev->tnr.cur_wr->list, &vdev->tnr.list_rpt);
			}
			vdev->tnr.cur_wr = NULL;
		}
	}

	if (!is_en) {
		if (buf_wr)
			list_add_tail(&buf_wr->list, &vdev->tnr.list_wr);

		if (vdev->tnr.nxt_rd) {
			if (sd) {
				v4l2_subdev_call(sd, video, s_rx_buffer,
						 vdev->tnr.nxt_rd, NULL);
			} else if (stream->streaming && vdev->tnr.nxt_rd->priv) {
				inbuf = vdev->tnr.nxt_rd->priv;
				vb2_buffer_done(&inbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			}
			vdev->tnr.nxt_rd = NULL;
		}
		list = &vdev->tnr.list_rd;
		while (!list_empty(list)) {
			struct rkisp_ispp_buf *buf = get_list_buf(list, true);

			rkispp_module_work_event(dev, buf, NULL,
						 ISPP_MODULE_NR, is_isr);
		}
		if (buf_rd)
			rkispp_module_work_event(dev, buf_rd, NULL,
						 ISPP_MODULE_NR, is_isr);
		goto end;
	}

	spin_lock_irqsave(&monitor->lock, lock_flags1);
	if (monitor->is_restart) {
		if (buf_rd)
			list_add_tail(&buf_rd->list, &vdev->tnr.list_rd);
		if (buf_wr)
			list_add_tail(&buf_wr->list, &vdev->tnr.list_wr);
		goto restart_unlock;
	}

	list = &vdev->tnr.list_rd;
	if (buf_rd && vdev->tnr.is_end && list_empty(list)) {
		/* tnr read buf from isp */
		vdev->tnr.cur_rd = vdev->tnr.nxt_rd;
		vdev->tnr.nxt_rd = buf_rd;
		/* first buf for 3to1 using twice */
		if (!is_3to1 ||
		    (rkispp_read(dev, RKISPP_TNR_CTRL) & SW_TNR_1ST_FRM))
			vdev->tnr.cur_rd = vdev->tnr.nxt_rd;
	} else if (vdev->tnr.is_end && !list_empty(list)) {
		/* tnr read buf from list
		 * tnr processing slow than isp
		 * new read buf from isp into list
		 */
		vdev->tnr.cur_rd = vdev->tnr.nxt_rd;
		vdev->tnr.nxt_rd = get_list_buf(list, true);
		if (!is_3to1)
			vdev->tnr.cur_rd = vdev->tnr.nxt_rd;

		if (buf_rd)
			list_add_tail(&buf_rd->list, list);
	} else if (!vdev->tnr.is_end && buf_rd) {
		/* tnr no idle
		 * new read buf from isp into list
		 */
		list_add_tail(&buf_rd->list, list);
	}

	list = &vdev->tnr.list_wr;
	if (vdev->tnr.is_end && !vdev->tnr.cur_wr) {
		/* tnr idle, get new write buf */
		vdev->tnr.cur_wr =
			buf_wr ? buf_wr : get_list_buf(list, true);
	} else if (buf_wr) {
		/* tnr no idle, write buf from nr into list */
		list_add_tail(&buf_wr->list, list);
	}

	if (vdev->tnr.cur_rd && vdev->tnr.nxt_rd && vdev->tnr.is_end) {
		if (vdev->tnr.cur_rd->priv) {
			inbuf = vdev->tnr.cur_rd->priv;
			val = inbuf->buff_addr[RKISPP_PLANE_Y];
			rkispp_write(dev, RKISPP_TNR_CUR_Y_BASE, val);
			val = inbuf->buff_addr[RKISPP_PLANE_UV];
			rkispp_write(dev, RKISPP_TNR_CUR_UV_BASE, val);
		} else {
			struct rkispp_isp_buf_pool *buf;

			buf = get_pool_buf(dev, vdev->tnr.cur_rd);
			val = buf->dma[GROUP_BUF_PIC];
			rkispp_write(dev, RKISPP_TNR_CUR_Y_BASE, val);
			val += vdev->tnr.uv_offset;
			rkispp_write(dev, RKISPP_TNR_CUR_UV_BASE, val);

			val = buf->dma[GROUP_BUF_GAIN];
			rkispp_write(dev, RKISPP_TNR_GAIN_CUR_Y_BASE, val);
			if (is_3to1) {
				buf = get_pool_buf(dev, vdev->tnr.nxt_rd);
				val = buf->dma[GROUP_BUF_PIC];
				rkispp_write(dev, RKISPP_TNR_NXT_Y_BASE, val);
				val += vdev->tnr.uv_offset;
				rkispp_write(dev, RKISPP_TNR_NXT_UV_BASE, val);

				val = buf->dma[GROUP_BUF_GAIN];
				rkispp_write(dev, RKISPP_TNR_GAIN_NXT_Y_BASE, val);

				if (rkispp_read(dev, RKISPP_TNR_CTRL) & SW_TNR_1ST_FRM)
					vdev->tnr.cur_rd = NULL;
			}
		}
		is_start = true;
	}

	if (vdev->tnr.cur_wr && is_start) {
		dbuf = vdev->tnr.cur_wr->dbuf[GROUP_BUF_PIC];
		dummy = dbuf_to_dummy(dbuf, &vdev->tnr.buf.iir, size);
		val = dummy->dma_addr;
		rkispp_write(dev, RKISPP_TNR_WR_Y_BASE, val);
		val += vdev->tnr.uv_offset;
		rkispp_write(dev, RKISPP_TNR_WR_UV_BASE, val);

		dbuf = vdev->tnr.cur_wr->dbuf[GROUP_BUF_GAIN];
		dummy = dbuf_to_dummy(dbuf, &vdev->tnr.buf.iir, size);
		val = dummy->dma_addr;
		rkispp_write(dev, RKISPP_TNR_GAIN_WR_Y_BASE, val);
	}

	if (is_start) {
		u32 seq = 0;

		if (vdev->tnr.nxt_rd) {
			seq = vdev->tnr.nxt_rd->frame_id;
			if (vdev->tnr.cur_wr) {
				vdev->tnr.cur_wr->frame_id = seq;
				vdev->tnr.cur_wr->frame_timestamp =
					vdev->tnr.nxt_rd->frame_timestamp;
				vdev->tnr.cur_wr->is_move_judge =
					vdev->tnr.nxt_rd->is_move_judge;
			}
		}

		if (!dev->hw_dev->is_single)
			rkispp_update_regs(dev, RKISPP_CTRL, RKISPP_TNR_CORE_WEIGHT);
		writel(TNR_FORCE_UPD, base + RKISPP_CTRL_UPDATE);

		v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
			 "TNR start seq:%d | Y_SHD nxt:0x%x cur:0x%x iir:0x%x wr:0x%x\n",
			 seq, readl(base + RKISPP_TNR_NXT_Y_BASE_SHD),
			 readl(base + RKISPP_TNR_CUR_Y_BASE_SHD),
			 readl(base + RKISPP_TNR_IIR_Y_BASE_SHD),
			 readl(base + RKISPP_TNR_WR_Y_BASE_SHD));

		/* iir using previous tnr write frame */
		rkispp_write(dev, RKISPP_TNR_IIR_Y_BASE,
			     rkispp_read(dev, RKISPP_TNR_WR_Y_BASE));
		rkispp_write(dev, RKISPP_TNR_IIR_UV_BASE,
			     rkispp_read(dev, RKISPP_TNR_WR_UV_BASE));

		rkispp_prepare_buffer(dev, &vdev->tnr.buf.gain_kg);

		vdev->tnr.dbg.id = seq;
		vdev->tnr.dbg.timestamp = ktime_get_ns();
		if (monitor->is_en) {
			monitor->tnr.time = vdev->tnr.dbg.interval / 1000 / 1000;
			monitor->monitoring_module |= MONITOR_TNR;
			monitor->tnr.is_err = false;
			if (!completion_done(&monitor->tnr.cmpl))
				complete(&monitor->tnr.cmpl);
		}

		if (rkispp_is_reg_withstream_global())
			rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id, seq);
		if (reg_buf && (rkispp_debug_reg & ISPP_MODULE_TNR)) {
			u32 offset, size;

			offset = reg_buf->reg_size;
			size = 4 + RKISPP_TNR_STATE - RKISPP_TNR_CTRL;
			reg_buf->ispp_size[ISPP_ID_TNR] = size;
			reg_buf->ispp_offset[ISPP_ID_TNR] = offset;
			memcpy_fromio(&reg_buf->reg[offset], base + RKISPP_TNR_CTRL, size);

			offset += size;
			reg_buf->reg_size = offset;
		}

		if (!dev->hw_dev->is_shutdown)
			writel(TNR_ST, base + RKISPP_CTRL_STRT);
		vdev->tnr.is_end = false;
	}

restart_unlock:
	spin_unlock_irqrestore(&monitor->lock, lock_flags1);
end:
	spin_unlock_irqrestore(&vdev->tnr.buf_lock, lock_flags);
}

static void fec_work_event(struct rkispp_device *dev,
			   void *buff_rd,
			   bool is_isr, bool is_quick)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_monitor *monitor = &vdev->monitor;
	struct list_head *list = &vdev->fec.list_rd;
	void __iomem *base = dev->hw_dev->base_addr;
	struct rkispp_dummy_buffer *dummy;
	struct rkispp_stream *stream;
	unsigned long lock_flags = 0, lock_flags1 = 0;
	bool is_start = false;
	struct rkisp_ispp_reg *reg_buf = NULL;
	u32 val;
	struct rkispp_dummy_buffer *buf_rd = buff_rd;

	if (!(vdev->module_ens & ISPP_MODULE_FEC))
		return;

	spin_lock_irqsave(&vdev->fec.buf_lock, lock_flags);

	/* event from fec frame end */
	if (!buf_rd && is_isr) {
		vdev->fec.is_end = true;

		if (vdev->fec.dummy_cur_rd || vdev->is_done_early)
			rkispp_module_work_event(dev, NULL, vdev->fec.dummy_cur_rd,
						 ISPP_MODULE_NR, false);
		vdev->fec.dummy_cur_rd = NULL;
	}

	spin_lock_irqsave(&monitor->lock, lock_flags1);
	if (monitor->is_restart && buf_rd) {
		list_add_tail(&buf_rd->list, list);
		goto restart_unlock;
	}

	if (buf_rd && vdev->fec.is_end && list_empty(list)) {
		/* fec read buf from nr */
		vdev->fec.dummy_cur_rd = buf_rd;
	} else if (vdev->fec.is_end && !list_empty(list)) {
		/* fec read buf from list
		 * fec processing slow than nr
		 * new read buf from nr into list
		 */
		vdev->fec.dummy_cur_rd = get_list_buf(list, false);
		if (buf_rd)
			list_add_tail(&buf_rd->list, list);
	} else if (!vdev->fec.is_end && buf_rd) {
		/* fec no idle
		 * new read buf from nr into list
		 */
		list_add_tail(&buf_rd->list, list);
	}

	if (vdev->fec.dummy_cur_rd && vdev->fec.is_end) {
		dummy = vdev->fec.dummy_cur_rd;
		val = dummy->dma_addr;
		rkispp_write(dev, RKISPP_FEC_RD_Y_BASE, val);
		val += vdev->fec.uv_offset;
		rkispp_write(dev, RKISPP_FEC_RD_UV_BASE, val);
		is_start = true;
	}

	if (is_start || is_quick) {
		u32 seq = 0;

		if (vdev->fec.dummy_cur_rd) {
			seq = vdev->fec.dummy_cur_rd->id;
			dev->ispp_sdev.frame_timestamp =
				vdev->fec.dummy_cur_rd->timestamp;
			dev->ispp_sdev.frm_sync_seq = seq;
		} else {
			seq = vdev->nr.buf.wr[0].id;
			dev->ispp_sdev.frame_timestamp =
				vdev->nr.buf.wr[0].timestamp;
			dev->ispp_sdev.frm_sync_seq = seq;
		}

		/* check MB config and output buf beforce start, when MB connect to FEC
		 * MB update by FEC_FORCE_UPD
		 */
		stream = &vdev->stream[STREAM_MB];
		if (stream->streaming) {
			if (!stream->is_cfg) {
				secure_config_mb(stream);
			} else if (!stream->curr_buf) {
				get_stream_buf(stream);
				if (stream->curr_buf)
					update_mi(stream);
			}
		}

		if (!dev->hw_dev->is_single)
			rkispp_update_regs(dev, RKISPP_FEC, RKISPP_FEC_CROP);
		writel(FEC_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
		if (vdev->nr.is_end) {
			if (!dev->hw_dev->is_single)
				rkispp_update_regs(dev, RKISPP_SCL0_CTRL, RKISPP_SCL2_FACTOR);
			writel(OTHER_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
			/* check scale stream stop state */
			for (val = STREAM_S0; val <= STREAM_S2; val++) {
				stream = &vdev->stream[val];
				if (stream->streaming && stream->stopping) {
					if (stream->ops->is_stopped(stream)) {
						stream->stopping = false;
						stream->streaming = false;
						wake_up(&stream->done);
					} else {
						stream->ops->stop(stream);
					}
				}
			}
		}
		v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
			 "FEC start seq:%d | Y_SHD rd:0x%x\n"
			 "\txint:0x%x xfra:0x%x yint:0x%x yfra:0x%x\n",
			 seq, readl(base + RKISPP_FEC_RD_Y_BASE_SHD),
			 readl(base + RKISPP_FEC_MESH_XINT_BASE_SHD),
			 readl(base + RKISPP_FEC_MESH_XFRA_BASE_SHD),
			 readl(base + RKISPP_FEC_MESH_YINT_BASE_SHD),
			 readl(base + RKISPP_FEC_MESH_YFRA_BASE_SHD));

		vdev->fec.dbg.id = seq;
		vdev->fec.dbg.timestamp = ktime_get_ns();
		if (monitor->is_en) {
			monitor->fec.time = vdev->fec.dbg.interval / 1000 / 1000;
			monitor->monitoring_module |= MONITOR_FEC;
			if (!completion_done(&monitor->fec.cmpl))
				complete(&monitor->fec.cmpl);
		}

		if (rkispp_is_reg_withstream_global())
			rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id, seq);
		if (reg_buf && (rkispp_debug_reg & ISPP_MODULE_FEC)) {
			u32 offset, size;

			offset = reg_buf->reg_size;
			size = 4 + RKISPP_FEC_CROP - RKISPP_FEC_CTRL;
			reg_buf->ispp_size[ISPP_ID_FEC] = size;
			reg_buf->ispp_offset[ISPP_ID_FEC] = offset;
			memcpy_fromio(&reg_buf->reg[offset], base + RKISPP_FEC_CTRL, size);

			offset += size;
			reg_buf->reg_size = offset;
		}

		if (!dev->hw_dev->is_shutdown) {
			writel(FEC_ST, base + RKISPP_CTRL_STRT);

			if (vdev->is_done_early)
				hrtimer_start(&vdev->frame_qst,
					      ns_to_ktime(5000000),
					      HRTIMER_MODE_REL);
		}
		vdev->fec.is_end = false;
	}
restart_unlock:
	spin_unlock_irqrestore(&monitor->lock, lock_flags1);
	spin_unlock_irqrestore(&vdev->fec.buf_lock, lock_flags);
}


void rkispp_sendbuf_to_nr(struct rkispp_device *dev,
			  struct rkispp_tnr_inf *tnr_inf)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_dummy_buffer *dummy;
	struct rkisp_ispp_buf *cur_buf;
	unsigned long lock_flags = 0;
	bool find_flg = false;
	struct dma_buf *dbuf;
	u32 size;

	size = sizeof(vdev->tnr.buf) / sizeof(*dummy);
	spin_lock_irqsave(&vdev->tnr.buf_lock, lock_flags);
	list_for_each_entry(cur_buf, &vdev->tnr.list_rpt, list) {
		if (cur_buf->index == tnr_inf->dev_id &&
		    cur_buf->didx[GROUP_BUF_GAIN] == tnr_inf->gainwr_idx) {
			find_flg = true;
			break;
		}
	}

	if (find_flg) {
		list_del(&cur_buf->list);

		dbuf = cur_buf->dbuf[GROUP_BUF_GAIN];
		dummy = dbuf_to_dummy(dbuf, &vdev->tnr.buf.iir, size);
		rkispp_prepare_buffer(dev, dummy);

		/* tnr write buf to nr */
		rkispp_module_work_event(dev, cur_buf, NULL,
					 ISPP_MODULE_NR, false);
	}
	spin_unlock_irqrestore(&vdev->tnr.buf_lock, lock_flags);
}

void rkispp_set_trigger_mode(struct rkispp_device *dev,
			     struct rkispp_trigger_mode *mode)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;

	if (mode->module & ISPP_MODULE_TNR)
		vdev->tnr.is_trigger = mode->on;
}

int rkispp_get_tnrbuf_fd(struct rkispp_device *dev, struct rkispp_buf_idxfd *idxfd)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkisp_ispp_buf *dbufs;
	struct rkispp_dummy_buffer *buf;
	unsigned long lock_flags = 0;
	int j, buf_idx, ret = 0;

	spin_lock_irqsave(&vdev->tnr.buf_lock, lock_flags);
	if (!vdev->tnr.is_but_init) {
		spin_unlock_irqrestore(&vdev->tnr.buf_lock, lock_flags);
		ret = -EAGAIN;
		return ret;
	}
	spin_unlock_irqrestore(&vdev->tnr.buf_lock, lock_flags);

	buf_idx = 0;
	list_for_each_entry(dbufs, &vdev->tnr.list_wr, list) {
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			dbufs->dfd[j] = dma_buf_fd(dbufs->dbuf[j], O_CLOEXEC);
			get_dma_buf(dbufs->dbuf[j]);
			idxfd->index[buf_idx] = dbufs->didx[j];
			idxfd->dmafd[buf_idx] = dbufs->dfd[j];
			buf_idx++;
		}
	}

	list_for_each_entry(dbufs, &vdev->tnr.list_rpt, list) {
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			dbufs->dfd[j] = dma_buf_fd(dbufs->dbuf[j], O_CLOEXEC);
			get_dma_buf(dbufs->dbuf[j]);
			idxfd->index[buf_idx] = dbufs->didx[j];
			idxfd->dmafd[buf_idx] = dbufs->dfd[j];
			buf_idx++;
		}
	}

	if (vdev->tnr.cur_wr) {
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			vdev->tnr.cur_wr->dfd[j] = dma_buf_fd(vdev->tnr.cur_wr->dbuf[j], O_CLOEXEC);
			get_dma_buf(vdev->tnr.cur_wr->dbuf[j]);
			idxfd->index[buf_idx] = vdev->tnr.cur_wr->didx[j];
			idxfd->dmafd[buf_idx] = vdev->tnr.cur_wr->dfd[j];
			buf_idx++;
		}
	}

	buf = &vdev->tnr.buf.gain_kg;
	buf->dma_fd = dma_buf_fd(buf->dbuf, O_CLOEXEC);
	get_dma_buf(buf->dbuf);
	idxfd->index[buf_idx] = buf->index;
	idxfd->dmafd[buf_idx] = buf->dma_fd;
	buf_idx++;

	idxfd->buf_num = buf_idx;

	return ret;
}

static void rkispp_module_work_event(struct rkispp_device *dev,
			      void *buf_rd, void *buf_wr,
			      u32 module, bool is_isr)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	bool is_fec_en = !!(vdev->module_ens & ISPP_MODULE_FEC);
	bool is_single = dev->hw_dev->is_single;
	//bool is_early = vdev->is_done_early;

	if (dev->hw_dev->is_shutdown)
		return;

	if (dev->ispp_sdev.state != ISPP_STOP) {
		if (module & ISPP_MODULE_TNR)
			tnr_work_event(dev, buf_rd, buf_wr, is_isr);
		else if (module & ISPP_MODULE_NR)
			nr_work_event(dev, buf_rd, buf_wr, is_isr);
		else
			fec_work_event(dev, buf_rd, is_isr, false);
	}

	/*
	 * ispp frame done to do next conditions
	 * mulit dev: cur frame (tnr->nr->fec) done for next frame
	 * 1.single dev: fec async with tnr, and sync with nr:
	 *   {    f0    }
	 *   tnr->nr->fec->|
	 *          |->tnr->nr->fec
	 *             {    f1    }
	 * 2.single dev and early mode:
	 *   {  f0 }  {  f1 }  {  f2 }
	 *   tnr->nr->tnr->nr->tnr->nr
	 *        |->fec->||->fec->|
	 *        {   f0  }{   f1  }
	 * 3.single fec
	 *
	 */
	if (is_isr && !buf_rd && !buf_wr &&
	    ((!is_fec_en && module == ISPP_MODULE_NR) ||
	     (is_fec_en &&
	      ((module == ISPP_MODULE_NR && is_single) ||
	       (module == ISPP_MODULE_FEC && !is_single))))) {
		dev->stream_vdev.monitor.retry = 0;
		rkispp_soft_reset(dev->hw_dev);
		rkispp_event_handle(dev, CMD_QUEUE_DMABUF, NULL);
	}

	if (dev->ispp_sdev.state == ISPP_STOP) {
		if ((module & (ISPP_MODULE_TNR | ISPP_MODULE_NR)) && buf_rd) {
			struct rkisp_ispp_buf *buf = buf_rd;

			if (buf->is_isp)
				v4l2_subdev_call(dev->ispp_sdev.remote_sd,
						 video, s_rx_buffer, buf, NULL);
		}
		if (!dev->hw_dev->is_idle)
			dev->hw_dev->is_idle = true;
	}
}

static int start_isp(struct rkispp_device *dev)
{
	struct rkispp_subdev *ispp_sdev = &dev->ispp_sdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream;
	struct rkisp_ispp_mode mode;
	int i, ret;

	if (dev->inp != INP_ISP || ispp_sdev->state)
		return 0;

	if (dev->stream_sync) {
		/* output stream enable then start isp */
		for (i = STREAM_MB; i <= STREAM_S2; i++) {
			stream = &vdev->stream[i];
			if (stream->linked && !stream->streaming)
				return 0;
		}
	} else if (atomic_read(&vdev->refcnt) > 1) {
		return 0;
	}

	rkispp_start_3a_run(dev);

	mutex_lock(&dev->hw_dev->dev_lock);

	mode.work_mode = dev->isp_mode;
	mode.buf_num = ((vdev->module_ens & ISPP_MODULE_TNR_3TO1) ==
			ISPP_MODULE_TNR_3TO1) ? 2 : 1;
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
	for (i = STREAM_MB; i <= STREAM_S2; i++) {
		stream = &vdev->stream[i];
		if (stream->streaming)
			stream->is_upd = true;
	}
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
	u32 i, mask = NR_INT | SHP_INT;
	bool is_fec_en = (vdev->module_ens & ISPP_MODULE_FEC);

	if (mis_val & TNR_INT)
		rkispp_module_work_event(dev, NULL, NULL,
					 ISPP_MODULE_TNR, true);
	if (mis_val & FEC_INT)
		rkispp_module_work_event(dev, NULL, NULL,
					 ISPP_MODULE_FEC, true);

	/* wait nr_shp/fec/scl idle */
	for (i = STREAM_S0; i <= STREAM_S2; i++) {
		stream = &vdev->stream[i];
		if (stream->is_upd && !is_fec_en &&
		    rkispp_read(dev, stream->config->reg.ctrl) & SW_SCL_ENABLE)
			mask |= stream->config->frame_end_id;
	}

	vdev->irq_ends |= (mis_val & mask);
	v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
		 "irq_ends:0x%x mask:0x%x\n",
		 vdev->irq_ends, mask);
	if (vdev->irq_ends != mask)
		return;
	vdev->irq_ends = 0;
	rkispp_module_work_event(dev, NULL, NULL,
				 ISPP_MODULE_NR, true);

	for (i = STREAM_MB; i <= STREAM_S2; i++) {
		stream = &vdev->stream[i];
		if (stream->streaming)
			stream->is_upd = true;
	}
}

static struct rkispp_stream_ops rkispp_stream_ops = {
	.config_modules = config_modules,
	.destroy_buf = rkispp_destroy_buf,
	.fec_work_event = fec_work_event,
	.start_isp = start_isp,
	.check_to_force_update = check_to_force_update,
	.update_mi = update_mi,
	.rkispp_frame_done_early = rkispp_frame_done_early,
	.rkispp_module_work_event = rkispp_module_work_event,
};

void rkispp_stream_init_ops_v10(struct rkispp_stream_vdev *stream_vdev)
{
	stream_vdev->stream_ops = &rkispp_stream_ops;
}
