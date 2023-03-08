// SPDX-License-Identifier: GPL-2.0+
/*
 * DW100 Hardware dewarper
 *
 * Copyright 2022 NXP
 * Author: Xavier Roumegue (xavier.roumegue@oss.nxp.com)
 *
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include <uapi/linux/dw100.h>

#include "dw100_regs.h"

#define DRV_NAME "dw100"

#define DW100_MIN_W		176u
#define DW100_MIN_H		144u
#define DW100_MAX_W		4096u
#define DW100_MAX_H		3072u
#define DW100_ALIGN_W		3
#define DW100_ALIGN_H		3

#define DW100_BLOCK_SIZE	16

#define DW100_DEF_W		640u
#define DW100_DEF_H		480u
#define DW100_DEF_LUT_W		(DIV_ROUND_UP(DW100_DEF_W, DW100_BLOCK_SIZE) + 1)
#define DW100_DEF_LUT_H		(DIV_ROUND_UP(DW100_DEF_H, DW100_BLOCK_SIZE) + 1)

/*
 * 16 controls have been reserved for this driver for future extension, but
 * let's limit the related driver allocation to the effective number of controls
 * in use.
 */
#define DW100_MAX_CTRLS			1
#define DW100_CTRL_DEWARPING_MAP	0

enum {
	DW100_QUEUE_SRC = 0,
	DW100_QUEUE_DST = 1,
};

enum {
	DW100_FMT_CAPTURE = BIT(0),
	DW100_FMT_OUTPUT = BIT(1),
};

struct dw100_device {
	struct platform_device		*pdev;
	struct v4l2_m2m_dev		*m2m_dev;
	struct v4l2_device		v4l2_dev;
	struct video_device		vfd;
	struct media_device		mdev;
	/* Video device lock */
	struct mutex			vfd_mutex;
	void __iomem			*mmio;
	struct clk_bulk_data		*clks;
	int				num_clks;
	struct dentry			*debugfs_root;
};

struct dw100_q_data {
	struct v4l2_pix_format_mplane	pix_fmt;
	unsigned int			sequence;
	const struct dw100_fmt		*fmt;
	struct v4l2_rect		crop;
};

struct dw100_ctx {
	struct v4l2_fh			fh;
	struct dw100_device		*dw_dev;
	struct v4l2_ctrl_handler	hdl;
	struct v4l2_ctrl		*ctrls[DW100_MAX_CTRLS];
	/* per context m2m queue lock */
	struct mutex			vq_mutex;

	/* Look Up Table for pixel remapping */
	unsigned int			*map;
	dma_addr_t			map_dma;
	size_t				map_size;
	unsigned int			map_width;
	unsigned int			map_height;
	bool				user_map_is_set;

	/* Source and destination queue data */
	struct dw100_q_data		q_data[2];
};

static const struct v4l2_frmsize_stepwise dw100_frmsize_stepwise = {
	.min_width = DW100_MIN_W,
	.min_height = DW100_MIN_H,
	.max_width = DW100_MAX_W,
	.max_height = DW100_MAX_H,
	.step_width = 1UL << DW100_ALIGN_W,
	.step_height = 1UL << DW100_ALIGN_H,
};

static const struct dw100_fmt {
	u32 fourcc;
	u32 types;
	u32 reg_format;
	bool reg_swap_uv;
} formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.types = DW100_FMT_OUTPUT | DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV422_SP,
		.reg_swap_uv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16M,
		.types = DW100_FMT_OUTPUT | DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV422_SP,
		.reg_swap_uv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.types = DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV422_SP,
		.reg_swap_uv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61M,
		.types = DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV422_SP,
		.reg_swap_uv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.types = DW100_FMT_OUTPUT | DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV422_PACKED,
		.reg_swap_uv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.types = DW100_FMT_OUTPUT | DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV422_PACKED,
		.reg_swap_uv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.types = DW100_FMT_OUTPUT | DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV420_SP,
		.reg_swap_uv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.types = DW100_FMT_OUTPUT | DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV420_SP,
		.reg_swap_uv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21,
		.types = DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV420_SP,
		.reg_swap_uv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.types = DW100_FMT_CAPTURE,
		.reg_format = DW100_DEWARP_CTRL_FORMAT_YUV420_SP,
		.reg_swap_uv = true,
	},
};

static inline int to_dw100_fmt_type(enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return DW100_FMT_OUTPUT;
	else
		return DW100_FMT_CAPTURE;
}

static const struct dw100_fmt *dw100_find_pixel_format(u32 pixel_format,
						       int fmt_type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		const struct dw100_fmt *fmt = &formats[i];

		if (fmt->fourcc == pixel_format && fmt->types & fmt_type)
			return fmt;
	}

	return NULL;
}

static const struct dw100_fmt *dw100_find_format(struct v4l2_format *f)
{
	return dw100_find_pixel_format(f->fmt.pix_mp.pixelformat,
				       to_dw100_fmt_type(f->type));
}

static inline u32 dw100_read(struct dw100_device *dw_dev, u32 reg)
{
	return readl(dw_dev->mmio + reg);
}

static inline void dw100_write(struct dw100_device *dw_dev, u32 reg, u32 val)
{
	writel(val, dw_dev->mmio + reg);
}

