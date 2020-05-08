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
		.wr_fmt = FMT_YUV420,
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

static void check_to_force_update(struct rkispp_device *dev, u32 mis_val)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream = &vdev->stream[STREAM_MB];
	void __iomem *base = dev->base_addr;
	u32 reg_y, reg_uv, is_switch = false;

	if (!stream->streaming)
		return;

	/* nr or fec input buffer is pingpong when work on frame mode
	 * need to keep address static after force update
	 */
	if (dev->inp == INP_DDR &&
	    (!(vdev->module_ens & ISPP_MODULE_TNR) ||
	     vdev->module_ens == ISPP_MODULE_FEC))
		is_switch = true;

	if ((stream->last_module == ISPP_MODULE_SHP ||
	     stream->last_module == ISPP_MODULE_NR) &&
	    mis_val & NR_INT) {
		if (is_switch) {
			reg_y = readl(base + RKISPP_NR_ADDR_BASE_Y);
			reg_uv = readl(base + RKISPP_NR_ADDR_BASE_UV);

			writel(readl(base + RKISPP_NR_ADDR_BASE_Y_SHD),
				base + RKISPP_NR_ADDR_BASE_Y);
			writel(readl(base + RKISPP_NR_ADDR_BASE_UV_SHD),
				base + RKISPP_NR_ADDR_BASE_UV);
		}

		writel(OTHER_FORCE_UPD, base + RKISPP_CTRL_UPDATE);

		if (is_switch) {
			writel(reg_y, base + RKISPP_NR_ADDR_BASE_Y);
			writel(reg_uv, base + RKISPP_NR_ADDR_BASE_UV);
		}
	} else if (stream->last_module == ISPP_MODULE_FEC &&
		   mis_val & FEC_INT) {
		if (is_switch) {
			reg_y = readl(base + RKISPP_FEC_RD_Y_BASE);
			reg_uv = readl(base + RKISPP_FEC_RD_UV_BASE);

			writel(readl(base + RKISPP_FEC_RD_Y_BASE_SHD),
				base + RKISPP_FEC_RD_Y_BASE);
			writel(readl(base + RKISPP_FEC_RD_UV_BASE_SHD),
				base + RKISPP_FEC_RD_UV_BASE);
		}

		writel(FEC_FORCE_UPD, base + RKISPP_CTRL_UPDATE);

		if (is_switch) {
			writel(reg_y, base + RKISPP_FEC_RD_Y_BASE);
			writel(reg_uv, base + RKISPP_FEC_RD_UV_BASE);
		}
	}
}

static void update_mi(struct rkispp_stream *stream)
{
	struct rkispp_stream_vdev *vdev = &stream->isppdev->stream_vdev;
	void __iomem *base = stream->isppdev->base_addr;
	struct rkispp_dummy_buffer *dummy_buf;
	u8 en = 0;

	if (stream->next_buf) {
		set_y_addr(stream,
			stream->next_buf->buff_addr[RKISPP_PLANE_Y]);
		set_uv_addr(stream,
			stream->next_buf->buff_addr[RKISPP_PLANE_UV]);
		if (stream->type == STREAM_INPUT && stream->curr_buf) {
			if (vdev->module_ens & ISPP_MODULE_FEC)
				en = FEC_ST;
			if (vdev->module_ens &
			    (ISPP_MODULE_NR | ISPP_MODULE_SHP))
				en = NR_SHP_ST;
			if (vdev->module_ens & ISPP_MODULE_TNR)
				en = TNR_ST;
			writel(en, base + RKISPP_CTRL_STRT);
		}
	}

	if (stream->type == STREAM_OUTPUT &&
	    !stream->next_buf) {
		dummy_buf = &stream->dummy_buf;
		if (!dummy_buf->vaddr)
			return;
		set_y_addr(stream, dummy_buf->dma_addr);
		set_uv_addr(stream, dummy_buf->dma_addr);
	}

	if (vdev->is_update_manual &&
	    stream->type == STREAM_OUTPUT)
		stream->curr_buf = stream->next_buf;

	if (stream->id == STREAM_MB &&
	    stream->isppdev->inp == INP_DDR &&
	    stream->last_module == ISPP_MODULE_TNR) {
		writel(readl(base + RKISPP_TNR_WR_Y_BASE_SHD),
			base + RKISPP_TNR_IIR_Y_BASE);
		writel(readl(base + RKISPP_TNR_WR_UV_BASE_SHD),
			base + RKISPP_TNR_IIR_UV_BASE);
	}
	v4l2_dbg(2, rkispp_debug, &stream->isppdev->v4l2_dev,
		 "%s stream:%d CUR(Y:0x%x UV:0x%x) SHD(Y:0x%x UV:0x%x)\n",
		 __func__, stream->id,
		 readl(base + stream->config->reg.cur_y_base),
		 readl(base + stream->config->reg.cur_uv_base),
		 readl(base + stream->config->reg.cur_y_base_shd),
		 readl(base + stream->config->reg.cur_uv_base_shd));
}

