// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#include <linux/cleanup.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#include "c3-isp-common.h"
#include "c3-isp-regs.h"

#define C3_ISP_WRMIFX3_REG(addr, id)	((addr) + (id) * 0x100)

static const struct c3_isp_cap_format_info cap_formats[] = {
	/* YUV formats */
	{
		.mbus_code = MEDIA_BUS_FMT_YUV10_1X30,
		.fourcc = V4L2_PIX_FMT_GREY,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_Y_ONLY,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X1,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_8BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_8BIT,
		.hdiv = 1,
		.vdiv = 1,
	}, {
		.mbus_code = MEDIA_BUS_FMT_YUV10_1X30,
		.fourcc = V4L2_PIX_FMT_NV12M,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_YUV420,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X2,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_8BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_UV,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_8BIT,
		.hdiv = 2,
		.vdiv = 2,
	}, {
		.mbus_code = MEDIA_BUS_FMT_YUV10_1X30,
		.fourcc = V4L2_PIX_FMT_NV21M,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_YUV420,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X2,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_8BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_8BIT,
		.hdiv = 2,
		.vdiv = 2,
	}, {
		.mbus_code = MEDIA_BUS_FMT_YUV10_1X30,
		.fourcc = V4L2_PIX_FMT_NV16M,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_YUV422,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X2,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_8BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_UV,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_8BIT,
		.hdiv = 1,
		.vdiv = 2
	}, {
		.mbus_code = MEDIA_BUS_FMT_YUV10_1X30,
		.fourcc = V4L2_PIX_FMT_NV61M,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_YUV422,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X2,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_8BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_8BIT,
		.hdiv = 1,
		.vdiv = 2,
	},
	/* RAW formats */
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB16_1X16,
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_RAW,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X1,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_16BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_16BIT,
		.hdiv = 1,
		.vdiv = 1,
	}, {
		.mbus_code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_RAW,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X1,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_16BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_16BIT,
		.hdiv = 1,
		.vdiv = 1,
	}, {
		.mbus_code = MEDIA_BUS_FMT_SGRBG16_1X16,
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_RAW,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X1,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_16BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_16BIT,
		.hdiv = 1,
		.vdiv = 1,
	}, {
		.mbus_code = MEDIA_BUS_FMT_SGBRG16_1X16,
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.format = ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_RAW,
		.planes = ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_X1,
		.ch0_pix_bits = ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_16BITS,
		.uv_swap = ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_VU,
		.in_bits = ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_16BIT,
		.hdiv = 1,
		.vdiv = 1,
	},
};

/* Hardware configuration */

/* Set the address of wrmifx3(write memory interface) */
static void c3_isp_cap_wrmifx3_buff(struct c3_isp_capture *cap)
{
	dma_addr_t y_dma_addr;
	dma_addr_t uv_dma_addr;

	if (cap->buff) {
		y_dma_addr = cap->buff->dma_addr[C3_ISP_PLANE_Y];
		uv_dma_addr = cap->buff->dma_addr[C3_ISP_PLANE_UV];
	} else {
		y_dma_addr = cap->dummy_buff.dma_addr;
		uv_dma_addr = cap->dummy_buff.dma_addr;
	}

	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_CH0_BADDR, cap->id),
		     ISP_WRMIFX3_0_CH0_BASE_ADDR(y_dma_addr));

	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_CH1_BADDR, cap->id),
		     ISP_WRMIFX3_0_CH1_BASE_ADDR(uv_dma_addr));
}

