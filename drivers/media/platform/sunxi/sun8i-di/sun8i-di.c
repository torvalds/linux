// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner sun8i deinterlacer with scaler driver
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * Based on vim2m driver.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>

#include "sun8i-di.h"

#define FLAG_SIZE (DEINTERLACE_MAX_WIDTH * DEINTERLACE_MAX_HEIGHT / 4)

static u32 deinterlace_formats[] = {
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
};

static inline u32 deinterlace_read(struct deinterlace_dev *dev, u32 reg)
{
	return readl(dev->base + reg);
}

static inline void deinterlace_write(struct deinterlace_dev *dev,
				     u32 reg, u32 value)
{
	writel(value, dev->base + reg);
}

static inline void deinterlace_set_bits(struct deinterlace_dev *dev,
					u32 reg, u32 bits)
{
	writel(readl(dev->base + reg) | bits, dev->base + reg);
}

static inline void deinterlace_clr_set_bits(struct deinterlace_dev *dev,
					    u32 reg, u32 clr, u32 set)
{
	u32 val = readl(dev->base + reg);

	val &= ~clr;
	val |= set;

	writel(val, dev->base + reg);
}

static void deinterlace_device_run(void *priv)
{
	struct deinterlace_ctx *ctx = priv;
	struct deinterlace_dev *dev = ctx->dev;
	u32 size, stride, width, height, val;
	struct vb2_v4l2_buffer *src, *dst;
	unsigned int hstep, vstep;
	dma_addr_t addr;

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	v4l2_m2m_buf_copy_metadata(src, dst, true);

	deinterlace_write(dev, DEINTERLACE_MOD_ENABLE,
			  DEINTERLACE_MOD_ENABLE_EN);

	if (ctx->field) {
		deinterlace_write(dev, DEINTERLACE_TILE_FLAG0,
				  ctx->flag1_buf_dma);
		deinterlace_write(dev, DEINTERLACE_TILE_FLAG1,
				  ctx->flag2_buf_dma);
	} else {
		deinterlace_write(dev, DEINTERLACE_TILE_FLAG0,
				  ctx->flag2_buf_dma);
		deinterlace_write(dev, DEINTERLACE_TILE_FLAG1,
				  ctx->flag1_buf_dma);
	}
	deinterlace_write(dev, DEINTERLACE_FLAG_LINE_STRIDE, 0x200);

	width = ctx->src_fmt.width;
	height = ctx->src_fmt.height;
	stride = ctx->src_fmt.bytesperline;
	size = stride * height;

	addr = vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 0);
	deinterlace_write(dev, DEINTERLACE_BUF_ADDR0, addr);
	deinterlace_write(dev, DEINTERLACE_BUF_ADDR1, addr + size);
	deinterlace_write(dev, DEINTERLACE_BUF_ADDR2, 0);

	deinterlace_write(dev, DEINTERLACE_LINE_STRIDE0, stride);
	deinterlace_write(dev, DEINTERLACE_LINE_STRIDE1, stride);

	deinterlace_write(dev, DEINTERLACE_CH0_IN_SIZE,
			  DEINTERLACE_SIZE(width, height));
	deinterlace_write(dev, DEINTERLACE_CH1_IN_SIZE,
			  DEINTERLACE_SIZE(width / 2, height / 2));

	val = DEINTERLACE_IN_FMT_FMT(DEINTERLACE_IN_FMT_YUV420) |
	      DEINTERLACE_IN_FMT_MOD(DEINTERLACE_MODE_UV_COMBINED);
	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_NV12:
		val |= DEINTERLACE_IN_FMT_PS(DEINTERLACE_PS_UVUV);
		break;
	case V4L2_PIX_FMT_NV21:
		val |= DEINTERLACE_IN_FMT_PS(DEINTERLACE_PS_VUVU);
		break;
	}
	deinterlace_write(dev, DEINTERLACE_IN_FMT, val);

	if (ctx->prev)
		addr = vb2_dma_contig_plane_dma_addr(&ctx->prev->vb2_buf, 0);

	deinterlace_write(dev, DEINTERLACE_PRELUMA, addr);
	deinterlace_write(dev, DEINTERLACE_PRECHROMA, addr + size);

	val = DEINTERLACE_OUT_FMT_FMT(DEINTERLACE_OUT_FMT_YUV420SP);
	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_NV12:
		val |= DEINTERLACE_OUT_FMT_PS(DEINTERLACE_PS_UVUV);
		break;
	case V4L2_PIX_FMT_NV21:
		val |= DEINTERLACE_OUT_FMT_PS(DEINTERLACE_PS_VUVU);
		break;
	}
	deinterlace_write(dev, DEINTERLACE_OUT_FMT, val);

	width = ctx->dst_fmt.width;
	height = ctx->dst_fmt.height;
	stride = ctx->dst_fmt.bytesperline;
	size = stride * height;

	deinterlace_write(dev, DEINTERLACE_CH0_OUT_SIZE,
			  DEINTERLACE_SIZE(width, height));
	deinterlace_write(dev, DEINTERLACE_CH1_OUT_SIZE,
			  DEINTERLACE_SIZE(width / 2, height / 2));

	deinterlace_write(dev, DEINTERLACE_WB_LINE_STRIDE0, stride);
	deinterlace_write(dev, DEINTERLACE_WB_LINE_STRIDE1, stride);

	addr = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);
	deinterlace_write(dev, DEINTERLACE_WB_ADDR0, addr);
	deinterlace_write(dev, DEINTERLACE_WB_ADDR1, addr + size);
	deinterlace_write(dev, DEINTERLACE_WB_ADDR2, 0);

	hstep = (ctx->src_fmt.width << 16) / ctx->dst_fmt.width;
	vstep = (ctx->src_fmt.height << 16) / ctx->dst_fmt.height;
	deinterlace_write(dev, DEINTERLACE_CH0_HORZ_FACT, hstep);
	deinterlace_write(dev, DEINTERLACE_CH0_VERT_FACT, vstep);
	deinterlace_write(dev, DEINTERLACE_CH1_HORZ_FACT, hstep);
	deinterlace_write(dev, DEINTERLACE_CH1_VERT_FACT, vstep);

	deinterlace_clr_set_bits(dev, DEINTERLACE_FIELD_CTRL,
				 DEINTERLACE_FIELD_CTRL_FIELD_CNT_MSK,
				 DEINTERLACE_FIELD_CTRL_FIELD_CNT(ctx->field));

	deinterlace_set_bits(dev, DEINTERLACE_FRM_CTRL,
			     DEINTERLACE_FRM_CTRL_START);

	deinterlace_set_bits(dev, DEINTERLACE_FRM_CTRL,
			     DEINTERLACE_FRM_CTRL_REG_READY);

	deinterlace_set_bits(dev, DEINTERLACE_INT_ENABLE,
			     DEINTERLACE_INT_ENABLE_WB_EN);

	deinterlace_set_bits(dev, DEINTERLACE_FRM_CTRL,
			     DEINTERLACE_FRM_CTRL_WB_EN);
}

