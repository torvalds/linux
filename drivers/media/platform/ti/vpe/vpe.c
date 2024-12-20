// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI VPE mem2mem driver, based on the virtual v4l2-mem2mem example driver
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * Based on the virtual v4l2-mem2mem example device
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/log2.h>
#include <linux/sizes.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "vpdma.h"
#include "vpdma_priv.h"
#include "vpe_regs.h"
#include "sc.h"
#include "csc.h"

#define VPE_MODULE_NAME "vpe"

/* minimum and maximum frame sizes */
#define MIN_W		32
#define MIN_H		32
#define MAX_W		2048
#define MAX_H		2048

/* required alignments */
#define S_ALIGN		0	/* multiple of 1 */
#define H_ALIGN		1	/* multiple of 2 */

/* flags that indicate a format can be used for capture/output */
#define VPE_FMT_TYPE_CAPTURE	(1 << 0)
#define VPE_FMT_TYPE_OUTPUT	(1 << 1)

/* used as plane indices */
#define VPE_MAX_PLANES	2
#define VPE_LUMA	0
#define VPE_CHROMA	1

/* per m2m context info */
#define VPE_MAX_SRC_BUFS	3	/* need 3 src fields to de-interlace */

#define VPE_DEF_BUFS_PER_JOB	1	/* default one buffer per batch job */

/*
 * each VPE context can need up to 3 config descriptors, 7 input descriptors,
 * 3 output descriptors, and 10 control descriptors
 */
#define VPE_DESC_LIST_SIZE	(10 * VPDMA_DTD_DESC_SIZE +	\
					13 * VPDMA_CFD_CTD_DESC_SIZE)

#define vpe_dbg(vpedev, fmt, arg...)	\
		dev_dbg((vpedev)->v4l2_dev.dev, fmt, ##arg)
#define vpe_err(vpedev, fmt, arg...)	\
		dev_err((vpedev)->v4l2_dev.dev, fmt, ##arg)

struct vpe_us_coeffs {
	unsigned short	anchor_fid0_c0;
	unsigned short	anchor_fid0_c1;
	unsigned short	anchor_fid0_c2;
	unsigned short	anchor_fid0_c3;
	unsigned short	interp_fid0_c0;
	unsigned short	interp_fid0_c1;
	unsigned short	interp_fid0_c2;
	unsigned short	interp_fid0_c3;
	unsigned short	anchor_fid1_c0;
	unsigned short	anchor_fid1_c1;
	unsigned short	anchor_fid1_c2;
	unsigned short	anchor_fid1_c3;
	unsigned short	interp_fid1_c0;
	unsigned short	interp_fid1_c1;
	unsigned short	interp_fid1_c2;
	unsigned short	interp_fid1_c3;
};

/*
 * Default upsampler coefficients
 */
static const struct vpe_us_coeffs us_coeffs[] = {
	{
		/* Coefficients for progressive input */
		0x00C8, 0x0348, 0x0018, 0x3FD8, 0x3FB8, 0x0378, 0x00E8, 0x3FE8,
		0x00C8, 0x0348, 0x0018, 0x3FD8, 0x3FB8, 0x0378, 0x00E8, 0x3FE8,
	},
	{
		/* Coefficients for Top Field Interlaced input */
		0x0051, 0x03D5, 0x3FE3, 0x3FF7, 0x3FB5, 0x02E9, 0x018F, 0x3FD3,
		/* Coefficients for Bottom Field Interlaced input */
		0x016B, 0x0247, 0x00B1, 0x3F9D, 0x3FCF, 0x03DB, 0x005D, 0x3FF9,
	},
};

/*
 * the following registers are for configuring some of the parameters of the
 * motion and edge detection blocks inside DEI, these generally remain the same,
 * these could be passed later via userspace if some one needs to tweak these.
 */
struct vpe_dei_regs {
	unsigned long mdt_spacial_freq_thr_reg;		/* VPE_DEI_REG2 */
	unsigned long edi_config_reg;			/* VPE_DEI_REG3 */
	unsigned long edi_lut_reg0;			/* VPE_DEI_REG4 */
	unsigned long edi_lut_reg1;			/* VPE_DEI_REG5 */
	unsigned long edi_lut_reg2;			/* VPE_DEI_REG6 */
	unsigned long edi_lut_reg3;			/* VPE_DEI_REG7 */
};

/*
 * default expert DEI register values, unlikely to be modified.
 */
static const struct vpe_dei_regs dei_regs = {
	.mdt_spacial_freq_thr_reg = 0x020C0804u,
	.edi_config_reg = 0x0118100Cu,
	.edi_lut_reg0 = 0x08040200u,
	.edi_lut_reg1 = 0x1010100Cu,
	.edi_lut_reg2 = 0x10101010u,
	.edi_lut_reg3 = 0x10101010u,
};

/*
 * The port_data structure contains per-port data.
 */
struct vpe_port_data {
	enum vpdma_channel channel;	/* VPDMA channel */
	u8	vb_index;		/* input frame f, f-1, f-2 index */
	u8	vb_part;		/* plane index for co-panar formats */
};

/*
 * Define indices into the port_data tables
 */
#define VPE_PORT_LUMA1_IN	0
#define VPE_PORT_CHROMA1_IN	1
#define VPE_PORT_LUMA2_IN	2
#define VPE_PORT_CHROMA2_IN	3
#define VPE_PORT_LUMA3_IN	4
#define VPE_PORT_CHROMA3_IN	5
#define VPE_PORT_MV_IN		6
#define VPE_PORT_MV_OUT		7
#define VPE_PORT_LUMA_OUT	8
#define VPE_PORT_CHROMA_OUT	9
#define VPE_PORT_RGB_OUT	10

static const struct vpe_port_data port_data[11] = {
	[VPE_PORT_LUMA1_IN] = {
		.channel	= VPE_CHAN_LUMA1_IN,
		.vb_index	= 0,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA1_IN] = {
		.channel	= VPE_CHAN_CHROMA1_IN,
		.vb_index	= 0,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_LUMA2_IN] = {
		.channel	= VPE_CHAN_LUMA2_IN,
		.vb_index	= 1,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA2_IN] = {
		.channel	= VPE_CHAN_CHROMA2_IN,
		.vb_index	= 1,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_LUMA3_IN] = {
		.channel	= VPE_CHAN_LUMA3_IN,
		.vb_index	= 2,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA3_IN] = {
		.channel	= VPE_CHAN_CHROMA3_IN,
		.vb_index	= 2,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_MV_IN] = {
		.channel	= VPE_CHAN_MV_IN,
	},
	[VPE_PORT_MV_OUT] = {
		.channel	= VPE_CHAN_MV_OUT,
	},
	[VPE_PORT_LUMA_OUT] = {
		.channel	= VPE_CHAN_LUMA_OUT,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA_OUT] = {
		.channel	= VPE_CHAN_CHROMA_OUT,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_RGB_OUT] = {
		.channel	= VPE_CHAN_RGB_OUT,
		.vb_part	= VPE_LUMA,
	},
};


/* driver info for each of the supported video formats */
struct vpe_fmt {
	u32	fourcc;			/* standard format identifier */
	u8	types;			/* CAPTURE and/or OUTPUT */
	u8	coplanar;		/* set for unpacked Luma and Chroma */
	/* vpdma format info for each plane */
	struct vpdma_data_format const *vpdma_fmt[VPE_MAX_PLANES];
};

static struct vpe_fmt vpe_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_NV16,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 1,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_Y444],
				    &vpdma_yuv_fmts[VPDMA_DATA_FMT_C444],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 1,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_Y420],
				    &vpdma_yuv_fmts[VPDMA_DATA_FMT_C420],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV21,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 1,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_Y420],
				    &vpdma_yuv_fmts[VPDMA_DATA_FMT_CB420],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_YCB422],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_CBY422],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.types		= VPE_FMT_TYPE_CAPTURE,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_rgb_fmts[VPDMA_DATA_FMT_RGB24],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.types		= VPE_FMT_TYPE_CAPTURE,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_rgb_fmts[VPDMA_DATA_FMT_ARGB32],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.types		= VPE_FMT_TYPE_CAPTURE,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_rgb_fmts[VPDMA_DATA_FMT_BGR24],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_BGR32,
		.types		= VPE_FMT_TYPE_CAPTURE,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_rgb_fmts[VPDMA_DATA_FMT_ABGR32],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.types		= VPE_FMT_TYPE_CAPTURE,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_rgb_fmts[VPDMA_DATA_FMT_RGB565],
				  },
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB555,
		.types		= VPE_FMT_TYPE_CAPTURE,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_rgb_fmts[VPDMA_DATA_FMT_RGBA16_5551],
				  },
	},
};

/*
 * per-queue, driver-specific private data.
 * there is one source queue and one destination queue for each m2m context.
 */
struct vpe_q_data {
	/* current v4l2 format info */
	struct v4l2_format	format;
	unsigned int		flags;
	struct v4l2_rect	c_rect;				/* crop/compose rectangle */
	struct vpe_fmt		*fmt;				/* format info */
};

/* vpe_q_data flag bits */
#define	Q_DATA_FRAME_1D			BIT(0)
#define	Q_DATA_MODE_TILED		BIT(1)
#define	Q_DATA_INTERLACED_ALTERNATE	BIT(2)
#define	Q_DATA_INTERLACED_SEQ_TB	BIT(3)
#define	Q_DATA_INTERLACED_SEQ_BT	BIT(4)

#define Q_IS_SEQ_XX		(Q_DATA_INTERLACED_SEQ_TB | \
				Q_DATA_INTERLACED_SEQ_BT)

#define Q_IS_INTERLACED		(Q_DATA_INTERLACED_ALTERNATE | \
				Q_DATA_INTERLACED_SEQ_TB | \
				Q_DATA_INTERLACED_SEQ_BT)

enum {
	Q_DATA_SRC = 0,
	Q_DATA_DST = 1,
};

