/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/delay.h>

#include "bdisp.h"
#include "bdisp-filter.h"
#include "bdisp-reg.h"

/* Max width of the source frame in a single node */
#define MAX_SRC_WIDTH           2048

/* Reset & boot poll config */
#define POLL_RST_MAX            50
#define POLL_RST_DELAY_MS       20

enum bdisp_target_plan {
	BDISP_RGB,
	BDISP_Y,
	BDISP_CBCR
};

struct bdisp_op_cfg {
	bool cconv;          /* RGB - YUV conversion */
	bool hflip;          /* Horizontal flip */
	bool vflip;          /* Vertical flip */
	bool wide;           /* Wide (>MAX_SRC_WIDTH) */
	bool scale;          /* Scale */
	u16  h_inc;          /* Horizontal increment in 6.10 format */
	u16  v_inc;          /* Vertical increment in 6.10 format */
	bool src_interlaced; /* is the src an interlaced buffer */
	u8   src_nbp;        /* nb of planes of the src */
	bool src_yuv;        /* is the src a YUV color format */
	bool src_420;        /* is the src 4:2:0 chroma subsampled */
	u8   dst_nbp;        /* nb of planes of the dst */
	bool dst_yuv;        /* is the dst a YUV color format */
	bool dst_420;        /* is the dst 4:2:0 chroma subsampled */
};

struct bdisp_filter_addr {
	u16 min;             /* Filter min scale factor (6.10 fixed point) */
	u16 max;             /* Filter max scale factor (6.10 fixed point) */
	void *virt;          /* Virtual address for filter table */
	dma_addr_t paddr;    /* Physical address for filter table */
};

static struct bdisp_filter_addr bdisp_h_filter[NB_H_FILTER];
static struct bdisp_filter_addr bdisp_v_filter[NB_V_FILTER];

/**
 * bdisp_hw_reset
 * @bdisp:      bdisp entity
 *
 * Resets HW
 *
 * RETURNS:
 * 0 on success.
 */
int bdisp_hw_reset(struct bdisp_dev *bdisp)
{
	unsigned int i;

	dev_dbg(bdisp->dev, "%s\n", __func__);

	/* Mask Interrupt */
	writel(0, bdisp->regs + BLT_ITM0);

	/* Reset */
	writel(readl(bdisp->regs + BLT_CTL) | BLT_CTL_RESET,
	       bdisp->regs + BLT_CTL);
	writel(0, bdisp->regs + BLT_CTL);

	/* Wait for reset done */
	for (i = 0; i < POLL_RST_MAX; i++) {
		if (readl(bdisp->regs + BLT_STA1) & BLT_STA1_IDLE)
			break;
		msleep(POLL_RST_DELAY_MS);
	}
	if (i == POLL_RST_MAX)
		dev_err(bdisp->dev, "Reset timeout\n");

	return (i == POLL_RST_MAX) ? -EAGAIN : 0;
}

/**
 * bdisp_hw_get_and_clear_irq
 * @bdisp:      bdisp entity
 *
 * Read then reset interrupt status
 *
 * RETURNS:
 * 0 if expected interrupt was raised.
 */
int bdisp_hw_get_and_clear_irq(struct bdisp_dev *bdisp)
{
	u32 its;

	its = readl(bdisp->regs + BLT_ITS);

	/* Check for the only expected IT: LastNode of AQ1 */
	if (!(its & BLT_ITS_AQ1_LNA)) {
		dev_dbg(bdisp->dev, "Unexpected IT status: 0x%08X\n", its);
		writel(its, bdisp->regs + BLT_ITS);
		return -1;
	}

	/* Clear and mask */
	writel(its, bdisp->regs + BLT_ITS);
	writel(0, bdisp->regs + BLT_ITM0);

	return 0;
}

/**
 * bdisp_hw_free_nodes
 * @ctx:        bdisp context
 *
 * Free node memory
 *
 * RETURNS:
 * None
 */
