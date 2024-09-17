/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#ifndef _SUN8I_MIXER_H_
#define _SUN8I_MIXER_H_

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "sunxi_engine.h"

#define SUN8I_MIXER_SIZE(w, h)			(((h) - 1) << 16 | ((w) - 1))
#define SUN8I_MIXER_COORD(x, y)			((y) << 16 | (x))

#define SUN8I_MIXER_GLOBAL_CTL			0x0
#define SUN8I_MIXER_GLOBAL_STATUS		0x4
#define SUN8I_MIXER_GLOBAL_DBUFF		0x8
#define SUN8I_MIXER_GLOBAL_SIZE			0xc

#define SUN8I_MIXER_GLOBAL_CTL_RT_EN		BIT(0)

#define SUN8I_MIXER_GLOBAL_DBUFF_ENABLE		BIT(0)

#define DE2_MIXER_UNIT_SIZE			0x6000
#define DE3_MIXER_UNIT_SIZE			0x3000

#define DE2_BLD_BASE				0x1000
#define DE2_CH_BASE				0x2000
#define DE2_CH_SIZE				0x1000

#define DE3_BLD_BASE				0x0800
#define DE3_CH_BASE				0x1000
#define DE3_CH_SIZE				0x0800

#define SUN8I_MIXER_BLEND_PIPE_CTL(base)	((base) + 0)
#define SUN8I_MIXER_BLEND_ATTR_FCOLOR(base, x)	((base) + 0x4 + 0x10 * (x))
#define SUN8I_MIXER_BLEND_ATTR_INSIZE(base, x)	((base) + 0x8 + 0x10 * (x))
#define SUN8I_MIXER_BLEND_ATTR_COORD(base, x)	((base) + 0xc + 0x10 * (x))
#define SUN8I_MIXER_BLEND_ROUTE(base)		((base) + 0x80)
#define SUN8I_MIXER_BLEND_PREMULTIPLY(base)	((base) + 0x84)
#define SUN8I_MIXER_BLEND_BKCOLOR(base)		((base) + 0x88)
#define SUN8I_MIXER_BLEND_OUTSIZE(base)		((base) + 0x8c)
#define SUN8I_MIXER_BLEND_MODE(base, x)		((base) + 0x90 + 0x04 * (x))
#define SUN8I_MIXER_BLEND_CK_CTL(base)		((base) + 0xb0)
#define SUN8I_MIXER_BLEND_CK_CFG(base)		((base) + 0xb4)
#define SUN8I_MIXER_BLEND_CK_MAX(base, x)	((base) + 0xc0 + 0x04 * (x))
#define SUN8I_MIXER_BLEND_CK_MIN(base, x)	((base) + 0xe0 + 0x04 * (x))
#define SUN8I_MIXER_BLEND_OUTCTL(base)		((base) + 0xfc)
#define SUN50I_MIXER_BLEND_CSC_CTL(base)	((base) + 0x100)
#define SUN50I_MIXER_BLEND_CSC_COEFF(base, layer, x) \
	((base) + 0x110 + (layer) * 0x30 +  (x) * 4)

#define SUN8I_MIXER_BLEND_PIPE_CTL_EN_MSK	GENMASK(12, 8)
#define SUN8I_MIXER_BLEND_PIPE_CTL_EN(pipe)	BIT(8 + pipe)
#define SUN8I_MIXER_BLEND_PIPE_CTL_FC_EN(pipe)	BIT(pipe)

/* colors are always in AARRGGBB format */
#define SUN8I_MIXER_BLEND_COLOR_BLACK		0xff000000
/* The following numbers are some still unknown magic numbers */
#define SUN8I_MIXER_BLEND_MODE_DEF		0x03010301

#define SUN8I_MIXER_BLEND_ROUTE_PIPE_MSK(n)	(0xf << ((n) << 2))
#define SUN8I_MIXER_BLEND_ROUTE_PIPE_SHIFT(n)	((n) << 2)

#define SUN8I_MIXER_BLEND_OUTCTL_INTERLACED	BIT(1)

#define SUN50I_MIXER_BLEND_CSC_CTL_EN(ch)	BIT(ch)
#define SUN50I_MIXER_BLEND_CSC_CONST_VAL(d, c)	(((d) << 16) | ((c) & 0xffff))

#define SUN8I_MIXER_FBFMT_ARGB8888	0
#define SUN8I_MIXER_FBFMT_ABGR8888	1
#define SUN8I_MIXER_FBFMT_RGBA8888	2
#define SUN8I_MIXER_FBFMT_BGRA8888	3
#define SUN8I_MIXER_FBFMT_XRGB8888	4
#define SUN8I_MIXER_FBFMT_XBGR8888	5
#define SUN8I_MIXER_FBFMT_RGBX8888	6
#define SUN8I_MIXER_FBFMT_BGRX8888	7
#define SUN8I_MIXER_FBFMT_RGB888	8
#define SUN8I_MIXER_FBFMT_BGR888	9
#define SUN8I_MIXER_FBFMT_RGB565	10
#define SUN8I_MIXER_FBFMT_BGR565	11
#define SUN8I_MIXER_FBFMT_ARGB4444	12
#define SUN8I_MIXER_FBFMT_ABGR4444	13
#define SUN8I_MIXER_FBFMT_RGBA4444	14
#define SUN8I_MIXER_FBFMT_BGRA4444	15
#define SUN8I_MIXER_FBFMT_ARGB1555	16
#define SUN8I_MIXER_FBFMT_ABGR1555	17
#define SUN8I_MIXER_FBFMT_RGBA5551	18
#define SUN8I_MIXER_FBFMT_BGRA5551	19
#define SUN8I_MIXER_FBFMT_ARGB2101010	20
#define SUN8I_MIXER_FBFMT_ABGR2101010	21
#define SUN8I_MIXER_FBFMT_RGBA1010102	22
#define SUN8I_MIXER_FBFMT_BGRA1010102	23

