// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2018 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/debugfs.h>
#include <linux/dma-buf.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_blend.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>

#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_formats.h"
#include "dpu_hw_sspp.h"
#include "dpu_hw_util.h"
#include "dpu_trace.h"
#include "dpu_crtc.h"
#include "dpu_vbif.h"
#include "dpu_plane.h"

#define DPU_DEBUG_PLANE(pl, fmt, ...) DRM_DEBUG_ATOMIC("plane%d " fmt,\
		(pl) ? (pl)->base.base.id : -1, ##__VA_ARGS__)

#define DPU_ERROR_PLANE(pl, fmt, ...) DPU_ERROR("plane%d " fmt,\
		(pl) ? (pl)->base.base.id : -1, ##__VA_ARGS__)

#define DECIMATED_DIMENSION(dim, deci) (((dim) + ((1 << (deci)) - 1)) >> (deci))
#define PHASE_STEP_SHIFT	21
#define PHASE_STEP_UNIT_SCALE   ((int) (1 << PHASE_STEP_SHIFT))
#define PHASE_RESIDUAL		15

#define SHARP_STRENGTH_DEFAULT	32
#define SHARP_EDGE_THR_DEFAULT	112
#define SHARP_SMOOTH_THR_DEFAULT	8
#define SHARP_NOISE_THR_DEFAULT	2

#define DPU_PLANE_COLOR_FILL_FLAG	BIT(31)
#define DPU_ZPOS_MAX 255

/*
 * Default Preload Values
 */
#define DPU_QSEED3_DEFAULT_PRELOAD_H 0x4
#define DPU_QSEED3_DEFAULT_PRELOAD_V 0x3
#define DPU_QSEED4_DEFAULT_PRELOAD_V 0x2
#define DPU_QSEED4_DEFAULT_PRELOAD_H 0x4

#define DEFAULT_REFRESH_RATE	60

static const uint32_t qcom_compressed_supported_formats[] = {
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_BGR565,

	DRM_FORMAT_NV12,
	DRM_FORMAT_P010,
};

/*
 * struct dpu_plane - local dpu plane structure
 * @aspace: address space pointer
 * @csc_ptr: Points to dpu_csc_cfg structure to use for current
 * @catalog: Points to dpu catalog structure
 * @revalidate: force revalidation of all the plane properties
 */
struct dpu_plane {
	struct drm_plane base;

	enum dpu_sspp pipe;

	uint32_t color_fill;
	bool is_error;
	bool is_rt_pipe;
	const struct dpu_mdss_cfg *catalog;
};

static const uint64_t supported_format_modifiers[] = {
	DRM_FORMAT_MOD_QCOM_COMPRESSED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

#define to_dpu_plane(x) container_of(x, struct dpu_plane, base)

static struct dpu_kms *_dpu_plane_get_kms(struct drm_plane *plane)
{
	struct msm_drm_private *priv = plane->dev->dev_private;

	return to_dpu_kms(priv->kms);
}

/**
 * _dpu_plane_calc_bw - calculate bandwidth required for a plane
 * @catalog: Points to dpu catalog structure
 * @fmt: Pointer to source buffer format
 * @mode: Pointer to drm display mode
 * @pipe_cfg: Pointer to pipe configuration
 * Result: Updates calculated bandwidth in the plane state.
 * BW Equation: src_w * src_h * bpp * fps * (v_total / v_dest)
 * Prefill BW Equation: line src bytes * line_time
 */
static u64 _dpu_plane_calc_bw(const struct dpu_mdss_cfg *catalog,
	const struct dpu_format *fmt,
	const struct drm_display_mode *mode,
	struct dpu_sw_pipe_cfg *pipe_cfg)
{
	int src_width, src_height, dst_height, fps;
	u64 plane_pixel_rate, plane_bit_rate;
	u64 plane_prefill_bw;
	u64 plane_bw;
	u32 hw_latency_lines;
	u64 scale_factor;
	int vbp, vpw, vfp;

	src_width = drm_rect_width(&pipe_cfg->src_rect);
	src_height = drm_rect_height(&pipe_cfg->src_rect);
	dst_height = drm_rect_height(&pipe_cfg->dst_rect);
	fps = drm_mode_vrefresh(mode);
	vbp = mode->vtotal - mode->vsync_end;
	vpw = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	hw_latency_lines =  catalog->perf->min_prefill_lines;
	scale_factor = src_height > dst_height ?
		mult_frac(src_height, 1, dst_height) : 1;

	plane_pixel_rate = src_width * mode->vtotal * fps;
	plane_bit_rate = plane_pixel_rate * fmt->bpp;

	plane_bw = plane_bit_rate * scale_factor;

	plane_prefill_bw = plane_bw * hw_latency_lines;

	if ((vbp+vpw) > hw_latency_lines)
		do_div(plane_prefill_bw, (vbp+vpw));
	else if ((vbp+vpw+vfp) < hw_latency_lines)
		do_div(plane_prefill_bw, (vbp+vpw+vfp));
	else
		do_div(plane_prefill_bw, hw_latency_lines);


	return max(plane_bw, plane_prefill_bw);
}

/**
 * _dpu_plane_calc_clk - calculate clock required for a plane
 * @mode: Pointer to drm display mode
 * @pipe_cfg: Pointer to pipe configuration
 * Result: Updates calculated clock in the plane state.
 * Clock equation: dst_w * v_total * fps * (src_h / dst_h)
 */
static u64 _dpu_plane_calc_clk(const struct drm_display_mode *mode,
		struct dpu_sw_pipe_cfg *pipe_cfg)
{
	int dst_width, src_height, dst_height, fps;
	u64 plane_clk;

	src_height = drm_rect_height(&pipe_cfg->src_rect);
	dst_width = drm_rect_width(&pipe_cfg->dst_rect);
	dst_height = drm_rect_height(&pipe_cfg->dst_rect);
	fps = drm_mode_vrefresh(mode);

	plane_clk =
		dst_width * mode->vtotal * fps;

	if (src_height > dst_height) {
		plane_clk *= src_height;
		do_div(plane_clk, dst_height);
	}

	return plane_clk;
}

/**
 * _dpu_plane_calc_fill_level - calculate fill level of the given source format
 * @plane:		Pointer to drm plane
 * @pipe:		Pointer to software pipe
 * @lut_usage:		LUT usecase
 * @fmt:		Pointer to source buffer format
 * @src_width:		width of source buffer
 * Return: fill level corresponding to the source buffer/format or 0 if error
 */
static int _dpu_plane_calc_fill_level(struct drm_plane *plane,
		struct dpu_sw_pipe *pipe,
		enum dpu_qos_lut_usage lut_usage,
		const struct dpu_format *fmt, u32 src_width)
{
	struct dpu_plane *pdpu;
	u32 fixed_buff_size;
	u32 total_fl;

	if (!fmt || !pipe || !src_width || !fmt->bpp) {
		DPU_ERROR("invalid arguments\n");
		return 0;
	}

	if (lut_usage == DPU_QOS_LUT_USAGE_NRT)
		return 0;

	pdpu = to_dpu_plane(plane);
	fixed_buff_size = pdpu->catalog->caps->pixel_ram_size;

	/* FIXME: in multirect case account for the src_width of all the planes */

	if (fmt->fetch_planes == DPU_PLANE_PSEUDO_PLANAR) {
		if (fmt->chroma_sample == DPU_CHROMA_420) {
			/* NV12 */
			total_fl = (fixed_buff_size / 2) /
				((src_width + 32) * fmt->bpp);
		} else {
			/* non NV12 */
			total_fl = (fixed_buff_size / 2) * 2 /
				((src_width + 32) * fmt->bpp);
		}
	} else {
		if (pipe->multirect_mode == DPU_SSPP_MULTIRECT_PARALLEL) {
			total_fl = (fixed_buff_size / 2) * 2 /
				((src_width + 32) * fmt->bpp);
		} else {
			total_fl = (fixed_buff_size) * 2 /
				((src_width + 32) * fmt->bpp);
		}
	}

	DPU_DEBUG_PLANE(pdpu, "pnum:%d fmt: %4.4s w:%u fl:%u\n",
			pipe->sspp->idx - SSPP_VIG0,
			(char *)&fmt->base.pixel_format,
			src_width, total_fl);

	return total_fl;
}

/**
 * _dpu_plane_set_qos_lut - set QoS LUT of the given plane
 * @plane:		Pointer to drm plane
 * @pipe:		Pointer to software pipe
 * @fmt:		Pointer to source buffer format
 * @pipe_cfg:		Pointer to pipe configuration
 */
static void _dpu_plane_set_qos_lut(struct drm_plane *plane,
		struct dpu_sw_pipe *pipe,
		const struct dpu_format *fmt, struct dpu_sw_pipe_cfg *pipe_cfg)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_hw_qos_cfg cfg;
	u32 total_fl, lut_usage;

	if (!pdpu->is_rt_pipe) {
		lut_usage = DPU_QOS_LUT_USAGE_NRT;
	} else {
		if (fmt && DPU_FORMAT_IS_LINEAR(fmt))
			lut_usage = DPU_QOS_LUT_USAGE_LINEAR;
		else
			lut_usage = DPU_QOS_LUT_USAGE_MACROTILE;
	}

	total_fl = _dpu_plane_calc_fill_level(plane, pipe, lut_usage, fmt,
				drm_rect_width(&pipe_cfg->src_rect));

	cfg.creq_lut = _dpu_hw_get_qos_lut(&pdpu->catalog->perf->qos_lut_tbl[lut_usage], total_fl);
	cfg.danger_lut = pdpu->catalog->perf->danger_lut_tbl[lut_usage];
	cfg.safe_lut = pdpu->catalog->perf->safe_lut_tbl[lut_usage];

	if (pipe->sspp->idx != SSPP_CURSOR0 &&
	    pipe->sspp->idx != SSPP_CURSOR1 &&
	    pdpu->is_rt_pipe)
		cfg.danger_safe_en = true;

	DPU_DEBUG_PLANE(pdpu, "pnum:%d ds:%d is_rt:%d\n",
		pdpu->pipe - SSPP_VIG0,
		cfg.danger_safe_en,
		pdpu->is_rt_pipe);

	trace_dpu_perf_set_qos_luts(pipe->sspp->idx - SSPP_VIG0,
			(fmt) ? fmt->base.pixel_format : 0,
			pdpu->is_rt_pipe, total_fl, cfg.creq_lut, lut_usage);

	DPU_DEBUG_PLANE(pdpu, "pnum:%d fmt: %4.4s rt:%d fl:%u lut:0x%llx\n",
			pdpu->pipe - SSPP_VIG0,
			fmt ? (char *)&fmt->base.pixel_format : NULL,
			pdpu->is_rt_pipe, total_fl, cfg.creq_lut);

	trace_dpu_perf_set_danger_luts(pdpu->pipe - SSPP_VIG0,
			(fmt) ? fmt->base.pixel_format : 0,
			(fmt) ? fmt->fetch_mode : 0,
			cfg.danger_lut,
			cfg.safe_lut);

	DPU_DEBUG_PLANE(pdpu, "pnum:%d fmt: %4.4s mode:%d luts[0x%x, 0x%x]\n",
		pdpu->pipe - SSPP_VIG0,
		fmt ? (char *)&fmt->base.pixel_format : NULL,
		fmt ? fmt->fetch_mode : -1,
		cfg.danger_lut,
		cfg.safe_lut);

	pipe->sspp->ops.setup_qos_lut(pipe->sspp, &cfg);
}