static inline int dw100_dump_regs(struct seq_file *m)
{
	struct dw100_device *dw_dev = m->private;
#define __DECLARE_REG(x) { #x, x }
	unsigned int i;
	static const struct reg_desc {
		const char * const name;
		unsigned int addr;
	} dw100_regs[] = {
		__DECLARE_REG(DW100_DEWARP_ID),
		__DECLARE_REG(DW100_DEWARP_CTRL),
		__DECLARE_REG(DW100_MAP_LUT_ADDR),
		__DECLARE_REG(DW100_MAP_LUT_SIZE),
		__DECLARE_REG(DW100_MAP_LUT_ADDR2),
		__DECLARE_REG(DW100_MAP_LUT_SIZE2),
		__DECLARE_REG(DW100_SRC_IMG_Y_BASE),
		__DECLARE_REG(DW100_SRC_IMG_UV_BASE),
		__DECLARE_REG(DW100_SRC_IMG_SIZE),
		__DECLARE_REG(DW100_SRC_IMG_STRIDE),
		__DECLARE_REG(DW100_DST_IMG_Y_BASE),
		__DECLARE_REG(DW100_DST_IMG_UV_BASE),
		__DECLARE_REG(DW100_DST_IMG_SIZE),
		__DECLARE_REG(DW100_DST_IMG_STRIDE),
		__DECLARE_REG(DW100_DST_IMG_Y_SIZE1),
		__DECLARE_REG(DW100_DST_IMG_UV_SIZE1),
		__DECLARE_REG(DW100_SRC_IMG_Y_BASE2),
		__DECLARE_REG(DW100_SRC_IMG_UV_BASE2),
		__DECLARE_REG(DW100_SRC_IMG_SIZE2),
		__DECLARE_REG(DW100_SRC_IMG_STRIDE2),
		__DECLARE_REG(DW100_DST_IMG_Y_BASE2),
		__DECLARE_REG(DW100_DST_IMG_UV_BASE2),
		__DECLARE_REG(DW100_DST_IMG_SIZE2),
		__DECLARE_REG(DW100_DST_IMG_STRIDE2),
		__DECLARE_REG(DW100_DST_IMG_Y_SIZE2),
		__DECLARE_REG(DW100_DST_IMG_UV_SIZE2),
		__DECLARE_REG(DW100_SWAP_CONTROL),
		__DECLARE_REG(DW100_VERTICAL_SPLIT_LINE),
		__DECLARE_REG(DW100_HORIZON_SPLIT_LINE),
		__DECLARE_REG(DW100_SCALE_FACTOR),
		__DECLARE_REG(DW100_ROI_START),
		__DECLARE_REG(DW100_BOUNDARY_PIXEL),
		__DECLARE_REG(DW100_INTERRUPT_STATUS),
		__DECLARE_REG(DW100_BUS_CTRL),
		__DECLARE_REG(DW100_BUS_CTRL1),
		__DECLARE_REG(DW100_BUS_TIME_OUT_CYCLE),
	};

	for (i = 0; i < ARRAY_SIZE(dw100_regs); i++)
		seq_printf(m, "%s: %#x\n", dw100_regs[i].name,
			   dw100_read(dw_dev, dw100_regs[i].addr));

	return 0;
}

static inline struct dw100_ctx *dw100_file2ctx(struct file *file)
{
	return container_of(file->private_data, struct dw100_ctx, fh);
}

static struct dw100_q_data *dw100_get_q_data(struct dw100_ctx *ctx,
					     enum v4l2_buf_type type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return &ctx->q_data[DW100_QUEUE_SRC];
	else
		return &ctx->q_data[DW100_QUEUE_DST];
}

static u32 dw100_get_n_vertices_from_length(u32 length)
{
	return DIV_ROUND_UP(length, DW100_BLOCK_SIZE) + 1;
}

static u16 dw100_map_convert_to_uq12_4(u32 a)
{
	return (u16)((a & 0xfff) << 4);
}

static u32 dw100_map_format_coordinates(u16 xq, u16 yq)
{
	return (u32)((yq << 16) | xq);
}

static u32 *dw100_get_user_map(struct dw100_ctx *ctx)
{
	struct v4l2_ctrl *ctrl = ctx->ctrls[DW100_CTRL_DEWARPING_MAP];

	return ctrl->p_cur.p_u32;
}

/*
 * Create the dewarp map used by the hardware from the V4L2 control values which
 * have been initialized with an identity map or set by the application.
 */
static int dw100_create_mapping(struct dw100_ctx *ctx)
{
	u32 *user_map;

	if (ctx->map)
		dma_free_coherent(&ctx->dw_dev->pdev->dev, ctx->map_size,
				  ctx->map, ctx->map_dma);

	ctx->map = dma_alloc_coherent(&ctx->dw_dev->pdev->dev, ctx->map_size,
				      &ctx->map_dma, GFP_KERNEL);

	if (!ctx->map)
		return -ENOMEM;

	user_map = dw100_get_user_map(ctx);
	memcpy(ctx->map, user_map, ctx->map_size);

	dev_dbg(&ctx->dw_dev->pdev->dev,
		"%ux%u %s mapping created (d:%pad-c:%p) for stream %ux%u->%ux%u\n",
		ctx->map_width, ctx->map_height,
		ctx->user_map_is_set ? "user" : "identity",
		&ctx->map_dma, ctx->map,
		ctx->q_data[DW100_QUEUE_SRC].pix_fmt.width,
		ctx->q_data[DW100_QUEUE_DST].pix_fmt.height,
		ctx->q_data[DW100_QUEUE_SRC].pix_fmt.width,
		ctx->q_data[DW100_QUEUE_DST].pix_fmt.height);

	return 0;
}

static void dw100_destroy_mapping(struct dw100_ctx *ctx)
{
	if (ctx->map) {
		dma_free_coherent(&ctx->dw_dev->pdev->dev, ctx->map_size,
				  ctx->map, ctx->map_dma);
		ctx->map = NULL;
	}
}