static int rkispp_frame_end(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct capture_fmt *fmt = &stream->out_cap_fmt;
	unsigned long lock_flags = 0;
	int i = 0;

	if (stream->curr_buf) {
		u64 ns = ktime_get_ns();

		for (i = 0; i < fmt->mplanes; i++) {
			u32 payload_size =
				stream->out_fmt.plane_fmt[i].sizeimage;
			vb2_set_plane_payload(&stream->curr_buf->vb.vb2_buf, i,
					      payload_size);
		}
		stream->curr_buf->vb.sequence =
			atomic_read(&dev->ispp_sdev.frm_sync_seq) - 1;
		stream->curr_buf->vb.vb2_buf.timestamp = ns;
		vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
		stream->curr_buf = NULL;
	}

	stream->curr_buf = stream->next_buf;
	stream->next_buf = NULL;
	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (!list_empty(&stream->buf_queue)) {
		stream->next_buf = list_first_entry(&stream->buf_queue,
					struct rkispp_buffer, queue);
		list_del(&stream->next_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	update_mi(stream);
	return 0;
}

static int config_tnr(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	void __iomem *base = dev->base_addr;
	int ret, mult = 1;
	u32 i, width, height, fmt;
	u32 pic_size, gain_size, kg_size;
	u32 addr_offs, w, h;

	vdev = &dev->stream_vdev;
	if (!(vdev->module_ens & ISPP_MODULE_TNR))
		return 0;

	if (dev->inp == INP_DDR) {
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = dev->ispp_sdev.out_fmt.wr_fmt | FMT_FBC;
	}

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;
	w = (fmt & FMT_FBC) ? ALIGN(width, 16) : width;
	h = (fmt & FMT_FBC) ? ALIGN(height, 16) : height;
	addr_offs = (fmt & FMT_FBC) ? w * h >> 4 : w * h;
	pic_size = (fmt & FMT_YUV422) ? w * h * 2 : w * h * 3 >> 1;
	if (fmt & FMT_FBC)
		pic_size += w * h >> 4;

	gain_size = ALIGN(width, 64) * ALIGN(height, 128) >> 4;
	kg_size = gain_size * 4;
	if (fmt & FMT_YUYV)
		mult = 2;

	if (vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)) {
		struct rkispp_dummy_buffer *buf;

		if (dev->inp == INP_ISP) {
			rkispp_set_bits(base + RKISPP_CTRL_QUICK,
					GLB_QUICK_MODE_MASK,
					GLB_QUICK_MODE(0));

			buf = &vdev->tnr_buf.pic_cur;
			buf->size = pic_size;
			ret = rkispp_allow_buffer(dev, buf);
			if (ret < 0)
				goto err;
			writel(buf->dma_addr,
				base + RKISPP_TNR_CUR_Y_BASE);
			writel(buf->dma_addr + addr_offs,
				base + RKISPP_TNR_CUR_UV_BASE);

			buf = &vdev->tnr_buf.gain_cur;
			buf->size = gain_size;
			ret = rkispp_allow_buffer(dev, buf);
			if (ret < 0)
				goto err;
			writel(buf->dma_addr,
				base + RKISPP_TNR_GAIN_CUR_Y_BASE);

			buf = &vdev->tnr_buf.iir;
			buf->size = pic_size;
			ret = rkispp_allow_buffer(dev, buf);
			if (ret < 0)
				goto err;

			if ((vdev->module_ens & ISPP_MODULE_TNR_3TO1) ==
			    ISPP_MODULE_TNR_3TO1) {
				buf = &vdev->tnr_buf.pic_next;
				buf->size = pic_size;
				ret = rkispp_allow_buffer(dev, buf);
				if (ret < 0)
					goto err;
				writel(buf->dma_addr,
					base + RKISPP_TNR_NXT_Y_BASE);
				writel(buf->dma_addr + addr_offs,
					base + RKISPP_TNR_NXT_UV_BASE);
				buf = &vdev->tnr_buf.gain_next;
				buf->size = gain_size;
				ret = rkispp_allow_buffer(dev, buf);
				if (ret < 0)
					goto err;
				writel(buf->dma_addr,
					base + RKISPP_TNR_GAIN_NXT_Y_BASE);
			}
		}
		buf = &vdev->tnr_buf.gain_kg;
		buf->size = kg_size;
		ret = rkispp_allow_buffer(dev, buf);
		if (ret < 0)
			goto err;
		writel(buf->dma_addr,
			base + RKISPP_TNR_GAIN_KG_Y_BASE);

		buf = &vdev->tnr_buf.pic_wr;
		buf->size = pic_size;
		ret = rkispp_allow_buffer(dev, buf);
		if (ret < 0)
			goto err;
		writel(buf->dma_addr,
			base + RKISPP_TNR_WR_Y_BASE);
		writel(buf->dma_addr + addr_offs,
			base + RKISPP_TNR_WR_UV_BASE);
		if (vdev->tnr_buf.iir.vaddr)
			buf = &vdev->tnr_buf.iir;
		writel(buf->dma_addr,
			base + RKISPP_TNR_IIR_Y_BASE);
		writel(buf->dma_addr + addr_offs,
			base + RKISPP_TNR_IIR_UV_BASE);

		buf = &vdev->tnr_buf.gain_wr;
		buf->size = gain_size;
		ret = rkispp_allow_buffer(dev, buf);
		if (ret < 0)
			goto err;
		writel(buf->dma_addr,
			base + RKISPP_TNR_GAIN_WR_Y_BASE);

		writel(ALIGN(width * mult, 16) >> 2,
			base + RKISPP_TNR_WR_VIR_STRIDE);
		writel(fmt << 4 | SW_TNR_1ST_FRM,
			base + RKISPP_TNR_CTRL);
	}

	if (stream) {
		stream->config->frame_end_id = TNR_INT;
		stream->config->reg.cur_y_base = RKISPP_TNR_CUR_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_TNR_CUR_UV_BASE;
		stream->config->reg.cur_y_base_shd = RKISPP_TNR_CUR_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_TNR_CUR_UV_BASE_SHD;
	}

	rkispp_set_bits(base + RKISPP_TNR_CTRL, FMT_RD_MASK, fmt);
	if (fmt & FMT_FBC) {
		writel(0, base + RKISPP_TNR_CUR_VIR_STRIDE);
		writel(0, base + RKISPP_TNR_IIR_VIR_STRIDE);
		writel(0, base + RKISPP_TNR_NXT_VIR_STRIDE);
	} else {
		writel(ALIGN(width * mult, 16) >> 2,
			base + RKISPP_TNR_CUR_VIR_STRIDE);
		writel(ALIGN(width * mult, 16) >> 2,
			base + RKISPP_TNR_IIR_VIR_STRIDE);
		writel(ALIGN(width * mult, 16) >> 2,
			base + RKISPP_TNR_NXT_VIR_STRIDE);
	}
	rkispp_set_bits(base + RKISPP_TNR_CORE_CTRL, SW_TNR_MODE,
		((vdev->module_ens & ISPP_MODULE_TNR_3TO1) ==
		 ISPP_MODULE_TNR_3TO1 &&
		 dev->inp == INP_ISP) ? SW_TNR_MODE : 0);
	writel(ALIGN(width, 64) >> 4,
		base + RKISPP_TNR_GAIN_CUR_VIR_STRIDE);
	writel(ALIGN(width, 64) >> 4,
		base + RKISPP_TNR_GAIN_NXT_VIR_STRIDE);
	writel(ALIGN(width, 16) * 6,
		base + RKISPP_TNR_GAIN_KG_VIR_STRIDE);
	writel(ALIGN(width, 64) >> 4,
		base + RKISPP_TNR_GAIN_WR_VIR_STRIDE);

	writel(height << 16 | width,
		base + RKISPP_CTRL_TNR_SIZE);

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, width, height,
		 readl(base + RKISPP_TNR_CTRL),
		 readl(base + RKISPP_TNR_CORE_CTRL));
	return 0;
err:
	for (i = 0; i < sizeof(vdev->tnr_buf) /
	     sizeof(struct rkispp_dummy_buffer); i++)
		rkispp_free_buffer(dev, &vdev->tnr_buf.pic_cur + i);
	v4l2_err(&dev->v4l2_dev,
		 "%s Failed to allocate buffer\n", __func__);
	return ret;
}

