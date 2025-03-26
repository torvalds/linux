// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 *
 * Author: Gaurav K Singh <gaurav.k.singh@intel.com>
 *         Manasi Navare <manasi.d.navare@intel.com>
 */
#include <linux/limits.h>

#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_fixed.h>
#include <drm/drm_print.h>

#include "i915_utils.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dsi.h"
#include "intel_qp_tables.h"
#include "intel_vdsc.h"
#include "intel_vdsc_regs.h"

bool intel_dsc_source_support(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!HAS_DSC(display))
		return false;

	if (DISPLAY_VER(display) == 11 && cpu_transcoder == TRANSCODER_A)
		return false;

	return true;
}

static bool is_pipe_dsc(struct intel_crtc *crtc, enum transcoder cpu_transcoder)
{
	struct intel_display *display = to_intel_display(crtc);

	if (DISPLAY_VER(display) >= 12)
		return true;

	if (cpu_transcoder == TRANSCODER_EDP ||
	    cpu_transcoder == TRANSCODER_DSI_0 ||
	    cpu_transcoder == TRANSCODER_DSI_1)
		return false;

	/* There's no pipe A DSC engine on ICL */
	drm_WARN_ON(display->drm, crtc->pipe == PIPE_A);

	return true;
}

static void
intel_vdsc_set_min_max_qp(struct drm_dsc_config *vdsc_cfg, int buf,
			  int bpp)
{
	int bpc = vdsc_cfg->bits_per_component;

	/* Read range_minqp and range_max_qp from qp tables */
	vdsc_cfg->rc_range_params[buf].range_min_qp =
		intel_lookup_range_min_qp(bpc, buf, bpp, vdsc_cfg->native_420);
	vdsc_cfg->rc_range_params[buf].range_max_qp =
		intel_lookup_range_max_qp(bpc, buf, bpp, vdsc_cfg->native_420);
}

static int
get_range_bpg_offset(int bpp_low, int offset_low, int bpp_high, int offset_high, int bpp)
{
	return offset_low + DIV_ROUND_UP((offset_high - offset_low) * (bpp - bpp_low),
					 (bpp_low - bpp_high));
}

/*
 * We are using the method provided in DSC 1.2a C-Model in codec_main.c
 * Above method use a common formula to derive values for any combination of DSC
 * variables. The formula approach may yield slight differences in the derived PPS
 * parameters from the original parameter sets. These differences are not consequential
 * to the coding performance because all parameter sets have been shown to produce
 * visually lossless quality (provides the same PPS values as
 * DSCParameterValuesVESA V1-2 spreadsheet).
 */