/**
 * _dpu_plane_set_qos_ctrl - set QoS control of the given plane
 * @plane:		Pointer to drm plane
 * @pipe:		Pointer to software pipe
 * @enable:		true to enable QoS control
 */
static void _dpu_plane_set_qos_ctrl(struct drm_plane *plane,
	struct dpu_sw_pipe *pipe,
	bool enable)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);

	if (!pdpu->is_rt_pipe)
		enable = false;

	DPU_DEBUG_PLANE(pdpu, "pnum:%d ds:%d is_rt:%d\n",
		pdpu->pipe - SSPP_VIG0,
		enable,
		pdpu->is_rt_pipe);

	pipe->sspp->ops.setup_qos_ctrl(pipe->sspp,
				       enable);
}

static bool _dpu_plane_sspp_clk_force_ctrl(struct dpu_hw_sspp *sspp,
					   struct dpu_hw_mdp *mdp,
					   bool enable, bool *forced_on)
{
	if (sspp->ops.setup_clk_force_ctrl) {
		*forced_on = sspp->ops.setup_clk_force_ctrl(sspp, enable);
		return true;
	}

	if (mdp->ops.setup_clk_force_ctrl) {
		*forced_on = mdp->ops.setup_clk_force_ctrl(mdp, sspp->cap->clk_ctrl, enable);
		return true;
	}

	return false;
}

/**
 * _dpu_plane_set_ot_limit - set OT limit for the given plane
 * @plane:		Pointer to drm plane
 * @pipe:		Pointer to software pipe
 * @pipe_cfg:		Pointer to pipe configuration
 * @frame_rate:		CRTC's frame rate
 */
static void _dpu_plane_set_ot_limit(struct drm_plane *plane,
		struct dpu_sw_pipe *pipe,
		struct dpu_sw_pipe_cfg *pipe_cfg,
		int frame_rate)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_vbif_set_ot_params ot_params;
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);
	bool forced_on = false;

	memset(&ot_params, 0, sizeof(ot_params));
	ot_params.xin_id = pipe->sspp->cap->xin_id;
	ot_params.num = pipe->sspp->idx - SSPP_NONE;
	ot_params.width = drm_rect_width(&pipe_cfg->src_rect);
	ot_params.height = drm_rect_height(&pipe_cfg->src_rect);
	ot_params.is_wfd = !pdpu->is_rt_pipe;
	ot_params.frame_rate = frame_rate;
	ot_params.vbif_idx = VBIF_RT;
	ot_params.rd = true;

	if (!_dpu_plane_sspp_clk_force_ctrl(pipe->sspp, dpu_kms->hw_mdp,
					    true, &forced_on))
		return;

	dpu_vbif_set_ot_limit(dpu_kms, &ot_params);

	if (forced_on)
		_dpu_plane_sspp_clk_force_ctrl(pipe->sspp, dpu_kms->hw_mdp,
					       false, &forced_on);
}