static int dw100_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw100_ctx *ctx =
		container_of(ctrl->handler, struct dw100_ctx, hdl);

	switch (ctrl->id) {
	case V4L2_CID_DW100_DEWARPING_16x16_VERTEX_MAP:
		ctx->user_map_is_set = true;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops dw100_ctrl_ops = {
	.s_ctrl = dw100_s_ctrl,
};

/*
 * Initialize the dewarping map with an identity mapping.
 *
 * A 16 pixels cell size grid is mapped on the destination image.
 * The last cells width/height might be lesser than 16 if the destination image
 * width/height is not divisible by 16. This dewarping grid map specifies the
 * source image pixel location (x, y) on each grid intersection point.
 * Bilinear interpolation is used to compute inner cell points locations.
 *
 * The coordinates are saved in UQ12.4 fixed point format.
 */
static void dw100_ctrl_dewarping_map_init(const struct v4l2_ctrl *ctrl,
					  u32 from_idx,
					  union v4l2_ctrl_ptr ptr)
{
	struct dw100_ctx *ctx =
		container_of(ctrl->handler, struct dw100_ctx, hdl);

	u32 sw, sh, mw, mh, idx;
	u16 qx, qy, qdx, qdy, qsh, qsw;
	u32 *map = ctrl->p_cur.p_u32;

	sw = ctx->q_data[DW100_QUEUE_SRC].pix_fmt.width;
	sh = ctx->q_data[DW100_QUEUE_SRC].pix_fmt.height;

	mw = ctrl->dims[0];
	mh = ctrl->dims[1];

	qsw = dw100_map_convert_to_uq12_4(sw);
	qsh = dw100_map_convert_to_uq12_4(sh);
	qdx = qsw / (mw - 1);
	qdy = qsh / (mh - 1);

	ctx->map_width = mw;
	ctx->map_height = mh;
	ctx->map_size = mh * mw * sizeof(u32);

	for (idx = from_idx; idx < ctrl->elems; idx++) {
		qy = min_t(u32, (idx / mw) * qdy, qsh);
		qx = min_t(u32, (idx % mw) * qdx, qsw);
		map[idx] = dw100_map_format_coordinates(qx, qy);
	}

	ctx->user_map_is_set = false;
}

static const struct v4l2_ctrl_type_ops dw100_ctrl_type_ops = {
	.init = dw100_ctrl_dewarping_map_init,
	.validate = v4l2_ctrl_type_op_validate,
	.log = v4l2_ctrl_type_op_log,
	.equal = v4l2_ctrl_type_op_equal,
};

static const struct v4l2_ctrl_config controls[] = {
	[DW100_CTRL_DEWARPING_MAP] = {
		.ops = &dw100_ctrl_ops,
		.type_ops = &dw100_ctrl_type_ops,
		.id = V4L2_CID_DW100_DEWARPING_16x16_VERTEX_MAP,
		.name = "Dewarping Vertex Map",
		.type = V4L2_CTRL_TYPE_U32,
		.min = 0x00000000,
		.max = 0xffffffff,
		.step = 1,
		.def = 0,
		.dims = { DW100_DEF_LUT_W, DW100_DEF_LUT_H },
	},
};

static int dw100_queue_setup(struct vb2_queue *vq,
			     unsigned int *nbuffers, unsigned int *nplanes,
			     unsigned int sizes[], struct device *alloc_devs[])
{
	struct dw100_ctx *ctx = vb2_get_drv_priv(vq);
	const struct v4l2_pix_format_mplane *format;
	unsigned int i;

	format = &dw100_get_q_data(ctx, vq->type)->pix_fmt;

	if (*nplanes) {
		if (*nplanes != format->num_planes)
			return -EINVAL;

		for (i = 0; i < *nplanes; ++i) {
			if (sizes[i] < format->plane_fmt[i].sizeimage)
				return -EINVAL;
		}

		return 0;
	}

	*nplanes = format->num_planes;

	for (i = 0; i < format->num_planes; ++i)
		sizes[i] = format->plane_fmt[i].sizeimage;

	return 0;
}

static int dw100_buf_prepare(struct vb2_buffer *vb)
{
	unsigned int i;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dw100_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct dw100_device *dw_dev = ctx->dw_dev;
	const struct v4l2_pix_format_mplane *pix_fmt =
		&dw100_get_q_data(ctx, vb->vb2_queue->type)->pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field != V4L2_FIELD_NONE) {
			dev_dbg(&dw_dev->pdev->dev, "%x field isn't supported\n",
				vbuf->field);
			return -EINVAL;
		}
	}

	for (i = 0; i < pix_fmt->num_planes; i++) {
		unsigned long size = pix_fmt->plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < size) {
			dev_dbg(&dw_dev->pdev->dev,
				"User buffer too small (%lu < %lu)\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void dw100_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dw100_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void dw100_return_all_buffers(struct vb2_queue *q,
				     enum vb2_buffer_state state)
{
	struct dw100_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			return;
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static int dw100_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct dw100_ctx *ctx = vb2_get_drv_priv(q);
	struct dw100_q_data *q_data = dw100_get_q_data(ctx, q->type);
	int ret;

	q_data->sequence = 0;

	ret = dw100_create_mapping(ctx);
	if (ret)
		goto err;

	ret = pm_runtime_resume_and_get(&ctx->dw_dev->pdev->dev);
	if (ret) {
		dw100_destroy_mapping(ctx);
		goto err;
	}

	return 0;
err:
	dw100_return_all_buffers(q, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void dw100_stop_streaming(struct vb2_queue *q)
{
	struct dw100_ctx *ctx = vb2_get_drv_priv(q);

	dw100_return_all_buffers(q, VB2_BUF_STATE_ERROR);

	pm_runtime_put_sync(&ctx->dw_dev->pdev->dev);

	dw100_destroy_mapping(ctx);
}

static const struct vb2_ops dw100_qops = {
	.queue_setup	 = dw100_queue_setup,
	.buf_prepare	 = dw100_buf_prepare,
	.buf_queue	 = dw100_buf_queue,
	.start_streaming = dw100_start_streaming,
	.stop_streaming  = dw100_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

static int dw100_m2m_queue_init(void *priv, struct vb2_queue *src_vq,
				struct vb2_queue *dst_vq)
{
	struct dw100_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &dw100_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->vq_mutex;
	src_vq->dev = ctx->dw_dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &dw100_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->vq_mutex;
	dst_vq->dev = ctx->dw_dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int dw100_open(struct file *file)
{
	struct dw100_device *dw_dev = video_drvdata(file);
	struct dw100_ctx *ctx;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_pix_format_mplane *pix_fmt;
	int ret, i;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->vq_mutex);
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dw_dev = dw_dev;

	ctx->q_data[DW100_QUEUE_SRC].fmt = &formats[0];

	pix_fmt = &ctx->q_data[DW100_QUEUE_SRC].pix_fmt;
	pix_fmt->field = V4L2_FIELD_NONE;
	pix_fmt->colorspace = V4L2_COLORSPACE_REC709;
	pix_fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix_fmt->colorspace);
	pix_fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix_fmt->colorspace);
	pix_fmt->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, pix_fmt->colorspace,
					      pix_fmt->ycbcr_enc);

	v4l2_fill_pixfmt_mp(pix_fmt, formats[0].fourcc, DW100_DEF_W, DW100_DEF_H);

	ctx->q_data[DW100_QUEUE_SRC].crop.top = 0;
	ctx->q_data[DW100_QUEUE_SRC].crop.left = 0;
	ctx->q_data[DW100_QUEUE_SRC].crop.width = DW100_DEF_W;
	ctx->q_data[DW100_QUEUE_SRC].crop.height = DW100_DEF_H;

	ctx->q_data[DW100_QUEUE_DST] = ctx->q_data[DW100_QUEUE_SRC];

	hdl = &ctx->hdl;
	v4l2_ctrl_handler_init(hdl, ARRAY_SIZE(controls));
	for (i = 0; i < ARRAY_SIZE(controls); i++) {
		ctx->ctrls[i] = v4l2_ctrl_new_custom(hdl, &controls[i], NULL);
		if (hdl->error) {
			dev_err(&ctx->dw_dev->pdev->dev,
				"Adding control (%d) failed\n", i);
			ret = hdl->error;
			goto err;
		}
	}
	ctx->fh.ctrl_handler = hdl;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dw_dev->m2m_dev,
					    ctx, &dw100_m2m_queue_init);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err;
	}

	v4l2_fh_add(&ctx->fh);

	return 0;

err:
	v4l2_ctrl_handler_free(hdl);
	v4l2_fh_exit(&ctx->fh);
	mutex_destroy(&ctx->vq_mutex);
	kfree(ctx);

	return ret;
}

