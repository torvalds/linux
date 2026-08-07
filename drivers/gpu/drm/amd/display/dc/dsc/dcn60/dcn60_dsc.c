// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include <drm/display/drm_dsc_helper.h>

#include "reg_helper.h"
#include "dcn60_dsc.h"
#include "dsc/dscc_types.h"
#include "dsc/rc_calc.h"

static const struct dsc_funcs dcn60_dsc_funcs = {
	.dsc_read_state = dsc401_read_state,
	.dsc_validate_stream = dsc401_validate_stream,
	.dsc_set_config = dsc60_set_config,
	.dsc_get_packed_pps = dsc2_get_packed_pps,
	.dsc_enable = dsc401_enable,
	.dsc_disable = dsc401_disable,
	.dsc_disconnect = dsc401_disconnect,
	.dsc_wait_disconnect_pending_clear = dsc401_wait_disconnect_pending_clear,
	.dsc_get_single_enc_caps = dsc60_get_single_enc_caps,
	.dsc_read_reg_state = dsc2_read_reg_state,
	.set_fgcg = dsc60_set_fgcg,
};

/* Macro definitios for REG_SET macros*/
#define CTX \
	dsc60->base.ctx

#define REG(reg)\
	dsc60->dsc_regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dsc60->dsc_shift->field_name, dsc60->dsc_mask->field_name
#define DC_LOGGER \
	dsc->ctx->logger

void dsc60_set_fgcg(struct display_stream_compressor *dsc, bool enable)
{
	struct dcn60_dsc *dsc60 = TO_DCN60_DSC(dsc);

	REG_UPDATE(DSC_TOP_CONTROL, DSC_FGCG_REP_DIS, !enable);
}

/* API functions (external or via structure->function_pointer) */

void dsc60_construct(struct dcn60_dsc *dsc,
	struct dc_context *ctx,
	int inst,
	const struct dcn401_dsc_registers *dsc_regs,
	const struct dcn60_dsc_shift *dsc_shift,
	const struct dcn60_dsc_mask *dsc_mask)
{
	dsc->base.ctx = ctx;
	dsc->base.inst = inst;
	dsc->base.funcs = &dcn60_dsc_funcs;

	dsc->dsc_regs = dsc_regs;
	dsc->dsc_shift = dsc_shift;
	dsc->dsc_mask = dsc_mask;

	dsc->max_image_width = 5184;
}

static void dsc60_init_reg_values(struct dsc60_reg_values *reg_vals)
{
	int i;

	memset(reg_vals, 0, sizeof(struct dsc60_reg_values));

	/* Non-PPS values */
	reg_vals->dsc_clock_enable = 1;
	reg_vals->dsc_clock_gating_disable = 0;
	reg_vals->underflow_recovery_en = 0;
	reg_vals->underflow_occurred_int_en = 0;
	reg_vals->underflow_occurred_status = 0;
	reg_vals->ich_reset_at_eol = 0;
	reg_vals->alternate_ich_encoding_en = 0;
	reg_vals->rc_buffer_model_size = 0;
	/*reg_vals->disable_ich                 = 0;*/
	reg_vals->dsc_dbg_en = 0;

	for (i = 0; i < 8; i++)
		reg_vals->rc_buffer_model_overflow_int_en[i] = 0;

	/* PPS values */
	reg_vals->pps.dsc_version_minor = 2;
	reg_vals->pps.dsc_version_major = 1;
	reg_vals->pps.line_buf_depth = 9;
	reg_vals->pps.bits_per_component = 8;
	reg_vals->pps.block_pred_enable = 1;
	reg_vals->pps.slice_chunk_size = 0;
	reg_vals->pps.pic_width = 0;
	reg_vals->pps.pic_height = 0;
	reg_vals->pps.slice_width = 0;
	reg_vals->pps.slice_height = 0;
	reg_vals->pps.initial_xmit_delay = 170;
	reg_vals->pps.initial_dec_delay = 0;
	reg_vals->pps.initial_scale_value = 0;
	reg_vals->pps.scale_increment_interval = 0;
	reg_vals->pps.scale_decrement_interval = 0;
	reg_vals->pps.nfl_bpg_offset = 0;
	reg_vals->pps.slice_bpg_offset = 0;
	reg_vals->pps.nsl_bpg_offset = 0;
	reg_vals->pps.initial_offset = 6144;
	reg_vals->pps.final_offset = 0;
	reg_vals->pps.flatness_min_qp = 3;
	reg_vals->pps.flatness_max_qp = 12;
	reg_vals->pps.rc_model_size = 8192;
	reg_vals->pps.rc_edge_factor = 6;
	reg_vals->pps.rc_quant_incr_limit0 = 11;
	reg_vals->pps.rc_quant_incr_limit1 = 11;
	reg_vals->pps.rc_tgt_offset_low = 3;
	reg_vals->pps.rc_tgt_offset_high = 3;
}

