/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <drm/display/drm_dsc_helper.h>

#include "reg_helper.h"
#include "dcn20_dsc.h"
#include "dsc/dscc_types.h"
#include "dsc/rc_calc.h"

static void dsc_write_to_registers(struct display_stream_compressor *dsc, const struct dsc_reg_values *reg_vals);

/* Object I/F functions */
static void dsc2_read_state(struct display_stream_compressor *dsc, struct dcn_dsc_state *s);
static bool dsc2_validate_stream(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg);
static void dsc2_set_config(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
		struct dsc_optc_config *dsc_optc_cfg);
static void dsc2_enable(struct display_stream_compressor *dsc, int opp_pipe);
static void dsc2_disable(struct display_stream_compressor *dsc);
static void dsc2_disconnect(struct display_stream_compressor *dsc);

static const struct dsc_funcs dcn20_dsc_funcs = {
	.dsc_get_enc_caps = dsc2_get_enc_caps,
	.dsc_read_state = dsc2_read_state,
	.dsc_validate_stream = dsc2_validate_stream,
	.dsc_set_config = dsc2_set_config,
	.dsc_get_packed_pps = dsc2_get_packed_pps,
	.dsc_enable = dsc2_enable,
	.dsc_disable = dsc2_disable,
	.dsc_disconnect = dsc2_disconnect,
};

/* Macro definitios for REG_SET macros*/
#define CTX \
	dsc20->base.ctx

#define REG(reg)\
	dsc20->dsc_regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dsc20->dsc_shift->field_name, dsc20->dsc_mask->field_name
#define DC_LOGGER \
	dsc->ctx->logger

enum dsc_bits_per_comp {
	DSC_BPC_8 = 8,
	DSC_BPC_10 = 10,
	DSC_BPC_12 = 12,
	DSC_BPC_UNKNOWN
};

/* API functions (external or via structure->function_pointer) */

void dsc2_construct(struct dcn20_dsc *dsc,
		struct dc_context *ctx,
		int inst,
		const struct dcn20_dsc_registers *dsc_regs,
		const struct dcn20_dsc_shift *dsc_shift,
		const struct dcn20_dsc_mask *dsc_mask)
{
	dsc->base.ctx = ctx;
	dsc->base.inst = inst;
	dsc->base.funcs = &dcn20_dsc_funcs;

	dsc->dsc_regs = dsc_regs;
	dsc->dsc_shift = dsc_shift;
	dsc->dsc_mask = dsc_mask;

	dsc->max_image_width = 5184;
}


#define DCN20_MAX_PIXEL_CLOCK_Mhz      1188
#define DCN20_MAX_DISPLAY_CLOCK_Mhz    1200

/* This returns the capabilities for a single DSC encoder engine. Number of slices and total throughput
 * can be doubled, tripled etc. by using additional DSC engines.
 */
void dsc2_get_enc_caps(struct dsc_enc_caps *dsc_enc_caps, int pixel_clock_100Hz)
{
	dsc_enc_caps->dsc_version = 0x21; /* v1.2 - DP spec defined it in reverse order and we kept it */

	dsc_enc_caps->slice_caps.bits.NUM_SLICES_1 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_2 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_3 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_4 = 1;

	dsc_enc_caps->lb_bit_depth = 13;
	dsc_enc_caps->is_block_pred_supported = true;

	dsc_enc_caps->color_formats.bits.RGB = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_444 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_SIMPLE_422 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_NATIVE_422 = 0;
	dsc_enc_caps->color_formats.bits.YCBCR_NATIVE_420 = 1;

	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_8_BPC = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_10_BPC = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_12_BPC = 1;

	/* Maximum total throughput with all the slices combined. This is different from how DP spec specifies it.
	 * Our decoder's total throughput in Pix/s is equal to DISPCLK. This is then shared between slices.
	 * The value below is the absolute maximum value. The actual throughput may be lower, but it'll always
	 * be sufficient to process the input pixel rate fed into a single DSC engine.
	 */
	dsc_enc_caps->max_total_throughput_mps = DCN20_MAX_DISPLAY_CLOCK_Mhz;

	/* For pixel clock bigger than a single-pipe limit we'll need two engines, which then doubles our
	 * throughput and number of slices, but also introduces a lower limit of 2 slices
	 */
	if (pixel_clock_100Hz >= DCN20_MAX_PIXEL_CLOCK_Mhz*10000) {
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_1 = 0;
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_8 = 1;
		dsc_enc_caps->max_total_throughput_mps = DCN20_MAX_DISPLAY_CLOCK_Mhz * 2;
	}

	/* For pixel clock bigger than a single-pipe limit needing four engines ODM 4:1, which then quardruples our
	 * throughput and number of slices
	 */
	if (pixel_clock_100Hz > DCN20_MAX_PIXEL_CLOCK_Mhz*10000*2) {
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_12 = 1;
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_16 = 1;
		dsc_enc_caps->max_total_throughput_mps = DCN20_MAX_DISPLAY_CLOCK_Mhz * 4;
	}

	dsc_enc_caps->max_slice_width = 5184; /* (including 64 overlap pixels for eDP MSO mode) */
	dsc_enc_caps->bpp_increment_div = 16; /* 1/16th of a bit */
}


/* this function read dsc related register fields to be logged later in dcn10_log_hw_state
 * into a dcn_dsc_state struct.
 */