/**
 * _dpu_plane_set_qos_remap - set vbif QoS for the given plane
 * @plane:		Pointer to drm plane
 * @pipe:		Pointer to software pipe
 */
static void _dpu_plane_set_qos_remap(struct drm_plane *plane,
		struct dpu_sw_pipe *pipe)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_vbif_set_qos_params qos_params;
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);
	bool forced_on = false;

	memset(&qos_params, 0, sizeof(qos_params));
	qos_params.vbif_idx = VBIF_RT;
	qos_params.xin_id = pipe->sspp->cap->xin_id;
	qos_params.num = pipe->sspp->idx - SSPP_VIG0;
	qos_params.is_rt = pdpu->is_rt_pipe;

	DPU_DEBUG_PLANE(pdpu, "pipe:%d vbif:%d xin:%d rt:%d\n",
			qos_params.num,
			qos_params.vbif_idx,
			qos_params.xin_id, qos_params.is_rt);

	if (!_dpu_plane_sspp_clk_force_ctrl(pipe->sspp, dpu_kms->hw_mdp,
					    true, &forced_on))
		return;

	dpu_vbif_set_qos_remap(dpu_kms, &qos_params);

	if (forced_on)
		_dpu_plane_sspp_clk_force_ctrl(pipe->sspp, dpu_kms->hw_mdp,
					       false, &forced_on);
}

static void _dpu_plane_setup_scaler3(struct dpu_hw_sspp *pipe_hw,
		uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
		struct dpu_hw_scaler3_cfg *scale_cfg,
		const struct dpu_format *fmt,
		uint32_t chroma_subsmpl_h, uint32_t chroma_subsmpl_v,
		unsigned int rotation)
{
	uint32_t i;
	bool inline_rotation = rotation & DRM_MODE_ROTATE_90;

	/*
	 * For inline rotation cases, scaler config is post-rotation,
	 * so swap the dimensions here. However, pixel extension will
	 * need pre-rotation settings.
	 */
	if (inline_rotation)
		swap(src_w, src_h);

	scale_cfg->phase_step_x[DPU_SSPP_COMP_0] =
		mult_frac((1 << PHASE_STEP_SHIFT), src_w, dst_w);
	scale_cfg->phase_step_y[DPU_SSPP_COMP_0] =
		mult_frac((1 << PHASE_STEP_SHIFT), src_h, dst_h);


	scale_cfg->phase_step_y[DPU_SSPP_COMP_1_2] =
		scale_cfg->phase_step_y[DPU_SSPP_COMP_0] / chroma_subsmpl_v;
	scale_cfg->phase_step_x[DPU_SSPP_COMP_1_2] =
		scale_cfg->phase_step_x[DPU_SSPP_COMP_0] / chroma_subsmpl_h;

	scale_cfg->phase_step_x[DPU_SSPP_COMP_2] =
		scale_cfg->phase_step_x[DPU_SSPP_COMP_1_2];
	scale_cfg->phase_step_y[DPU_SSPP_COMP_2] =
		scale_cfg->phase_step_y[DPU_SSPP_COMP_1_2];

	scale_cfg->phase_step_x[DPU_SSPP_COMP_3] =
		scale_cfg->phase_step_x[DPU_SSPP_COMP_0];
	scale_cfg->phase_step_y[DPU_SSPP_COMP_3] =
		scale_cfg->phase_step_y[DPU_SSPP_COMP_0];

	for (i = 0; i < DPU_MAX_PLANES; i++) {
		scale_cfg->src_width[i] = src_w;
		scale_cfg->src_height[i] = src_h;
		if (i == DPU_SSPP_COMP_1_2 || i == DPU_SSPP_COMP_2) {
			scale_cfg->src_width[i] /= chroma_subsmpl_h;
			scale_cfg->src_height[i] /= chroma_subsmpl_v;
		}

		if (pipe_hw->cap->sblk->scaler_blk.version >= 0x3000) {
			scale_cfg->preload_x[i] = DPU_QSEED4_DEFAULT_PRELOAD_H;
			scale_cfg->preload_y[i] = DPU_QSEED4_DEFAULT_PRELOAD_V;
		} else {
			scale_cfg->preload_x[i] = DPU_QSEED3_DEFAULT_PRELOAD_H;
			scale_cfg->preload_y[i] = DPU_QSEED3_DEFAULT_PRELOAD_V;
		}
	}
	if (!(DPU_FORMAT_IS_YUV(fmt)) && (src_h == dst_h)
		&& (src_w == dst_w))
		return;

	scale_cfg->dst_width = dst_w;
	scale_cfg->dst_height = dst_h;
	scale_cfg->y_rgb_filter_cfg = DPU_SCALE_BIL;
	scale_cfg->uv_filter_cfg = DPU_SCALE_BIL;
	scale_cfg->alpha_filter_cfg = DPU_SCALE_ALPHA_BIL;
	scale_cfg->lut_flag = 0;
	scale_cfg->blend_cfg = 1;
	scale_cfg->enable = 1;
}

static void _dpu_plane_setup_pixel_ext(struct dpu_hw_scaler3_cfg *scale_cfg,
				struct dpu_hw_pixel_ext *pixel_ext,
				uint32_t src_w, uint32_t src_h,
				uint32_t chroma_subsmpl_h, uint32_t chroma_subsmpl_v)
{
	int i;

	for (i = 0; i < DPU_MAX_PLANES; i++) {
		if (i == DPU_SSPP_COMP_1_2 || i == DPU_SSPP_COMP_2) {
			src_w /= chroma_subsmpl_h;
			src_h /= chroma_subsmpl_v;
		}

		pixel_ext->num_ext_pxls_top[i] = src_h;
		pixel_ext->num_ext_pxls_left[i] = src_w;
	}
}

static const struct dpu_csc_cfg *_dpu_plane_get_csc(struct dpu_sw_pipe *pipe,
						    const struct dpu_format *fmt)
{
	const struct dpu_csc_cfg *csc_ptr;

	if (!DPU_FORMAT_IS_YUV(fmt))
		return NULL;

	if (BIT(DPU_SSPP_CSC_10BIT) & pipe->sspp->cap->features)
		csc_ptr = &dpu_csc10_YUV2RGB_601L;
	else
		csc_ptr = &dpu_csc_YUV2RGB_601L;

	return csc_ptr;
}