void bdisp_hw_free_nodes(struct bdisp_ctx *ctx)
{
	if (ctx && ctx->node[0]) {
		DEFINE_DMA_ATTRS(attrs);

		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
		dma_free_attrs(ctx->bdisp_dev->dev,
			       sizeof(struct bdisp_node) * MAX_NB_NODE,
			       ctx->node[0], ctx->node_paddr[0], &attrs);
	}
}

/**
 * bdisp_hw_alloc_nodes
 * @ctx:        bdisp context
 *
 * Allocate dma memory for nodes
 *
 * RETURNS:
 * 0 on success
 */
int bdisp_hw_alloc_nodes(struct bdisp_ctx *ctx)
{
	struct device *dev = ctx->bdisp_dev->dev;
	unsigned int i, node_size = sizeof(struct bdisp_node);
	void *base;
	dma_addr_t paddr;
	DEFINE_DMA_ATTRS(attrs);

	/* Allocate all the nodes within a single memory page */
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
	base = dma_alloc_attrs(dev, node_size * MAX_NB_NODE, &paddr,
			       GFP_KERNEL | GFP_DMA, &attrs);
	if (!base) {
		dev_err(dev, "%s no mem\n", __func__);
		return -ENOMEM;
	}

	memset(base, 0, node_size * MAX_NB_NODE);

	for (i = 0; i < MAX_NB_NODE; i++) {
		ctx->node[i] = base;
		ctx->node_paddr[i] = paddr;
		dev_dbg(dev, "node[%d]=0x%p (paddr=%pad)\n", i, ctx->node[i],
			&paddr);
		base += node_size;
		paddr += node_size;
	}

	return 0;
}

/**
 * bdisp_hw_free_filters
 * @dev:        device
 *
 * Free filters memory
 *
 * RETURNS:
 * None
 */
void bdisp_hw_free_filters(struct device *dev)
{
	int size = (BDISP_HF_NB * NB_H_FILTER) + (BDISP_VF_NB * NB_V_FILTER);

	if (bdisp_h_filter[0].virt) {
		DEFINE_DMA_ATTRS(attrs);

		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
		dma_free_attrs(dev, size, bdisp_h_filter[0].virt,
			       bdisp_h_filter[0].paddr, &attrs);
	}
}

/**
 * bdisp_hw_alloc_filters
 * @dev:        device
 *
 * Allocate dma memory for filters
 *
 * RETURNS:
 * 0 on success
 */
int bdisp_hw_alloc_filters(struct device *dev)
{
	unsigned int i, size;
	void *base;
	dma_addr_t paddr;
	DEFINE_DMA_ATTRS(attrs);

	/* Allocate all the filters within a single memory page */
	size = (BDISP_HF_NB * NB_H_FILTER) + (BDISP_VF_NB * NB_V_FILTER);
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
	base = dma_alloc_attrs(dev, size, &paddr, GFP_KERNEL | GFP_DMA, &attrs);
	if (!base)
		return -ENOMEM;

	/* Setup filter addresses */
	for (i = 0; i < NB_H_FILTER; i++) {
		bdisp_h_filter[i].min = bdisp_h_spec[i].min;
		bdisp_h_filter[i].max = bdisp_h_spec[i].max;
		memcpy(base, bdisp_h_spec[i].coef, BDISP_HF_NB);
		bdisp_h_filter[i].virt = base;
		bdisp_h_filter[i].paddr = paddr;
		base += BDISP_HF_NB;
		paddr += BDISP_HF_NB;
	}

	for (i = 0; i < NB_V_FILTER; i++) {
		bdisp_v_filter[i].min = bdisp_v_spec[i].min;
		bdisp_v_filter[i].max = bdisp_v_spec[i].max;
		memcpy(base, bdisp_v_spec[i].coef, BDISP_VF_NB);
		bdisp_v_filter[i].virt = base;
		bdisp_v_filter[i].paddr = paddr;
		base += BDISP_VF_NB;
		paddr += BDISP_VF_NB;
	}

	return 0;
}