static void dsc2_read_state(struct display_stream_compressor *dsc, struct dcn_dsc_state *s)
{
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &s->dsc_clock_en);
	REG_GET(DSCC_PPS_CONFIG3, SLICE_WIDTH, &s->dsc_slice_width);
	REG_GET(DSCC_PPS_CONFIG1, BITS_PER_PIXEL, &s->dsc_bits_per_pixel);
	REG_GET(DSCC_PPS_CONFIG3, SLICE_HEIGHT, &s->dsc_slice_height);
	REG_GET(DSCC_PPS_CONFIG1, CHUNK_SIZE, &s->dsc_chunk_size);
	REG_GET(DSCC_PPS_CONFIG2, PIC_WIDTH, &s->dsc_pic_width);
	REG_GET(DSCC_PPS_CONFIG2, PIC_HEIGHT, &s->dsc_pic_height);
	REG_GET(DSCC_PPS_CONFIG7, SLICE_BPG_OFFSET, &s->dsc_slice_bpg_offset);
	REG_GET_2(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN, &s->dsc_fw_en,
		DSCRM_DSC_OPP_PIPE_SOURCE, &s->dsc_opp_source);
}


static bool dsc2_validate_stream(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg)
{
	struct dsc_optc_config dsc_optc_cfg;
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);

	if (dsc_cfg->pic_width > dsc20->max_image_width)
		return false;

	return dsc_prepare_config(dsc_cfg, &dsc20->reg_vals, &dsc_optc_cfg);
}


void dsc_config_log(struct display_stream_compressor *dsc, const struct dsc_config *config)
{
	DC_LOG_DSC("\tnum_slices_h %d", config->dc_dsc_cfg.num_slices_h);
	DC_LOG_DSC("\tnum_slices_v %d", config->dc_dsc_cfg.num_slices_v);
	DC_LOG_DSC("\tbits_per_pixel %d (%d.%04d)",
		config->dc_dsc_cfg.bits_per_pixel,
		config->dc_dsc_cfg.bits_per_pixel / 16,
		((config->dc_dsc_cfg.bits_per_pixel % 16) * 10000) / 16);
	DC_LOG_DSC("\tcolor_depth %d", config->color_depth);
}

static void dsc2_set_config(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
		struct dsc_optc_config *dsc_optc_cfg)
{
	bool is_config_ok;
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);

	DC_LOG_DSC("Setting DSC Config at DSC inst %d", dsc->inst);
	dsc_config_log(dsc, dsc_cfg);
	is_config_ok = dsc_prepare_config(dsc_cfg, &dsc20->reg_vals, dsc_optc_cfg);
	ASSERT(is_config_ok);
	DC_LOG_DSC("programming DSC Picture Parameter Set (PPS):");
	dsc_log_pps(dsc, &dsc20->reg_vals.pps);
	dsc_write_to_registers(dsc, &dsc20->reg_vals);
}


bool dsc2_get_packed_pps(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg, uint8_t *dsc_packed_pps)
{
	bool is_config_ok;
	struct dsc_reg_values dsc_reg_vals;
	struct dsc_optc_config dsc_optc_cfg;

	memset(&dsc_reg_vals, 0, sizeof(dsc_reg_vals));
	memset(&dsc_optc_cfg, 0, sizeof(dsc_optc_cfg));

	DC_LOG_DSC("Getting packed DSC PPS for DSC Config:");
	dsc_config_log(dsc, dsc_cfg);
	DC_LOG_DSC("DSC Picture Parameter Set (PPS):");
	is_config_ok = dsc_prepare_config(dsc_cfg, &dsc_reg_vals, &dsc_optc_cfg);
	ASSERT(is_config_ok);
	drm_dsc_pps_payload_pack((struct drm_dsc_picture_parameter_set *)dsc_packed_pps, &dsc_reg_vals.pps);
	dsc_log_pps(dsc, &dsc_reg_vals.pps);

	return is_config_ok;
}


static void dsc2_enable(struct display_stream_compressor *dsc, int opp_pipe)
{
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);
	int dsc_clock_en;
	int dsc_fw_config;
	int enabled_opp_pipe;

	DC_LOG_DSC("enable DSC %d at opp pipe %d", dsc->inst, opp_pipe);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &dsc_clock_en);
	REG_GET_2(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN, &dsc_fw_config, DSCRM_DSC_OPP_PIPE_SOURCE, &enabled_opp_pipe);
	if ((dsc_clock_en || dsc_fw_config) && enabled_opp_pipe != opp_pipe) {
		DC_LOG_DSC("ERROR: DSC %d at opp pipe %d already enabled!", dsc->inst, enabled_opp_pipe);
		ASSERT(0);
	}

	REG_UPDATE(DSC_TOP_CONTROL,
		DSC_CLOCK_EN, 1);

	REG_UPDATE_2(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 1,
		DSCRM_DSC_OPP_PIPE_SOURCE, opp_pipe);
}


static void dsc2_disable(struct display_stream_compressor *dsc)
{
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);
	int dsc_clock_en;
	int dsc_fw_config;
	int enabled_opp_pipe;

	DC_LOG_DSC("disable DSC %d", dsc->inst);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &dsc_clock_en);
	REG_GET_2(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN, &dsc_fw_config, DSCRM_DSC_OPP_PIPE_SOURCE, &enabled_opp_pipe);
	if (!dsc_clock_en || !dsc_fw_config) {
		DC_LOG_DSC("ERROR: DSC %d at opp pipe %d already disabled!", dsc->inst, enabled_opp_pipe);
		ASSERT(0);
	}

	REG_UPDATE(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 0);

	REG_UPDATE(DSC_TOP_CONTROL,
		DSC_CLOCK_EN, 0);
}