static int deinterlace_job_ready(void *priv)
{
	struct deinterlace_ctx *ctx = priv;

	return v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) >= 1 &&
	       v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) >= 2;
}

static void deinterlace_job_abort(void *priv)
{
	struct deinterlace_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	ctx->aborting = 1;
}

static irqreturn_t deinterlace_irq(int irq, void *data)
{
	struct deinterlace_dev *dev = data;
	struct vb2_v4l2_buffer *src, *dst;
	enum vb2_buffer_state state;
	struct deinterlace_ctx *ctx;
	unsigned int val;

	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (!ctx) {
		v4l2_err(&dev->v4l2_dev,
			 "Instance released before the end of transaction\n");
		return IRQ_NONE;
	}

	val = deinterlace_read(dev, DEINTERLACE_INT_STATUS);
	if (!(val & DEINTERLACE_INT_STATUS_WRITEBACK))
		return IRQ_NONE;

	deinterlace_write(dev, DEINTERLACE_INT_ENABLE, 0);
	deinterlace_set_bits(dev, DEINTERLACE_INT_STATUS,
			     DEINTERLACE_INT_STATUS_WRITEBACK);
	deinterlace_write(dev, DEINTERLACE_MOD_ENABLE, 0);
	deinterlace_clr_set_bits(dev, DEINTERLACE_FRM_CTRL,
				 DEINTERLACE_FRM_CTRL_START, 0);

	val = deinterlace_read(dev, DEINTERLACE_STATUS);
	if (val & DEINTERLACE_STATUS_WB_ERROR)
		state = VB2_BUF_STATE_ERROR;
	else
		state = VB2_BUF_STATE_DONE;

	dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(dst, state);

	if (ctx->field != ctx->first_field || ctx->aborting) {
		ctx->field = ctx->first_field;

		src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		if (ctx->prev)
			v4l2_m2m_buf_done(ctx->prev, state);
		ctx->prev = src;

		v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
	} else {
		ctx->field = !ctx->first_field;
		deinterlace_device_run(ctx);
	}

	return IRQ_HANDLED;
}