/**
 * bdisp_hw_get_hf_addr
 * @inc:        resize increment
 *
 * Find the horizontal filter table that fits the resize increment
 *
 * RETURNS:
 * table physical address
 */
static dma_addr_t bdisp_hw_get_hf_addr(u16 inc)
{
	unsigned int i;

	for (i = NB_H_FILTER - 1; i > 0; i--)
		if ((bdisp_h_filter[i].min < inc) &&
		    (inc <= bdisp_h_filter[i].max))
			break;

	return bdisp_h_filter[i].paddr;
}

/**
 * bdisp_hw_get_vf_addr
 * @inc:        resize increment
 *
 * Find the vertical filter table that fits the resize increment
 *
 * RETURNS:
 * table physical address
 */
static dma_addr_t bdisp_hw_get_vf_addr(u16 inc)
{
	unsigned int i;

	for (i = NB_V_FILTER - 1; i > 0; i--)
		if ((bdisp_v_filter[i].min < inc) &&
		    (inc <= bdisp_v_filter[i].max))
			break;

	return bdisp_v_filter[i].paddr;
}

/**
 * bdisp_hw_get_inc
 * @from:       input size
 * @to:         output size
 * @inc:        resize increment in 6.10 format
 *
 * Computes the increment (inverse of scale) in 6.10 format
 *
 * RETURNS:
 * 0 on success
 */
static int bdisp_hw_get_inc(u32 from, u32 to, u16 *inc)
{
	u32 tmp;

	if (!to)
		return -EINVAL;

	if (to == from) {
		*inc = 1 << 10;
		return 0;
	}

	tmp = (from << 10) / to;
	if ((tmp > 0xFFFF) || (!tmp))
		/* overflow (downscale x 63) or too small (upscale x 1024) */
		return -EINVAL;

	*inc = (u16)tmp;

	return 0;
}

/**
 * bdisp_hw_get_hv_inc
 * @ctx:        device context
 * @h_inc:      horizontal increment
 * @v_inc:      vertical increment
 *
 * Computes the horizontal & vertical increments (inverse of scale)
 *
 * RETURNS:
 * 0 on success
 */
static int bdisp_hw_get_hv_inc(struct bdisp_ctx *ctx, u16 *h_inc, u16 *v_inc)
{
	u32 src_w, src_h, dst_w, dst_h;

	src_w = ctx->src.crop.width;
	src_h = ctx->src.crop.height;
	dst_w = ctx->dst.width;
	dst_h = ctx->dst.height;

	if (bdisp_hw_get_inc(src_w, dst_w, h_inc) ||
	    bdisp_hw_get_inc(src_h, dst_h, v_inc)) {
		dev_err(ctx->bdisp_dev->dev,
			"scale factors failed (%dx%d)->(%dx%d)\n",
			src_w, src_h, dst_w, dst_h);
		return -EINVAL;
	}

	return 0;
}

/**
 * bdisp_hw_get_op_cfg
 * @ctx:        device context
 * @c:          operation configuration
 *
 * Check which blitter operations are expected and sets the scaling increments
 *
 * RETURNS:
 * 0 on success
 */