static void _dpu_plane_setup_scaler(struct dpu_sw_pipe *pipe,
		const struct dpu_format *fmt, bool color_fill,
		struct dpu_sw_pipe_cfg *pipe_cfg,
		unsigned int rotation)
{
	struct dpu_hw_sspp *pipe_hw = pipe->sspp;
	const struct drm_format_info *info = drm_format_info(fmt->base.pixel_format);
	struct dpu_hw_scaler3_cfg scaler3_cfg;
	struct dpu_hw_pixel_ext pixel_ext;
	u32 src_width = drm_rect_width(&pipe_cfg->src_rect);
	u32 src_height = drm_rect_height(&pipe_cfg->src_rect);
	u32 dst_width = drm_rect_width(&pipe_cfg->dst_rect);
	u32 dst_height = drm_rect_height(&pipe_cfg->dst_rect);

	memset(&scaler3_cfg, 0, sizeof(scaler3_cfg));
	memset(&pixel_ext, 0, sizeof(pixel_ext));

	/* don't chroma subsample if decimating */
	/* update scaler. calculate default config for QSEED3 */
	_dpu_plane_setup_scaler3(pipe_hw,
			src_width,
			src_height,
			dst_width,
			dst_height,
			&scaler3_cfg, fmt,
			info->hsub, info->vsub,
			rotation);

	/* configure pixel extension based on scalar config */
	_dpu_plane_setup_pixel_ext(&scaler3_cfg, &pixel_ext,
			src_width, src_height, info->hsub, info->vsub);

	if (pipe_hw->ops.setup_pe)
		pipe_hw->ops.setup_pe(pipe_hw,
				&pixel_ext);

	/**
	 * when programmed in multirect mode, scalar block will be
	 * bypassed. Still we need to update alpha and bitwidth
	 * ONLY for RECT0
	 */
	if (pipe_hw->ops.setup_scaler &&
			pipe->multirect_index != DPU_SSPP_RECT_1)
		pipe_hw->ops.setup_scaler(pipe_hw,
				&scaler3_cfg,
				fmt);
}

static void _dpu_plane_color_fill_pipe(struct dpu_plane_state *pstate,
				       struct dpu_sw_pipe *pipe,
				       struct drm_rect *dst_rect,
				       u32 fill_color,
				       const struct dpu_format *fmt)
{
	struct dpu_sw_pipe_cfg pipe_cfg;

	/* update sspp */
	if (!pipe->sspp->ops.setup_solidfill)
		return;

	pipe->sspp->ops.setup_solidfill(pipe, fill_color);

	/* override scaler/decimation if solid fill */
	pipe_cfg.dst_rect = *dst_rect;

	pipe_cfg.src_rect.x1 = 0;
	pipe_cfg.src_rect.y1 = 0;
	pipe_cfg.src_rect.x2 =
		drm_rect_width(&pipe_cfg.dst_rect);
	pipe_cfg.src_rect.y2 =
		drm_rect_height(&pipe_cfg.dst_rect);

	if (pipe->sspp->ops.setup_format)
		pipe->sspp->ops.setup_format(pipe, fmt, DPU_SSPP_SOLID_FILL);

	if (pipe->sspp->ops.setup_rects)
		pipe->sspp->ops.setup_rects(pipe, &pipe_cfg);

	_dpu_plane_setup_scaler(pipe, fmt, true, &pipe_cfg, pstate->rotation);
}

/**
 * _dpu_plane_color_fill - enables color fill on plane
 * @pdpu:   Pointer to DPU plane object
 * @color:  RGB fill color value, [23..16] Blue, [15..8] Green, [7..0] Red
 * @alpha:  8-bit fill alpha value, 255 selects 100% alpha
 */
static void _dpu_plane_color_fill(struct dpu_plane *pdpu,
		uint32_t color, uint32_t alpha)
{
	const struct dpu_format *fmt;
	const struct drm_plane *plane = &pdpu->base;
	struct dpu_plane_state *pstate = to_dpu_plane_state(plane->state);
	u32 fill_color = (color & 0xFFFFFF) | ((alpha & 0xFF) << 24);

	DPU_DEBUG_PLANE(pdpu, "\n");

	/*
	 * select fill format to match user property expectation,
	 * h/w only supports RGB variants
	 */
	fmt = dpu_get_dpu_format(DRM_FORMAT_ABGR8888);
	/* should not happen ever */
	if (!fmt)
		return;

	/* update sspp */
	_dpu_plane_color_fill_pipe(pstate, &pstate->pipe, &pstate->pipe_cfg.dst_rect,
				   fill_color, fmt);

	if (pstate->r_pipe.sspp)
		_dpu_plane_color_fill_pipe(pstate, &pstate->r_pipe, &pstate->r_pipe_cfg.dst_rect,
					   fill_color, fmt);
}

static int dpu_plane_prepare_fb(struct drm_plane *plane,
		struct drm_plane_state *new_state)
{
	struct drm_framebuffer *fb = new_state->fb;
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_plane_state *pstate = to_dpu_plane_state(new_state);
	struct dpu_hw_fmt_layout layout;
	struct dpu_kms *kms = _dpu_plane_get_kms(&pdpu->base);
	int ret;

	if (!new_state->fb)
		return 0;

	DPU_DEBUG_PLANE(pdpu, "FB[%u]\n", fb->base.id);

	/* cache aspace */
	pstate->aspace = kms->base.aspace;

	/*
	 * TODO: Need to sort out the msm_framebuffer_prepare() call below so
	 *       we can use msm_atomic_prepare_fb() instead of doing the
	 *       implicit fence and fb prepare by hand here.
	 */
	drm_gem_plane_helper_prepare_fb(plane, new_state);

	if (pstate->aspace) {
		ret = msm_framebuffer_prepare(new_state->fb,
				pstate->aspace, pstate->needs_dirtyfb);
		if (ret) {
			DPU_ERROR("failed to prepare framebuffer\n");
			return ret;
		}
	}

	/* validate framebuffer layout before commit */
	ret = dpu_format_populate_layout(pstate->aspace,
			new_state->fb, &layout);
	if (ret) {
		DPU_ERROR_PLANE(pdpu, "failed to get format layout, %d\n", ret);
		return ret;
	}

	return 0;
}

static void dpu_plane_cleanup_fb(struct drm_plane *plane,
		struct drm_plane_state *old_state)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_plane_state *old_pstate;

	if (!old_state || !old_state->fb)
		return;

	old_pstate = to_dpu_plane_state(old_state);

	DPU_DEBUG_PLANE(pdpu, "FB[%u]\n", old_state->fb->base.id);

	msm_framebuffer_cleanup(old_state->fb, old_pstate->aspace,
				old_pstate->needs_dirtyfb);
}

static int dpu_plane_check_inline_rotation(struct dpu_plane *pdpu,
						const struct dpu_sspp_sub_blks *sblk,
						struct drm_rect src, const struct dpu_format *fmt)
{
	size_t num_formats;
	const u32 *supported_formats;

	if (!sblk->rotation_cfg) {
		DPU_ERROR("invalid rotation cfg\n");
		return -EINVAL;
	}

	if (drm_rect_width(&src) > sblk->rotation_cfg->rot_maxheight) {
		DPU_DEBUG_PLANE(pdpu, "invalid height for inline rot:%d max:%d\n",
				src.y2, sblk->rotation_cfg->rot_maxheight);
		return -EINVAL;
	}

	supported_formats = sblk->rotation_cfg->rot_format_list;
	num_formats = sblk->rotation_cfg->rot_num_formats;

