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
#include <drm/drm_damage_helper.h>
#include <drm/drm_gem_atomic_helper.h>

#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_formats.h"
#include "dpu_hw_sspp.h"
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

#define DPU_NAME_SIZE  12

#define DPU_PLANE_COLOR_FILL_FLAG	BIT(31)
#define DPU_ZPOS_MAX 255

/* multirect rect index */
enum {
	R0,
	R1,
	R_MAX
};

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
	DRM_FORMAT_BGR565,

	DRM_FORMAT_NV12,
};

/**
 * enum dpu_plane_qos - Different qos configurations for each pipe
 *
 * @DPU_PLANE_QOS_VBLANK_CTRL: Setup VBLANK qos for the pipe.
 * @DPU_PLANE_QOS_VBLANK_AMORTIZE: Enables Amortization within pipe.
 *	this configuration is mutually exclusive from VBLANK_CTRL.
 * @DPU_PLANE_QOS_PANIC_CTRL: Setup panic for the pipe.
 */
enum dpu_plane_qos {
	DPU_PLANE_QOS_VBLANK_CTRL = BIT(0),
	DPU_PLANE_QOS_VBLANK_AMORTIZE = BIT(1),
	DPU_PLANE_QOS_PANIC_CTRL = BIT(2),
};

/*
 * struct dpu_plane - local dpu plane structure
 * @aspace: address space pointer
 * @mplane_list: List of multirect planes of the same pipe
 * @catalog: Points to dpu catalog structure
 * @revalidate: force revalidation of all the plane properties
 */
struct dpu_plane {
	struct drm_plane base;

	struct mutex lock;

	enum dpu_sspp pipe;

	struct dpu_hw_pipe *pipe_hw;
	uint32_t color_fill;
	bool is_error;
	bool is_rt_pipe;
	bool is_virtual;
	struct list_head mplane_list;
	struct dpu_mdss_cfg *catalog;
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
 * @plane: Pointer to drm plane.
 * @fb:   Pointer to framebuffer associated with the given plane
 * @pipe_cfg: Pointer to pipe configuration
 * Result: Updates calculated bandwidth in the plane state.
 * BW Equation: src_w * src_h * bpp * fps * (v_total / v_dest)
 * Prefill BW Equation: line src bytes * line_time
 */
static void _dpu_plane_calc_bw(struct drm_plane *plane,
	struct drm_framebuffer *fb,
	struct dpu_hw_pipe_cfg *pipe_cfg)
{
	struct dpu_plane_state *pstate;
	struct drm_display_mode *mode;
	const struct dpu_format *fmt = NULL;
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);
	int src_width, src_height, dst_height, fps;
	u64 plane_prefill_bw;
	u64 plane_bw;
	u32 hw_latency_lines;
	u64 scale_factor;
	int vbp, vpw, vfp;

	pstate = to_dpu_plane_state(plane->state);
	mode = &plane->state->crtc->mode;

	fmt = dpu_get_dpu_format_ext(fb->format->format, fb->modifier);

	src_width = drm_rect_width(&pipe_cfg->src_rect);
	src_height = drm_rect_height(&pipe_cfg->src_rect);
	dst_height = drm_rect_height(&pipe_cfg->dst_rect);
	fps = drm_mode_vrefresh(mode);
	vbp = mode->vtotal - mode->vsync_end;
	vpw = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	hw_latency_lines =  dpu_kms->catalog->perf.min_prefill_lines;
	scale_factor = src_height > dst_height ?
		mult_frac(src_height, 1, dst_height) : 1;

	plane_bw =
		src_width * mode->vtotal * fps * fmt->bpp *
		scale_factor;

	plane_prefill_bw =
		src_width * hw_latency_lines * fps * fmt->bpp *
		scale_factor * mode->vtotal;

	if ((vbp+vpw) > hw_latency_lines)
		do_div(plane_prefill_bw, (vbp+vpw));
	else if ((vbp+vpw+vfp) < hw_latency_lines)
		do_div(plane_prefill_bw, (vbp+vpw+vfp));
	else
		do_div(plane_prefill_bw, hw_latency_lines);


	pstate->plane_fetch_bw = max(plane_bw, plane_prefill_bw);
}

/**
 * _dpu_plane_calc_clk - calculate clock required for a plane
 * @plane: Pointer to drm plane.
 * @pipe_cfg: Pointer to pipe configuration
 * Result: Updates calculated clock in the plane state.
 * Clock equation: dst_w * v_total * fps * (src_h / dst_h)
 */
static void _dpu_plane_calc_clk(struct drm_plane *plane, struct dpu_hw_pipe_cfg *pipe_cfg)
{
	struct dpu_plane_state *pstate;
	struct drm_display_mode *mode;
	int dst_width, src_height, dst_height, fps;

	pstate = to_dpu_plane_state(plane->state);
	mode = &plane->state->crtc->mode;

	src_height = drm_rect_height(&pipe_cfg->src_rect);
	dst_width = drm_rect_width(&pipe_cfg->dst_rect);
	dst_height = drm_rect_height(&pipe_cfg->dst_rect);
	fps = drm_mode_vrefresh(mode);

	pstate->plane_clk =
		dst_width * mode->vtotal * fps;

	if (src_height > dst_height) {
		pstate->plane_clk *= src_height;
		do_div(pstate->plane_clk, dst_height);
	}
}

/**
 * _dpu_plane_calc_fill_level - calculate fill level of the given source format
 * @plane:		Pointer to drm plane
 * @fmt:		Pointer to source buffer format
 * @src_width:		width of source buffer
 * Return: fill level corresponding to the source buffer/format or 0 if error
 */