static void
calculate_rc_params(struct drm_dsc_config *vdsc_cfg)
{
	int bpp = fxp_q4_to_int(vdsc_cfg->bits_per_pixel);
	int bpc = vdsc_cfg->bits_per_component;
	int qp_bpc_modifier = (bpc - 8) * 2;
	int uncompressed_bpg_rate;
	int first_line_bpg_offset;
	u32 buf_i, bpp_i;

	if (vdsc_cfg->slice_height >= 8)
		first_line_bpg_offset =
			12 + (9 * min(34, vdsc_cfg->slice_height - 8)) / 100;
	else
		first_line_bpg_offset = 2 * (vdsc_cfg->slice_height - 1);

	uncompressed_bpg_rate = (3 * bpc + (vdsc_cfg->convert_rgb ? 0 : 2)) * 3;
	vdsc_cfg->first_line_bpg_offset = clamp(first_line_bpg_offset, 0,
						uncompressed_bpg_rate - 3 * bpp);

	/*
	 * According to DSC 1.2 spec in Section 4.1 if native_420 is set:
	 * -second_line_bpg_offset is 12 in general and equal to 2*(slice_height-1) if slice
	 * height < 8.
	 * -second_line_offset_adj is 512 as shown by empirical values to yield best chroma
	 * preservation in second line.
	 * -nsl_bpg_offset is calculated as second_line_offset/slice_height -1 then rounded
	 * up to 16 fractional bits, we left shift second line offset by 11 to preserve 11
	 * fractional bits.
	 */
	if (vdsc_cfg->native_420) {
		if (vdsc_cfg->slice_height >= 8)
			vdsc_cfg->second_line_bpg_offset = 12;
		else
			vdsc_cfg->second_line_bpg_offset =
				2 * (vdsc_cfg->slice_height - 1);

		vdsc_cfg->second_line_offset_adj = 512;
		vdsc_cfg->nsl_bpg_offset = DIV_ROUND_UP(vdsc_cfg->second_line_bpg_offset << 11,
							vdsc_cfg->slice_height - 1);
	}

	if (bpp >= 12)
		vdsc_cfg->initial_offset = 2048;
	else if (bpp >= 10)
		vdsc_cfg->initial_offset = 5632 - DIV_ROUND_UP(((bpp - 10) * 3584), 2);
	else if (bpp >= 8)
		vdsc_cfg->initial_offset = 6144 - DIV_ROUND_UP(((bpp - 8) * 512), 2);
	else
		vdsc_cfg->initial_offset = 6144;

	/* initial_xmit_delay = rc_model_size/2/compression_bpp */
	vdsc_cfg->initial_xmit_delay = DIV_ROUND_UP(DSC_RC_MODEL_SIZE_CONST, 2 * bpp);

	vdsc_cfg->flatness_min_qp = 3 + qp_bpc_modifier;
	vdsc_cfg->flatness_max_qp = 12 + qp_bpc_modifier;

	vdsc_cfg->rc_quant_incr_limit0 = 11 + qp_bpc_modifier;
	vdsc_cfg->rc_quant_incr_limit1 = 11 + qp_bpc_modifier;

	if (vdsc_cfg->native_420) {
		static const s8 ofs_und4[] = {
			2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12
		};
		static const s8 ofs_und5[] = {
			2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12
		};
		static const s8 ofs_und6[] = {
			2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12
		};
		static const s8 ofs_und8[] = {
			10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -10, -12, -12, -12
		};
		/*
		 * For 420 format since bits_per_pixel (bpp) is set to target bpp * 2,
		 * QP table values for target bpp 4.0 to 4.4375 (rounded to 4.0) are
		 * actually for bpp 8 to 8.875 (rounded to 4.0 * 2 i.e 8).
		 * Similarly values for target bpp 4.5 to 4.8375 (rounded to 4.5)
		 * are for bpp 9 to 9.875 (rounded to 4.5 * 2 i.e 9), and so on.
		 */
		bpp_i  = bpp - 8;
		for (buf_i = 0; buf_i < DSC_NUM_BUF_RANGES; buf_i++) {
			u8 range_bpg_offset;

			intel_vdsc_set_min_max_qp(vdsc_cfg, buf_i, bpp_i);

			/* Calculate range_bpg_offset */
			if (bpp <= 8)
				range_bpg_offset = ofs_und4[buf_i];
			else if (bpp <= 10)
				range_bpg_offset = get_range_bpg_offset(8, ofs_und4[buf_i],
									10, ofs_und5[buf_i], bpp);
			else if (bpp <= 12)
				range_bpg_offset = get_range_bpg_offset(10, ofs_und5[buf_i],
									12, ofs_und6[buf_i], bpp);
			else if (bpp <= 16)
				range_bpg_offset = get_range_bpg_offset(12, ofs_und6[buf_i],
									16, ofs_und8[buf_i], bpp);
			else
				range_bpg_offset = ofs_und8[buf_i];

			vdsc_cfg->rc_range_params[buf_i].range_bpg_offset =
				range_bpg_offset & DSC_RANGE_BPG_OFFSET_MASK;
		}
	} else {
		/* fractional bpp part * 10000 (for precision up to 4 decimal places) */
		int fractional_bits = fxp_q4_to_frac(vdsc_cfg->bits_per_pixel);

		static const s8 ofs_und6[] = {
			0, -2, -2, -4, -6, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12
		};
		static const s8 ofs_und8[] = {
			2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12
		};
		static const s8 ofs_und12[] = {
			2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12
		};
		static const s8 ofs_und15[] = {
			10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -10, -12, -12, -12
		};

		/*
		 * QP table rows have values in increment of 0.5.
		 * So 6.0 bpp to 6.4375 will have index 0, 6.5 to 6.9375 will have index 1,
		 * and so on.
		 * 0.5 fractional part with 4 decimal precision becomes 5000
		 */
		bpp_i  = ((bpp - 6) + (fractional_bits < 5000 ? 0 : 1));

		for (buf_i = 0; buf_i < DSC_NUM_BUF_RANGES; buf_i++) {
			u8 range_bpg_offset;

			intel_vdsc_set_min_max_qp(vdsc_cfg, buf_i, bpp_i);

			/* Calculate range_bpg_offset */
			if (bpp <= 6)
				range_bpg_offset = ofs_und6[buf_i];
			else if (bpp <= 8)
				range_bpg_offset = get_range_bpg_offset(6, ofs_und6[buf_i],
									8, ofs_und8[buf_i], bpp);
			else if (bpp <= 12)
				range_bpg_offset = get_range_bpg_offset(8, ofs_und8[buf_i],
									12, ofs_und12[buf_i], bpp);
			else if (bpp <= 15)
				range_bpg_offset = get_range_bpg_offset(12, ofs_und12[buf_i],
									15, ofs_und15[buf_i], bpp);
			else
				range_bpg_offset = ofs_und15[buf_i];

			vdsc_cfg->rc_range_params[buf_i].range_bpg_offset =
				range_bpg_offset & DSC_RANGE_BPG_OFFSET_MASK;
		}
	}
}

static int intel_dsc_slice_dimensions_valid(struct intel_crtc_state *pipe_config,
					    struct drm_dsc_config *vdsc_cfg)
{
	if (pipe_config->output_format == INTEL_OUTPUT_FORMAT_RGB ||
	    pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR444) {
		if (vdsc_cfg->slice_height > 4095)
			return -EINVAL;
		if (vdsc_cfg->slice_height * vdsc_cfg->slice_width < 15000)
			return -EINVAL;
	} else if (pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420) {
		if (vdsc_cfg->slice_width % 2)
			return -EINVAL;
		if (vdsc_cfg->slice_height % 2)
			return -EINVAL;
		if (vdsc_cfg->slice_height > 4094)
			return -EINVAL;
		if (vdsc_cfg->slice_height * vdsc_cfg->slice_width < 30000)
			return -EINVAL;
	}

	return 0;
}

static bool is_dsi_dsc_1_1(struct intel_crtc_state *crtc_state)
{
	struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;

	return vdsc_cfg->dsc_version_major == 1 &&
		vdsc_cfg->dsc_version_minor == 1 &&
		intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI);
}