static void c3_isp_cap_wrmifx3_format(struct c3_isp_capture *cap)
{
	struct v4l2_pix_format_mplane *pix_mp = &cap->format.pix_mp;
	const struct c3_isp_cap_format_info *info = cap->format.info;
	u32 stride;
	u32 chrom_h;
	u32 chrom_v;

	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_FMT_SIZE, cap->id),
		     ISP_WRMIFX3_0_FMT_SIZE_HSIZE(pix_mp->width) |
		     ISP_WRMIFX3_0_FMT_SIZE_VSIZE(pix_mp->height));

	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_FMT_CTRL, cap->id),
			   ISP_WRMIFX3_0_FMT_CTRL_MODE_OUT_MASK, info->format);

	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_FMT_CTRL, cap->id),
			   ISP_WRMIFX3_0_FMT_CTRL_MTX_IBITS_MASK,
			   info->in_bits);

	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_FMT_CTRL, cap->id),
			   ISP_WRMIFX3_0_FMT_CTRL_MTX_PLANE_MASK, info->planes);

	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_FMT_CTRL, cap->id),
			   ISP_WRMIFX3_0_FMT_CTRL_MTX_UV_SWAP_MASK,
			   info->uv_swap);

	stride = DIV_ROUND_UP(pix_mp->plane_fmt[C3_ISP_PLANE_Y].bytesperline,
			      C3_ISP_DMA_SIZE_ALIGN_BYTES);
	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_CH0_CTRL0, cap->id),
			   ISP_WRMIFX3_0_CH0_CTRL0_STRIDE_MASK,
			   ISP_WRMIFX3_0_CH0_CTRL0_STRIDE(stride));

	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_CH0_CTRL1, cap->id),
			   ISP_WRMIFX3_0_CH0_CTRL1_PIX_BITS_MODE_MASK,
			   info->ch0_pix_bits);

	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_WIN_LUMA_H, cap->id),
		     ISP_WRMIFX3_0_WIN_LUMA_H_LUMA_HEND(pix_mp->width));

	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_WIN_LUMA_V, cap->id),
		     ISP_WRMIFX3_0_WIN_LUMA_V_LUMA_VEND(pix_mp->height));

	stride = DIV_ROUND_UP(pix_mp->plane_fmt[C3_ISP_PLANE_UV].bytesperline,
			      C3_ISP_DMA_SIZE_ALIGN_BYTES);
	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_CH1_CTRL0, cap->id),
			   ISP_WRMIFX3_0_CH1_CTRL0_STRIDE_MASK,
			   ISP_WRMIFX3_0_CH1_CTRL0_STRIDE(stride));

	c3_isp_update_bits(cap->isp,
			   C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_CH1_CTRL1, cap->id),
			   ISP_WRMIFX3_0_CH1_CTRL1_PIX_BITS_MODE_MASK,
			   ISP_WRMIFX3_0_CH1_CTRL1_PIX_BITS_16BITS);

	chrom_h = DIV_ROUND_UP(pix_mp->width, info->hdiv);
	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_WIN_CHROM_H, cap->id),
		     ISP_WRMIFX3_0_WIN_CHROM_H_CHROM_HEND(chrom_h));

	chrom_v = DIV_ROUND_UP(pix_mp->height, info->vdiv);
	c3_isp_write(cap->isp,
		     C3_ISP_WRMIFX3_REG(ISP_WRMIFX3_0_WIN_CHROM_V, cap->id),
		     ISP_WRMIFX3_0_WIN_CHROM_V_CHROM_VEND(chrom_v));
}

static int c3_isp_cap_dummy_buff_create(struct c3_isp_capture *cap)
{
	struct c3_isp_dummy_buffer *dummy_buff = &cap->dummy_buff;
	struct v4l2_pix_format_mplane *pix_mp = &cap->format.pix_mp;

	if (pix_mp->num_planes == 1)
		dummy_buff->size = pix_mp->plane_fmt[C3_ISP_PLANE_Y].sizeimage;
	else
		dummy_buff->size =
			max(pix_mp->plane_fmt[C3_ISP_PLANE_Y].sizeimage,
			    pix_mp->plane_fmt[C3_ISP_PLANE_UV].sizeimage);

	/* The driver never access vaddr, no mapping is required */
	dummy_buff->vaddr = dma_alloc_attrs(cap->isp->dev, dummy_buff->size,
					    &dummy_buff->dma_addr, GFP_KERNEL,
					    DMA_ATTR_NO_KERNEL_MAPPING);
	if (!dummy_buff->vaddr)
		return -ENOMEM;

	return 0;
}

static void c3_isp_cap_dummy_buff_destroy(struct c3_isp_capture *cap)
{
	dma_free_attrs(cap->isp->dev, cap->dummy_buff.size,
		       cap->dummy_buff.vaddr, cap->dummy_buff.dma_addr,
		       DMA_ATTR_NO_KERNEL_MAPPING);
}

static void c3_isp_cap_cfg_buff(struct c3_isp_capture *cap)
{
	cap->buff = list_first_entry_or_null(&cap->pending,
					     struct c3_isp_cap_buffer, list);

	c3_isp_cap_wrmifx3_buff(cap);

	if (cap->buff)
		list_del(&cap->buff->list);
}