static int config_nr_shp(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	void __iomem *base = dev->base_addr;
	struct rkispp_dummy_buffer *buf;
	u32 width, height, fmt;
	u32 pic_size, gain_size;
	u32 i, addr_offs, w, h;
	int ret, mult = 1;

	vdev = &dev->stream_vdev;
	if (!(vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)))
		return 0;

	if (dev->inp == INP_DDR) {
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = dev->ispp_sdev.out_fmt.wr_fmt | FMT_FBC;
	}

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;
	w = (fmt & FMT_FBC) ? ALIGN(width, 16) : width;
	h = (fmt & FMT_FBC) ? ALIGN(height, 16) : height;
	addr_offs = (fmt & FMT_FBC) ? w * h >> 4 : w * h;
	pic_size = (fmt & FMT_YUV422) ? w * h * 2 : w * h * 3 >> 1;
	if (fmt & FMT_FBC)
		pic_size += w * h >> 4;

	gain_size = ALIGN(width, 64) * ALIGN(height, 128) >> 4;
	if (fmt & FMT_YUYV)
		mult = 2;

	if (vdev->module_ens & ISPP_MODULE_TNR) {
		writel(readl(base + RKISPP_TNR_WR_Y_BASE),
			base + RKISPP_NR_ADDR_BASE_Y);
		writel(readl(base + RKISPP_TNR_WR_UV_BASE),
			base + RKISPP_NR_ADDR_BASE_UV);
		writel(readl(base + RKISPP_TNR_GAIN_WR_Y_BASE),
			base + RKISPP_NR_ADDR_BASE_GAIN);
		rkispp_set_bits(base + RKISPP_CTRL_QUICK, 0, GLB_NR_SD32_TNR);
	} else {
		/* tnr need to set same format with nr in the fbc mode */
		rkispp_set_bits(base + RKISPP_TNR_CTRL,
				FMT_RD_MASK, fmt);
		if (dev->inp == INP_ISP) {
			rkispp_set_bits(base + RKISPP_CTRL_QUICK,
					GLB_QUICK_MODE_MASK,
					GLB_QUICK_MODE(2));

			buf = &vdev->nr_buf.pic_cur;
			buf->size = pic_size;
			ret = rkispp_allow_buffer(dev, buf);
			if (ret < 0)
				goto err;
			writel(buf->dma_addr, base + RKISPP_NR_ADDR_BASE_Y);
			writel(buf->dma_addr + addr_offs,
				base + RKISPP_NR_ADDR_BASE_UV);
			buf = &vdev->nr_buf.gain_cur;
			buf->size = gain_size;
			ret = rkispp_allow_buffer(dev, buf);
			if (ret < 0)
				goto err;
			writel(buf->dma_addr, base + RKISPP_NR_ADDR_BASE_GAIN);
			rkispp_clear_bits(base + RKISPP_CTRL_QUICK, GLB_NR_SD32_TNR);
		} else if (stream) {
			stream->config->frame_end_id = NR_INT;
			stream->config->reg.cur_y_base = RKISPP_NR_ADDR_BASE_Y;
			stream->config->reg.cur_uv_base = RKISPP_NR_ADDR_BASE_UV;
			stream->config->reg.cur_y_base_shd = RKISPP_NR_ADDR_BASE_Y_SHD;
			stream->config->reg.cur_uv_base_shd = RKISPP_NR_ADDR_BASE_UV_SHD;
		}
	}

	rkispp_clear_bits(base + RKISPP_CTRL_QUICK, GLB_FEC2SCL_EN);
	if (vdev->module_ens & ISPP_MODULE_FEC) {
		pic_size = (fmt & FMT_YUV422) ? width * height * 2 :
			width * height * 3 >> 1;
		addr_offs = width * height;
		buf = &vdev->nr_buf.pic_wr;
		buf->size = pic_size;
		ret = rkispp_allow_buffer(dev, buf);
		if (ret < 0)
			goto err;
		writel(buf->dma_addr, base + RKISPP_SHARP_WR_Y_BASE);
		writel(buf->dma_addr + addr_offs,
			base + RKISPP_SHARP_WR_UV_BASE);

		writel(ALIGN(width * mult, 16) >> 2,
			base + RKISPP_SHARP_WR_VIR_STRIDE);
		rkispp_set_bits(base + RKISPP_SHARP_CTRL,
				SW_SHP_WR_FORMAT_MASK, fmt & (~FMT_FBC));
		rkispp_clear_bits(base + RKISPP_SHARP_CORE_CTRL, SW_SHP_DMA_DIS);
	}

	buf = &vdev->nr_buf.tmp_yuv;
	buf->size = width * 42;
	ret = rkispp_allow_buffer(dev, buf);
	if (ret < 0)
		goto err;
	writel(buf->dma_addr, base + RKISPP_SHARP_TMP_YUV_BASE);

	/* fix to use new nr algorithm */
	rkispp_set_bits(base + RKISPP_NR_CTRL, NR_NEW_ALGO, NR_NEW_ALGO);
	rkispp_set_bits(base + RKISPP_NR_CTRL, FMT_RD_MASK, fmt);
	if (fmt & FMT_FBC) {
		writel(0, base + RKISPP_NR_VIR_STRIDE);
		writel(ALIGN(height, 16), base + RKISPP_FBC_VIR_HEIGHT);
	} else {
		writel(ALIGN(width * mult, 16) >> 2, base + RKISPP_NR_VIR_STRIDE);
	}
	writel(ALIGN(width, 64) >> 4, base + RKISPP_NR_VIR_STRIDE_GAIN);
	writel(height << 16 | width, base + RKISPP_CTRL_SIZE);

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d\n"
		 "nr ctrl:0x%x ctrl_para:0x%x\n"
		 "shp ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, width, height,
		 readl(base + RKISPP_NR_CTRL),
		 readl(base + RKISPP_NR_UVNR_CTRL_PARA),
		 readl(base + RKISPP_SHARP_CTRL),
		 readl(base + RKISPP_SHARP_CORE_CTRL));
	return 0;