static int bdisp_hw_get_op_cfg(struct bdisp_ctx *ctx, struct bdisp_op_cfg *c)
{
	struct device *dev = ctx->bdisp_dev->dev;
	struct bdisp_frame *src = &ctx->src;
	struct bdisp_frame *dst = &ctx->dst;

	if (src->width > MAX_SRC_WIDTH * MAX_VERTICAL_STRIDES) {
		dev_err(dev, "Image width out of HW caps\n");
		return -EINVAL;
	}

	c->wide = src->width > MAX_SRC_WIDTH;

	c->hflip = ctx->hflip;
	c->vflip = ctx->vflip;

	c->src_interlaced = (src->field == V4L2_FIELD_INTERLACED);

	c->src_nbp = src->fmt->nb_planes;
	c->src_yuv = (src->fmt->pixelformat == V4L2_PIX_FMT_NV12) ||
			(src->fmt->pixelformat == V4L2_PIX_FMT_YUV420);
	c->src_420 = c->src_yuv;

	c->dst_nbp = dst->fmt->nb_planes;
	c->dst_yuv = (dst->fmt->pixelformat == V4L2_PIX_FMT_NV12) ||
			(dst->fmt->pixelformat == V4L2_PIX_FMT_YUV420);
	c->dst_420 = c->dst_yuv;

	c->cconv = (c->src_yuv != c->dst_yuv);

	if (bdisp_hw_get_hv_inc(ctx, &c->h_inc, &c->v_inc)) {
		dev_err(dev, "Scale factor out of HW caps\n");
		return -EINVAL;
	}

	/* Deinterlacing adjustment : stretch a field to a frame */
	if (c->src_interlaced)
		c->v_inc /= 2;

	if ((c->h_inc != (1 << 10)) || (c->v_inc != (1 << 10)))
		c->scale = true;
	else
		c->scale = false;

	return 0;
}

/**
 * bdisp_hw_color_format
 * @pixelformat: v4l2 pixel format
 *
 * v4l2 to bdisp pixel format convert
 *
 * RETURNS:
 * bdisp pixel format
 */
static u32 bdisp_hw_color_format(u32 pixelformat)
{
	u32 ret;

	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:
		ret = (BDISP_YUV_3B << BLT_TTY_COL_SHIFT);
		break;
	case V4L2_PIX_FMT_NV12:
		ret = (BDISP_NV12 << BLT_TTY_COL_SHIFT) | BLT_TTY_BIG_END;
		break;
	case V4L2_PIX_FMT_RGB565:
		ret = (BDISP_RGB565 << BLT_TTY_COL_SHIFT);
		break;
	case V4L2_PIX_FMT_XBGR32: /* This V4L format actually refers to xRGB */
		ret = (BDISP_XRGB8888 << BLT_TTY_COL_SHIFT);
		break;
	case V4L2_PIX_FMT_RGB24:  /* RGB888 format */
		ret = (BDISP_RGB888 << BLT_TTY_COL_SHIFT) | BLT_TTY_BIG_END;
		break;
	case V4L2_PIX_FMT_ABGR32: /* This V4L format actually refers to ARGB */

	default:
		ret = (BDISP_ARGB8888 << BLT_TTY_COL_SHIFT) | BLT_TTY_ALPHA_R;
		break;
	}

	return ret;
}

/**
 * bdisp_hw_build_node
 * @ctx:        device context
 * @cfg:        operation configuration
 * @node:       node to be set
 * @t_plan:     whether the node refers to a RGB/Y or a CbCr plane
 * @src_x_offset: x offset in the source image
 *
 * Build a node
 *
 * RETURNS:
 * None
 */
static void bdisp_hw_build_node(struct bdisp_ctx *ctx,
				struct bdisp_op_cfg *cfg,
				struct bdisp_node *node,
				enum bdisp_target_plan t_plan, int src_x_offset)
{
	struct bdisp_frame *src = &ctx->src;
	struct bdisp_frame *dst = &ctx->dst;
	u16 h_inc, v_inc, yh_inc, yv_inc;
	struct v4l2_rect src_rect = src->crop;
	struct v4l2_rect dst_rect = dst->crop;
	int dst_x_offset;
	s32 dst_width = dst->crop.width;
	u32 src_fmt, dst_fmt;
	const u32 *ivmx;

	dev_dbg(ctx->bdisp_dev->dev, "%s\n", __func__);

	memset(node, 0, sizeof(*node));

	/* Adjust src and dst areas wrt src_x_offset */
	src_rect.left += src_x_offset;
	src_rect.width -= src_x_offset;
	src_rect.width = min_t(__s32, MAX_SRC_WIDTH, src_rect.width);

	dst_x_offset = (src_x_offset * dst->width) / ctx->src.crop.width;
	dst_rect.left += dst_x_offset;
	dst_rect.width = (src_rect.width * dst->width) / ctx->src.crop.width;