static void c3_isp_cap_start(struct c3_isp_capture *cap)
{
	u32 mask;
	u32 val;

	scoped_guard(spinlock_irqsave, &cap->buff_lock)
		c3_isp_cap_cfg_buff(cap);

	c3_isp_cap_wrmifx3_format(cap);

	if (cap->id == C3_ISP_CAP_DEV_0) {
		mask = ISP_TOP_PATH_EN_WRMIF0_EN_MASK;
		val = ISP_TOP_PATH_EN_WRMIF0_EN;
	} else if (cap->id == C3_ISP_CAP_DEV_1) {
		mask = ISP_TOP_PATH_EN_WRMIF1_EN_MASK;
		val = ISP_TOP_PATH_EN_WRMIF1_EN;
	} else {
		mask = ISP_TOP_PATH_EN_WRMIF2_EN_MASK;
		val = ISP_TOP_PATH_EN_WRMIF2_EN;
	}

	c3_isp_update_bits(cap->isp, ISP_TOP_PATH_EN, mask, val);
}

static void c3_isp_cap_stop(struct c3_isp_capture *cap)
{
	u32 mask;
	u32 val;

	if (cap->id == C3_ISP_CAP_DEV_0) {
		mask = ISP_TOP_PATH_EN_WRMIF0_EN_MASK;
		val = ISP_TOP_PATH_EN_WRMIF0_DIS;
	} else if (cap->id == C3_ISP_CAP_DEV_1) {
		mask = ISP_TOP_PATH_EN_WRMIF1_EN_MASK;
		val = ISP_TOP_PATH_EN_WRMIF1_DIS;
	} else {
		mask = ISP_TOP_PATH_EN_WRMIF2_EN_MASK;
		val = ISP_TOP_PATH_EN_WRMIF2_DIS;
	}

	c3_isp_update_bits(cap->isp, ISP_TOP_PATH_EN, mask, val);
}

static void c3_isp_cap_done(struct c3_isp_capture *cap)
{
	struct c3_isp_cap_buffer *buff = cap->buff;

	guard(spinlock_irqsave)(&cap->buff_lock);

	if (buff) {
		buff->vb.sequence = cap->isp->frm_sequence;
		buff->vb.vb2_buf.timestamp = ktime_get();
		buff->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&buff->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	c3_isp_cap_cfg_buff(cap);
}

/* V4L2 video operations */

static const struct c3_isp_cap_format_info *c3_cap_find_fmt(u32 fourcc)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(cap_formats); i++) {
		if (cap_formats[i].fourcc == fourcc)
			return &cap_formats[i];
	}

	return NULL;
}

static void c3_cap_try_fmt(struct v4l2_pix_format_mplane *pix_mp)
{
	const struct c3_isp_cap_format_info *fmt;
	const struct v4l2_format_info *info;
	struct v4l2_plane_pix_format *plane;

	fmt = c3_cap_find_fmt(pix_mp->pixelformat);
	if (!fmt)
		fmt = &cap_formats[0];

	pix_mp->width = clamp(pix_mp->width, C3_ISP_MIN_WIDTH,
			      C3_ISP_MAX_WIDTH);
	pix_mp->height = clamp(pix_mp->height, C3_ISP_MIN_HEIGHT,
			       C3_ISP_MAX_HEIGHT);
	pix_mp->pixelformat = fmt->fourcc;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
	pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_601;
	pix_mp->quantization = V4L2_QUANTIZATION_LIM_RANGE;

	info = v4l2_format_info(fmt->fourcc);
	pix_mp->num_planes = info->mem_planes;
	memset(pix_mp->plane_fmt, 0, sizeof(pix_mp->plane_fmt));

	for (unsigned int i = 0; i < info->comp_planes; i++) {
		unsigned int hdiv = (i == 0) ? 1 : info->hdiv;
		unsigned int vdiv = (i == 0) ? 1 : info->vdiv;

		plane = &pix_mp->plane_fmt[i];

		plane->bytesperline = DIV_ROUND_UP(pix_mp->width, hdiv) *
				      info->bpp[i] / info->bpp_div[i];
		plane->bytesperline = ALIGN(plane->bytesperline,
					    C3_ISP_DMA_SIZE_ALIGN_BYTES);
		plane->sizeimage = plane->bytesperline *
				   DIV_ROUND_UP(pix_mp->height, vdiv);
	}
}