static int dw100_release(struct file *file)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mutex_destroy(&ctx->vq_mutex);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations dw100_fops = {
	.owner		= THIS_MODULE,
	.open		= dw100_open,
	.release	= dw100_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static int dw100_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	strscpy(cap->driver, DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, "DW100 dewarper", sizeof(cap->card));

	return 0;
}

static int dw100_enum_fmt_vid(struct file *file, void *priv,
			      struct v4l2_fmtdesc *f)
{
	int i, num = 0;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].types & to_dw100_fmt_type(f->type)) {
			if (num == f->index) {
				f->pixelformat = formats[i].fourcc;
				return 0;
			}
			++num;
		}
	}

	return -EINVAL;
}

static int dw100_enum_framesizes(struct file *file, void *priv,
				 struct v4l2_frmsizeenum *fsize)
{
	const struct dw100_fmt *fmt;

	if (fsize->index)
		return -EINVAL;

	fmt = dw100_find_pixel_format(fsize->pixel_format,
				      DW100_FMT_OUTPUT | DW100_FMT_CAPTURE);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = dw100_frmsize_stepwise;

	return 0;
}

static int dw100_g_fmt_vid(struct file *file, void *priv, struct v4l2_format *f)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);
	struct vb2_queue *vq;
	struct dw100_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = dw100_get_q_data(ctx, f->type);

	f->fmt.pix_mp = q_data->pix_fmt;

	return 0;
}

static int dw100_try_fmt(struct file *file, struct v4l2_format *f)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	const struct dw100_fmt *fmt;

	fmt = dw100_find_format(f);
	if (!fmt) {
		fmt = &formats[0];
		pix->pixelformat = fmt->fourcc;
	}

	v4l2_apply_frmsize_constraints(&pix->width, &pix->height,
				       &dw100_frmsize_stepwise);

	v4l2_fill_pixfmt_mp(pix, fmt->fourcc, pix->width, pix->height);

	pix->field = V4L2_FIELD_NONE;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (pix->colorspace == V4L2_COLORSPACE_DEFAULT)
			pix->colorspace = V4L2_COLORSPACE_REC709;
		if (pix->xfer_func == V4L2_XFER_FUNC_DEFAULT)
			pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);
		if (pix->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
			pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
		if (pix->quantization == V4L2_QUANTIZATION_DEFAULT)
			pix->quantization =
				V4L2_MAP_QUANTIZATION_DEFAULT(false,
							      pix->colorspace,
							      pix->ycbcr_enc);
	} else {
		/*
		 * The DW100 can't perform colorspace conversion, the colorspace
		 * on the capture queue must be identical to the output queue.
		 */
		const struct dw100_q_data *q_data =
			dw100_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		pix->colorspace = q_data->pix_fmt.colorspace;
		pix->xfer_func = q_data->pix_fmt.xfer_func;
		pix->ycbcr_enc = q_data->pix_fmt.ycbcr_enc;
		pix->quantization = q_data->pix_fmt.quantization;
	}

	return 0;
}

static int dw100_s_fmt(struct dw100_ctx *ctx, struct v4l2_format *f)
{
	struct dw100_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = dw100_get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		dev_dbg(&ctx->dw_dev->pdev->dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fmt = dw100_find_format(f);
	q_data->pix_fmt = f->fmt.pix_mp;
	q_data->crop.top = 0;
	q_data->crop.left = 0;
	q_data->crop.width = f->fmt.pix_mp.width;
	q_data->crop.height = f->fmt.pix_mp.height;

	/* Propagate buffers encoding */

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct dw100_q_data *dst_q_data =
			dw100_get_q_data(ctx,
					 V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

		dst_q_data->pix_fmt.colorspace = q_data->pix_fmt.colorspace;
		dst_q_data->pix_fmt.ycbcr_enc = q_data->pix_fmt.ycbcr_enc;
		dst_q_data->pix_fmt.quantization = q_data->pix_fmt.quantization;
		dst_q_data->pix_fmt.xfer_func = q_data->pix_fmt.xfer_func;
	}

	dev_dbg(&ctx->dw_dev->pdev->dev,
		"Setting format for type %u, wxh: %ux%u, fmt: %p4cc\n",
		f->type, q_data->pix_fmt.width, q_data->pix_fmt.height,
		&q_data->pix_fmt.pixelformat);

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		int ret;
		u32 dims[V4L2_CTRL_MAX_DIMS] = {};
		struct v4l2_ctrl *ctrl = ctx->ctrls[DW100_CTRL_DEWARPING_MAP];

		dims[0] = dw100_get_n_vertices_from_length(q_data->pix_fmt.width);
		dims[1] = dw100_get_n_vertices_from_length(q_data->pix_fmt.height);

		ret = v4l2_ctrl_modify_dimensions(ctrl, dims);

		if (ret) {
			dev_err(&ctx->dw_dev->pdev->dev,
				"Modifying LUT dimensions failed with error %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int dw100_try_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	return dw100_try_fmt(file, f);
}

static int dw100_s_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);
	int ret;

	ret = dw100_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	ret = dw100_s_fmt(ctx, f);
	if (ret)
		return ret;

	return 0;
}

static int dw100_try_fmt_vid_out(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	return dw100_try_fmt(file, f);
}

static int dw100_s_fmt_vid_out(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);
	int ret;

	ret = dw100_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = dw100_s_fmt(ctx, f);
	if (ret)
		return ret;

	return 0;
}

static int dw100_g_selection(struct file *file, void *fh,
			     struct v4l2_selection *sel)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);
	struct dw100_q_data *src_q_data;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	src_q_data = dw100_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = src_q_data->pix_fmt.width;
		sel->r.height = src_q_data->pix_fmt.height;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r.top = src_q_data->crop.top;
		sel->r.left = src_q_data->crop.left;
		sel->r.width = src_q_data->crop.width;
		sel->r.height = src_q_data->crop.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dw100_s_selection(struct file *file, void *fh,
			     struct v4l2_selection *sel)
{
	struct dw100_ctx *ctx = dw100_file2ctx(file);
	struct dw100_q_data *src_q_data;
	u32 qscalex, qscaley, qscale;
	int x, y, w, h;
	unsigned int wframe, hframe;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	src_q_data = dw100_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	dev_dbg(&ctx->dw_dev->pdev->dev,
		">>> Buffer Type: %u Target: %u Rect: %ux%u@%d.%d\n",
		sel->type, sel->target,
		sel->r.width, sel->r.height, sel->r.left, sel->r.top);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		wframe = src_q_data->pix_fmt.width;
		hframe = src_q_data->pix_fmt.height;

