/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <media/v4l2-common.h>
#include "regs.h"

void disable_dcrop(struct rkisp1_stream *stream, bool async)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *dc_ctrl_addr = base + stream->config->dual_crop.ctrl;
	u32 dc_ctrl = readl(dc_ctrl_addr);
	u32 mask = ~(stream->config->dual_crop.yuvmode_mask |
			stream->config->dual_crop.rawmode_mask);
	u32 val = dc_ctrl & mask;

	if (async)
		val |= CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		val |= CIF_DUAL_CROP_CFG_UPD;
	writel(val, dc_ctrl_addr);
}

void config_dcrop(struct rkisp1_stream *stream, struct v4l2_rect *rect, bool async)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *dc_ctrl_addr = base + stream->config->dual_crop.ctrl;
	u32 dc_ctrl = readl(dc_ctrl_addr);

	writel(rect->left, base + stream->config->dual_crop.h_offset);
	writel(rect->top, base + stream->config->dual_crop.v_offset);
	writel(rect->width, base + stream->config->dual_crop.h_size);
	writel(rect->height, base + stream->config->dual_crop.v_size);
	dc_ctrl |= stream->config->dual_crop.yuvmode_mask;
	if (async)
		dc_ctrl |= CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= CIF_DUAL_CROP_CFG_UPD;
	writel(dc_ctrl, dc_ctrl_addr);
}

void dump_rsz_regs(struct rkisp1_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

	pr_info("RSZ_CTRL 0x%08x/0x%08x\n"
			"RSZ_SCALE_HY %d/%d\n"
			"RSZ_SCALE_HCB %d/%d\n"
			"RSZ_SCALE_HCR %d/%d\n"
			"RSZ_SCALE_VY %d/%d\n"
			"RSZ_SCALE_VC %d/%d\n"
			"RSZ_PHASE_HY %d/%d\n"
			"RSZ_PHASE_HC %d/%d\n"
			"RSZ_PHASE_VY %d/%d\n"
			"RSZ_PHASE_VC %d/%d\n",
			readl(base + stream->config->rsz.ctrl),
			readl(base + stream->config->rsz.ctrl_shd),
			readl(base + stream->config->rsz.scale_hy),
			readl(base + stream->config->rsz.scale_hy_shd),
			readl(base + stream->config->rsz.scale_hcb),
			readl(base + stream->config->rsz.scale_hcb_shd),
			readl(base + stream->config->rsz.scale_hcr),
			readl(base + stream->config->rsz.scale_hcr_shd),
			readl(base + stream->config->rsz.scale_vy),
			readl(base + stream->config->rsz.scale_vy_shd),
			readl(base + stream->config->rsz.scale_vc),
			readl(base + stream->config->rsz.scale_vc_shd),
			readl(base + stream->config->rsz.phase_hy),
			readl(base + stream->config->rsz.phase_hy_shd),
			readl(base + stream->config->rsz.phase_hc),
			readl(base + stream->config->rsz.phase_hc_shd),
			readl(base + stream->config->rsz.phase_vy),
			readl(base + stream->config->rsz.phase_vy_shd),
			readl(base + stream->config->rsz.phase_vc),
			readl(base + stream->config->rsz.phase_vc_shd));
}

static void update_rsz_shadow(struct rkisp1_stream *stream, bool async)
{
	void *addr = stream->ispdev->base_addr + stream->config->rsz.ctrl;
	u32 ctrl_cfg = readl(addr);

	if (async)
		writel(CIF_RSZ_CTRL_CFG_UPD_AUTO | ctrl_cfg, addr);
	else
		writel(CIF_RSZ_CTRL_CFG_UPD | ctrl_cfg, addr);
}

