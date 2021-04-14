// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
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

#define STREAM_IN_REQ_BUFS_MIN 1
#define STREAM_OUT_REQ_BUFS_MIN 0

/* memory align for mpp */
#define RK_MPP_ALIGN 4096

/*
 * DDR->|                                 |->MB------->DDR
 *      |->TNR->DDR->NR->SHARP->DDR->FEC->|->SCL0----->DDR
 * ISP->|                                 |->SCL1----->DDR
 *                                        |->SCL2----->DDR
 */

static const struct capture_fmt input_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YC_SWAP | FMT_YUYV | FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YUYV | FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV420,
	}
};

static const struct capture_fmt mb_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YC_SWAP | FMT_YUYV | FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YUYV | FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_FBC2,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV422 | FMT_FBC,
	}, {
		.fourcc = V4L2_PIX_FMT_FBC0,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV420 | FMT_FBC,
	}
};

static const struct capture_fmt scl_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.wr_fmt = FMT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_GREY,
		.bpp = { 8 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YC_SWAP | FMT_YUYV | FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.wr_fmt = FMT_YUYV | FMT_YUV422,
	}
};

static struct stream_config input_config = {
	.fmts = input_fmts,
	.fmt_size = ARRAY_SIZE(input_fmts),
};

static struct stream_config mb_config = {
	.fmts = mb_fmts,
	.fmt_size = ARRAY_SIZE(mb_fmts),
};

static struct stream_config scl0_config = {
	.fmts = scl_fmts,
	.fmt_size = ARRAY_SIZE(scl_fmts),
	.frame_end_id = SCL0_INT,
	.reg = {
		.ctrl = RKISPP_SCL0_CTRL,
		.factor = RKISPP_SCL0_FACTOR,
		.cur_y_base = RKISPP_SCL0_CUR_Y_BASE,
		.cur_uv_base = RKISPP_SCL0_CUR_UV_BASE,
		.cur_vir_stride = RKISPP_SCL0_CUR_VIR_STRIDE,
		.cur_y_base_shd = RKISPP_SCL0_CUR_Y_BASE_SHD,
		.cur_uv_base_shd = RKISPP_SCL0_CUR_UV_BASE_SHD,
	},
};

static struct stream_config scl1_config = {
	.fmts = scl_fmts,
	.fmt_size = ARRAY_SIZE(scl_fmts),
	.frame_end_id = SCL1_INT,
	.reg = {
		.ctrl = RKISPP_SCL1_CTRL,
		.factor = RKISPP_SCL1_FACTOR,
		.cur_y_base = RKISPP_SCL1_CUR_Y_BASE,
		.cur_uv_base = RKISPP_SCL1_CUR_UV_BASE,
		.cur_vir_stride = RKISPP_SCL1_CUR_VIR_STRIDE,
		.cur_y_base_shd = RKISPP_SCL1_CUR_Y_BASE_SHD,
		.cur_uv_base_shd = RKISPP_SCL1_CUR_UV_BASE_SHD,
	},
};

static struct stream_config scl2_config = {
	.fmts = scl_fmts,
	.fmt_size = ARRAY_SIZE(scl_fmts),
	.frame_end_id = SCL2_INT,
	.reg = {
		.ctrl = RKISPP_SCL2_CTRL,
		.factor = RKISPP_SCL2_FACTOR,
		.cur_y_base = RKISPP_SCL2_CUR_Y_BASE,
		.cur_uv_base = RKISPP_SCL2_CUR_UV_BASE,
		.cur_vir_stride = RKISPP_SCL2_CUR_VIR_STRIDE,
		.cur_y_base_shd = RKISPP_SCL2_CUR_Y_BASE_SHD,
		.cur_uv_base_shd = RKISPP_SCL2_CUR_UV_BASE_SHD,
	},
};

static void set_y_addr(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_y_base, val);
}

static void set_uv_addr(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_uv_base, val);
}

static void set_vir_stride(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.cur_vir_stride, val);
}

static void set_scl_factor(struct rkispp_stream *stream, u32 val)
{
	rkispp_write(stream->isppdev, stream->config->reg.factor, val);
}

