/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Boris Brezillon <boris.brezillon@free-electrons.com>
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef __SUN4I_TCON_H__
#define __SUN4I_TCON_H__

#include <drm/drm_crtc.h>

#include <linux/kernel.h>
#include <linux/reset.h>

#define SUN4I_TCON_GCTL_REG			0x0
#define SUN4I_TCON_GCTL_TCON_ENABLE			BIT(31)
#define SUN4I_TCON_GCTL_IOMAP_MASK			BIT(0)
#define SUN4I_TCON_GCTL_IOMAP_TCON1			(1 << 0)
#define SUN4I_TCON_GCTL_IOMAP_TCON0			(0 << 0)

#define SUN4I_TCON_GINT0_REG			0x4
#define SUN4I_TCON_GINT0_VBLANK_ENABLE(pipe)		BIT(31 - (pipe))
#define SUN4I_TCON_GINT0_VBLANK_INT(pipe)		BIT(15 - (pipe))

#define SUN4I_TCON_GINT1_REG			0x8
#define SUN4I_TCON_FRM_CTL_REG			0x10

#define SUN4I_TCON0_CTL_REG			0x40
#define SUN4I_TCON0_CTL_TCON_ENABLE			BIT(31)
#define SUN4I_TCON0_CTL_CLK_DELAY_MASK			GENMASK(8, 4)
#define SUN4I_TCON0_CTL_CLK_DELAY(delay)		((delay << 4) & SUN4I_TCON0_CTL_CLK_DELAY_MASK)

#define SUN4I_TCON0_DCLK_REG			0x44
#define SUN4I_TCON0_DCLK_GATE_BIT			(31)
#define SUN4I_TCON0_DCLK_DIV_SHIFT			(0)
#define SUN4I_TCON0_DCLK_DIV_WIDTH			(7)

#define SUN4I_TCON0_BASIC0_REG			0x48
#define SUN4I_TCON0_BASIC0_X(width)			((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON0_BASIC0_Y(height)			(((height) - 1) & 0xfff)

#define SUN4I_TCON0_BASIC1_REG			0x4c
#define SUN4I_TCON0_BASIC1_H_TOTAL(total)		((((total) - 1) & 0x1fff) << 16)
#define SUN4I_TCON0_BASIC1_H_BACKPORCH(bp)		(((bp) - 1) & 0xfff)

#define SUN4I_TCON0_BASIC2_REG			0x50
#define SUN4I_TCON0_BASIC2_V_TOTAL(total)		((((total) * 2) & 0x1fff) << 16)
#define SUN4I_TCON0_BASIC2_V_BACKPORCH(bp)		(((bp) - 1) & 0xfff)

#define SUN4I_TCON0_BASIC3_REG			0x54
#define SUN4I_TCON0_BASIC3_H_SYNC(width)		((((width) - 1) & 0x7ff) << 16)
#define SUN4I_TCON0_BASIC3_V_SYNC(height)		(((height) - 1) & 0x7ff)

#define SUN4I_TCON0_HV_IF_REG			0x58
#define SUN4I_TCON0_CPU_IF_REG			0x60
#define SUN4I_TCON0_CPU_WR_REG			0x64
#define SUN4I_TCON0_CPU_RD0_REG			0x68
#define SUN4I_TCON0_CPU_RDA_REG			0x6c
#define SUN4I_TCON0_TTL0_REG			0x70
#define SUN4I_TCON0_TTL1_REG			0x74
#define SUN4I_TCON0_TTL2_REG			0x78
#define SUN4I_TCON0_TTL3_REG			0x7c
#define SUN4I_TCON0_TTL4_REG			0x80
#define SUN4I_TCON0_LVDS_IF_REG			0x84
#define SUN4I_TCON0_IO_POL_REG			0x88
#define SUN4I_TCON0_IO_POL_DCLK_PHASE(phase)		((phase & 3) << 28)
#define SUN4I_TCON0_IO_POL_HSYNC_POSITIVE		BIT(25)
#define SUN4I_TCON0_IO_POL_VSYNC_POSITIVE		BIT(24)

#define SUN4I_TCON0_IO_TRI_REG			0x8c
#define SUN4I_TCON0_IO_TRI_HSYNC_DISABLE		BIT(25)
#define SUN4I_TCON0_IO_TRI_VSYNC_DISABLE		BIT(24)
#define SUN4I_TCON0_IO_TRI_DATA_PINS_DISABLE(pins)	GENMASK(pins, 0)

#define SUN4I_TCON1_CTL_REG			0x90
#define SUN4I_TCON1_CTL_TCON_ENABLE			BIT(31)
#define SUN4I_TCON1_CTL_INTERLACE_ENABLE		BIT(20)
#define SUN4I_TCON1_CTL_CLK_DELAY_MASK			GENMASK(8, 4)
#define SUN4I_TCON1_CTL_CLK_DELAY(delay)		((delay << 4) & SUN4I_TCON1_CTL_CLK_DELAY_MASK)