static void dsc2_disconnect(struct display_stream_compressor *dsc)
{
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);

	DC_LOG_DSC("disconnect DSC %d", dsc->inst);

	REG_UPDATE(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 0);
}

/* This module's internal functions */
void dsc_log_pps(struct display_stream_compressor *dsc, struct drm_dsc_config *pps)
{
	int i;
	int bits_per_pixel = pps->bits_per_pixel;

	DC_LOG_DSC("\tdsc_version_major %d", pps->dsc_version_major);
	DC_LOG_DSC("\tdsc_version_minor %d", pps->dsc_version_minor);
	DC_LOG_DSC("\tbits_per_component %d", pps->bits_per_component);
	DC_LOG_DSC("\tline_buf_depth %d", pps->line_buf_depth);
	DC_LOG_DSC("\tblock_pred_enable %d", pps->block_pred_enable);
	DC_LOG_DSC("\tconvert_rgb %d", pps->convert_rgb);
	DC_LOG_DSC("\tsimple_422 %d", pps->simple_422);
	DC_LOG_DSC("\tvbr_enable %d", pps->vbr_enable);
	DC_LOG_DSC("\tbits_per_pixel %d (%d.%04d)", bits_per_pixel, bits_per_pixel / 16, ((bits_per_pixel % 16) * 10000) / 16);
	DC_LOG_DSC("\tpic_height %d", pps->pic_height);
	DC_LOG_DSC("\tpic_width %d", pps->pic_width);
	DC_LOG_DSC("\tslice_height %d", pps->slice_height);
	DC_LOG_DSC("\tslice_width %d", pps->slice_width);
	DC_LOG_DSC("\tslice_chunk_size %d", pps->slice_chunk_size);
	DC_LOG_DSC("\tinitial_xmit_delay %d", pps->initial_xmit_delay);
	DC_LOG_DSC("\tinitial_dec_delay %d", pps->initial_dec_delay);
	DC_LOG_DSC("\tinitial_scale_value %d", pps->initial_scale_value);
	DC_LOG_DSC("\tscale_increment_interval %d", pps->scale_increment_interval);
	DC_LOG_DSC("\tscale_decrement_interval %d", pps->scale_decrement_interval);
	DC_LOG_DSC("\tfirst_line_bpg_offset %d", pps->first_line_bpg_offset);
	DC_LOG_DSC("\tnfl_bpg_offset %d", pps->nfl_bpg_offset);
	DC_LOG_DSC("\tslice_bpg_offset %d", pps->slice_bpg_offset);
	DC_LOG_DSC("\tinitial_offset %d", pps->initial_offset);
	DC_LOG_DSC("\tfinal_offset %d", pps->final_offset);
	DC_LOG_DSC("\tflatness_min_qp %d", pps->flatness_min_qp);
	DC_LOG_DSC("\tflatness_max_qp %d", pps->flatness_max_qp);
	/* DC_LOG_DSC("\trc_parameter_set %d", pps->rc_parameter_set); */
	DC_LOG_DSC("\tnative_420 %d", pps->native_420);
	DC_LOG_DSC("\tnative_422 %d", pps->native_422);
	DC_LOG_DSC("\tsecond_line_bpg_offset %d", pps->second_line_bpg_offset);
	DC_LOG_DSC("\tnsl_bpg_offset %d", pps->nsl_bpg_offset);
	DC_LOG_DSC("\tsecond_line_offset_adj %d", pps->second_line_offset_adj);
	DC_LOG_DSC("\trc_model_size %d", pps->rc_model_size);
	DC_LOG_DSC("\trc_edge_factor %d", pps->rc_edge_factor);
	DC_LOG_DSC("\trc_quant_incr_limit0 %d", pps->rc_quant_incr_limit0);
	DC_LOG_DSC("\trc_quant_incr_limit1 %d", pps->rc_quant_incr_limit1);
	DC_LOG_DSC("\trc_tgt_offset_high %d", pps->rc_tgt_offset_high);
	DC_LOG_DSC("\trc_tgt_offset_low %d", pps->rc_tgt_offset_low);

	for (i = 0; i < NUM_BUF_RANGES - 1; i++)
		DC_LOG_DSC("\trc_buf_thresh[%d] %d", i, pps->rc_buf_thresh[i]);

	for (i = 0; i < NUM_BUF_RANGES; i++) {
		DC_LOG_DSC("\trc_range_parameters[%d].range_min_qp %d", i, pps->rc_range_params[i].range_min_qp);
		DC_LOG_DSC("\trc_range_parameters[%d].range_max_qp %d", i, pps->rc_range_params[i].range_max_qp);
		DC_LOG_DSC("\trc_range_parameters[%d].range_bpg_offset %d", i, pps->rc_range_params[i].range_bpg_offset);
	}
}