/* find our format description corresponding to the passed v4l2_format */
static struct vpe_fmt *__find_format(u32 fourcc)
{
	struct vpe_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(vpe_formats); k++) {
		fmt = &vpe_formats[k];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

static struct vpe_fmt *find_format(struct v4l2_format *f)
{
	return __find_format(f->fmt.pix.pixelformat);
}

/*
 * there is one vpe_dev structure in the driver, it is shared by
 * all instances.
 */
struct vpe_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
	struct v4l2_m2m_dev	*m2m_dev;

	atomic_t		num_instances;	/* count of driver instances */
	dma_addr_t		loaded_mmrs;	/* shadow mmrs in device */
	struct mutex		dev_mutex;
	spinlock_t		lock;

	int			irq;
	void __iomem		*base;
	struct resource		*res;

	struct vpdma_data	vpdma_data;
	struct vpdma_data	*vpdma;		/* vpdma data handle */
	struct sc_data		*sc;		/* scaler data handle */
	struct csc_data		*csc;		/* csc data handle */
};

/*
 * There is one vpe_ctx structure for each m2m context.
 */
struct vpe_ctx {
	struct v4l2_fh		fh;
	struct vpe_dev		*dev;
	struct v4l2_ctrl_handler hdl;

	unsigned int		field;			/* current field */
	unsigned int		sequence;		/* current frame/field seq */
	unsigned int		aborting;		/* abort after next irq */

	unsigned int		bufs_per_job;		/* input buffers per batch */
	unsigned int		bufs_completed;		/* bufs done in this batch */

	struct vpe_q_data	q_data[2];		/* src & dst queue data */
	struct vb2_v4l2_buffer	*src_vbs[VPE_MAX_SRC_BUFS];
	struct vb2_v4l2_buffer	*dst_vb;

	dma_addr_t		mv_buf_dma[2];		/* dma addrs of motion vector in/out bufs */
	void			*mv_buf[2];		/* virtual addrs of motion vector bufs */
	size_t			mv_buf_size;		/* current motion vector buffer size */
	struct vpdma_buf	mmr_adb;		/* shadow reg addr/data block */
	struct vpdma_buf	sc_coeff_h;		/* h coeff buffer */
	struct vpdma_buf	sc_coeff_v;		/* v coeff buffer */
	struct vpdma_desc_list	desc_list;		/* DMA descriptor list */

	bool			deinterlacing;		/* using de-interlacer */
	bool			load_mmrs;		/* have new shadow reg values */

	unsigned int		src_mv_buf_selector;
};


/*
 * M2M devices get 2 queues.
 * Return the queue given the type.
 */
static struct vpe_q_data *get_q_data(struct vpe_ctx *ctx,
				     enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->q_data[Q_DATA_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->q_data[Q_DATA_DST];
	default:
		return NULL;
	}
	return NULL;
}

static u32 read_reg(struct vpe_dev *dev, int offset)
{
	return ioread32(dev->base + offset);
}

static void write_reg(struct vpe_dev *dev, int offset, u32 value)
{
	iowrite32(value, dev->base + offset);
}

/* register field read/write helpers */
static int get_field(u32 value, u32 mask, int shift)
{
	return (value & (mask << shift)) >> shift;
}

static int read_field_reg(struct vpe_dev *dev, int offset, u32 mask, int shift)
{
	return get_field(read_reg(dev, offset), mask, shift);
}

static void write_field(u32 *valp, u32 field, u32 mask, int shift)
{
	u32 val = *valp;

	val &= ~(mask << shift);
	val |= (field & mask) << shift;
	*valp = val;
}

static void write_field_reg(struct vpe_dev *dev, int offset, u32 field,
		u32 mask, int shift)
{
	u32 val = read_reg(dev, offset);

	write_field(&val, field, mask, shift);

	write_reg(dev, offset, val);
}

/*
 * DMA address/data block for the shadow registers
 */
struct vpe_mmr_adb {
	struct vpdma_adb_hdr	out_fmt_hdr;
	u32			out_fmt_reg[1];
	u32			out_fmt_pad[3];
	struct vpdma_adb_hdr	us1_hdr;
	u32			us1_regs[8];
	struct vpdma_adb_hdr	us2_hdr;
	u32			us2_regs[8];
	struct vpdma_adb_hdr	us3_hdr;
	u32			us3_regs[8];
	struct vpdma_adb_hdr	dei_hdr;
	u32			dei_regs[8];
	struct vpdma_adb_hdr	sc_hdr0;
	u32			sc_regs0[7];
	u32			sc_pad0[1];
	struct vpdma_adb_hdr	sc_hdr8;
	u32			sc_regs8[6];
	u32			sc_pad8[2];
	struct vpdma_adb_hdr	sc_hdr17;
	u32			sc_regs17[9];
	u32			sc_pad17[3];
	struct vpdma_adb_hdr	csc_hdr;
	u32			csc_regs[6];
	u32			csc_pad[2];
};

#define GET_OFFSET_TOP(ctx, obj, reg)	\
	((obj)->res->start - ctx->dev->res->start + reg)

#define VPE_SET_MMR_ADB_HDR(ctx, hdr, regs, offset_a)	\
	VPDMA_SET_MMR_ADB_HDR(ctx->mmr_adb, vpe_mmr_adb, hdr, regs, offset_a)
/*
 * Set the headers for all of the address/data block structures.
 */
static void init_adb_hdrs(struct vpe_ctx *ctx)
{
	VPE_SET_MMR_ADB_HDR(ctx, out_fmt_hdr, out_fmt_reg, VPE_CLK_FORMAT_SELECT);
	VPE_SET_MMR_ADB_HDR(ctx, us1_hdr, us1_regs, VPE_US1_R0);
	VPE_SET_MMR_ADB_HDR(ctx, us2_hdr, us2_regs, VPE_US2_R0);
	VPE_SET_MMR_ADB_HDR(ctx, us3_hdr, us3_regs, VPE_US3_R0);
	VPE_SET_MMR_ADB_HDR(ctx, dei_hdr, dei_regs, VPE_DEI_FRAME_SIZE);
	VPE_SET_MMR_ADB_HDR(ctx, sc_hdr0, sc_regs0,
		GET_OFFSET_TOP(ctx, ctx->dev->sc, CFG_SC0));
	VPE_SET_MMR_ADB_HDR(ctx, sc_hdr8, sc_regs8,
		GET_OFFSET_TOP(ctx, ctx->dev->sc, CFG_SC8));
	VPE_SET_MMR_ADB_HDR(ctx, sc_hdr17, sc_regs17,
		GET_OFFSET_TOP(ctx, ctx->dev->sc, CFG_SC17));
	VPE_SET_MMR_ADB_HDR(ctx, csc_hdr, csc_regs,
		GET_OFFSET_TOP(ctx, ctx->dev->csc, CSC_CSC00));
};

/*
 * Allocate or re-allocate the motion vector DMA buffers
 * There are two buffers, one for input and one for output.
 * However, the roles are reversed after each field is processed.
 * In other words, after each field is processed, the previous
 * output (dst) MV buffer becomes the new input (src) MV buffer.
 */
static int realloc_mv_buffers(struct vpe_ctx *ctx, size_t size)
{
	struct device *dev = ctx->dev->v4l2_dev.dev;

	if (ctx->mv_buf_size == size)
		return 0;

	if (ctx->mv_buf[0])
		dma_free_coherent(dev, ctx->mv_buf_size, ctx->mv_buf[0],
			ctx->mv_buf_dma[0]);

	if (ctx->mv_buf[1])
		dma_free_coherent(dev, ctx->mv_buf_size, ctx->mv_buf[1],
			ctx->mv_buf_dma[1]);

	if (size == 0)
		return 0;

	ctx->mv_buf[0] = dma_alloc_coherent(dev, size, &ctx->mv_buf_dma[0],
				GFP_KERNEL);
	if (!ctx->mv_buf[0]) {
		vpe_err(ctx->dev, "failed to allocate motion vector buffer\n");
		return -ENOMEM;
	}

	ctx->mv_buf[1] = dma_alloc_coherent(dev, size, &ctx->mv_buf_dma[1],
				GFP_KERNEL);
	if (!ctx->mv_buf[1]) {
		vpe_err(ctx->dev, "failed to allocate motion vector buffer\n");
		dma_free_coherent(dev, size, ctx->mv_buf[0],
			ctx->mv_buf_dma[0]);

		return -ENOMEM;
	}

	ctx->mv_buf_size = size;
	ctx->src_mv_buf_selector = 0;

	return 0;
}

static void free_mv_buffers(struct vpe_ctx *ctx)
{
	realloc_mv_buffers(ctx, 0);
}

/*
 * While de-interlacing, we keep the two most recent input buffers
 * around.  This function frees those two buffers when we have
 * finished processing the current stream.
 */