	/* General */
	src_fmt = src->fmt->pixelformat;
	dst_fmt = dst->fmt->pixelformat;

	node->nip = 0;
	node->cic = BLT_CIC_ALL_GRP;
	node->ack = BLT_ACK_BYPASS_S2S3;

	switch (cfg->src_nbp) {
	case 1:
		/* Src2 = RGB / Src1 = Src3 = off */
		node->ins = BLT_INS_S1_OFF | BLT_INS_S2_MEM | BLT_INS_S3_OFF;
		break;
	case 2:
		/* Src3 = Y
		 * Src2 = CbCr or ColorFill if writing the Y plane
		 * Src1 = off */
		node->ins = BLT_INS_S1_OFF | BLT_INS_S3_MEM;
		if (t_plan == BDISP_Y)
			node->ins |= BLT_INS_S2_CF;
		else
			node->ins |= BLT_INS_S2_MEM;
		break;
	case 3:
	default:
		/* Src3 = Y
		 * Src2 = Cb or ColorFill if writing the Y plane
		 * Src1 = Cr or ColorFill if writing the Y plane */
		node->ins = BLT_INS_S3_MEM;
		if (t_plan == BDISP_Y)
			node->ins |= BLT_INS_S2_CF | BLT_INS_S1_CF;
		else
			node->ins |= BLT_INS_S2_MEM | BLT_INS_S1_MEM;
		break;
	}

	/* Color convert */
	node->ins |= cfg->cconv ? BLT_INS_IVMX : 0;
	/* Scale needed if scaling OR 4:2:0 up/downsampling */
	node->ins |= (cfg->scale || cfg->src_420 || cfg->dst_420) ?
			BLT_INS_SCALE : 0;

	/* Target */
	node->tba = (t_plan == BDISP_CBCR) ? dst->paddr[1] : dst->paddr[0];

	node->tty = dst->bytesperline;
	node->tty |= bdisp_hw_color_format(dst_fmt);
	node->tty |= BLT_TTY_DITHER;
	node->tty |= (t_plan == BDISP_CBCR) ? BLT_TTY_CHROMA : 0;
	node->tty |= cfg->hflip ? BLT_TTY_HSO : 0;
	node->tty |= cfg->vflip ? BLT_TTY_VSO : 0;

	if (cfg->dst_420 && (t_plan == BDISP_CBCR)) {
		/* 420 chroma downsampling */
		dst_rect.height /= 2;
		dst_rect.width /= 2;
		dst_rect.left /= 2;
		dst_rect.top /= 2;
		dst_x_offset /= 2;
		dst_width /= 2;
	}

	node->txy = cfg->vflip ? (dst_rect.height - 1) : dst_rect.top;
	node->txy <<= 16;
	node->txy |= cfg->hflip ? (dst_width - dst_x_offset - 1) :
			dst_rect.left;

	node->tsz = dst_rect.height << 16 | dst_rect.width;

	if (cfg->src_interlaced) {
		/* handle only the top field which is half height of a frame */
		src_rect.top /= 2;
		src_rect.height /= 2;
	}