static int fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	switch (fcc) {
	case V4L2_PIX_FMT_GREY:
		*xsubs = 1;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_FBC2:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_FBC0:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const
struct capture_fmt *find_fmt(struct rkispp_stream *stream,
			     const u32 pixelfmt)
{
	const struct capture_fmt *fmt;
	unsigned int i;

	for (i = 0; i < stream->config->fmt_size; i++) {
		fmt = &stream->config->fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}
	return NULL;
}

static void vir_cpy_image(struct work_struct *work)
{
	struct rkispp_vir_cpy *cpy =
		container_of(work, struct rkispp_vir_cpy, work);
	struct rkispp_stream *vir = cpy->stream;
	struct rkispp_buffer *src_buf = NULL;
	unsigned long lock_flags = 0;
	u32 i;

	v4l2_dbg(1, rkispp_debug, &vir->isppdev->v4l2_dev,
		 "%s enter\n", __func__);

	vir->streaming = true;
	spin_lock_irqsave(&vir->vbq_lock, lock_flags);
	if (!list_empty(&cpy->queue)) {
		src_buf = list_first_entry(&cpy->queue,
				struct rkispp_buffer, queue);
		list_del(&src_buf->queue);
	}
	spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);

	while (src_buf || vir->streaming) {
		if (vir->stopping || !vir->streaming)
			goto end;
		if (!src_buf)
			wait_for_completion(&cpy->cmpl);

		vir->is_end = false;
		spin_lock_irqsave(&vir->vbq_lock, lock_flags);
		if (!src_buf && !list_empty(&cpy->queue)) {
			src_buf = list_first_entry(&cpy->queue,
					struct rkispp_buffer, queue);
			list_del(&src_buf->queue);
		}
		if (src_buf && !vir->curr_buf && !list_empty(&vir->buf_queue)) {
			vir->curr_buf = list_first_entry(&vir->buf_queue,
					struct rkispp_buffer, queue);
			list_del(&vir->curr_buf->queue);
		}
		spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);
		if (!vir->curr_buf || !src_buf)
			goto end;
		for (i = 0; i < vir->out_cap_fmt.mplanes; i++) {
			u32 payload_size = vir->out_fmt.plane_fmt[i].sizeimage;
			void *src = vb2_plane_vaddr(&src_buf->vb.vb2_buf, i);
			void *dst = vb2_plane_vaddr(&vir->curr_buf->vb.vb2_buf, i);

			if (!src || !dst)
				break;
			vb2_set_plane_payload(&vir->curr_buf->vb.vb2_buf, i, payload_size);
			memcpy(dst, src, payload_size);
		}
		vir->curr_buf->vb.sequence = src_buf->vb.sequence;
		vir->curr_buf->vb.vb2_buf.timestamp = src_buf->vb.vb2_buf.timestamp;
		vb2_buffer_done(&vir->curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		vir->curr_buf = NULL;
end:
		if (src_buf)
			vb2_buffer_done(&src_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		src_buf = NULL;
		spin_lock_irqsave(&vir->vbq_lock, lock_flags);
		if (!list_empty(&cpy->queue)) {
			src_buf = list_first_entry(&cpy->queue,
					struct rkispp_buffer, queue);
			list_del(&src_buf->queue);
		} else if (vir->stopping) {
			vir->streaming = false;
		}
		spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);
	}

	vir->is_end = true;
	if (vir->stopping) {
		vir->stopping = false;
		vir->streaming = false;
		wake_up(&vir->done);
	}
	v4l2_dbg(1, rkispp_debug, &vir->isppdev->v4l2_dev,
		 "%s exit\n", __func__);
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
		if (stream->is_upd && !is_fec_en)
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

static void irq_work(struct work_struct *work)
{
	struct rkispp_device *dev = container_of(work, struct rkispp_device, irq_work);

	dev->hw_dev->is_first = false;
	rkispp_set_clk_rate(dev->hw_dev->clks[0], dev->hw_dev->core_clk_max);
	check_to_force_update(dev, dev->mis_val);
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

static int rkispp_frame_end(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct capture_fmt *fmt = &stream->out_cap_fmt;
	struct rkisp_ispp_reg *reg_buf = NULL;
	unsigned long lock_flags = 0;
	int i = 0;

	if (stream->curr_buf) {
		struct rkispp_stream *vir = &dev->stream_vdev.stream[STREAM_VIR];
		u64 ns = dev->ispp_sdev.frame_timestamp;

		if (!ns)
			ns = ktime_get_ns();

		for (i = 0; i < fmt->mplanes; i++) {
			u32 payload_size =
				stream->out_fmt.plane_fmt[i].sizeimage;
			vb2_set_plane_payload(&stream->curr_buf->vb.vb2_buf, i,
					      payload_size);
		}
		stream->curr_buf->vb.sequence = dev->ispp_sdev.frm_sync_seq;
		stream->curr_buf->vb.vb2_buf.timestamp = ns;

		if (stream->is_reg_withstream &&
		    (fmt->wr_fmt & FMT_FBC || fmt->wr_fmt == FMT_YUV420)) {
			void *addr = vb2_plane_vaddr(&stream->curr_buf->vb.vb2_buf, i);

			rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id,
						 stream->curr_buf->vb.sequence);
			if (reg_buf) {
				u32 cpy_size = offsetof(struct rkisp_ispp_reg, reg);

				cpy_size += reg_buf->reg_size;
				memcpy(addr, reg_buf, cpy_size);

				rkispp_release_regbuf(dev, reg_buf);
				vb2_set_plane_payload(&stream->curr_buf->vb.vb2_buf, 1, cpy_size);
				v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
					 "stream(0x%x) write reg buf to last plane\n",
					 stream->id);
			} else {
				v4l2_err(&dev->v4l2_dev,
					 "%s can not find reg buf: dev_id %d, sequence %d\n",
					 __func__, dev->dev_id, stream->curr_buf->vb.sequence);
			}
		}

		if (vir->streaming && vir->conn_id == stream->id) {
			spin_lock_irqsave(&vir->vbq_lock, lock_flags);
			if (vir->streaming)
				list_add_tail(&stream->curr_buf->queue,
					&dev->stream_vdev.vir_cpy.queue);
			spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);
			if (!completion_done(&dev->stream_vdev.vir_cpy.cmpl))
				complete(&dev->stream_vdev.vir_cpy.cmpl);
			if (!vir->streaming)
				vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
						VB2_BUF_STATE_DONE);
		} else {
			vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
					VB2_BUF_STATE_DONE);
		}
		ns = ktime_get_ns();
		stream->dbg.interval = ns - stream->dbg.timestamp;
		stream->dbg.timestamp = ns;
		stream->dbg.delay = ns - stream->curr_buf->vb.vb2_buf.timestamp;
		stream->dbg.id = stream->curr_buf->vb.sequence;

		stream->curr_buf = NULL;
	} else if (stream->is_reg_withstream &&
		   (fmt->wr_fmt & FMT_FBC || fmt->wr_fmt == FMT_YUV420)) {
		u32 frame_id;

		frame_id = dev->ispp_sdev.frm_sync_seq;
		rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id, frame_id);
		if (reg_buf) {
			rkispp_release_regbuf(dev, reg_buf);
			v4l2_info(&dev->v4l2_dev,
				  "%s: current frame use dummy buffer(dev_id %d, sequence %d)\n",
				  __func__, dev->dev_id, frame_id);
		}
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (!list_empty(&stream->buf_queue) && !stream->curr_buf) {
		stream->curr_buf =
			list_first_entry(&stream->buf_queue,
					 struct rkispp_buffer, queue);
		list_del(&stream->curr_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	update_mi(stream);
	return 0;
}

static void *get_pool_buf(struct rkispp_device *dev,
			  struct rkisp_ispp_buf *dbufs)
{
	int i;

	for (i = 0; i < RKISPP_BUF_POOL_MAX; i++)
		if (dev->hw_dev->pool[i].dbufs == dbufs)
			return &dev->hw_dev->pool[i];

	return NULL;
}

static void *dbuf_to_dummy(struct dma_buf *dbuf,
			   struct rkispp_dummy_buffer *pool,
			   int num)
{
	int i;

	for (i = 0; i < num; i++) {
		if (pool->dbuf == dbuf)
			return pool;
		pool++;
	}

	return NULL;
}

static void *get_list_buf(struct list_head *list, bool is_isp_ispp)
{
	void *buf = NULL;

	if (!list_empty(list)) {
		if (is_isp_ispp) {
			buf = list_first_entry(list,
				struct rkisp_ispp_buf, list);
			list_del(&((struct rkisp_ispp_buf *)buf)->list);
		} else {
			buf = list_first_entry(list,
				struct rkispp_dummy_buffer, list);
			list_del(&((struct rkispp_dummy_buffer *)buf)->list);
		}
	}
	return buf;
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
		cnt = RKISPP_BUF_MAX;
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
	} else {
		stream = &vdev->stream[STREAM_MB];
		if (!stream->streaming) {
			val = hw->dummy_buf.dma_addr;
			rkispp_write(dev, RKISPP_SHARP_WR_Y_BASE, val);
			rkispp_write(dev, RKISPP_SHARP_WR_UV_BASE, val);
			rkispp_write(dev, RKISPP_SHARP_WR_VIR_STRIDE, ALIGN(width * mult, 16) >> 2);
			rkispp_set_bits(dev, RKISPP_SHARP_CTRL, SW_SHP_WR_FORMAT_MASK, FMT_FBC);
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
	rkispp_write(dev, RKISPP_FEC_PIC_SIZE, height << 16 | width);
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

static void rkispp_start_3a_run(struct rkispp_device *dev)
{
	struct rkispp_params_vdev *params_vdev = &dev->params_vdev;
	struct video_device *vdev = &params_vdev->vnode.vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_START,
	};
	int ret;

	if (!params_vdev->is_subs_evt)
		return;

	v4l2_event_queue(vdev, &ev);
	ret = wait_event_timeout(dev->sync_onoff,
			params_vdev->streamon && !params_vdev->first_params,
			msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->v4l2_dev,
			  "waiting on params stream on event timeout\n");
	else
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "Waiting for 3A on use %d ms\n", 1000 - ret);
}

static void rkispp_stop_3a_run(struct rkispp_device *dev)
{
	struct rkispp_params_vdev *params_vdev = &dev->params_vdev;
	struct video_device *vdev = &params_vdev->vnode.vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_STOP,
	};
	int ret;

	if (!params_vdev->is_subs_evt)
		return;

	v4l2_event_queue(vdev, &ev);
	ret = wait_event_timeout(dev->sync_onoff, !params_vdev->streamon,
				 msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->v4l2_dev,
			  "waiting on params stream off event timeout\n");
	else
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "Waiting for 3A off use %d ms\n", 1000 - ret);
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

static int config_modules(struct rkispp_device *dev)
{
	int ret;

	rkispp_start_3a_run(dev);

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "stream module ens:0x%x\n", dev->stream_vdev.module_ens);
	dev->stream_vdev.monitor.monitoring_module = 0;
	dev->stream_vdev.monitor.restart_module = 0;
	dev->stream_vdev.monitor.is_restart = false;
	dev->stream_vdev.monitor.retry = 0;
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
	rkispp_params_cfg(&dev->params_vdev, 0);
	return 0;
free_nr:
	nr_free_buf(dev);
free_tnr:
	tnr_free_buf(dev);
	return ret;
}

static int start_ii(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	unsigned long lock_flags = 0;
	struct rkispp_buffer *buf;
	int i;

	v4l2_subdev_call(&dev->ispp_sdev.sd, video, s_stream, true);
	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue, struct rkispp_buffer, queue);
		list_del(&buf->queue);
		i = buf->vb.vb2_buf.index;
		vdev->input[i].priv = buf;
		vdev->input[i].index = dev->dev_id;
		vdev->input[i].frame_timestamp = buf->vb.vb2_buf.timestamp;
		vdev->input[i].frame_id = ++dev->ispp_sdev.frm_sync_seq;
		rkispp_event_handle(dev, CMD_QUEUE_DMABUF, &vdev->input[i]);
	}
	stream->streaming = true;
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
	return 0;
}