static void free_vbs(struct vpe_ctx *ctx)
{
	struct vpe_dev *dev = ctx->dev;
	unsigned long flags;

	if (ctx->src_vbs[2] == NULL)
		return;

	spin_lock_irqsave(&dev->lock, flags);
	if (ctx->src_vbs[2]) {
		v4l2_m2m_buf_done(ctx->src_vbs[2], VB2_BUF_STATE_DONE);
		if (ctx->src_vbs[1] && (ctx->src_vbs[1] != ctx->src_vbs[2]))
			v4l2_m2m_buf_done(ctx->src_vbs[1], VB2_BUF_STATE_DONE);
		ctx->src_vbs[2] = NULL;
		ctx->src_vbs[1] = NULL;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
}

/*
 * Enable or disable the VPE clocks
 */
static void vpe_set_clock_enable(struct vpe_dev *dev, bool on)
{
	u32 val = 0;

	if (on)
		val = VPE_DATA_PATH_CLK_ENABLE | VPE_VPEDMA_CLK_ENABLE;
	write_reg(dev, VPE_CLK_ENABLE, val);
}

static void vpe_top_reset(struct vpe_dev *dev)
{

	write_field_reg(dev, VPE_CLK_RESET, 1, VPE_DATA_PATH_CLK_RESET_MASK,
		VPE_DATA_PATH_CLK_RESET_SHIFT);

	usleep_range(100, 150);

	write_field_reg(dev, VPE_CLK_RESET, 0, VPE_DATA_PATH_CLK_RESET_MASK,
		VPE_DATA_PATH_CLK_RESET_SHIFT);
}

static void vpe_top_vpdma_reset(struct vpe_dev *dev)
{
	write_field_reg(dev, VPE_CLK_RESET, 1, VPE_VPDMA_CLK_RESET_MASK,
		VPE_VPDMA_CLK_RESET_SHIFT);

	usleep_range(100, 150);

	write_field_reg(dev, VPE_CLK_RESET, 0, VPE_VPDMA_CLK_RESET_MASK,
		VPE_VPDMA_CLK_RESET_SHIFT);
}

/*
 * Load the correct of upsampler coefficients into the shadow MMRs
 */
static void set_us_coefficients(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	struct vpe_q_data *s_q_data = &ctx->q_data[Q_DATA_SRC];
	u32 *us1_reg = &mmr_adb->us1_regs[0];
	u32 *us2_reg = &mmr_adb->us2_regs[0];
	u32 *us3_reg = &mmr_adb->us3_regs[0];
	const unsigned short *cp, *end_cp;

	cp = &us_coeffs[0].anchor_fid0_c0;

	if (s_q_data->flags & Q_IS_INTERLACED)		/* interlaced */
		cp += sizeof(us_coeffs[0]) / sizeof(*cp);

	end_cp = cp + sizeof(us_coeffs[0]) / sizeof(*cp);

	while (cp < end_cp) {
		write_field(us1_reg, *cp++, VPE_US_C0_MASK, VPE_US_C0_SHIFT);
		write_field(us1_reg, *cp++, VPE_US_C1_MASK, VPE_US_C1_SHIFT);
		*us2_reg++ = *us1_reg;
		*us3_reg++ = *us1_reg++;
	}
	ctx->load_mmrs = true;
}

/*
 * Set the upsampler config mode and the VPDMA line mode in the shadow MMRs.
 */
static void set_cfg_modes(struct vpe_ctx *ctx)
{
	struct vpe_fmt *fmt = ctx->q_data[Q_DATA_SRC].fmt;
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *us1_reg0 = &mmr_adb->us1_regs[0];
	u32 *us2_reg0 = &mmr_adb->us2_regs[0];
	u32 *us3_reg0 = &mmr_adb->us3_regs[0];
	int cfg_mode = 1;

	/*
	 * Cfg Mode 0: YUV420 source, enable upsampler, DEI is de-interlacing.
	 * Cfg Mode 1: YUV422 source, disable upsampler, DEI is de-interlacing.
	 */

	if (fmt->fourcc == V4L2_PIX_FMT_NV12 ||
	    fmt->fourcc == V4L2_PIX_FMT_NV21)
		cfg_mode = 0;

	write_field(us1_reg0, cfg_mode, VPE_US_MODE_MASK, VPE_US_MODE_SHIFT);
	write_field(us2_reg0, cfg_mode, VPE_US_MODE_MASK, VPE_US_MODE_SHIFT);
	write_field(us3_reg0, cfg_mode, VPE_US_MODE_MASK, VPE_US_MODE_SHIFT);

	ctx->load_mmrs = true;
}

static void set_line_modes(struct vpe_ctx *ctx)
{
	struct vpe_fmt *fmt = ctx->q_data[Q_DATA_SRC].fmt;
	int line_mode = 1;

	if (fmt->fourcc == V4L2_PIX_FMT_NV12 ||
	    fmt->fourcc == V4L2_PIX_FMT_NV21)
		line_mode = 0;		/* double lines to line buffer */

	/* regs for now */
	vpdma_set_line_mode(ctx->dev->vpdma, line_mode, VPE_CHAN_CHROMA1_IN);
	vpdma_set_line_mode(ctx->dev->vpdma, line_mode, VPE_CHAN_CHROMA2_IN);
	vpdma_set_line_mode(ctx->dev->vpdma, line_mode, VPE_CHAN_CHROMA3_IN);

	/* frame start for input luma */
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_LUMA1_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_LUMA2_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_LUMA3_IN);

	/* frame start for input chroma */
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_CHROMA1_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_CHROMA2_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_CHROMA3_IN);

	/* frame start for MV in client */
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_MV_IN);
}

/*
 * Set the shadow registers that are modified when the source
 * format changes.
 */
static void set_src_registers(struct vpe_ctx *ctx)
{
	set_us_coefficients(ctx);
}

/*
 * Set the shadow registers that are modified when the destination
 * format changes.
 */
static void set_dst_registers(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	struct vpe_fmt *fmt = ctx->q_data[Q_DATA_DST].fmt;
	const struct v4l2_format_info *finfo;
	u32 val = 0;

	finfo = v4l2_format_info(fmt->fourcc);
	if (v4l2_is_format_rgb(finfo)) {
		val |= VPE_RGB_OUT_SELECT;
		vpdma_set_bg_color(ctx->dev->vpdma,
			(struct vpdma_data_format *)fmt->vpdma_fmt[0], 0xff);
	} else if (fmt->fourcc == V4L2_PIX_FMT_NV16)
		val |= VPE_COLOR_SEPARATE_422;

	/*
	 * the source of CHR_DS and CSC is always the scaler, irrespective of
	 * whether it's used or not
	 */
	val |= VPE_DS_SRC_DEI_SCALER | VPE_CSC_SRC_DEI_SCALER;

	if (fmt->fourcc != V4L2_PIX_FMT_NV12 &&
	    fmt->fourcc != V4L2_PIX_FMT_NV21)
		val |= VPE_DS_BYPASS;

	mmr_adb->out_fmt_reg[0] = val;

	ctx->load_mmrs = true;
}

/*
 * Set the de-interlacer shadow register values
 */
static void set_dei_regs(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	struct vpe_q_data *s_q_data = &ctx->q_data[Q_DATA_SRC];
	unsigned int src_h = s_q_data->c_rect.height;
	unsigned int src_w = s_q_data->c_rect.width;
	u32 *dei_mmr0 = &mmr_adb->dei_regs[0];
	bool deinterlace = true;
	u32 val = 0;

	/*
	 * according to TRM, we should set DEI in progressive bypass mode when
	 * the input content is progressive, however, DEI is bypassed correctly
	 * for both progressive and interlace content in interlace bypass mode.
	 * It has been recommended not to use progressive bypass mode.
	 */
	if (!(s_q_data->flags & Q_IS_INTERLACED) || !ctx->deinterlacing) {
		deinterlace = false;
		val = VPE_DEI_INTERLACE_BYPASS;
	}

	src_h = deinterlace ? src_h * 2 : src_h;

	val |= (src_h << VPE_DEI_HEIGHT_SHIFT) |
		(src_w << VPE_DEI_WIDTH_SHIFT) |
		VPE_DEI_FIELD_FLUSH;

	*dei_mmr0 = val;

	ctx->load_mmrs = true;
}

static void set_dei_shadow_registers(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *dei_mmr = &mmr_adb->dei_regs[0];
	const struct vpe_dei_regs *cur = &dei_regs;

	dei_mmr[2]  = cur->mdt_spacial_freq_thr_reg;
	dei_mmr[3]  = cur->edi_config_reg;
	dei_mmr[4]  = cur->edi_lut_reg0;
	dei_mmr[5]  = cur->edi_lut_reg1;
	dei_mmr[6]  = cur->edi_lut_reg2;
	dei_mmr[7]  = cur->edi_lut_reg3;

	ctx->load_mmrs = true;
}

static void config_edi_input_mode(struct vpe_ctx *ctx, int mode)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *edi_config_reg = &mmr_adb->dei_regs[3];

	if (mode & 0x2)
		write_field(edi_config_reg, 1, 1, 2);	/* EDI_ENABLE_3D */

	if (mode & 0x3)
		write_field(edi_config_reg, 1, 1, 3);	/* EDI_CHROMA_3D  */

	write_field(edi_config_reg, mode, VPE_EDI_INP_MODE_MASK,
		VPE_EDI_INP_MODE_SHIFT);

	ctx->load_mmrs = true;
}

/*
 * Set the shadow registers whose values are modified when either the
 * source or destination format is changed.
 */
static int set_srcdst_params(struct vpe_ctx *ctx)
{
	struct vpe_q_data *s_q_data =  &ctx->q_data[Q_DATA_SRC];
	struct vpe_q_data *d_q_data =  &ctx->q_data[Q_DATA_DST];
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	unsigned int src_w = s_q_data->c_rect.width;
	unsigned int src_h = s_q_data->c_rect.height;
	unsigned int dst_w = d_q_data->c_rect.width;
	unsigned int dst_h = d_q_data->c_rect.height;
	struct v4l2_pix_format_mplane *spix;
	size_t mv_buf_size;
	int ret;

	ctx->sequence = 0;
	ctx->field = V4L2_FIELD_TOP;
	spix = &s_q_data->format.fmt.pix_mp;

	if ((s_q_data->flags & Q_IS_INTERLACED) &&
			!(d_q_data->flags & Q_IS_INTERLACED)) {
		int bytes_per_line;
		const struct vpdma_data_format *mv =
			&vpdma_misc_fmts[VPDMA_DATA_FMT_MV];

		/*
		 * we make sure that the source image has a 16 byte aligned
		 * stride, we need to do the same for the motion vector buffer
		 * by aligning it's stride to the next 16 byte boundary. this
		 * extra space will not be used by the de-interlacer, but will
		 * ensure that vpdma operates correctly
		 */
		bytes_per_line = ALIGN((spix->width * mv->depth) >> 3,
				       VPDMA_STRIDE_ALIGN);
		mv_buf_size = bytes_per_line * spix->height;

		ctx->deinterlacing = true;
		src_h <<= 1;
	} else {
		ctx->deinterlacing = false;
		mv_buf_size = 0;
	}

	free_vbs(ctx);
	ctx->src_vbs[2] = ctx->src_vbs[1] = ctx->src_vbs[0] = NULL;

	ret = realloc_mv_buffers(ctx, mv_buf_size);
	if (ret)
		return ret;

	set_cfg_modes(ctx);
	set_dei_regs(ctx);

	csc_set_coeff(ctx->dev->csc, &mmr_adb->csc_regs[0],
		      &s_q_data->format, &d_q_data->format);

	sc_set_hs_coeffs(ctx->dev->sc, ctx->sc_coeff_h.addr, src_w, dst_w);
	sc_set_vs_coeffs(ctx->dev->sc, ctx->sc_coeff_v.addr, src_h, dst_h);

	sc_config_scaler(ctx->dev->sc, &mmr_adb->sc_regs0[0],
		&mmr_adb->sc_regs8[0], &mmr_adb->sc_regs17[0],
		src_w, src_h, dst_w, dst_h);

	return 0;
}

/*
 * mem2mem callbacks
 */

/*
 * job_ready() - check whether an instance is ready to be scheduled to run
 */
