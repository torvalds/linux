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
#include <linux/rk-camera-module.h>
#include "regs.h"

void rkisp_disable_dcrop(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 mask = stream->config->dual_crop.yuvmode_mask |
			stream->config->dual_crop.rawmode_mask;
	u32 val = CIF_DUAL_CROP_CFG_UPD;

	if (async && dev->hw_dev->is_single)
		val = CIF_DUAL_CROP_GEN_CFG_UPD;
	rkisp_unite_set_bits(dev, stream->config->dual_crop.ctrl, mask, val, false);
}

void rkisp_config_dcrop(struct rkisp_stream *stream,
			struct v4l2_rect *rect, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 val = stream->config->dual_crop.yuvmode_mask;
	bool is_unite = !!dev->hw_dev->unite;
	struct v4l2_rect tmp = *rect;
	u32 reg;

	if (is_unite) {
		tmp.width /= 2;
		if (stream->id == RKISP_STREAM_FBC)
			tmp.width &= ~0xf;
	}
	reg = stream->config->dual_crop.h_offset;
	rkisp_write(dev, reg, tmp.left, false);
	reg = stream->config->dual_crop.h_size;
	rkisp_write(dev, reg, tmp.width, false);

	reg = stream->config->dual_crop.v_offset;
	rkisp_unite_write(dev, reg, tmp.top, false);
	reg = stream->config->dual_crop.v_size;
	rkisp_unite_write(dev, reg, tmp.height, false);

	if (async && dev->hw_dev->is_single)
		val |= CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		val |= CIF_DUAL_CROP_CFG_UPD;
	if (is_unite) {
		u32 right_w, left_w = tmp.width;

		reg = stream->config->dual_crop.h_offset;
		rkisp_next_write(dev, reg, RKMOUDLE_UNITE_EXTEND_PIXEL, false);
		reg = stream->config->dual_crop.h_size;
		right_w = rect->width - left_w;
		rkisp_next_write(dev, reg, right_w, false);
		reg = stream->config->dual_crop.ctrl;
		rkisp_next_set_bits(dev, reg, 0, val, false);
		/* output with scale */
		if (stream->out_fmt.width < rect->width) {
			left_w += RKMOUDLE_UNITE_EXTEND_PIXEL;
			reg = stream->config->dual_crop.h_size;
			rkisp_write(dev, reg, left_w, false);
		}
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "left dcrop (%d, %d) %dx%d\n",
			 tmp.left, tmp.top, left_w, tmp.height);
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "right dcrop (%d, %d) %dx%d\n",
			 RKMOUDLE_UNITE_EXTEND_PIXEL, tmp.top, right_w, tmp.height);
	}
	if (val) {
		reg = stream->config->dual_crop.ctrl;
		rkisp_set_bits(dev, reg, 0, val, false);
	}
}

void rkisp_dump_rsz_regs(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;

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
		rkisp_read(dev, stream->config->rsz.ctrl, false),
		rkisp_read(dev, stream->config->rsz.ctrl_shd, true),
		rkisp_read(dev, stream->config->rsz.scale_hy, false),
		rkisp_read(dev, stream->config->rsz.scale_hy_shd, true),
		rkisp_read(dev, stream->config->rsz.scale_hcb, false),
		rkisp_read(dev, stream->config->rsz.scale_hcb_shd, true),
		rkisp_read(dev, stream->config->rsz.scale_hcr, false),
		rkisp_read(dev, stream->config->rsz.scale_hcr_shd, true),
		rkisp_read(dev, stream->config->rsz.scale_vy, false),
		rkisp_read(dev, stream->config->rsz.scale_vy_shd, true),
		rkisp_read(dev, stream->config->rsz.scale_vc, false),
		rkisp_read(dev, stream->config->rsz.scale_vc_shd, true),
		rkisp_read(dev, stream->config->rsz.phase_hy, false),
		rkisp_read(dev, stream->config->rsz.phase_hy_shd, true),
		rkisp_read(dev, stream->config->rsz.phase_hc, false),
		rkisp_read(dev, stream->config->rsz.phase_hc_shd, true),
		rkisp_read(dev, stream->config->rsz.phase_vy, false),
		rkisp_read(dev, stream->config->rsz.phase_vy_shd, true),
		rkisp_read(dev, stream->config->rsz.phase_vc, false),
		rkisp_read(dev, stream->config->rsz.phase_vc_shd, true));
}

static void update_rsz_shadow(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 val = CIF_RSZ_CTRL_CFG_UPD;

	if (async && dev->hw_dev->is_single)
		val = CIF_RSZ_CTRL_CFG_UPD_AUTO;
	rkisp_unite_set_bits(dev, stream->config->rsz.ctrl, 0, val, false);
}