static int config_ii(struct rkispp_stream *stream)
{
	stream->is_cfg = true;
	return config_modules(stream->isppdev);
}

static int is_stopped_ii(struct rkispp_stream *stream)
{
	stream->streaming = false;
	return true;
}

static void secure_config_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	u32 limit_range;

	/* enable dma immediately, config in idle state */
	switch (stream->last_module) {
	case ISPP_MODULE_NR:
	case ISPP_MODULE_SHP:
		limit_range = (stream->out_fmt.quantization != V4L2_QUANTIZATION_LIM_RANGE) ?
			0 : SW_SHP_WR_YUV_LIMIT;
		rkispp_set_bits(dev, RKISPP_SHARP_CTRL,
				SW_SHP_WR_YUV_LIMIT | SW_SHP_WR_FORMAT_MASK,
				limit_range | stream->out_cap_fmt.wr_fmt);
		rkispp_clear_bits(dev, RKISPP_SHARP_CORE_CTRL, SW_SHP_DMA_DIS);
		break;
	case ISPP_MODULE_FEC:
		limit_range = (stream->out_fmt.quantization != V4L2_QUANTIZATION_LIM_RANGE) ?
			0 : SW_FEC_WR_YUV_LIMIT;
		rkispp_set_bits(dev, RKISPP_FEC_CTRL, SW_FEC_WR_YUV_LIMIT | FMT_WR_MASK,
				limit_range | stream->out_cap_fmt.wr_fmt << 4);
		rkispp_write(dev, RKISPP_FEC_PIC_SIZE,
			     stream->out_fmt.height << 16 | stream->out_fmt.width);
		rkispp_clear_bits(dev, RKISPP_FEC_CORE_CTRL, SW_FEC2DDR_DIS);
		break;
	default:
		break;
	}
	stream->is_cfg = true;
}

static int config_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	u32 i, mult = 1;

	for (i = ISPP_MODULE_FEC; i > 0; i = i >> 1) {
		if (dev->stream_vdev.module_ens & i)
			break;
	}
	if (!i)
		return -EINVAL;

	stream->last_module = i;
	switch (i) {
	case ISPP_MODULE_TNR:
		stream->config->frame_end_id = TNR_INT;
		stream->config->reg.cur_y_base = RKISPP_TNR_WR_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_TNR_WR_UV_BASE;
		stream->config->reg.cur_vir_stride = RKISPP_TNR_WR_VIR_STRIDE;
		stream->config->reg.cur_y_base_shd = RKISPP_TNR_WR_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_TNR_WR_UV_BASE_SHD;
		rkispp_set_bits(dev, RKISPP_TNR_CTRL, FMT_WR_MASK,
				SW_TNR_1ST_FRM | stream->out_cap_fmt.wr_fmt << 4);
		break;
	case ISPP_MODULE_NR:
	case ISPP_MODULE_SHP:
		stream->config->frame_end_id = SHP_INT;
		stream->config->reg.cur_y_base = RKISPP_SHARP_WR_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_SHARP_WR_UV_BASE;
		stream->config->reg.cur_vir_stride = RKISPP_SHARP_WR_VIR_STRIDE;
		stream->config->reg.cur_y_base_shd = RKISPP_SHARP_WR_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_SHARP_WR_UV_BASE_SHD;
		break;
	default:
		stream->config->frame_end_id = FEC_INT;
		stream->config->reg.cur_y_base = RKISPP_FEC_WR_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_FEC_WR_UV_BASE;
		stream->config->reg.cur_vir_stride = RKISPP_FEC_WR_VIR_STRIDE;
		stream->config->reg.cur_y_base_shd = RKISPP_FEC_WR_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_FEC_WR_UV_BASE_SHD;
	}
	if (stream->out_cap_fmt.wr_fmt & FMT_YUYV)
		mult = 2;
	else if (stream->out_cap_fmt.wr_fmt & FMT_FBC)
		mult = 0;
	set_vir_stride(stream, ALIGN(stream->out_fmt.width * mult, 16) >> 2);

	/* config first buf */
	rkispp_frame_end(stream);

	if (dev->ispp_sdev.state == ISPP_STOP)
		secure_config_mb(stream);
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s last module:%d\n", __func__, i);
	return 0;
}

static int is_stopped_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	bool is_stopped = true;
	u32 val;

	if (vdev->module_ens & ISPP_MODULE_FEC) {
		/* close dma write immediately */
		rkispp_clear_bits(dev, RKISPP_FEC_CTRL, FMT_FBC << 4);
		rkispp_set_bits(dev, RKISPP_FEC_CORE_CTRL,
				0, SW_FEC2DDR_DIS);
	} else if (vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)) {
		val = dev->hw_dev->dummy_buf.dma_addr;
		rkispp_write(dev, RKISPP_SHARP_WR_Y_BASE, val);
		rkispp_write(dev, RKISPP_SHARP_WR_UV_BASE, val);
		rkispp_set_bits(dev, RKISPP_SHARP_CTRL, SW_SHP_WR_FORMAT_MASK, FMT_FBC);
	}

	/* for wait last frame */
	if (atomic_read(&dev->stream_vdev.refcnt) == 1) {
		val = readl(dev->hw_dev->base_addr + RKISPP_CTRL_SYS_STATUS);
		is_stopped = (val & 0x8f) ? false : true;
	}

	return is_stopped;
}

static int limit_check_mb(struct rkispp_stream *stream,
			  struct v4l2_pix_format_mplane *try_fmt)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_subdev *sdev = &dev->ispp_sdev;
	u32 *w = try_fmt ? &try_fmt->width : &stream->out_fmt.width;
	u32 *h = try_fmt ? &try_fmt->height : &stream->out_fmt.height;

	if (*w != sdev->out_fmt.width || *h != sdev->out_fmt.height) {
		v4l2_err(&dev->v4l2_dev,
			 "output:%dx%d should euqal to input:%dx%d\n",
			 *w, *h, sdev->out_fmt.width, sdev->out_fmt.height);
		if (!try_fmt) {
			*w = 0;
			*h = 0;
		}
		return -EINVAL;
	}

	return 0;
}

static int config_scl(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	const struct capture_fmt *fmt = &stream->out_cap_fmt;
	u32 in_width = dev->ispp_sdev.out_fmt.width;
	u32 in_height = dev->ispp_sdev.out_fmt.height;
	u32 hy_fac = (stream->out_fmt.width - 1) * 8192 /
			(in_width - 1) + 1;
	u32 vy_fac = (stream->out_fmt.height - 1) * 8192 /
			(in_height - 1) + 1;
	u32 val = SW_SCL_ENABLE, mult = 1;
	u32 mask = SW_SCL_WR_YUV_LIMIT | SW_SCL_WR_YUYV_YCSWAP |
		SW_SCL_WR_YUYV_FORMAT | SW_SCL_WR_YUV_FORMAT |
		SW_SCL_WR_UV_DIS | SW_SCL_BYPASS;

	/* config first buf */
	rkispp_frame_end(stream);
	if (hy_fac == 8193 && vy_fac == 8193)
		val |= SW_SCL_BYPASS;
	if (fmt->wr_fmt & FMT_YUYV)
		mult = 2;
	set_vir_stride(stream, ALIGN(stream->out_fmt.width * mult, 16) >> 2);
	set_scl_factor(stream, vy_fac << 16 | hy_fac);
	val |= fmt->wr_fmt << 3 |
		((fmt->fourcc != V4L2_PIX_FMT_GREY) ? 0 : SW_SCL_WR_UV_DIS) |
		((stream->out_fmt.quantization != V4L2_QUANTIZATION_LIM_RANGE) ?
		 0 : SW_SCL_WR_YUV_LIMIT);
	rkispp_set_bits(dev, stream->config->reg.ctrl, mask, val);
	stream->is_cfg = true;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "scl%d ctrl:0x%x stride:0x%x factor:0x%x\n",
		 stream->id - STREAM_S0,
		 rkispp_read(dev, stream->config->reg.ctrl),
		 rkispp_read(dev, stream->config->reg.cur_vir_stride),
		 rkispp_read(dev, stream->config->reg.factor));
	return 0;
}

static void stop_scl(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;

	rkispp_clear_bits(dev, stream->config->reg.ctrl, SW_SCL_ENABLE);
}