	if (cfg->src_nbp == 1) {
		/* Src 2 : RGB */
		node->s2ba = src->paddr[0];

		node->s2ty = src->bytesperline;
		if (cfg->src_interlaced)
			node->s2ty *= 2;

		node->s2ty |= bdisp_hw_color_format(src_fmt);

		node->s2xy = src_rect.top << 16 | src_rect.left;
		node->s2sz = src_rect.height << 16 | src_rect.width;
	} else {
		/* Src 2 : Cb or CbCr */
		if (cfg->src_420) {
			/* 420 chroma upsampling */
			src_rect.top /= 2;
			src_rect.left /= 2;
			src_rect.width /= 2;
			src_rect.height /= 2;
		}

		node->s2ba = src->paddr[1];

		node->s2ty = src->bytesperline;
		if (cfg->src_nbp == 3)
			node->s2ty /= 2;
		if (cfg->src_interlaced)
			node->s2ty *= 2;

		node->s2ty |= bdisp_hw_color_format(src_fmt);

		node->s2xy = src_rect.top << 16 | src_rect.left;
		node->s2sz = src_rect.height << 16 | src_rect.width;

		if (cfg->src_nbp == 3) {
			/* Src 1 : Cr */
			node->s1ba = src->paddr[2];

			node->s1ty = node->s2ty;
			node->s1xy = node->s2xy;
		}

		/* Src 3 : Y */
		node->s3ba = src->paddr[0];

		node->s3ty = src->bytesperline;
		if (cfg->src_interlaced)
			node->s3ty *= 2;
		node->s3ty |= bdisp_hw_color_format(src_fmt);

		if ((t_plan != BDISP_CBCR) && cfg->src_420) {
			/* No chroma upsampling for output RGB / Y plane */
			node->s3xy = node->s2xy * 2;
			node->s3sz = node->s2sz * 2;
		} else {
			/* No need to read Y (Src3) when writing Chroma */
			node->s3ty |= BLT_S3TY_BLANK_ACC;
			node->s3xy = node->s2xy;
			node->s3sz = node->s2sz;
		}
	}

	/* Resize (scale OR 4:2:0: chroma up/downsampling) */
	if (node->ins & BLT_INS_SCALE) {
		/* no need to compute Y when writing CbCr from RGB input */
		bool skip_y = (t_plan == BDISP_CBCR) && !cfg->src_yuv;

		/* FCTL */
		if (cfg->scale) {
			node->fctl = BLT_FCTL_HV_SCALE;
			if (!skip_y)
				node->fctl |= BLT_FCTL_Y_HV_SCALE;
		} else {
			node->fctl = BLT_FCTL_HV_SAMPLE;
			if (!skip_y)
				node->fctl |= BLT_FCTL_Y_HV_SAMPLE;
		}

		/* RSF - Chroma may need to be up/downsampled */
		h_inc = cfg->h_inc;
		v_inc = cfg->v_inc;
		if (!cfg->src_420 && cfg->dst_420 && (t_plan == BDISP_CBCR)) {
			/* RGB to 4:2:0 for Chroma: downsample */
			h_inc *= 2;
			v_inc *= 2;
		} else if (cfg->src_420 && !cfg->dst_420) {
			/* 4:2:0: to RGB: upsample*/
			h_inc /= 2;
			v_inc /= 2;
		}
		node->rsf = v_inc << 16 | h_inc;

		/* RZI */
		node->rzi = BLT_RZI_DEFAULT;

		/* Filter table physical addr */
		node->hfp = bdisp_hw_get_hf_addr(h_inc);
		node->vfp = bdisp_hw_get_vf_addr(v_inc);

		/* Y version */
		if (!skip_y) {
			yh_inc = cfg->h_inc;
			yv_inc = cfg->v_inc;

			node->y_rsf = yv_inc << 16 | yh_inc;
			node->y_rzi = BLT_RZI_DEFAULT;
			node->y_hfp = bdisp_hw_get_hf_addr(yh_inc);
			node->y_vfp = bdisp_hw_get_vf_addr(yv_inc);
		}
	}

	/* Versatile matrix for RGB / YUV conversion */
	if (cfg->cconv) {
		ivmx = cfg->src_yuv ? bdisp_yuv_to_rgb : bdisp_rgb_to_yuv;

		node->ivmx0 = ivmx[0];
		node->ivmx1 = ivmx[1];
		node->ivmx2 = ivmx[2];
		node->ivmx3 = ivmx[3];
	}
}

/**
 * bdisp_hw_build_all_nodes
 * @ctx:        device context
 *
 * Build all the nodes for the blitter operation
 *
 * RETURNS:
 * 0 on success
 */
