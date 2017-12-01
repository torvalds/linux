/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rk618_output.h"

#define RK618_VIF0_REG0			0x0000
#define VIF_ENABLE			HIWORD_UPDATE(1, 0, 0)
#define VIF_DISABLE			HIWORD_UPDATE(0, 0, 0)
#define RK618_VIF0_REG1			0x0004
#define VIF_FRAME_VST(x)		UPDATE(x, 27, 16)
#define VIF_FRAME_HST(x)		UPDATE(x, 11, 0)
#define RK618_VIF0_REG2			0x0008
#define VIF_HS_END(x)			UPDATE(x, 23, 16)
#define VIF_HTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_VIF0_REG3			0x000c
#define VIF_HACT_END(x)			UPDATE(x, 27, 16)
#define VIF_HACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_VIF0_REG4			0x0010
#define VIF_VS_END(x)			UPDATE(x, 23, 16)
#define VIF_VTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_VIF0_REG5			0x0014
#define VIF_VACT_END(x)			UPDATE(x, 27, 16)
#define VIF_VACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_VIF1_REG0			0x0018
#define RK618_VIF1_REG1			0x001c
#define RK618_VIF1_REG2			0x0020
#define RK618_VIF1_REG3			0x0024
#define RK618_VIF1_REG4			0x0028
#define RK618_VIF1_REG5			0x002c

void rk618_vif_enable(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_VIF0_REG0, VIF_ENABLE);
}
EXPORT_SYMBOL(rk618_vif_enable);

void rk618_vif_disable(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_VIF0_REG0, VIF_DISABLE);
}
EXPORT_SYMBOL(rk618_vif_disable);

void rk618_vif_configure(struct rk618 *rk618,
			 const struct drm_display_mode *mode)
{
	struct videomode vm;
	u32 vif_frame_vst, vif_frame_hst;
	u32 vif_hs_end, vif_htotal, vif_hact_end, vif_hact_st;
	u32 vif_vs_end, vif_vtotal, vif_vact_end, vif_vact_st;

	drm_display_mode_to_videomode(mode, &vm);

	/* XXX */
	vif_frame_vst = 1;
	vif_frame_hst = 207;

	vif_hs_end = vm.hsync_len;
	vif_htotal = vm.hsync_len + vm.hback_porch + vm.hfront_porch +
		     vm.hactive;
	vif_hact_end = vm.hsync_len + vm.hback_porch + vm.hactive;
	vif_hact_st = vm.hsync_len + vm.hback_porch;
	vif_vs_end = vm.vsync_len;
	vif_vtotal = vm.vsync_len + vm.vback_porch + vm.vfront_porch +
		     vm.vactive;
	vif_vact_end = vm.vsync_len + vm.vback_porch + vm.vactive;
	vif_vact_st = vm.vsync_len + vm.vback_porch;

	regmap_write(rk618->regmap, RK618_VIF0_REG1,
		     VIF_FRAME_VST(vif_frame_vst) |
		     VIF_FRAME_HST(vif_frame_hst));
	regmap_write(rk618->regmap, RK618_VIF0_REG2,
		     VIF_HS_END(vif_hs_end) | VIF_HTOTAL(vif_htotal));
	regmap_write(rk618->regmap, RK618_VIF0_REG3,
		     VIF_HACT_END(vif_hact_end) | VIF_HACT_ST(vif_hact_st));
	regmap_write(rk618->regmap, RK618_VIF0_REG4,
		     VIF_VS_END(vif_vs_end) | VIF_VTOTAL(vif_vtotal));
	regmap_write(rk618->regmap, RK618_VIF0_REG5,
		     VIF_VACT_END(vif_vact_end) | VIF_VACT_ST(vif_vact_st));
	regmap_write(rk618->regmap, RK618_IO_CON0,
		     VIF0_SYNC_MODE_ENABLE);
}
EXPORT_SYMBOL(rk618_vif_configure);