		sel->r.top = clamp_t(int, sel->r.top, 0, hframe - DW100_MIN_H);
		sel->r.left = clamp_t(int, sel->r.left, 0, wframe - DW100_MIN_W);
		sel->r.height =
			clamp(sel->r.height, DW100_MIN_H, hframe - sel->r.top);
		sel->r.width =
			clamp(sel->r.width, DW100_MIN_W, wframe - sel->r.left);

		/* UQ16.16 for float operations */
		qscalex = (sel->r.width << 16) / wframe;
		qscaley = (sel->r.height << 16) / hframe;
		y = sel->r.top;
		x = sel->r.left;
		if (qscalex == qscaley) {
			qscale = qscalex;
		} else {
			switch (sel->flags) {
			case 0:
				qscale = (qscalex + qscaley) / 2;
				break;
			case V4L2_SEL_FLAG_GE:
				qscale = max(qscaley, qscalex);
				break;
			case V4L2_SEL_FLAG_LE:
				qscale = min(qscaley, qscalex);
				break;
			case V4L2_SEL_FLAG_LE | V4L2_SEL_FLAG_GE:
				return -ERANGE;
			default:
				return -EINVAL;
			}
		}

		w = (u32)((((u64)wframe << 16) * qscale) >> 32);
		h = (u32)((((u64)hframe << 16) * qscale) >> 32);
		x = x + (sel->r.width  - w) / 2;
		y = y + (sel->r.height  - h) / 2;
		x = min(wframe - w, (unsigned int)max(0, x));
		y = min(hframe - h, (unsigned int)max(0, y));

		sel->r.top = y;
		sel->r.left = x;
		sel->r.width = w;
		sel->r.height = h;

		src_q_data->crop.top = sel->r.top;
		src_q_data->crop.left = sel->r.left;
		src_q_data->crop.width = sel->r.width;
		src_q_data->crop.height = sel->r.height;
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(&ctx->dw_dev->pdev->dev,
		"<<< Buffer Type: %u Target: %u Rect: %ux%u@%d.%d\n",
		sel->type, sel->target,
		sel->r.width, sel->r.height, sel->r.left, sel->r.top);

	return 0;
}