err:
	for (i = 0; i < sizeof(vdev->nr_buf) /
	     sizeof(struct rkispp_dummy_buffer); i++)
		rkispp_free_buffer(dev, &vdev->nr_buf.pic_cur + i);
	v4l2_err(&dev->v4l2_dev,
		 "%s Failed to allocate buffer\n", __func__);
	return ret;
}

static int config_fec(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;
	void __iomem *base = dev->base_addr;
	struct rkispp_dummy_buffer *buf;
	u32 width, height, fmt, mult = 1;
	u32 i, mesh_size;
	int ret;

	vdev = &dev->stream_vdev;
	if (!(vdev->module_ens & ISPP_MODULE_FEC))
		return 0;

	if (dev->inp == INP_DDR) {
		stream = &vdev->stream[STREAM_II];
		fmt = stream->out_cap_fmt.wr_fmt;
	} else {
		fmt = dev->ispp_sdev.out_fmt.wr_fmt;
	}

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;

	if (fmt & FMT_YUYV)
		mult = 2;

	if (vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP)) {
		writel(readl(base + RKISPP_SHARP_WR_Y_BASE),
			base + RKISPP_FEC_RD_Y_BASE);
		writel(readl(base + RKISPP_SHARP_WR_UV_BASE),
			base + RKISPP_FEC_RD_UV_BASE);
	} else if (stream) {
		stream->config->frame_end_id = FEC_INT;
		stream->config->reg.cur_y_base = RKISPP_FEC_RD_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_FEC_RD_UV_BASE;
		stream->config->reg.cur_y_base_shd = RKISPP_FEC_RD_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_FEC_RD_UV_BASE_SHD;
	}

	mesh_size = cal_fec_mesh(width, height, 0);
	buf = &vdev->fec_buf.mesh_xint;
	buf->size = ALIGN(mesh_size * 2, 8);
	ret = rkispp_allow_buffer(dev, buf);
	if (ret < 0)
		goto err;
	writel(buf->dma_addr, base + RKISPP_FEC_MESH_XINT_BASE);

	buf = &vdev->fec_buf.mesh_yint;
	buf->size = ALIGN(mesh_size * 2, 8);
	ret = rkispp_allow_buffer(dev, buf);
	if (ret < 0)
		goto err;
	writel(buf->dma_addr, base + RKISPP_FEC_MESH_YINT_BASE);

	buf = &vdev->fec_buf.mesh_xfra;
	buf->size = ALIGN(mesh_size, 8);
	ret = rkispp_allow_buffer(dev, buf);
	if (ret < 0)
		goto err;
	writel(buf->dma_addr, base + RKISPP_FEC_MESH_XFRA_BASE);

	buf = &vdev->fec_buf.mesh_yfra;
	buf->size = ALIGN(mesh_size, 8);
	ret = rkispp_allow_buffer(dev, buf);
	if (ret < 0)
		goto err;
	writel(buf->dma_addr, base + RKISPP_FEC_MESH_YFRA_BASE);

	rkispp_set_bits(base + RKISPP_FEC_CTRL, FMT_RD_MASK, fmt);
	writel(ALIGN(width * mult, 16) >> 2, base + RKISPP_FEC_RD_VIR_STRIDE);
	writel(height << 16 | width, base + RKISPP_FEC_PIC_SIZE);
	rkispp_set_bits(base + RKISPP_CTRL_QUICK, 0, GLB_FEC2SCL_EN);
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s size:%dx%d ctrl:0x%x core_ctrl:0x%x\n",
		 __func__, width, height,
		 readl(base + RKISPP_FEC_CTRL),
		 readl(base + RKISPP_FEC_CORE_CTRL));
	return 0;