static int _dpu_plane_calc_fill_level(struct drm_plane *plane,
		const struct dpu_format *fmt, u32 src_width)
{
	struct dpu_plane *pdpu, *tmp;
	struct dpu_plane_state *pstate;
	u32 fixed_buff_size;
	u32 total_fl;

	if (!fmt || !plane->state || !src_width || !fmt->bpp) {
		DPU_ERROR("invalid arguments\n");
		return 0;
	}

	pdpu = to_dpu_plane(plane);
	pstate = to_dpu_plane_state(plane->state);
	fixed_buff_size = pdpu->catalog->caps->pixel_ram_size;

	list_for_each_entry(tmp, &pdpu->mplane_list, mplane_list) {
		u32 tmp_width;

		if (!tmp->base.state->visible)
			continue;
		tmp_width = drm_rect_width(&tmp->base.state->src) >> 16;
		DPU_DEBUG("plane%d/%d src_width:%d/%d\n",
				pdpu->base.base.id, tmp->base.base.id,
				src_width,
				tmp_width);
		src_width = max_t(u32, src_width,
				  tmp_width);
	}

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
		if (pstate->multirect_mode == DPU_SSPP_MULTIRECT_PARALLEL) {
			total_fl = (fixed_buff_size / 2) * 2 /
				((src_width + 32) * fmt->bpp);
		} else {
			total_fl = (fixed_buff_size) * 2 /
				((src_width + 32) * fmt->bpp);
		}
	}

	DPU_DEBUG_PLANE(pdpu, "pnum:%d fmt: %4.4s w:%u fl:%u\n",
			pdpu->pipe - SSPP_VIG0,
			(char *)&fmt->base.pixel_format,
			src_width, total_fl);

	return total_fl;
}

/**
 * _dpu_plane_get_qos_lut - get LUT mapping based on fill level
 * @tbl:		Pointer to LUT table
 * @total_fl:		fill level
 * Return: LUT setting corresponding to the fill level
 */
static u64 _dpu_plane_get_qos_lut(const struct dpu_qos_lut_tbl *tbl,
		u32 total_fl)
{
	int i;

	if (!tbl || !tbl->nentry || !tbl->entries)
		return 0;

	for (i = 0; i < tbl->nentry; i++)
		if (total_fl <= tbl->entries[i].fl)
			return tbl->entries[i].lut;

	/* if last fl is zero, use as default */
	if (!tbl->entries[i-1].fl)
		return tbl->entries[i-1].lut;

	return 0;
}

/**
 * _dpu_plane_set_qos_lut - set QoS LUT of the given plane
 * @plane:		Pointer to drm plane
 * @fb:			Pointer to framebuffer associated with the given plane
 * @pipe_cfg:		Pointer to pipe configuration
 */
static void _dpu_plane_set_qos_lut(struct drm_plane *plane,
		struct drm_framebuffer *fb, struct dpu_hw_pipe_cfg *pipe_cfg)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	const struct dpu_format *fmt = NULL;
	u64 qos_lut;
	u32 total_fl = 0, lut_usage;

	if (!pdpu->is_rt_pipe) {
		lut_usage = DPU_QOS_LUT_USAGE_NRT;
	} else {
		fmt = dpu_get_dpu_format_ext(
				fb->format->format,
				fb->modifier);
		total_fl = _dpu_plane_calc_fill_level(plane, fmt,
				drm_rect_width(&pipe_cfg->src_rect));

		if (fmt && DPU_FORMAT_IS_LINEAR(fmt))
			lut_usage = DPU_QOS_LUT_USAGE_LINEAR;
		else
			lut_usage = DPU_QOS_LUT_USAGE_MACROTILE;
	}

	qos_lut = _dpu_plane_get_qos_lut(
			&pdpu->catalog->perf.qos_lut_tbl[lut_usage], total_fl);

	trace_dpu_perf_set_qos_luts(pdpu->pipe - SSPP_VIG0,
			(fmt) ? fmt->base.pixel_format : 0,
			pdpu->is_rt_pipe, total_fl, qos_lut, lut_usage);

	DPU_DEBUG_PLANE(pdpu, "pnum:%d fmt: %4.4s rt:%d fl:%u lut:0x%llx\n",
			pdpu->pipe - SSPP_VIG0,
			fmt ? (char *)&fmt->base.pixel_format : NULL,
			pdpu->is_rt_pipe, total_fl, qos_lut);

	pdpu->pipe_hw->ops.setup_creq_lut(pdpu->pipe_hw, qos_lut);
}

/**
 * _dpu_plane_set_danger_lut - set danger/safe LUT of the given plane
 * @plane:		Pointer to drm plane
 * @fb:			Pointer to framebuffer associated with the given plane
 */
static void _dpu_plane_set_danger_lut(struct drm_plane *plane,
		struct drm_framebuffer *fb)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	const struct dpu_format *fmt = NULL;
	u32 danger_lut, safe_lut;

	if (!pdpu->is_rt_pipe) {
		danger_lut = pdpu->catalog->perf.danger_lut_tbl
				[DPU_QOS_LUT_USAGE_NRT];
		safe_lut = pdpu->catalog->perf.safe_lut_tbl
				[DPU_QOS_LUT_USAGE_NRT];
	} else {
		fmt = dpu_get_dpu_format_ext(
				fb->format->format,
				fb->modifier);

		if (fmt && DPU_FORMAT_IS_LINEAR(fmt)) {
			danger_lut = pdpu->catalog->perf.danger_lut_tbl
					[DPU_QOS_LUT_USAGE_LINEAR];
			safe_lut = pdpu->catalog->perf.safe_lut_tbl
					[DPU_QOS_LUT_USAGE_LINEAR];
		} else {
			danger_lut = pdpu->catalog->perf.danger_lut_tbl
					[DPU_QOS_LUT_USAGE_MACROTILE];
			safe_lut = pdpu->catalog->perf.safe_lut_tbl
					[DPU_QOS_LUT_USAGE_MACROTILE];
		}
	}

	trace_dpu_perf_set_danger_luts(pdpu->pipe - SSPP_VIG0,
			(fmt) ? fmt->base.pixel_format : 0,
			(fmt) ? fmt->fetch_mode : 0,
			danger_lut,
			safe_lut);

	DPU_DEBUG_PLANE(pdpu, "pnum:%d fmt: %4.4s mode:%d luts[0x%x, 0x%x]\n",
		pdpu->pipe - SSPP_VIG0,
		fmt ? (char *)&fmt->base.pixel_format : NULL,
		fmt ? fmt->fetch_mode : -1,
		danger_lut,
		safe_lut);

	pdpu->pipe_hw->ops.setup_danger_safe_lut(pdpu->pipe_hw,
			danger_lut, safe_lut);
}