static const struct v4l2_ioctl_ops dw100_ioctl_ops = {
	.vidioc_querycap		= dw100_querycap,

	.vidioc_enum_fmt_vid_cap	= dw100_enum_fmt_vid,
	.vidioc_enum_framesizes		= dw100_enum_framesizes,
	.vidioc_g_fmt_vid_cap_mplane	= dw100_g_fmt_vid,
	.vidioc_try_fmt_vid_cap_mplane	= dw100_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane	= dw100_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= dw100_enum_fmt_vid,
	.vidioc_g_fmt_vid_out_mplane	= dw100_g_fmt_vid,
	.vidioc_try_fmt_vid_out_mplane	= dw100_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out_mplane	= dw100_s_fmt_vid_out,

	.vidioc_g_selection		= dw100_g_selection,
	.vidioc_s_selection		= dw100_s_selection,
	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static void dw100_job_finish(struct dw100_device *dw_dev, bool with_error)
{
	struct dw100_ctx *curr_ctx;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	enum vb2_buffer_state buf_state;

	curr_ctx = v4l2_m2m_get_curr_priv(dw_dev->m2m_dev);

	if (!curr_ctx) {
		dev_err(&dw_dev->pdev->dev,
			"Instance released before the end of transaction\n");
		return;
	}

	src_vb = v4l2_m2m_src_buf_remove(curr_ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(curr_ctx->fh.m2m_ctx);

	if (likely(!with_error))
		buf_state = VB2_BUF_STATE_DONE;
	else
		buf_state = VB2_BUF_STATE_ERROR;

	v4l2_m2m_buf_done(src_vb, buf_state);
	v4l2_m2m_buf_done(dst_vb, buf_state);

	dev_dbg(&dw_dev->pdev->dev, "Finishing transaction with%s error(s)\n",
		with_error ? "" : "out");

	v4l2_m2m_job_finish(dw_dev->m2m_dev, curr_ctx->fh.m2m_ctx);
}

static void dw100_hw_reset(struct dw100_device *dw_dev)
{
	u32 val;

	val = dw100_read(dw_dev, DW100_DEWARP_CTRL);
	val |= DW100_DEWARP_CTRL_ENABLE;
	val |= DW100_DEWARP_CTRL_SOFT_RESET;
	dw100_write(dw_dev, DW100_DEWARP_CTRL, val);
	val &= ~DW100_DEWARP_CTRL_SOFT_RESET;
	dw100_write(dw_dev, DW100_DEWARP_CTRL, val);
}

static void _dw100_hw_set_master_bus_enable(struct dw100_device *dw_dev,
					    unsigned int enable)
{
	u32 val;

	dev_dbg(&dw_dev->pdev->dev, "%sable master bus\n",
		enable ? "En" : "Dis");

	val = dw100_read(dw_dev, DW100_BUS_CTRL);

	if (enable)
		val |= DW100_BUS_CTRL_AXI_MASTER_ENABLE;
	else
		val &= ~DW100_BUS_CTRL_AXI_MASTER_ENABLE;

	dw100_write(dw_dev, DW100_BUS_CTRL, val);
}

static void dw100_hw_master_bus_enable(struct dw100_device *dw_dev)
{
	_dw100_hw_set_master_bus_enable(dw_dev, 1);
}

static void dw100_hw_master_bus_disable(struct dw100_device *dw_dev)
{
	_dw100_hw_set_master_bus_enable(dw_dev, 0);
}

static void dw100_hw_dewarp_start(struct dw100_device *dw_dev)
{
	u32 val;

	val = dw100_read(dw_dev, DW100_DEWARP_CTRL);

	dev_dbg(&dw_dev->pdev->dev, "Starting Hardware CTRL:0x%08x\n", val);
	dw100_write(dw_dev, DW100_DEWARP_CTRL, val | DW100_DEWARP_CTRL_START);
	dw100_write(dw_dev, DW100_DEWARP_CTRL, val);
}

static void dw100_hw_init_ctrl(struct dw100_device *dw_dev)
{
	u32 val;
	/*
	 * Input format YUV422_SP
	 * Output format YUV422_SP
	 * No hardware handshake (SW)
	 * No automatic double src buffering (Single)
	 * No automatic double dst buffering (Single)
	 * No Black Line
	 * Prefetch image pixel traversal
	 */

	val = DW100_DEWARP_CTRL_ENABLE
	    /* Valid only for auto prefetch mode*/
	    | DW100_DEWARP_CTRL_PREFETCH_THRESHOLD(32);

	/*
	 * Calculation mode required to support any scaling factor,
	 * but x4 slower than traversal mode.
	 *
	 * DW100_DEWARP_CTRL_PREFETCH_MODE_TRAVERSAL
	 * DW100_DEWARP_CTRL_PREFETCH_MODE_CALCULATION
	 * DW100_DEWARP_CTRL_PREFETCH_MODE_AUTO
	 *
	 * TODO: Find heuristics requiring calculation mode
	 */
	val |= DW100_DEWARP_CTRL_PREFETCH_MODE_CALCULATION;

	dw100_write(dw_dev, DW100_DEWARP_CTRL, val);
}

static void dw100_hw_set_pixel_boundary(struct dw100_device *dw_dev)
{
	u32 val;

	val = DW100_BOUNDARY_PIXEL_V(128)
		| DW100_BOUNDARY_PIXEL_U(128)
		| DW100_BOUNDARY_PIXEL_Y(0);

	dw100_write(dw_dev, DW100_BOUNDARY_PIXEL, val);
}

static void dw100_hw_set_scale(struct dw100_device *dw_dev, u8 scale)
{
	dev_dbg(&dw_dev->pdev->dev, "Setting scale factor to %u\n", scale);

	dw100_write(dw_dev, DW100_SCALE_FACTOR, scale);
}

static void dw100_hw_set_roi(struct dw100_device *dw_dev, u32 x, u32 y)
{
	u32 val;

	dev_dbg(&dw_dev->pdev->dev, "Setting ROI region to %u.%u\n", x, y);

	val = DW100_ROI_START_X(x) | DW100_ROI_START_Y(y);

	dw100_write(dw_dev, DW100_ROI_START, val);
}

static void dw100_hw_set_src_crop(struct dw100_device *dw_dev,
				  const struct dw100_q_data *src_q_data,
				  const struct dw100_q_data *dst_q_data)
{
	const struct v4l2_rect *rect = &src_q_data->crop;
	u32 src_scale, qscale, left_scale, top_scale;

	/* HW Scale is UQ1.7 encoded */
	src_scale = (rect->width << 7) / src_q_data->pix_fmt.width;
	dw100_hw_set_scale(dw_dev, src_scale);

	qscale = (dst_q_data->pix_fmt.width << 7)  / src_q_data->pix_fmt.width;

	left_scale = ((rect->left << 7) * qscale) >> 14;
	top_scale = ((rect->top << 7) * qscale) >> 14;

	dw100_hw_set_roi(dw_dev, left_scale, top_scale);
}

static void dw100_hw_set_source(struct dw100_device *dw_dev,
				const struct dw100_q_data *q_data,
				struct vb2_buffer *buffer)
{
	u32 width, height, stride, fourcc, val;
	const struct dw100_fmt *fmt = q_data->fmt;
	dma_addr_t addr_y = vb2_dma_contig_plane_dma_addr(buffer, 0);
	dma_addr_t addr_uv;

	width =  q_data->pix_fmt.width;
	height = q_data->pix_fmt.height;
	stride = q_data->pix_fmt.plane_fmt[0].bytesperline;
	fourcc = q_data->fmt->fourcc;

	if (q_data->pix_fmt.num_planes == 2)
		addr_uv = vb2_dma_contig_plane_dma_addr(buffer, 1);
	else
		addr_uv = addr_y + (stride * height);

	dev_dbg(&dw_dev->pdev->dev,
		"Set HW source registers for %ux%u - stride %u, pixfmt: %p4cc, dma:%pad\n",
		width, height, stride, &fourcc, &addr_y);

	/* Pixel Format */
	val = dw100_read(dw_dev, DW100_DEWARP_CTRL);

	val &= ~DW100_DEWARP_CTRL_INPUT_FORMAT_MASK;
	val |= DW100_DEWARP_CTRL_INPUT_FORMAT(fmt->reg_format);

	dw100_write(dw_dev, DW100_DEWARP_CTRL, val);

	/* Swap */
	val = dw100_read(dw_dev, DW100_SWAP_CONTROL);

	val &= ~DW100_SWAP_CONTROL_SRC_MASK;
	/*
	 * Data swapping is performed only on Y plane for source image.
	 */
	if (fmt->reg_swap_uv &&
	    fmt->reg_format == DW100_DEWARP_CTRL_FORMAT_YUV422_PACKED)
		val |= DW100_SWAP_CONTROL_SRC(DW100_SWAP_CONTROL_Y
					      (DW100_SWAP_CONTROL_BYTE));

	dw100_write(dw_dev, DW100_SWAP_CONTROL, val);

	/* Image resolution */
	dw100_write(dw_dev, DW100_SRC_IMG_SIZE,
		    DW100_IMG_SIZE_WIDTH(width) | DW100_IMG_SIZE_HEIGHT(height));

	dw100_write(dw_dev,  DW100_SRC_IMG_STRIDE, stride);

	/* Buffers */
	dw100_write(dw_dev, DW100_SRC_IMG_Y_BASE, DW100_IMG_Y_BASE(addr_y));
	dw100_write(dw_dev, DW100_SRC_IMG_UV_BASE, DW100_IMG_UV_BASE(addr_uv));
}

static void dw100_hw_set_destination(struct dw100_device *dw_dev,
				     const struct dw100_q_data *q_data,
				     const struct dw100_fmt *ifmt,
				     struct vb2_buffer *buffer)
{
	u32 width, height, stride, fourcc, val, size_y, size_uv;
	const struct dw100_fmt *fmt = q_data->fmt;
	dma_addr_t addr_y, addr_uv;

	width =  q_data->pix_fmt.width;
	height = q_data->pix_fmt.height;
	stride = q_data->pix_fmt.plane_fmt[0].bytesperline;
	fourcc = fmt->fourcc;

	addr_y = vb2_dma_contig_plane_dma_addr(buffer, 0);
	size_y = q_data->pix_fmt.plane_fmt[0].sizeimage;

	if (q_data->pix_fmt.num_planes == 2) {
		addr_uv = vb2_dma_contig_plane_dma_addr(buffer, 1);
		size_uv = q_data->pix_fmt.plane_fmt[1].sizeimage;
	} else {
		addr_uv = addr_y + ALIGN(stride * height, 16);
		size_uv = size_y;
		if (fmt->reg_format == DW100_DEWARP_CTRL_FORMAT_YUV420_SP)
			size_uv /= 2;
	}

	dev_dbg(&dw_dev->pdev->dev,
		"Set HW source registers for %ux%u - stride %u, pixfmt: %p4cc, dma:%pad\n",
		width, height, stride, &fourcc, &addr_y);

	/* Pixel Format */
	val = dw100_read(dw_dev, DW100_DEWARP_CTRL);

	val &= ~DW100_DEWARP_CTRL_OUTPUT_FORMAT_MASK;
	val |= DW100_DEWARP_CTRL_OUTPUT_FORMAT(fmt->reg_format);

	dw100_write(dw_dev, DW100_DEWARP_CTRL, val);

	/* Swap */
	val = dw100_read(dw_dev, DW100_SWAP_CONTROL);

	val &= ~DW100_SWAP_CONTROL_DST_MASK;

	/*
	 * Avoid to swap twice
	 */
	if (fmt->reg_swap_uv ^
	    (ifmt->reg_swap_uv && ifmt->reg_format !=
	     DW100_DEWARP_CTRL_FORMAT_YUV422_PACKED)) {
		if (fmt->reg_format == DW100_DEWARP_CTRL_FORMAT_YUV422_PACKED)
			val |= DW100_SWAP_CONTROL_DST(DW100_SWAP_CONTROL_Y
						      (DW100_SWAP_CONTROL_BYTE));
		else
			val |= DW100_SWAP_CONTROL_DST(DW100_SWAP_CONTROL_UV
						      (DW100_SWAP_CONTROL_BYTE));
	}

	dw100_write(dw_dev, DW100_SWAP_CONTROL, val);

	/* Image resolution */
	dw100_write(dw_dev, DW100_DST_IMG_SIZE,
		    DW100_IMG_SIZE_WIDTH(width) | DW100_IMG_SIZE_HEIGHT(height));
	dw100_write(dw_dev, DW100_DST_IMG_STRIDE, stride);
	dw100_write(dw_dev, DW100_DST_IMG_Y_BASE, DW100_IMG_Y_BASE(addr_y));
	dw100_write(dw_dev, DW100_DST_IMG_UV_BASE, DW100_IMG_UV_BASE(addr_uv));
	dw100_write(dw_dev, DW100_DST_IMG_Y_SIZE1, DW100_DST_IMG_Y_SIZE(size_y));
	dw100_write(dw_dev, DW100_DST_IMG_UV_SIZE1,
		    DW100_DST_IMG_UV_SIZE(size_uv));
}

static void dw100_hw_set_mapping(struct dw100_device *dw_dev, dma_addr_t addr,
				 u32 width, u32 height)
{
	dev_dbg(&dw_dev->pdev->dev,
		"Set HW mapping registers for %ux%u addr:%pad",
		width, height, &addr);

	dw100_write(dw_dev, DW100_MAP_LUT_ADDR, DW100_MAP_LUT_ADDR_ADDR(addr));
	dw100_write(dw_dev, DW100_MAP_LUT_SIZE, DW100_MAP_LUT_SIZE_WIDTH(width)
		    | DW100_MAP_LUT_SIZE_HEIGHT(height));
}

static void dw100_hw_clear_irq(struct dw100_device *dw_dev, unsigned int irq)
{
	dw100_write(dw_dev, DW100_INTERRUPT_STATUS,
		    DW100_INTERRUPT_STATUS_INT_CLEAR(irq));
}

static void dw100_hw_enable_irq(struct dw100_device *dw_dev)
{
	dw100_write(dw_dev, DW100_INTERRUPT_STATUS,
		    DW100_INTERRUPT_STATUS_INT_ENABLE_MASK);
}

static void dw100_hw_disable_irq(struct dw100_device *dw_dev)
{
	dw100_write(dw_dev, DW100_INTERRUPT_STATUS, 0);
}

static u32 dw_hw_get_pending_irqs(struct dw100_device *dw_dev)
{
	u32 val;

	val = dw100_read(dw_dev, DW100_INTERRUPT_STATUS);

	return DW100_INTERRUPT_STATUS_INT_STATUS(val);
}

static irqreturn_t dw100_irq_handler(int irq, void *dev_id)
{
	struct dw100_device *dw_dev = dev_id;
	u32 pending_irqs, err_irqs, frame_done_irq;
	bool with_error = true;

	pending_irqs = dw_hw_get_pending_irqs(dw_dev);
	frame_done_irq = pending_irqs & DW100_INTERRUPT_STATUS_INT_FRAME_DONE;
	err_irqs = DW100_INTERRUPT_STATUS_INT_ERR_STATUS(pending_irqs);

	if (frame_done_irq) {
		dev_dbg(&dw_dev->pdev->dev, "Frame done interrupt\n");
		with_error = false;
		err_irqs &= ~DW100_INTERRUPT_STATUS_INT_ERR_STATUS
			(DW100_INTERRUPT_STATUS_INT_ERR_FRAME_DONE);
	}

	if (err_irqs)
		dev_err(&dw_dev->pdev->dev, "Interrupt error: %#x\n", err_irqs);

	dw100_hw_disable_irq(dw_dev);
	dw100_hw_master_bus_disable(dw_dev);
	dw100_hw_clear_irq(dw_dev, pending_irqs |
			   DW100_INTERRUPT_STATUS_INT_ERR_TIME_OUT);

	dw100_job_finish(dw_dev, with_error);

	return IRQ_HANDLED;
}

static void dw100_start(struct dw100_ctx *ctx, struct vb2_v4l2_buffer *in_vb,
			struct vb2_v4l2_buffer *out_vb)
{
	struct dw100_device *dw_dev = ctx->dw_dev;

	out_vb->sequence =
		dw100_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)->sequence++;
	in_vb->sequence =
		dw100_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)->sequence++;

	dev_dbg(&ctx->dw_dev->pdev->dev,
		"Starting queues %p->%p, sequence %u->%u\n",
		v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE),
		v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE),
		in_vb->sequence, out_vb->sequence);

	v4l2_m2m_buf_copy_metadata(in_vb, out_vb, true);

	/* Now, let's deal with hardware ... */
	dw100_hw_master_bus_disable(dw_dev);
	dw100_hw_init_ctrl(dw_dev);
	dw100_hw_set_pixel_boundary(dw_dev);
	dw100_hw_set_src_crop(dw_dev, &ctx->q_data[DW100_QUEUE_SRC],
			      &ctx->q_data[DW100_QUEUE_DST]);
	dw100_hw_set_source(dw_dev, &ctx->q_data[DW100_QUEUE_SRC],
			    &in_vb->vb2_buf);
	dw100_hw_set_destination(dw_dev, &ctx->q_data[DW100_QUEUE_DST],
				 ctx->q_data[DW100_QUEUE_SRC].fmt,
				 &out_vb->vb2_buf);
	dw100_hw_set_mapping(dw_dev, ctx->map_dma,
			     ctx->map_width, ctx->map_height);
	dw100_hw_enable_irq(dw_dev);
	dw100_hw_dewarp_start(dw_dev);

	/* Enable Bus */
	dw100_hw_master_bus_enable(dw_dev);
}

