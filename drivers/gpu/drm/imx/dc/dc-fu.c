// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/math.h>

#include "dc-fu.h"
#include "dc-pe.h"

/* STATICCONTROL */
#define SHDLDREQSTICKY_MASK		GENMASK(31, 24)
#define SHDLDREQSTICKY(x)		FIELD_PREP(SHDLDREQSTICKY_MASK, (x))
#define BASEADDRESSAUTOUPDATE_MASK	GENMASK(23, 16)
#define BASEADDRESSAUTOUPDATE(x)	FIELD_PREP(BASEADDRESSAUTOUPDATE_MASK, (x))

/* BURSTBUFFERMANAGEMENT */
#define SETBURSTLENGTH_MASK		GENMASK(12, 8)
#define SETBURSTLENGTH(x)		FIELD_PREP(SETBURSTLENGTH_MASK, (x))
#define SETNUMBUFFERS_MASK		GENMASK(7, 0)
#define SETNUMBUFFERS(x)		FIELD_PREP(SETNUMBUFFERS_MASK, (x))
#define LINEMODE_MASK			BIT(31)

/* SOURCEBUFFERATTRIBUTES */
#define BITSPERPIXEL_MASK		GENMASK(21, 16)
#define BITSPERPIXEL(x)			FIELD_PREP(BITSPERPIXEL_MASK, (x))
#define STRIDE_MASK			GENMASK(15, 0)
#define STRIDE(x)			FIELD_PREP(STRIDE_MASK, (x) - 1)

/* SOURCEBUFFERDIMENSION */
#define LINEWIDTH(x)			FIELD_PREP(GENMASK(13, 0), (x))
#define LINECOUNT(x)			FIELD_PREP(GENMASK(29, 16), (x))

/* LAYEROFFSET */
#define LAYERXOFFSET(x)			FIELD_PREP(GENMASK(14, 0), (x))
#define LAYERYOFFSET(x)			FIELD_PREP(GENMASK(30, 16), (x))

/* CLIPWINDOWOFFSET */
#define CLIPWINDOWXOFFSET(x)		FIELD_PREP(GENMASK(14, 0), (x))
#define CLIPWINDOWYOFFSET(x)		FIELD_PREP(GENMASK(30, 16), (x))

/* CLIPWINDOWDIMENSIONS */
#define CLIPWINDOWWIDTH(x)		FIELD_PREP(GENMASK(13, 0), (x) - 1)
#define CLIPWINDOWHEIGHT(x)		FIELD_PREP(GENMASK(29, 16), (x) - 1)

enum dc_linemode {
	/*
	 * Mandatory setting for operation in the Display Controller.
	 * Works also for Blit Engine with marginal performance impact.
	 */
	LINEMODE_DISPLAY = 0,
};

struct dc_fu_pixel_format {
	u32 pixel_format;
	u32 bits;
	u32 shifts;
};

static const struct dc_fu_pixel_format pixel_formats[] = {
	{
		DRM_FORMAT_XRGB8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(16) | G_SHIFT(8)  | B_SHIFT(0)  | A_SHIFT(0),
	},
};

void dc_fu_get_pixel_format_bits(struct dc_fu *fu, u32 format, u32 *bits)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		if (pixel_formats[i].pixel_format == format) {
			*bits = pixel_formats[i].bits;
			return;
		}
	}
}

void
dc_fu_get_pixel_format_shifts(struct dc_fu *fu, u32 format, u32 *shifts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		if (pixel_formats[i].pixel_format == format) {
			*shifts = pixel_formats[i].shifts;
			return;
		}
	}
}

static inline void dc_fu_enable_shden(struct dc_fu *fu)
{
	regmap_write_bits(fu->reg_cfg, STATICCONTROL, SHDEN, SHDEN);
}

static inline void dc_fu_baddr_autoupdate(struct dc_fu *fu, u8 layer_mask)
{
	regmap_write_bits(fu->reg_cfg, STATICCONTROL,
			  BASEADDRESSAUTOUPDATE_MASK,
			  BASEADDRESSAUTOUPDATE(layer_mask));
}

void dc_fu_shdldreq_sticky(struct dc_fu *fu, u8 layer_mask)
{
	regmap_write_bits(fu->reg_cfg, STATICCONTROL, SHDLDREQSTICKY_MASK,
			  SHDLDREQSTICKY(layer_mask));
}

static inline void dc_fu_set_linemode(struct dc_fu *fu, enum dc_linemode mode)
{
	regmap_write_bits(fu->reg_cfg, BURSTBUFFERMANAGEMENT, LINEMODE_MASK,
			  mode);
}

static inline void dc_fu_set_numbuffers(struct dc_fu *fu, unsigned int num)
{
	regmap_write_bits(fu->reg_cfg, BURSTBUFFERMANAGEMENT,
			  SETNUMBUFFERS_MASK, SETNUMBUFFERS(num));
}