static void c3_isp_cap_return_buffers(struct c3_isp_capture *cap,
				      enum vb2_buffer_state state)
{
	struct c3_isp_cap_buffer *buff;

	guard(spinlock_irqsave)(&cap->buff_lock);

	if (cap->buff) {
		vb2_buffer_done(&cap->buff->vb.vb2_buf, state);
		cap->buff = NULL;
	}

	while (!list_empty(&cap->pending)) {
		buff = list_first_entry(&cap->pending,
					struct c3_isp_cap_buffer, list);
		list_del(&buff->list);
		vb2_buffer_done(&buff->vb.vb2_buf, state);
	}
}

static int c3_isp_cap_querycap(struct file *file, void *fh,
			       struct v4l2_capability *cap)
{
	strscpy(cap->driver, C3_ISP_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "AML C3 ISP", sizeof(cap->card));

	return 0;
}

static int c3_isp_cap_enum_fmt(struct file *file, void *fh,
			       struct v4l2_fmtdesc *f)
{
	const struct c3_isp_cap_format_info *fmt;
	unsigned int index = 0;
	unsigned int i;

	if (!f->mbus_code) {
		if (f->index >= ARRAY_SIZE(cap_formats))
			return -EINVAL;

		fmt = &cap_formats[f->index];
		f->pixelformat = fmt->fourcc;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(cap_formats); i++) {
		fmt = &cap_formats[i];
		if (f->mbus_code != fmt->mbus_code)
			continue;

		if (index++ == f->index) {
			f->pixelformat = cap_formats[i].fourcc;
			return 0;
		}
	}

	return -EINVAL;
}

static int c3_isp_cap_g_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct c3_isp_capture *cap = video_drvdata(file);

	f->fmt.pix_mp = cap->format.pix_mp;

	return 0;
}

static int c3_isp_cap_s_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct c3_isp_capture *cap = video_drvdata(file);

	c3_cap_try_fmt(&f->fmt.pix_mp);

	cap->format.pix_mp = f->fmt.pix_mp;
	cap->format.info = c3_cap_find_fmt(f->fmt.pix_mp.pixelformat);

	return 0;
}

static int c3_isp_cap_try_fmt_mplane(struct file *file, void *fh,
				     struct v4l2_format *f)
{
	c3_cap_try_fmt(&f->fmt.pix_mp);

	return 0;
}

static int c3_isp_cap_enum_frmsize(struct file *file, void *fh,
				   struct v4l2_frmsizeenum *fsize)
{
	const struct c3_isp_cap_format_info *fmt;

	if (fsize->index)
		return -EINVAL;

	fmt = c3_cap_find_fmt(fsize->pixel_format);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = C3_ISP_MIN_WIDTH;
	fsize->stepwise.min_height = C3_ISP_MIN_HEIGHT;
	fsize->stepwise.max_width = C3_ISP_MAX_WIDTH;
	fsize->stepwise.max_height = C3_ISP_MAX_HEIGHT;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.step_height = 2;

	return 0;
}

static const struct v4l2_ioctl_ops isp_cap_v4l2_ioctl_ops = {
	.vidioc_querycap		= c3_isp_cap_querycap,
	.vidioc_enum_fmt_vid_cap	= c3_isp_cap_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= c3_isp_cap_g_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= c3_isp_cap_s_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= c3_isp_cap_try_fmt_mplane,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_enum_framesizes         = c3_isp_cap_enum_frmsize,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations isp_cap_v4l2_fops = {
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

static int c3_isp_cap_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct v4l2_subdev *sd =
		media_entity_to_v4l2_subdev(link->source->entity);
	struct c3_isp_capture *cap = video_get_drvdata(vdev);
	struct v4l2_subdev_format src_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = link->source->index,
	};
	int ret;

	ret = v4l2_subdev_call_state_active(sd, pad, get_fmt, &src_fmt);
	if (ret)
		return ret;

	if (src_fmt.format.width != cap->format.pix_mp.width ||
	    src_fmt.format.height != cap->format.pix_mp.height ||
	    src_fmt.format.code != cap->format.info->mbus_code) {
		dev_err(cap->isp->dev,
			"link %s: %u -> %s: %u not valid: 0x%04x/%ux%u not match 0x%04x/%ux%u\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index,
			src_fmt.format.code, src_fmt.format.width,
			src_fmt.format.height, cap->format.info->mbus_code,
			cap->format.pix_mp.width, cap->format.pix_mp.height);

		return -EPIPE;
	}

	return 0;
}