static void dw100_device_run(void *priv)
{
	struct dw100_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	dw100_start(ctx, src_buf, dst_buf);
}

static const struct v4l2_m2m_ops dw100_m2m_ops = {
	.device_run	= dw100_device_run,
};

static struct video_device *dw100_init_video_device(struct dw100_device *dw_dev)
{
	struct video_device *vfd = &dw_dev->vfd;

	vfd->vfl_dir = VFL_DIR_M2M;
	vfd->fops = &dw100_fops;
	vfd->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	vfd->ioctl_ops = &dw100_ioctl_ops;
	vfd->minor = -1;
	vfd->release = video_device_release_empty;
	vfd->v4l2_dev = &dw_dev->v4l2_dev;
	vfd->lock = &dw_dev->vfd_mutex;

	strscpy(vfd->name, DRV_NAME, sizeof(vfd->name));
	mutex_init(vfd->lock);
	video_set_drvdata(vfd, dw_dev);

	return vfd;
}

static int dw100_dump_regs_show(struct seq_file *m, void *private)
{
	struct dw100_device *dw_dev = m->private;
	int ret;

	ret = pm_runtime_resume_and_get(&dw_dev->pdev->dev);
	if (ret < 0)
		return ret;

	ret = dw100_dump_regs(m);

	pm_runtime_put_sync(&dw_dev->pdev->dev);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(dw100_dump_regs);

static void dw100_debugfs_init(struct dw100_device *dw_dev)
{
	dw_dev->debugfs_root =
		debugfs_create_dir(dev_name(&dw_dev->pdev->dev), NULL);

	debugfs_create_file("dump_regs", 0600, dw_dev->debugfs_root, dw_dev,
			    &dw100_dump_regs_fops);
}

static void dw100_debugfs_exit(struct dw100_device *dw_dev)
{
	debugfs_remove_recursive(dw_dev->debugfs_root);
}

static int dw100_probe(struct platform_device *pdev)
{
	struct dw100_device *dw_dev;
	struct video_device *vfd;
	struct resource *res;
	int ret, irq;

	dw_dev = devm_kzalloc(&pdev->dev, sizeof(*dw_dev), GFP_KERNEL);
	if (!dw_dev)
		return -ENOMEM;
	dw_dev->pdev = pdev;

	ret = devm_clk_bulk_get_all(&pdev->dev, &dw_dev->clks);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to get clocks: %d\n", ret);
		return ret;
	}
	dw_dev->num_clks = ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dw_dev->mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dw_dev->mmio))
		return PTR_ERR(dw_dev->mmio);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	platform_set_drvdata(pdev, dw_dev);

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to resume the device: %d\n", ret);
		goto err_pm;
	}

	pm_runtime_put_sync(&pdev->dev);

	ret = devm_request_irq(&pdev->dev, irq, dw100_irq_handler, IRQF_ONESHOT,
			       dev_name(&pdev->dev), dw_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", ret);
		goto err_pm;
	}

	ret = v4l2_device_register(&pdev->dev, &dw_dev->v4l2_dev);
	if (ret)
		goto err_pm;

	vfd = dw100_init_video_device(dw_dev);

	dw_dev->m2m_dev = v4l2_m2m_init(&dw100_m2m_ops);
	if (IS_ERR(dw_dev->m2m_dev)) {
		dev_err(&pdev->dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dw_dev->m2m_dev);
		goto err_v4l2;
	}

	dw_dev->mdev.dev = &pdev->dev;
	strscpy(dw_dev->mdev.model, "dw100", sizeof(dw_dev->mdev.model));
	media_device_init(&dw_dev->mdev);
	dw_dev->v4l2_dev.mdev = &dw_dev->mdev;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register video device\n");
		goto err_m2m;
	}

	ret = v4l2_m2m_register_media_controller(dw_dev->m2m_dev, vfd,
						 MEDIA_ENT_F_PROC_VIDEO_SCALER);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init mem2mem media controller\n");
		goto error_v4l2;
	}

	ret = media_device_register(&dw_dev->mdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register mem2mem media device\n");
		goto error_m2m_mc;
	}

	dw100_debugfs_init(dw_dev);

	dev_info(&pdev->dev,
		 "dw100 v4l2 m2m registered as /dev/video%u\n", vfd->num);

	return 0;