static int is_stopped_scl(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	u32 scl_en, other_en = 0, val = SW_SCL_ENABLE;
	bool is_stopped;

	if (dev->hw_dev->is_single)
		val = SW_SCL_ENABLE_SHD;
	scl_en = rkispp_read(dev, stream->config->reg.ctrl) & val;
	if (atomic_read(&dev->stream_vdev.refcnt) == 1) {
		val = readl(dev->hw_dev->base_addr + RKISPP_CTRL_SYS_STATUS);
		other_en = val & 0x8f;
	}
	is_stopped = (scl_en | other_en) ? false : true;
	return is_stopped;
}

static int limit_check_scl(struct rkispp_stream *stream,
			   struct v4l2_pix_format_mplane *try_fmt)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_subdev *sdev = &dev->ispp_sdev;
	u32 max_width = 1280, max_ratio = 8, min_ratio = 2;
	u32 *w = try_fmt ? &try_fmt->width : &stream->out_fmt.width;
	u32 *h = try_fmt ? &try_fmt->height : &stream->out_fmt.height;
	u32 forcc = try_fmt ? try_fmt->pixelformat : stream->out_fmt.pixelformat;
	int ret = 0;

	/* bypass scale */
	if (*w == sdev->out_fmt.width && *h == sdev->out_fmt.height)
		return ret;

	if (stream->id == STREAM_S0) {
		if (*h == sdev->out_fmt.height || (forcc != V4L2_PIX_FMT_NV12))
			max_width = 3264;
		else
			max_width = 2080;
		min_ratio = 1;
	}

	if (*w > max_width ||
	    *w * max_ratio < sdev->out_fmt.width ||
	    *h * max_ratio < sdev->out_fmt.height ||
	    *w * min_ratio > sdev->out_fmt.width ||
	    *h * min_ratio > sdev->out_fmt.height) {
		v4l2_err(&dev->v4l2_dev,
			 "scale%d:%dx%d out of range:\n"
			 "\t[width max:%d ratio max:%d min:%d]\n",
			 stream->id - STREAM_S0, *w, *h,
			 max_width, max_ratio, min_ratio);
		if (!try_fmt) {
			*w = 0;
			*h = 0;
		}
		ret = -EINVAL;
	}

	return ret;
}

static struct streams_ops input_stream_ops = {
	.config = config_ii,
	.start = start_ii,
	.is_stopped = is_stopped_ii,
};

static struct streams_ops mb_stream_ops = {
	.config = config_mb,
	.is_stopped = is_stopped_mb,
	.limit_check = limit_check_mb,
};

static struct streams_ops scal_stream_ops = {
	.config = config_scl,
	.stop = stop_scl,
	.is_stopped = is_stopped_scl,
	.limit_check = limit_check_scl,
};

/***************************** vb2 operations*******************************/

static int rkispp_queue_setup(struct vb2_queue *queue,
			      unsigned int *num_buffers,
			      unsigned int *num_planes,
			      unsigned int sizes[],
			      struct device *alloc_ctxs[])
{
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct capture_fmt *cap_fmt = NULL;
	u32 i;

	pixm = &stream->out_fmt;
	if (!pixm->width || !pixm->height)
		return -EINVAL;
	cap_fmt = &stream->out_cap_fmt;
	*num_planes = cap_fmt->mplanes;

	for (i = 0; i < cap_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;

		plane_fmt = &pixm->plane_fmt[i];
		/* height to align with 16 when allocating memory
		 * so that Rockchip encoder can use DMA buffer directly
		 */
		sizes[i] = (stream->type == STREAM_OUTPUT &&
			    cap_fmt->wr_fmt != FMT_FBC) ?
				plane_fmt->sizeimage / pixm->height *
				ALIGN(pixm->height, 16) :
				plane_fmt->sizeimage;
	}

	if (stream->is_reg_withstream &&
	    (cap_fmt->wr_fmt & FMT_FBC || cap_fmt->wr_fmt == FMT_YUV420)) {
		(*num_planes)++;
		sizes[1] = sizeof(struct rkisp_ispp_reg);
	}

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s stream:%d count %d, size %d\n",
		 v4l2_type_names[queue->type],
		 stream->id, *num_buffers, sizes[0]);

	return 0;
}

static void rkispp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkispp_buffer *isppbuf = to_rkispp_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *cap_fmt = &stream->out_cap_fmt;
	unsigned long lock_flags = 0;
	u32 height, size, offset;
	struct sg_table *sgt;
	int i;

	memset(isppbuf->buff_addr, 0, sizeof(isppbuf->buff_addr));
	for (i = 0; i < cap_fmt->mplanes; i++) {
		vb2_plane_vaddr(vb, i);
		if (stream->isppdev->hw_dev->is_mmu) {
			sgt = vb2_dma_sg_plane_desc(vb, i);
			isppbuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			isppbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
	}
	/*
	 * NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	 * memory plane formats, so calculate the size explicitly.
	 */
	if (cap_fmt->mplanes == 1) {
		for (i = 0; i < cap_fmt->cplanes - 1; i++) {
			/* FBC mode calculate payload offset */
			height = (cap_fmt->wr_fmt & FMT_FBC) ?
				ALIGN(pixm->height, 16) >> 4 : pixm->height;
			size = (i == 0) ?
				pixm->plane_fmt[i].bytesperline * height :
				pixm->plane_fmt[i].sizeimage;
			offset = (cap_fmt->wr_fmt & FMT_FBC) ?
				ALIGN(size, RK_MPP_ALIGN) : size;
			isppbuf->buff_addr[i + 1] =
				isppbuf->buff_addr[i] + offset;
		}
	}

	v4l2_dbg(2, rkispp_debug, &stream->isppdev->v4l2_dev,
		 "%s stream:%d buf:0x%x\n", __func__,
		 stream->id, isppbuf->buff_addr[0]);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->type == STREAM_OUTPUT ||
	    (stream->id == STREAM_II && !stream->streaming)) {
		list_add_tail(&isppbuf->queue, &stream->buf_queue);
	} else {
		i = vb->index;
		vdev->input[i].priv = isppbuf;
		vdev->input[i].index = dev->dev_id;
		vdev->input[i].frame_timestamp = vb->timestamp;
		vdev->input[i].frame_id = ++dev->ispp_sdev.frm_sync_seq;
		rkispp_event_handle(dev, CMD_QUEUE_DMABUF, &vdev->input[i]);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void rkispp_destroy_buf(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev= &dev->stream_vdev;

	if (atomic_read(&vdev->refcnt) == 1) {
		vdev->irq_ends = 0;
		tnr_free_buf(dev);
		nr_free_buf(dev);
		fec_free_buf(dev);
		rkispp_event_handle(dev, CMD_FREE_POOL, NULL);
	}
}

static void rkispp_stream_stop(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	bool is_wait = true;
	int ret = 0;

	stream->stopping = true;
	if (atomic_read(&dev->stream_vdev.refcnt) == 1) {
		v4l2_subdev_call(&dev->ispp_sdev.sd, video, s_stream, false);
		rkispp_stop_3a_run(dev);
		if (dev->stream_vdev.fec.is_end &&
		    (dev->dev_id != dev->hw_dev->cur_dev_id || dev->hw_dev->is_idle))
			is_wait = false;
	}
	if (is_wait) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(500));
		if (!ret)
			v4l2_warn(&dev->v4l2_dev,
				  "stream:%d stop timeout\n", stream->id);
	}
	if (stream->ops) {
		/* scl stream close dma write */
		if (stream->ops->stop)
			stream->ops->stop(stream);
		else if (stream->ops->is_stopped)
			/* mb stream close dma write immediately */
			stream->ops->is_stopped(stream);
	}
	stream->is_upd = false;
	stream->streaming = false;
	stream->stopping = false;
}

static void destroy_buf_queue(struct rkispp_stream *stream,
			      enum vb2_buffer_state state)
{
	struct vb2_queue *queue = &stream->vnode.buf_queue;
	unsigned long lock_flags = 0;
	struct rkispp_buffer *buf;
	u32 i;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		stream->curr_buf = NULL;
	}
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue,
			struct rkispp_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	for (i = 0; i < queue->num_buffers; ++i) {
		if (queue->bufs[i]->state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(queue->bufs[i], VB2_BUF_STATE_ERROR);
	}
}