int intel_dsc_compute_params(struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(pipe_config);
	struct drm_dsc_config *vdsc_cfg = &pipe_config->dsc.config;
	u16 compressed_bpp = fxp_q4_to_int(pipe_config->dsc.compressed_bpp_x16);
	int err;
	int ret;

	vdsc_cfg->pic_width = pipe_config->hw.adjusted_mode.crtc_hdisplay;
	vdsc_cfg->slice_width = DIV_ROUND_UP(vdsc_cfg->pic_width,
					     pipe_config->dsc.slice_count);

	err = intel_dsc_slice_dimensions_valid(pipe_config, vdsc_cfg);

	if (err) {
		drm_dbg_kms(display->drm, "Slice dimension requirements not met\n");
		return err;
	}

	/*
	 * According to DSC 1.2 specs if colorspace is YCbCr then convert_rgb is 0
	 * else 1
	 */
	vdsc_cfg->convert_rgb = pipe_config->output_format != INTEL_OUTPUT_FORMAT_YCBCR420 &&
				pipe_config->output_format != INTEL_OUTPUT_FORMAT_YCBCR444;

	if (DISPLAY_VER(display) >= 14 &&
	    pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		vdsc_cfg->native_420 = true;
	/* We do not support YcBCr422 as of now */
	vdsc_cfg->native_422 = false;
	vdsc_cfg->simple_422 = false;
	/* Gen 11 does not support VBR */
	vdsc_cfg->vbr_enable = false;

	vdsc_cfg->bits_per_pixel = pipe_config->dsc.compressed_bpp_x16;

	/*
	 * According to DSC 1.2 specs in Section 4.1 if native_420 is set
	 * we need to double the current bpp.
	 */
	if (vdsc_cfg->native_420)
		vdsc_cfg->bits_per_pixel <<= 1;

	vdsc_cfg->bits_per_component = pipe_config->pipe_bpp / 3;

	if (vdsc_cfg->bits_per_component < 8) {
		drm_dbg_kms(display->drm, "DSC bpc requirements not met bpc: %d\n",
			    vdsc_cfg->bits_per_component);
		return -EINVAL;
	}

	drm_dsc_set_rc_buf_thresh(vdsc_cfg);

	/*
	 * From XE_LPD onwards we supports compression bpps in steps of 1
	 * upto uncompressed bpp-1, hence add calculations for all the rc
	 * parameters
	 *
	 * We don't want to calculate all rc parameters when the panel
	 * is MIPI DSI and it's using DSC 1.1. The reason being that some
	 * DSI panels vendors have hardcoded PPS params in the VBT causing
	 * the parameters sent from the source which are derived through
	 * interpolation to differ from the params the panel expects.
	 * This causes a noise in the display.
	 * Furthermore for DSI panels we are currently using  bits_per_pixel
	 * (compressed bpp) hardcoded from VBT, (unlike other encoders where we
	 * find the optimum compressed bpp) so dont need to rely on interpolation,
	 * as we can get the required rc parameters from the tables.
	 */
	if (DISPLAY_VER(display) >= 13 && !is_dsi_dsc_1_1(pipe_config)) {
		calculate_rc_params(vdsc_cfg);
	} else {
		if ((compressed_bpp == 8 ||
		     compressed_bpp == 12) &&
		    (vdsc_cfg->bits_per_component == 8 ||
		     vdsc_cfg->bits_per_component == 10 ||
		     vdsc_cfg->bits_per_component == 12))
			ret = drm_dsc_setup_rc_params(vdsc_cfg, DRM_DSC_1_1_PRE_SCR);
		else
			ret = drm_dsc_setup_rc_params(vdsc_cfg, DRM_DSC_1_2_444);

		if (ret)
			return ret;
	}

	/*
	 * BitsPerComponent value determines mux_word_size:
	 * When BitsPerComponent is less than or 10bpc, muxWordSize will be equal to
	 * 48 bits otherwise 64
	 */
	if (vdsc_cfg->bits_per_component <= 10)
		vdsc_cfg->mux_word_size = DSC_MUX_WORD_SIZE_8_10_BPC;
	else
		vdsc_cfg->mux_word_size = DSC_MUX_WORD_SIZE_12_BPC;

	/* InitialScaleValue is a 6 bit value with 3 fractional bits (U3.3) */
	vdsc_cfg->initial_scale_value = (vdsc_cfg->rc_model_size << 3) /
		(vdsc_cfg->rc_model_size - vdsc_cfg->initial_offset);

	return 0;
}

enum intel_display_power_domain
intel_dsc_power_domain(struct intel_crtc *crtc, enum transcoder cpu_transcoder)
{
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;

	/*
	 * VDSC/joining uses a separate power well, PW2, and requires
	 * POWER_DOMAIN_TRANSCODER_VDSC_PW2 power domain in two cases:
	 *
	 *  - ICL eDP/DSI transcoder
	 *  - Display version 12 (except RKL) pipe A
	 *
	 * For any other pipe, VDSC/joining uses the power well associated with
	 * the pipe in use. Hence another reference on the pipe power domain
	 * will suffice. (Except no VDSC/joining on ICL pipe A.)
	 */
	if (DISPLAY_VER(display) == 12 && !display->platform.rocketlake &&
	    pipe == PIPE_A)
		return POWER_DOMAIN_TRANSCODER_VDSC_PW2;
	else if (is_pipe_dsc(crtc, cpu_transcoder))
		return POWER_DOMAIN_PIPE(pipe);
	else
		return POWER_DOMAIN_TRANSCODER_VDSC_PW2;
}

static int intel_dsc_get_vdsc_per_pipe(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->dsc.num_streams;
}

int intel_dsc_get_num_vdsc_instances(const struct intel_crtc_state *crtc_state)
{
	int num_vdsc_instances = intel_dsc_get_vdsc_per_pipe(crtc_state);
	int num_joined_pipes = intel_crtc_num_joined_pipes(crtc_state);

	num_vdsc_instances *= num_joined_pipes;

	return num_vdsc_instances;
}

static void intel_dsc_get_pps_reg(const struct intel_crtc_state *crtc_state, int pps,
				  i915_reg_t *dsc_reg, int dsc_reg_num)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	bool pipe_dsc;

	pipe_dsc = is_pipe_dsc(crtc, cpu_transcoder);

	if (dsc_reg_num >= 4)
		MISSING_CASE(dsc_reg_num);
	if (dsc_reg_num >= 3)
		dsc_reg[2] = BMG_DSC2_PPS(pipe, pps);
	if (dsc_reg_num >= 2)
		dsc_reg[1] = pipe_dsc ? ICL_DSC1_PPS(pipe, pps) : DSCC_PPS(pps);
	if (dsc_reg_num >= 1)
		dsc_reg[0] = pipe_dsc ? ICL_DSC0_PPS(pipe, pps) : DSCA_PPS(pps);
}