error_m2m_mc:
	v4l2_m2m_unregister_media_controller(dw_dev->m2m_dev);
error_v4l2:
	video_unregister_device(vfd);
err_m2m:
	media_device_cleanup(&dw_dev->mdev);
	v4l2_m2m_release(dw_dev->m2m_dev);
err_v4l2:
	v4l2_device_unregister(&dw_dev->v4l2_dev);
err_pm:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int dw100_remove(struct platform_device *pdev)
{
	struct dw100_device *dw_dev = platform_get_drvdata(pdev);

	dw100_debugfs_exit(dw_dev);

	pm_runtime_disable(&pdev->dev);

	media_device_unregister(&dw_dev->mdev);
	v4l2_m2m_unregister_media_controller(dw_dev->m2m_dev);
	media_device_cleanup(&dw_dev->mdev);

	video_unregister_device(&dw_dev->vfd);
	mutex_destroy(dw_dev->vfd.lock);
	v4l2_m2m_release(dw_dev->m2m_dev);
	v4l2_device_unregister(&dw_dev->v4l2_dev);

	return 0;
}

static int __maybe_unused dw100_runtime_suspend(struct device *dev)
{
	struct dw100_device *dw_dev = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(dw_dev->num_clks, dw_dev->clks);

	return 0;
}

static int __maybe_unused dw100_runtime_resume(struct device *dev)
{
	int ret;
	struct dw100_device *dw_dev = dev_get_drvdata(dev);

	ret = clk_bulk_prepare_enable(dw_dev->num_clks, dw_dev->clks);

	if (ret)
		return ret;

	dw100_hw_reset(dw_dev);

	return 0;
}

static const struct dev_pm_ops dw100_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw100_runtime_suspend,
			   dw100_runtime_resume, NULL)
};

static const struct of_device_id dw100_dt_ids[] = {
	{ .compatible = "nxp,imx8mp-dw100", .data = NULL },
	{ },
};
MODULE_DEVICE_TABLE(of, dw100_dt_ids);

static struct platform_driver dw100_driver = {
	.probe		= dw100_probe,
	.remove		= dw100_remove,
	.driver		= {
		.name	= DRV_NAME,
		.pm = &dw100_pm,
		.of_match_table = dw100_dt_ids,
	},
};

module_platform_driver(dw100_driver);

MODULE_DESCRIPTION("DW100 Hardware dewarper");
MODULE_AUTHOR("Xavier Roumegue <Xavier.Roumegue@oss.nxp.com>");
MODULE_LICENSE("GPL");