static int bdisp_hw_build_all_nodes(struct bdisp_ctx *ctx)
{
	struct bdisp_op_cfg cfg;
	unsigned int i, nid = 0;
	int src_x_offset = 0;

	for (i = 0; i < MAX_NB_NODE; i++)
		if (!ctx->node[i]) {
			dev_err(ctx->bdisp_dev->dev, "node %d is null\n", i);
			return -EINVAL;
		}

	/* Get configuration (scale, flip, ...) */
	if (bdisp_hw_get_op_cfg(ctx, &cfg))
		return -EINVAL;

	/* Split source in vertical strides (HW constraint) */
	for (i = 0; i < MAX_VERTICAL_STRIDES; i++) {
		/* Build RGB/Y node and link it to the previous node */
		bdisp_hw_build_node(ctx, &cfg, ctx->node[nid],
				    cfg.dst_nbp == 1 ? BDISP_RGB : BDISP_Y,
				    src_x_offset);
		if (nid)
			ctx->node[nid - 1]->nip = ctx->node_paddr[nid];
		nid++;

		/* Build additional Cb(Cr) node, link it to the previous one */
		if (cfg.dst_nbp > 1) {
			bdisp_hw_build_node(ctx, &cfg, ctx->node[nid],
					    BDISP_CBCR, src_x_offset);
			ctx->node[nid - 1]->nip = ctx->node_paddr[nid];
			nid++;
		}

		/* Next stride until full width covered */
		src_x_offset += MAX_SRC_WIDTH;
		if (src_x_offset >= ctx->src.crop.width)
			break;
	}

	/* Mark last node as the last */
	ctx->node[nid - 1]->nip = 0;

	return 0;
}

/**
 * bdisp_hw_save_request
 * @ctx:        device context
 *
 * Save a copy of the request and of the built nodes
 *
 * RETURNS:
 * None
 */
static void bdisp_hw_save_request(struct bdisp_ctx *ctx)
{
	struct bdisp_node **copy_node = ctx->bdisp_dev->dbg.copy_node;
	struct bdisp_request *request = &ctx->bdisp_dev->dbg.copy_request;
	struct bdisp_node **node = ctx->node;
	int i;

	/* Request copy */
	request->src = ctx->src;
	request->dst = ctx->dst;
	request->hflip = ctx->hflip;
	request->vflip = ctx->vflip;
	request->nb_req++;

	/* Nodes copy */
	for (i = 0; i < MAX_NB_NODE; i++) {
		/* Allocate memory if not done yet */
		if (!copy_node[i]) {
			copy_node[i] = devm_kzalloc(ctx->bdisp_dev->dev,
						    sizeof(*copy_node),
						    GFP_KERNEL);
			if (!copy_node[i])
				return;
		}
		copy_node[i] = node[i];
	}
}

/**
 * bdisp_hw_update
 * @ctx:        device context
 *
 * Send the request to the HW
 *
 * RETURNS:
 * 0 on success
 */
int bdisp_hw_update(struct bdisp_ctx *ctx)
{
	int ret;
	struct bdisp_dev *bdisp = ctx->bdisp_dev;
	struct device *dev = bdisp->dev;
	unsigned int node_id;

	dev_dbg(dev, "%s\n", __func__);

	/* build nodes */
	ret = bdisp_hw_build_all_nodes(ctx);
	if (ret) {
		dev_err(dev, "cannot build nodes (%d)\n", ret);
		return ret;
	}

	/* Save a copy of the request */
	bdisp_hw_save_request(ctx);

	/* Configure interrupt to 'Last Node Reached for AQ1' */
	writel(BLT_AQ1_CTL_CFG, bdisp->regs + BLT_AQ1_CTL);
	writel(BLT_ITS_AQ1_LNA, bdisp->regs + BLT_ITM0);

	/* Write first node addr */
	writel(ctx->node_paddr[0], bdisp->regs + BLT_AQ1_IP);

	/* Find and write last node addr : this starts the HW processing */
	for (node_id = 0; node_id < MAX_NB_NODE - 1; node_id++) {
		if (!ctx->node[node_id]->nip)
			break;
	}
	writel(ctx->node_paddr[node_id], bdisp->regs + BLT_AQ1_LNA);

	return 0;
}