/*Updates dsc_reg_values::reg_vals::xxx fields based on the values from computed params.
* This is required because dscc_compute_dsc_parameters returns a modified PPS, which in turn
* affects non - PPS register values.
*/
static void dsc60_update_from_dsc_parameters(struct dsc60_reg_values *reg_vals, const struct dsc_parameters *dsc_params)
{
	int i;

	reg_vals->pps = dsc_params->pps;

	// pps_computed will have the "expanded" values; need to shift them to make them fit for regs.
	for (i = 0; i < NUM_BUF_RANGES - 1; i++)
		reg_vals->pps.rc_buf_thresh[i] = reg_vals->pps.rc_buf_thresh[i] >> 6;

	reg_vals->rc_buffer_model_size = dsc_params->rc_buffer_model_size;
}

static bool dsc60_prepare_config(const struct dsc_config *dsc_cfg, struct dsc60_reg_values *dsc_reg_vals,
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

	dsc60_init_reg_values(dsc_reg_vals);

	/* Copy input config */
	dsc_reg_vals->pixel_format = dsc_dc_pixel_encoding_to_dsc_pixel_format(dsc_cfg->pixel_encoding, dsc_cfg->dc_dsc_cfg.ycbcr422_simple);
	dsc_reg_vals->num_slices_h = dsc_cfg->dc_dsc_cfg.num_slices_h;
	dsc_reg_vals->num_slices_v = dsc_cfg->dc_dsc_cfg.num_slices_v;
	dsc_reg_vals->pps.dsc_version_minor = (u8)dsc_cfg->dc_dsc_cfg.version_minor;
	dsc_reg_vals->pps.pic_width = (u16)dsc_cfg->pic_width;
	dsc_reg_vals->pps.pic_height = (u16)dsc_cfg->pic_height;
	dsc_reg_vals->pps.bits_per_component = dsc_dc_color_depth_to_dsc_bits_per_comp(dsc_cfg->color_depth);
	dsc_reg_vals->pps.block_pred_enable = dsc_cfg->dc_dsc_cfg.block_pred_enable;
	dsc_reg_vals->pps.line_buf_depth = (u8)dsc_cfg->dc_dsc_cfg.linebuf_depth;
	dsc_reg_vals->alternate_ich_encoding_en = dsc_reg_vals->pps.dsc_version_minor == 1 ? 0 : 1;
	dsc_reg_vals->ich_reset_at_eol = (dsc_cfg->is_odm || dsc_reg_vals->num_slices_h > 1) ? 0xF : 0;

	// TODO: in addition to validating slice height (pic height must be divisible by slice height),
	// see what happens when the same condition doesn't apply for slice_width/pic_width.
	dsc_reg_vals->pps.slice_width = (u16)(dsc_cfg->pic_width / dsc_cfg->dc_dsc_cfg.num_slices_h);
	dsc_reg_vals->pps.slice_height = (u16)(dsc_cfg->pic_height / dsc_cfg->dc_dsc_cfg.num_slices_v);

	ASSERT(dsc_reg_vals->pps.slice_height * dsc_cfg->dc_dsc_cfg.num_slices_v == dsc_cfg->pic_height);
	if (!(dsc_reg_vals->pps.slice_height * dsc_cfg->dc_dsc_cfg.num_slices_v == dsc_cfg->pic_height)) {
		dm_output_to_console("%s: pix height %d not divisible by num_slices_v %d\n\n", __func__, dsc_cfg->pic_height, dsc_cfg->dc_dsc_cfg.num_slices_v);
		return false;
	}

	dsc_reg_vals->bpp_x32 = dsc_cfg->dc_dsc_cfg.bits_per_pixel << 1;
	if (dsc_reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420 || dsc_reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422)
		dsc_reg_vals->pps.bits_per_pixel = (u16)dsc_reg_vals->bpp_x32;
	else
		dsc_reg_vals->pps.bits_per_pixel = (u16)(dsc_reg_vals->bpp_x32 >> 1);

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

	dsc60_update_from_dsc_parameters(dsc_reg_vals, &dsc_params);

	dsc_optc_cfg->bytes_per_pixel = dsc_params.bytes_per_pixel;
	dsc_optc_cfg->slice_width = dsc_reg_vals->pps.slice_width;
	dsc_optc_cfg->is_pixel_format_444 = dsc_reg_vals->pixel_format == DSC_PIXFMT_RGB ||
		dsc_reg_vals->pixel_format == DSC_PIXFMT_YCBCR444 ||
		dsc_reg_vals->pixel_format == DSC_PIXFMT_SIMPLE_YCBCR422;

	return true;
}