static void deinterlace_init(struct deinterlace_dev *dev)
{
	u32 val;
	int i;

	deinterlace_write(dev, DEINTERLACE_BYPASS,
			  DEINTERLACE_BYPASS_CSC);
	deinterlace_write(dev, DEINTERLACE_WB_LINE_STRIDE_CTRL,
			  DEINTERLACE_WB_LINE_STRIDE_CTRL_EN);
	deinterlace_set_bits(dev, DEINTERLACE_FRM_CTRL,
			     DEINTERLACE_FRM_CTRL_OUT_CTRL);
	deinterlace_write(dev, DEINTERLACE_AGTH_SEL,
			  DEINTERLACE_AGTH_SEL_LINEBUF);

	val = DEINTERLACE_CTRL_EN |
	      DEINTERLACE_CTRL_MODE_MIXED |
	      DEINTERLACE_CTRL_DIAG_INTP_EN |
	      DEINTERLACE_CTRL_TEMP_DIFF_EN;
	deinterlace_write(dev, DEINTERLACE_CTRL, val);

	deinterlace_clr_set_bits(dev, DEINTERLACE_LUMA_TH,
				 DEINTERLACE_LUMA_TH_MIN_LUMA_MSK,
				 DEINTERLACE_LUMA_TH_MIN_LUMA(4));

	deinterlace_clr_set_bits(dev, DEINTERLACE_SPAT_COMP,
				 DEINTERLACE_SPAT_COMP_TH2_MSK,
				 DEINTERLACE_SPAT_COMP_TH2(5));

	deinterlace_clr_set_bits(dev, DEINTERLACE_TEMP_DIFF,
				 DEINTERLACE_TEMP_DIFF_AMBIGUITY_TH_MSK,
				 DEINTERLACE_TEMP_DIFF_AMBIGUITY_TH(5));

	val = DEINTERLACE_DIAG_INTP_TH0(60) |
	      DEINTERLACE_DIAG_INTP_TH1(0) |
	      DEINTERLACE_DIAG_INTP_TH3(30);
	deinterlace_write(dev, DEINTERLACE_DIAG_INTP, val);

	deinterlace_clr_set_bits(dev, DEINTERLACE_CHROMA_DIFF,
				 DEINTERLACE_CHROMA_DIFF_TH_MSK,
				 DEINTERLACE_CHROMA_DIFF_TH(5));

	/* neutral filter coefficients */
	deinterlace_set_bits(dev, DEINTERLACE_FRM_CTRL,
			     DEINTERLACE_FRM_CTRL_COEF_ACCESS);
	readl_poll_timeout(dev->base + DEINTERLACE_STATUS, val,
			   val & DEINTERLACE_STATUS_COEF_STATUS, 2, 40);

	for (i = 0; i < 32; i++) {
		deinterlace_write(dev, DEINTERLACE_CH0_HORZ_COEF0 + i * 4,
				  DEINTERLACE_IDENTITY_COEF);
		deinterlace_write(dev, DEINTERLACE_CH0_VERT_COEF + i * 4,
				  DEINTERLACE_IDENTITY_COEF);
		deinterlace_write(dev, DEINTERLACE_CH1_HORZ_COEF0 + i * 4,
				  DEINTERLACE_IDENTITY_COEF);
		deinterlace_write(dev, DEINTERLACE_CH1_VERT_COEF + i * 4,
				  DEINTERLACE_IDENTITY_COEF);
	}

	deinterlace_clr_set_bits(dev, DEINTERLACE_FRM_CTRL,
				 DEINTERLACE_FRM_CTRL_COEF_ACCESS, 0);
}