#define SUN8I_MIXER_FBFMT_YUYV		0
#define SUN8I_MIXER_FBFMT_UYVY		1
#define SUN8I_MIXER_FBFMT_YVYU		2
#define SUN8I_MIXER_FBFMT_VYUY		3
#define SUN8I_MIXER_FBFMT_NV16		4
#define SUN8I_MIXER_FBFMT_NV61		5
#define SUN8I_MIXER_FBFMT_YUV422	6
/* format 7 doesn't exist */
#define SUN8I_MIXER_FBFMT_NV12		8
#define SUN8I_MIXER_FBFMT_NV21		9
#define SUN8I_MIXER_FBFMT_YUV420	10
/* format 11 doesn't exist */
/* format 12 is semi-planar YUV411 UVUV */
/* format 13 is semi-planar YUV411 VUVU */
#define SUN8I_MIXER_FBFMT_YUV411	14
/* format 15 doesn't exist */
#define SUN8I_MIXER_FBFMT_P010_YUV	16
/* format 17 is P010 YVU */
#define SUN8I_MIXER_FBFMT_P210_YUV	18
/* format 19 is P210 YVU */
/* format 20 is packed YVU444 10-bit */
/* format 21 is packed YUV444 10-bit */

/*
 * Sub-engines listed bellow are unused for now. The EN registers are here only
 * to be used to disable these sub-engines.
 */
#define SUN8I_MIXER_FCE_EN			0xa0000
#define SUN8I_MIXER_BWS_EN			0xa2000
#define SUN8I_MIXER_LTI_EN			0xa4000
#define SUN8I_MIXER_PEAK_EN			0xa6000
#define SUN8I_MIXER_ASE_EN			0xa8000
#define SUN8I_MIXER_FCC_EN			0xaa000
#define SUN8I_MIXER_DCSC_EN			0xb0000

#define SUN50I_MIXER_FCE_EN			0x70000
#define SUN50I_MIXER_PEAK_EN			0x70800
#define SUN50I_MIXER_LCTI_EN			0x71000
#define SUN50I_MIXER_BLS_EN			0x71800
#define SUN50I_MIXER_FCC_EN			0x72000
#define SUN50I_MIXER_DNS_EN			0x80000
#define SUN50I_MIXER_DRC_EN			0xa0000
#define SUN50I_MIXER_FMT_EN			0xa8000
#define SUN50I_MIXER_CDC0_EN			0xd0000
#define SUN50I_MIXER_CDC1_EN			0xd8000

enum {
	/* First mixer or second mixer with VEP support. */
	CCSC_MIXER0_LAYOUT,
	/* Second mixer without VEP support. */
	CCSC_MIXER1_LAYOUT,
	/* First mixer with the MMIO layout found in the D1 SoC. */
	CCSC_D1_MIXER0_LAYOUT,
};

/**
 * struct sun8i_mixer_cfg - mixer HW configuration
 * @vi_num: number of VI channels
 * @ui_num: number of UI channels
 * @scaler_mask: bitmask which tells which channel supports scaling
 *	First, scaler supports for VI channels is defined and after that, scaler
 *	support for UI channels. For example, if mixer has 2 VI channels without
 *	scaler and 2 UI channels with scaler, bitmask would be 0xC.
 * @ccsc: select set of CCSC base addresses from the enumeration above.
 * @mod_rate: module clock rate that needs to be set in order to have
 *	a functional block.
 * @is_de3: true, if this is next gen display engine 3.0, false otherwise.
 * @scaline_yuv: size of a scanline for VI scaler for YUV formats.
 */
struct sun8i_mixer_cfg {
	int		vi_num;
	int		ui_num;
	int		scaler_mask;
	int		ccsc;
	unsigned long	mod_rate;
	unsigned int	is_de3 : 1;
	unsigned int	scanline_yuv;
};

struct sun8i_mixer {
	struct sunxi_engine		engine;

	const struct sun8i_mixer_cfg	*cfg;

	struct reset_control		*reset;

	struct clk			*bus_clk;
	struct clk			*mod_clk;
};

static inline struct sun8i_mixer *
engine_to_sun8i_mixer(struct sunxi_engine *engine)
{
	return container_of(engine, struct sun8i_mixer, engine);
}

static inline u32
sun8i_blender_base(struct sun8i_mixer *mixer)
{
	return mixer->cfg->is_de3 ? DE3_BLD_BASE : DE2_BLD_BASE;
}

static inline u32
sun8i_channel_base(struct sun8i_mixer *mixer, int channel)
{
	if (mixer->cfg->is_de3)
		return DE3_CH_BASE + channel * DE3_CH_SIZE;
	else
		return DE2_CH_BASE + channel * DE2_CH_SIZE;
}

int sun8i_mixer_drm_format_to_hw(u32 format, u32 *hw_format);
#endif /* _SUN8I_MIXER_H_ */