static void intel_dsc_pps_write(const struct intel_crtc_state *crtc_state,
				int pps, u32 pps_val)
{
	struct intel_display *display = to_intel_display(crtc_state);
	i915_reg_t dsc_reg[3];
	int i, vdsc_per_pipe, dsc_reg_num;

	vdsc_per_pipe = intel_dsc_get_vdsc_per_pipe(crtc_state);
	dsc_reg_num = min_t(int, ARRAY_SIZE(dsc_reg), vdsc_per_pipe);

	drm_WARN_ON_ONCE(display->drm, dsc_reg_num < vdsc_per_pipe);

	intel_dsc_get_pps_reg(crtc_state, pps, dsc_reg, dsc_reg_num);

	for (i = 0; i < dsc_reg_num; i++)
		intel_de_write(display, dsc_reg[i], pps_val);
}

static void intel_dsc_pps_configure(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	u32 pps_val;
	u32 rc_buf_thresh_dword[4];
	u32 rc_range_params_dword[8];
	int i = 0;
	int num_vdsc_instances = intel_dsc_get_num_vdsc_instances(crtc_state);
	int vdsc_instances_per_pipe = intel_dsc_get_vdsc_per_pipe(crtc_state);

	/* PPS 0 */
	pps_val = DSC_PPS0_VER_MAJOR(1) |
		DSC_PPS0_VER_MINOR(vdsc_cfg->dsc_version_minor) |
		DSC_PPS0_BPC(vdsc_cfg->bits_per_component) |
		DSC_PPS0_LINE_BUF_DEPTH(vdsc_cfg->line_buf_depth);
	if (vdsc_cfg->dsc_version_minor == 2) {
		pps_val |= DSC_PPS0_ALT_ICH_SEL;
		if (vdsc_cfg->native_420)
			pps_val |= DSC_PPS0_NATIVE_420_ENABLE;
		if (vdsc_cfg->native_422)
			pps_val |= DSC_PPS0_NATIVE_422_ENABLE;
	}
	if (vdsc_cfg->block_pred_enable)
		pps_val |= DSC_PPS0_BLOCK_PREDICTION;
	if (vdsc_cfg->convert_rgb)
		pps_val |= DSC_PPS0_COLOR_SPACE_CONVERSION;
	if (vdsc_cfg->simple_422)
		pps_val |= DSC_PPS0_422_ENABLE;
	if (vdsc_cfg->vbr_enable)
		pps_val |= DSC_PPS0_VBR_ENABLE;
	intel_dsc_pps_write(crtc_state, 0, pps_val);

	/* PPS 1 */
	pps_val = DSC_PPS1_BPP(vdsc_cfg->bits_per_pixel);
	intel_dsc_pps_write(crtc_state, 1, pps_val);

	/* PPS 2 */
	pps_val = DSC_PPS2_PIC_HEIGHT(vdsc_cfg->pic_height) |
		DSC_PPS2_PIC_WIDTH(vdsc_cfg->pic_width / num_vdsc_instances);
	intel_dsc_pps_write(crtc_state, 2, pps_val);

	/* PPS 3 */
	pps_val = DSC_PPS3_SLICE_HEIGHT(vdsc_cfg->slice_height) |
		DSC_PPS3_SLICE_WIDTH(vdsc_cfg->slice_width);
	intel_dsc_pps_write(crtc_state, 3, pps_val);

	/* PPS 4 */
	pps_val = DSC_PPS4_INITIAL_XMIT_DELAY(vdsc_cfg->initial_xmit_delay) |
		DSC_PPS4_INITIAL_DEC_DELAY(vdsc_cfg->initial_dec_delay);
	intel_dsc_pps_write(crtc_state, 4, pps_val);

	/* PPS 5 */
	pps_val = DSC_PPS5_SCALE_INC_INT(vdsc_cfg->scale_increment_interval) |
		DSC_PPS5_SCALE_DEC_INT(vdsc_cfg->scale_decrement_interval);
	intel_dsc_pps_write(crtc_state, 5, pps_val);

	/* PPS 6 */
	pps_val = DSC_PPS6_INITIAL_SCALE_VALUE(vdsc_cfg->initial_scale_value) |
		DSC_PPS6_FIRST_LINE_BPG_OFFSET(vdsc_cfg->first_line_bpg_offset) |
		DSC_PPS6_FLATNESS_MIN_QP(vdsc_cfg->flatness_min_qp) |
		DSC_PPS6_FLATNESS_MAX_QP(vdsc_cfg->flatness_max_qp);
	intel_dsc_pps_write(crtc_state, 6, pps_val);

	/* PPS 7 */
	pps_val = DSC_PPS7_SLICE_BPG_OFFSET(vdsc_cfg->slice_bpg_offset) |
		DSC_PPS7_NFL_BPG_OFFSET(vdsc_cfg->nfl_bpg_offset);
	intel_dsc_pps_write(crtc_state, 7, pps_val);

	/* PPS 8 */
	pps_val = DSC_PPS8_FINAL_OFFSET(vdsc_cfg->final_offset) |
		DSC_PPS8_INITIAL_OFFSET(vdsc_cfg->initial_offset);
	intel_dsc_pps_write(crtc_state, 8, pps_val);

	/* PPS 9 */
	pps_val = DSC_PPS9_RC_MODEL_SIZE(vdsc_cfg->rc_model_size) |
		DSC_PPS9_RC_EDGE_FACTOR(DSC_RC_EDGE_FACTOR_CONST);
	intel_dsc_pps_write(crtc_state, 9, pps_val);

	/* PPS 10 */
	pps_val = DSC_PPS10_RC_QUANT_INC_LIMIT0(vdsc_cfg->rc_quant_incr_limit0) |
		DSC_PPS10_RC_QUANT_INC_LIMIT1(vdsc_cfg->rc_quant_incr_limit1) |
		DSC_PPS10_RC_TARGET_OFF_HIGH(DSC_RC_TGT_OFFSET_HI_CONST) |
		DSC_PPS10_RC_TARGET_OFF_LOW(DSC_RC_TGT_OFFSET_LO_CONST);
	intel_dsc_pps_write(crtc_state, 10, pps_val);

	/* PPS 16 */
	pps_val = DSC_PPS16_SLICE_CHUNK_SIZE(vdsc_cfg->slice_chunk_size) |
		DSC_PPS16_SLICE_PER_LINE((vdsc_cfg->pic_width / num_vdsc_instances) /
					 vdsc_cfg->slice_width) |
		DSC_PPS16_SLICE_ROW_PER_FRAME(vdsc_cfg->pic_height /
					      vdsc_cfg->slice_height);
	intel_dsc_pps_write(crtc_state, 16, pps_val);

	if (DISPLAY_VER(display) >= 14) {
		/* PPS 17 */
		pps_val = DSC_PPS17_SL_BPG_OFFSET(vdsc_cfg->second_line_bpg_offset);
		intel_dsc_pps_write(crtc_state, 17, pps_val);

		/* PPS 18 */
		pps_val = DSC_PPS18_NSL_BPG_OFFSET(vdsc_cfg->nsl_bpg_offset) |
			DSC_PPS18_SL_OFFSET_ADJ(vdsc_cfg->second_line_offset_adj);
		intel_dsc_pps_write(crtc_state, 18, pps_val);
	}

	/* Populate the RC_BUF_THRESH registers */
	memset(rc_buf_thresh_dword, 0, sizeof(rc_buf_thresh_dword));
	for (i = 0; i < DSC_NUM_BUF_RANGES - 1; i++)
		rc_buf_thresh_dword[i / 4] |=
			(u32)(vdsc_cfg->rc_buf_thresh[i] <<
			      BITS_PER_BYTE * (i % 4));
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(display, DSCA_RC_BUF_THRESH_0,
			       rc_buf_thresh_dword[0]);
		intel_de_write(display, DSCA_RC_BUF_THRESH_0_UDW,
			       rc_buf_thresh_dword[1]);
		intel_de_write(display, DSCA_RC_BUF_THRESH_1,
			       rc_buf_thresh_dword[2]);
		intel_de_write(display, DSCA_RC_BUF_THRESH_1_UDW,
			       rc_buf_thresh_dword[3]);
		if (vdsc_instances_per_pipe > 1) {
			intel_de_write(display, DSCC_RC_BUF_THRESH_0,
				       rc_buf_thresh_dword[0]);
			intel_de_write(display, DSCC_RC_BUF_THRESH_0_UDW,
				       rc_buf_thresh_dword[1]);
			intel_de_write(display, DSCC_RC_BUF_THRESH_1,
				       rc_buf_thresh_dword[2]);
			intel_de_write(display, DSCC_RC_BUF_THRESH_1_UDW,
				       rc_buf_thresh_dword[3]);
		}
	} else {
		intel_de_write(display, ICL_DSC0_RC_BUF_THRESH_0(pipe),
			       rc_buf_thresh_dword[0]);
		intel_de_write(display, ICL_DSC0_RC_BUF_THRESH_0_UDW(pipe),
			       rc_buf_thresh_dword[1]);
		intel_de_write(display, ICL_DSC0_RC_BUF_THRESH_1(pipe),
			       rc_buf_thresh_dword[2]);
		intel_de_write(display, ICL_DSC0_RC_BUF_THRESH_1_UDW(pipe),
			       rc_buf_thresh_dword[3]);
		if (vdsc_instances_per_pipe > 1) {
			intel_de_write(display,
				       ICL_DSC1_RC_BUF_THRESH_0(pipe),
				       rc_buf_thresh_dword[0]);
			intel_de_write(display,
				       ICL_DSC1_RC_BUF_THRESH_0_UDW(pipe),
				       rc_buf_thresh_dword[1]);
			intel_de_write(display,
				       ICL_DSC1_RC_BUF_THRESH_1(pipe),
				       rc_buf_thresh_dword[2]);
			intel_de_write(display,
				       ICL_DSC1_RC_BUF_THRESH_1_UDW(pipe),
				       rc_buf_thresh_dword[3]);
		}
	}

	/* Populate the RC_RANGE_PARAMETERS registers */
	memset(rc_range_params_dword, 0, sizeof(rc_range_params_dword));
	for (i = 0; i < DSC_NUM_BUF_RANGES; i++)
		rc_range_params_dword[i / 2] |=
			(u32)(((vdsc_cfg->rc_range_params[i].range_bpg_offset <<
				RC_BPG_OFFSET_SHIFT) |
			       (vdsc_cfg->rc_range_params[i].range_max_qp <<
				RC_MAX_QP_SHIFT) |
			       (vdsc_cfg->rc_range_params[i].range_min_qp <<
				RC_MIN_QP_SHIFT)) << 16 * (i % 2));
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_0,
			       rc_range_params_dword[0]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_0_UDW,
			       rc_range_params_dword[1]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_1,
			       rc_range_params_dword[2]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_1_UDW,
			       rc_range_params_dword[3]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_2,
			       rc_range_params_dword[4]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_2_UDW,
			       rc_range_params_dword[5]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_3,
			       rc_range_params_dword[6]);
		intel_de_write(display, DSCA_RC_RANGE_PARAMETERS_3_UDW,
			       rc_range_params_dword[7]);
		if (vdsc_instances_per_pipe > 1) {
			intel_de_write(display, DSCC_RC_RANGE_PARAMETERS_0,
				       rc_range_params_dword[0]);
			intel_de_write(display,
				       DSCC_RC_RANGE_PARAMETERS_0_UDW,
				       rc_range_params_dword[1]);
			intel_de_write(display, DSCC_RC_RANGE_PARAMETERS_1,
				       rc_range_params_dword[2]);
			intel_de_write(display,
				       DSCC_RC_RANGE_PARAMETERS_1_UDW,
				       rc_range_params_dword[3]);
			intel_de_write(display, DSCC_RC_RANGE_PARAMETERS_2,
				       rc_range_params_dword[4]);
			intel_de_write(display,
				       DSCC_RC_RANGE_PARAMETERS_2_UDW,
				       rc_range_params_dword[5]);
			intel_de_write(display, DSCC_RC_RANGE_PARAMETERS_3,
				       rc_range_params_dword[6]);
			intel_de_write(display,
				       DSCC_RC_RANGE_PARAMETERS_3_UDW,
				       rc_range_params_dword[7]);
		}
	} else {
		intel_de_write(display, ICL_DSC0_RC_RANGE_PARAMETERS_0(pipe),
			       rc_range_params_dword[0]);
		intel_de_write(display,
			       ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW(pipe),
			       rc_range_params_dword[1]);
		intel_de_write(display, ICL_DSC0_RC_RANGE_PARAMETERS_1(pipe),
			       rc_range_params_dword[2]);
		intel_de_write(display,
			       ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW(pipe),
			       rc_range_params_dword[3]);
		intel_de_write(display, ICL_DSC0_RC_RANGE_PARAMETERS_2(pipe),
			       rc_range_params_dword[4]);
		intel_de_write(display,
			       ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW(pipe),
			       rc_range_params_dword[5]);
		intel_de_write(display, ICL_DSC0_RC_RANGE_PARAMETERS_3(pipe),
			       rc_range_params_dword[6]);
		intel_de_write(display,
			       ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW(pipe),
			       rc_range_params_dword[7]);
		if (vdsc_instances_per_pipe > 1) {
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_0(pipe),
				       rc_range_params_dword[0]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW(pipe),
				       rc_range_params_dword[1]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_1(pipe),
				       rc_range_params_dword[2]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW(pipe),
				       rc_range_params_dword[3]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_2(pipe),
				       rc_range_params_dword[4]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW(pipe),
				       rc_range_params_dword[5]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_3(pipe),
				       rc_range_params_dword[6]);
			intel_de_write(display,
				       ICL_DSC1_RC_RANGE_PARAMETERS_3_UDW(pipe),
				       rc_range_params_dword[7]);
		}
	}
}