err:
	for (i = 0; i < sizeof(vdev->fec_buf) /
	     sizeof(struct rkispp_dummy_buffer); i++)
		rkispp_free_buffer(dev, &vdev->fec_buf.mesh_xint + i);
	v4l2_err(&dev->v4l2_dev,
		 "%s Failed to allocate buffer\n", __func__);
	return ret;
}

static int config_modules(struct rkispp_device *dev)
{
	int ret;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "stream module ens:0x%x\n", dev->stream_vdev.module_ens);

	ret = config_tnr(dev);
	if (ret < 0)
		return ret;

	ret = config_nr_shp(dev);
	if (ret < 0)
		return ret;

	ret = config_fec(dev);
	if (ret < 0)
		return ret;

	rkispp_params_configure(&dev->params_vdev);

	return ret;
}

static int start_ii(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	void __iomem *base = dev->base_addr;
	struct rkispp_stream *st;
	int i;

	rkispp_clear_bits(base + RKISPP_CTRL_QUICK, GLB_QUICK_EN);
	writel(ALL_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
	if (vdev->is_update_manual)
		i = (vdev->module_ens & ISPP_MODULE_FEC) ?
			STREAM_S0 : STREAM_MAX;
	else
		i = STREAM_MB;

	for (; i < STREAM_MAX; i++) {
		st = &vdev->stream[i];
		rkispp_frame_end(st);
	}

	stream->streaming = true;
	rkispp_frame_end(stream);

	return 0;
}

static int config_ii(struct rkispp_stream *stream)
{
	return config_modules(stream->isppdev);
}

static int config_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	void __iomem *base = dev->base_addr;
	u32 i, limit_range, mult = 1;

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
		rkispp_set_bits(base + RKISPP_TNR_CTRL, FMT_WR_MASK,
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
		limit_range = (stream->out_fmt.quantization != V4L2_QUANTIZATION_LIM_RANGE) ?
			0 : SW_SHP_WR_YUV_LIMIT;
		rkispp_set_bits(base + RKISPP_SHARP_CTRL,
				SW_SHP_WR_YUV_LIMIT | SW_SHP_WR_FORMAT_MASK,
				limit_range | stream->out_cap_fmt.wr_fmt);
		rkispp_clear_bits(base + RKISPP_SHARP_CORE_CTRL, SW_SHP_DMA_DIS);
		break;
	default:
		stream->config->frame_end_id = FEC_INT;
		stream->config->reg.cur_y_base = RKISPP_FEC_WR_Y_BASE;
		stream->config->reg.cur_uv_base = RKISPP_FEC_WR_UV_BASE;
		stream->config->reg.cur_vir_stride = RKISPP_FEC_WR_VIR_STRIDE;
		stream->config->reg.cur_y_base_shd = RKISPP_FEC_WR_Y_BASE_SHD;
		stream->config->reg.cur_uv_base_shd = RKISPP_FEC_WR_UV_BASE_SHD;
		limit_range = (stream->out_fmt.quantization != V4L2_QUANTIZATION_LIM_RANGE) ?
			0 : SW_FEC_WR_YUV_LIMIT;
		rkispp_set_bits(base + RKISPP_FEC_CTRL, SW_FEC_WR_YUV_LIMIT | FMT_WR_MASK,
				limit_range | stream->out_cap_fmt.wr_fmt << 4);
		writel((stream->out_fmt.height << 16) | stream->out_fmt.width,
			base + RKISPP_FEC_PIC_SIZE);
		rkispp_clear_bits(base + RKISPP_FEC_CORE_CTRL, SW_FEC2DDR_DIS);
	}
	if (stream->out_cap_fmt.wr_fmt & FMT_YUYV)
		mult = 2;
	set_vir_stride(stream, ALIGN(stream->out_fmt.width * mult, 16) >> 2);
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s last module:%d\n", __func__, i);
	return 0;
}