static void dsc60_write_to_registers(struct display_stream_compressor *dsc, const struct dsc60_reg_values *reg_vals)
{
	uint32_t temp_int;
	struct dcn60_dsc *dsc60 = TO_DCN60_DSC(dsc);

	REG_SET(DSC_DEBUG_CONTROL, 0,
		DSC_DBG_EN, reg_vals->dsc_dbg_en);

	// dscc registers
	if (dsc60->dsc_mask->ICH_RESET_AT_END_OF_LINE == 0) {
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

	REG_SET_8(DSCC_INTERRUPT_CONTROL0, 0,
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN0, reg_vals->rc_buffer_model_overflow_int_en[0],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN1, reg_vals->rc_buffer_model_overflow_int_en[1],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN2, reg_vals->rc_buffer_model_overflow_int_en[2],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN3, reg_vals->rc_buffer_model_overflow_int_en[3],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN4, reg_vals->rc_buffer_model_overflow_int_en[4],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN5, reg_vals->rc_buffer_model_overflow_int_en[5],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN6, reg_vals->rc_buffer_model_overflow_int_en[6],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN7, reg_vals->rc_buffer_model_overflow_int_en[7]);

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

void dsc60_set_config(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
	struct dsc_optc_config *dsc_optc_cfg)
{
	bool is_config_ok;
	struct dcn60_dsc *dsc60 = TO_DCN60_DSC(dsc);

	DC_LOG_DSC("Setting DSC Config at DSC inst %d", dsc->inst);
	dsc_config_log(dsc, dsc_cfg);
	is_config_ok = dsc60_prepare_config(dsc_cfg, &dsc60->reg_vals, dsc_optc_cfg);
	ASSERT(is_config_ok);
	DC_LOG_DSC("programming DSC Picture Parameter Set (PPS):");
	dsc_log_pps(dsc, &dsc60->reg_vals.pps);
	dsc60_write_to_registers(dsc, &dsc60->reg_vals);
}

void dsc60_get_single_enc_caps(struct dsc_enc_caps *dsc_enc_caps, unsigned int max_dscclk_khz)
{
	dsc_enc_caps->dsc_version = 0x21; /* v1.2 - DP spec defined it in reverse order and we kept it */

	dsc_enc_caps->slice_caps.bits.NUM_SLICES_1 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_2 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_3 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_4 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_8 = 1;

	dsc_enc_caps->lb_bit_depth = 13;
	dsc_enc_caps->is_block_pred_supported = true;

	dsc_enc_caps->color_formats.bits.RGB = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_444 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_SIMPLE_422 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_NATIVE_422 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_NATIVE_420 = 1;

	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_8_BPC = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_10_BPC = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_12_BPC = 1;
	dsc_enc_caps->max_total_throughput_mps = max_dscclk_khz * 3 / 1000;

	dsc_enc_caps->max_slice_width = 5760; /* (including 64 overlap pixels for eDP MSO mode) */
	dsc_enc_caps->bpp_increment_div = 16; /* 1/16th of a bit */
}