static void rkispp_stop_streaming(struct vb2_queue *queue)
{
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_hw_dev *hw = dev->hw_dev;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d enter\n", __func__, stream->id);

	if (!stream->streaming)
		return;

	if (stream->id == STREAM_VIR) {
		stream->stopping = true;
		wait_event_timeout(stream->done,
				   stream->is_end,
				   msecs_to_jiffies(500));
		stream->streaming = false;
		stream->stopping = false;
		destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);
		if (!completion_done(&dev->stream_vdev.vir_cpy.cmpl))
			complete(&dev->stream_vdev.vir_cpy.cmpl);
		return;
	}

	mutex_lock(&dev->hw_dev->dev_lock);
	rkispp_stream_stop(stream);
	destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);
	rkispp_destroy_buf(stream);
	mutex_unlock(&dev->hw_dev->dev_lock);
	rkispp_free_common_dummy_buf(dev);
	atomic_dec(&dev->stream_vdev.refcnt);

	if (!atomic_read(&hw->refcnt) &&
	    !atomic_read(&dev->stream_vdev.refcnt)) {
		rkispp_set_clk_rate(hw->clks[0], hw->core_clk_min);
		hw->is_idle = true;
		hw->is_first = true;
	}
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d exit\n", __func__, stream->id);
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

static int rkispp_start_streaming(struct vb2_queue *queue,
				  unsigned int count)
{
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_hw_dev *hw = dev->hw_dev;
	int ret = -1;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d enter\n", __func__, stream->id);

	if (stream->streaming)
		return -EBUSY;

	stream->is_end = true;
	if (stream->id == STREAM_VIR) {
		struct rkispp_stream *t = &dev->stream_vdev.stream[stream->conn_id];

		if (t->streaming) {
			INIT_WORK(&dev->stream_vdev.vir_cpy.work, vir_cpy_image);
			init_completion(&dev->stream_vdev.vir_cpy.cmpl);
			INIT_LIST_HEAD(&dev->stream_vdev.vir_cpy.queue);
			dev->stream_vdev.vir_cpy.stream = stream;
			schedule_work(&dev->stream_vdev.vir_cpy.work);
			ret = 0;
		} else {
			v4l2_err(&dev->v4l2_dev,
				 "no stream enable for iqtool\n");
			destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
			ret = -EINVAL;
		}
		return ret;
	}

	if (dev->inp == INP_DDR &&
	    !atomic_read(&hw->refcnt) &&
	    !atomic_read(&dev->stream_vdev.refcnt) &&
	    clk_get_rate(hw->clks[0]) <= hw->core_clk_min) {
		dev->hw_dev->is_first = false;
		rkispp_set_clk_rate(hw->clks[0], hw->core_clk_max);
	}

	stream->is_upd = false;
	stream->is_cfg = false;
	atomic_inc(&dev->stream_vdev.refcnt);
	if (!dev->inp || !stream->linked) {
		v4l2_err(&dev->v4l2_dev,
			 "no link or invalid input source\n");
		goto free_buf_queue;
	}

	ret = rkispp_alloc_common_dummy_buf(stream->isppdev);
	if (ret < 0)
		goto free_buf_queue;

	if (dev->inp == INP_ISP)
		dev->stream_vdev.module_ens |= ISPP_MODULE_NR;

	if (stream->ops && stream->ops->config) {
		ret = stream->ops->config(stream);
		if (ret < 0)
			goto free_dummy_buf;
	}

	/* start from ddr */
	if (stream->ops && stream->ops->start)
		stream->ops->start(stream);

	stream->streaming = true;

	/* start from isp */
	ret = start_isp(dev);
	if (ret)
		goto free_dummy_buf;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d exit\n", __func__, stream->id);
	return 0;
free_dummy_buf:
	rkispp_free_common_dummy_buf(stream->isppdev);
free_buf_queue:
	destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
	atomic_dec(&dev->stream_vdev.refcnt);
	stream->streaming = false;
	stream->is_upd = false;
	v4l2_err(&dev->v4l2_dev, "%s id:%d failed ret:%d\n",
		 __func__, stream->id, ret);
	return ret;
}

static struct vb2_ops stream_vb2_ops = {
	.queue_setup = rkispp_queue_setup,
	.buf_queue = rkispp_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkispp_stop_streaming,
	.start_streaming = rkispp_start_streaming,
};

static int rkispp_init_vb2_queue(struct vb2_queue *q,
				 struct rkispp_stream *stream,
				 enum v4l2_buf_type buf_type)
{
	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;
	q->drv_priv = stream;
	q->ops = &stream_vb2_ops;
	q->mem_ops = stream->isppdev->hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct rkispp_buffer);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		q->min_buffers_needed = STREAM_IN_REQ_BUFS_MIN;
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	} else {
		q->min_buffers_needed = STREAM_OUT_REQ_BUFS_MIN;
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	}
	q->lock = &stream->isppdev->apilock;
	q->dev = stream->isppdev->hw_dev->dev;
	q->allow_cache_hints = 1;
	if (stream->isppdev->hw_dev->is_dma_contig)
		q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	return vb2_queue_init(q);
}

static int rkispp_set_fmt(struct rkispp_stream *stream,
			  struct v4l2_pix_format_mplane *pixm,
			  bool try)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_subdev *sdev = &dev->ispp_sdev;
	const struct capture_fmt *fmt;
	unsigned int imagsize = 0;
	unsigned int planes;
	u32 xsubs = 1, ysubs = 1;
	unsigned int i;

	if (stream->id == STREAM_VIR) {
		for (i = STREAM_MB; i <= STREAM_S2; i++) {
			struct rkispp_stream *t = &dev->stream_vdev.stream[i];

			if (t->out_cap_fmt.wr_fmt & FMT_FBC || !t->streaming)
				continue;
			if (t->out_fmt.plane_fmt[0].sizeimage > imagsize) {
				imagsize = t->out_fmt.plane_fmt[0].sizeimage;
				*pixm = t->out_fmt;
				stream->conn_id = t->id;
			}
		}
		if (!imagsize) {
			v4l2_err(&dev->v4l2_dev, "no output stream for iqtool\n");
			return -EINVAL;
		}
		imagsize = 0;
	}

	fmt = find_fmt(stream, pixm->pixelformat);
	if (!fmt) {
		v4l2_err(&dev->v4l2_dev,
			 "nonsupport pixelformat:%c%c%c%c\n",
			 pixm->pixelformat,
			 pixm->pixelformat >> 8,
			 pixm->pixelformat >> 16,
			 pixm->pixelformat >> 24);
		return -EINVAL;
	}

	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	if (!pixm->quantization)
		pixm->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	/* calculate size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;
	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		unsigned int width, height, bytesperline, w, h;

		plane_fmt = pixm->plane_fmt + i;

		w = (fmt->wr_fmt & FMT_FBC) ?
			ALIGN(pixm->width, 16) : pixm->width;
		h = (fmt->wr_fmt & FMT_FBC) ?
			ALIGN(pixm->height, 16) : pixm->height;
		width = i ? w / xsubs : w;
		height = i ? h / ysubs : h;

		bytesperline = width * DIV_ROUND_UP(fmt->bpp[i], 8);

		if (i != 0 || plane_fmt->bytesperline < bytesperline)
			plane_fmt->bytesperline = bytesperline;

		plane_fmt->sizeimage = plane_fmt->bytesperline * height;
		/* FBC header: width * height / 16, and 4096 align for mpp
		 * FBC payload: yuv420 or yuv422 size
		 * FBC width and height need 16 align
		 */
		if (fmt->wr_fmt & FMT_FBC && i == 0)
			plane_fmt->sizeimage =
				ALIGN(plane_fmt->sizeimage >> 4, RK_MPP_ALIGN);
		else if (fmt->wr_fmt & FMT_FBC)
			plane_fmt->sizeimage += w * h;
		imagsize += plane_fmt->sizeimage;
	}

	if (fmt->mplanes == 1)
		pixm->plane_fmt[0].sizeimage = imagsize;

	stream->is_reg_withstream = rkispp_is_reg_withstream_local(&stream->vnode.vdev.dev);
	if (stream->is_reg_withstream &&
	    (fmt->wr_fmt & FMT_FBC || fmt->wr_fmt == FMT_YUV420))
		pixm->num_planes++;

	if (!try) {
		stream->out_cap_fmt = *fmt;
		stream->out_fmt = *pixm;

		if (stream->id == STREAM_II && stream->linked) {
			sdev->in_fmt.width = pixm->width;
			sdev->in_fmt.height = pixm->height;
			sdev->out_fmt.width = pixm->width;
			sdev->out_fmt.height = pixm->height;
		}
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "%s: stream: %d req(%d, %d) out(%d, %d)\n",
			 __func__, stream->id, pixm->width, pixm->height,
			 stream->out_fmt.width, stream->out_fmt.height);

		if (sdev->out_fmt.width > RKISPP_MAX_WIDTH ||
		    sdev->out_fmt.height > RKISPP_MAX_HEIGHT ||
		    sdev->out_fmt.width < RKISPP_MIN_WIDTH ||
		    sdev->out_fmt.height < RKISPP_MIN_HEIGHT) {
			v4l2_err(&dev->v4l2_dev,
				 "ispp input min:%dx%d max:%dx%d\n",
				 RKISPP_MIN_WIDTH, RKISPP_MIN_HEIGHT,
				 RKISPP_MAX_WIDTH, RKISPP_MAX_HEIGHT);
			stream->out_fmt.width = 0;
			stream->out_fmt.height = 0;
			return -EINVAL;
		}
	}

	if (stream->ops && stream->ops->limit_check)
		return stream->ops->limit_check(stream, try ? pixm : NULL);

	return 0;
}

