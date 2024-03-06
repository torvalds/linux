// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Free Electrons
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _SUN4I_FRONTEND_H_
#define _SUN4I_FRONTEND_H_

#include <linux/list.h>
#include <linux/mod_devicetable.h>

#define SUN4I_FRONTEND_EN_REG			0x000
#define SUN4I_FRONTEND_EN_EN				BIT(0)

#define SUN4I_FRONTEND_FRM_CTRL_REG		0x004
#define SUN4I_FRONTEND_FRM_CTRL_COEF_ACCESS_CTRL	BIT(23)
#define SUN4I_FRONTEND_FRM_CTRL_FRM_START		BIT(16)
#define SUN4I_FRONTEND_FRM_CTRL_COEF_RDY		BIT(1)
#define SUN4I_FRONTEND_FRM_CTRL_REG_RDY			BIT(0)

#define SUN4I_FRONTEND_BYPASS_REG		0x008
#define SUN4I_FRONTEND_BYPASS_CSC_EN			BIT(1)

#define SUN4I_FRONTEND_BUF_ADDR0_REG		0x020
#define SUN4I_FRONTEND_BUF_ADDR1_REG		0x024
#define SUN4I_FRONTEND_BUF_ADDR2_REG		0x028

#define SUN4I_FRONTEND_TB_OFF0_REG		0x030
#define SUN4I_FRONTEND_TB_OFF1_REG		0x034
#define SUN4I_FRONTEND_TB_OFF2_REG		0x038
#define SUN4I_FRONTEND_TB_OFF_X1(x1)			((x1) << 16)
#define SUN4I_FRONTEND_TB_OFF_Y0(y0)			((y0) << 8)
#define SUN4I_FRONTEND_TB_OFF_X0(x0)			(x0)

#define SUN4I_FRONTEND_LINESTRD0_REG		0x040
#define SUN4I_FRONTEND_LINESTRD1_REG		0x044
#define SUN4I_FRONTEND_LINESTRD2_REG		0x048

/*
 * In tiled mode, the stride is defined as the distance between the start of the
 * end line of the current tile and the start of the first line in the next
 * vertical tile.
 *
 * Tiles are represented in row-major order, thus the end line of current tile
 * starts at: 31 * 32 (31 lines of 32 cols), the next vertical tile starts at:
 * 32-bit-aligned-width * 32 and the distance is:
 * 32 * (32-bit-aligned-width - 31).
 */
#define SUN4I_FRONTEND_LINESTRD_TILED(stride)		(((stride) - 31) * 32)

#define SUN4I_FRONTEND_INPUT_FMT_REG		0x04c
#define SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_PLANAR	(0 << 8)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_PACKED	(1 << 8)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_SEMIPLANAR	(2 << 8)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_MB32_PLANAR	(4 << 8)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_MB32_SEMIPLANAR (6 << 8)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV444	(0 << 4)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV422	(1 << 4)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV420	(2 << 4)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV411	(3 << 4)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_RGB		(5 << 4)
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_UYVY		0
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_YUYV		1
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_VYUY		2
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_YVYU		3
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_UV		0
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_VU		1
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_BGRX		0
#define SUN4I_FRONTEND_INPUT_FMT_DATA_PS_XRGB		1

#define SUN4I_FRONTEND_OUTPUT_FMT_REG		0x05c
#define SUN4I_FRONTEND_OUTPUT_FMT_DATA_FMT_BGRX8888	1
#define SUN4I_FRONTEND_OUTPUT_FMT_DATA_FMT_XRGB8888	2

#define SUN4I_FRONTEND_CSC_COEF_REG(c)		(0x070 + (0x4 * (c)))

#define SUN4I_FRONTEND_CH0_INSIZE_REG		0x100
#define SUN4I_FRONTEND_INSIZE(h, w)			((((h) - 1) << 16) | (((w) - 1)))

#define SUN4I_FRONTEND_CH0_OUTSIZE_REG		0x104
#define SUN4I_FRONTEND_OUTSIZE(h, w)			((((h) - 1) << 16) | (((w) - 1)))

#define SUN4I_FRONTEND_CH0_HORZFACT_REG		0x108
#define SUN4I_FRONTEND_HORZFACT(i, f)			(((i) << 16) | (f))

#define SUN4I_FRONTEND_CH0_VERTFACT_REG		0x10c
#define SUN4I_FRONTEND_VERTFACT(i, f)			(((i) << 16) | (f))

#define SUN4I_FRONTEND_CH0_HORZPHASE_REG	0x110
#define SUN4I_FRONTEND_CH0_VERTPHASE0_REG	0x114
#define SUN4I_FRONTEND_CH0_VERTPHASE1_REG	0x118

#define SUN4I_FRONTEND_CH1_INSIZE_REG		0x200
#define SUN4I_FRONTEND_CH1_OUTSIZE_REG		0x204
#define SUN4I_FRONTEND_CH1_HORZFACT_REG		0x208
#define SUN4I_FRONTEND_CH1_VERTFACT_REG		0x20c

#define SUN4I_FRONTEND_CH1_HORZPHASE_REG	0x210
#define SUN4I_FRONTEND_CH1_VERTPHASE0_REG	0x214
#define SUN4I_FRONTEND_CH1_VERTPHASE1_REG	0x218

#define SUN4I_FRONTEND_CH0_HORZCOEF0_REG(i)	(0x400 + i * 4)
#define SUN4I_FRONTEND_CH0_HORZCOEF1_REG(i)	(0x480 + i * 4)
#define SUN4I_FRONTEND_CH0_VERTCOEF_REG(i)	(0x500 + i * 4)
#define SUN4I_FRONTEND_CH1_HORZCOEF0_REG(i)	(0x600 + i * 4)
#define SUN4I_FRONTEND_CH1_HORZCOEF1_REG(i)	(0x680 + i * 4)
#define SUN4I_FRONTEND_CH1_VERTCOEF_REG(i)	(0x700 + i * 4)

struct clk;
struct device_node;
struct drm_plane;
struct regmap;
struct reset_control;

struct sun4i_frontend_data {
	bool	has_coef_access_ctrl;
	bool	has_coef_rdy;
	u32	ch_phase[2];
};

struct sun4i_frontend {
	struct list_head	list;
	struct device		*dev;
	struct device_node	*node;

	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct clk		*ram_clk;
	struct regmap		*regs;
	struct reset_control	*reset;

	const struct sun4i_frontend_data	*data;
};

extern const struct of_device_id sun4i_frontend_of_table[];
extern const u32 sunxi_bt601_yuv2rgb_coef[12];

int sun4i_frontend_init(struct sun4i_frontend *frontend);
void sun4i_frontend_exit(struct sun4i_frontend *frontend);
int sun4i_frontend_enable(struct sun4i_frontend *frontend);

void sun4i_frontend_update_buffer(struct sun4i_frontend *frontend,
				  struct drm_plane *plane);
void sun4i_frontend_update_coord(struct sun4i_frontend *frontend,
				 struct drm_plane *plane);
int sun4i_frontend_update_formats(struct sun4i_frontend *frontend,
				  struct drm_plane *plane, uint32_t out_fmt);
bool sun4i_frontend_format_is_supported(uint32_t fmt, uint64_t modifier);

#endif /* _SUN4I_FRONTEND_H_ */
