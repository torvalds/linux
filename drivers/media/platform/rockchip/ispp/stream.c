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

#define STREAM_IN_REQ_BUFS_MIN 1
#define STREAM_OUT_REQ_BUFS_MIN 0

/* memory align for mpp */
#define RK_MPP_ALIGN 4096

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

static void irq_work(struct work_struct *work)
{
	struct rkispp_device *dev = container_of(work, struct rkispp_device, irq_work);

	rkispp_set_clk_rate(dev->hw_dev->clks[0], dev->hw_dev->core_clk_max);
	dev->stream_vdev.stream_ops->check_to_force_update(dev, dev->mis_val);
	dev->hw_dev->is_first = false;
}

void get_stream_buf(struct rkispp_stream *stream)
{
	unsigned long lock_flags = 0;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (!list_empty(&stream->buf_queue) && !stream->curr_buf) {
		stream->curr_buf =
			list_first_entry(&stream->buf_queue,
					 struct rkispp_buffer, queue);
		list_del(&stream->curr_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

int rkispp_frame_end(struct rkispp_stream *stream, u32 state)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct capture_fmt *fmt = &stream->out_cap_fmt;
	struct rkisp_ispp_reg *reg_buf = NULL;
	unsigned long lock_flags = 0;
	int i = 0;

	if (state == FRAME_IRQ && dev->stream_vdev.is_done_early)
		return 0;

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
	} else {
		u32 frame_id = dev->ispp_sdev.frm_sync_seq;

		if (stream->is_cfg) {
			stream->dbg.frameloss++;
			v4l2_dbg(0, rkispp_debug, &dev->v4l2_dev,
				 "stream:%d no buf, lost frame:%d\n",
				 stream->id, frame_id);
		}

		if (stream->is_reg_withstream &&
		    (fmt->wr_fmt & FMT_FBC || fmt->wr_fmt == FMT_YUV420)) {
			rkispp_find_regbuf_by_id(dev, &reg_buf, dev->dev_id, frame_id);
			if (reg_buf) {
				rkispp_release_regbuf(dev, reg_buf);
				v4l2_info(&dev->v4l2_dev,
					  "%s: current frame use dummy buffer(dev_id %d, sequence %d)\n",
					  __func__, dev->dev_id, frame_id);
			}
		}
	}

	get_stream_buf(stream);
	vdev->stream_ops->update_mi(stream);
	return 0;
}

void *get_pool_buf(struct rkispp_device *dev,
			  struct rkisp_ispp_buf *dbufs)
{
	int i;

	for (i = 0; i < RKISPP_BUF_POOL_MAX; i++)
		if (dev->hw_dev->pool[i].dbufs == dbufs)
			return &dev->hw_dev->pool[i];

	return NULL;
}

void *dbuf_to_dummy(struct dma_buf *dbuf,
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

void *get_list_buf(struct list_head *list, bool is_isp_ispp)
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

void rkispp_start_3a_run(struct rkispp_device *dev)
{
	struct rkispp_params_vdev *params_vdev;
	struct video_device *vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_START,
	};
	int ret;

	if (dev->ispp_ver == ISPP_V10)
		params_vdev = &dev->params_vdev[PARAM_VDEV_NR];
	else
		params_vdev = &dev->params_vdev[PARAM_VDEV_FEC];
	if (!params_vdev->is_subs_evt)
		return;
	vdev = &params_vdev->vnode.vdev;
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
	struct rkispp_params_vdev *params_vdev;
	struct video_device *vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_STOP,
	};
	int ret;

	if (dev->ispp_ver == ISPP_V10)
		params_vdev = &dev->params_vdev[PARAM_VDEV_NR];
	else
		params_vdev = &dev->params_vdev[PARAM_VDEV_FEC];
	if (!params_vdev->is_subs_evt)
		return;
	vdev = &params_vdev->vnode.vdev;
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
	struct rkispp_stream_vdev *stream_vdev = &stream->isppdev->stream_vdev;

	stream->is_cfg = true;
	rkispp_start_3a_run(stream->isppdev);
	return stream_vdev->stream_ops->config_modules(stream->isppdev);
}

static int is_stopped_ii(struct rkispp_stream *stream)
{
	stream->streaming = false;
	return true;
}