void intel_dsc_dsi_pps_write(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	const struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct mipi_dsi_device *dsi;
	struct drm_dsc_picture_parameter_set pps;
	enum port port;

	if (!crtc_state->dsc.compression_enable)
		return;

	drm_dsc_pps_payload_pack(&pps, vdsc_cfg);

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi = intel_dsi->dsi_hosts[port]->device;

		mipi_dsi_picture_parameter_set(dsi, &pps);
		mipi_dsi_compression_mode(dsi, true);
	}
}

void intel_dsc_dp_pps_write(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	const struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	struct drm_dsc_pps_infoframe dp_dsc_pps_sdp;

	if (!crtc_state->dsc.compression_enable)
		return;

	/* Prepare DP SDP PPS header as per DP 1.4 spec, Table 2-123 */
	drm_dsc_dp_pps_header_init(&dp_dsc_pps_sdp.pps_header);

	/* Fill the PPS payload bytes as per DSC spec 1.2 Table 4-1 */
	drm_dsc_pps_payload_pack(&dp_dsc_pps_sdp.pps_payload, vdsc_cfg);

	dig_port->write_infoframe(encoder, crtc_state,
				  DP_SDP_PPS, &dp_dsc_pps_sdp,
				  sizeof(dp_dsc_pps_sdp));
}