static int job_ready(void *priv)
{
	struct vpe_ctx *ctx = priv;

	/*
	 * This check is needed as this might be called directly from driver
	 * When called by m2m framework, this will always satisfy, but when
	 * called from vpe_irq, this might fail. (src stream with zero buffers)
	 */
	if (v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) <= 0 ||
		v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) <= 0)
		return 0;

	return 1;
}

static void job_abort(void *priv)
{
	struct vpe_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	ctx->aborting = 1;
}

static void vpe_dump_regs(struct vpe_dev *dev)
{
#define DUMPREG(r) vpe_dbg(dev, "%-35s %08x\n", #r, read_reg(dev, VPE_##r))

	vpe_dbg(dev, "VPE Registers:\n");

	DUMPREG(PID);
	DUMPREG(SYSCONFIG);
	DUMPREG(INT0_STATUS0_RAW);
	DUMPREG(INT0_STATUS0);
	DUMPREG(INT0_ENABLE0);
	DUMPREG(INT0_STATUS1_RAW);
	DUMPREG(INT0_STATUS1);
	DUMPREG(INT0_ENABLE1);
	DUMPREG(CLK_ENABLE);
	DUMPREG(CLK_RESET);
	DUMPREG(CLK_FORMAT_SELECT);
	DUMPREG(CLK_RANGE_MAP);
	DUMPREG(US1_R0);
	DUMPREG(US1_R1);
	DUMPREG(US1_R2);
	DUMPREG(US1_R3);
	DUMPREG(US1_R4);
	DUMPREG(US1_R5);
	DUMPREG(US1_R6);
	DUMPREG(US1_R7);
	DUMPREG(US2_R0);
	DUMPREG(US2_R1);
	DUMPREG(US2_R2);
	DUMPREG(US2_R3);
	DUMPREG(US2_R4);
	DUMPREG(US2_R5);
	DUMPREG(US2_R6);
	DUMPREG(US2_R7);
	DUMPREG(US3_R0);
	DUMPREG(US3_R1);
	DUMPREG(US3_R2);
	DUMPREG(US3_R3);
	DUMPREG(US3_R4);
	DUMPREG(US3_R5);
	DUMPREG(US3_R6);
	DUMPREG(US3_R7);
	DUMPREG(DEI_FRAME_SIZE);
	DUMPREG(MDT_BYPASS);
	DUMPREG(MDT_SF_THRESHOLD);
	DUMPREG(EDI_CONFIG);
	DUMPREG(DEI_EDI_LUT_R0);
	DUMPREG(DEI_EDI_LUT_R1);
	DUMPREG(DEI_EDI_LUT_R2);
	DUMPREG(DEI_EDI_LUT_R3);
	DUMPREG(DEI_FMD_WINDOW_R0);
	DUMPREG(DEI_FMD_WINDOW_R1);
	DUMPREG(DEI_FMD_CONTROL_R0);
	DUMPREG(DEI_FMD_CONTROL_R1);
	DUMPREG(DEI_FMD_STATUS_R0);
	DUMPREG(DEI_FMD_STATUS_R1);
	DUMPREG(DEI_FMD_STATUS_R2);
#undef DUMPREG

	sc_dump_regs(dev->sc);
	csc_dump_regs(dev->csc);
}

static void add_out_dtd(struct vpe_ctx *ctx, int port)
{
	struct vpe_q_data *q_data = &ctx->q_data[Q_DATA_DST];
	const struct vpe_port_data *p_data = &port_data[port];
	struct vb2_buffer *vb = &ctx->dst_vb->vb2_buf;
	struct vpe_fmt *fmt = q_data->fmt;
	const struct vpdma_data_format *vpdma_fmt;
	int mv_buf_selector = !ctx->src_mv_buf_selector;
	struct v4l2_pix_format_mplane *pix;
	dma_addr_t dma_addr;
	u32 flags = 0;
	u32 offset = 0;
	u32 stride;

	if (port == VPE_PORT_MV_OUT) {
		vpdma_fmt = &vpdma_misc_fmts[VPDMA_DATA_FMT_MV];
		dma_addr = ctx->mv_buf_dma[mv_buf_selector];
		q_data = &ctx->q_data[Q_DATA_SRC];
		pix = &q_data->format.fmt.pix_mp;
		stride = ALIGN((pix->width * vpdma_fmt->depth) >> 3,
			       VPDMA_STRIDE_ALIGN);
	} else {
		/* to incorporate interleaved formats */
		int plane = fmt->coplanar ? p_data->vb_part : 0;

		pix = &q_data->format.fmt.pix_mp;
		vpdma_fmt = fmt->vpdma_fmt[plane];
		/*
		 * If we are using a single plane buffer and
		 * we need to set a separate vpdma chroma channel.
		 */
		if (pix->num_planes == 1 && plane) {
			dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
			/* Compute required offset */
			offset = pix->plane_fmt[0].bytesperline * pix->height;
		} else {
			dma_addr = vb2_dma_contig_plane_dma_addr(vb, plane);
			/* Use address as is, no offset */
			offset = 0;
		}
		if (!dma_addr) {
			vpe_err(ctx->dev,
				"acquiring output buffer(%d) dma_addr failed\n",
				port);
			return;
		}
		/* Apply the offset */
		dma_addr += offset;
		stride = pix->plane_fmt[VPE_LUMA].bytesperline;
	}

	if (q_data->flags & Q_DATA_FRAME_1D)
		flags |= VPDMA_DATA_FRAME_1D;
	if (q_data->flags & Q_DATA_MODE_TILED)
		flags |= VPDMA_DATA_MODE_TILED;

	vpdma_set_max_size(ctx->dev->vpdma, VPDMA_MAX_SIZE1,
			   MAX_W, MAX_H);

	vpdma_add_out_dtd(&ctx->desc_list, pix->width,
			  stride, &q_data->c_rect,
			  vpdma_fmt, dma_addr, MAX_OUT_WIDTH_REG1,
			  MAX_OUT_HEIGHT_REG1, p_data->channel, flags);
}

static void add_in_dtd(struct vpe_ctx *ctx, int port)
{
	struct vpe_q_data *q_data = &ctx->q_data[Q_DATA_SRC];
	const struct vpe_port_data *p_data = &port_data[port];
	struct vb2_buffer *vb = &ctx->src_vbs[p_data->vb_index]->vb2_buf;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpe_fmt *fmt = q_data->fmt;
	struct v4l2_pix_format_mplane *pix;
	const struct vpdma_data_format *vpdma_fmt;
	int mv_buf_selector = ctx->src_mv_buf_selector;
	int field = vbuf->field == V4L2_FIELD_BOTTOM;
	int frame_width, frame_height;
	dma_addr_t dma_addr;
	u32 flags = 0;
	u32 offset = 0;
	u32 stride;

	pix = &q_data->format.fmt.pix_mp;
	if (port == VPE_PORT_MV_IN) {
		vpdma_fmt = &vpdma_misc_fmts[VPDMA_DATA_FMT_MV];
		dma_addr = ctx->mv_buf_dma[mv_buf_selector];
		stride = ALIGN((pix->width * vpdma_fmt->depth) >> 3,
			       VPDMA_STRIDE_ALIGN);
	} else {
		/* to incorporate interleaved formats */
		int plane = fmt->coplanar ? p_data->vb_part : 0;

		vpdma_fmt = fmt->vpdma_fmt[plane];
		/*
		 * If we are using a single plane buffer and
		 * we need to set a separate vpdma chroma channel.
		 */
		if (pix->num_planes == 1 && plane) {
			dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
			/* Compute required offset */
			offset = pix->plane_fmt[0].bytesperline * pix->height;
		} else {
			dma_addr = vb2_dma_contig_plane_dma_addr(vb, plane);
			/* Use address as is, no offset */
			offset = 0;
		}
		if (!dma_addr) {
			vpe_err(ctx->dev,
				"acquiring output buffer(%d) dma_addr failed\n",
				port);
			return;
		}
		/* Apply the offset */
		dma_addr += offset;
		stride = pix->plane_fmt[VPE_LUMA].bytesperline;

		/*
		 * field used in VPDMA desc  = 0 (top) / 1 (bottom)
		 * Use top or bottom field from same vb alternately
		 * For each de-interlacing operation, f,f-1,f-2 should be one
		 * of TBT or BTB
		 */
		if (q_data->flags & Q_DATA_INTERLACED_SEQ_TB ||
		    q_data->flags & Q_DATA_INTERLACED_SEQ_BT) {
			/* Select initial value based on format */
			if (q_data->flags & Q_DATA_INTERLACED_SEQ_BT)
				field = 1;
			else
				field = 0;

			/* Toggle for each vb_index and each operation */
			field = (field + p_data->vb_index + ctx->sequence) % 2;

			if (field) {
				int height = pix->height / 2;
				int bpp;

				if (fmt->fourcc == V4L2_PIX_FMT_NV12 ||
				    fmt->fourcc == V4L2_PIX_FMT_NV21)
					bpp = 1;
				else
					bpp = vpdma_fmt->depth >> 3;

				if (plane)
					height /= 2;

				dma_addr += pix->width * height * bpp;
			}
		}
	}

	if (q_data->flags & Q_DATA_FRAME_1D)
		flags |= VPDMA_DATA_FRAME_1D;
	if (q_data->flags & Q_DATA_MODE_TILED)
		flags |= VPDMA_DATA_MODE_TILED;

	frame_width = q_data->c_rect.width;
	frame_height = q_data->c_rect.height;

	if (p_data->vb_part && (fmt->fourcc == V4L2_PIX_FMT_NV12 ||
				fmt->fourcc == V4L2_PIX_FMT_NV21))
		frame_height /= 2;

	vpdma_add_in_dtd(&ctx->desc_list, pix->width, stride,
			 &q_data->c_rect, vpdma_fmt, dma_addr,
			 p_data->channel, field, flags, frame_width,
			 frame_height, 0, 0);
}

/*
 * Enable the expected IRQ sources
 */
static void enable_irqs(struct vpe_ctx *ctx)
{
	write_reg(ctx->dev, VPE_INT0_ENABLE0_SET, VPE_INT0_LIST0_COMPLETE);
	write_reg(ctx->dev, VPE_INT0_ENABLE1_SET, VPE_DEI_ERROR_INT |
				VPE_DS1_UV_ERROR_INT);

	vpdma_enable_list_complete_irq(ctx->dev->vpdma, 0, 0, true);
}