static void stop_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	void __iomem *base = dev->base_addr;

	switch (stream->last_module) {
	case ISPP_MODULE_TNR:
		rkispp_clear_bits(base + RKISPP_TNR_CORE_CTRL, SW_TNR_EN);
		break;
	case ISPP_MODULE_NR:
	case ISPP_MODULE_SHP:
		break;
	default:
		rkispp_clear_bits(base + RKISPP_FEC_CORE_CTRL, SW_FEC_EN);
	}
}

static int is_stopped_mb(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	void __iomem *base = dev->base_addr;
	u32 val, en;

	switch (stream->last_module) {
	case ISPP_MODULE_TNR:
		en = SW_TNR_EN_SHD;
		val = readl(base + RKISPP_TNR_CORE_CTRL);
		break;
	case ISPP_MODULE_NR:
	case ISPP_MODULE_SHP:
		/* close dma write immediately */
		rkispp_set_bits(base + RKISPP_SHARP_CORE_CTRL,
				SW_SHP_DMA_DIS, SW_SHP_DMA_DIS);
		val = false;
		en = false;
		break;
	default:
		en = SW_FEC_EN_SHD;
		val = readl(base + RKISPP_FEC_CORE_CTRL);
		rkispp_set_bits(base + RKISPP_FEC_CORE_CTRL,
				0, SW_FEC2DDR_DIS);
	}

	return !(val & en);
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

	if (stream->out_cap_fmt.wr_fmt & FMT_FBC)
		dev->stream_vdev.is_update_manual = true;

	return 0;
}

static int config_scl(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	void __iomem *base = dev->base_addr;
	const struct capture_fmt *fmt = &stream->out_cap_fmt;
	u32 in_width = dev->ispp_sdev.out_fmt.width;
	u32 in_height = dev->ispp_sdev.out_fmt.height;
	u32 hy_fac = (stream->out_fmt.width - 1) * 8192 /
			(in_width - 1) + 1;
	u32 vy_fac = (stream->out_fmt.height - 1) * 8192 /
			(in_height - 1) + 1;
	u32 value = 0;
	u8 bypass = 0, mult = 1;

	if (hy_fac == 8193 && vy_fac == 8193)
		bypass = SW_SCL_BYPASS;
	if (fmt->wr_fmt & FMT_YUYV)
		mult = 2;
	set_vir_stride(stream, ALIGN(stream->out_fmt.width * mult, 16) >> 2);
	set_scl_factor(stream, vy_fac << 16 | hy_fac);
	value = SW_SCL_ENABLE | bypass | fmt->wr_fmt << 3 |
		((fmt->fourcc != V4L2_PIX_FMT_GREY) ? 0 : SW_SCL_WR_UV_DIS) |
		((stream->out_fmt.quantization != V4L2_QUANTIZATION_LIM_RANGE) ?
		 0 : SW_SCL_WR_YUV_LIMIT);
	set_ctrl(stream, value);

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "scl%d ctrl:0x%x stride:0x%x factor:0x%x\n",
		 stream->id - STREAM_S0,
		 readl(base + stream->config->reg.ctrl),
		 readl(base + stream->config->reg.cur_vir_stride),
		 readl(base + stream->config->reg.factor));
	return 0;
}

static void stop_scl(struct rkispp_stream *stream)
{
	void __iomem *base = stream->isppdev->base_addr;

	rkispp_clear_bits(base + stream->config->reg.ctrl, SW_SCL_ENABLE);
}

static int is_stopped_scl(struct rkispp_stream *stream)
{
	void __iomem *base = stream->isppdev->base_addr;

	return !(readl(base + stream->config->reg.ctrl) & SW_SCL_ENABLE_SHD);
}

static int limit_check_scl(struct rkispp_stream *stream,
			   struct v4l2_pix_format_mplane *try_fmt)
{
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_subdev *sdev = &dev->ispp_sdev;
	u32 max_width = 1280, max_ratio = 8, min_ratio = 2;
	u32 *w = try_fmt ? &try_fmt->width : &stream->out_fmt.width;
	u32 *h = try_fmt ? &try_fmt->height : &stream->out_fmt.height;
	int ret = 0;

	/* bypass scale */
	if (*w == sdev->out_fmt.width && *h == sdev->out_fmt.height)
		return ret;

	if (stream->id == STREAM_S0) {
		max_width = 3264;
		min_ratio = 1;
	}

	if (*w > max_width ||
	    *w * max_ratio < sdev->out_fmt.width ||
	    *h * max_ratio < sdev->out_fmt.height ||
	    *w * min_ratio > sdev->out_fmt.width ||
	    *h * min_ratio > sdev->out_fmt.height) {
		ret = -EINVAL;
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
};

static struct streams_ops mb_stream_ops = {
	.config = config_mb,
	.stop = stop_mb,
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

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

static void rkispp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkispp_buffer *isppbuf = to_rkispp_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkispp_stream *stream = queue->drv_priv;
	unsigned long lock_flags = 0;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *cap_fmt = &stream->out_cap_fmt;
	u32 height, size, offset;
	int i;