/**
 * _dpu_plane_set_qos_ctrl - set QoS control of the given plane
 * @plane:		Pointer to drm plane
 * @enable:		true to enable QoS control
 * @flags:		QoS control mode (enum dpu_plane_qos)
 */
static void _dpu_plane_set_qos_ctrl(struct drm_plane *plane,
	bool enable, u32 flags)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_hw_pipe_qos_cfg pipe_qos_cfg;

	memset(&pipe_qos_cfg, 0, sizeof(pipe_qos_cfg));

	if (flags & DPU_PLANE_QOS_VBLANK_CTRL) {
		pipe_qos_cfg.creq_vblank = pdpu->pipe_hw->cap->sblk->creq_vblank;
		pipe_qos_cfg.danger_vblank =
				pdpu->pipe_hw->cap->sblk->danger_vblank;
		pipe_qos_cfg.vblank_en = enable;
	}

	if (flags & DPU_PLANE_QOS_VBLANK_AMORTIZE) {
		/* this feature overrules previous VBLANK_CTRL */
		pipe_qos_cfg.vblank_en = false;
		pipe_qos_cfg.creq_vblank = 0; /* clear vblank bits */
	}

	if (flags & DPU_PLANE_QOS_PANIC_CTRL)
		pipe_qos_cfg.danger_safe_en = enable;

	if (!pdpu->is_rt_pipe) {
		pipe_qos_cfg.vblank_en = false;
		pipe_qos_cfg.danger_safe_en = false;
	}

	DPU_DEBUG_PLANE(pdpu, "pnum:%d ds:%d vb:%d pri[0x%x, 0x%x] is_rt:%d\n",
		pdpu->pipe - SSPP_VIG0,
		pipe_qos_cfg.danger_safe_en,
		pipe_qos_cfg.vblank_en,
		pipe_qos_cfg.creq_vblank,
		pipe_qos_cfg.danger_vblank,
		pdpu->is_rt_pipe);

	pdpu->pipe_hw->ops.setup_qos_ctrl(pdpu->pipe_hw,
			&pipe_qos_cfg);
}

/**
 * _dpu_plane_set_ot_limit - set OT limit for the given plane
 * @plane:		Pointer to drm plane
 * @crtc:		Pointer to drm crtc
 * @pipe_cfg:		Pointer to pipe configuration
 */
static void _dpu_plane_set_ot_limit(struct drm_plane *plane,
		struct drm_crtc *crtc, struct dpu_hw_pipe_cfg *pipe_cfg)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_vbif_set_ot_params ot_params;
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);

	memset(&ot_params, 0, sizeof(ot_params));
	ot_params.xin_id = pdpu->pipe_hw->cap->xin_id;
	ot_params.num = pdpu->pipe_hw->idx - SSPP_NONE;
	ot_params.width = drm_rect_width(&pipe_cfg->src_rect);
	ot_params.height = drm_rect_height(&pipe_cfg->src_rect);
	ot_params.is_wfd = !pdpu->is_rt_pipe;
	ot_params.frame_rate = drm_mode_vrefresh(&crtc->mode);
	ot_params.vbif_idx = VBIF_RT;
	ot_params.clk_ctrl = pdpu->pipe_hw->cap->clk_ctrl;
	ot_params.rd = true;

	dpu_vbif_set_ot_limit(dpu_kms, &ot_params);
}

/**
 * _dpu_plane_set_qos_remap - set vbif QoS for the given plane
 * @plane:		Pointer to drm plane
 */
static void _dpu_plane_set_qos_remap(struct drm_plane *plane)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_vbif_set_qos_params qos_params;
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);

	memset(&qos_params, 0, sizeof(qos_params));
	qos_params.vbif_idx = VBIF_RT;
	qos_params.clk_ctrl = pdpu->pipe_hw->cap->clk_ctrl;
	qos_params.xin_id = pdpu->pipe_hw->cap->xin_id;
	qos_params.num = pdpu->pipe_hw->idx - SSPP_VIG0;
	qos_params.is_rt = pdpu->is_rt_pipe;

	DPU_DEBUG_PLANE(pdpu, "pipe:%d vbif:%d xin:%d rt:%d, clk_ctrl:%d\n",
			qos_params.num,
			qos_params.vbif_idx,
			qos_params.xin_id, qos_params.is_rt,
			qos_params.clk_ctrl);

	dpu_vbif_set_qos_remap(dpu_kms, &qos_params);
}

static void _dpu_plane_set_scanout(struct drm_plane *plane,
		struct dpu_plane_state *pstate,
		struct dpu_hw_pipe_cfg *pipe_cfg,
		struct drm_framebuffer *fb)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_kms *kms = _dpu_plane_get_kms(&pdpu->base);
	struct msm_gem_address_space *aspace = kms->base.aspace;
	int ret;

	ret = dpu_format_populate_layout(aspace, fb, &pipe_cfg->layout);
	if (ret == -EAGAIN)
		DPU_DEBUG_PLANE(pdpu, "not updating same src addrs\n");
	else if (ret)
		DPU_ERROR_PLANE(pdpu, "failed to get format layout, %d\n", ret);
	else if (pdpu->pipe_hw->ops.setup_sourceaddress) {
		trace_dpu_plane_set_scanout(pdpu->pipe_hw->idx,
					    &pipe_cfg->layout,
					    pstate->multirect_index);
		pdpu->pipe_hw->ops.setup_sourceaddress(pdpu->pipe_hw, pipe_cfg,
						pstate->multirect_index);
	}
}

