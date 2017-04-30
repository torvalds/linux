/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN4I_BACKEND_H_
#define _SUN4I_BACKEND_H_

#include <linux/clk.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "sunxi_engine.h"

#define SUN4I_BACKEND_MODCTL_REG		0x800
#define SUN4I_BACKEND_MODCTL_LINE_SEL			BIT(29)
#define SUN4I_BACKEND_MODCTL_ITLMOD_EN			BIT(28)
#define SUN4I_BACKEND_MODCTL_OUT_SEL			GENMASK(22, 20)
#define SUN4I_BACKEND_MODCTL_OUT_LCD0				(0 << 20)
#define SUN4I_BACKEND_MODCTL_OUT_LCD1				(1 << 20)
#define SUN4I_BACKEND_MODCTL_OUT_FE0				(6 << 20)
#define SUN4I_BACKEND_MODCTL_OUT_FE1				(7 << 20)
#define SUN4I_BACKEND_MODCTL_HWC_EN			BIT(16)
#define SUN4I_BACKEND_MODCTL_LAY_EN(l)			BIT(8 + l)
#define SUN4I_BACKEND_MODCTL_OCSC_EN			BIT(5)
#define SUN4I_BACKEND_MODCTL_DFLK_EN			BIT(4)
#define SUN4I_BACKEND_MODCTL_DLP_START_CTL		BIT(2)
#define SUN4I_BACKEND_MODCTL_START_CTL			BIT(1)
#define SUN4I_BACKEND_MODCTL_DEBE_EN			BIT(0)

#define SUN4I_BACKEND_BACKCOLOR_REG		0x804
#define SUN4I_BACKEND_BACKCOLOR(r, g, b)		(((r) << 16) | ((g) << 8) | (b))

#define SUN4I_BACKEND_DISSIZE_REG		0x808
#define SUN4I_BACKEND_DISSIZE(w, h)			(((((h) - 1) & 0xffff) << 16) | \
							 (((w) - 1) & 0xffff))

#define SUN4I_BACKEND_LAYSIZE_REG(l)		(0x810 + (0x4 * (l)))
#define SUN4I_BACKEND_LAYSIZE(w, h)			(((((h) - 1) & 0x1fff) << 16) | \
							 (((w) - 1) & 0x1fff))

#define SUN4I_BACKEND_LAYCOOR_REG(l)		(0x820 + (0x4 * (l)))
#define SUN4I_BACKEND_LAYCOOR(x, y)			((((u32)(y) & 0xffff) << 16) | \
							 ((u32)(x) & 0xffff))

#define SUN4I_BACKEND_LAYLINEWIDTH_REG(l)	(0x840 + (0x4 * (l)))

#define SUN4I_BACKEND_LAYFB_L32ADD_REG(l)	(0x850 + (0x4 * (l)))

#define SUN4I_BACKEND_LAYFB_H4ADD_REG		0x860
#define SUN4I_BACKEND_LAYFB_H4ADD_MSK(l)		GENMASK(3 + ((l) * 8), (l) * 8)
#define SUN4I_BACKEND_LAYFB_H4ADD(l, val)		((val) << ((l) * 8))

#define SUN4I_BACKEND_REGBUFFCTL_REG		0x870
#define SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS		BIT(1)
#define SUN4I_BACKEND_REGBUFFCTL_LOADCTL		BIT(0)

#define SUN4I_BACKEND_CKMAX_REG			0x880
#define SUN4I_BACKEND_CKMIN_REG			0x884
#define SUN4I_BACKEND_CKCFG_REG			0x888
#define SUN4I_BACKEND_ATTCTL_REG0(l)		(0x890 + (0x4 * (l)))
#define SUN4I_BACKEND_ATTCTL_REG0_LAY_PIPESEL_MASK	BIT(15)
#define SUN4I_BACKEND_ATTCTL_REG0_LAY_PIPESEL(x)		((x) << 15)
#define SUN4I_BACKEND_ATTCTL_REG0_LAY_PRISEL_MASK	GENMASK(11, 10)
#define SUN4I_BACKEND_ATTCTL_REG0_LAY_PRISEL(x)			((x) << 10)
#define SUN4I_BACKEND_ATTCTL_REG0_LAY_VDOEN		BIT(1)

#define SUN4I_BACKEND_ATTCTL_REG1(l)		(0x8a0 + (0x4 * (l)))
#define SUN4I_BACKEND_ATTCTL_REG1_LAY_HSCAFCT		GENMASK(15, 14)
#define SUN4I_BACKEND_ATTCTL_REG1_LAY_WSCAFCT		GENMASK(13, 12)
#define SUN4I_BACKEND_ATTCTL_REG1_LAY_FBFMT		GENMASK(11, 8)
#define SUN4I_BACKEND_LAY_FBFMT_1BPP				(0 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_2BPP				(1 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_4BPP				(2 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_8BPP				(3 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_RGB655				(4 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_RGB565				(5 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_RGB556				(6 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_ARGB1555			(7 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_RGBA5551			(8 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_XRGB8888			(9 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_ARGB8888			(10 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_RGB888				(11 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_ARGB4444			(12 << 8)
#define SUN4I_BACKEND_LAY_FBFMT_RGBA4444			(13 << 8)