static void disable_irqs(struct vpe_ctx *ctx)
{
	write_reg(ctx->dev, VPE_INT0_ENABLE0_CLR, 0xffffffff);
	write_reg(ctx->dev, VPE_INT0_ENABLE1_CLR, 0xffffffff);

	vpdma_enable_list_complete_irq(ctx->dev->vpdma, 0, 0, false);
}

/* device_run() - prepares and starts the device
 *
 * This function is only called when both the source and destination
 * buffers are in place.
 */
static void device_run(void *priv)
{
	struct vpe_ctx *ctx = priv;
	struct sc_data *sc = ctx->dev->sc;
	struct vpe_q_data *d_q_data = &ctx->q_data[Q_DATA_DST];
	struct vpe_q_data *s_q_data = &ctx->q_data[Q_DATA_SRC];
	const struct v4l2_format_info *d_finfo;

	d_finfo = v4l2_format_info(d_q_data->fmt->fourcc);

	if (ctx->deinterlacing && s_q_data->flags & Q_IS_SEQ_XX &&
	    ctx->sequence % 2 == 0) {
		/* When using SEQ_XX type buffers, each buffer has two fields
		 * each buffer has two fields (top & bottom)
		 * Removing one buffer is actually getting two fields
		 * Alternate between two operations:-
		 * Even : consume one field but DO NOT REMOVE from queue
		 * Odd : consume other field and REMOVE from queue
		 */
		ctx->src_vbs[0] = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		WARN_ON(ctx->src_vbs[0] == NULL);
	} else {
		ctx->src_vbs[0] = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		WARN_ON(ctx->src_vbs[0] == NULL);
	}

	ctx->dst_vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	WARN_ON(ctx->dst_vb == NULL);

	if (ctx->deinterlacing) {

		if (ctx->src_vbs[2] == NULL) {
			ctx->src_vbs[2] = ctx->src_vbs[0];
			WARN_ON(ctx->src_vbs[2] == NULL);
			ctx->src_vbs[1] = ctx->src_vbs[0];
			WARN_ON(ctx->src_vbs[1] == NULL);
		}

		/*
		 * we have output the first 2 frames through line average, we
		 * now switch to EDI de-interlacer
		 */
		if (ctx->sequence == 2)
			config_edi_input_mode(ctx, 0x3); /* EDI (Y + UV) */
	}

	/* config descriptors */
	if (ctx->dev->loaded_mmrs != ctx->mmr_adb.dma_addr || ctx->load_mmrs) {
		vpdma_map_desc_buf(ctx->dev->vpdma, &ctx->mmr_adb);
		vpdma_add_cfd_adb(&ctx->desc_list, CFD_MMR_CLIENT, &ctx->mmr_adb);

		set_line_modes(ctx);

		ctx->dev->loaded_mmrs = ctx->mmr_adb.dma_addr;
		ctx->load_mmrs = false;
	}

	if (sc->loaded_coeff_h != ctx->sc_coeff_h.dma_addr ||
			sc->load_coeff_h) {
		vpdma_map_desc_buf(ctx->dev->vpdma, &ctx->sc_coeff_h);
		vpdma_add_cfd_block(&ctx->desc_list, CFD_SC_CLIENT,
			&ctx->sc_coeff_h, 0);

		sc->loaded_coeff_h = ctx->sc_coeff_h.dma_addr;
		sc->load_coeff_h = false;
	}

	if (sc->loaded_coeff_v != ctx->sc_coeff_v.dma_addr ||
			sc->load_coeff_v) {
		vpdma_map_desc_buf(ctx->dev->vpdma, &ctx->sc_coeff_v);
		vpdma_add_cfd_block(&ctx->desc_list, CFD_SC_CLIENT,
			&ctx->sc_coeff_v, SC_COEF_SRAM_SIZE >> 4);

		sc->loaded_coeff_v = ctx->sc_coeff_v.dma_addr;
		sc->load_coeff_v = false;
	}

	/* output data descriptors */
	if (ctx->deinterlacing)
		add_out_dtd(ctx, VPE_PORT_MV_OUT);

	if (v4l2_is_format_rgb(d_finfo)) {
		add_out_dtd(ctx, VPE_PORT_RGB_OUT);
	} else {
		add_out_dtd(ctx, VPE_PORT_LUMA_OUT);
		if (d_q_data->fmt->coplanar)
			add_out_dtd(ctx, VPE_PORT_CHROMA_OUT);
	}

	/* input data descriptors */
	if (ctx->deinterlacing) {
		add_in_dtd(ctx, VPE_PORT_LUMA3_IN);
		add_in_dtd(ctx, VPE_PORT_CHROMA3_IN);

		add_in_dtd(ctx, VPE_PORT_LUMA2_IN);
		add_in_dtd(ctx, VPE_PORT_CHROMA2_IN);
	}

	add_in_dtd(ctx, VPE_PORT_LUMA1_IN);
	add_in_dtd(ctx, VPE_PORT_CHROMA1_IN);

	if (ctx->deinterlacing)
		add_in_dtd(ctx, VPE_PORT_MV_IN);

	/* sync on channel control descriptors for input ports */
	vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_LUMA1_IN);
	vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_CHROMA1_IN);

	if (ctx->deinterlacing) {
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_LUMA2_IN);
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_CHROMA2_IN);

		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_LUMA3_IN);
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_CHROMA3_IN);

		vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_MV_IN);
	}

	/* sync on channel control descriptors for output ports */
	if (v4l2_is_format_rgb(d_finfo)) {
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_RGB_OUT);
	} else {
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_LUMA_OUT);
		if (d_q_data->fmt->coplanar)
			vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
				VPE_CHAN_CHROMA_OUT);
	}

	if (ctx->deinterlacing)
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_MV_OUT);

	enable_irqs(ctx);

	vpdma_map_desc_buf(ctx->dev->vpdma, &ctx->desc_list.buf);
	vpdma_submit_descs(ctx->dev->vpdma, &ctx->desc_list, 0);
}

static void dei_error(struct vpe_ctx *ctx)
{
	dev_warn(ctx->dev->v4l2_dev.dev,
		"received DEI error interrupt\n");
}

static void ds1_uv_error(struct vpe_ctx *ctx)
{
	dev_warn(ctx->dev->v4l2_dev.dev,
		"received downsampler error interrupt\n");
}

static irqreturn_t vpe_irq(int irq_vpe, void *data)
{
	struct vpe_dev *dev = (struct vpe_dev *)data;
	struct vpe_ctx *ctx;
	struct vpe_q_data *d_q_data;
	struct vb2_v4l2_buffer *s_vb, *d_vb;
	unsigned long flags;
	u32 irqst0, irqst1;
	bool list_complete = false;

	irqst0 = read_reg(dev, VPE_INT0_STATUS0);
	if (irqst0) {
		write_reg(dev, VPE_INT0_STATUS0_CLR, irqst0);
		vpe_dbg(dev, "INT0_STATUS0 = 0x%08x\n", irqst0);
	}

	irqst1 = read_reg(dev, VPE_INT0_STATUS1);
	if (irqst1) {
		write_reg(dev, VPE_INT0_STATUS1_CLR, irqst1);
		vpe_dbg(dev, "INT0_STATUS1 = 0x%08x\n", irqst1);
	}

	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (!ctx) {
		vpe_err(dev, "instance released before end of transaction\n");
		goto handled;
	}

	if (irqst1) {
		if (irqst1 & VPE_DEI_ERROR_INT) {
			irqst1 &= ~VPE_DEI_ERROR_INT;
			dei_error(ctx);
		}
		if (irqst1 & VPE_DS1_UV_ERROR_INT) {
			irqst1 &= ~VPE_DS1_UV_ERROR_INT;
			ds1_uv_error(ctx);
		}
	}

	if (irqst0) {
		if (irqst0 & VPE_INT0_LIST0_COMPLETE)
			vpdma_clear_list_stat(ctx->dev->vpdma, 0, 0);

		irqst0 &= ~(VPE_INT0_LIST0_COMPLETE);
		list_complete = true;
	}

	if (irqst0 | irqst1) {
		dev_warn(dev->v4l2_dev.dev, "Unexpected interrupt: INT0_STATUS0 = 0x%08x, INT0_STATUS1 = 0x%08x\n",
			irqst0, irqst1);
	}

	/*
	 * Setup next operation only when list complete IRQ occurs
	 * otherwise, skip the following code
	 */
	if (!list_complete)
		goto handled;

	disable_irqs(ctx);

	vpdma_unmap_desc_buf(dev->vpdma, &ctx->desc_list.buf);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->mmr_adb);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->sc_coeff_h);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->sc_coeff_v);

	vpdma_reset_desc_list(&ctx->desc_list);

	 /* the previous dst mv buffer becomes the next src mv buffer */
	ctx->src_mv_buf_selector = !ctx->src_mv_buf_selector;

	s_vb = ctx->src_vbs[0];
	d_vb = ctx->dst_vb;

	d_vb->flags = s_vb->flags;
	d_vb->vb2_buf.timestamp = s_vb->vb2_buf.timestamp;

	if (s_vb->flags & V4L2_BUF_FLAG_TIMECODE)
		d_vb->timecode = s_vb->timecode;

	d_vb->sequence = ctx->sequence;
	s_vb->sequence = ctx->sequence;

	d_q_data = &ctx->q_data[Q_DATA_DST];
	if (d_q_data->flags & Q_IS_INTERLACED) {
		d_vb->field = ctx->field;
		if (ctx->field == V4L2_FIELD_BOTTOM) {
			ctx->sequence++;
			ctx->field = V4L2_FIELD_TOP;
		} else {
			WARN_ON(ctx->field != V4L2_FIELD_TOP);
			ctx->field = V4L2_FIELD_BOTTOM;
		}
	} else {
		d_vb->field = V4L2_FIELD_NONE;
		ctx->sequence++;
	}

	if (ctx->deinterlacing) {
		/*
		 * Allow source buffer to be dequeued only if it won't be used
		 * in the next iteration. All vbs are initialized to first
		 * buffer and we are shifting buffers every iteration, for the
		 * first two iterations, no buffer will be dequeued.
		 * This ensures that driver will keep (n-2)th (n-1)th and (n)th
		 * field when deinterlacing is enabled
		 */
		if (ctx->src_vbs[2] != ctx->src_vbs[1])
			s_vb = ctx->src_vbs[2];
		else
			s_vb = NULL;
	}

	spin_lock_irqsave(&dev->lock, flags);

	if (s_vb)
		v4l2_m2m_buf_done(s_vb, VB2_BUF_STATE_DONE);

	v4l2_m2m_buf_done(d_vb, VB2_BUF_STATE_DONE);

	spin_unlock_irqrestore(&dev->lock, flags);

	if (ctx->deinterlacing) {
		ctx->src_vbs[2] = ctx->src_vbs[1];
		ctx->src_vbs[1] = ctx->src_vbs[0];
	}

	/*
	 * Since the vb2_buf_done has already been called fir therse
	 * buffer we can now NULL them out so that we won't try
	 * to clean out stray pointer later on.
	*/
	ctx->src_vbs[0] = NULL;
	ctx->dst_vb = NULL;

	if (ctx->aborting)
		goto finished;

	ctx->bufs_completed++;
	if (ctx->bufs_completed < ctx->bufs_per_job && job_ready(ctx)) {
		device_run(ctx);
		goto handled;
	}