static void _dpu_plane_setup_scaler3(struct dpu_plane *pdpu,
		struct dpu_plane_state *pstate,
		uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
		struct dpu_hw_scaler3_cfg *scale_cfg,
		struct dpu_hw_pixel_ext *pixel_ext,
		const struct dpu_format *fmt,
		uint32_t chroma_subsmpl_h, uint32_t chroma_subsmpl_v)
{
	uint32_t i;

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

		if (pdpu->pipe_hw->cap->features &
			BIT(DPU_SSPP_SCALER_QSEED4)) {
			scale_cfg->preload_x[i] = DPU_QSEED4_DEFAULT_PRELOAD_H;
			scale_cfg->preload_y[i] = DPU_QSEED4_DEFAULT_PRELOAD_V;
		} else {
			scale_cfg->preload_x[i] = DPU_QSEED3_DEFAULT_PRELOAD_H;
			scale_cfg->preload_y[i] = DPU_QSEED3_DEFAULT_PRELOAD_V;
		}

		pixel_ext->num_ext_pxls_top[i] =
			scale_cfg->src_height[i];
		pixel_ext->num_ext_pxls_left[i] =
			scale_cfg->src_width[i];
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

static const struct dpu_csc_cfg dpu_csc_YUV2RGB_601L = {
	{
		/* S15.16 format */
		0x00012A00, 0x00000000, 0x00019880,
		0x00012A00, 0xFFFF9B80, 0xFFFF3000,
		0x00012A00, 0x00020480, 0x00000000,
	},
	/* signed bias */
	{ 0xfff0, 0xff80, 0xff80,},
	{ 0x0, 0x0, 0x0,},
	/* unsigned clamp */
	{ 0x10, 0xeb, 0x10, 0xf0, 0x10, 0xf0,},
	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,},
};

static const struct dpu_csc_cfg dpu_csc10_YUV2RGB_601L = {
	{
		/* S15.16 format */
		0x00012A00, 0x00000000, 0x00019880,
		0x00012A00, 0xFFFF9B80, 0xFFFF3000,
		0x00012A00, 0x00020480, 0x00000000,
		},
	/* signed bias */
	{ 0xffc0, 0xfe00, 0xfe00,},
	{ 0x0, 0x0, 0x0,},
	/* unsigned clamp */
	{ 0x40, 0x3ac, 0x40, 0x3c0, 0x40, 0x3c0,},
	{ 0x00, 0x3ff, 0x00, 0x3ff, 0x00, 0x3ff,},
};

static const struct dpu_csc_cfg *_dpu_plane_get_csc(struct dpu_plane *pdpu, const struct dpu_format *fmt)
{
	const struct dpu_csc_cfg *csc_ptr;

	if (!pdpu) {
		DPU_ERROR("invalid plane\n");
		return NULL;
	}

	if (!DPU_FORMAT_IS_YUV(fmt))
		return NULL;

	if (BIT(DPU_SSPP_CSC_10BIT) & pdpu->pipe_hw->cap->features)
		csc_ptr = &dpu_csc10_YUV2RGB_601L;
	else
		csc_ptr = &dpu_csc_YUV2RGB_601L;

	DPU_DEBUG_PLANE(pdpu, "using 0x%X 0x%X 0x%X...\n",
			csc_ptr->csc_mv[0],
			csc_ptr->csc_mv[1],
			csc_ptr->csc_mv[2]);

	return csc_ptr;
}

static void _dpu_plane_setup_scaler(struct dpu_plane *pdpu,
		struct dpu_plane_state *pstate,
		const struct dpu_format *fmt, bool color_fill,
		struct dpu_hw_pipe_cfg *pipe_cfg)
{
	const struct drm_format_info *info = drm_format_info(fmt->base.pixel_format);
	struct dpu_hw_scaler3_cfg scaler3_cfg;
	struct dpu_hw_pixel_ext pixel_ext;

	memset(&scaler3_cfg, 0, sizeof(scaler3_cfg));
	memset(&pixel_ext, 0, sizeof(pixel_ext));

	/* don't chroma subsample if decimating */
	/* update scaler. calculate default config for QSEED3 */
	_dpu_plane_setup_scaler3(pdpu, pstate,
			drm_rect_width(&pipe_cfg->src_rect),
			drm_rect_height(&pipe_cfg->src_rect),
			drm_rect_width(&pipe_cfg->dst_rect),
			drm_rect_height(&pipe_cfg->dst_rect),
			&scaler3_cfg, &pixel_ext, fmt,
			info->hsub, info->vsub);

	if (pdpu->pipe_hw->ops.setup_pe)
		pdpu->pipe_hw->ops.setup_pe(pdpu->pipe_hw,
				&pixel_ext);

	/**
	 * when programmed in multirect mode, scalar block will be
	 * bypassed. Still we need to update alpha and bitwidth
	 * ONLY for RECT0
	 */
	if (pdpu->pipe_hw->ops.setup_scaler &&
			pstate->multirect_index != DPU_SSPP_RECT_1)
		pdpu->pipe_hw->ops.setup_scaler(pdpu->pipe_hw,
				pipe_cfg,
				&scaler3_cfg);
}

/**
 * _dpu_plane_color_fill - enables color fill on plane
 * @pdpu:   Pointer to DPU plane object
 * @color:  RGB fill color value, [23..16] Blue, [15..8] Green, [7..0] Red
 * @alpha:  8-bit fill alpha value, 255 selects 100% alpha
 * Returns: 0 on success
 */
static int _dpu_plane_color_fill(struct dpu_plane *pdpu,
		uint32_t color, uint32_t alpha)
{
	const struct dpu_format *fmt;
	const struct drm_plane *plane = &pdpu->base;
	struct dpu_plane_state *pstate = to_dpu_plane_state(plane->state);
	struct dpu_hw_pipe_cfg pipe_cfg;

	DPU_DEBUG_PLANE(pdpu, "\n");

	/*
	 * select fill format to match user property expectation,
	 * h/w only supports RGB variants
	 */
	fmt = dpu_get_dpu_format(DRM_FORMAT_ABGR8888);

	/* update sspp */
	if (fmt && pdpu->pipe_hw->ops.setup_solidfill) {
		pdpu->pipe_hw->ops.setup_solidfill(pdpu->pipe_hw,
				(color & 0xFFFFFF) | ((alpha & 0xFF) << 24),
				pstate->multirect_index);

		/* override scaler/decimation if solid fill */
		pipe_cfg.dst_rect = pstate->base.dst;

		pipe_cfg.src_rect.x1 = 0;
		pipe_cfg.src_rect.y1 = 0;
		pipe_cfg.src_rect.x2 =
			drm_rect_width(&pipe_cfg.dst_rect);
		pipe_cfg.src_rect.y2 =
			drm_rect_height(&pipe_cfg.dst_rect);

		if (pdpu->pipe_hw->ops.setup_format)
			pdpu->pipe_hw->ops.setup_format(pdpu->pipe_hw,
					fmt, DPU_SSPP_SOLID_FILL,
					pstate->multirect_index);

		if (pdpu->pipe_hw->ops.setup_rects)
			pdpu->pipe_hw->ops.setup_rects(pdpu->pipe_hw,
					&pipe_cfg,
					pstate->multirect_index);

		_dpu_plane_setup_scaler(pdpu, pstate, fmt, true, &pipe_cfg);
	}

	return 0;
}