void secure_config_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	u32 limit_range, mult = 1;

	/* enable dma immediately, config in idle state */
	switch (stream->last_module) {
	case ISPP_MODULE_TNR:
		rkispp_set_bits(dev, RKISPP_TNR_CTRL, FMT_WR_MASK,
				SW_TNR_1ST_FRM | stream->out_cap_fmt.wr_fmt << 4);
		break;
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
		rkispp_write(dev, RKISPP_FEC_DST_SIZE,
			     stream->out_fmt.height << 16 | stream->out_fmt.width);
		rkispp_clear_bits(dev, RKISPP_FEC_CORE_CTRL, SW_FEC2DDR_DIS);
		break;
	default:
		break;
	}

	if (stream->out_cap_fmt.wr_fmt & FMT_YUYV)
		mult = 2;
	else if (stream->out_cap_fmt.wr_fmt & FMT_FBC)
		mult = 0;
	set_vir_stride(stream, ALIGN(stream->out_fmt.width * mult, 16) >> 2);

	/* config first buf */
	rkispp_frame_end(stream, FRAME_INIT);

	stream->is_cfg = true;
}

static int config_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	u32 i;

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
		if (dev->inp == INP_ISP)
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
	rkispp_frame_end(stream, FRAME_INIT);
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
		if (stream->isppdev->hw_dev->is_dma_sg_ops) {
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
			if (cap_fmt->wr_fmt & FMT_FBC && dev->ispp_ver == ISPP_V20)
				rkispp_write(dev, RKISPP_FEC_FBCE_HEAD_OFFSET,
					     offset | SW_OFFSET_ENABLE);

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
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;

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
	vdev->stream_ops->destroy_buf(stream);
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

static int rkispp_start_streaming(struct vb2_queue *queue,
				  unsigned int count)
{
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
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

	if (!atomic_read(&hw->refcnt) &&
	    !atomic_read(&dev->stream_vdev.refcnt) &&
	    clk_get_rate(hw->clks[0]) <= hw->core_clk_min &&
	    (dev->inp == INP_DDR || dev->ispp_ver == ISPP_V20)) {
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

	if (dev->inp == INP_ISP) {
		if (dev->ispp_ver == ISPP_V10)
			dev->stream_vdev.module_ens |= ISPP_MODULE_NR;
		else if (dev->ispp_ver == ISPP_V20)
			dev->stream_vdev.module_ens = ISPP_MODULE_FEC;
	}

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
	ret = vdev->stream_ops->start_isp(dev);
	if (ret)
		goto free_dummy_buf;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d exit\n", __func__, stream->id);
	return 0;
free_dummy_buf:
	rkispp_free_common_dummy_buf(stream->isppdev);
free_buf_queue:
	destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
	vdev->stream_ops->destroy_buf(stream);
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
	q->bidirectional = 1;
	if (stream->isppdev->hw_dev->is_dma_contig)
		q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	q->gfp_flags = GFP_DMA32;
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

		if (pixm->width == RKISPP_MAX_WIDTH_V20) {
			w = ALIGN(pixm->width, 16);
			h = ALIGN(pixm->height, 16);
		} else {
			w = (fmt->wr_fmt & FMT_FBC) ?
				ALIGN(pixm->width, 16) : pixm->width;
			h = (fmt->wr_fmt & FMT_FBC) ?
				ALIGN(pixm->height, 16) : pixm->height;
		}

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

		if (dev->ispp_ver == ISPP_V10) {
			if (sdev->out_fmt.width > RKISPP_MAX_WIDTH_V10 ||
			sdev->out_fmt.height > RKISPP_MAX_HEIGHT_V10 ||
			sdev->out_fmt.width < RKISPP_MIN_WIDTH_V10 ||
			sdev->out_fmt.height < RKISPP_MIN_HEIGHT_V10) {
				v4l2_err(&dev->v4l2_dev,
					"ispp input min:%dx%d max:%dx%d\n",
					RKISPP_MIN_WIDTH_V10, RKISPP_MIN_HEIGHT_V10,
					RKISPP_MAX_WIDTH_V10, RKISPP_MAX_HEIGHT_V10);
				stream->out_fmt.width = 0;
				stream->out_fmt.height = 0;
				return -EINVAL;
			}
		} else if (dev->ispp_ver == ISPP_V20) {
			if (sdev->out_fmt.width > RKISPP_MAX_WIDTH_V20 ||
			sdev->out_fmt.height > RKISPP_MAX_HEIGHT_V20 ||
			sdev->out_fmt.width < RKISPP_MIN_WIDTH_V20 ||
			sdev->out_fmt.height < RKISPP_MIN_HEIGHT_V20) {
				v4l2_err(&dev->v4l2_dev,
					"ispp input min:%dx%d max:%dx%d\n",
					RKISPP_MIN_WIDTH_V20, RKISPP_MIN_HEIGHT_V20,
					RKISPP_MAX_WIDTH_V20, RKISPP_MAX_HEIGHT_V20);
				stream->out_fmt.width = 0;
				stream->out_fmt.height = 0;
				return -EINVAL;
			}
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
		ret = v4l2_pipeline_pm_get(&stream->vnode.vdev.entity);
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
	int ret;

	ret = vb2_fop_release(filp);
	if (!ret)
		v4l2_pipeline_pm_put(&stream->vnode.vdev.entity);
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
	switch (f->pixelformat) {
	case V4L2_PIX_FMT_FBC2:
		strscpy(f->description,
			"Rockchip yuv422sp fbc encoder",
			sizeof(f->description));
		break;
	case V4L2_PIX_FMT_FBC0:
		strscpy(f->description,
			"Rockchip yuv420sp fbc encoder",
			sizeof(f->description));
		break;
	default:
		break;
	}
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
	.vidioc_enum_fmt_vid_cap = rkispp_enum_fmt_vid_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkispp_s_fmt_vid_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkispp_g_fmt_vid_mplane,
	.vidioc_try_fmt_vid_out_mplane = rkispp_try_fmt_vid_mplane,
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

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
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

static void dump_file(struct rkispp_device *dev, u32 restart_module)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	void __iomem *base = dev->hw_dev->base_addr;
	struct rkispp_isp_buf_pool *buf;
	struct rkispp_dummy_buffer *dummy;
	struct file *fp = NULL;
	char file[160], reg[48];
	int i;

	snprintf(file, sizeof(file), "%s/%s%d.reg",
		 rkispp_dump_path, DRIVER_NAME, dev->dev_id);
	fp = filp_open(file, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		v4l2_err(&dev->v4l2_dev, "%s open %s fail\n", __func__, file);
		return;
	}
	for (i = 0; i < 0x1000; i += 16) {
		snprintf(reg, sizeof(reg), "ffb6%04x:  %08x %08x %08x %08x\n",
			 i, readl(base + i), readl(base + i + 4),
			 readl(base + i + 8), readl(base + i + 12));
		kernel_write(fp, reg, strlen(reg), &fp->f_pos);
	}
	filp_close(fp, NULL);

	if (restart_module & MONITOR_TNR) {
		if (vdev->tnr.cur_rd) {
			snprintf(file, sizeof(file), "%s/%s%d_tnr_cur.fbc",
				 rkispp_dump_path, DRIVER_NAME, dev->dev_id);
			fp = filp_open(file, O_RDWR | O_CREAT, 0644);
			if (IS_ERR(fp)) {
				v4l2_err(&dev->v4l2_dev,
					 "%s open %s fail\n", __func__, file);
				return;
			}
			buf = get_pool_buf(dev, vdev->tnr.cur_rd);
			kernel_write(fp, buf->vaddr[0], vdev->tnr.cur_rd->dbuf[0]->size, &fp->f_pos);
			filp_close(fp, NULL);
			v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
				 "dump tnr cur_rd dma:%pad vaddr:%p\n",
				 &buf->dma[0], buf->vaddr[0]);
		}

		if (vdev->tnr.nxt_rd && vdev->tnr.nxt_rd != vdev->tnr.cur_rd) {
			snprintf(file, sizeof(file), "%s/%s%d_tnr_nxt.fbc",
				 rkispp_dump_path, DRIVER_NAME, dev->dev_id);
			fp = filp_open(file, O_RDWR | O_CREAT, 0644);
			if (IS_ERR(fp)) {
				v4l2_err(&dev->v4l2_dev,
					 "%s open %s fail\n", __func__, file);
				return;
			}
			buf = get_pool_buf(dev, vdev->tnr.nxt_rd);
			kernel_write(fp, buf->vaddr[0], vdev->tnr.nxt_rd->dbuf[0]->size, &fp->f_pos);
			filp_close(fp, NULL);
			v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
				 "dump tnr nxt_rd dma:%pad vaddr:%p\n",
				 &buf->dma[0], buf->vaddr[0]);
		}
	}

	if (!(restart_module & MONITOR_FEC)) {
		for (i = 0; i < RKISPP_BUF_MAX; i++) {
			dummy = &vdev->tnr.buf.wr[i][0];
			if (!dummy->mem_priv)
				break;
			snprintf(file, sizeof(file), "%s/%s%d_iir%d.fbc",
				 rkispp_dump_path, DRIVER_NAME, dev->dev_id, i);
			fp = filp_open(file, O_RDWR | O_CREAT, 0644);
			if (IS_ERR(fp)) {
				v4l2_err(&dev->v4l2_dev,
					 "%s open %s fail\n", __func__, file);
				return;
			}
			kernel_write(fp, dummy->vaddr, dummy->size, &fp->f_pos);
			filp_close(fp, NULL);
			v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
				 "dump tnr wr dma:%pad vaddr:%p\n",
				 &dummy->dma_addr, dummy->vaddr);
		}
	}
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
		monitor->is_restart = false;
		monitor->is_en = false;
		goto end;
	}
	if (monitor->monitoring_module)
		wait_for_completion_timeout(&monitor->cmpl,
					    msecs_to_jiffies(500));
	if (dev->ispp_sdev.state == ISPP_STOP) {
		monitor->is_restart = false;
		monitor->is_en = false;
		goto end;
	}

	if (rkispp_dump_path[0] == '/')
		dump_file(dev, monitor->restart_module);

	if (monitor->restart_module & MONITOR_TNR && monitor->tnr.is_err) {
		rkispp_set_bits(dev, RKISPP_TNR_CTRL, 0, SW_TNR_1ST_FRM);
		monitor->tnr.is_err = false;
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
		if (monitor->nr.is_err) {
			struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
			struct v4l2_subdev *sd = dev->ispp_sdev.remote_sd;
			struct rkispp_buffer *inbuf;

			if (vdev->nr.cur_rd) {
				if (vdev->nr.cur_rd->is_isp) {
					v4l2_subdev_call(sd, video, s_rx_buffer,
							 vdev->nr.cur_rd, NULL);
				} else if (!vdev->nr.cur_rd->priv) {
					list_add_tail(&vdev->nr.cur_rd->list,
						      &vdev->tnr.list_wr);
				} else {
					inbuf = vdev->nr.cur_rd->priv;
					vb2_buffer_done(&inbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
				}
				vdev->nr.cur_rd = NULL;
			}
			rkispp_set_bits(dev, RKISPP_TNR_CTRL, 0, SW_TNR_1ST_FRM);
			vdev->nr.is_end = true;
			monitor->nr.is_err = false;
			monitor->is_restart = false;
			monitor->restart_module = 0;
			rkispp_event_handle(dev, CMD_QUEUE_DMABUF, NULL);
			goto end;
		}
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
		if (dev->hw_dev->is_shutdown || dev->ispp_sdev.state == ISPP_STOP)
			break;
		if (!(monitor->monitoring_module & m_monitor->module) ||
		    ret || !monitor->is_en)
			continue;
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

static enum hrtimer_restart rkispp_fec_do_early(struct hrtimer *timer)
{
	struct rkispp_stream_vdev *vdev =
		container_of(timer, struct rkispp_stream_vdev, fec_qst);
	struct rkispp_stream *stream = &vdev->stream[0];
	struct rkispp_device *dev = stream->isppdev;
	void __iomem *base = dev->hw_dev->base_addr;
	enum hrtimer_restart ret = HRTIMER_NORESTART;
	u32 ycnt, tile = readl(base + RKISPP_CTRL_SYS_CTL_STA0);
	u32 working = readl(base + RKISPP_CTRL_SYS_STATUS);
	u64 ns = ktime_get_ns();
	u32 time;

	working &= NR_WORKING;
	tile &= NR_TILE_LINE_CNT_MASK;
	ycnt = tile >> 8;
	time = (u32)(ns - vdev->nr.dbg.timestamp);
	if (dev->ispp_sdev.state == ISPP_STOP) {
		vdev->is_done_early = false;
		goto end;
	} else if (working && !ycnt) {
		hrtimer_forward(timer, timer->base->get_time(), ns_to_ktime(500000));
		ret = HRTIMER_RESTART;
	} else {
		v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
			 "%s seq:%d ycnt:%d time:%dus\n",
			 __func__, vdev->nr.dbg.id, ycnt * 128, time / 1000);
		vdev->stream_ops->fec_work_event(dev, NULL, false, true);
	}
end:
	return ret;
}

void rkispp_isr(u32 mis_val, struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream;
	u32 i, nr_err = NR_LOST_ERR | FBCH_EMPTY_NR |
		FBCD_DEC_ERR_NR | BUS_ERR_NR;
	u32 tnr_err = TNR_LOST_ERR | FBCH_EMPTY_TNR |
		FBCD_DEC_ERR_TNR | BUS_ERR_TNR;
	u64 ns = ktime_get_ns();

	v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
		 "isr:0x%x\n", mis_val);

	vdev = &dev->stream_vdev;
	dev->isr_cnt++;
	if (mis_val & (tnr_err | nr_err)) {
		if (mis_val & tnr_err)
			vdev->monitor.tnr.is_err = true;
		if (mis_val & nr_err)
			vdev->monitor.nr.is_err = true;
		dev->isr_err_cnt++;
		v4l2_err(&dev->v4l2_dev,
			 "ispp err:0x%x, seq:%d\n",
			 mis_val, dev->ispp_sdev.frm_sync_seq);
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

	if (mis_val & TNR_INT) {
		if (rkispp_read(dev, RKISPP_TNR_CTRL) & SW_TNR_1ST_FRM)
			rkispp_clear_bits(dev, RKISPP_TNR_CTRL, SW_TNR_1ST_FRM);
		rkispp_stats_isr(&dev->stats_vdev[STATS_VDEV_TNR]);
	}
	if (mis_val & NR_INT)
		rkispp_stats_isr(&dev->stats_vdev[STATS_VDEV_NR]);

	for (i = 0; i <= STREAM_S2; i++) {
		stream = &vdev->stream[i];

		if (!stream->streaming || !stream->is_cfg ||
		    !(mis_val & INT_FRAME(stream)))
			continue;
		if (stream->stopping &&
		    stream->ops->is_stopped &&
		    (stream->ops->is_stopped(stream) ||
		     dev->ispp_sdev.state == ISPP_STOP)) {
			stream->stopping = false;
			stream->streaming = false;
			stream->is_upd = false;
			wake_up(&stream->done);
		} else if (i != STREAM_II) {
			rkispp_frame_end(stream, FRAME_IRQ);
		}
	}

	if (mis_val & NR_INT && dev->hw_dev->is_first) {
		dev->mis_val = mis_val;
		INIT_WORK(&dev->irq_work, irq_work);
		schedule_work(&dev->irq_work);
	} else {
		vdev->stream_ops->check_to_force_update(dev, mis_val);
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
	INIT_LIST_HEAD(&stream_vdev->nr.list_rpt);
	INIT_LIST_HEAD(&stream_vdev->fec.list_rd);
	spin_lock_init(&stream_vdev->tnr.buf_lock);
	spin_lock_init(&stream_vdev->nr.buf_lock);
	spin_lock_init(&stream_vdev->fec.buf_lock);
	stream_vdev->tnr.is_buf_init = false;
	stream_vdev->nr.is_buf_init = false;

	if (dev->ispp_ver == ISPP_V10) {
		dev->stream_max = STREAM_MAX;
		rkispp_stream_init_ops_v10(stream_vdev);
		hrtimer_init(&stream_vdev->fec_qst, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		stream_vdev->fec_qst.function = rkispp_fec_do_early;
		hrtimer_init(&stream_vdev->frame_qst, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		stream_vdev->frame_qst.function = stream_vdev->stream_ops->rkispp_frame_done_early;
		dev->hw_dev->pool[0].group_buf_max = GROUP_BUF_MAX;
	} else if (dev->ispp_ver == ISPP_V20) {
		dev->stream_max = STREAM_VIR + 1;
		rkispp_stream_init_ops_v20(stream_vdev);
		dev->hw_dev->pool[0].group_buf_max = GROUP_BUF_GAIN;
	}
	for (i = 0; i < dev->stream_max; i++) {
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
	for (i = 0; i < dev->stream_max; i++) {
		stream = &stream_vdev->stream[i];
		rkispp_unregister_stream_video(stream);
	}
}