static inline struct deinterlace_ctx *deinterlace_file2ctx(struct file *file)
{
	return container_of(file->private_data, struct deinterlace_ctx, fh);
}

static bool deinterlace_check_format(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(deinterlace_formats); i++)
		if (deinterlace_formats[i] == pixelformat)
			return true;

	return false;
}

static void deinterlace_prepare_format(struct v4l2_pix_format *pix_fmt)
{
	unsigned int height = pix_fmt->height;
	unsigned int width = pix_fmt->width;
	unsigned int bytesperline;
	unsigned int sizeimage;

	width = clamp(width, DEINTERLACE_MIN_WIDTH,
		      DEINTERLACE_MAX_WIDTH);
	height = clamp(height, DEINTERLACE_MIN_HEIGHT,
		       DEINTERLACE_MAX_HEIGHT);

	bytesperline = ALIGN(width, 2);
	/* luma */
	sizeimage = bytesperline * height;
	/* chroma */
	sizeimage += bytesperline * height / 2;

	pix_fmt->width = width;
	pix_fmt->height = height;
	pix_fmt->bytesperline = bytesperline;
	pix_fmt->sizeimage = sizeimage;
}

static int deinterlace_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	strscpy(cap->driver, DEINTERLACE_NAME, sizeof(cap->driver));
	strscpy(cap->card, DEINTERLACE_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", DEINTERLACE_NAME);

	return 0;
}

static int deinterlace_enum_fmt(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	if (f->index < ARRAY_SIZE(deinterlace_formats)) {
		f->pixelformat = deinterlace_formats[f->index];

		return 0;
	}

	return -EINVAL;
}

static int deinterlace_enum_framesizes(struct file *file, void *priv,
				       struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0)
		return -EINVAL;

	if (!deinterlace_check_format(fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = DEINTERLACE_MIN_WIDTH;
	fsize->stepwise.min_height = DEINTERLACE_MIN_HEIGHT;
	fsize->stepwise.max_width = DEINTERLACE_MAX_WIDTH;
	fsize->stepwise.max_height = DEINTERLACE_MAX_HEIGHT;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int deinterlace_g_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct deinterlace_ctx *ctx = deinterlace_file2ctx(file);

	f->fmt.pix = ctx->dst_fmt;

	return 0;
}

static int deinterlace_g_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct deinterlace_ctx *ctx = deinterlace_file2ctx(file);

	f->fmt.pix = ctx->src_fmt;

	return 0;
}

static int deinterlace_try_fmt_vid_cap(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	if (!deinterlace_check_format(f->fmt.pix.pixelformat))
		f->fmt.pix.pixelformat = deinterlace_formats[0];

	if (f->fmt.pix.field != V4L2_FIELD_NONE)
		f->fmt.pix.field = V4L2_FIELD_NONE;

	deinterlace_prepare_format(&f->fmt.pix);

	return 0;
}

static int deinterlace_try_fmt_vid_out(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	if (!deinterlace_check_format(f->fmt.pix.pixelformat))
		f->fmt.pix.pixelformat = deinterlace_formats[0];

	if (f->fmt.pix.field != V4L2_FIELD_INTERLACED_TB &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED_BT &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED)
		f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	deinterlace_prepare_format(&f->fmt.pix);

	return 0;
}

static int deinterlace_s_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct deinterlace_ctx *ctx = deinterlace_file2ctx(file);
	struct vb2_queue *vq;
	int ret;

	ret = deinterlace_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ctx->dst_fmt = f->fmt.pix;

	return 0;
}