void dsc_override_rc_params(struct rc_params *rc, const struct dc_dsc_rc_params_override *override)
{
	uint8_t i;

	rc->rc_model_size = override->rc_model_size;
	for (i = 0; i < DC_DSC_RC_BUF_THRESH_SIZE; i++)
		rc->rc_buf_thresh[i] = override->rc_buf_thresh[i];
	for (i = 0; i < DC_DSC_QP_SET_SIZE; i++) {
		rc->qp_min[i] = override->rc_minqp[i];
		rc->qp_max[i] = override->rc_maxqp[i];
		rc->ofs[i] = override->rc_offset[i];
	}

	rc->rc_tgt_offset_hi = override->rc_tgt_offset_hi;
	rc->rc_tgt_offset_lo = override->rc_tgt_offset_lo;
	rc->rc_edge_factor = override->rc_edge_factor;
	rc->rc_quant_incr_limit0 = override->rc_quant_incr_limit0;
	rc->rc_quant_incr_limit1 = override->rc_quant_incr_limit1;

	rc->initial_fullness_offset = override->initial_fullness_offset;
	rc->initial_xmit_delay = override->initial_delay;

	rc->flatness_min_qp = override->flatness_min_qp;
	rc->flatness_max_qp = override->flatness_max_qp;
	rc->flatness_det_thresh = override->flatness_det_thresh;
}

bool dsc_prepare_config(const struct dsc_config *dsc_cfg, struct dsc_reg_values *dsc_reg_vals,
			struct dsc_optc_config *dsc_optc_cfg)
{
	struct dsc_parameters dsc_params;
	struct rc_params rc;

	/* Validate input parameters */
	ASSERT(dsc_cfg->dc_dsc_cfg.num_slices_h);
	ASSERT(dsc_cfg->dc_dsc_cfg.num_slices_v);
	ASSERT(dsc_cfg->dc_dsc_cfg.version_minor == 1 || dsc_cfg->dc_dsc_cfg.version_minor == 2);
	ASSERT(dsc_cfg->pic_width);
	ASSERT(dsc_cfg->pic_height);
	ASSERT((dsc_cfg->dc_dsc_cfg.version_minor == 1 &&
		  (8 <= dsc_cfg->dc_dsc_cfg.linebuf_depth && dsc_cfg->dc_dsc_cfg.linebuf_depth <= 13)) ||
		(dsc_cfg->dc_dsc_cfg.version_minor == 2 &&
		  ((8 <= dsc_cfg->dc_dsc_cfg.linebuf_depth && dsc_cfg->dc_dsc_cfg.linebuf_depth <= 15) ||
		    dsc_cfg->dc_dsc_cfg.linebuf_depth == 0)));
	ASSERT(96 <= dsc_cfg->dc_dsc_cfg.bits_per_pixel && dsc_cfg->dc_dsc_cfg.bits_per_pixel <= 0x3ff); // 6.0 <= bits_per_pixel <= 63.9375

	if (!dsc_cfg->dc_dsc_cfg.num_slices_v || !dsc_cfg->dc_dsc_cfg.num_slices_h ||
		!(dsc_cfg->dc_dsc_cfg.version_minor == 1 || dsc_cfg->dc_dsc_cfg.version_minor == 2) ||
		!dsc_cfg->pic_width || !dsc_cfg->pic_height ||
		!((dsc_cfg->dc_dsc_cfg.version_minor == 1 && // v1.1 line buffer depth range:
			8 <= dsc_cfg->dc_dsc_cfg.linebuf_depth && dsc_cfg->dc_dsc_cfg.linebuf_depth <= 13) ||
		(dsc_cfg->dc_dsc_cfg.version_minor == 2 && // v1.2 line buffer depth range:
			((8 <= dsc_cfg->dc_dsc_cfg.linebuf_depth && dsc_cfg->dc_dsc_cfg.linebuf_depth <= 15) ||
			dsc_cfg->dc_dsc_cfg.linebuf_depth == 0))) ||
		!(96 <= dsc_cfg->dc_dsc_cfg.bits_per_pixel && dsc_cfg->dc_dsc_cfg.bits_per_pixel <= 0x3ff)) {
		dm_output_to_console("%s: Invalid parameters\n", __func__);
		return false;
	}

	dsc_init_reg_values(dsc_reg_vals);

	/* Copy input config */
	dsc_reg_vals->pixel_format = dsc_dc_pixel_encoding_to_dsc_pixel_format(dsc_cfg->pixel_encoding, dsc_cfg->dc_dsc_cfg.ycbcr422_simple);
	dsc_reg_vals->num_slices_h = dsc_cfg->dc_dsc_cfg.num_slices_h;
	dsc_reg_vals->num_slices_v = dsc_cfg->dc_dsc_cfg.num_slices_v;
	dsc_reg_vals->pps.dsc_version_minor = dsc_cfg->dc_dsc_cfg.version_minor;
	dsc_reg_vals->pps.pic_width = dsc_cfg->pic_width;
	dsc_reg_vals->pps.pic_height = dsc_cfg->pic_height;
	dsc_reg_vals->pps.bits_per_component = dsc_dc_color_depth_to_dsc_bits_per_comp(dsc_cfg->color_depth);
	dsc_reg_vals->pps.block_pred_enable = dsc_cfg->dc_dsc_cfg.block_pred_enable;
	dsc_reg_vals->pps.line_buf_depth = dsc_cfg->dc_dsc_cfg.linebuf_depth;
	dsc_reg_vals->alternate_ich_encoding_en = dsc_reg_vals->pps.dsc_version_minor == 1 ? 0 : 1;
	dsc_reg_vals->ich_reset_at_eol = (dsc_cfg->is_odm || dsc_reg_vals->num_slices_h > 1) ? 0xF : 0;

	// TODO: in addition to validating slice height (pic height must be divisible by slice height),
	// see what happens when the same condition doesn't apply for slice_width/pic_width.
	dsc_reg_vals->pps.slice_width = dsc_cfg->pic_width / dsc_cfg->dc_dsc_cfg.num_slices_h;
	dsc_reg_vals->pps.slice_height = dsc_cfg->pic_height / dsc_cfg->dc_dsc_cfg.num_slices_v;