static i915_reg_t dss_ctl1_reg(struct intel_crtc *crtc, enum transcoder cpu_transcoder)
{
	return is_pipe_dsc(crtc, cpu_transcoder) ?
		ICL_PIPE_DSS_CTL1(crtc->pipe) : DSS_CTL1;
}

static i915_reg_t dss_ctl2_reg(struct intel_crtc *crtc, enum transcoder cpu_transcoder)
{
	return is_pipe_dsc(crtc, cpu_transcoder) ?
		ICL_PIPE_DSS_CTL2(crtc->pipe) : DSS_CTL2;
}

void intel_uncompressed_joiner_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 dss_ctl1_val = 0;

	if (crtc_state->joiner_pipes && !crtc_state->dsc.compression_enable) {
		if (intel_crtc_is_bigjoiner_secondary(crtc_state))
			dss_ctl1_val |= UNCOMPRESSED_JOINER_SECONDARY;
		else
			dss_ctl1_val |= UNCOMPRESSED_JOINER_PRIMARY;

		intel_de_write(display, dss_ctl1_reg(crtc, crtc_state->cpu_transcoder),
			       dss_ctl1_val);
	}
}

void intel_dsc_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 dss_ctl1_val = 0;
	u32 dss_ctl2_val = 0;
	int vdsc_instances_per_pipe = intel_dsc_get_vdsc_per_pipe(crtc_state);

	if (!crtc_state->dsc.compression_enable)
		return;

	intel_dsc_pps_configure(crtc_state);

	dss_ctl2_val |= VDSC0_ENABLE;
	if (vdsc_instances_per_pipe > 1) {
		dss_ctl2_val |= VDSC1_ENABLE;
		dss_ctl1_val |= JOINER_ENABLE;
	}

	if (vdsc_instances_per_pipe > 2) {
		dss_ctl2_val |= VDSC2_ENABLE;
		dss_ctl2_val |= SMALL_JOINER_CONFIG_3_ENGINES;
	}

	if (crtc_state->joiner_pipes) {
		if (intel_crtc_ultrajoiner_enable_needed(crtc_state))
			dss_ctl1_val |= ULTRA_JOINER_ENABLE;

		if (intel_crtc_is_ultrajoiner_primary(crtc_state))
			dss_ctl1_val |= PRIMARY_ULTRA_JOINER_ENABLE;

		dss_ctl1_val |= BIG_JOINER_ENABLE;

		if (intel_crtc_is_bigjoiner_primary(crtc_state))
			dss_ctl1_val |= PRIMARY_BIG_JOINER_ENABLE;
	}
	intel_de_write(display, dss_ctl1_reg(crtc, crtc_state->cpu_transcoder), dss_ctl1_val);
	intel_de_write(display, dss_ctl2_reg(crtc, crtc_state->cpu_transcoder), dss_ctl2_val);
}