static void set_scale(struct rkisp_stream *stream, struct v4l2_rect *in_y,
		struct v4l2_rect *in_c, struct v4l2_rect *out_y,
		struct v4l2_rect *out_c)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 scale_hy_addr = stream->config->rsz.scale_hy;
	u32 scale_hcr_addr = stream->config->rsz.scale_hcr;
	u32 scale_hcb_addr = stream->config->rsz.scale_hcb;
	u32 scale_vy_addr = stream->config->rsz.scale_vy;
	u32 scale_vc_addr = stream->config->rsz.scale_vc;
	u32 rsz_ctrl_addr = stream->config->rsz.ctrl;
	u32 scale_hy = 1, scale_hc = 1, scale_vy = 1, scale_vc = 1;
	u32 rsz_ctrl = 0;

	if (in_y->width < out_y->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE |
				CIF_RSZ_CTRL_SCALE_HY_UP;
		scale_hy = ((in_y->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_y->width - 1);
		rkisp_write(dev, scale_hy_addr, scale_hy, false);
	} else if (in_y->width > out_y->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		scale_hy = ((out_y->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_y->width - 1) + 1;
		rkisp_write(dev, scale_hy_addr, scale_hy, false);
	}
	if (in_c->width < out_c->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE |
				CIF_RSZ_CTRL_SCALE_HC_UP;
		scale_hc = ((in_c->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_c->width - 1);
		rkisp_write(dev, scale_hcb_addr, scale_hc, false);
		rkisp_write(dev, scale_hcr_addr, scale_hc, false);
	} else if (in_c->width > out_c->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		scale_hc = ((out_c->width - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_c->width - 1) + 1;
		rkisp_write(dev, scale_hcb_addr, scale_hc, false);
		rkisp_write(dev, scale_hcr_addr, scale_hc, false);
	}

	if (in_y->height < out_y->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE |
				CIF_RSZ_CTRL_SCALE_VY_UP;
		scale_vy = ((in_y->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_y->height - 1);
		rkisp_write(dev, scale_vy_addr, scale_vy, false);
	} else if (in_y->height > out_y->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		scale_vy = ((out_y->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_y->height - 1) + 1;
		rkisp_write(dev, scale_vy_addr, scale_vy, false);
	}

	if (in_c->height < out_c->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE |
				CIF_RSZ_CTRL_SCALE_VC_UP;
		scale_vc = ((in_c->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_c->height - 1);
		rkisp_write(dev, scale_vc_addr, scale_vc, false);
	} else if (in_c->height > out_c->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		scale_vc = ((out_c->height - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_c->height - 1) + 1;
		rkisp_write(dev, scale_vc_addr, scale_vc, false);
	}

	if (dev->hw_dev->unite) {
		u32 hy_size_reg, hc_size_reg, hy_offs_mi_reg, hc_offs_mi_reg, in_crop_offs_reg;
		u32 isp_in_w = in_y->width / 2 + RKMOUDLE_UNITE_EXTEND_PIXEL;
		u32 scl_w = out_y->width / 2;
		u32 left_y = scale_hy == 1 ? scl_w : DIV_ROUND_UP(scl_w * 65536, scale_hy);
		u32 left_c = scale_hc == 1 ? scl_w / 2 : DIV_ROUND_UP(scl_w * 65536 / 2, scale_hc);
		u32 phase_src_y = left_y * scale_hy;
		u32 phase_dst_y = scl_w * 65536;
		u32 phase_left_y = scale_hy == 1 ? 0 : scale_hy - (phase_src_y - phase_dst_y);
		u32 phase_src_c = left_c * scale_hc;
		u32 phase_dst_c = scl_w * 65536 / 2;
		u32 phase_left_c = scale_hc == 1 ? 0 : scale_hc - (phase_src_c - phase_dst_c);
		u32 right_y = phase_left_y ? in_y->width - (left_y - 1) : in_y->width - left_y;
		u32 right_c = phase_left_c ? in_y->width - (left_c - 1) * 2 : in_y->width - left_c * 2;
		u32 right_crop_y = isp_in_w - right_y;
		u32 right_crop_c = isp_in_w - right_c;
		u32 extend = RKMOUDLE_UNITE_EXTEND_PIXEL;
		u32 right_scl_in_y;
		u32 right_scl_in_c;

		switch (stream->id) {
		case RKISP_STREAM_MP:
			hy_size_reg = ISP3X_MAIN_RESIZE_HY_SIZE;
			hc_size_reg = ISP3X_MAIN_RESIZE_HC_SIZE;
			hy_offs_mi_reg = ISP3X_MAIN_RESIZE_HY_OFFS_MI;
			hc_offs_mi_reg = ISP3X_MAIN_RESIZE_HC_OFFS_MI;
			in_crop_offs_reg = ISP3X_MAIN_RESIZE_IN_CROP_OFFSET;
			break;
		case RKISP_STREAM_SP:
			hy_size_reg = ISP3X_SELF_RESIZE_HY_SIZE;
			hc_size_reg = ISP3X_SELF_RESIZE_HC_SIZE;
			hy_offs_mi_reg = ISP3X_SELF_RESIZE_HY_OFFS_MI;
			hc_offs_mi_reg = ISP3X_SELF_RESIZE_HC_OFFS_MI;
			in_crop_offs_reg = ISP3X_SELF_RESIZE_IN_CROP_OFFSET;
			break;
		case RKISP_STREAM_BP:
			hy_size_reg = ISP32_BP_RESIZE_HY_SIZE;
			hc_size_reg = ISP32_BP_RESIZE_HC_SIZE;
			hy_offs_mi_reg = ISP32_BP_RESIZE_HY_OFFS_MI;
			hc_offs_mi_reg = ISP32_BP_RESIZE_HC_OFFS_MI;
			in_crop_offs_reg = ISP32_BP_RESIZE_IN_CROP_OFFSET;
			break;
		default:
			v4l2_warn(&dev->v4l2_dev, "%s no support unite for stream:%d\n",
				  __func__, stream->id);
			return;
		}

		if (right_crop_y < RKMOUDLE_UNITE_EXTEND_PIXEL) {
			u32 reg;

			extend = right_crop_y & ~0x1;
			reg = stream->config->dual_crop.h_offset;
			rkisp_next_write(dev, reg, extend, false);
			reg = stream->config->dual_crop.h_size;
			rkisp_next_write(dev, reg, isp_in_w - extend, false);
			reg = stream->config->dual_crop.ctrl;
			rkisp_next_write(dev, reg, rkisp_next_read_reg_cache(dev, reg), false);
		}
		right_scl_in_y = right_crop_y - extend;
		right_scl_in_c = right_crop_c - extend;

		/* left isp */
		rkisp_write(dev, hy_size_reg, scl_w, false);
		rkisp_write(dev, hc_size_reg, scl_w, false);
		rkisp_write(dev, hy_offs_mi_reg, 0, false);
		rkisp_write(dev, hc_offs_mi_reg, 0, false);
		rkisp_write(dev, in_crop_offs_reg, 0, false);

		/* right isp */
		rkisp_next_write(dev, hy_size_reg, scl_w, false);
		rkisp_next_write(dev, hc_size_reg, scl_w, false);
		rkisp_next_write(dev, scale_hy_addr, scale_hy, false);
		rkisp_next_write(dev, scale_hcb_addr, scale_hc, false);
		rkisp_next_write(dev, scale_hcr_addr, scale_hc, false);
		rkisp_next_write(dev, scale_vy_addr, scale_vy, false);
		rkisp_next_write(dev, scale_vc_addr, scale_vc, false);
		rkisp_next_write(dev, stream->config->rsz.phase_hy, phase_left_y, false);
		rkisp_next_write(dev, stream->config->rsz.phase_hc, phase_left_c, false);
		rkisp_next_write(dev, stream->config->rsz.phase_vy, 0, false);
		rkisp_next_write(dev, stream->config->rsz.phase_vc, 0, false);
		rkisp_next_write(dev, hy_offs_mi_reg, scl_w & 15, false);
		rkisp_next_write(dev, hc_offs_mi_reg, scl_w & 15, false);
		rkisp_next_write(dev, in_crop_offs_reg,
				 right_scl_in_c << 4 | right_scl_in_y, false);

		rsz_ctrl |= ISP3X_SCL_CLIP_EN;
		rkisp_next_write(dev, rsz_ctrl_addr,
				 rsz_ctrl | ISP3X_SCL_HPHASE_EN | ISP3X_SCL_IN_CLIP_EN, false);
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "scl:%dx%d, scl factor[hy:%d hc:%d vy:%d vc:%d]\n",
			 scl_w, out_y->height, scale_hy, scale_hc, scale_vy, scale_vc);
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "scl_left size[y:%d c:%d] phase[y:%d c:%d]\n",
			 left_y, left_c, phase_left_y, phase_left_c);
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "scl_right size[y:%d c:%d] offs_mi[y:%d c:%d] in_crop[y:%d c:%d]\n",
			 right_y, right_c, scl_w & 15, scl_w & 15, right_scl_in_y, right_scl_in_c);
	}
	rkisp_write(dev, rsz_ctrl_addr, rsz_ctrl, false);
}

static void set_bilinear_scale(struct rkisp_stream *stream, struct v4l2_rect *in_y,
				struct v4l2_rect *in_c, struct v4l2_rect *out_y,
				struct v4l2_rect *out_c, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 rsz_ctrl = 0, val, hy, hc;
	bool is_avg = false;

	rkisp_write(dev, ISP32_SELF_SCALE_HY_OFFS, 0, true);
	rkisp_write(dev, ISP32_SELF_SCALE_HC_OFFS, 0, true);
	rkisp_write(dev, ISP32_SELF_SCALE_PHASE_HY, 0, true);
	rkisp_write(dev, ISP32_SELF_SCALE_PHASE_HC, 0, true);
	rkisp_write(dev, ISP32_SELF_SCALE_PHASE_VY, 0, true);
	rkisp_write(dev, ISP32_SELF_SCALE_PHASE_VC, 0, true);

	val = in_y->width | in_y->height << 16;
	rkisp_write(dev, ISP32_SELF_SCALE_SRC_SIZE, val, false);
	val = out_y->width | out_y->height << 16;
	rkisp_write(dev, ISP32_SELF_SCALE_DST_SIZE, val, false);

	if (in_y->width != out_y->width) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE | CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		if (is_avg) {
			hy = ((out_y->width - 1) * ISP32_SCALE_AVE_FACTOR) / (in_y->width - 1) + 1;
			hc = ((out_c->width - 1) * ISP32_SCALE_AVE_FACTOR) / (in_c->width - 1) + 1;
			rsz_ctrl |= ISP32_SCALE_AVG_H_EN;
		} else {
			hy = ((in_y->width - 1) * ISP32_SCALE_BIL_FACTOR) / (out_y->width - 1);
			hc = ((in_c->width - 1) * ISP32_SCALE_BIL_FACTOR) / (out_c->width - 1);
		}
		rkisp_write(dev, ISP32_SELF_SCALE_HY_FAC, hy, false);
		rkisp_write(dev, ISP32_SELF_SCALE_HC_FAC, hc, false);
	}

	if (in_y->height != out_y->height) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE | CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		if (is_avg) {
			val = ((out_y->height - 1) * ISP32_SCALE_AVE_FACTOR) / (in_y->height - 1) + 1;
			rsz_ctrl |= ISP32_SCALE_AVG_V_EN;
		} else {
			val = ((in_y->height - 1) * ISP32_SCALE_BIL_FACTOR) / (out_y->height - 1);
		}
		rkisp_write(dev, ISP32_SELF_SCALE_VY_FAC, val, false);
		rkisp_write(dev, ISP32_SELF_SCALE_VC_FAC, val, false);
	}

	rkisp_write(dev, ISP32_SELF_SCALE_CTRL, rsz_ctrl, false);
	val = ISP32_SCALE_FORCE_UPD;
	if (async && dev->hw_dev->is_single)
		val = ISP32_SCALE_GEN_UPD;
	rkisp_write(dev, ISP32_SELF_SCALE_UPDATE, val, true);
}

void rkisp_config_rsz(struct rkisp_stream *stream, struct v4l2_rect *in_y,
	struct v4l2_rect *in_c, struct v4l2_rect *out_y,
	struct v4l2_rect *out_c, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	int i = 0;

	if (dev->isp_ver == ISP_V32_L && stream->id == RKISP_STREAM_SP) {
		set_bilinear_scale(stream, in_y, in_c, out_y, out_c, async);
		return;
	}

	/* No phase offset */
	rkisp_write(dev, stream->config->rsz.phase_hy, 0, true);
	rkisp_write(dev, stream->config->rsz.phase_hc, 0, true);
	rkisp_write(dev, stream->config->rsz.phase_vy, 0, true);
	rkisp_write(dev, stream->config->rsz.phase_vc, 0, true);

	/* Linear interpolation */
	for (i = 0; i < 64; i++) {
		rkisp_unite_write(dev, stream->config->rsz.scale_lut_addr, i, true);
		rkisp_unite_write(dev, stream->config->rsz.scale_lut, i, true);
	}

	set_scale(stream, in_y, in_c, out_y, out_c);

	update_rsz_shadow(stream, async);
}

void rkisp_disable_rsz(struct rkisp_stream *stream, bool async)
{
	rkisp_unite_write(stream->ispdev, stream->config->rsz.ctrl, 0, false);
	if (stream->ispdev->isp_ver == ISP_V32_L && stream->id == RKISP_STREAM_SP)
		return;
	update_rsz_shadow(stream, async);
}