#define SUN4I_TCON1_BASIC0_REG			0x94
#define SUN4I_TCON1_BASIC0_X(width)			((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON1_BASIC0_Y(height)			(((height) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC1_REG			0x98
#define SUN4I_TCON1_BASIC1_X(width)			((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON1_BASIC1_Y(height)			(((height) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC2_REG			0x9c
#define SUN4I_TCON1_BASIC2_X(width)			((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON1_BASIC2_Y(height)			(((height) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC3_REG			0xa0
#define SUN4I_TCON1_BASIC3_H_TOTAL(total)		((((total) - 1) & 0x1fff) << 16)
#define SUN4I_TCON1_BASIC3_H_BACKPORCH(bp)		(((bp) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC4_REG			0xa4
#define SUN4I_TCON1_BASIC4_V_TOTAL(total)		(((total) & 0x1fff) << 16)
#define SUN4I_TCON1_BASIC4_V_BACKPORCH(bp)		(((bp) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC5_REG			0xa8
#define SUN4I_TCON1_BASIC5_H_SYNC(width)		((((width) - 1) & 0x3ff) << 16)
#define SUN4I_TCON1_BASIC5_V_SYNC(height)		(((height) - 1) & 0x3ff)

#define SUN4I_TCON1_IO_POL_REG			0xf0
#define SUN4I_TCON1_IO_TRI_REG			0xf4
#define SUN4I_TCON_CEU_CTL_REG			0x100
#define SUN4I_TCON_CEU_MUL_RR_REG		0x110
#define SUN4I_TCON_CEU_MUL_RG_REG		0x114
#define SUN4I_TCON_CEU_MUL_RB_REG		0x118
#define SUN4I_TCON_CEU_ADD_RC_REG		0x11c
#define SUN4I_TCON_CEU_MUL_GR_REG		0x120
#define SUN4I_TCON_CEU_MUL_GG_REG		0x124
#define SUN4I_TCON_CEU_MUL_GB_REG		0x128
#define SUN4I_TCON_CEU_ADD_GC_REG		0x12c
#define SUN4I_TCON_CEU_MUL_BR_REG		0x130
#define SUN4I_TCON_CEU_MUL_BG_REG		0x134
#define SUN4I_TCON_CEU_MUL_BB_REG		0x138
#define SUN4I_TCON_CEU_ADD_BC_REG		0x13c
#define SUN4I_TCON_CEU_RANGE_R_REG		0x140
#define SUN4I_TCON_CEU_RANGE_G_REG		0x144
#define SUN4I_TCON_CEU_RANGE_B_REG		0x148
#define SUN4I_TCON_MUX_CTRL_REG			0x200
#define SUN4I_TCON1_FILL_CTL_REG		0x300
#define SUN4I_TCON1_FILL_BEG0_REG		0x304
#define SUN4I_TCON1_FILL_END0_REG		0x308
#define SUN4I_TCON1_FILL_DATA0_REG		0x30c
#define SUN4I_TCON1_FILL_BEG1_REG		0x310
#define SUN4I_TCON1_FILL_END1_REG		0x314
#define SUN4I_TCON1_FILL_DATA1_REG		0x318
#define SUN4I_TCON1_FILL_BEG2_REG		0x31c
#define SUN4I_TCON1_FILL_END2_REG		0x320
#define SUN4I_TCON1_FILL_DATA2_REG		0x324
#define SUN4I_TCON1_GAMMA_TABLE_REG		0x400

#define SUN4I_TCON_MAX_CHANNELS		2

struct sun4i_tcon {
	struct device			*dev;
	struct drm_device		*drm;
	struct regmap			*regs;

	/* Main bus clock */
	struct clk			*clk;

	/* Clocks for the TCON channels */
	struct clk			*sclk0;
	struct clk			*sclk1;

	/* Pixel clock */
	struct clk			*dclk;

	/* Reset control */
	struct reset_control		*lcd_rst;

	/* Platform adjustments */
	bool				has_mux;

	struct drm_panel		*panel;
};

struct drm_panel *sun4i_tcon_find_panel(struct device_node *node);

/* Global Control */
void sun4i_tcon_disable(struct sun4i_tcon *tcon);
void sun4i_tcon_enable(struct sun4i_tcon *tcon);

/* Channel Control */
void sun4i_tcon_channel_disable(struct sun4i_tcon *tcon, int channel);
void sun4i_tcon_channel_enable(struct sun4i_tcon *tcon, int channel);

void sun4i_tcon_enable_vblank(struct sun4i_tcon *tcon, bool enable);

/* Mode Related Controls */
void sun4i_tcon_switch_interlace(struct sun4i_tcon *tcon,
				 bool enable);
void sun4i_tcon0_mode_set(struct sun4i_tcon *tcon,
			  struct drm_display_mode *mode);
void sun4i_tcon1_mode_set(struct sun4i_tcon *tcon,
			  struct drm_display_mode *mode);

#endif /* __SUN4I_TCON_H__ */