finished:
	vpe_dbg(ctx->dev, "finishing transaction\n");
	ctx->bufs_completed = 0;
	v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
handled:
	return IRQ_HANDLED;
}

/*
 * video ioctls
 */
static int vpe_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	strscpy(cap->driver, VPE_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, VPE_MODULE_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		VPE_MODULE_NAME);
	return 0;
}

static int __enum_fmt(struct v4l2_fmtdesc *f, u32 type)
{
	int i, index;
	struct vpe_fmt *fmt = NULL;

	index = 0;
	for (i = 0; i < ARRAY_SIZE(vpe_formats); ++i) {
		if (vpe_formats[i].types & type) {
			if (index == f->index) {
				fmt = &vpe_formats[i];
				break;
			}
			index++;
		}
	}

	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vpe_enum_fmt(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		return __enum_fmt(f, VPE_FMT_TYPE_OUTPUT);

	return __enum_fmt(f, VPE_FMT_TYPE_CAPTURE);
}

static int vpe_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct vpe_ctx *ctx = file->private_data;
	struct vb2_queue *vq;
	struct vpe_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	*f = q_data->format;

	if (V4L2_TYPE_IS_CAPTURE(f->type)) {
		struct vpe_q_data *s_q_data;
		struct v4l2_pix_format_mplane *spix;

		/* get colorimetry from the source queue */
		s_q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		spix = &s_q_data->format.fmt.pix_mp;

		pix->colorspace = spix->colorspace;
		pix->xfer_func = spix->xfer_func;
		pix->ycbcr_enc = spix->ycbcr_enc;
		pix->quantization = spix->quantization;
	}

	return 0;
}

static int __vpe_try_fmt(struct vpe_ctx *ctx, struct v4l2_format *f,
		       struct vpe_fmt *fmt, int type)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt;
	unsigned int w_align;
	int i, depth, depth_bytes, height;
	unsigned int stride = 0;
	const struct v4l2_format_info *finfo;

	if (!fmt || !(fmt->types & type)) {
		vpe_dbg(ctx->dev, "Fourcc format (0x%08x) invalid.\n",
			pix->pixelformat);
		fmt = __find_format(V4L2_PIX_FMT_YUYV);
	}

	if (pix->field != V4L2_FIELD_NONE &&
	    pix->field != V4L2_FIELD_ALTERNATE &&
	    pix->field != V4L2_FIELD_SEQ_TB &&
	    pix->field != V4L2_FIELD_SEQ_BT)
		pix->field = V4L2_FIELD_NONE;

	depth = fmt->vpdma_fmt[VPE_LUMA]->depth;

	/*
	 * the line stride should 16 byte aligned for VPDMA to work, based on
	 * the bytes per pixel, figure out how much the width should be aligned
	 * to make sure line stride is 16 byte aligned
	 */
	depth_bytes = depth >> 3;

	if (depth_bytes == 3) {
		/*
		 * if bpp is 3(as in some RGB formats), the pixel width doesn't
		 * really help in ensuring line stride is 16 byte aligned
		 */
		w_align = 4;
	} else {
		/*
		 * for the remainder bpp(4, 2 and 1), the pixel width alignment
		 * can ensure a line stride alignment of 16 bytes. For example,
		 * if bpp is 2, then the line stride can be 16 byte aligned if
		 * the width is 8 byte aligned
		 */

		/*
		 * HACK: using order_base_2() here causes lots of asm output
		 * errors with smatch, on i386:
		 * ./arch/x86/include/asm/bitops.h:457:22:
		 *		 warning: asm output is not an lvalue
		 * Perhaps some gcc optimization is doing the wrong thing
		 * there.
		 * Let's get rid of them by doing the calculus on two steps
		 */
		w_align = roundup_pow_of_two(VPDMA_DESC_ALIGN / depth_bytes);
		w_align = ilog2(w_align);
	}

	v4l_bound_align_image(&pix->width, MIN_W, MAX_W, w_align,
			      &pix->height, MIN_H, MAX_H, H_ALIGN,
			      S_ALIGN);

	if (!pix->num_planes || pix->num_planes > 2)
		pix->num_planes = fmt->coplanar ? 2 : 1;
	else if (pix->num_planes > 1 && !fmt->coplanar)
		pix->num_planes = 1;

	pix->pixelformat = fmt->fourcc;
	finfo = v4l2_format_info(fmt->fourcc);

	/*
	 * For the actual image parameters, we need to consider the field
	 * height of the image for SEQ_XX buffers.
	 */
	if (pix->field == V4L2_FIELD_SEQ_TB || pix->field == V4L2_FIELD_SEQ_BT)
		height = pix->height / 2;
	else
		height = pix->height;

	if (!pix->colorspace) {
		if (v4l2_is_format_rgb(finfo)) {
			pix->colorspace = V4L2_COLORSPACE_SRGB;
		} else {
			if (height > 1280)	/* HD */
				pix->colorspace = V4L2_COLORSPACE_REC709;
			else			/* SD */
				pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
		}
	}

	for (i = 0; i < pix->num_planes; i++) {
		plane_fmt = &pix->plane_fmt[i];
		depth = fmt->vpdma_fmt[i]->depth;

		stride = (pix->width * fmt->vpdma_fmt[VPE_LUMA]->depth) >> 3;
		if (stride > plane_fmt->bytesperline)
			plane_fmt->bytesperline = stride;

		plane_fmt->bytesperline = clamp_t(u32, plane_fmt->bytesperline,
						  stride,
						  VPDMA_MAX_STRIDE);

		plane_fmt->bytesperline = ALIGN(plane_fmt->bytesperline,
						VPDMA_STRIDE_ALIGN);

		if (i == VPE_LUMA) {
			plane_fmt->sizeimage = pix->height *
					       plane_fmt->bytesperline;

			if (pix->num_planes == 1 && fmt->coplanar)
				plane_fmt->sizeimage += pix->height *
					plane_fmt->bytesperline *
					fmt->vpdma_fmt[VPE_CHROMA]->depth >> 3;

		} else { /* i == VIP_CHROMA */
			plane_fmt->sizeimage = (pix->height *
					       plane_fmt->bytesperline *
					       depth) >> 3;
		}
	}

	return 0;
}

static int vpe_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vpe_ctx *ctx = file->private_data;
	struct vpe_fmt *fmt = find_format(f);

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		return __vpe_try_fmt(ctx, f, fmt, VPE_FMT_TYPE_OUTPUT);
	else
		return __vpe_try_fmt(ctx, f, fmt, VPE_FMT_TYPE_CAPTURE);
}

static int __vpe_s_fmt(struct vpe_ctx *ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct v4l2_pix_format_mplane *qpix;
	struct vpe_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		vpe_err(ctx->dev, "queue busy\n");
		return -EBUSY;
	}

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	qpix = &q_data->format.fmt.pix_mp;
	q_data->fmt		= find_format(f);
	q_data->format = *f;

	q_data->c_rect.left	= 0;
	q_data->c_rect.top	= 0;
	q_data->c_rect.width	= pix->width;
	q_data->c_rect.height	= pix->height;

	if (qpix->field == V4L2_FIELD_ALTERNATE)
		q_data->flags |= Q_DATA_INTERLACED_ALTERNATE;
	else if (qpix->field == V4L2_FIELD_SEQ_TB)
		q_data->flags |= Q_DATA_INTERLACED_SEQ_TB;
	else if (qpix->field == V4L2_FIELD_SEQ_BT)
		q_data->flags |= Q_DATA_INTERLACED_SEQ_BT;
	else
		q_data->flags &= ~Q_IS_INTERLACED;

	/* the crop height is halved for the case of SEQ_XX buffers */
	if (q_data->flags & Q_IS_SEQ_XX)
		q_data->c_rect.height /= 2;

	vpe_dbg(ctx->dev, "Setting format for type %d, wxh: %dx%d, fmt: %d bpl_y %d",
		f->type, pix->width, pix->height, pix->pixelformat,
		pix->plane_fmt[0].bytesperline);
	if (pix->num_planes == 2)
		vpe_dbg(ctx->dev, " bpl_uv %d\n",
			pix->plane_fmt[1].bytesperline);

	return 0;
}

static int vpe_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	int ret;
	struct vpe_ctx *ctx = file->private_data;

	ret = vpe_try_fmt(file, priv, f);
	if (ret)
		return ret;

	ret = __vpe_s_fmt(ctx, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		set_src_registers(ctx);
	else
		set_dst_registers(ctx);

	return set_srcdst_params(ctx);
}

static int __vpe_try_selection(struct vpe_ctx *ctx, struct v4l2_selection *s)
{
	struct vpe_q_data *q_data;
	struct v4l2_pix_format_mplane *pix;
	int height;

	if ((s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
	    (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT))
		return -EINVAL;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	pix = &q_data->format.fmt.pix_mp;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
		/*
		 * COMPOSE target is only valid for capture buffer type, return
		 * error for output buffer type
		 */
		if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_CROP:
		/*
		 * CROP target is only valid for output buffer type, return
		 * error for capture buffer type
		 */
		if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	/*
	 * bound and default crop/compose targets are invalid targets to
	 * try/set
	 */
	default:
		return -EINVAL;
	}

	/*
	 * For SEQ_XX buffers, crop height should be less than the height of
	 * the field height, not the buffer height
	 */
	if (q_data->flags & Q_IS_SEQ_XX)
		height = pix->height / 2;
	else
		height = pix->height;

	if (s->r.top < 0 || s->r.left < 0) {
		vpe_err(ctx->dev, "negative values for top and left\n");
		s->r.top = s->r.left = 0;
	}

	v4l_bound_align_image(&s->r.width, MIN_W, pix->width, 1,
		&s->r.height, MIN_H, height, H_ALIGN, S_ALIGN);

	/* adjust left/top if cropping rectangle is out of bounds */
	if (s->r.left + s->r.width > pix->width)
		s->r.left = pix->width - s->r.width;
	if (s->r.top + s->r.height > pix->height)
		s->r.top = pix->height - s->r.height;

	return 0;
}