#define SUN4I_BACKEND_DLCDPCTL_REG		0x8b0
#define SUN4I_BACKEND_DLCDPFRMBUF_ADDRCTL_REG	0x8b4
#define SUN4I_BACKEND_DLCDPCOOR_REG0		0x8b8
#define SUN4I_BACKEND_DLCDPCOOR_REG1		0x8bc

#define SUN4I_BACKEND_INT_EN_REG		0x8c0
#define SUN4I_BACKEND_INT_FLAG_REG		0x8c4
#define SUN4I_BACKEND_REG_LOAD_FINISHED			BIT(1)

#define SUN4I_BACKEND_HWCCTL_REG		0x8d8
#define SUN4I_BACKEND_HWCFBCTL_REG		0x8e0
#define SUN4I_BACKEND_WBCTL_REG			0x8f0
#define SUN4I_BACKEND_WBADD_REG			0x8f4
#define SUN4I_BACKEND_WBLINEWIDTH_REG		0x8f8
#define SUN4I_BACKEND_SPREN_REG			0x900
#define SUN4I_BACKEND_SPRFMTCTL_REG		0x908
#define SUN4I_BACKEND_SPRALPHACTL_REG		0x90c
#define SUN4I_BACKEND_IYUVCTL_REG		0x920
#define SUN4I_BACKEND_IYUVADD_REG(c)		(0x930 + (0x4 * (c)))

#define SUN4I_BACKEND_IYUVLINEWIDTH_REG(c)	(0x940 + (0x4 * (c)))

#define SUN4I_BACKEND_YGCOEF_REG(c)		(0x950 + (0x4 * (c)))
#define SUN4I_BACKEND_YGCONS_REG		0x95c
#define SUN4I_BACKEND_URCOEF_REG(c)		(0x960 + (0x4 * (c)))
#define SUN4I_BACKEND_URCONS_REG		0x96c
#define SUN4I_BACKEND_VBCOEF_REG(c)		(0x970 + (0x4 * (c)))
#define SUN4I_BACKEND_VBCONS_REG		0x97c
#define SUN4I_BACKEND_KSCTL_REG			0x980
#define SUN4I_BACKEND_KSBKCOLOR_REG		0x984
#define SUN4I_BACKEND_KSFSTLINEWIDTH_REG	0x988
#define SUN4I_BACKEND_KSVSCAFCT_REG		0x98c
#define SUN4I_BACKEND_KSHSCACOEF_REG(x)		(0x9a0 + (0x4 * (x)))
#define SUN4I_BACKEND_OCCTL_REG			0x9c0
#define SUN4I_BACKEND_OCCTL_ENABLE			BIT(0)

#define SUN4I_BACKEND_OCRCOEF_REG(x)		(0x9d0 + (0x4 * (x)))
#define SUN4I_BACKEND_OCRCONS_REG		0x9dc
#define SUN4I_BACKEND_OCGCOEF_REG(x)		(0x9e0 + (0x4 * (x)))
#define SUN4I_BACKEND_OCGCONS_REG		0x9ec
#define SUN4I_BACKEND_OCBCOEF_REG(x)		(0x9f0 + (0x4 * (x)))
#define SUN4I_BACKEND_OCBCONS_REG		0x9fc
#define SUN4I_BACKEND_SPRCOORCTL_REG(s)		(0xa00 + (0x4 * (s)))
#define SUN4I_BACKEND_SPRATTCTL_REG(s)		(0xb00 + (0x4 * (s)))
#define SUN4I_BACKEND_SPRADD_REG(s)		(0xc00 + (0x4 * (s)))
#define SUN4I_BACKEND_SPRLINEWIDTH_REG(s)	(0xd00 + (0x4 * (s)))

#define SUN4I_BACKEND_SPRPALTAB_OFF		0x4000
#define SUN4I_BACKEND_GAMMATAB_OFF		0x4400
#define SUN4I_BACKEND_HWCPATTERN_OFF		0x4800
#define SUN4I_BACKEND_HWCCOLORTAB_OFF		0x4c00
#define SUN4I_BACKEND_PIPE_OFF(p)		(0x5000 + (0x400 * (p)))

#define SUN4I_BACKEND_NUM_FRONTEND_LAYERS	1

struct sun4i_backend {
	struct sunxi_engine	engine;
	struct sun4i_frontend	*frontend;

	struct reset_control	*reset;

	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct clk		*ram_clk;

	struct clk		*sat_clk;
	struct reset_control	*sat_reset;

	/* Protects against races in the frontend teardown */
	spinlock_t		frontend_lock;
	bool			frontend_teardown;
};

static inline struct sun4i_backend *
engine_to_sun4i_backend(struct sunxi_engine *engine)
{
	return container_of(engine, struct sun4i_backend, engine);
}

void sun4i_backend_layer_enable(struct sun4i_backend *backend,
				int layer, bool enable);
int sun4i_backend_update_layer_coord(struct sun4i_backend *backend,
				     int layer, struct drm_plane *plane);
int sun4i_backend_update_layer_formats(struct sun4i_backend *backend,
				       int layer, struct drm_plane *plane);
int sun4i_backend_update_layer_buffer(struct sun4i_backend *backend,
				      int layer, struct drm_plane *plane);
int sun4i_backend_update_layer_frontend(struct sun4i_backend *backend,
					int layer, uint32_t in_fmt);

#endif /* _SUN4I_BACKEND_H_ */