	if (!DPU_FORMAT_IS_UBWC(fmt) ||
		!dpu_find_format(fmt->base.pixel_format, supported_formats, num_formats))
		return -EINVAL;

	return 0;
}

static int dpu_plane_atomic_check_pipe(struct dpu_plane *pdpu,
		struct dpu_sw_pipe *pipe,
		struct dpu_sw_pipe_cfg *pipe_cfg,
		const struct dpu_format *fmt,
		const struct drm_display_mode *mode)
{
	uint32_t min_src_size;
	struct dpu_kms *kms = _dpu_plane_get_kms(&pdpu->base);

	min_src_size = DPU_FORMAT_IS_YUV(fmt) ? 2 : 1;

	if (DPU_FORMAT_IS_YUV(fmt) &&
	    (!pipe->sspp->cap->sblk->scaler_blk.len ||
	     !pipe->sspp->cap->sblk->csc_blk.len)) {
		DPU_DEBUG_PLANE(pdpu,
				"plane doesn't have scaler/csc for yuv\n");
		return -EINVAL;
	}

	/* check src bounds */
	if (drm_rect_width(&pipe_cfg->src_rect) < min_src_size ||
	    drm_rect_height(&pipe_cfg->src_rect) < min_src_size) {
		DPU_DEBUG_PLANE(pdpu, "invalid source " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&pipe_cfg->src_rect));
		return -E2BIG;
	}

	/* valid yuv image */
	if (DPU_FORMAT_IS_YUV(fmt) &&
	    (pipe_cfg->src_rect.x1 & 0x1 ||
	     pipe_cfg->src_rect.y1 & 0x1 ||
	     drm_rect_width(&pipe_cfg->src_rect) & 0x1 ||
	     drm_rect_height(&pipe_cfg->src_rect) & 0x1)) {
		DPU_DEBUG_PLANE(pdpu, "invalid yuv source " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&pipe_cfg->src_rect));
		return -EINVAL;
	}

	/* min dst support */
	if (drm_rect_width(&pipe_cfg->dst_rect) < 0x1 ||
	    drm_rect_height(&pipe_cfg->dst_rect) < 0x1) {
		DPU_DEBUG_PLANE(pdpu, "invalid dest rect " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&pipe_cfg->dst_rect));
		return -EINVAL;
	}

	/* max clk check */
	if (_dpu_plane_calc_clk(mode, pipe_cfg) > kms->perf.max_core_clk_rate) {
		DPU_DEBUG_PLANE(pdpu, "plane exceeds max mdp core clk limits\n");
		return -E2BIG;
	}

	return 0;
}

static int dpu_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	int ret = 0, min_scale;
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_kms *kms = _dpu_plane_get_kms(&pdpu->base);
	u64 max_mdp_clk_rate = kms->perf.max_core_clk_rate;
	struct dpu_plane_state *pstate = to_dpu_plane_state(new_plane_state);
	struct dpu_sw_pipe *pipe = &pstate->pipe;
	struct dpu_sw_pipe *r_pipe = &pstate->r_pipe;
	const struct drm_crtc_state *crtc_state = NULL;
	const struct dpu_format *fmt;
	struct dpu_sw_pipe_cfg *pipe_cfg = &pstate->pipe_cfg;
	struct dpu_sw_pipe_cfg *r_pipe_cfg = &pstate->r_pipe_cfg;
	struct drm_rect fb_rect = { 0 };
	uint32_t max_linewidth;
	unsigned int rotation;
	uint32_t supported_rotations;
	const struct dpu_sspp_cfg *pipe_hw_caps = pstate->pipe.sspp->cap;
	const struct dpu_sspp_sub_blks *sblk = pstate->pipe.sspp->cap->sblk;

	if (new_plane_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   new_plane_state->crtc);

	min_scale = FRAC_16_16(1, sblk->maxupscale);
	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  min_scale,
						  sblk->maxdwnscale << 16,
						  true, true);
	if (ret) {
		DPU_DEBUG_PLANE(pdpu, "Check plane state failed (%d)\n", ret);
		return ret;
	}
	if (!new_plane_state->visible)
		return 0;

	pipe->multirect_index = DPU_SSPP_RECT_SOLO;
	pipe->multirect_mode = DPU_SSPP_MULTIRECT_NONE;
	r_pipe->multirect_index = DPU_SSPP_RECT_SOLO;
	r_pipe->multirect_mode = DPU_SSPP_MULTIRECT_NONE;
	r_pipe->sspp = NULL;

	pstate->stage = DPU_STAGE_0 + pstate->base.normalized_zpos;
	if (pstate->stage >= pdpu->catalog->caps->max_mixer_blendstages) {
		DPU_ERROR("> %d plane stages assigned\n",
			  pdpu->catalog->caps->max_mixer_blendstages - DPU_STAGE_0);
		return -EINVAL;
	}

	pipe_cfg->src_rect = new_plane_state->src;

	/* state->src is 16.16, src_rect is not */
	pipe_cfg->src_rect.x1 >>= 16;
	pipe_cfg->src_rect.x2 >>= 16;
	pipe_cfg->src_rect.y1 >>= 16;
	pipe_cfg->src_rect.y2 >>= 16;

	pipe_cfg->dst_rect = new_plane_state->dst;

	fb_rect.x2 = new_plane_state->fb->width;
	fb_rect.y2 = new_plane_state->fb->height;

	/* Ensure fb size is supported */
	if (drm_rect_width(&fb_rect) > MAX_IMG_WIDTH ||
	    drm_rect_height(&fb_rect) > MAX_IMG_HEIGHT) {
		DPU_DEBUG_PLANE(pdpu, "invalid framebuffer " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&fb_rect));
		return -E2BIG;
	}

	fmt = to_dpu_format(msm_framebuffer_format(new_plane_state->fb));

	max_linewidth = pdpu->catalog->caps->max_linewidth;

	if ((drm_rect_width(&pipe_cfg->src_rect) > max_linewidth) ||
	     _dpu_plane_calc_clk(&crtc_state->adjusted_mode, pipe_cfg) > max_mdp_clk_rate) {
		/*
		 * In parallel multirect case only the half of the usual width
		 * is supported for tiled formats. If we are here, we know that
		 * full width is more than max_linewidth, thus each rect is
		 * wider than allowed.
		 */
		if (DPU_FORMAT_IS_UBWC(fmt) &&
		    drm_rect_width(&pipe_cfg->src_rect) > max_linewidth) {
			DPU_DEBUG_PLANE(pdpu, "invalid src " DRM_RECT_FMT " line:%u, tiled format\n",
					DRM_RECT_ARG(&pipe_cfg->src_rect), max_linewidth);
			return -E2BIG;
		}

		if (drm_rect_width(&pipe_cfg->src_rect) > 2 * max_linewidth) {
			DPU_DEBUG_PLANE(pdpu, "invalid src " DRM_RECT_FMT " line:%u\n",
					DRM_RECT_ARG(&pipe_cfg->src_rect), max_linewidth);
			return -E2BIG;
		}

		if (drm_rect_width(&pipe_cfg->src_rect) != drm_rect_width(&pipe_cfg->dst_rect) ||
		    drm_rect_height(&pipe_cfg->src_rect) != drm_rect_height(&pipe_cfg->dst_rect) ||
		    (!test_bit(DPU_SSPP_SMART_DMA_V1, &pipe->sspp->cap->features) &&
		     !test_bit(DPU_SSPP_SMART_DMA_V2, &pipe->sspp->cap->features)) ||
		    DPU_FORMAT_IS_YUV(fmt)) {
			DPU_DEBUG_PLANE(pdpu, "invalid src " DRM_RECT_FMT " line:%u, can't use split source\n",
					DRM_RECT_ARG(&pipe_cfg->src_rect), max_linewidth);
			return -E2BIG;
		}

		/*
		 * Use multirect for wide plane. We do not support dynamic
		 * assignment of SSPPs, so we know the configuration.
		 */
		pipe->multirect_index = DPU_SSPP_RECT_0;
		pipe->multirect_mode = DPU_SSPP_MULTIRECT_PARALLEL;

		r_pipe->sspp = pipe->sspp;
		r_pipe->multirect_index = DPU_SSPP_RECT_1;
		r_pipe->multirect_mode = DPU_SSPP_MULTIRECT_PARALLEL;

		*r_pipe_cfg = *pipe_cfg;
		pipe_cfg->src_rect.x2 = (pipe_cfg->src_rect.x1 + pipe_cfg->src_rect.x2) >> 1;
		pipe_cfg->dst_rect.x2 = (pipe_cfg->dst_rect.x1 + pipe_cfg->dst_rect.x2) >> 1;
		r_pipe_cfg->src_rect.x1 = pipe_cfg->src_rect.x2;
		r_pipe_cfg->dst_rect.x1 = pipe_cfg->dst_rect.x2;
	}

	ret = dpu_plane_atomic_check_pipe(pdpu, pipe, pipe_cfg, fmt, &crtc_state->adjusted_mode);
	if (ret)
		return ret;

	if (r_pipe->sspp) {
		ret = dpu_plane_atomic_check_pipe(pdpu, r_pipe, r_pipe_cfg, fmt,
						  &crtc_state->adjusted_mode);
		if (ret)
			return ret;
	}

	supported_rotations = DRM_MODE_REFLECT_MASK | DRM_MODE_ROTATE_0;

	if (pipe_hw_caps->features & BIT(DPU_SSPP_INLINE_ROTATION))
		supported_rotations |= DRM_MODE_ROTATE_90;

	rotation = drm_rotation_simplify(new_plane_state->rotation,
					supported_rotations);

	if ((pipe_hw_caps->features & BIT(DPU_SSPP_INLINE_ROTATION)) &&
		(rotation & DRM_MODE_ROTATE_90)) {
		ret = dpu_plane_check_inline_rotation(pdpu, sblk, pipe_cfg->src_rect, fmt);
		if (ret)
			return ret;
	}

	pstate->rotation = rotation;
	pstate->needs_qos_remap = drm_atomic_crtc_needs_modeset(crtc_state);

	return 0;
}