static const struct media_entity_operations isp_cap_entity_ops = {
	.link_validate = c3_isp_cap_link_validate,
};

static int c3_isp_vb2_queue_setup(struct vb2_queue *q,
				  unsigned int *num_buffers,
				  unsigned int *num_planes,
				  unsigned int sizes[],
				  struct device *alloc_devs[])
{
	struct c3_isp_capture *cap = vb2_get_drv_priv(q);
	const struct v4l2_pix_format_mplane *pix_mp = &cap->format.pix_mp;
	unsigned int i;

	if (*num_planes) {
		if (*num_planes != pix_mp->num_planes)
			return -EINVAL;

		for (i = 0; i < pix_mp->num_planes; i++)
			if (sizes[i] < pix_mp->plane_fmt[i].sizeimage)
				return -EINVAL;

		return 0;
	}

	*num_planes = pix_mp->num_planes;
	for (i = 0; i < pix_mp->num_planes; i++)
		sizes[i] = pix_mp->plane_fmt[i].sizeimage;

	return 0;
}

static void c3_isp_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_cap_buffer *buf =
			container_of(v4l2_buf, struct c3_isp_cap_buffer, vb);
	struct c3_isp_capture *cap = vb2_get_drv_priv(vb->vb2_queue);

	guard(spinlock_irqsave)(&cap->buff_lock);

	list_add_tail(&buf->list, &cap->pending);
}