static int deinterlace_s_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct deinterlace_ctx *ctx = deinterlace_file2ctx(file);
	struct vb2_queue *vq;
	int ret;

	ret = deinterlace_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ctx->src_fmt = f->fmt.pix;

	/* Propagate colorspace information to capture. */
	ctx->dst_fmt.colorspace = f->fmt.pix.colorspace;
	ctx->dst_fmt.xfer_func = f->fmt.pix.xfer_func;
	ctx->dst_fmt.ycbcr_enc = f->fmt.pix.ycbcr_enc;
	ctx->dst_fmt.quantization = f->fmt.pix.quantization;

	return 0;
}

static const struct v4l2_ioctl_ops deinterlace_ioctl_ops = {
	.vidioc_querycap		= deinterlace_querycap,

	.vidioc_enum_framesizes		= deinterlace_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= deinterlace_enum_fmt,
	.vidioc_g_fmt_vid_cap		= deinterlace_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= deinterlace_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= deinterlace_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= deinterlace_enum_fmt,
	.vidioc_g_fmt_vid_out		= deinterlace_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= deinterlace_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= deinterlace_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
};

static int deinterlace_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				   unsigned int *nplanes, unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt;
	else
		pix_fmt = &ctx->dst_fmt;

	if (*nplanes) {
		if (sizes[0] < pix_fmt->sizeimage)
			return -EINVAL;
	} else {
		sizes[0] = pix_fmt->sizeimage;
		*nplanes = 1;
	}

	return 0;
}

static int deinterlace_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt;
	else
		pix_fmt = &ctx->dst_fmt;

	if (vb2_plane_size(vb, 0) < pix_fmt->sizeimage)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);

	return 0;
}

static void deinterlace_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void deinterlace_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	do {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (vbuf)
			v4l2_m2m_buf_done(vbuf, state);
	} while (vbuf);

	if (V4L2_TYPE_IS_OUTPUT(vq->type) && ctx->prev)
		v4l2_m2m_buf_done(ctx->prev, state);
}

static int deinterlace_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vq);
	struct device *dev = ctx->dev->dev;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0) {
			dev_err(dev, "Failed to enable module\n");

			goto err_runtime_get;
		}

		ctx->first_field =
			ctx->src_fmt.field == V4L2_FIELD_INTERLACED_BT;
		ctx->field = ctx->first_field;

		ctx->prev = NULL;
		ctx->aborting = 0;

		ctx->flag1_buf = dma_alloc_coherent(dev, FLAG_SIZE,
						    &ctx->flag1_buf_dma,
						    GFP_KERNEL);
		if (!ctx->flag1_buf) {
			ret = -ENOMEM;

			goto err_no_mem1;
		}

		ctx->flag2_buf = dma_alloc_coherent(dev, FLAG_SIZE,
						    &ctx->flag2_buf_dma,
						    GFP_KERNEL);
		if (!ctx->flag2_buf) {
			ret = -ENOMEM;

			goto err_no_mem2;
		}
	}

	return 0;

err_no_mem2:
	dma_free_coherent(dev, FLAG_SIZE, ctx->flag1_buf,
			  ctx->flag1_buf_dma);
err_no_mem1:
	pm_runtime_put(dev);