/************************* v4l2_file_operations***************************/

static int rkispp_fh_open(struct file *filp)
{
	struct rkispp_stream *stream = video_drvdata(filp);
	struct rkispp_device *isppdev = stream->isppdev;
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&stream->vnode.vdev.entity, 1);
		if (ret < 0) {
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power on failed %d\n", ret);
			vb2_fop_release(filp);
		}
	}
	return ret;
}

static int rkispp_fh_release(struct file *filp)
{
	struct rkispp_stream *stream = video_drvdata(filp);
	struct rkispp_device *isppdev = stream->isppdev;
	int ret;

	ret = vb2_fop_release(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&stream->vnode.vdev.entity, 0);
		if (ret < 0)
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power off failed %d\n", ret);
	}
	return ret;
}

static const struct v4l2_file_operations rkispp_fops = {
	.open = rkispp_fh_open,
	.release = rkispp_fh_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int rkispp_enum_input(struct file *file, void *priv,
			struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int rkispp_try_fmt_vid_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkispp_stream *stream = video_drvdata(file);

	return rkispp_set_fmt(stream, &f->fmt.pix_mp, true);
}

static int rkispp_enum_fmt_vid_mplane(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	struct rkispp_stream *stream = video_drvdata(file);
	const struct capture_fmt *fmt = NULL;

	if (f->index >= stream->config->fmt_size)
		return -EINVAL;

	fmt = &stream->config->fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int rkispp_s_fmt_vid_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkispp_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkispp_vdev_node *node = vdev_to_node(vdev);
	struct rkispp_device *dev = stream->isppdev;

	/* Change not allowed if queue is streaming. */
	if (vb2_is_streaming(&node->buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	return rkispp_set_fmt(stream, &f->fmt.pix_mp, false);
}

static int rkispp_g_fmt_vid_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkispp_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->out_fmt;

	return 0;
}

static int rkispp_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkispp_stream *stream = video_drvdata(file);
	struct device *dev = stream->isppdev->dev;
	struct video_device *vdev = video_devdata(file);

	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", dev->driver->name,
		 stream->isppdev->ispp_ver >> 4);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));

	return 0;
}

static const struct v4l2_ioctl_ops rkispp_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = rkispp_enum_input,
	.vidioc_try_fmt_vid_cap_mplane = rkispp_try_fmt_vid_mplane,
	.vidioc_enum_fmt_vid_cap_mplane = rkispp_enum_fmt_vid_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkispp_s_fmt_vid_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkispp_g_fmt_vid_mplane,
	.vidioc_try_fmt_vid_out_mplane = rkispp_try_fmt_vid_mplane,
	.vidioc_enum_fmt_vid_out_mplane = rkispp_enum_fmt_vid_mplane,
	.vidioc_s_fmt_vid_out_mplane = rkispp_s_fmt_vid_mplane,
	.vidioc_g_fmt_vid_out_mplane = rkispp_g_fmt_vid_mplane,
	.vidioc_querycap = rkispp_querycap,
};

static void rkispp_unregister_stream_video(struct rkispp_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

static int rkispp_register_stream_video(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkispp_vdev_node *node;
	enum v4l2_buf_type buf_type;
	int ret = 0;

	node = vdev_to_node(vdev);
	vdev->release = video_device_release_empty;
	vdev->fops = &rkispp_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &dev->apilock;
	video_set_drvdata(vdev, stream);

	vdev->ioctl_ops = &rkispp_v4l2_ioctl_ops;
	if (stream->type == STREAM_INPUT) {
		vdev->device_caps = V4L2_CAP_STREAMING |
			V4L2_CAP_VIDEO_OUTPUT_MPLANE;
		vdev->vfl_dir = VFL_DIR_TX;
		node->pad.flags = MEDIA_PAD_FL_SOURCE;
		buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else {
		vdev->device_caps = V4L2_CAP_STREAMING |
			V4L2_CAP_VIDEO_CAPTURE_MPLANE;
		vdev->vfl_dir = VFL_DIR_RX;
		node->pad.flags = MEDIA_PAD_FL_SINK;
		buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	}

	rkispp_init_vb2_queue(&node->buf_queue, stream, buf_type);
	vdev->queue = &node->buf_queue;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video register failed with error %d\n", ret);
		return ret;
	}

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto unreg;
	return 0;
unreg:
	video_unregister_device(vdev);
	return ret;
}

static void restart_module(struct rkispp_device *dev)
{
	struct rkispp_monitor *monitor = &dev->stream_vdev.monitor;
	void __iomem *base = dev->hw_dev->base_addr;
	u32 val = 0;

	monitor->retry++;
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s enter\n", __func__);
	if (dev->ispp_sdev.state == ISPP_STOP || monitor->retry > 3) {
		monitor->is_en = false;
		monitor->is_restart = false;
		goto end;
	}
	if (monitor->monitoring_module)
		wait_for_completion_timeout(&monitor->cmpl,
					    msecs_to_jiffies(500));
	if (dev->ispp_sdev.state == ISPP_STOP) {
		monitor->is_en = false;
		monitor->is_restart = false;
		goto end;
	}
	rkispp_soft_reset(dev->hw_dev);
	rkispp_update_regs(dev, RKISPP_CTRL_QUICK, RKISPP_FEC_CROP);
	writel(ALL_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
	if (monitor->restart_module & MONITOR_TNR) {
		val |= TNR_ST;
		rkispp_write(dev, RKISPP_TNR_IIR_Y_BASE,
			     rkispp_read(dev, RKISPP_TNR_WR_Y_BASE));
		rkispp_write(dev, RKISPP_TNR_IIR_UV_BASE,
			     rkispp_read(dev, RKISPP_TNR_WR_UV_BASE));
		monitor->monitoring_module |= MONITOR_TNR;
		if (!completion_done(&monitor->tnr.cmpl))
			complete(&monitor->tnr.cmpl);
	}
	if (monitor->restart_module & MONITOR_NR) {
		val |= NR_SHP_ST;
		monitor->monitoring_module |= MONITOR_NR;
		if (!completion_done(&monitor->nr.cmpl))
			complete(&monitor->nr.cmpl);
	}
	if (monitor->restart_module & MONITOR_FEC) {
		val |= FEC_ST;
		monitor->monitoring_module |= MONITOR_FEC;
		if (!completion_done(&monitor->fec.cmpl))
			complete(&monitor->fec.cmpl);
	}
	if (!dev->hw_dev->is_shutdown)
		writel(val, base + RKISPP_CTRL_STRT);
	monitor->is_restart = false;
	monitor->restart_module = 0;
end:
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s exit en:%d cnt:%d, monitoring:0x%x\n", __func__,
		 monitor->is_en, monitor->retry, monitor->monitoring_module);
}

static void restart_monitor(struct work_struct *work)
{
	struct module_monitor *m_monitor =
		container_of(work, struct module_monitor, work);
	struct rkispp_device *dev = m_monitor->dev;
	struct rkispp_monitor *monitor = &dev->stream_vdev.monitor;
	unsigned long lock_flags = 0;
	long time;
	int ret;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s module:0x%x enter\n", __func__, m_monitor->module);
	while (monitor->is_en) {
		/* max timeout for module idle */
		time = MAX_SCHEDULE_TIMEOUT;
		if (monitor->monitoring_module & m_monitor->module)
			time = (m_monitor->time <= 0 ? 300 : m_monitor->time) + 150;
		ret = wait_for_completion_timeout(&m_monitor->cmpl,
						  msecs_to_jiffies(time));
		if (!(monitor->monitoring_module & m_monitor->module) ||
		    ret || !monitor->is_en)
			continue;
		if (dev->hw_dev->is_shutdown)
			break;
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "module:0x%x wait %ldms timeout ret:%d, monitoring:0x%x\n",
			 m_monitor->module, time, ret, monitor->monitoring_module);

		spin_lock_irqsave(&monitor->lock, lock_flags);
		monitor->monitoring_module &= ~m_monitor->module;
		monitor->restart_module |= m_monitor->module;
		if (monitor->is_restart)
			ret = true;
		else
			monitor->is_restart = true;
		if (m_monitor->module == MONITOR_TNR) {
			rkispp_write(dev, RKISPP_TNR_IIR_Y_BASE,
				     readl(dev->hw_dev->base_addr + RKISPP_TNR_IIR_Y_BASE_SHD));
			rkispp_write(dev, RKISPP_TNR_IIR_UV_BASE,
				     readl(dev->hw_dev->base_addr + RKISPP_TNR_IIR_UV_BASE_SHD));
		}
		spin_unlock_irqrestore(&monitor->lock, lock_flags);
		if (!ret && monitor->is_restart)
			restart_module(dev);
		/* waitting for other working module if need restart ispp */
		if (monitor->is_restart &&
		    !monitor->monitoring_module &&
		    !completion_done(&monitor->cmpl))
			complete(&monitor->cmpl);
	}
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s module:0x%x exit\n", __func__, m_monitor->module);
}