static void dpu_plane_flush_csc(struct dpu_plane *pdpu, struct dpu_sw_pipe *pipe)
{
	const struct dpu_format *format =
		to_dpu_format(msm_framebuffer_format(pdpu->base.state->fb));
	const struct dpu_csc_cfg *csc_ptr;

	if (!pipe->sspp || !pipe->sspp->ops.setup_csc)
		return;

	csc_ptr = _dpu_plane_get_csc(pipe, format);
	if (!csc_ptr)
		return;

	DPU_DEBUG_PLANE(pdpu, "using 0x%X 0x%X 0x%X...\n",
			csc_ptr->csc_mv[0],
			csc_ptr->csc_mv[1],
			csc_ptr->csc_mv[2]);

	pipe->sspp->ops.setup_csc(pipe->sspp, csc_ptr);

}

void dpu_plane_flush(struct drm_plane *plane)
{
	struct dpu_plane *pdpu;
	struct dpu_plane_state *pstate;

	if (!plane || !plane->state) {
		DPU_ERROR("invalid plane\n");
		return;
	}

	pdpu = to_dpu_plane(plane);
	pstate = to_dpu_plane_state(plane->state);

	/*
	 * These updates have to be done immediately before the plane flush
	 * timing, and may not be moved to the atomic_update/mode_set functions.
	 */
	if (pdpu->is_error)
		/* force white frame with 100% alpha pipe output on error */
		_dpu_plane_color_fill(pdpu, 0xFFFFFF, 0xFF);
	else if (pdpu->color_fill & DPU_PLANE_COLOR_FILL_FLAG)
		/* force 100% alpha */
		_dpu_plane_color_fill(pdpu, pdpu->color_fill, 0xFF);
	else {
		dpu_plane_flush_csc(pdpu, &pstate->pipe);
		dpu_plane_flush_csc(pdpu, &pstate->r_pipe);
	}

	/* flag h/w flush complete */
	if (plane->state)
		pstate->pending = false;
}

/**
 * dpu_plane_set_error: enable/disable error condition
 * @plane: pointer to drm_plane structure
 * @error: error value to set
 */
void dpu_plane_set_error(struct drm_plane *plane, bool error)
{
	struct dpu_plane *pdpu;

	if (!plane)
		return;

	pdpu = to_dpu_plane(plane);
	pdpu->is_error = error;
}

static void dpu_plane_sspp_update_pipe(struct drm_plane *plane,
				       struct dpu_sw_pipe *pipe,
				       struct dpu_sw_pipe_cfg *pipe_cfg,
				       const struct dpu_format *fmt,
				       int frame_rate,
				       struct dpu_hw_fmt_layout *layout)
{
	uint32_t src_flags;
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct dpu_plane_state *pstate = to_dpu_plane_state(state);

	if (layout && pipe->sspp->ops.setup_sourceaddress) {
		trace_dpu_plane_set_scanout(pipe, layout);
		pipe->sspp->ops.setup_sourceaddress(pipe, layout);
	}

	/* override for color fill */
	if (pdpu->color_fill & DPU_PLANE_COLOR_FILL_FLAG) {
		_dpu_plane_set_qos_ctrl(plane, pipe, false);

		/* skip remaining processing on color fill */
		return;
	}

	if (pipe->sspp->ops.setup_rects) {
		pipe->sspp->ops.setup_rects(pipe,
				pipe_cfg);
	}

	_dpu_plane_setup_scaler(pipe, fmt, false, pipe_cfg, pstate->rotation);

	if (pipe->sspp->ops.setup_multirect)
		pipe->sspp->ops.setup_multirect(
				pipe);

	if (pipe->sspp->ops.setup_format) {
		unsigned int rotation = pstate->rotation;

		src_flags = 0x0;

		if (rotation & DRM_MODE_REFLECT_X)
			src_flags |= DPU_SSPP_FLIP_LR;

		if (rotation & DRM_MODE_REFLECT_Y)
			src_flags |= DPU_SSPP_FLIP_UD;

		if (rotation & DRM_MODE_ROTATE_90)
			src_flags |= DPU_SSPP_ROT_90;

		/* update format */
		pipe->sspp->ops.setup_format(pipe, fmt, src_flags);

		if (pipe->sspp->ops.setup_cdp) {
			const struct dpu_perf_cfg *perf = pdpu->catalog->perf;

			pipe->sspp->ops.setup_cdp(pipe, fmt,
						  perf->cdp_cfg[DPU_PERF_CDP_USAGE_RT].rd_enable);
		}
	}