void dpu_plane_clear_multirect(const struct drm_plane_state *drm_state)
{
	struct dpu_plane_state *pstate = to_dpu_plane_state(drm_state);

	pstate->multirect_index = DPU_SSPP_RECT_SOLO;
	pstate->multirect_mode = DPU_SSPP_MULTIRECT_NONE;
}

int dpu_plane_validate_multirect_v2(struct dpu_multirect_plane_states *plane)
{
	struct dpu_plane_state *pstate[R_MAX];
	const struct drm_plane_state *drm_state[R_MAX];
	struct drm_rect src[R_MAX], dst[R_MAX];
	struct dpu_plane *dpu_plane[R_MAX];
	const struct dpu_format *fmt[R_MAX];
	int i, buffer_lines;
	unsigned int max_tile_height = 1;
	bool parallel_fetch_qualified = true;
	bool has_tiled_rect = false;

	for (i = 0; i < R_MAX; i++) {
		const struct msm_format *msm_fmt;

		drm_state[i] = i ? plane->r1 : plane->r0;
		msm_fmt = msm_framebuffer_format(drm_state[i]->fb);
		fmt[i] = to_dpu_format(msm_fmt);

		if (DPU_FORMAT_IS_UBWC(fmt[i])) {
			has_tiled_rect = true;
			if (fmt[i]->tile_height > max_tile_height)
				max_tile_height = fmt[i]->tile_height;
		}
	}

	for (i = 0; i < R_MAX; i++) {
		int width_threshold;

		pstate[i] = to_dpu_plane_state(drm_state[i]);
		dpu_plane[i] = to_dpu_plane(drm_state[i]->plane);

		if (pstate[i] == NULL) {
			DPU_ERROR("DPU plane state of plane id %d is NULL\n",
				drm_state[i]->plane->base.id);
			return -EINVAL;
		}

		src[i].x1 = drm_state[i]->src_x >> 16;
		src[i].y1 = drm_state[i]->src_y >> 16;
		src[i].x2 = src[i].x1 + (drm_state[i]->src_w >> 16);
		src[i].y2 = src[i].y1 + (drm_state[i]->src_h >> 16);

		dst[i] = drm_plane_state_dest(drm_state[i]);

		if (drm_rect_calc_hscale(&src[i], &dst[i], 1, 1) != 1 ||
		    drm_rect_calc_vscale(&src[i], &dst[i], 1, 1) != 1) {
			DPU_ERROR_PLANE(dpu_plane[i],
				"scaling is not supported in multirect mode\n");
			return -EINVAL;
		}

		if (DPU_FORMAT_IS_YUV(fmt[i])) {
			DPU_ERROR_PLANE(dpu_plane[i],
				"Unsupported format for multirect mode\n");
			return -EINVAL;
		}

		/**
		 * SSPP PD_MEM is split half - one for each RECT.
		 * Tiled formats need 5 lines of buffering while fetching
		 * whereas linear formats need only 2 lines.
		 * So we cannot support more than half of the supported SSPP
		 * width for tiled formats.
		 */
		width_threshold = dpu_plane[i]->catalog->caps->max_linewidth;
		if (has_tiled_rect)
			width_threshold /= 2;

		if (parallel_fetch_qualified &&
		    drm_rect_width(&src[i]) > width_threshold)
			parallel_fetch_qualified = false;

	}

	/* Validate RECT's and set the mode */

	/* Prefer PARALLEL FETCH Mode over TIME_MX Mode */
	if (parallel_fetch_qualified) {
		pstate[R0]->multirect_mode = DPU_SSPP_MULTIRECT_PARALLEL;
		pstate[R1]->multirect_mode = DPU_SSPP_MULTIRECT_PARALLEL;

		goto done;
	}

	/* TIME_MX Mode */
	buffer_lines = 2 * max_tile_height;

	if (dst[R1].y1 >= dst[R0].y2 + buffer_lines ||
	    dst[R0].y1 >= dst[R1].y2 + buffer_lines) {
		pstate[R0]->multirect_mode = DPU_SSPP_MULTIRECT_TIME_MX;
		pstate[R1]->multirect_mode = DPU_SSPP_MULTIRECT_TIME_MX;
	} else {
		DPU_ERROR(
			"No multirect mode possible for the planes (%d - %d)\n",
			drm_state[R0]->plane->base.id,
			drm_state[R1]->plane->base.id);
		return -EINVAL;
	}

done:
	if (dpu_plane[R0]->is_virtual) {
		pstate[R0]->multirect_index = DPU_SSPP_RECT_1;
		pstate[R1]->multirect_index = DPU_SSPP_RECT_0;
	} else {
		pstate[R0]->multirect_index = DPU_SSPP_RECT_0;
		pstate[R1]->multirect_index = DPU_SSPP_RECT_1;
	}

	DPU_DEBUG_PLANE(dpu_plane[R0], "R0: %d - %d\n",
		pstate[R0]->multirect_mode, pstate[R0]->multirect_index);
	DPU_DEBUG_PLANE(dpu_plane[R1], "R1: %d - %d\n",
		pstate[R1]->multirect_mode, pstate[R1]->multirect_index);
	return 0;
}

/**
 * dpu_plane_get_ctl_flush - get control flush for the given plane
 * @plane: Pointer to drm plane structure
 * @ctl: Pointer to hardware control driver
 * @flush_sspp: Pointer to sspp flush control word
 */