static int vpe_g_selection(struct file *file, void *fh,
		struct v4l2_selection *s)
{
	struct vpe_ctx *ctx = file->private_data;
	struct vpe_q_data *q_data;
	struct v4l2_pix_format_mplane *pix;
	bool use_c_rect = false;

	if ((s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
	    (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT))
		return -EINVAL;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	pix = &q_data->format.fmt.pix_mp;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		use_c_rect = true;
		break;
	case V4L2_SEL_TGT_CROP:
		if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		use_c_rect = true;
		break;
	default:
		return -EINVAL;
	}

	if (use_c_rect) {
		/*
		 * for CROP/COMPOSE target type, return c_rect params from the
		 * respective buffer type
		 */
		s->r = q_data->c_rect;
	} else {
		/*
		 * for DEFAULT/BOUNDS target type, return width and height from
		 * S_FMT of the respective buffer type
		 */
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = pix->width;
		s->r.height = pix->height;
	}

	return 0;
}


static int vpe_s_selection(struct file *file, void *fh,
		struct v4l2_selection *s)
{
	struct vpe_ctx *ctx = file->private_data;
	struct vpe_q_data *q_data;
	struct v4l2_selection sel = *s;
	int ret;

	ret = __vpe_try_selection(ctx, &sel);
	if (ret)
		return ret;

	q_data = get_q_data(ctx, sel.type);
	if (!q_data)
		return -EINVAL;

	if ((q_data->c_rect.left == sel.r.left) &&
			(q_data->c_rect.top == sel.r.top) &&
			(q_data->c_rect.width == sel.r.width) &&
			(q_data->c_rect.height == sel.r.height)) {
		vpe_dbg(ctx->dev,
			"requested crop/compose values are already set\n");
		return 0;
	}

	q_data->c_rect = sel.r;

	return set_srcdst_params(ctx);
}

/*
 * defines number of buffers/frames a context can process with VPE before
 * switching to a different context. default value is 1 buffer per context
 */
#define V4L2_CID_VPE_BUFS_PER_JOB		(V4L2_CID_USER_TI_VPE_BASE + 0)

static int vpe_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpe_ctx *ctx =
		container_of(ctrl->handler, struct vpe_ctx, hdl);

	switch (ctrl->id) {
	case V4L2_CID_VPE_BUFS_PER_JOB:
		ctx->bufs_per_job = ctrl->val;
		break;

	default:
		vpe_err(ctx->dev, "Invalid control\n");
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops vpe_ctrl_ops = {
	.s_ctrl = vpe_s_ctrl,
};

static const struct v4l2_ioctl_ops vpe_ioctl_ops = {
	.vidioc_querycap		= vpe_querycap,

	.vidioc_enum_fmt_vid_cap	= vpe_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= vpe_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= vpe_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= vpe_s_fmt,

	.vidioc_enum_fmt_vid_out	= vpe_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane	= vpe_g_fmt,
	.vidioc_try_fmt_vid_out_mplane	= vpe_try_fmt,
	.vidioc_s_fmt_vid_out_mplane	= vpe_s_fmt,

	.vidioc_g_selection		= vpe_g_selection,
	.vidioc_s_selection		= vpe_s_selection,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*
 * Queue operations
 */
static int vpe_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	int i;
	struct vpe_ctx *ctx = vb2_get_drv_priv(vq);
	struct vpe_q_data *q_data;
	struct v4l2_pix_format_mplane *pix;

	q_data = get_q_data(ctx, vq->type);
	if (!q_data)
		return -EINVAL;

	pix = &q_data->format.fmt.pix_mp;
	*nplanes = pix->num_planes;

	for (i = 0; i < *nplanes; i++)
		sizes[i] = pix->plane_fmt[i].sizeimage;

	vpe_dbg(ctx->dev, "get %d buffer(s) of size %d", *nbuffers,
		sizes[VPE_LUMA]);
	if (*nplanes == 2)
		vpe_dbg(ctx->dev, " and %d\n", sizes[VPE_CHROMA]);

	return 0;
}

static int vpe_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpe_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vpe_q_data *q_data;
	struct v4l2_pix_format_mplane *pix;
	int i;

	vpe_dbg(ctx->dev, "type: %d\n", vb->vb2_queue->type);

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	if (!q_data)
		return -EINVAL;

	pix = &q_data->format.fmt.pix_mp;

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (!(q_data->flags & Q_IS_INTERLACED)) {
			vbuf->field = V4L2_FIELD_NONE;
		} else {
			if (vbuf->field != V4L2_FIELD_TOP &&
			    vbuf->field != V4L2_FIELD_BOTTOM &&
			    vbuf->field != V4L2_FIELD_SEQ_TB &&
			    vbuf->field != V4L2_FIELD_SEQ_BT)
				return -EINVAL;
		}
	}

	for (i = 0; i < pix->num_planes; i++) {
		if (vb2_plane_size(vb, i) < pix->plane_fmt[i].sizeimage) {
			vpe_err(ctx->dev,
				"data will not fit into plane (%lu < %lu)\n",
				vb2_plane_size(vb, i),
				(long)pix->plane_fmt[i].sizeimage);
			return -EINVAL;
		}
	}

	for (i = 0; i < pix->num_planes; i++)
		vb2_set_plane_payload(vb, i, pix->plane_fmt[i].sizeimage);

	return 0;
}

static void vpe_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpe_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int check_srcdst_sizes(struct vpe_ctx *ctx)
{
	struct vpe_q_data *s_q_data =  &ctx->q_data[Q_DATA_SRC];
	struct vpe_q_data *d_q_data =  &ctx->q_data[Q_DATA_DST];
	unsigned int src_w = s_q_data->c_rect.width;
	unsigned int src_h = s_q_data->c_rect.height;
	unsigned int dst_w = d_q_data->c_rect.width;
	unsigned int dst_h = d_q_data->c_rect.height;

	if (src_w == dst_w && src_h == dst_h)
		return 0;

	if (src_h <= SC_MAX_PIXEL_HEIGHT &&
	    src_w <= SC_MAX_PIXEL_WIDTH &&
	    dst_h <= SC_MAX_PIXEL_HEIGHT &&
	    dst_w <= SC_MAX_PIXEL_WIDTH)
		return 0;

	return -1;
}

static void vpe_return_all_buffers(struct vpe_ctx *ctx,  struct vb2_queue *q,
				   enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *vb;
	unsigned long flags;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vb = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vb)
			break;
		spin_lock_irqsave(&ctx->dev->lock, flags);
		v4l2_m2m_buf_done(vb, state);
		spin_unlock_irqrestore(&ctx->dev->lock, flags);
	}

	/*
	 * Cleanup the in-transit vb2 buffers that have been
	 * removed from their respective queue already but for
	 * which procecessing has not been completed yet.
	 */
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		spin_lock_irqsave(&ctx->dev->lock, flags);

		if (ctx->src_vbs[2])
			v4l2_m2m_buf_done(ctx->src_vbs[2], state);

		if (ctx->src_vbs[1] && (ctx->src_vbs[1] != ctx->src_vbs[2]))
			v4l2_m2m_buf_done(ctx->src_vbs[1], state);

		if (ctx->src_vbs[0] &&
		    (ctx->src_vbs[0] != ctx->src_vbs[1]) &&
		    (ctx->src_vbs[0] != ctx->src_vbs[2]))
			v4l2_m2m_buf_done(ctx->src_vbs[0], state);

		ctx->src_vbs[2] = NULL;
		ctx->src_vbs[1] = NULL;
		ctx->src_vbs[0] = NULL;

		spin_unlock_irqrestore(&ctx->dev->lock, flags);
	} else {
		if (ctx->dst_vb) {
			spin_lock_irqsave(&ctx->dev->lock, flags);

			v4l2_m2m_buf_done(ctx->dst_vb, state);
			ctx->dst_vb = NULL;
			spin_unlock_irqrestore(&ctx->dev->lock, flags);
		}
	}
}

static int vpe_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpe_ctx *ctx = vb2_get_drv_priv(q);

	/* Check any of the size exceed maximum scaling sizes */
	if (check_srcdst_sizes(ctx)) {
		vpe_err(ctx->dev,
			"Conversion setup failed, check source and destination parameters\n"
			);
		vpe_return_all_buffers(ctx, q, VB2_BUF_STATE_QUEUED);
		return -EINVAL;
	}

	if (ctx->deinterlacing)
		config_edi_input_mode(ctx, 0x0);

	if (ctx->sequence != 0)
		set_srcdst_params(ctx);

	return 0;
}