void intel_dsc_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);

	/* Disable only if either of them is enabled */
	if (old_crtc_state->dsc.compression_enable ||
	    old_crtc_state->joiner_pipes) {
		intel_de_write(display, dss_ctl1_reg(crtc, old_crtc_state->cpu_transcoder), 0);
		intel_de_write(display, dss_ctl2_reg(crtc, old_crtc_state->cpu_transcoder), 0);
	}
}

static u32 intel_dsc_pps_read(struct intel_crtc_state *crtc_state, int pps,
			      bool *all_equal)
{
	struct intel_display *display = to_intel_display(crtc_state);
	i915_reg_t dsc_reg[3];
	int i, vdsc_per_pipe, dsc_reg_num;
	u32 val;

	vdsc_per_pipe = intel_dsc_get_vdsc_per_pipe(crtc_state);
	dsc_reg_num = min_t(int, ARRAY_SIZE(dsc_reg), vdsc_per_pipe);

	drm_WARN_ON_ONCE(display->drm, dsc_reg_num < vdsc_per_pipe);

	intel_dsc_get_pps_reg(crtc_state, pps, dsc_reg, dsc_reg_num);

	*all_equal = true;

	val = intel_de_read(display, dsc_reg[0]);

	for (i = 1; i < dsc_reg_num; i++) {
		if (intel_de_read(display, dsc_reg[i]) != val) {
			*all_equal = false;
			break;
		}
	}

	return val;
}

static u32 intel_dsc_pps_read_and_verify(struct intel_crtc_state *crtc_state, int pps)
{
	struct intel_display *display = to_intel_display(crtc_state);
	u32 val;
	bool all_equal;

	val = intel_dsc_pps_read(crtc_state, pps, &all_equal);
	drm_WARN_ON(display->drm, !all_equal);

	return val;
}

static void intel_dsc_get_pps_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	int num_vdsc_instances = intel_dsc_get_num_vdsc_instances(crtc_state);
	u32 pps_temp;

	/* PPS 0 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 0);

	vdsc_cfg->bits_per_component = REG_FIELD_GET(DSC_PPS0_BPC_MASK, pps_temp);
	vdsc_cfg->line_buf_depth = REG_FIELD_GET(DSC_PPS0_LINE_BUF_DEPTH_MASK, pps_temp);
	vdsc_cfg->block_pred_enable = pps_temp & DSC_PPS0_BLOCK_PREDICTION;
	vdsc_cfg->convert_rgb = pps_temp & DSC_PPS0_COLOR_SPACE_CONVERSION;
	vdsc_cfg->simple_422 = pps_temp & DSC_PPS0_422_ENABLE;
	vdsc_cfg->native_422 = pps_temp & DSC_PPS0_NATIVE_422_ENABLE;
	vdsc_cfg->native_420 = pps_temp & DSC_PPS0_NATIVE_420_ENABLE;
	vdsc_cfg->vbr_enable = pps_temp & DSC_PPS0_VBR_ENABLE;

	/* PPS 1 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 1);

	vdsc_cfg->bits_per_pixel = REG_FIELD_GET(DSC_PPS1_BPP_MASK, pps_temp);

	if (vdsc_cfg->native_420)
		vdsc_cfg->bits_per_pixel >>= 1;

	crtc_state->dsc.compressed_bpp_x16 = vdsc_cfg->bits_per_pixel;

	/* PPS 2 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 2);

	vdsc_cfg->pic_width = REG_FIELD_GET(DSC_PPS2_PIC_WIDTH_MASK, pps_temp) * num_vdsc_instances;
	vdsc_cfg->pic_height = REG_FIELD_GET(DSC_PPS2_PIC_HEIGHT_MASK, pps_temp);

	/* PPS 3 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 3);

	vdsc_cfg->slice_width = REG_FIELD_GET(DSC_PPS3_SLICE_WIDTH_MASK, pps_temp);
	vdsc_cfg->slice_height = REG_FIELD_GET(DSC_PPS3_SLICE_HEIGHT_MASK, pps_temp);

	/* PPS 4 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 4);

	vdsc_cfg->initial_dec_delay = REG_FIELD_GET(DSC_PPS4_INITIAL_DEC_DELAY_MASK, pps_temp);
	vdsc_cfg->initial_xmit_delay = REG_FIELD_GET(DSC_PPS4_INITIAL_XMIT_DELAY_MASK, pps_temp);

	/* PPS 5 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 5);

	vdsc_cfg->scale_decrement_interval = REG_FIELD_GET(DSC_PPS5_SCALE_DEC_INT_MASK, pps_temp);
	vdsc_cfg->scale_increment_interval = REG_FIELD_GET(DSC_PPS5_SCALE_INC_INT_MASK, pps_temp);

	/* PPS 6 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 6);

	vdsc_cfg->initial_scale_value = REG_FIELD_GET(DSC_PPS6_INITIAL_SCALE_VALUE_MASK, pps_temp);
	vdsc_cfg->first_line_bpg_offset = REG_FIELD_GET(DSC_PPS6_FIRST_LINE_BPG_OFFSET_MASK, pps_temp);
	vdsc_cfg->flatness_min_qp = REG_FIELD_GET(DSC_PPS6_FLATNESS_MIN_QP_MASK, pps_temp);
	vdsc_cfg->flatness_max_qp = REG_FIELD_GET(DSC_PPS6_FLATNESS_MAX_QP_MASK, pps_temp);

	/* PPS 7 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 7);

	vdsc_cfg->nfl_bpg_offset = REG_FIELD_GET(DSC_PPS7_NFL_BPG_OFFSET_MASK, pps_temp);
	vdsc_cfg->slice_bpg_offset = REG_FIELD_GET(DSC_PPS7_SLICE_BPG_OFFSET_MASK, pps_temp);

	/* PPS 8 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 8);

	vdsc_cfg->initial_offset = REG_FIELD_GET(DSC_PPS8_INITIAL_OFFSET_MASK, pps_temp);
	vdsc_cfg->final_offset = REG_FIELD_GET(DSC_PPS8_FINAL_OFFSET_MASK, pps_temp);

	/* PPS 9 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 9);

	vdsc_cfg->rc_model_size = REG_FIELD_GET(DSC_PPS9_RC_MODEL_SIZE_MASK, pps_temp);

	/* PPS 10 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 10);

	vdsc_cfg->rc_quant_incr_limit0 = REG_FIELD_GET(DSC_PPS10_RC_QUANT_INC_LIMIT0_MASK, pps_temp);
	vdsc_cfg->rc_quant_incr_limit1 = REG_FIELD_GET(DSC_PPS10_RC_QUANT_INC_LIMIT1_MASK, pps_temp);

	/* PPS 16 */
	pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 16);

	vdsc_cfg->slice_chunk_size = REG_FIELD_GET(DSC_PPS16_SLICE_CHUNK_SIZE_MASK, pps_temp);

	if (DISPLAY_VER(display) >= 14) {
		/* PPS 17 */
		pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 17);

		vdsc_cfg->second_line_bpg_offset = REG_FIELD_GET(DSC_PPS17_SL_BPG_OFFSET_MASK, pps_temp);

		/* PPS 18 */
		pps_temp = intel_dsc_pps_read_and_verify(crtc_state, 18);

		vdsc_cfg->nsl_bpg_offset = REG_FIELD_GET(DSC_PPS18_NSL_BPG_OFFSET_MASK, pps_temp);
		vdsc_cfg->second_line_offset_adj = REG_FIELD_GET(DSC_PPS18_SL_OFFSET_ADJ_MASK, pps_temp);
	}
}