	ASSERT(dsc_reg_vals->pps.slice_height * dsc_cfg->dc_dsc_cfg.num_slices_v == dsc_cfg->pic_height);
	if (!(dsc_reg_vals->pps.slice_height * dsc_cfg->dc_dsc_cfg.num_slices_v == dsc_cfg->pic_height)) {
		dm_output_to_console("%s: pix height %d not divisible by num_slices_v %d\n\n", __func__, dsc_cfg->pic_height, dsc_cfg->dc_dsc_cfg.num_slices_v);
		return false;
	}

	dsc_reg_vals->bpp_x32 = dsc_cfg->dc_dsc_cfg.bits_per_pixel << 1;
	if (dsc_reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420 || dsc_reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422)
		dsc_reg_vals->pps.bits_per_pixel = dsc_reg_vals->bpp_x32;
	else
		dsc_reg_vals->pps.bits_per_pixel = dsc_reg_vals->bpp_x32 >> 1;

	dsc_reg_vals->pps.convert_rgb = dsc_reg_vals->pixel_format == DSC_PIXFMT_RGB ? 1 : 0;
	dsc_reg_vals->pps.native_422 = (dsc_reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422);
	dsc_reg_vals->pps.native_420 = (dsc_reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420);
	dsc_reg_vals->pps.simple_422 = (dsc_reg_vals->pixel_format == DSC_PIXFMT_SIMPLE_YCBCR422);

	calc_rc_params(&rc, &dsc_reg_vals->pps);

	if (dsc_cfg->dc_dsc_cfg.rc_params_ovrd)
		dsc_override_rc_params(&rc, dsc_cfg->dc_dsc_cfg.rc_params_ovrd);

	if (dscc_compute_dsc_parameters(&dsc_reg_vals->pps, &rc, &dsc_params)) {
		dm_output_to_console("%s: DSC config failed\n", __func__);
		return false;
	}

	dsc_update_from_dsc_parameters(dsc_reg_vals, &dsc_params);

	dsc_optc_cfg->bytes_per_pixel = dsc_params.bytes_per_pixel;
	dsc_optc_cfg->slice_width = dsc_reg_vals->pps.slice_width;
	dsc_optc_cfg->is_pixel_format_444 = dsc_reg_vals->pixel_format == DSC_PIXFMT_RGB ||
					dsc_reg_vals->pixel_format == DSC_PIXFMT_YCBCR444 ||
					dsc_reg_vals->pixel_format == DSC_PIXFMT_SIMPLE_YCBCR422;

	return true;
}


enum dsc_pixel_format dsc_dc_pixel_encoding_to_dsc_pixel_format(enum dc_pixel_encoding dc_pix_enc, bool is_ycbcr422_simple)
{
	enum dsc_pixel_format dsc_pix_fmt = DSC_PIXFMT_UNKNOWN;

	/* NOTE: We don't support DSC_PIXFMT_SIMPLE_YCBCR422 */

	switch (dc_pix_enc) {
	case PIXEL_ENCODING_RGB:
		dsc_pix_fmt = DSC_PIXFMT_RGB;
		break;
	case PIXEL_ENCODING_YCBCR422:
		if (is_ycbcr422_simple)
			dsc_pix_fmt = DSC_PIXFMT_SIMPLE_YCBCR422;
		else
			dsc_pix_fmt = DSC_PIXFMT_NATIVE_YCBCR422;
		break;
	case PIXEL_ENCODING_YCBCR444:
		dsc_pix_fmt = DSC_PIXFMT_YCBCR444;
		break;
	case PIXEL_ENCODING_YCBCR420:
		dsc_pix_fmt = DSC_PIXFMT_NATIVE_YCBCR420;
		break;
	default:
		dsc_pix_fmt = DSC_PIXFMT_UNKNOWN;
		break;
	}

	ASSERT(dsc_pix_fmt != DSC_PIXFMT_UNKNOWN);
	return dsc_pix_fmt;
}


enum dsc_bits_per_comp dsc_dc_color_depth_to_dsc_bits_per_comp(enum dc_color_depth dc_color_depth)
{
	enum dsc_bits_per_comp bpc = DSC_BPC_UNKNOWN;

	switch (dc_color_depth) {
	case COLOR_DEPTH_888:
		bpc = DSC_BPC_8;
		break;
	case COLOR_DEPTH_101010:
		bpc = DSC_BPC_10;
		break;
	case COLOR_DEPTH_121212:
		bpc = DSC_BPC_12;
		break;
	default:
		bpc = DSC_BPC_UNKNOWN;
		break;
	}

	return bpc;
}