	_dpu_plane_set_qos_lut(plane, pipe, fmt, pipe_cfg);

	if (pipe->sspp->idx != SSPP_CURSOR0 &&
	    pipe->sspp->idx != SSPP_CURSOR1)
		_dpu_plane_set_ot_limit(plane, pipe, pipe_cfg, frame_rate);

	if (pstate->needs_qos_remap)
		_dpu_plane_set_qos_remap(plane, pipe);
}

static void dpu_plane_sspp_atomic_update(struct drm_plane *plane)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct dpu_plane_state *pstate = to_dpu_plane_state(state);
	struct dpu_sw_pipe *pipe = &pstate->pipe;
	struct dpu_sw_pipe *r_pipe = &pstate->r_pipe;
	struct drm_crtc *crtc = state->crtc;
	struct drm_framebuffer *fb = state->fb;
	bool is_rt_pipe;
	const struct dpu_format *fmt =
		to_dpu_format(msm_framebuffer_format(fb));
	struct dpu_sw_pipe_cfg *pipe_cfg = &pstate->pipe_cfg;
	struct dpu_sw_pipe_cfg *r_pipe_cfg = &pstate->r_pipe_cfg;
	struct dpu_kms *kms = _dpu_plane_get_kms(&pdpu->base);
	struct msm_gem_address_space *aspace = kms->base.aspace;
	struct dpu_hw_fmt_layout layout;
	bool layout_valid = false;
	int ret;

	ret = dpu_format_populate_layout(aspace, fb, &layout);
	if (ret)
		DPU_ERROR_PLANE(pdpu, "failed to get format layout, %d\n", ret);
	else
		layout_valid = true;

	pstate->pending = true;

	is_rt_pipe = (dpu_crtc_get_client_type(crtc) != NRT_CLIENT);
	pstate->needs_qos_remap |= (is_rt_pipe != pdpu->is_rt_pipe);
	pdpu->is_rt_pipe = is_rt_pipe;

	DPU_DEBUG_PLANE(pdpu, "FB[%u] " DRM_RECT_FP_FMT "->crtc%u " DRM_RECT_FMT
			", %4.4s ubwc %d\n", fb->base.id, DRM_RECT_FP_ARG(&state->src),
			crtc->base.id, DRM_RECT_ARG(&state->dst),
			(char *)&fmt->base.pixel_format, DPU_FORMAT_IS_UBWC(fmt));

	dpu_plane_sspp_update_pipe(plane, pipe, pipe_cfg, fmt,
				   drm_mode_vrefresh(&crtc->mode),
				   layout_valid ? &layout : NULL);

	if (r_pipe->sspp) {
		dpu_plane_sspp_update_pipe(plane, r_pipe, r_pipe_cfg, fmt,
					   drm_mode_vrefresh(&crtc->mode),
					   layout_valid ? &layout : NULL);
	}

	if (pstate->needs_qos_remap)
		pstate->needs_qos_remap = false;

	pstate->plane_fetch_bw = _dpu_plane_calc_bw(pdpu->catalog, fmt,
						    &crtc->mode, pipe_cfg);

	pstate->plane_clk = _dpu_plane_calc_clk(&crtc->mode, pipe_cfg);

	if (r_pipe->sspp) {
		pstate->plane_fetch_bw += _dpu_plane_calc_bw(pdpu->catalog, fmt, &crtc->mode, r_pipe_cfg);

		pstate->plane_clk = max(pstate->plane_clk, _dpu_plane_calc_clk(&crtc->mode, r_pipe_cfg));
	}
}

static void _dpu_plane_atomic_disable(struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct dpu_plane_state *pstate = to_dpu_plane_state(state);
	struct dpu_sw_pipe *r_pipe = &pstate->r_pipe;

	trace_dpu_plane_disable(DRMID(plane), false,
				pstate->pipe.multirect_mode);

	if (r_pipe->sspp) {
		r_pipe->multirect_index = DPU_SSPP_RECT_SOLO;
		r_pipe->multirect_mode = DPU_SSPP_MULTIRECT_NONE;

		if (r_pipe->sspp->ops.setup_multirect)
			r_pipe->sspp->ops.setup_multirect(r_pipe);
	}

	pstate->pending = true;
}

static void dpu_plane_atomic_update(struct drm_plane *plane,
				struct drm_atomic_state *state)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);

	pdpu->is_error = false;

	DPU_DEBUG_PLANE(pdpu, "\n");

	if (!new_state->visible) {
		_dpu_plane_atomic_disable(plane);
	} else {
		dpu_plane_sspp_atomic_update(plane);
	}
}

static void dpu_plane_destroy_state(struct drm_plane *plane,
		struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_dpu_plane_state(state));
}

static struct drm_plane_state *
dpu_plane_duplicate_state(struct drm_plane *plane)
{
	struct dpu_plane *pdpu;
	struct dpu_plane_state *pstate;
	struct dpu_plane_state *old_state;

	if (!plane) {
		DPU_ERROR("invalid plane\n");
		return NULL;
	} else if (!plane->state) {
		DPU_ERROR("invalid plane state\n");
		return NULL;
	}

	old_state = to_dpu_plane_state(plane->state);
	pdpu = to_dpu_plane(plane);
	pstate = kmemdup(old_state, sizeof(*old_state), GFP_KERNEL);
	if (!pstate) {
		DPU_ERROR_PLANE(pdpu, "failed to allocate state\n");
		return NULL;
	}

	DPU_DEBUG_PLANE(pdpu, "\n");

	pstate->pending = false;

	__drm_atomic_helper_plane_duplicate_state(plane, &pstate->base);

	return &pstate->base;
}

static const char * const multirect_mode_name[] = {
	[DPU_SSPP_MULTIRECT_NONE] = "none",
	[DPU_SSPP_MULTIRECT_PARALLEL] = "parallel",
	[DPU_SSPP_MULTIRECT_TIME_MX] = "time_mx",
};

static const char * const multirect_index_name[] = {
	[DPU_SSPP_RECT_SOLO] = "solo",
	[DPU_SSPP_RECT_0] = "rect_0",
	[DPU_SSPP_RECT_1] = "rect_1",
};

static const char *dpu_get_multirect_mode(enum dpu_sspp_multirect_mode mode)
{
	if (WARN_ON(mode >= ARRAY_SIZE(multirect_mode_name)))
		return "unknown";

	return multirect_mode_name[mode];
}

static const char *dpu_get_multirect_index(enum dpu_sspp_multirect_index index)
{
	if (WARN_ON(index >= ARRAY_SIZE(multirect_index_name)))
		return "unknown";

	return multirect_index_name[index];
}