void intel_dsc_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	u32 dss_ctl1, dss_ctl2;

	if (!intel_dsc_source_support(crtc_state))
		return;

	power_domain = intel_dsc_power_domain(crtc, cpu_transcoder);

	wakeref = intel_display_power_get_if_enabled(display, power_domain);
	if (!wakeref)
		return;

	dss_ctl1 = intel_de_read(display, dss_ctl1_reg(crtc, cpu_transcoder));
	dss_ctl2 = intel_de_read(display, dss_ctl2_reg(crtc, cpu_transcoder));

	crtc_state->dsc.compression_enable = dss_ctl2 & VDSC0_ENABLE;
	if (!crtc_state->dsc.compression_enable)
		goto out;

	if (dss_ctl1 & JOINER_ENABLE && dss_ctl2 & (VDSC2_ENABLE | SMALL_JOINER_CONFIG_3_ENGINES))
		crtc_state->dsc.num_streams = 3;
	else if (dss_ctl1 & JOINER_ENABLE && dss_ctl2 & VDSC1_ENABLE)
		crtc_state->dsc.num_streams = 2;
	else
		crtc_state->dsc.num_streams = 1;

	intel_dsc_get_pps_config(crtc_state);
out:
	intel_display_power_put(display, power_domain, wakeref);
}

static void intel_vdsc_dump_state(struct drm_printer *p, int indent,
				  const struct intel_crtc_state *crtc_state)
{
	drm_printf_indent(p, indent,
			  "dsc-dss: compressed-bpp:" FXP_Q4_FMT ", slice-count: %d, num_streams: %d\n",
			  FXP_Q4_ARGS(crtc_state->dsc.compressed_bpp_x16),
			  crtc_state->dsc.slice_count,
			  crtc_state->dsc.num_streams);
}

void intel_vdsc_state_dump(struct drm_printer *p, int indent,
			   const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->dsc.compression_enable)
		return;

	intel_vdsc_dump_state(p, indent, crtc_state);
	drm_dsc_dump_config(p, indent, &crtc_state->dsc.config);
}

int intel_vdsc_min_cdclk(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	int num_vdsc_instances = intel_dsc_get_num_vdsc_instances(crtc_state);
	int min_cdclk;

	if (!crtc_state->dsc.compression_enable)
		return 0;

	/*
	 * When we decide to use only one VDSC engine, since
	 * each VDSC operates with 1 ppc throughput, pixel clock
	 * cannot be higher than the VDSC clock (cdclk)
	 * If there 2 VDSC engines, then pixel clock can't be higher than
	 * VDSC clock(cdclk) * 2 and so on.
	 */
	min_cdclk = DIV_ROUND_UP(crtc_state->pixel_rate, num_vdsc_instances);

	if (crtc_state->joiner_pipes) {
		int pixel_clock = intel_dp_mode_to_fec_clock(crtc_state->hw.adjusted_mode.clock);

		/*
		 * According to Bigjoiner bw check:
		 * compressed_bpp <= PPC * CDCLK * Big joiner Interface bits / Pixel clock
		 *
		 * We have already computed compressed_bpp, so now compute the min CDCLK that
		 * is required to support this compressed_bpp.
		 *
		 * => CDCLK >= compressed_bpp * Pixel clock / (PPC * Bigjoiner Interface bits)
		 *
		 * Since PPC = 2 with bigjoiner
		 * => CDCLK >= compressed_bpp * Pixel clock  / 2 * Bigjoiner Interface bits
		 */
		int bigjoiner_interface_bits = DISPLAY_VER(display) >= 14 ? 36 : 24;
		int min_cdclk_bj =
			(fxp_q4_to_int_roundup(crtc_state->dsc.compressed_bpp_x16) *
			 pixel_clock) / (2 * bigjoiner_interface_bits);

		min_cdclk = max(min_cdclk, min_cdclk_bj);
	}

	return min_cdclk;
}