void dsc_init_reg_values(struct dsc_reg_values *reg_vals)
{
	int i;

	memset(reg_vals, 0, sizeof(struct dsc_reg_values));

	/* Non-PPS values */
	reg_vals->dsc_clock_enable            = 1;
	reg_vals->dsc_clock_gating_disable    = 0;
	reg_vals->underflow_recovery_en       = 0;
	reg_vals->underflow_occurred_int_en   = 0;
	reg_vals->underflow_occurred_status   = 0;
	reg_vals->ich_reset_at_eol            = 0;
	reg_vals->alternate_ich_encoding_en   = 0;
	reg_vals->rc_buffer_model_size        = 0;
	/*reg_vals->disable_ich                 = 0;*/
	reg_vals->dsc_dbg_en                  = 0;

	for (i = 0; i < 4; i++)
		reg_vals->rc_buffer_model_overflow_int_en[i] = 0;

	/* PPS values */
	reg_vals->pps.dsc_version_minor           = 2;
	reg_vals->pps.dsc_version_major           = 1;
	reg_vals->pps.line_buf_depth              = 9;
	reg_vals->pps.bits_per_component          = 8;
	reg_vals->pps.block_pred_enable           = 1;
	reg_vals->pps.slice_chunk_size            = 0;
	reg_vals->pps.pic_width                   = 0;
	reg_vals->pps.pic_height                  = 0;
	reg_vals->pps.slice_width                 = 0;
	reg_vals->pps.slice_height                = 0;
	reg_vals->pps.initial_xmit_delay          = 170;
	reg_vals->pps.initial_dec_delay           = 0;
	reg_vals->pps.initial_scale_value         = 0;
	reg_vals->pps.scale_increment_interval    = 0;
	reg_vals->pps.scale_decrement_interval    = 0;
	reg_vals->pps.nfl_bpg_offset              = 0;
	reg_vals->pps.slice_bpg_offset            = 0;
	reg_vals->pps.nsl_bpg_offset              = 0;
	reg_vals->pps.initial_offset              = 6144;
	reg_vals->pps.final_offset                = 0;
	reg_vals->pps.flatness_min_qp             = 3;
	reg_vals->pps.flatness_max_qp             = 12;
	reg_vals->pps.rc_model_size               = 8192;
	reg_vals->pps.rc_edge_factor              = 6;
	reg_vals->pps.rc_quant_incr_limit0        = 11;
	reg_vals->pps.rc_quant_incr_limit1        = 11;
	reg_vals->pps.rc_tgt_offset_low           = 3;
	reg_vals->pps.rc_tgt_offset_high          = 3;
}

/* Updates dsc_reg_values::reg_vals::xxx fields based on the values from computed params.
 * This is required because dscc_compute_dsc_parameters returns a modified PPS, which in turn
 * affects non-PPS register values.
 */
void dsc_update_from_dsc_parameters(struct dsc_reg_values *reg_vals, const struct dsc_parameters *dsc_params)
{
	int i;

	reg_vals->pps = dsc_params->pps;

	// pps_computed will have the "expanded" values; need to shift them to make them fit for regs.
	for (i = 0; i < NUM_BUF_RANGES - 1; i++)
		reg_vals->pps.rc_buf_thresh[i] = reg_vals->pps.rc_buf_thresh[i] >> 6;

	reg_vals->rc_buffer_model_size = dsc_params->rc_buffer_model_size;
}