void dpu_plane_get_ctl_flush(struct drm_plane *plane, struct dpu_hw_ctl *ctl,
		u32 *flush_sspp)
{
	*flush_sspp = ctl->ops.get_bitmask_sspp(ctl, dpu_plane_pipe(plane));
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

static bool dpu_plane_validate_src(struct drm_rect *src,
				   struct drm_rect *fb_rect,
				   uint32_t min_src_size)
{
	/* Ensure fb size is supported */
	if (drm_rect_width(fb_rect) > MAX_IMG_WIDTH ||
	    drm_rect_height(fb_rect) > MAX_IMG_HEIGHT)
		return false;

	/* Ensure src rect is above the minimum size */
	if (drm_rect_width(src) < min_src_size ||
	    drm_rect_height(src) < min_src_size)
		return false;

	/* Ensure src is fully encapsulated in fb */
	return drm_rect_intersect(fb_rect, src) &&
		drm_rect_equals(fb_rect, src);
}

static int dpu_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	int ret = 0, min_scale;
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_plane_state *pstate = to_dpu_plane_state(new_plane_state);
	const struct drm_crtc_state *crtc_state = NULL;
	const struct dpu_format *fmt;
	struct drm_rect src, dst, fb_rect = { 0 };
	uint32_t min_src_size, max_linewidth;

	if (new_plane_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   new_plane_state->crtc);

	min_scale = FRAC_16_16(1, pdpu->pipe_hw->cap->sblk->maxupscale);
	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  min_scale,
						  pdpu->pipe_hw->cap->sblk->maxdwnscale << 16,
						  true, true);
	if (ret) {
		DPU_DEBUG_PLANE(pdpu, "Check plane state failed (%d)\n", ret);
		return ret;
	}
	if (!new_plane_state->visible)
		return 0;

	src.x1 = new_plane_state->src_x >> 16;
	src.y1 = new_plane_state->src_y >> 16;
	src.x2 = src.x1 + (new_plane_state->src_w >> 16);
	src.y2 = src.y1 + (new_plane_state->src_h >> 16);

	dst = drm_plane_state_dest(new_plane_state);

	fb_rect.x2 = new_plane_state->fb->width;
	fb_rect.y2 = new_plane_state->fb->height;

	max_linewidth = pdpu->catalog->caps->max_linewidth;

	fmt = to_dpu_format(msm_framebuffer_format(new_plane_state->fb));

	min_src_size = DPU_FORMAT_IS_YUV(fmt) ? 2 : 1;

	if (DPU_FORMAT_IS_YUV(fmt) &&
		(!(pdpu->pipe_hw->cap->features & DPU_SSPP_SCALER) ||
		 !(pdpu->pipe_hw->cap->features & DPU_SSPP_CSC_ANY))) {
		DPU_DEBUG_PLANE(pdpu,
				"plane doesn't have scaler/csc for yuv\n");
		return -EINVAL;

	/* check src bounds */
	} else if (!dpu_plane_validate_src(&src, &fb_rect, min_src_size)) {
		DPU_DEBUG_PLANE(pdpu, "invalid source " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&src));
		return -E2BIG;

	/* valid yuv image */
	} else if (DPU_FORMAT_IS_YUV(fmt) &&
		   (src.x1 & 0x1 || src.y1 & 0x1 ||
		    drm_rect_width(&src) & 0x1 ||
		    drm_rect_height(&src) & 0x1)) {
		DPU_DEBUG_PLANE(pdpu, "invalid yuv source " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&src));
		return -EINVAL;

	/* min dst support */
	} else if (drm_rect_width(&dst) < 0x1 || drm_rect_height(&dst) < 0x1) {
		DPU_DEBUG_PLANE(pdpu, "invalid dest rect " DRM_RECT_FMT "\n",
				DRM_RECT_ARG(&dst));
		return -EINVAL;

	/* check decimated source width */
	} else if (drm_rect_width(&src) > max_linewidth) {
		DPU_DEBUG_PLANE(pdpu, "invalid src " DRM_RECT_FMT " line:%u\n",
				DRM_RECT_ARG(&src), max_linewidth);
		return -E2BIG;
	}

	pstate->needs_qos_remap = drm_atomic_crtc_needs_modeset(crtc_state);

	return 0;
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
	else if (pdpu->pipe_hw && pdpu->pipe_hw->ops.setup_csc) {
		const struct dpu_format *fmt = to_dpu_format(msm_framebuffer_format(plane->state->fb));
		const struct dpu_csc_cfg *csc_ptr = _dpu_plane_get_csc(pdpu, fmt);

		if (csc_ptr)
			pdpu->pipe_hw->ops.setup_csc(pdpu->pipe_hw, csc_ptr);
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

static void dpu_plane_sspp_atomic_update(struct drm_plane *plane)
{
	uint32_t src_flags;
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct dpu_plane_state *pstate = to_dpu_plane_state(state);
	struct drm_crtc *crtc = state->crtc;
	struct drm_framebuffer *fb = state->fb;
	bool is_rt_pipe, update_qos_remap;
	const struct dpu_format *fmt =
		to_dpu_format(msm_framebuffer_format(fb));
	struct dpu_hw_pipe_cfg pipe_cfg;

	memset(&pipe_cfg, 0, sizeof(struct dpu_hw_pipe_cfg));

	_dpu_plane_set_scanout(plane, pstate, &pipe_cfg, fb);

	pstate->pending = true;

	is_rt_pipe = (dpu_crtc_get_client_type(crtc) != NRT_CLIENT);
	_dpu_plane_set_qos_ctrl(plane, false, DPU_PLANE_QOS_PANIC_CTRL);

	DPU_DEBUG_PLANE(pdpu, "FB[%u] " DRM_RECT_FP_FMT "->crtc%u " DRM_RECT_FMT
			", %4.4s ubwc %d\n", fb->base.id, DRM_RECT_FP_ARG(&state->src),
			crtc->base.id, DRM_RECT_ARG(&state->dst),
			(char *)&fmt->base.pixel_format, DPU_FORMAT_IS_UBWC(fmt));

	pipe_cfg.src_rect = state->src;

	/* state->src is 16.16, src_rect is not */
	pipe_cfg.src_rect.x1 >>= 16;
	pipe_cfg.src_rect.x2 >>= 16;
	pipe_cfg.src_rect.y1 >>= 16;
	pipe_cfg.src_rect.y2 >>= 16;

	pipe_cfg.dst_rect = state->dst;

	/* override for color fill */
	if (pdpu->color_fill & DPU_PLANE_COLOR_FILL_FLAG) {
		/* skip remaining processing on color fill */
		return;
	}

	if (pdpu->pipe_hw->ops.setup_rects) {
		pdpu->pipe_hw->ops.setup_rects(pdpu->pipe_hw,
				&pipe_cfg,
				pstate->multirect_index);
	}

	_dpu_plane_setup_scaler(pdpu, pstate, fmt, false, &pipe_cfg);

	if (pdpu->pipe_hw->ops.setup_multirect)
		pdpu->pipe_hw->ops.setup_multirect(
				pdpu->pipe_hw,
				pstate->multirect_index,
				pstate->multirect_mode);

	if (pdpu->pipe_hw->ops.setup_format) {
		unsigned int rotation;

		src_flags = 0x0;

		rotation = drm_rotation_simplify(state->rotation,
						 DRM_MODE_ROTATE_0 |
						 DRM_MODE_REFLECT_X |
						 DRM_MODE_REFLECT_Y);

		if (rotation & DRM_MODE_REFLECT_X)
			src_flags |= DPU_SSPP_FLIP_LR;

		if (rotation & DRM_MODE_REFLECT_Y)
			src_flags |= DPU_SSPP_FLIP_UD;

		/* update format */
		pdpu->pipe_hw->ops.setup_format(pdpu->pipe_hw, fmt, src_flags,
				pstate->multirect_index);

		if (pdpu->pipe_hw->ops.setup_cdp) {
			struct dpu_hw_pipe_cdp_cfg cdp_cfg;

			memset(&cdp_cfg, 0, sizeof(struct dpu_hw_pipe_cdp_cfg));

			cdp_cfg.enable = pdpu->catalog->perf.cdp_cfg
					[DPU_PERF_CDP_USAGE_RT].rd_enable;
			cdp_cfg.ubwc_meta_enable =
					DPU_FORMAT_IS_UBWC(fmt);
			cdp_cfg.tile_amortize_enable =
					DPU_FORMAT_IS_UBWC(fmt) ||
					DPU_FORMAT_IS_TILE(fmt);
			cdp_cfg.preload_ahead = DPU_SSPP_CDP_PRELOAD_AHEAD_64;

			pdpu->pipe_hw->ops.setup_cdp(pdpu->pipe_hw, &cdp_cfg, pstate->multirect_index);
		}
	}

	_dpu_plane_set_qos_lut(plane, fb, &pipe_cfg);
	_dpu_plane_set_danger_lut(plane, fb);

	if (plane->type != DRM_PLANE_TYPE_CURSOR) {
		_dpu_plane_set_qos_ctrl(plane, true, DPU_PLANE_QOS_PANIC_CTRL);
		_dpu_plane_set_ot_limit(plane, crtc, &pipe_cfg);
	}

	update_qos_remap = (is_rt_pipe != pdpu->is_rt_pipe) ||
			pstate->needs_qos_remap;

	if (update_qos_remap) {
		if (is_rt_pipe != pdpu->is_rt_pipe)
			pdpu->is_rt_pipe = is_rt_pipe;
		else if (pstate->needs_qos_remap)
			pstate->needs_qos_remap = false;
		_dpu_plane_set_qos_remap(plane);
	}

	_dpu_plane_calc_bw(plane, fb, &pipe_cfg);

	_dpu_plane_calc_clk(plane, &pipe_cfg);
}

static void _dpu_plane_atomic_disable(struct drm_plane *plane)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct dpu_plane_state *pstate = to_dpu_plane_state(state);

	trace_dpu_plane_disable(DRMID(plane), is_dpu_plane_virtual(plane),
				pstate->multirect_mode);

	pstate->pending = true;

	if (is_dpu_plane_virtual(plane) &&
			pdpu->pipe_hw && pdpu->pipe_hw->ops.setup_multirect)
		pdpu->pipe_hw->ops.setup_multirect(pdpu->pipe_hw,
				DPU_SSPP_RECT_SOLO, DPU_SSPP_MULTIRECT_NONE);
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

static void dpu_plane_destroy(struct drm_plane *plane)
{
	struct dpu_plane *pdpu = plane ? to_dpu_plane(plane) : NULL;

	DPU_DEBUG_PLANE(pdpu, "\n");

	if (pdpu) {
		_dpu_plane_set_qos_ctrl(plane, false, DPU_PLANE_QOS_PANIC_CTRL);

		mutex_destroy(&pdpu->lock);

		/* this will destroy the states as well */
		drm_plane_cleanup(plane);

		dpu_hw_sspp_destroy(pdpu->pipe_hw);

		kfree(pdpu);
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
	const struct dpu_plane *pdpu = to_dpu_plane(state->plane);

	drm_printf(p, "\tstage=%d\n", pstate->stage);
	drm_printf(p, "\tsspp=%s\n", pdpu->pipe_hw->cap->name);
	drm_printf(p, "\tmultirect_mode=%s\n", dpu_get_multirect_mode(pstate->multirect_mode));
	drm_printf(p, "\tmultirect_index=%s\n", dpu_get_multirect_index(pstate->multirect_index));
}

static void dpu_plane_reset(struct drm_plane *plane)
{
	struct dpu_plane *pdpu;
	struct dpu_plane_state *pstate;

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

	__drm_atomic_helper_plane_reset(plane, &pstate->base);
}

#ifdef CONFIG_DEBUG_FS
void dpu_plane_danger_signal_ctrl(struct drm_plane *plane, bool enable)
{
	struct dpu_plane *pdpu = to_dpu_plane(plane);
	struct dpu_kms *dpu_kms = _dpu_plane_get_kms(plane);

	if (!pdpu->is_rt_pipe)
		return;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	_dpu_plane_set_qos_ctrl(plane, enable, DPU_PLANE_QOS_PANIC_CTRL);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
}

/* SSPP live inside dpu_plane private data only. Enumerate them here. */
void dpu_debugfs_sspp_init(struct dpu_kms *dpu_kms, struct dentry *debugfs_root)
{
	struct drm_plane *plane;
	struct dentry *entry = debugfs_create_dir("sspp", debugfs_root);

	if (IS_ERR(entry))
		return;

	drm_for_each_plane(plane, dpu_kms->dev) {
		struct dpu_plane *pdpu = to_dpu_plane(plane);

		_dpu_hw_sspp_init_debugfs(pdpu->pipe_hw, dpu_kms, entry);
	}
}
#endif

static bool dpu_plane_format_mod_supported(struct drm_plane *plane,
		uint32_t format, uint64_t modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED) {
		int i;
		for (i = 0; i < ARRAY_SIZE(qcom_compressed_supported_formats); i++) {
			if (format == qcom_compressed_supported_formats[i])
				return true;
		}
	}

	return false;
}

static const struct drm_plane_funcs dpu_plane_funcs = {
		.update_plane = drm_atomic_helper_update_plane,
		.disable_plane = drm_atomic_helper_disable_plane,
		.destroy = dpu_plane_destroy,
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

enum dpu_sspp dpu_plane_pipe(struct drm_plane *plane)
{
	return plane ? to_dpu_plane(plane)->pipe : SSPP_NONE;
}

bool is_dpu_plane_virtual(struct drm_plane *plane)
{
	return plane ? to_dpu_plane(plane)->is_virtual : false;
}

/* initialize plane */
struct drm_plane *dpu_plane_init(struct drm_device *dev,
		uint32_t pipe, enum drm_plane_type type,
		unsigned long possible_crtcs, u32 master_plane_id)
{
	struct drm_plane *plane = NULL, *master_plane = NULL;
	const uint32_t *format_list;
	struct dpu_plane *pdpu;
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_kms *kms = to_dpu_kms(priv->kms);
	int zpos_max = DPU_ZPOS_MAX;
	uint32_t num_formats;
	int ret = -EINVAL;

	/* create and zero local structure */
	pdpu = kzalloc(sizeof(*pdpu), GFP_KERNEL);
	if (!pdpu) {
		DPU_ERROR("[%u]failed to allocate local plane struct\n", pipe);
		ret = -ENOMEM;
		return ERR_PTR(ret);
	}

	/* cache local stuff for later */
	plane = &pdpu->base;
	pdpu->pipe = pipe;
	pdpu->is_virtual = (master_plane_id != 0);
	INIT_LIST_HEAD(&pdpu->mplane_list);
	master_plane = drm_plane_find(dev, NULL, master_plane_id);
	if (master_plane) {
		struct dpu_plane *mpdpu = to_dpu_plane(master_plane);

		list_add_tail(&pdpu->mplane_list, &mpdpu->mplane_list);
	}

	/* initialize underlying h/w driver */
	pdpu->pipe_hw = dpu_hw_sspp_init(pipe, kms->mmio, kms->catalog,
							master_plane_id != 0);
	if (IS_ERR(pdpu->pipe_hw)) {
		DPU_ERROR("[%u]SSPP init failed\n", pipe);
		ret = PTR_ERR(pdpu->pipe_hw);
		goto clean_plane;
	} else if (!pdpu->pipe_hw->cap || !pdpu->pipe_hw->cap->sblk) {
		DPU_ERROR("[%u]SSPP init returned invalid cfg\n", pipe);
		goto clean_sspp;
	}

	if (pdpu->is_virtual) {
		format_list = pdpu->pipe_hw->cap->sblk->virt_format_list;
		num_formats = pdpu->pipe_hw->cap->sblk->virt_num_formats;
	}
	else {
		format_list = pdpu->pipe_hw->cap->sblk->format_list;
		num_formats = pdpu->pipe_hw->cap->sblk->num_formats;
	}

	ret = drm_universal_plane_init(dev, plane, 0xff, &dpu_plane_funcs,
				format_list, num_formats,
				supported_format_modifiers, type, NULL);
	if (ret)
		goto clean_sspp;

	pdpu->catalog = kms->catalog;

	if (kms->catalog->mixer_count &&
		kms->catalog->mixer[0].sblk->maxblendstages) {
		zpos_max = kms->catalog->mixer[0].sblk->maxblendstages - 1;
		if (zpos_max > DPU_STAGE_MAX - DPU_STAGE_0 - 1)
			zpos_max = DPU_STAGE_MAX - DPU_STAGE_0 - 1;
	}

	ret = drm_plane_create_zpos_property(plane, 0, 0, zpos_max);
	if (ret)
		DPU_ERROR("failed to install zpos property, rc = %d\n", ret);

	drm_plane_create_alpha_property(plane);
	drm_plane_create_blend_mode_property(plane,
			BIT(DRM_MODE_BLEND_PIXEL_NONE) |
			BIT(DRM_MODE_BLEND_PREMULTI) |
			BIT(DRM_MODE_BLEND_COVERAGE));

	drm_plane_create_rotation_property(plane,
			DRM_MODE_ROTATE_0,
			DRM_MODE_ROTATE_0 |
			DRM_MODE_ROTATE_180 |
			DRM_MODE_REFLECT_X |
			DRM_MODE_REFLECT_Y);

	drm_plane_enable_fb_damage_clips(plane);

	/* success! finalize initialization */
	drm_plane_helper_add(plane, &dpu_plane_helper_funcs);

	mutex_init(&pdpu->lock);

	DPU_DEBUG("%s created for pipe:%u id:%u virtual:%u\n", plane->name,
					pipe, plane->base.id, master_plane_id);
	return plane;

clean_sspp:
	if (pdpu && pdpu->pipe_hw)
		dpu_hw_sspp_destroy(pdpu->pipe_hw);
clean_plane:
	list_del(&pdpu->mplane_list);
	kfree(pdpu);
	return ERR_PTR(ret);
}