	memset(isppbuf->buff_addr, 0, sizeof(isppbuf->buff_addr));
	for (i = 0; i < cap_fmt->mplanes; i++)
		isppbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

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
		 "stream:%d queue buf:0x%x\n",
		 stream->id, isppbuf->buff_addr[0]);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->type == STREAM_INPUT &&
	    stream->streaming &&
	    !stream->next_buf) {
		stream->next_buf = isppbuf;
		update_mi(stream);
	} else {
		list_add_tail(&isppbuf->queue, &stream->buf_queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int rkispp_create_dummy_buf(struct rkispp_stream *stream)
{
	struct rkispp_dummy_buffer *buf = &stream->dummy_buf;
	struct rkispp_device *dev = stream->isppdev;

	if (stream->type != STREAM_OUTPUT)
		return 0;

	buf->size = max3(stream->out_fmt.plane_fmt[0].bytesperline *
			 stream->out_fmt.height,
			 stream->out_fmt.plane_fmt[1].sizeimage,
			 stream->out_fmt.plane_fmt[2].sizeimage);
	return rkispp_allow_buffer(dev, buf);
}

static void rkispp_destroy_dummy_buf(struct rkispp_stream *stream)
{
	struct rkispp_dummy_buffer *buf = &stream->dummy_buf;
	struct rkispp_device *dev = stream->isppdev;
	struct rkispp_stream_vdev *vdev;
	u32 i;

	vdev = &dev->stream_vdev;
	rkispp_free_buffer(dev, buf);

	if (atomic_read(&vdev->refcnt) == 1) {
		vdev->is_update_manual = false;
		for (i = 0; i < sizeof(vdev->tnr_buf) /
		     sizeof(struct rkispp_dummy_buffer); i++)
			rkispp_free_buffer(dev, &vdev->tnr_buf.pic_cur + i);
		for (i = 0; i < sizeof(vdev->nr_buf) /
		     sizeof(struct rkispp_dummy_buffer); i++)
			rkispp_free_buffer(dev, &vdev->nr_buf.pic_cur + i);
		for (i = 0; i < sizeof(vdev->fec_buf) /
		     sizeof(struct rkispp_dummy_buffer); i++)
			rkispp_free_buffer(dev, &vdev->fec_buf.mesh_xint + i);
	}
}

static void rkispp_stream_stop(struct rkispp_stream *stream)
{
	struct rkispp_device *dev = stream->isppdev;
	bool wait = false;
	int ret = 0;

	stream->stopping = true;
	if (stream->ops->stop) {
		stream->ops->stop(stream);
		if (dev->inp == INP_DDR &&
		    !dev->stream_vdev.stream[STREAM_II].streaming)
			wait = false;
		else
			wait = true;
	}
	if (dev->inp == INP_ISP &&
	    atomic_read(&dev->stream_vdev.refcnt) == 1)
		v4l2_subdev_call(&dev->ispp_sdev.sd,
				 video, s_stream, false);
	if (wait) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(1000));
		if (!ret)
			v4l2_warn(&dev->v4l2_dev,
				  "waiting on event ret:%d\n", ret);
	}
	stream->streaming = false;
	stream->stopping = false;
}

static void destroy_buf_queue(struct rkispp_stream *stream,
			      enum vb2_buffer_state state)
{
	unsigned long lock_flags = 0;
	struct rkispp_buffer *buf;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		if (stream->next_buf == stream->curr_buf)
			stream->next_buf = NULL;
		stream->curr_buf = NULL;
	}
	if (stream->next_buf) {
		list_add_tail(&stream->next_buf->queue, &stream->buf_queue);
		stream->next_buf = NULL;
	}
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue,
			struct rkispp_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void rkispp_stop_streaming(struct vb2_queue *queue)
{
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d\n", __func__, stream->id);

	if (!stream->streaming)
		return;

	rkispp_stream_stop(stream);
	destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);
	rkispp_destroy_dummy_buf(stream);
	atomic_dec(&dev->stream_vdev.refcnt);
}

static int start_isp(struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkispp_stream *stream;
	int i, ret;

	if (dev->inp != INP_ISP)
		return 0;

	/* output stream enable then start isp*/
	for (i = STREAM_MB; i < STREAM_S2 + 1; i++) {
		stream = &vdev->stream[i];
		if (stream->linked && !stream->streaming)
			return 0;
	}
	ret = config_modules(dev);
	if (ret)
		return ret;
	writel(ALL_FORCE_UPD, dev->base_addr + RKISPP_CTRL_UPDATE);
	if (vdev->is_update_manual)
		i = (vdev->module_ens & ISPP_MODULE_FEC) ?
			STREAM_S0 : STREAM_MAX;
	else
		i = STREAM_MB;
	for (; i < STREAM_MAX; i++) {
		stream = &vdev->stream[i];
		rkispp_frame_end(stream);
	}
	rkispp_set_bits(dev->base_addr + RKISPP_CTRL_QUICK,
			0, GLB_QUICK_EN);
	return v4l2_subdev_call(&dev->ispp_sdev.sd,
				video, s_stream, true);
}