static void dc_fu_set_burstlength(struct dc_fu *fu, dma_addr_t baddr)
{
	unsigned int burst_size, burst_length;

	burst_size = 1 << __ffs(baddr);
	burst_size = round_up(burst_size, 8);
	burst_size = min(burst_size, 128U);
	burst_length = burst_size / 8;

	regmap_write_bits(fu->reg_cfg, BURSTBUFFERMANAGEMENT,
			  SETBURSTLENGTH_MASK, SETBURSTLENGTH(burst_length));
}

static void dc_fu_set_baseaddress(struct dc_fu *fu, enum dc_fu_frac frac,
				  dma_addr_t baddr)
{
	regmap_write(fu->reg_cfg, fu->reg_baseaddr[frac], baddr);
}

void dc_fu_set_src_bpp(struct dc_fu *fu, enum dc_fu_frac frac, unsigned int bpp)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_sourcebufferattributes[frac],
			  BITSPERPIXEL_MASK, BITSPERPIXEL(bpp));
}

static void dc_fu_set_src_stride(struct dc_fu *fu, enum dc_fu_frac frac,
				 unsigned int stride)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_sourcebufferattributes[frac],
			  STRIDE_MASK, STRIDE(stride));
}

static void dc_fu_set_src_buf_dimensions(struct dc_fu *fu, enum dc_fu_frac frac,
					 int w, int h)
{
	regmap_write(fu->reg_cfg, fu->reg_sourcebufferdimension[frac],
		     LINEWIDTH(w) | LINECOUNT(h));
}

static inline void dc_fu_layeroffset(struct dc_fu *fu, enum dc_fu_frac frac,
				     unsigned int x, unsigned int y)
{
	regmap_write(fu->reg_cfg, fu->reg_layeroffset[frac],
		     LAYERXOFFSET(x) | LAYERYOFFSET(y));
}

static inline void dc_fu_clipoffset(struct dc_fu *fu, enum dc_fu_frac frac,
				    unsigned int x, unsigned int y)
{
	regmap_write(fu->reg_cfg, fu->reg_clipwindowoffset[frac],
		     CLIPWINDOWXOFFSET(x) | CLIPWINDOWYOFFSET(y));
}

static inline void dc_fu_clipdimensions(struct dc_fu *fu, enum dc_fu_frac frac,
					unsigned int w, unsigned int h)
{
	regmap_write(fu->reg_cfg, fu->reg_clipwindowdimensions[frac],
		     CLIPWINDOWWIDTH(w) | CLIPWINDOWHEIGHT(h));
}

static inline void
dc_fu_set_pixel_blend_mode(struct dc_fu *fu, enum dc_fu_frac frac)
{
	regmap_write(fu->reg_cfg, fu->reg_layerproperty[frac], 0);
	regmap_write(fu->reg_cfg, fu->reg_constantcolor[frac], 0);
}

static void dc_fu_enable_src_buf(struct dc_fu *fu, enum dc_fu_frac frac)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_layerproperty[frac],
			  SOURCEBUFFERENABLE, SOURCEBUFFERENABLE);
}

static void dc_fu_disable_src_buf(struct dc_fu *fu, enum dc_fu_frac frac)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_layerproperty[frac],
			  SOURCEBUFFERENABLE, 0);

	if (fu->lb) {
		dc_lb_pec_clken(fu->lb, CLKEN_DISABLE);
		dc_lb_mode(fu->lb, LB_NEUTRAL);
	}
}

static void dc_fu_set_layerblend(struct dc_fu *fu, struct dc_lb *lb)
{
	fu->lb = lb;
}

static enum dc_link_id dc_fu_get_link_id(struct dc_fu *fu)
{
	return fu->link_id;
}

static const char *dc_fu_get_name(struct dc_fu *fu)
{
	return fu->name;
}

const struct dc_fu_ops dc_fu_common_ops = {
	.set_burstlength	= dc_fu_set_burstlength,
	.set_baseaddress	= dc_fu_set_baseaddress,
	.set_src_stride		= dc_fu_set_src_stride,
	.set_src_buf_dimensions = dc_fu_set_src_buf_dimensions,
	.enable_src_buf		= dc_fu_enable_src_buf,
	.disable_src_buf	= dc_fu_disable_src_buf,
	.set_layerblend		= dc_fu_set_layerblend,
	.get_link_id		= dc_fu_get_link_id,
	.get_name		= dc_fu_get_name,
};

const struct dc_fu_ops *dc_fu_get_ops(struct dc_fu *fu)
{
	return &fu->ops;
}

void dc_fu_common_hw_init(struct dc_fu *fu)
{
	enum dc_fu_frac i;

	dc_fu_baddr_autoupdate(fu, 0x0);
	dc_fu_enable_shden(fu);
	dc_fu_set_linemode(fu, LINEMODE_DISPLAY);
	dc_fu_set_numbuffers(fu, 16);

	for (i = DC_FETCHUNIT_FRAC0; i < DC_FETCHUNIT_FRAC_NUM; i++) {
		dc_fu_layeroffset(fu, i, 0, 0);
		dc_fu_clipoffset(fu, i, 0, 0);
		dc_fu_clipdimensions(fu, i, 1, 1);
		dc_fu_disable_src_buf(fu, i);
		dc_fu_set_pixel_blend_mode(fu, i);
	}
}