err_runtime_get:
	deinterlace_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void deinterlace_stop_streaming(struct vb2_queue *vq)
{
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		struct device *dev = ctx->dev->dev;

		dma_free_coherent(dev, FLAG_SIZE, ctx->flag1_buf,
				  ctx->flag1_buf_dma);
		dma_free_coherent(dev, FLAG_SIZE, ctx->flag2_buf,
				  ctx->flag2_buf_dma);

		pm_runtime_put(dev);
	}

	deinterlace_queue_cleanup(vq, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops deinterlace_qops = {
	.queue_setup		= deinterlace_queue_setup,
	.buf_prepare		= deinterlace_buf_prepare,
	.buf_queue		= deinterlace_buf_queue,
	.start_streaming	= deinterlace_start_streaming,
	.stop_streaming		= deinterlace_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int deinterlace_queue_init(void *priv, struct vb2_queue *src_vq,
				  struct vb2_queue *dst_vq)
{
	struct deinterlace_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->min_buffers_needed = 1;
	src_vq->ops = &deinterlace_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->dev_mutex;
	src_vq->dev = ctx->dev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->min_buffers_needed = 2;
	dst_vq->ops = &deinterlace_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->dev_mutex;
	dst_vq->dev = ctx->dev->dev;

	ret = vb2_queue_init(dst_vq);
	if (ret)
		return ret;

	return 0;
}

static int deinterlace_open(struct file *file)
{
	struct deinterlace_dev *dev = video_drvdata(file);
	struct deinterlace_ctx *ctx = NULL;
	int ret;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mutex_unlock(&dev->dev_mutex);
		return -ENOMEM;
	}

	/* default output format */
	ctx->src_fmt.pixelformat = deinterlace_formats[0];
	ctx->src_fmt.field = V4L2_FIELD_INTERLACED;
	ctx->src_fmt.width = 640;
	ctx->src_fmt.height = 480;
	deinterlace_prepare_format(&ctx->src_fmt);

	/* default capture format */
	ctx->dst_fmt.pixelformat = deinterlace_formats[0];
	ctx->dst_fmt.field = V4L2_FIELD_NONE;
	ctx->dst_fmt.width = 640;
	ctx->dst_fmt.height = 480;
	deinterlace_prepare_format(&ctx->dst_fmt);

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx,
					    &deinterlace_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_free;
	}

	v4l2_fh_add(&ctx->fh);

	mutex_unlock(&dev->dev_mutex);

	return 0;

err_free:
	kfree(ctx);
	mutex_unlock(&dev->dev_mutex);

	return ret;
}

static int deinterlace_release(struct file *file)
{
	struct deinterlace_dev *dev = video_drvdata(file);
	struct deinterlace_ctx *ctx = container_of(file->private_data,
						   struct deinterlace_ctx, fh);

	mutex_lock(&dev->dev_mutex);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	kfree(ctx);

	mutex_unlock(&dev->dev_mutex);

	return 0;
}