static int rkispp_start_streaming(struct vb2_queue *queue,
				  unsigned int count)
{
	struct rkispp_stream *stream = queue->drv_priv;
	struct rkispp_device *dev = stream->isppdev;
	int ret = -1;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s id:%d\n", __func__, stream->id);

	if (stream->streaming)
		return -EBUSY;

	atomic_inc(&dev->stream_vdev.refcnt);
	if (!dev->inp || !stream->linked) {
		v4l2_err(&dev->v4l2_dev,
			 "invalid input source\n");
		goto free_buf_queue;
	}

	ret = rkispp_create_dummy_buf(stream);
	if (ret < 0)
		goto free_buf_queue;

	if (dev->inp == INP_ISP)
		dev->stream_vdev.module_ens |= ISPP_MODULE_NR;

	if (stream->ops->config) {
		ret = stream->ops->config(stream);
		if (ret < 0)
			goto free_dummy_buf;
	}

	/* config first buf */
	rkispp_frame_end(stream);

	/* start from ddr */
	if (stream->ops->start)
		stream->ops->start(stream);

	stream->streaming = true;

	/* start from isp */
	ret = start_isp(dev);
	if (ret)
		goto free_dummy_buf;
	return 0;
free_dummy_buf:
	rkispp_destroy_dummy_buf(stream);
free_buf_queue:
	destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
	atomic_dec(&dev->stream_vdev.refcnt);
	stream->streaming = false;
	v4l2_err(&dev->v4l2_dev,
		 "ispp stream:%d on failed ret:%d\n",
		 stream->id, ret);
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
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkispp_buffer);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		q->min_buffers_needed = STREAM_IN_REQ_BUFS_MIN;
	else
		q->min_buffers_needed = STREAM_OUT_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->isppdev->apilock;
	q->dev = stream->isppdev->dev;

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
				 "ispp input max:%dx%d min:%dx%d\n",
				 RKISPP_MIN_WIDTH, RKISPP_MIN_HEIGHT,
				 RKISPP_MAX_WIDTH, RKISPP_MAX_HEIGHT);
			stream->out_fmt.width = 0;
			stream->out_fmt.height = 0;
			return -EINVAL;
		}
	}

	if (stream->ops->limit_check)
		return stream->ops->limit_check(stream, try ? pixm : NULL);

	return 0;
}

/************************* v4l2_file_operations***************************/

static const struct v4l2_file_operations rkispp_fops = {
	.open = rkispp_fh_open,
	.release = rkispp_fh_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

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

	if (vb2_is_busy(&node->buf_queue)) {
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

void rkispp_isr(u32 mis_val, struct rkispp_device *dev)
{
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream;
	void __iomem *base = dev->base_addr;
	u32 i, err_mask = NR_LOST_ERR | TNR_LOST_ERR |
		UVNR_MONITOR_ERR | FBCH_EMPTY_NR |
		FBCH_EMPTY_TNR | FBCD_DEC_ERR_NR |
		FBCD_DEC_ERR_TNR | BUS_ERR_NR | BUS_ERR_TNR;

	v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
		 "isr:0x%x\n", mis_val);

	vdev = &dev->stream_vdev;
	if (mis_val & err_mask)
		v4l2_err(&dev->v4l2_dev,
			 "ispp err:0x%x\n", mis_val);

	if (mis_val & (CMD_TNR_ST_DONE | CMD_NR_SHP_ST_DONE))
		atomic_inc(&dev->ispp_sdev.frm_sync_seq);

	rkispp_params_isr(&dev->params_vdev, mis_val);
	rkispp_stats_isr(&dev->stats_vdev, mis_val);

	if (mis_val & TNR_INT) {
		if (readl(base + RKISPP_TNR_CTRL) & SW_TNR_1ST_FRM)
			rkispp_clear_bits(base + RKISPP_TNR_CTRL,
					  SW_TNR_1ST_FRM);
		if (dev->inp == INP_DDR &&
		    vdev->module_ens & (ISPP_MODULE_NR | ISPP_MODULE_SHP))
			writel(NR_SHP_ST, base + RKISPP_CTRL_STRT);
	}

	if (mis_val & SHP_INT &&
	    vdev->module_ens & ISPP_MODULE_FEC)
		writel(FEC_ST, base + RKISPP_CTRL_STRT);

	for (i = 0; i < STREAM_MAX; i++) {
		stream = &vdev->stream[i];

		if (!stream->streaming ||
		    !(mis_val & INT_FRAME(stream)))
			continue;
		if (stream->stopping) {
			if (stream->ops->is_stopped &&
			    stream->ops->is_stopped(stream)) {
				stream->stopping = false;
				stream->streaming = false;
				wake_up(&stream->done);
			}
		} else {
			rkispp_frame_end(stream);
		}
	}

	if (vdev->is_update_manual)
		check_to_force_update(dev, mis_val);
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
		default:
			v4l2_err(&dev->v4l2_dev, "Invalid stream:%d\n", i);
			return -EINVAL;
		}
		strlcpy(vdev->name, vdev_name, sizeof(vdev->name));
		ret = rkispp_register_stream_video(stream);
		if (ret < 0)
			goto err;
	}
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