static int c3_isp_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct c3_isp_capture *cap = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size;

	for (unsigned int i = 0; i < cap->format.pix_mp.num_planes; i++) {
		size = cap->format.pix_mp.plane_fmt[i].sizeimage;
		if (vb2_plane_size(vb, i) < size) {
			dev_err(cap->isp->dev,
				"User buffer too small (%ld < %lu)\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static int c3_isp_vb2_buf_init(struct vb2_buffer *vb)
{
	struct c3_isp_capture *cap = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_cap_buffer *buf =
		container_of(v4l2_buf, struct c3_isp_cap_buffer, vb);

	for (unsigned int i = 0; i < cap->format.pix_mp.num_planes; i++)
		buf->dma_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	return 0;
}

static int c3_isp_vb2_start_streaming(struct vb2_queue *q,
				      unsigned int count)
{
	struct c3_isp_capture *cap = vb2_get_drv_priv(q);
	int ret;

	ret = video_device_pipeline_start(&cap->vdev, &cap->isp->pipe);
	if (ret) {
		dev_err(cap->isp->dev,
			"Failed to start cap%u pipeline: %d\n", cap->id, ret);
		goto err_return_buffers;
	}

	ret = c3_isp_cap_dummy_buff_create(cap);
	if (ret)
		goto err_pipeline_stop;

	ret = pm_runtime_resume_and_get(cap->isp->dev);
	if (ret)
		goto err_dummy_destroy;

	c3_isp_cap_start(cap);

	ret = v4l2_subdev_enable_streams(&cap->rsz->sd, C3_ISP_RSZ_PAD_SOURCE,
					 BIT(0));
	if (ret)
		goto err_pm_put;

	return 0;

err_pm_put:
	pm_runtime_put(cap->isp->dev);
err_dummy_destroy:
	c3_isp_cap_dummy_buff_destroy(cap);
err_pipeline_stop:
	video_device_pipeline_stop(&cap->vdev);
err_return_buffers:
	c3_isp_cap_return_buffers(cap, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void c3_isp_vb2_stop_streaming(struct vb2_queue *q)
{
	struct c3_isp_capture *cap = vb2_get_drv_priv(q);

	c3_isp_cap_stop(cap);

	c3_isp_cap_return_buffers(cap, VB2_BUF_STATE_ERROR);

	v4l2_subdev_disable_streams(&cap->rsz->sd, C3_ISP_RSZ_PAD_SOURCE,
				    BIT(0));

	pm_runtime_put(cap->isp->dev);

	c3_isp_cap_dummy_buff_destroy(cap);

	video_device_pipeline_stop(&cap->vdev);
}

static const struct vb2_ops isp_video_vb2_ops = {
	.queue_setup = c3_isp_vb2_queue_setup,
	.buf_queue = c3_isp_vb2_buf_queue,
	.buf_prepare = c3_isp_vb2_buf_prepare,
	.buf_init = c3_isp_vb2_buf_init,
	.start_streaming = c3_isp_vb2_start_streaming,
	.stop_streaming = c3_isp_vb2_stop_streaming,
};

static int c3_isp_register_capture(struct c3_isp_capture *cap)
{
	struct video_device *vdev = &cap->vdev;
	struct vb2_queue *vb2_q = &cap->vb2_q;
	int ret;

	snprintf(vdev->name, sizeof(vdev->name), "c3-isp-cap%u", cap->id);
	vdev->fops = &isp_cap_v4l2_fops;
	vdev->ioctl_ops = &isp_cap_v4l2_ioctl_ops;
	vdev->v4l2_dev = &cap->isp->v4l2_dev;
	vdev->entity.ops = &isp_cap_entity_ops;
	vdev->lock = &cap->lock;
	vdev->minor = -1;
	vdev->queue = vb2_q;
	vdev->release = video_device_release_empty;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_RX;
	video_set_drvdata(vdev, cap);

	vb2_q->drv_priv = cap;
	vb2_q->mem_ops = &vb2_dma_contig_memops;
	vb2_q->ops = &isp_video_vb2_ops;
	vb2_q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	vb2_q->io_modes = VB2_DMABUF | VB2_MMAP;
	vb2_q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2_q->buf_struct_size = sizeof(struct c3_isp_cap_buffer);
	vb2_q->dev = cap->isp->dev;
	vb2_q->lock = &cap->lock;

	ret = vb2_queue_init(vb2_q);
	if (ret)
		goto err_destroy;

	cap->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &cap->pad);
	if (ret)
		goto err_queue_release;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(cap->isp->dev,
			"Failed to register %s: %d\n", vdev->name, ret);
		goto err_entity_cleanup;
	}

	return 0;

err_entity_cleanup:
	media_entity_cleanup(&vdev->entity);
err_queue_release:
	vb2_queue_release(vb2_q);
err_destroy:
	mutex_destroy(&cap->lock);
	return ret;
}

int c3_isp_captures_register(struct c3_isp_device *isp)
{
	int ret;
	unsigned int i;
	struct c3_isp_capture *cap;

	for (i = C3_ISP_CAP_DEV_0; i < C3_ISP_NUM_CAP_DEVS; i++) {
		cap = &isp->caps[i];
		memset(cap, 0, sizeof(*cap));

		cap->format.pix_mp.width = C3_ISP_DEFAULT_WIDTH;
		cap->format.pix_mp.height = C3_ISP_DEFAULT_HEIGHT;
		cap->format.pix_mp.field = V4L2_FIELD_NONE;
		cap->format.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
		cap->format.pix_mp.colorspace = V4L2_COLORSPACE_SRGB;
		cap->format.info =
			c3_cap_find_fmt(cap->format.pix_mp.pixelformat);

		c3_cap_try_fmt(&cap->format.pix_mp);

		cap->id = i;
		cap->rsz = &isp->resizers[i];
		cap->isp = isp;
		INIT_LIST_HEAD(&cap->pending);
		spin_lock_init(&cap->buff_lock);
		mutex_init(&cap->lock);

		ret = c3_isp_register_capture(cap);
		if (ret) {
			cap->isp = NULL;
			mutex_destroy(&cap->lock);
			c3_isp_captures_unregister(isp);
			return ret;
		}
	}

	return 0;
}

void c3_isp_captures_unregister(struct c3_isp_device *isp)
{
	unsigned int i;
	struct c3_isp_capture *cap;

	for (i = C3_ISP_CAP_DEV_0; i < C3_ISP_NUM_CAP_DEVS; i++) {
		cap = &isp->caps[i];

		if (!cap->isp)
			continue;
		vb2_queue_release(&cap->vb2_q);
		media_entity_cleanup(&cap->vdev.entity);
		video_unregister_device(&cap->vdev);
		mutex_destroy(&cap->lock);
	}
}

void c3_isp_captures_isr(struct c3_isp_device *isp)
{
	c3_isp_cap_done(&isp->caps[C3_ISP_CAP_DEV_0]);
	c3_isp_cap_done(&isp->caps[C3_ISP_CAP_DEV_1]);
	c3_isp_cap_done(&isp->caps[C3_ISP_CAP_DEV_2]);
}