static void dpu_plane_atomic_print_state(struct drm_printer *p,
		const struct drm_plane_state *state)
{
	const struct dpu_plane_state *pstate = to_dpu_plane_state(state);
	const struct dpu_sw_pipe *pipe = &pstate->pipe;
	const struct dpu_sw_pipe_cfg *pipe_cfg = &pstate->pipe_cfg;
	const struct dpu_sw_pipe *r_pipe = &pstate->r_pipe;
	const struct dpu_sw_pipe_cfg *r_pipe_cfg = &pstate->r_pipe_cfg;

	drm_printf(p, "\tstage=%d\n", pstate->stage);

	drm_printf(p, "\tsspp[0]=%s\n", pipe->sspp->cap->name);
	drm_printf(p, "\tmultirect_mode[0]=%s\n", dpu_get_multirect_mode(pipe->multirect_mode));
	drm_printf(p, "\tmultirect_index[0]=%s\n",
		   dpu_get_multirect_index(pipe->multirect_index));
	drm_printf(p, "\tsrc[0]=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&pipe_cfg->src_rect));
	drm_printf(p, "\tdst[0]=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&pipe_cfg->dst_rect));

	if (r_pipe->sspp) {
		drm_printf(p, "\tsspp[1]=%s\n", r_pipe->sspp->cap->name);
		drm_printf(p, "\tmultirect_mode[1]=%s\n",
			   dpu_get_multirect_mode(r_pipe->multirect_mode));
		drm_printf(p, "\tmultirect_index[1]=%s\n",
			   dpu_get_multirect_index(r_pipe->multirect_index));
		drm_printf(p, "\tsrc[1]=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&r_pipe_cfg->src_rect));
		drm_printf(p, "\tdst[1]=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&r_pipe_cfg->dst_rect));
	}
}

static void dpu_plane_reset(struct drm_plane *plane)
{
	struct dpu_plane *pdpu;
	struct dpu_plane_state *pstate;
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);

	if (!plane) {
		DPU_ERROR("invalid plane\n");
		return;
	}

	pdpu = to_dpu_plane(plane);
	DPU_DEBUG_PLANE(pdpu, "\n");

	/* remove previous state, if present */
	if (plane->state) {
		dpu_plane_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	pstate = kzalloc(sizeof(*pstate), GFP_KERNEL);
	if (!pstate) {
		DPU_ERROR_PLANE(pdpu, "failed to allocate state\n");
		return;
	}

	/*
	 * Set the SSPP here until we have proper virtualized DPU planes.
	 * This is the place where the state is allocated, so fill it fully.
	 */
	pstate->pipe.sspp = dpu_rm_get_sspp(&dpu_kms->rm, pdpu->pipe);
	pstate->pipe.multirect_index = DPU_SSPP_RECT_SOLO;
	pstate->pipe.multirect_mode = DPU_SSPP_MULTIRECT_NONE;

	pstate->r_pipe.sspp = NULL;

	__drm_atomic_helper_plane_reset(plane, &pstate->base);
}

#ifdef CONFIG_DEBUG_FS
void dpu_plane_danger_signal_ctrl(struct drm_plane *plane, bool enable)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_plane_state *pstate = to_dpu_plane_state(plane->state);
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);

	if (!pdpu->is_rt_pipe)
		return;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	_dpu_plane_set_qos_ctrl(plane, &pstate->pipe, enable);
	if (pstate->r_pipe.sspp)
		_dpu_plane_set_qos_ctrl(plane, &pstate->r_pipe, enable);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
}
#endif

static bool dpu_plane_format_mod_supported(struct drm_plane *plane,
		uint32_t format, uint64_t modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED)
		return dpu_find_format(format, qcom_compressed_supported_formats,
				ARRAY_SIZE(qcom_compressed_supported_formats));

	return false;
}

static const struct drm_plane_funcs dpu_plane_funcs = {
		.update_plane = drm_atomic_helper_update_plane,
		.disable_plane = drm_atomic_helper_disable_plane,
		.reset = dpu_plane_reset,
		.atomic_duplicate_state = dpu_plane_duplicate_state,
		.atomic_destroy_state = dpu_plane_destroy_state,
		.atomic_print_state = dpu_plane_atomic_print_state,
		.format_mod_supported = dpu_plane_format_mod_supported,
};

static const struct drm_plane_helper_funcs dpu_plane_helper_funcs = {
		.prepare_fb = dpu_plane_prepare_fb,
		.cleanup_fb = dpu_plane_cleanup_fb,
		.atomic_check = dpu_plane_atomic_check,
		.atomic_update = dpu_plane_atomic_update,
};

/* initialize plane */
struct drm_plane *dpu_plane_init(struct drm_device *dev,
		uint32_t pipe, enum drm_plane_type type,
		unsigned long possible_crtcs)
{
	struct drm_plane *plane = NULL;
	const uint32_t *format_list;
	struct dpu_plane *pdpu;
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_kms *kms = to_dpu_kms(priv->kms);
	struct dpu_hw_sspp *pipe_hw;
	uint32_t num_formats;
	uint32_t supported_rotations;
	int ret;

	/* initialize underlying h/w driver */
	pipe_hw = dpu_rm_get_sspp(&kms->rm, pipe);
	if (!pipe_hw || !pipe_hw->cap || !pipe_hw->cap->sblk) {
		DPU_ERROR("[%u]SSPP is invalid\n", pipe);
		return ERR_PTR(-EINVAL);
	}

	format_list = pipe_hw->cap->sblk->format_list;
	num_formats = pipe_hw->cap->sblk->num_formats;

	pdpu = drmm_universal_plane_alloc(dev, struct dpu_plane, base,
				0xff, &dpu_plane_funcs,
				format_list, num_formats,
				supported_format_modifiers, type, NULL);
	if (IS_ERR(pdpu))
		return ERR_CAST(pdpu);

	/* cache local stuff for later */
	plane = &pdpu->base;
	pdpu->pipe = pipe;

	pdpu->catalog = kms->catalog;

	ret = drm_plane_create_zpos_property(plane, 0, 0, DPU_ZPOS_MAX);
	if (ret)
		DPU_ERROR("failed to install zpos property, rc = %d\n", ret);

	drm_plane_create_alpha_property(plane);
	drm_plane_create_blend_mode_property(plane,
			BIT(DRM_MODE_BLEND_PIXEL_NONE) |
			BIT(DRM_MODE_BLEND_PREMULTI) |
			BIT(DRM_MODE_BLEND_COVERAGE));

	supported_rotations = DRM_MODE_REFLECT_MASK | DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180;

	if (pipe_hw->cap->features & BIT(DPU_SSPP_INLINE_ROTATION))
		supported_rotations |= DRM_MODE_ROTATE_MASK;

	drm_plane_create_rotation_property(plane,
		    DRM_MODE_ROTATE_0, supported_rotations);

	drm_plane_enable_fb_damage_clips(plane);

	/* success! finalize initialization */
	drm_plane_helper_add(plane, &dpu_plane_helper_funcs);

	DPU_DEBUG("%s created for pipe:%u id:%u\n", plane->name,
					pipe, plane->base.id);
	return plane;
}
