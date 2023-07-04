// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 *
 * Author: Gaurav K Singh <gaurav.k.singh@intel.com>
 *         Manasi Navare <manasi.d.navare@intel.com>
 */
#include <linux/limits.h>

#include <drm/display/drm_dsc_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsi.h"
#include "intel_qp_tables.h"
#include "intel_vdsc.h"
#include "intel_vdsc_regs.h"

bool intel_dsc_source_support(const struct intel_crtc_state *crtc_state)
{
	const struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!HAS_DSC(i915))
		return false;

	if (DISPLAY_VER(i915) == 11 && cpu_transcoder == TRANSCODER_A)
		return false;

	return true;
}

static bool is_pipe_dsc(struct intel_crtc *crtc, enum transcoder cpu_transcoder)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (DISPLAY_VER(i915) >= 12)
		return true;

	if (cpu_transcoder == TRANSCODER_EDP ||
	    cpu_transcoder == TRANSCODER_DSI_0 ||
	    cpu_transcoder == TRANSCODER_DSI_1)
		return false;

	/* There's no pipe A DSC engine on ICL */
	drm_WARN_ON(&i915->drm, crtc->pipe == PIPE_A);

	return true;
}

static void
calculate_rc_params(struct drm_dsc_config *vdsc_cfg)
{
	int bpc = vdsc_cfg->bits_per_component;
	int bpp = vdsc_cfg->bits_per_pixel >> 4;
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
	int qp_bpc_modifier = (bpc - 8) * 2;
	u32 res, buf_i, bpp_i;

	if (vdsc_cfg->slice_height >= 8)
		vdsc_cfg->first_line_bpg_offset =
			12 + DIV_ROUND_UP((9 * min(34, vdsc_cfg->slice_height - 8)), 100);
	else
		vdsc_cfg->first_line_bpg_offset = 2 * (vdsc_cfg->slice_height - 1);

	/* Our hw supports only 444 modes as of today */
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

	bpp_i  = (2 * (bpp - 6));
	for (buf_i = 0; buf_i < DSC_NUM_BUF_RANGES; buf_i++) {
		u8 range_bpg_offset;

		/* Read range_minqp and range_max_qp from qp tables */
		vdsc_cfg->rc_range_params[buf_i].range_min_qp =
			intel_lookup_range_min_qp(bpc, buf_i, bpp_i, vdsc_cfg->native_420);
		vdsc_cfg->rc_range_params[buf_i].range_max_qp =
			intel_lookup_range_max_qp(bpc, buf_i, bpp_i, vdsc_cfg->native_420);

		/* Calculate range_bpg_offset */
		if (bpp <= 6) {
			range_bpg_offset = ofs_und6[buf_i];
		} else if (bpp <= 8) {
			res = DIV_ROUND_UP(((bpp - 6) * (ofs_und8[buf_i] - ofs_und6[buf_i])), 2);
			range_bpg_offset = ofs_und6[buf_i] + res;
		} else if (bpp <= 12) {
			range_bpg_offset = ofs_und8[buf_i];
		} else if (bpp <= 15) {
			res = DIV_ROUND_UP(((bpp - 12) * (ofs_und15[buf_i] - ofs_und12[buf_i])), 3);
			range_bpg_offset = ofs_und12[buf_i] + res;
		} else {
			range_bpg_offset = ofs_und15[buf_i];
		}

		vdsc_cfg->rc_range_params[buf_i].range_bpg_offset =
			range_bpg_offset & DSC_RANGE_BPG_OFFSET_MASK;
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

int intel_dsc_compute_params(struct intel_crtc_state *pipe_config)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct drm_dsc_config *vdsc_cfg = &pipe_config->dsc.config;
	u16 compressed_bpp = pipe_config->dsc.compressed_bpp;
	int err;
	int ret;

	vdsc_cfg->pic_width = pipe_config->hw.adjusted_mode.crtc_hdisplay;
	vdsc_cfg->slice_width = DIV_ROUND_UP(vdsc_cfg->pic_width,
					     pipe_config->dsc.slice_count);

	err = intel_dsc_slice_dimensions_valid(pipe_config, vdsc_cfg);

	if (err) {
		drm_dbg_kms(&dev_priv->drm, "Slice dimension requirements not met\n");
		return err;
	}

	/*
	 * According to DSC 1.2 specs if colorspace is YCbCr then convert_rgb is 0
	 * else 1
	 */
	vdsc_cfg->convert_rgb = pipe_config->output_format != INTEL_OUTPUT_FORMAT_YCBCR420 &&
				pipe_config->output_format != INTEL_OUTPUT_FORMAT_YCBCR444;

	if (DISPLAY_VER(dev_priv) >= 14 &&
	    pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		vdsc_cfg->native_420 = true;
	/* We do not support YcBCr422 as of now */
	vdsc_cfg->native_422 = false;
	vdsc_cfg->simple_422 = false;
	/* Gen 11 does not support VBR */
	vdsc_cfg->vbr_enable = false;

	/* Gen 11 only supports integral values of bpp */
	vdsc_cfg->bits_per_pixel = compressed_bpp << 4;

	/*
	 * According to DSC 1.2 specs in Section 4.1 if native_420 is set:
	 * -We need to double the current bpp.
	 * -second_line_bpg_offset is 12 in general and equal to 2*(slice_height-1) if slice
	 * height < 8.
	 * -second_line_offset_adj is 512 as shown by emperical values to yeild best chroma
	 * preservation in second line.
	 * -nsl_bpg_offset is calculated as second_line_offset/slice_height -1 then rounded
	 * up to 16 fractional bits, we left shift second line offset by 11 to preserve 11
	 * fractional bits.
	 */
	if (vdsc_cfg->native_420) {
		vdsc_cfg->bits_per_pixel <<= 1;

		if (vdsc_cfg->slice_height >= 8)
			vdsc_cfg->second_line_bpg_offset = 12;
		else
			vdsc_cfg->second_line_bpg_offset =
				2 * (vdsc_cfg->slice_height - 1);

		vdsc_cfg->second_line_offset_adj = 512;
		vdsc_cfg->nsl_bpg_offset = DIV_ROUND_UP(vdsc_cfg->second_line_bpg_offset << 11,
							vdsc_cfg->slice_height - 1);
	}

	vdsc_cfg->bits_per_component = pipe_config->pipe_bpp / 3;

	drm_dsc_set_rc_buf_thresh(vdsc_cfg);

	/*
	 * From XE_LPD onwards we supports compression bpps in steps of 1
	 * upto uncompressed bpp-1, hence add calculations for all the rc
	 * parameters
	 */
	if (DISPLAY_VER(dev_priv) >= 13) {
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

		/*
		 * FIXME: verify that the hardware actually needs these
		 * modifications rather than them being simple typos.
		 */
		if (compressed_bpp == 6 &&
		    vdsc_cfg->bits_per_component == 8)
			vdsc_cfg->rc_quant_incr_limit1 = 23;

		if (compressed_bpp == 8 &&
		    vdsc_cfg->bits_per_component == 14)
			vdsc_cfg->rc_range_params[0].range_bpg_offset = 0;
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
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
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
	if (DISPLAY_VER(i915) == 12 && !IS_ROCKETLAKE(i915) && pipe == PIPE_A)
		return POWER_DOMAIN_TRANSCODER_VDSC_PW2;
	else if (is_pipe_dsc(crtc, cpu_transcoder))
		return POWER_DOMAIN_PIPE(pipe);
	else
		return POWER_DOMAIN_TRANSCODER_VDSC_PW2;
}

int intel_dsc_get_num_vdsc_instances(const struct intel_crtc_state *crtc_state)
{
	int num_vdsc_instances = (crtc_state->dsc.dsc_split) ? 2 : 1;

	if (crtc_state->bigjoiner_pipes)
		num_vdsc_instances *= 2;

	return num_vdsc_instances;
}

static void intel_dsc_pps_configure(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	u32 pps_val = 0;
	u32 rc_buf_thresh_dword[4];
	u32 rc_range_params_dword[8];
	int i = 0;
	int num_vdsc_instances = intel_dsc_get_num_vdsc_instances(crtc_state);

	/* Populate PICTURE_PARAMETER_SET_0 registers */
	pps_val = DSC_VER_MAJ | vdsc_cfg->dsc_version_minor <<
		DSC_VER_MIN_SHIFT |
		vdsc_cfg->bits_per_component << DSC_BPC_SHIFT |
		vdsc_cfg->line_buf_depth << DSC_LINE_BUF_DEPTH_SHIFT;
	if (vdsc_cfg->dsc_version_minor == 2) {
		pps_val |= DSC_ALT_ICH_SEL;
		if (vdsc_cfg->native_420)
			pps_val |= DSC_NATIVE_420_ENABLE;
		if (vdsc_cfg->native_422)
			pps_val |= DSC_NATIVE_422_ENABLE;
	}
	if (vdsc_cfg->block_pred_enable)
		pps_val |= DSC_BLOCK_PREDICTION;
	if (vdsc_cfg->convert_rgb)
		pps_val |= DSC_COLOR_SPACE_CONVERSION;
	if (vdsc_cfg->simple_422)
		pps_val |= DSC_422_ENABLE;
	if (vdsc_cfg->vbr_enable)
		pps_val |= DSC_VBR_ENABLE;
	drm_dbg_kms(&dev_priv->drm, "PPS0 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_0,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_0,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_0(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_0(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_1 registers */
	pps_val = 0;
	pps_val |= DSC_BPP(vdsc_cfg->bits_per_pixel);
	drm_dbg_kms(&dev_priv->drm, "PPS1 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_1,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_1,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_1(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_1(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_2 registers */
	pps_val = 0;
	pps_val |= DSC_PIC_HEIGHT(vdsc_cfg->pic_height) |
		DSC_PIC_WIDTH(vdsc_cfg->pic_width / num_vdsc_instances);
	drm_dbg_kms(&dev_priv->drm, "PPS2 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_2,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_2,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_2(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_2(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_3 registers */
	pps_val = 0;
	pps_val |= DSC_SLICE_HEIGHT(vdsc_cfg->slice_height) |
		DSC_SLICE_WIDTH(vdsc_cfg->slice_width);
	drm_dbg_kms(&dev_priv->drm, "PPS3 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_3,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_3,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_3(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_3(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_4 registers */
	pps_val = 0;
	pps_val |= DSC_INITIAL_XMIT_DELAY(vdsc_cfg->initial_xmit_delay) |
		DSC_INITIAL_DEC_DELAY(vdsc_cfg->initial_dec_delay);
	drm_dbg_kms(&dev_priv->drm, "PPS4 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_4,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_4,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_4(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_4(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_5 registers */
	pps_val = 0;
	pps_val |= DSC_SCALE_INC_INT(vdsc_cfg->scale_increment_interval) |
		DSC_SCALE_DEC_INT(vdsc_cfg->scale_decrement_interval);
	drm_dbg_kms(&dev_priv->drm, "PPS5 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_5,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_5,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_5(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_5(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_6 registers */
	pps_val = 0;
	pps_val |= DSC_INITIAL_SCALE_VALUE(vdsc_cfg->initial_scale_value) |
		DSC_FIRST_LINE_BPG_OFFSET(vdsc_cfg->first_line_bpg_offset) |
		DSC_FLATNESS_MIN_QP(vdsc_cfg->flatness_min_qp) |
		DSC_FLATNESS_MAX_QP(vdsc_cfg->flatness_max_qp);
	drm_dbg_kms(&dev_priv->drm, "PPS6 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_6,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_6,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_6(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_6(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_7 registers */
	pps_val = 0;
	pps_val |= DSC_SLICE_BPG_OFFSET(vdsc_cfg->slice_bpg_offset) |
		DSC_NFL_BPG_OFFSET(vdsc_cfg->nfl_bpg_offset);
	drm_dbg_kms(&dev_priv->drm, "PPS7 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_7,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_7,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_7(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_7(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_8 registers */
	pps_val = 0;
	pps_val |= DSC_FINAL_OFFSET(vdsc_cfg->final_offset) |
		DSC_INITIAL_OFFSET(vdsc_cfg->initial_offset);
	drm_dbg_kms(&dev_priv->drm, "PPS8 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_8,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_8,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_8(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_8(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_9 registers */
	pps_val = 0;
	pps_val |= DSC_RC_MODEL_SIZE(vdsc_cfg->rc_model_size) |
		DSC_RC_EDGE_FACTOR(DSC_RC_EDGE_FACTOR_CONST);
	drm_dbg_kms(&dev_priv->drm, "PPS9 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_9,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv, DSCC_PICTURE_PARAMETER_SET_9,
				       pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_9(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_9(pipe),
				       pps_val);
	}

	/* Populate PICTURE_PARAMETER_SET_10 registers */
	pps_val = 0;
	pps_val |= DSC_RC_QUANT_INC_LIMIT0(vdsc_cfg->rc_quant_incr_limit0) |
		DSC_RC_QUANT_INC_LIMIT1(vdsc_cfg->rc_quant_incr_limit1) |
		DSC_RC_TARGET_OFF_HIGH(DSC_RC_TGT_OFFSET_HI_CONST) |
		DSC_RC_TARGET_OFF_LOW(DSC_RC_TGT_OFFSET_LO_CONST);
	drm_dbg_kms(&dev_priv->drm, "PPS10 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_10,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       DSCC_PICTURE_PARAMETER_SET_10, pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_10(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_10(pipe),
				       pps_val);
	}

	/* Populate Picture parameter set 16 */
	pps_val = 0;
	pps_val |= DSC_SLICE_CHUNK_SIZE(vdsc_cfg->slice_chunk_size) |
		DSC_SLICE_PER_LINE((vdsc_cfg->pic_width / num_vdsc_instances) /
				   vdsc_cfg->slice_width) |
		DSC_SLICE_ROW_PER_FRAME(vdsc_cfg->pic_height /
					vdsc_cfg->slice_height);
	drm_dbg_kms(&dev_priv->drm, "PPS16 = 0x%08x\n", pps_val);
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_PICTURE_PARAMETER_SET_16,
			       pps_val);
		/*
		 * If 2 VDSC instances are needed, configure PPS for second
		 * VDSC
		 */
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       DSCC_PICTURE_PARAMETER_SET_16, pps_val);
	} else {
		intel_de_write(dev_priv,
			       ICL_DSC0_PICTURE_PARAMETER_SET_16(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       ICL_DSC1_PICTURE_PARAMETER_SET_16(pipe),
				       pps_val);
	}

	if (DISPLAY_VER(dev_priv) >= 14) {
		/* Populate PICTURE_PARAMETER_SET_17 registers */
		pps_val = 0;
		pps_val |= DSC_SL_BPG_OFFSET(vdsc_cfg->second_line_bpg_offset);
		drm_dbg_kms(&dev_priv->drm, "PPS17 = 0x%08x\n", pps_val);
		intel_de_write(dev_priv,
			       MTL_DSC0_PICTURE_PARAMETER_SET_17(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       MTL_DSC1_PICTURE_PARAMETER_SET_17(pipe),
				       pps_val);

		/* Populate PICTURE_PARAMETER_SET_18 registers */
		pps_val = 0;
		pps_val |= DSC_NSL_BPG_OFFSET(vdsc_cfg->nsl_bpg_offset) |
			   DSC_SL_OFFSET_ADJ(vdsc_cfg->second_line_offset_adj);
		drm_dbg_kms(&dev_priv->drm, "PPS18 = 0x%08x\n", pps_val);
		intel_de_write(dev_priv,
			       MTL_DSC0_PICTURE_PARAMETER_SET_18(pipe),
			       pps_val);
		if (crtc_state->dsc.dsc_split)
			intel_de_write(dev_priv,
				       MTL_DSC1_PICTURE_PARAMETER_SET_18(pipe),
				       pps_val);
	}

	/* Populate the RC_BUF_THRESH registers */
	memset(rc_buf_thresh_dword, 0, sizeof(rc_buf_thresh_dword));
	for (i = 0; i < DSC_NUM_BUF_RANGES - 1; i++) {
		rc_buf_thresh_dword[i / 4] |=
			(u32)(vdsc_cfg->rc_buf_thresh[i] <<
			      BITS_PER_BYTE * (i % 4));
		drm_dbg_kms(&dev_priv->drm, "RC_BUF_THRESH_%d = 0x%08x\n", i,
			    rc_buf_thresh_dword[i / 4]);
	}
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_RC_BUF_THRESH_0,
			       rc_buf_thresh_dword[0]);
		intel_de_write(dev_priv, DSCA_RC_BUF_THRESH_0_UDW,
			       rc_buf_thresh_dword[1]);
		intel_de_write(dev_priv, DSCA_RC_BUF_THRESH_1,
			       rc_buf_thresh_dword[2]);
		intel_de_write(dev_priv, DSCA_RC_BUF_THRESH_1_UDW,
			       rc_buf_thresh_dword[3]);
		if (crtc_state->dsc.dsc_split) {
			intel_de_write(dev_priv, DSCC_RC_BUF_THRESH_0,
				       rc_buf_thresh_dword[0]);
			intel_de_write(dev_priv, DSCC_RC_BUF_THRESH_0_UDW,
				       rc_buf_thresh_dword[1]);
			intel_de_write(dev_priv, DSCC_RC_BUF_THRESH_1,
				       rc_buf_thresh_dword[2]);
			intel_de_write(dev_priv, DSCC_RC_BUF_THRESH_1_UDW,
				       rc_buf_thresh_dword[3]);
		}
	} else {
		intel_de_write(dev_priv, ICL_DSC0_RC_BUF_THRESH_0(pipe),
			       rc_buf_thresh_dword[0]);
		intel_de_write(dev_priv, ICL_DSC0_RC_BUF_THRESH_0_UDW(pipe),
			       rc_buf_thresh_dword[1]);
		intel_de_write(dev_priv, ICL_DSC0_RC_BUF_THRESH_1(pipe),
			       rc_buf_thresh_dword[2]);
		intel_de_write(dev_priv, ICL_DSC0_RC_BUF_THRESH_1_UDW(pipe),
			       rc_buf_thresh_dword[3]);
		if (crtc_state->dsc.dsc_split) {
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_BUF_THRESH_0(pipe),
				       rc_buf_thresh_dword[0]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_BUF_THRESH_0_UDW(pipe),
				       rc_buf_thresh_dword[1]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_BUF_THRESH_1(pipe),
				       rc_buf_thresh_dword[2]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_BUF_THRESH_1_UDW(pipe),
				       rc_buf_thresh_dword[3]);
		}
	}

	/* Populate the RC_RANGE_PARAMETERS registers */
	memset(rc_range_params_dword, 0, sizeof(rc_range_params_dword));
	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		rc_range_params_dword[i / 2] |=
			(u32)(((vdsc_cfg->rc_range_params[i].range_bpg_offset <<
				RC_BPG_OFFSET_SHIFT) |
			       (vdsc_cfg->rc_range_params[i].range_max_qp <<
				RC_MAX_QP_SHIFT) |
			       (vdsc_cfg->rc_range_params[i].range_min_qp <<
				RC_MIN_QP_SHIFT)) << 16 * (i % 2));
		drm_dbg_kms(&dev_priv->drm, "RC_RANGE_PARAM_%d = 0x%08x\n", i,
			    rc_range_params_dword[i / 2]);
	}
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_0,
			       rc_range_params_dword[0]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_0_UDW,
			       rc_range_params_dword[1]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_1,
			       rc_range_params_dword[2]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_1_UDW,
			       rc_range_params_dword[3]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_2,
			       rc_range_params_dword[4]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_2_UDW,
			       rc_range_params_dword[5]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_3,
			       rc_range_params_dword[6]);
		intel_de_write(dev_priv, DSCA_RC_RANGE_PARAMETERS_3_UDW,
			       rc_range_params_dword[7]);
		if (crtc_state->dsc.dsc_split) {
			intel_de_write(dev_priv, DSCC_RC_RANGE_PARAMETERS_0,
				       rc_range_params_dword[0]);
			intel_de_write(dev_priv,
				       DSCC_RC_RANGE_PARAMETERS_0_UDW,
				       rc_range_params_dword[1]);
			intel_de_write(dev_priv, DSCC_RC_RANGE_PARAMETERS_1,
				       rc_range_params_dword[2]);
			intel_de_write(dev_priv,
				       DSCC_RC_RANGE_PARAMETERS_1_UDW,
				       rc_range_params_dword[3]);
			intel_de_write(dev_priv, DSCC_RC_RANGE_PARAMETERS_2,
				       rc_range_params_dword[4]);
			intel_de_write(dev_priv,
				       DSCC_RC_RANGE_PARAMETERS_2_UDW,
				       rc_range_params_dword[5]);
			intel_de_write(dev_priv, DSCC_RC_RANGE_PARAMETERS_3,
				       rc_range_params_dword[6]);
			intel_de_write(dev_priv,
				       DSCC_RC_RANGE_PARAMETERS_3_UDW,
				       rc_range_params_dword[7]);
		}
	} else {
		intel_de_write(dev_priv, ICL_DSC0_RC_RANGE_PARAMETERS_0(pipe),
			       rc_range_params_dword[0]);
		intel_de_write(dev_priv,
			       ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW(pipe),
			       rc_range_params_dword[1]);
		intel_de_write(dev_priv, ICL_DSC0_RC_RANGE_PARAMETERS_1(pipe),
			       rc_range_params_dword[2]);
		intel_de_write(dev_priv,
			       ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW(pipe),
			       rc_range_params_dword[3]);
		intel_de_write(dev_priv, ICL_DSC0_RC_RANGE_PARAMETERS_2(pipe),
			       rc_range_params_dword[4]);
		intel_de_write(dev_priv,
			       ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW(pipe),
			       rc_range_params_dword[5]);
		intel_de_write(dev_priv, ICL_DSC0_RC_RANGE_PARAMETERS_3(pipe),
			       rc_range_params_dword[6]);
		intel_de_write(dev_priv,
			       ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW(pipe),
			       rc_range_params_dword[7]);
		if (crtc_state->dsc.dsc_split) {
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_0(pipe),
				       rc_range_params_dword[0]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW(pipe),
				       rc_range_params_dword[1]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_1(pipe),
				       rc_range_params_dword[2]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW(pipe),
				       rc_range_params_dword[3]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_2(pipe),
				       rc_range_params_dword[4]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW(pipe),
				       rc_range_params_dword[5]);
			intel_de_write(dev_priv,
				       ICL_DSC1_RC_RANGE_PARAMETERS_3(pipe),
				       rc_range_params_dword[6]);
			intel_de_write(dev_priv,
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
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dss_ctl1_val = 0;

	if (crtc_state->bigjoiner_pipes && !crtc_state->dsc.compression_enable) {
		if (intel_crtc_is_bigjoiner_slave(crtc_state))
			dss_ctl1_val |= UNCOMPRESSED_JOINER_SLAVE;
		else
			dss_ctl1_val |= UNCOMPRESSED_JOINER_MASTER;

		intel_de_write(dev_priv, dss_ctl1_reg(crtc, crtc_state->cpu_transcoder), dss_ctl1_val);
	}
}

void intel_dsc_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dss_ctl1_val = 0;
	u32 dss_ctl2_val = 0;

	if (!crtc_state->dsc.compression_enable)
		return;

	intel_dsc_pps_configure(crtc_state);

	dss_ctl2_val |= LEFT_BRANCH_VDSC_ENABLE;
	if (crtc_state->dsc.dsc_split) {
		dss_ctl2_val |= RIGHT_BRANCH_VDSC_ENABLE;
		dss_ctl1_val |= JOINER_ENABLE;
	}
	if (crtc_state->bigjoiner_pipes) {
		dss_ctl1_val |= BIG_JOINER_ENABLE;
		if (!intel_crtc_is_bigjoiner_slave(crtc_state))
			dss_ctl1_val |= MASTER_BIG_JOINER_ENABLE;
	}
	intel_de_write(dev_priv, dss_ctl1_reg(crtc, crtc_state->cpu_transcoder), dss_ctl1_val);
	intel_de_write(dev_priv, dss_ctl2_reg(crtc, crtc_state->cpu_transcoder), dss_ctl2_val);
}

void intel_dsc_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	/* Disable only if either of them is enabled */
	if (old_crtc_state->dsc.compression_enable ||
	    old_crtc_state->bigjoiner_pipes) {
		intel_de_write(dev_priv, dss_ctl1_reg(crtc, old_crtc_state->cpu_transcoder), 0);
		intel_de_write(dev_priv, dss_ctl2_reg(crtc, old_crtc_state->cpu_transcoder), 0);
	}
}

void intel_dsc_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	u32 dss_ctl1, dss_ctl2, pps0 = 0, pps1 = 0;

	if (!intel_dsc_source_support(crtc_state))
		return;

	power_domain = intel_dsc_power_domain(crtc, cpu_transcoder);

	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return;

	dss_ctl1 = intel_de_read(dev_priv, dss_ctl1_reg(crtc, cpu_transcoder));
	dss_ctl2 = intel_de_read(dev_priv, dss_ctl2_reg(crtc, cpu_transcoder));

	crtc_state->dsc.compression_enable = dss_ctl2 & LEFT_BRANCH_VDSC_ENABLE;
	if (!crtc_state->dsc.compression_enable)
		goto out;

	crtc_state->dsc.dsc_split = (dss_ctl2 & RIGHT_BRANCH_VDSC_ENABLE) &&
		(dss_ctl1 & JOINER_ENABLE);

	/* FIXME: add more state readout as needed */

	/* PPS0 & PPS1 */
	if (!is_pipe_dsc(crtc, cpu_transcoder)) {
		pps1 = intel_de_read(dev_priv, DSCA_PICTURE_PARAMETER_SET_1);
	} else {
		pps0 = intel_de_read(dev_priv,
				     ICL_DSC0_PICTURE_PARAMETER_SET_0(pipe));
		pps1 = intel_de_read(dev_priv,
				     ICL_DSC0_PICTURE_PARAMETER_SET_1(pipe));
	}

	vdsc_cfg->bits_per_pixel = pps1;

	if (pps0 & DSC_NATIVE_420_ENABLE)
		vdsc_cfg->bits_per_pixel >>= 1;

	crtc_state->dsc.compressed_bpp = vdsc_cfg->bits_per_pixel >> 4;
out:
	intel_display_power_put(dev_priv, power_domain, wakeref);
}