static void monitor_init(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_monitor *monitor = &vdev->monitor;

	monitor->tnr.dev = dev;
	monitor->nr.dev = dev;
	monitor->fec.dev = dev;
	monitor->tnr.module = MONITOR_TNR;
	monitor->nr.module = MONITOR_NR;
	monitor->fec.module = MONITOR_FEC;
	INIT_WORK(&monitor->tnr.work, restart_monitor);
	INIT_WORK(&monitor->nr.work, restart_monitor);
	INIT_WORK(&monitor->fec.work, restart_monitor);
	init_completion(&monitor->tnr.cmpl);
	init_completion(&monitor->nr.cmpl);
	init_completion(&monitor->fec.cmpl);
	init_completion(&monitor->cmpl);
	spin_lock_init(&monitor->lock);
	monitor->is_restart = false;
}

static void fec_work_event(struct rkispp_device *dev,
			   struct rkispp_dummy_buffer *buf_rd,
			   bool is_isr)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_monitor *monitor = &vdev->monitor;
	struct list_head *list = &vdev->fec.list_rd;
	void __iomem *base = dev->hw_dev->base_addr;
	struct rkispp_dummy_buffer *dummy;
	struct rkispp_stream *stream;
	unsigned long lock_flags = 0, lock_flags1 = 0;
	bool is_start = false, is_quick = false;
	struct rkisp_ispp_reg *reg_buf = NULL;
	u32 val;

	if (!(vdev->module_ens & ISPP_MODULE_FEC))
		return;

	if (dev->inp == INP_ISP && dev->isp_mode & ISP_ISPP_QUICK)
		is_quick = true;

	spin_lock_irqsave(&vdev->fec.buf_lock, lock_flags);

	/* event from fec frame end */
	if (!buf_rd && is_isr) {
		vdev->fec.is_end = true;

		if (vdev->fec.cur_rd)
			rkispp_module_work_event(dev, NULL, vdev->fec.cur_rd,
						 ISPP_MODULE_NR, false);
		vdev->fec.cur_rd = NULL;
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
		vdev->fec.cur_rd = get_list_buf(list, false);
		if (buf_rd)
			list_add_tail(&buf_rd->list, list);
	} else if (!vdev->fec.is_end && buf_rd) {
		/* fec no idle
		 * new read buf from nr into list
		 */
		list_add_tail(&buf_rd->list, list);
	}

	if (vdev->fec.cur_rd && vdev->fec.is_end) {
		dummy = vdev->fec.cur_rd;
		val = dummy->dma_addr;
		rkispp_write(dev, RKISPP_FEC_RD_Y_BASE, val);
		val += vdev->fec.uv_offset;
		rkispp_write(dev, RKISPP_FEC_RD_UV_BASE, val);
		is_start = true;
	}

	if (is_start) {
		u32 seq = 0;

		if (vdev->fec.cur_rd && !is_quick) {
			seq = vdev->fec.cur_rd->id;
			dev->ispp_sdev.frame_timestamp =
				vdev->fec.cur_rd->timestamp;
			dev->ispp_sdev.frm_sync_seq = seq;
		}

		stream = &vdev->stream[STREAM_MB];
		if (stream->streaming && !stream->is_cfg)
			secure_config_mb(stream);

		if (!dev->hw_dev->is_single)
			rkispp_update_regs(dev, RKISPP_FEC, RKISPP_FEC_CROP);
		writel(FEC_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
		if (vdev->nr.is_end) {
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
			 "FEC start seq:%d | Y_SHD rd:0x%x\n",
			 seq, readl(base + RKISPP_FEC_RD_Y_BASE_SHD));

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

		if (!dev->hw_dev->is_shutdown)
			writel(FEC_ST, base + RKISPP_CTRL_STRT);
		vdev->fec.is_end = false;
	}
restart_unlock:
	spin_unlock_irqrestore(&monitor->lock, lock_flags1);
	spin_unlock_irqrestore(&vdev->fec.buf_lock, lock_flags);
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
			if (dev->hw_dev->is_first && dev->hw_dev->first_frame_dma != -1) {
				val = dev->hw_dev->first_frame_dma;
				dev->hw_dev->first_frame_dma = -1;
			}
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
			}
			if (!is_fec_en && !is_quick) {
				dev->ispp_sdev.frame_timestamp = timestamp;
				dev->ispp_sdev.frm_sync_seq = seq;
			}
		}

		stream = &vdev->stream[STREAM_MB];
		if (!is_fec_en && stream->streaming && !stream->is_cfg)
			secure_config_mb(stream);

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

		for (val = STREAM_S0; val <= STREAM_S2; val++) {
			stream = &vdev->stream[val];
			if (stream->stopping && stream->ops->stop)
				stream->ops->stop(stream);
		}

		vdev->nr.dbg.id = seq;
		vdev->nr.dbg.timestamp = ktime_get_ns();
		if (monitor->is_en) {
			monitor->nr.time = vdev->nr.dbg.interval / 1000 / 1000;
			monitor->monitoring_module |= MONITOR_NR;
			if (!completion_done(&monitor->nr.cmpl))
				complete(&monitor->nr.cmpl);
		}

		if (stream->is_reg_withstream)
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

		if (!is_quick && !dev->hw_dev->is_shutdown)
			writel(NR_SHP_ST, base + RKISPP_CTRL_STRT);
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

		if (dev->hw_dev->is_first && vdev->tnr.nxt_rd) {
			struct rkispp_isp_buf_pool *tbuf = get_pool_buf(dev, vdev->tnr.nxt_rd);

			dev->hw_dev->first_frame_dma = tbuf->dma[GROUP_BUF_PIC];
		}

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
			if (!completion_done(&monitor->tnr.cmpl))
				complete(&monitor->tnr.cmpl);
		}

		if (stream->is_reg_withstream)
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