static void dsc_write_to_registers(struct display_stream_compressor *dsc, const struct dsc_reg_values *reg_vals)
{
	uint32_t temp_int;
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);

	REG_SET(DSC_DEBUG_CONTROL, 0,
		DSC_DBG_EN, reg_vals->dsc_dbg_en);

	// dsccif registers
	REG_SET_5(DSCCIF_CONFIG0, 0,
		INPUT_INTERFACE_UNDERFLOW_RECOVERY_EN, reg_vals->underflow_recovery_en,
		INPUT_INTERFACE_UNDERFLOW_OCCURRED_INT_EN, reg_vals->underflow_occurred_int_en,
		INPUT_INTERFACE_UNDERFLOW_OCCURRED_STATUS, reg_vals->underflow_occurred_status,
		INPUT_PIXEL_FORMAT, reg_vals->pixel_format,
		DSCCIF_CONFIG0__BITS_PER_COMPONENT, reg_vals->pps.bits_per_component);

	REG_SET_2(DSCCIF_CONFIG1, 0,
		PIC_WIDTH, reg_vals->pps.pic_width,
		PIC_HEIGHT, reg_vals->pps.pic_height);

	// dscc registers
	if (dsc20->dsc_mask->ICH_RESET_AT_END_OF_LINE == 0) {
		REG_SET_3(DSCC_CONFIG0, 0,
			  NUMBER_OF_SLICES_PER_LINE, reg_vals->num_slices_h - 1,
			  ALTERNATE_ICH_ENCODING_EN, reg_vals->alternate_ich_encoding_en,
			  NUMBER_OF_SLICES_IN_VERTICAL_DIRECTION, reg_vals->num_slices_v - 1);
	} else {
		REG_SET_4(DSCC_CONFIG0, 0, ICH_RESET_AT_END_OF_LINE,
			  reg_vals->ich_reset_at_eol, NUMBER_OF_SLICES_PER_LINE,
			  reg_vals->num_slices_h - 1, ALTERNATE_ICH_ENCODING_EN,
			  reg_vals->alternate_ich_encoding_en, NUMBER_OF_SLICES_IN_VERTICAL_DIRECTION,
			  reg_vals->num_slices_v - 1);
	}

	REG_SET(DSCC_CONFIG1, 0,
			DSCC_RATE_CONTROL_BUFFER_MODEL_SIZE, reg_vals->rc_buffer_model_size);
	/*REG_SET_2(DSCC_CONFIG1, 0,
		DSCC_RATE_CONTROL_BUFFER_MODEL_SIZE, reg_vals->rc_buffer_model_size,
		DSCC_DISABLE_ICH, reg_vals->disable_ich);*/

	REG_SET_4(DSCC_INTERRUPT_CONTROL_STATUS, 0,
		DSCC_RATE_CONTROL_BUFFER_MODEL0_OVERFLOW_OCCURRED_INT_EN, reg_vals->rc_buffer_model_overflow_int_en[0],
		DSCC_RATE_CONTROL_BUFFER_MODEL1_OVERFLOW_OCCURRED_INT_EN, reg_vals->rc_buffer_model_overflow_int_en[1],
		DSCC_RATE_CONTROL_BUFFER_MODEL2_OVERFLOW_OCCURRED_INT_EN, reg_vals->rc_buffer_model_overflow_int_en[2],
		DSCC_RATE_CONTROL_BUFFER_MODEL3_OVERFLOW_OCCURRED_INT_EN, reg_vals->rc_buffer_model_overflow_int_en[3]);

	REG_SET_3(DSCC_PPS_CONFIG0, 0,
		DSC_VERSION_MINOR, reg_vals->pps.dsc_version_minor,
		LINEBUF_DEPTH, reg_vals->pps.line_buf_depth,
		DSCC_PPS_CONFIG0__BITS_PER_COMPONENT, reg_vals->pps.bits_per_component);

	if (reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420 || reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422)
		temp_int = reg_vals->bpp_x32;
	else
		temp_int = reg_vals->bpp_x32 >> 1;

	REG_SET_7(DSCC_PPS_CONFIG1, 0,
		BITS_PER_PIXEL, temp_int,
		SIMPLE_422, reg_vals->pixel_format == DSC_PIXFMT_SIMPLE_YCBCR422,
		CONVERT_RGB, reg_vals->pixel_format == DSC_PIXFMT_RGB,
		BLOCK_PRED_ENABLE, reg_vals->pps.block_pred_enable,
		NATIVE_422, reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422,
		NATIVE_420, reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420,
		CHUNK_SIZE, reg_vals->pps.slice_chunk_size);

	REG_SET_2(DSCC_PPS_CONFIG2, 0,
		PIC_WIDTH, reg_vals->pps.pic_width,
		PIC_HEIGHT, reg_vals->pps.pic_height);

	REG_SET_2(DSCC_PPS_CONFIG3, 0,
		SLICE_WIDTH, reg_vals->pps.slice_width,
		SLICE_HEIGHT, reg_vals->pps.slice_height);

	REG_SET(DSCC_PPS_CONFIG4, 0,
		INITIAL_XMIT_DELAY, reg_vals->pps.initial_xmit_delay);

	REG_SET_2(DSCC_PPS_CONFIG5, 0,
		INITIAL_SCALE_VALUE, reg_vals->pps.initial_scale_value,
		SCALE_INCREMENT_INTERVAL, reg_vals->pps.scale_increment_interval);

	REG_SET_3(DSCC_PPS_CONFIG6, 0,
		SCALE_DECREMENT_INTERVAL, reg_vals->pps.scale_decrement_interval,
		FIRST_LINE_BPG_OFFSET, reg_vals->pps.first_line_bpg_offset,
		SECOND_LINE_BPG_OFFSET, reg_vals->pps.second_line_bpg_offset);

	REG_SET_2(DSCC_PPS_CONFIG7, 0,
		NFL_BPG_OFFSET, reg_vals->pps.nfl_bpg_offset,
		SLICE_BPG_OFFSET, reg_vals->pps.slice_bpg_offset);

	REG_SET_2(DSCC_PPS_CONFIG8, 0,
		NSL_BPG_OFFSET, reg_vals->pps.nsl_bpg_offset,
		SECOND_LINE_OFFSET_ADJ, reg_vals->pps.second_line_offset_adj);

	REG_SET_2(DSCC_PPS_CONFIG9, 0,
		INITIAL_OFFSET, reg_vals->pps.initial_offset,
		FINAL_OFFSET, reg_vals->pps.final_offset);

	REG_SET_3(DSCC_PPS_CONFIG10, 0,
		FLATNESS_MIN_QP, reg_vals->pps.flatness_min_qp,
		FLATNESS_MAX_QP, reg_vals->pps.flatness_max_qp,
		RC_MODEL_SIZE, reg_vals->pps.rc_model_size);

	REG_SET_5(DSCC_PPS_CONFIG11, 0,
		RC_EDGE_FACTOR, reg_vals->pps.rc_edge_factor,
		RC_QUANT_INCR_LIMIT0, reg_vals->pps.rc_quant_incr_limit0,
		RC_QUANT_INCR_LIMIT1, reg_vals->pps.rc_quant_incr_limit1,
		RC_TGT_OFFSET_LO, reg_vals->pps.rc_tgt_offset_low,
		RC_TGT_OFFSET_HI, reg_vals->pps.rc_tgt_offset_high);

	REG_SET_4(DSCC_PPS_CONFIG12, 0,
		RC_BUF_THRESH0, reg_vals->pps.rc_buf_thresh[0],
		RC_BUF_THRESH1, reg_vals->pps.rc_buf_thresh[1],
		RC_BUF_THRESH2, reg_vals->pps.rc_buf_thresh[2],
		RC_BUF_THRESH3, reg_vals->pps.rc_buf_thresh[3]);

	REG_SET_4(DSCC_PPS_CONFIG13, 0,
		RC_BUF_THRESH4, reg_vals->pps.rc_buf_thresh[4],
		RC_BUF_THRESH5, reg_vals->pps.rc_buf_thresh[5],
		RC_BUF_THRESH6, reg_vals->pps.rc_buf_thresh[6],
		RC_BUF_THRESH7, reg_vals->pps.rc_buf_thresh[7]);

	REG_SET_4(DSCC_PPS_CONFIG14, 0,
		RC_BUF_THRESH8, reg_vals->pps.rc_buf_thresh[8],
		RC_BUF_THRESH9, reg_vals->pps.rc_buf_thresh[9],
		RC_BUF_THRESH10, reg_vals->pps.rc_buf_thresh[10],
		RC_BUF_THRESH11, reg_vals->pps.rc_buf_thresh[11]);

	REG_SET_5(DSCC_PPS_CONFIG15, 0,
		RC_BUF_THRESH12, reg_vals->pps.rc_buf_thresh[12],
		RC_BUF_THRESH13, reg_vals->pps.rc_buf_thresh[13],
		RANGE_MIN_QP0, reg_vals->pps.rc_range_params[0].range_min_qp,
		RANGE_MAX_QP0, reg_vals->pps.rc_range_params[0].range_max_qp,
		RANGE_BPG_OFFSET0, reg_vals->pps.rc_range_params[0].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG16, 0,
		RANGE_MIN_QP1, reg_vals->pps.rc_range_params[1].range_min_qp,
		RANGE_MAX_QP1, reg_vals->pps.rc_range_params[1].range_max_qp,
		RANGE_BPG_OFFSET1, reg_vals->pps.rc_range_params[1].range_bpg_offset,
		RANGE_MIN_QP2, reg_vals->pps.rc_range_params[2].range_min_qp,
		RANGE_MAX_QP2, reg_vals->pps.rc_range_params[2].range_max_qp,
		RANGE_BPG_OFFSET2, reg_vals->pps.rc_range_params[2].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG17, 0,
		RANGE_MIN_QP3, reg_vals->pps.rc_range_params[3].range_min_qp,
		RANGE_MAX_QP3, reg_vals->pps.rc_range_params[3].range_max_qp,
		RANGE_BPG_OFFSET3, reg_vals->pps.rc_range_params[3].range_bpg_offset,
		RANGE_MIN_QP4, reg_vals->pps.rc_range_params[4].range_min_qp,
		RANGE_MAX_QP4, reg_vals->pps.rc_range_params[4].range_max_qp,
		RANGE_BPG_OFFSET4, reg_vals->pps.rc_range_params[4].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG18, 0,
		RANGE_MIN_QP5, reg_vals->pps.rc_range_params[5].range_min_qp,
		RANGE_MAX_QP5, reg_vals->pps.rc_range_params[5].range_max_qp,
		RANGE_BPG_OFFSET5, reg_vals->pps.rc_range_params[5].range_bpg_offset,
		RANGE_MIN_QP6, reg_vals->pps.rc_range_params[6].range_min_qp,
		RANGE_MAX_QP6, reg_vals->pps.rc_range_params[6].range_max_qp,
		RANGE_BPG_OFFSET6, reg_vals->pps.rc_range_params[6].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG19, 0,
		RANGE_MIN_QP7, reg_vals->pps.rc_range_params[7].range_min_qp,
		RANGE_MAX_QP7, reg_vals->pps.rc_range_params[7].range_max_qp,
		RANGE_BPG_OFFSET7, reg_vals->pps.rc_range_params[7].range_bpg_offset,
		RANGE_MIN_QP8, reg_vals->pps.rc_range_params[8].range_min_qp,
		RANGE_MAX_QP8, reg_vals->pps.rc_range_params[8].range_max_qp,
		RANGE_BPG_OFFSET8, reg_vals->pps.rc_range_params[8].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG20, 0,
		RANGE_MIN_QP9, reg_vals->pps.rc_range_params[9].range_min_qp,
		RANGE_MAX_QP9, reg_vals->pps.rc_range_params[9].range_max_qp,
		RANGE_BPG_OFFSET9, reg_vals->pps.rc_range_params[9].range_bpg_offset,
		RANGE_MIN_QP10, reg_vals->pps.rc_range_params[10].range_min_qp,
		RANGE_MAX_QP10, reg_vals->pps.rc_range_params[10].range_max_qp,
		RANGE_BPG_OFFSET10, reg_vals->pps.rc_range_params[10].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG21, 0,
		RANGE_MIN_QP11, reg_vals->pps.rc_range_params[11].range_min_qp,
		RANGE_MAX_QP11, reg_vals->pps.rc_range_params[11].range_max_qp,
		RANGE_BPG_OFFSET11, reg_vals->pps.rc_range_params[11].range_bpg_offset,
		RANGE_MIN_QP12, reg_vals->pps.rc_range_params[12].range_min_qp,
		RANGE_MAX_QP12, reg_vals->pps.rc_range_params[12].range_max_qp,
		RANGE_BPG_OFFSET12, reg_vals->pps.rc_range_params[12].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG22, 0,
		RANGE_MIN_QP13, reg_vals->pps.rc_range_params[13].range_min_qp,
		RANGE_MAX_QP13, reg_vals->pps.rc_range_params[13].range_max_qp,
		RANGE_BPG_OFFSET13, reg_vals->pps.rc_range_params[13].range_bpg_offset,
		RANGE_MIN_QP14, reg_vals->pps.rc_range_params[14].range_min_qp,
		RANGE_MAX_QP14, reg_vals->pps.rc_range_params[14].range_max_qp,
		RANGE_BPG_OFFSET14, reg_vals->pps.rc_range_params[14].range_bpg_offset);

}