static const struct v4l2_file_operations deinterlace_fops = {
	.owner		= THIS_MODULE,
	.open		= deinterlace_open,
	.release	= deinterlace_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device deinterlace_video_device = {
	.name		= DEINTERLACE_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &deinterlace_fops,
	.ioctl_ops	= &deinterlace_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.device_caps	= V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static const struct v4l2_m2m_ops deinterlace_m2m_ops = {
	.device_run	= deinterlace_device_run,
	.job_ready	= deinterlace_job_ready,
	.job_abort	= deinterlace_job_abort,
};

static int deinterlace_probe(struct platform_device *pdev)
{
	struct deinterlace_dev *dev;
	struct video_device *vfd;
	struct resource *res;
	int irq, ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->vfd = deinterlace_video_device;
	dev->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return irq;

	ret = devm_request_irq(dev->dev, irq, deinterlace_irq,
			       0, dev_name(dev->dev), dev);
	if (ret) {
		dev_err(dev->dev, "Failed to request IRQ\n");

		return ret;
	}

	ret = of_dma_configure(dev->dev, dev->dev->of_node, true);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->bus_clk = devm_clk_get(dev->dev, "bus");
	if (IS_ERR(dev->bus_clk)) {
		dev_err(dev->dev, "Failed to get bus clock\n");

		return PTR_ERR(dev->bus_clk);
	}

	dev->mod_clk = devm_clk_get(dev->dev, "mod");
	if (IS_ERR(dev->mod_clk)) {
		dev_err(dev->dev, "Failed to get mod clock\n");

		return PTR_ERR(dev->mod_clk);
	}

	dev->ram_clk = devm_clk_get(dev->dev, "ram");
	if (IS_ERR(dev->ram_clk)) {
		dev_err(dev->dev, "Failed to get ram clock\n");

		return PTR_ERR(dev->ram_clk);
	}

	dev->rstc = devm_reset_control_get(dev->dev, NULL);
	if (IS_ERR(dev->rstc)) {
		dev_err(dev->dev, "Failed to get reset control\n");

		return PTR_ERR(dev->rstc);
	}

	mutex_init(&dev->dev_mutex);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(dev->dev, "Failed to register V4L2 device\n");

		return ret;
	}

	vfd = &dev->vfd;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	snprintf(vfd->name, sizeof(vfd->name), "%s",
		 deinterlace_video_device.name);
	video_set_drvdata(vfd, dev);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");

		goto err_v4l2;
	}

	v4l2_info(&dev->v4l2_dev,
		  "Device registered as /dev/video%d\n", vfd->num);

	dev->m2m_dev = v4l2_m2m_init(&deinterlace_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to initialize V4L2 M2M device\n");
		ret = PTR_ERR(dev->m2m_dev);

		goto err_video;
	}

	platform_set_drvdata(pdev, dev);

	pm_runtime_enable(dev->dev);

	return 0;

err_video:
	video_unregister_device(&dev->vfd);
err_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static int deinterlace_remove(struct platform_device *pdev)
{
	struct deinterlace_dev *dev = platform_get_drvdata(pdev);

	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(&dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);

	pm_runtime_force_suspend(&pdev->dev);

	return 0;
}

static int deinterlace_runtime_resume(struct device *device)
{
	struct deinterlace_dev *dev = dev_get_drvdata(device);
	int ret;

	ret = clk_set_rate_exclusive(dev->mod_clk, 300000000);
	if (ret) {
		dev_err(dev->dev, "Failed to set exclusive mod clock rate\n");

		return ret;
	}

	ret = clk_prepare_enable(dev->bus_clk);
	if (ret) {
		dev_err(dev->dev, "Failed to enable bus clock\n");

		goto err_exclusive_rate;
	}

	ret = clk_prepare_enable(dev->mod_clk);
	if (ret) {
		dev_err(dev->dev, "Failed to enable mod clock\n");

		goto err_bus_clk;
	}

	ret = clk_prepare_enable(dev->ram_clk);
	if (ret) {
		dev_err(dev->dev, "Failed to enable ram clock\n");

		goto err_mod_clk;
	}

	ret = reset_control_deassert(dev->rstc);
	if (ret) {
		dev_err(dev->dev, "Failed to apply reset\n");

		goto err_ram_clk;
	}

	deinterlace_init(dev);

	return 0;

err_ram_clk:
	clk_disable_unprepare(dev->ram_clk);
err_mod_clk:
	clk_disable_unprepare(dev->mod_clk);
err_bus_clk:
	clk_disable_unprepare(dev->bus_clk);
err_exclusive_rate:
	clk_rate_exclusive_put(dev->mod_clk);

	return ret;
}

static int deinterlace_runtime_suspend(struct device *device)
{
	struct deinterlace_dev *dev = dev_get_drvdata(device);

	reset_control_assert(dev->rstc);

	clk_disable_unprepare(dev->ram_clk);
	clk_disable_unprepare(dev->mod_clk);
	clk_disable_unprepare(dev->bus_clk);
	clk_rate_exclusive_put(dev->mod_clk);

	return 0;
}

static const struct of_device_id deinterlace_dt_match[] = {
	{ .compatible = "allwinner,sun8i-h3-deinterlace" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, deinterlace_dt_match);

static const struct dev_pm_ops deinterlace_pm_ops = {
	.runtime_resume		= deinterlace_runtime_resume,
	.runtime_suspend	= deinterlace_runtime_suspend,
};

static struct platform_driver deinterlace_driver = {
	.probe		= deinterlace_probe,
	.remove		= deinterlace_remove,
	.driver		= {
		.name		= DEINTERLACE_NAME,
		.of_match_table	= deinterlace_dt_match,
		.pm		= &deinterlace_pm_ops,
	},
};
module_platform_driver(deinterlace_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@siol.net>");
MODULE_DESCRIPTION("Allwinner Deinterlace driver");