void rkispp_module_work_event(struct rkispp_device *dev,
			      void *buf_rd, void *buf_wr,
			      u32 module, bool is_isr)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;

	if (dev->hw_dev->is_shutdown)
		return;

	if (dev->ispp_sdev.state != ISPP_STOP) {
		if (module & ISPP_MODULE_TNR)
			tnr_work_event(dev, buf_rd, buf_wr, is_isr);
		else if (module & ISPP_MODULE_NR)
			nr_work_event(dev, buf_rd, buf_wr, is_isr);
		else
			fec_work_event(dev, buf_rd, is_isr);
	}

	/* cur frame (tnr->nr->fec) done for next frame
	 * fec start at nr end if fec enable, and fec can async with
	 * tnr different frames for single device.
	 * tnr->nr->fec frame0
	 *       |->tnr->nr->fec frame1
	 */
	if (is_isr && !buf_rd && !buf_wr &&
	    ((module == ISPP_MODULE_FEC && !dev->hw_dev->is_single) ||
	     (module == ISPP_MODULE_NR && (dev->hw_dev->is_single || vdev->fec.is_end)))) {
		dev->stream_vdev.monitor.retry = 0;
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

void rkispp_isr(u32 mis_val, struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream;
	u32 i, err_mask = NR_LOST_ERR | TNR_LOST_ERR |
		FBCH_EMPTY_NR | FBCH_EMPTY_TNR | FBCD_DEC_ERR_NR |
		FBCD_DEC_ERR_TNR | BUS_ERR_NR | BUS_ERR_TNR;
	u64 ns = ktime_get_ns();

	v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
		 "isr:0x%x\n", mis_val);

	vdev = &dev->stream_vdev;
	dev->isr_cnt++;
	if (mis_val & err_mask) {
		dev->isr_err_cnt++;
		v4l2_err(&dev->v4l2_dev,
			 "ispp err:0x%x\n", mis_val);
	}

	if (mis_val & TNR_INT) {
		if (vdev->monitor.is_en) {
			vdev->monitor.monitoring_module &= ~MONITOR_TNR;
			if (!completion_done(&vdev->monitor.tnr.cmpl))
				complete(&vdev->monitor.tnr.cmpl);
		}
		vdev->tnr.dbg.interval = ns - vdev->tnr.dbg.timestamp;
	}
	if (mis_val & NR_INT) {
		if (vdev->monitor.is_en) {
			vdev->monitor.monitoring_module &= ~MONITOR_NR;
			if (!completion_done(&vdev->monitor.nr.cmpl))
				complete(&vdev->monitor.nr.cmpl);
		}
		vdev->nr.dbg.interval = ns - vdev->nr.dbg.timestamp;
	}
	if (mis_val & FEC_INT) {
		if (vdev->monitor.is_en) {
			vdev->monitor.monitoring_module &= ~MONITOR_FEC;
			if (!completion_done(&vdev->monitor.fec.cmpl))
				complete(&vdev->monitor.fec.cmpl);
		}
		vdev->fec.dbg.interval = ns - vdev->fec.dbg.timestamp;
	}

	if (mis_val & (CMD_TNR_ST_DONE | CMD_NR_SHP_ST_DONE) &&
	    (dev->isp_mode & ISP_ISPP_QUICK))
		++dev->ispp_sdev.frm_sync_seq;

	if (mis_val & TNR_INT)
		if (rkispp_read(dev, RKISPP_TNR_CTRL) & SW_TNR_1ST_FRM)
			rkispp_clear_bits(dev, RKISPP_TNR_CTRL, SW_TNR_1ST_FRM);

	rkispp_stats_isr(&dev->stats_vdev, mis_val);

	for (i = 0; i <= STREAM_S2; i++) {
		stream = &vdev->stream[i];

		if (!stream->streaming || !stream->is_cfg ||
		    !(mis_val & INT_FRAME(stream)))
			continue;
		if (stream->stopping &&
		    stream->ops->is_stopped &&
		    stream->ops->is_stopped(stream)) {
			stream->stopping = false;
			stream->streaming = false;
			stream->is_upd = false;
			wake_up(&stream->done);
		} else if (i != STREAM_II) {
			rkispp_frame_end(stream);
		}
	}

	if (mis_val & NR_INT && dev->hw_dev->is_first) {
		dev->mis_val = mis_val;
		INIT_WORK(&dev->irq_work, irq_work);
		schedule_work(&dev->irq_work);
	} else {
		check_to_force_update(dev, mis_val);
	}
}

int rkispp_register_stream_vdevs(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *stream_vdev;
	struct rkispp_stream *stream;
	struct video_device *vdev;
	char *vdev_name;
	int i, j, ret = 0;

	stream_vdev = &dev->stream_vdev;
	memset(stream_vdev, 0, sizeof(*stream_vdev));
	atomic_set(&stream_vdev->refcnt, 0);
	INIT_LIST_HEAD(&stream_vdev->tnr.list_rd);
	INIT_LIST_HEAD(&stream_vdev->tnr.list_wr);
	INIT_LIST_HEAD(&stream_vdev->tnr.list_rpt);
	INIT_LIST_HEAD(&stream_vdev->nr.list_rd);
	INIT_LIST_HEAD(&stream_vdev->nr.list_wr);
	INIT_LIST_HEAD(&stream_vdev->fec.list_rd);
	spin_lock_init(&stream_vdev->tnr.buf_lock);
	spin_lock_init(&stream_vdev->nr.buf_lock);
	spin_lock_init(&stream_vdev->fec.buf_lock);
	stream_vdev->tnr.is_but_init = false;

	for (i = 0; i < STREAM_MAX; i++) {
		stream = &stream_vdev->stream[i];
		stream->id = i;
		stream->isppdev = dev;
		INIT_LIST_HEAD(&stream->buf_queue);
		init_waitqueue_head(&stream->done);
		spin_lock_init(&stream->vbq_lock);
		vdev = &stream->vnode.vdev;
		switch (i) {
		case STREAM_II:
			vdev_name = II_VDEV_NAME;
			stream->type = STREAM_INPUT;
			stream->ops = &input_stream_ops;
			stream->config = &input_config;
			break;
		case STREAM_MB:
			vdev_name = MB_VDEV_NAME;
			stream->type = STREAM_OUTPUT;
			stream->ops = &mb_stream_ops;
			stream->config = &mb_config;
			break;
		case STREAM_S0:
			vdev_name = S0_VDEV_NAME;
			stream->type = STREAM_OUTPUT;
			stream->ops = &scal_stream_ops;
			stream->config = &scl0_config;
			break;
		case STREAM_S1:
			vdev_name = S1_VDEV_NAME;
			stream->type = STREAM_OUTPUT;
			stream->ops = &scal_stream_ops;
			stream->config = &scl1_config;
			break;
		case STREAM_S2:
			vdev_name = S2_VDEV_NAME;
			stream->type = STREAM_OUTPUT;
			stream->ops = &scal_stream_ops;
			stream->config = &scl2_config;
			break;
		case STREAM_VIR:
			vdev_name = VIR_VDEV_NAME;
			stream->type = STREAM_OUTPUT;
			stream->config = &input_config;
			stream->ops = NULL;
			break;
		default:
			v4l2_err(&dev->v4l2_dev, "Invalid stream:%d\n", i);
			return -EINVAL;
		}
		strlcpy(vdev->name, vdev_name, sizeof(vdev->name));
		ret = rkispp_register_stream_video(stream);
		if (ret < 0)
			goto err;
	}
	monitor_init(dev);
	return 0;
err:
	for (j = 0; j < i; j++) {
		stream = &stream_vdev->stream[j];
		rkispp_unregister_stream_video(stream);
	}
	return ret;
}

void rkispp_unregister_stream_vdevs(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *stream_vdev;
	struct rkispp_stream *stream;
	int i;

	stream_vdev = &dev->stream_vdev;
	for (i = 0; i < STREAM_MAX; i++) {
		stream = &stream_vdev->stream[i];
		rkispp_unregister_stream_video(stream);
	}
}