static void set_scale(struct rkisp1_stream *stream, struct v4l2_rect *in_y,
		struct v4l2_rect *in_c, struct v4l2_rect *out_y,
		struct v4l2_rect *out_c)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *scale_hy_addr = base + stream->config->rsz.scale_hy;
	void __iomem *scale_hcr_addr = base + stream->config->rsz.scale_hcr;
	void __iomem *scale_hcb_addr = base + stream->config->rsz.scale_hcb;
	void __iomem *scale_vy_addr = base + stream->config->rsz.scale_vy;
	void __iomem *scale_vc_addr = base + stream->config->rsz.scale_vc;
	void __iomem *rsz_ctrl_addr = base + stream->config->rsz.ctrl;
	u32 scale_hy, scale_hc, scale_vy, scale_vc, rsz_ctrl = 0;

	if (in_y->width < out_y->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE |
				CIF_RSZ_CTRL_SCALE_HY_UP;
		scale_hy = ((in_y->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_y->width - 1);
		writel(scale_hy, scale_hy_addr);
	} else if (in_y->width > out_y->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		scale_hy = ((out_y->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_y->width - 1) + 1;
		writel(scale_hy, scale_hy_addr);
	}
	if (in_c->width < out_c->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE |
				CIF_RSZ_CTRL_SCALE_HC_UP;
		scale_hc = ((in_c->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_c->width - 1);
		writel(scale_hc, scale_hcb_addr);
		writel(scale_hc, scale_hcr_addr);
	} else if (in_c->width > out_c->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		scale_hc = ((out_c->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_c->width - 1) + 1;
		writel(scale_hc, scale_hcb_addr);
		writel(scale_hc, scale_hcr_addr);
	}

	if (in_y->height < out_y->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE |
				CIF_RSZ_CTRL_SCALE_VY_UP;
		scale_vy = ((in_y->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_y->height - 1);
		writel(scale_vy, scale_vy_addr);
	} else if (in_y->height > out_y->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		scale_vy = ((out_y->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_y->height - 1) + 1;
		writel(scale_vy, scale_vy_addr);
	}

	if (in_c->height < out_c->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE |
				CIF_RSZ_CTRL_SCALE_VC_UP;
		scale_vc = ((in_c->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_c->height - 1);
		writel(scale_vc, scale_vc_addr);
	} else if (in_c->height > out_c->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		scale_vc = ((out_c->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_c->height - 1) + 1;
		writel(scale_vc, scale_vc_addr);
	}

	writel(rsz_ctrl, rsz_ctrl_addr);
}

void config_rsz(struct rkisp1_stream *stream, struct v4l2_rect *in_y,
	struct v4l2_rect *in_c, struct v4l2_rect *out_y,
	struct v4l2_rect *out_c, bool async)
{
	int i = 0;

	/* No phase offset */
	writel(0, stream->ispdev->base_addr + stream->config->rsz.phase_hy);
	writel(0, stream->ispdev->base_addr + stream->config->rsz.phase_hc);
	writel(0, stream->ispdev->base_addr + stream->config->rsz.phase_vy);
	writel(0, stream->ispdev->base_addr + stream->config->rsz.phase_vc);

	/* Linear interpolation */
	for (i = 0; i < 64; i++) {
		writel(i, stream->ispdev->base_addr + stream->config->rsz.scale_lut_addr);
		writel(i, stream->ispdev->base_addr + stream->config->rsz.scale_lut);
	}

	set_scale(stream, in_y, in_c, out_y, out_c);

	update_rsz_shadow(stream, async);
}

void disable_rsz(struct rkisp1_stream *stream, bool async)
{
	writel(0, stream->ispdev->base_addr + stream->config->rsz.ctrl);

	if (!async)
		update_rsz_shadow(stream, async);
}

void config_mi_ctrl(struct rkisp1_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	void __iomem *addr = base + CIF_MI_CTRL;
	u32 reg;

	reg = readl(addr) & ~GENMASK(17, 16);
	writel(reg | CIF_MI_CTRL_BURST_LEN_LUM_64, addr);
	reg = readl(addr) & ~GENMASK(19, 18);
	writel(reg | CIF_MI_CTRL_BURST_LEN_CHROM_64, addr);
	reg = readl(addr);
	writel(reg | CIF_MI_CTRL_INIT_BASE_EN, addr);
	reg = readl(addr);
	writel(reg | CIF_MI_CTRL_INIT_OFFSET_EN, addr);
}

bool mp_is_stream_stopped(void __iomem *base)
{
	int en;

	en = CIF_MI_CTRL_SHD_MP_IN_ENABLED | CIF_MI_CTRL_SHD_RAW_OUT_ENABLED;
	return !(readl(base + CIF_MI_CTRL_SHD) & en);
}

bool sp_is_stream_stopped(void __iomem *base)
{
	return !(readl(base + CIF_MI_CTRL_SHD) & CIF_MI_CTRL_SHD_SP_IN_ENABLED);
}
