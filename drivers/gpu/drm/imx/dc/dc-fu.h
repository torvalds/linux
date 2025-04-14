/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_FETCHUNIT_H__
#define __DC_FETCHUNIT_H__

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>

#include "dc-pe.h"

#define FRAC_OFFSET			0x28

#define STATICCONTROL			0x8
#define BURSTBUFFERMANAGEMENT		0xc

/* COLORCOMPONENTBITS */
#define R_BITS(x)			FIELD_PREP_CONST(GENMASK(27, 24), (x))
#define G_BITS(x)			FIELD_PREP_CONST(GENMASK(19, 16), (x))
#define B_BITS(x)			FIELD_PREP_CONST(GENMASK(11, 8), (x))
#define A_BITS(x)			FIELD_PREP_CONST(GENMASK(3, 0), (x))

/* COLORCOMPONENTSHIFT */
#define R_SHIFT(x)			FIELD_PREP_CONST(GENMASK(28, 24), (x))
#define G_SHIFT(x)			FIELD_PREP_CONST(GENMASK(20, 16), (x))
#define B_SHIFT(x)			FIELD_PREP_CONST(GENMASK(12, 8), (x))
#define A_SHIFT(x)			FIELD_PREP_CONST(GENMASK(4, 0), (x))

/* LAYERPROPERTY */
#define YUVCONVERSIONMODE_MASK		GENMASK(18, 17)
#define YUVCONVERSIONMODE(x)		FIELD_PREP(YUVCONVERSIONMODE_MASK, (x))
#define SOURCEBUFFERENABLE		BIT(31)

/* FRAMEDIMENSIONS */
#define FRAMEWIDTH(x)			FIELD_PREP(GENMASK(13, 0), (x))
#define FRAMEHEIGHT(x)			FIELD_PREP(GENMASK(29, 16), (x))

/* CONTROL */
#define INPUTSELECT_MASK		GENMASK(4, 3)
#define INPUTSELECT(x)			FIELD_PREP(INPUTSELECT_MASK, (x))
#define RASTERMODE_MASK			GENMASK(2, 0)
#define RASTERMODE(x)			FIELD_PREP(RASTERMODE_MASK, (x))

enum dc_yuvconversionmode {
	YUVCONVERSIONMODE_OFF,
};

enum dc_inputselect {
	INPUTSELECT_INACTIVE,
};

enum dc_rastermode {
	RASTERMODE_NORMAL,
};

enum {
	DC_FETCHUNIT_FL0,
	DC_FETCHUNIT_FW2,
};

enum dc_fu_frac {
	DC_FETCHUNIT_FRAC0,
	DC_FETCHUNIT_FRAC1,
	DC_FETCHUNIT_FRAC2,
	DC_FETCHUNIT_FRAC3,
	DC_FETCHUNIT_FRAC4,
	DC_FETCHUNIT_FRAC5,
	DC_FETCHUNIT_FRAC6,
	DC_FETCHUNIT_FRAC7,
	DC_FETCHUNIT_FRAC_NUM
};

struct dc_fu;
struct dc_lb;

struct dc_fu_ops {
	void (*init)(struct dc_fu *fu);
	void (*set_burstlength)(struct dc_fu *fu, dma_addr_t baddr);
	void (*set_baseaddress)(struct dc_fu *fu, enum dc_fu_frac frac,
				dma_addr_t baddr);
	void (*set_src_stride)(struct dc_fu *fu, enum dc_fu_frac frac,
			       unsigned int stride);
	void (*set_src_buf_dimensions)(struct dc_fu *fu, enum dc_fu_frac frac,
				       int w, int h);
	void (*set_fmt)(struct dc_fu *fu, enum dc_fu_frac frac,
			const struct drm_format_info *format);
	void (*enable_src_buf)(struct dc_fu *fu, enum dc_fu_frac frac);
	void (*disable_src_buf)(struct dc_fu *fu, enum dc_fu_frac frac);
	void (*set_framedimensions)(struct dc_fu *fu, int w, int h);
	void (*set_layerblend)(struct dc_fu *fu, struct dc_lb *lb);
	enum dc_link_id (*get_link_id)(struct dc_fu *fu);
	const char *(*get_name)(struct dc_fu *fu);
};

struct dc_fu {
	struct regmap *reg_pec;
	struct regmap *reg_cfg;
	char name[21];
	u32 reg_baseaddr[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_sourcebufferattributes[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_sourcebufferdimension[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_layeroffset[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_clipwindowoffset[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_clipwindowdimensions[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_constantcolor[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_layerproperty[DC_FETCHUNIT_FRAC_NUM];
	unsigned int id;
	enum dc_link_id link_id;
	struct dc_fu_ops ops;
	struct dc_lb *lb;
};

extern const struct dc_fu_ops dc_fu_common_ops;

void dc_fu_get_pixel_format_bits(struct dc_fu *fu, u32 format, u32 *bits);
void dc_fu_get_pixel_format_shifts(struct dc_fu *fu, u32 format, u32 *shifts);
void dc_fu_shdldreq_sticky(struct dc_fu *fu, u8 layer_mask);
void dc_fu_set_src_bpp(struct dc_fu *fu, enum dc_fu_frac frac, unsigned int bpp);
void dc_fu_common_hw_init(struct dc_fu *fu);

const struct dc_fu_ops *dc_fu_get_ops(struct dc_fu *fu);

#endif /* __DC_FETCHUNIT_H__ */