static void vpe_stop_streaming(struct vb2_queue *q)
{
	struct vpe_ctx *ctx = vb2_get_drv_priv(q);

	vpe_dump_regs(ctx->dev);
	vpdma_dump_regs(ctx->dev->vpdma);

	vpe_return_all_buffers(ctx, q, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops vpe_qops = {
	.queue_setup	 = vpe_queue_setup,
	.buf_prepare	 = vpe_buf_prepare,
	.buf_queue	 = vpe_buf_queue,
	.start_streaming = vpe_start_streaming,
	.stop_streaming  = vpe_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct vpe_ctx *ctx = priv;
	struct vpe_dev *dev = ctx->dev;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &vpe_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &dev->dev_mutex;
	src_vq->dev = dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &vpe_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &dev->dev_mutex;
	dst_vq->dev = dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static const struct v4l2_ctrl_config vpe_bufs_per_job = {
	.ops = &vpe_ctrl_ops,
	.id = V4L2_CID_VPE_BUFS_PER_JOB,
	.name = "Buffers Per Transaction",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = VPE_DEF_BUFS_PER_JOB,
	.min = 1,
	.max = VIDEO_MAX_FRAME,
	.step = 1,
};

/*
 * File operations
 */
static int vpe_open(struct file *file)
{
	struct vpe_dev *dev = video_drvdata(file);
	struct vpe_q_data *s_q_data;
	struct v4l2_ctrl_handler *hdl;
	struct vpe_ctx *ctx;
	struct v4l2_pix_format_mplane *pix;
	int ret;

	vpe_dbg(dev, "vpe_open\n");

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	if (mutex_lock_interruptible(&dev->dev_mutex)) {
		ret = -ERESTARTSYS;
		goto free_ctx;
	}

	ret = vpdma_create_desc_list(&ctx->desc_list, VPE_DESC_LIST_SIZE,
			VPDMA_LIST_TYPE_NORMAL);
	if (ret != 0)
		goto unlock;

	ret = vpdma_alloc_desc_buf(&ctx->mmr_adb, sizeof(struct vpe_mmr_adb));
	if (ret != 0)
		goto free_desc_list;

	ret = vpdma_alloc_desc_buf(&ctx->sc_coeff_h, SC_COEF_SRAM_SIZE);
	if (ret != 0)
		goto free_mmr_adb;

	ret = vpdma_alloc_desc_buf(&ctx->sc_coeff_v, SC_COEF_SRAM_SIZE);
	if (ret != 0)
		goto free_sc_h;

	init_adb_hdrs(ctx);

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = ctx;

	hdl = &ctx->hdl;
	v4l2_ctrl_handler_init(hdl, 1);
	v4l2_ctrl_new_custom(hdl, &vpe_bufs_per_job, NULL);
	if (hdl->error) {
		ret = hdl->error;
		goto exit_fh;
	}
	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	s_q_data = &ctx->q_data[Q_DATA_SRC];
	pix = &s_q_data->format.fmt.pix_mp;
	s_q_data->fmt = __find_format(V4L2_PIX_FMT_YUYV);
	pix->pixelformat = s_q_data->fmt->fourcc;
	s_q_data->format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	pix->width = 1920;
	pix->height = 1080;
	pix->num_planes = 1;
	pix->plane_fmt[VPE_LUMA].bytesperline = (pix->width *
			s_q_data->fmt->vpdma_fmt[VPE_LUMA]->depth) >> 3;
	pix->plane_fmt[VPE_LUMA].sizeimage =
			pix->plane_fmt[VPE_LUMA].bytesperline *
			pix->height;
	pix->colorspace = V4L2_COLORSPACE_REC709;
	pix->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	pix->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix->field = V4L2_FIELD_NONE;
	s_q_data->c_rect.left = 0;
	s_q_data->c_rect.top = 0;
	s_q_data->c_rect.width = pix->width;
	s_q_data->c_rect.height = pix->height;
	s_q_data->flags = 0;

	ctx->q_data[Q_DATA_DST] = *s_q_data;
	ctx->q_data[Q_DATA_DST].format.type =
			V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	set_dei_shadow_registers(ctx);
	set_src_registers(ctx);
	set_dst_registers(ctx);
	ret = set_srcdst_params(ctx);
	if (ret)
		goto exit_fh;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto exit_fh;
	}

	v4l2_fh_add(&ctx->fh);

	/*
	 * for now, just report the creation of the first instance, we can later
	 * optimize the driver to enable or disable clocks when the first
	 * instance is created or the last instance released
	 */
	if (atomic_inc_return(&dev->num_instances) == 1)
		vpe_dbg(dev, "first instance created\n");

	ctx->bufs_per_job = VPE_DEF_BUFS_PER_JOB;

	ctx->load_mmrs = true;

	vpe_dbg(dev, "created instance %p, m2m_ctx: %p\n",
		ctx, ctx->fh.m2m_ctx);

	mutex_unlock(&dev->dev_mutex);

	return 0;
exit_fh:
	v4l2_ctrl_handler_free(hdl);
	v4l2_fh_exit(&ctx->fh);
	vpdma_free_desc_buf(&ctx->sc_coeff_v);
free_sc_h:
	vpdma_free_desc_buf(&ctx->sc_coeff_h);
free_mmr_adb:
	vpdma_free_desc_buf(&ctx->mmr_adb);
free_desc_list:
	vpdma_free_desc_list(&ctx->desc_list);
unlock:
	mutex_unlock(&dev->dev_mutex);
free_ctx:
	kfree(ctx);
	return ret;
}

static int vpe_release(struct file *file)
{
	struct vpe_dev *dev = video_drvdata(file);
	struct vpe_ctx *ctx = file->private_data;

	vpe_dbg(dev, "releasing instance %p\n", ctx);

	mutex_lock(&dev->dev_mutex);
	free_mv_buffers(ctx);

	vpdma_unmap_desc_buf(dev->vpdma, &ctx->desc_list.buf);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->mmr_adb);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->sc_coeff_h);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->sc_coeff_v);

	vpdma_free_desc_list(&ctx->desc_list);
	vpdma_free_desc_buf(&ctx->mmr_adb);

	vpdma_free_desc_buf(&ctx->sc_coeff_v);
	vpdma_free_desc_buf(&ctx->sc_coeff_h);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	kfree(ctx);

	/*
	 * for now, just report the release of the last instance, we can later
	 * optimize the driver to enable or disable clocks when the first
	 * instance is created or the last instance released
	 */
	if (atomic_dec_return(&dev->num_instances) == 0)
		vpe_dbg(dev, "last instance released\n");

	mutex_unlock(&dev->dev_mutex);

	return 0;
}

static const struct v4l2_file_operations vpe_fops = {
	.owner		= THIS_MODULE,
	.open		= vpe_open,
	.release	= vpe_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device vpe_videodev = {
	.name		= VPE_MODULE_NAME,
	.fops		= &vpe_fops,
	.ioctl_ops	= &vpe_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.vfl_dir	= VFL_DIR_M2M,
	.device_caps	= V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING,
};

static const struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_ready	= job_ready,
	.job_abort	= job_abort,
};

static int vpe_runtime_get(struct platform_device *pdev)
{
	int r;

	dev_dbg(&pdev->dev, "vpe_runtime_get\n");

	r = pm_runtime_resume_and_get(&pdev->dev);
	WARN_ON(r < 0);
	return r;
}

static void vpe_runtime_put(struct platform_device *pdev)
{

	int r;

	dev_dbg(&pdev->dev, "vpe_runtime_put\n");

	r = pm_runtime_put_sync(&pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static void vpe_fw_cb(struct platform_device *pdev)
{
	struct vpe_dev *dev = platform_get_drvdata(pdev);
	struct video_device *vfd;
	int ret;

	vfd = &dev->vfd;
	*vfd = vpe_videodev;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		vpe_err(dev, "Failed to register video device\n");

		vpe_set_clock_enable(dev, 0);
		vpe_runtime_put(pdev);
		pm_runtime_disable(&pdev->dev);
		v4l2_m2m_release(dev->m2m_dev);
		v4l2_device_unregister(&dev->v4l2_dev);

		return;
	}

	video_set_drvdata(vfd, dev);
	dev_info(dev->v4l2_dev.dev, "Device registered as /dev/video%d\n",
		vfd->num);
}

static int vpe_probe(struct platform_device *pdev)
{
	struct vpe_dev *dev;
	int ret, irq, func;

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev,
			"32-bit consistent DMA enable failed\n");
		return ret;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	atomic_set(&dev->num_instances, 0);
	mutex_init(&dev->dev_mutex);

	dev->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"vpe_top");
	if (!dev->res) {
		dev_err(&pdev->dev, "missing 'vpe_top' resources data\n");
		return -ENODEV;
	}

	/*
	 * HACK: we get resource info from device tree in the form of a list of
	 * VPE sub blocks, the driver currently uses only the base of vpe_top
	 * for register access, the driver should be changed later to access
	 * registers based on the sub block base addresses
	 */
	dev->base = devm_ioremap(&pdev->dev, dev->res->start, SZ_32K);
	if (!dev->base) {
		ret = -ENOMEM;
		goto v4l2_dev_unreg;
	}

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, vpe_irq, 0, VPE_MODULE_NAME,
			dev);
	if (ret)
		goto v4l2_dev_unreg;

	platform_set_drvdata(pdev, dev);

	dev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		vpe_err(dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto v4l2_dev_unreg;
	}

	pm_runtime_enable(&pdev->dev);

	ret = vpe_runtime_get(pdev);
	if (ret < 0)
		goto rel_m2m;

	/* Perform clk enable followed by reset */
	vpe_set_clock_enable(dev, 1);

	vpe_top_reset(dev);

	func = read_field_reg(dev, VPE_PID, VPE_PID_FUNC_MASK,
		VPE_PID_FUNC_SHIFT);
	vpe_dbg(dev, "VPE PID function %x\n", func);

	vpe_top_vpdma_reset(dev);

	dev->sc = sc_create(pdev, "sc");
	if (IS_ERR(dev->sc)) {
		ret = PTR_ERR(dev->sc);
		goto runtime_put;
	}

	dev->csc = csc_create(pdev, "csc");
	if (IS_ERR(dev->csc)) {
		ret = PTR_ERR(dev->csc);
		goto runtime_put;
	}

	dev->vpdma = &dev->vpdma_data;
	ret = vpdma_create(pdev, dev->vpdma, vpe_fw_cb);
	if (ret)
		goto runtime_put;

	return 0;

runtime_put:
	vpe_runtime_put(pdev);
rel_m2m:
	pm_runtime_disable(&pdev->dev);
	v4l2_m2m_release(dev->m2m_dev);
v4l2_dev_unreg:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static void vpe_remove(struct platform_device *pdev)
{
	struct vpe_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " VPE_MODULE_NAME);

	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(&dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);

	vpe_set_clock_enable(dev, 0);
	vpe_runtime_put(pdev);
	pm_runtime_disable(&pdev->dev);
}

#if defined(CONFIG_OF)
static const struct of_device_id vpe_of_match[] = {
	{
		.compatible = "ti,dra7-vpe",
	},
	{},
};
MODULE_DEVICE_TABLE(of, vpe_of_match);
#endif

static struct platform_driver vpe_pdrv = {
	.probe		= vpe_probe,
	.remove		= vpe_remove,
	.driver		= {
		.name	= VPE_MODULE_NAME,
		.of_match_table = of_match_ptr(vpe_of_match),
	},
};

module_platform_driver(vpe_pdrv);

MODULE_DESCRIPTION("TI VPE driver");
MODULE_AUTHOR("Dale Farnsworth, <dale@farnsworth.org>");
MODULE_LICENSE("GPL");
