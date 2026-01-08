// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - Configuration parameters output device
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */
#include <linux/media/arm/mali-c55-config.h>
#include <linux/pm_runtime.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-isp.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

/**
 * union mali_c55_params_block - Generalisation of a parameter block
 *
 * This union allows the driver to treat a block as a generic pointer to this
 * union and safely access the header and block-specific struct without having
 * to resort to casting. The header member is accessed first, and the type field
 * checked which allows the driver to determine which of the other members
 * should be used. The data member at the end allows a pointer to an address
 * within the data member of :c:type:`mali_c55_params_buffer` to initialise a
 * union variable.
 *
 * @header:		Pointer to the shared header struct embedded as the
 *			first member of all the possible other members (except
 *			@data). This member would be accessed first and the type
 *			field checked to determine which of the other members
 *			should be accessed.
 * @sensor_offs:	For header->type == MALI_C55_PARAM_BLOCK_SENSOR_OFFS
 * @aexp_hist:		For header->type == MALI_C55_PARAM_BLOCK_AEXP_HIST and
 *			header->type == MALI_C55_PARAM_BLOCK_AEXP_IHIST
 * @aexp_weights:	For header->type == MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS
 *			and header->type =  MALI_C55_PARAM_BLOCK_AEXP_IHIST_WEIGHTS
 * @digital_gain:	For header->type == MALI_C55_PARAM_BLOCK_DIGITAL_GAIN
 * @awb_gains:		For header->type == MALI_C55_PARAM_BLOCK_AWB_GAINS and
 *			header->type = MALI_C55_PARAM_BLOCK_AWB_GAINS_AEXP
 * @awb_config:		For header->type == MALI_C55_PARAM_MESH_SHADING_CONFIG
 * @shading_config:	For header->type == MALI_C55_PARAM_MESH_SHADING_SELECTION
 * @shading_selection:	For header->type == MALI_C55_PARAM_BLOCK_SENSOR_OFFS
 * @data:		Allows easy initialisation of a union variable with a
 *			pointer into a __u8 array.
 */
union mali_c55_params_block {
	const struct v4l2_isp_params_block_header *header;
	const struct mali_c55_params_sensor_off_preshading *sensor_offs;
	const struct mali_c55_params_aexp_hist *aexp_hist;
	const struct mali_c55_params_aexp_weights *aexp_weights;
	const struct mali_c55_params_digital_gain *digital_gain;
	const struct mali_c55_params_awb_gains *awb_gains;
	const struct mali_c55_params_awb_config *awb_config;
	const struct mali_c55_params_mesh_shading_config *shading_config;
	const struct mali_c55_params_mesh_shading_selection *shading_selection;
	const __u8 *data;
};

typedef void (*mali_c55_params_handler)(struct mali_c55 *mali_c55,
					union mali_c55_params_block block);

#define to_mali_c55_params_buf(vbuf) \
	container_of(vbuf, struct mali_c55_params_buf, vb)

static void mali_c55_params_sensor_offs(struct mali_c55 *mali_c55,
					union mali_c55_params_block block)
{
	const struct mali_c55_params_sensor_off_preshading *p;
	__u32 global_offset;

	p = block.sensor_offs;

	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BYPASS_3,
			MALI_C55_REG_BYPASS_3_SENSOR_OFFSET_PRE_SH,
			MALI_C55_REG_BYPASS_3_SENSOR_OFFSET_PRE_SH);
		return;
	}

	if (!(p->chan00 || p->chan01 || p->chan10 || p->chan11))
		return;

	mali_c55_ctx_write(mali_c55, MALI_C55_REG_SENSOR_OFF_PRE_SHA_00,
			   p->chan00 & MALI_C55_SENSOR_OFF_PRE_SHA_MASK);
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_SENSOR_OFF_PRE_SHA_01,
			   p->chan01 & MALI_C55_SENSOR_OFF_PRE_SHA_MASK);
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_SENSOR_OFF_PRE_SHA_10,
			   p->chan10 & MALI_C55_SENSOR_OFF_PRE_SHA_MASK);
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_SENSOR_OFF_PRE_SHA_11,
			   p->chan11 & MALI_C55_SENSOR_OFF_PRE_SHA_MASK);

	/*
	 * The average offset is applied as a global offset for the digital
	 * gain block
	 */
	global_offset = (p->chan00 + p->chan01 + p->chan10 + p->chan11) >> 2;
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_DIGITAL_GAIN_OFFSET,
				 MALI_C55_DIGITAL_GAIN_OFFSET_MASK,
				 global_offset);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BYPASS_3,
				 MALI_C55_REG_BYPASS_3_SENSOR_OFFSET_PRE_SH,
				 0x00);
}

static void mali_c55_params_aexp_hist(struct mali_c55 *mali_c55,
				      union mali_c55_params_block block)
{
	const struct mali_c55_params_aexp_hist *params;
	u32 disable_mask;
	u32 disable_val;
	u32 base;

	if (block.header->type == MALI_C55_PARAM_BLOCK_AEXP_HIST) {
		disable_mask = MALI_C55_AEXP_HIST_DISABLE_MASK;
		disable_val = MALI_C55_AEXP_HIST_DISABLE;
		base = MALI_C55_REG_AEXP_HIST_BASE;
	} else {
		disable_mask = MALI_C55_AEXP_IHIST_DISABLE_MASK;
		disable_val = MALI_C55_AEXP_IHIST_DISABLE;
		base = MALI_C55_REG_AEXP_IHIST_BASE;
	}

	params = block.aexp_hist;

	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_METERING_CONFIG,
					 disable_mask, disable_val);
		return;
	}

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_METERING_CONFIG,
				 disable_mask, false);

	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_SKIP_OFFSET,
				 MALI_C55_AEXP_HIST_SKIP_X_MASK, params->skip_x);
	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_SKIP_OFFSET,
				 MALI_C55_AEXP_HIST_OFFSET_X_MASK,
				 MALI_C55_AEXP_HIST_OFFSET_X(params->offset_x));
	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_SKIP_OFFSET,
				 MALI_C55_AEXP_HIST_SKIP_Y_MASK,
				 MALI_C55_AEXP_HIST_SKIP_Y(params->skip_y));
	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_SKIP_OFFSET,
				 MALI_C55_AEXP_HIST_OFFSET_Y_MASK,
				 MALI_C55_AEXP_HIST_OFFSET_Y(params->offset_y));

	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_SCALE_OFFSET,
				 MALI_C55_AEXP_HIST_SCALE_BOTTOM_MASK,
				 params->scale_bottom);
	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_SCALE_OFFSET,
				 MALI_C55_AEXP_HIST_SCALE_TOP_MASK,
				 MALI_C55_AEXP_HIST_SCALE_TOP(params->scale_top));

	mali_c55_ctx_update_bits(mali_c55, base + MALI_C55_AEXP_HIST_PLANE_MODE_OFFSET,
				 MALI_C55_AEXP_HIST_PLANE_MODE_MASK,
				 params->plane_mode);

	if (block.header->type == MALI_C55_PARAM_BLOCK_AEXP_HIST)
		mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_METERING_CONFIG,
					 MALI_C55_AEXP_HIST_SWITCH_MASK,
					 MALI_C55_AEXP_HIST_SWITCH(params->tap_point));
}

static void
mali_c55_params_aexp_hist_weights(struct mali_c55 *mali_c55,
				  union mali_c55_params_block block)
{
	const struct mali_c55_params_aexp_weights *params;
	u32 base, val, addr;

	params = block.aexp_weights;

	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		return;

	base = block.header->type == MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS ?
				      MALI_C55_REG_AEXP_HIST_BASE :
				      MALI_C55_REG_AEXP_IHIST_BASE;

	mali_c55_ctx_update_bits(mali_c55,
				 base + MALI_C55_AEXP_HIST_NODES_USED_OFFSET,
				 MALI_C55_AEXP_HIST_NODES_USED_HORIZ_MASK,
				 params->nodes_used_horiz);
	mali_c55_ctx_update_bits(mali_c55,
				 base + MALI_C55_AEXP_HIST_NODES_USED_OFFSET,
				 MALI_C55_AEXP_HIST_NODES_USED_VERT_MASK,
				 MALI_C55_AEXP_HIST_NODES_USED_VERT(params->nodes_used_vert));

	/*
	 * The zone weights array is a 225-element array of u8 values, but that
	 * is a bit annoying to handle given the ISP expects 32-bit writes. We
	 * just reinterpret it as 56-element array of 32-bit values for the
	 * purposes of this transaction. The last register is handled separately
	 * to stop static analysers worrying about buffer overflow. The 3 bytes
	 * of additional space at the end of the write is just padding for the
	 * array of weights in the ISP memory space anyway, so there's no risk
	 * of overwriting other registers.
	 */
	for (unsigned int i = 0; i < 56; i++) {
		val = ((u32 *)params->zone_weights)[i]
			    & MALI_C55_AEXP_HIST_ZONE_WEIGHT_MASK;
		addr = base + MALI_C55_AEXP_HIST_ZONE_WEIGHTS_OFFSET + (4 * i);

		mali_c55_ctx_write(mali_c55, addr, val);
	}

	val = params->zone_weights[MALI_C55_MAX_ZONES - 1];
	addr = base + MALI_C55_AEXP_HIST_ZONE_WEIGHTS_OFFSET + (4 * 56);
}

static void mali_c55_params_digital_gain(struct mali_c55 *mali_c55,
					 union mali_c55_params_block block)
{
	const struct mali_c55_params_digital_gain *dgain;
	u32 gain;

	dgain = block.digital_gain;

	/*
	 * If the block is flagged as disabled we write a gain of 1.0, which in
	 * Q5.8 format is 256.
	 */
	gain = block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE ?
	       256 : dgain->gain;

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_DIGITAL_GAIN,
				 MALI_C55_DIGITAL_GAIN_MASK,
				 gain);
}

static void mali_c55_params_awb_gains(struct mali_c55 *mali_c55,
				      union mali_c55_params_block block)
{
	const struct mali_c55_params_awb_gains *gains;
	u32 gain00, gain01, gain10, gain11;

	gains = block.awb_gains;

	/*
	 * There are two places AWB gains can be set in the ISP; one affects the
	 * image output data and the other affects the statistics for the
	 * AEXP-0 tap point.
	 */
	u32 addr1 = block.header->type == MALI_C55_PARAM_BLOCK_AWB_GAINS ?
					   MALI_C55_REG_AWB_GAINS1 :
					   MALI_C55_REG_AWB_GAINS1_AEXP;
	u32 addr2 = block.header->type == MALI_C55_PARAM_BLOCK_AWB_GAINS ?
					   MALI_C55_REG_AWB_GAINS2 :
					   MALI_C55_REG_AWB_GAINS2_AEXP;

	/* If the block is flagged disabled, set all of the gains to 1.0 */
	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		gain00 = 256;
		gain01 = 256;
		gain10 = 256;
		gain11 = 256;
	} else {
		gain00 = gains->gain00;
		gain01 = gains->gain01;
		gain10 = gains->gain10;
		gain11 = gains->gain11;
	}

	mali_c55_ctx_update_bits(mali_c55, addr1, MALI_C55_AWB_GAIN00_MASK,
				 gain00);
	mali_c55_ctx_update_bits(mali_c55, addr1, MALI_C55_AWB_GAIN01_MASK,
				 MALI_C55_AWB_GAIN01(gain01));
	mali_c55_ctx_update_bits(mali_c55, addr2, MALI_C55_AWB_GAIN10_MASK,
				 gain10);
	mali_c55_ctx_update_bits(mali_c55, addr2, MALI_C55_AWB_GAIN11_MASK,
				 MALI_C55_AWB_GAIN11(gain11));
}

static void mali_c55_params_awb_config(struct mali_c55 *mali_c55,
				       union mali_c55_params_block block)
{
	const struct mali_c55_params_awb_config *params;

	params = block.awb_config;

	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_METERING_CONFIG,
					 MALI_C55_AWB_DISABLE_MASK,
					 MALI_C55_AWB_DISABLE_MASK);
		return;
	}

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_METERING_CONFIG,
				 MALI_C55_AWB_DISABLE_MASK, false);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_STATS_MODE,
				 MALI_C55_AWB_STATS_MODE_MASK, params->stats_mode);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_WHITE_LEVEL,
				 MALI_C55_AWB_WHITE_LEVEL_MASK, params->white_level);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_BLACK_LEVEL,
				 MALI_C55_AWB_BLACK_LEVEL_MASK, params->black_level);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CR_MAX,
				 MALI_C55_AWB_CR_MAX_MASK, params->cr_max);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CR_MIN,
				 MALI_C55_AWB_CR_MIN_MASK, params->cr_min);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CB_MAX,
				 MALI_C55_AWB_CB_MAX_MASK, params->cb_max);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CB_MIN,
				 MALI_C55_AWB_CB_MIN_MASK, params->cb_min);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_NODES_USED,
				 MALI_C55_AWB_NODES_USED_HORIZ_MASK,
				 params->nodes_used_horiz);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_NODES_USED,
				 MALI_C55_AWB_NODES_USED_VERT_MASK,
				 MALI_C55_AWB_NODES_USED_VERT(params->nodes_used_vert));

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CR_HIGH,
				 MALI_C55_AWB_CR_HIGH_MASK, params->cr_high);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CR_LOW,
				 MALI_C55_AWB_CR_LOW_MASK, params->cr_low);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CB_HIGH,
				 MALI_C55_AWB_CB_HIGH_MASK, params->cb_high);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_AWB_CB_LOW,
				 MALI_C55_AWB_CB_LOW_MASK, params->cb_low);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_METERING_CONFIG,
				 MALI_C55_AWB_SWITCH_MASK,
				 MALI_C55_AWB_SWITCH(params->tap_point));
}

static void mali_c55_params_lsc_config(struct mali_c55 *mali_c55,
				       union mali_c55_params_block block)
{
	const struct mali_c55_params_mesh_shading_config *params;
	unsigned int i;
	u32 addr;

	params = block.shading_config;

	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
					 MALI_C55_MESH_SHADING_ENABLE_MASK,
					 false);
		return;
	}

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_ENABLE_MASK, true);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_MESH_SHOW_MASK,
				 MALI_C55_MESH_SHADING_MESH_SHOW(params->mesh_show));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_SCALE_MASK,
				 MALI_C55_MESH_SHADING_SCALE(params->mesh_scale));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_PAGE_R_MASK,
				 MALI_C55_MESH_SHADING_PAGE_R(params->mesh_page_r));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_PAGE_G_MASK,
				 MALI_C55_MESH_SHADING_PAGE_G(params->mesh_page_g));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_PAGE_B_MASK,
				 MALI_C55_MESH_SHADING_PAGE_B(params->mesh_page_b));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_MESH_WIDTH_MASK,
				 MALI_C55_MESH_SHADING_MESH_WIDTH(params->mesh_width));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_CONFIG,
				 MALI_C55_MESH_SHADING_MESH_HEIGHT_MASK,
				 MALI_C55_MESH_SHADING_MESH_HEIGHT(params->mesh_height));

	for (i = 0; i < MALI_C55_NUM_MESH_SHADING_ELEMENTS; i++) {
		addr = MALI_C55_REG_MESH_SHADING_TABLES + (i * 4);
		mali_c55_ctx_write(mali_c55, addr, params->mesh[i]);
	}
}

static void mali_c55_params_lsc_selection(struct mali_c55 *mali_c55,
					  union mali_c55_params_block block)
{
	const struct mali_c55_params_mesh_shading_selection *params;

	params = block.shading_selection;

	if (block.header->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		return;

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_ALPHA_BANK,
				 MALI_C55_MESH_SHADING_ALPHA_BANK_R_MASK,
				 params->mesh_alpha_bank_r);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_ALPHA_BANK,
				 MALI_C55_MESH_SHADING_ALPHA_BANK_G_MASK,
				 MALI_C55_MESH_SHADING_ALPHA_BANK_G(params->mesh_alpha_bank_g));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_ALPHA_BANK,
				 MALI_C55_MESH_SHADING_ALPHA_BANK_B_MASK,
				 MALI_C55_MESH_SHADING_ALPHA_BANK_B(params->mesh_alpha_bank_b));

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_ALPHA,
				 MALI_C55_MESH_SHADING_ALPHA_R_MASK,
				 params->mesh_alpha_r);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_ALPHA,
				 MALI_C55_MESH_SHADING_ALPHA_G_MASK,
				 MALI_C55_MESH_SHADING_ALPHA_G(params->mesh_alpha_g));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_MESH_SHADING_ALPHA,
				 MALI_C55_MESH_SHADING_ALPHA_B_MASK,
				 MALI_C55_MESH_SHADING_ALPHA_B(params->mesh_alpha_b));

	mali_c55_ctx_update_bits(mali_c55,
				 MALI_C55_REG_MESH_SHADING_MESH_STRENGTH,
				 MALI_c55_MESH_STRENGTH_MASK,
				 params->mesh_strength);
}

static const mali_c55_params_handler mali_c55_params_handlers[] = {
	[MALI_C55_PARAM_BLOCK_SENSOR_OFFS] = &mali_c55_params_sensor_offs,
	[MALI_C55_PARAM_BLOCK_AEXP_HIST] = &mali_c55_params_aexp_hist,
	[MALI_C55_PARAM_BLOCK_AEXP_IHIST] = &mali_c55_params_aexp_hist,
	[MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS] = &mali_c55_params_aexp_hist_weights,
	[MALI_C55_PARAM_BLOCK_AEXP_IHIST_WEIGHTS] = &mali_c55_params_aexp_hist_weights,
	[MALI_C55_PARAM_BLOCK_DIGITAL_GAIN] = &mali_c55_params_digital_gain,
	[MALI_C55_PARAM_BLOCK_AWB_GAINS] = &mali_c55_params_awb_gains,
	[MALI_C55_PARAM_BLOCK_AWB_CONFIG] = &mali_c55_params_awb_config,
	[MALI_C55_PARAM_BLOCK_AWB_GAINS_AEXP] = &mali_c55_params_awb_gains,
	[MALI_C55_PARAM_MESH_SHADING_CONFIG] = &mali_c55_params_lsc_config,
	[MALI_C55_PARAM_MESH_SHADING_SELECTION] = &mali_c55_params_lsc_selection,
};

static const struct v4l2_isp_params_block_type_info
mali_c55_params_block_types_info[] = {
	[MALI_C55_PARAM_BLOCK_SENSOR_OFFS] = {
		.size = sizeof(struct mali_c55_params_sensor_off_preshading),
	},
	[MALI_C55_PARAM_BLOCK_AEXP_HIST] = {
		.size = sizeof(struct mali_c55_params_aexp_hist),
	},
	[MALI_C55_PARAM_BLOCK_AEXP_IHIST] = {
		.size = sizeof(struct mali_c55_params_aexp_hist),
	},
	[MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS] = {
		.size = sizeof(struct mali_c55_params_aexp_weights),
	},
	[MALI_C55_PARAM_BLOCK_AEXP_IHIST_WEIGHTS] = {
		.size = sizeof(struct mali_c55_params_aexp_weights),
	},
	[MALI_C55_PARAM_BLOCK_DIGITAL_GAIN] = {
		.size = sizeof(struct mali_c55_params_digital_gain),
	},
	[MALI_C55_PARAM_BLOCK_AWB_GAINS] = {
		.size = sizeof(struct mali_c55_params_awb_gains),
	},
	[MALI_C55_PARAM_BLOCK_AWB_CONFIG] = {
		.size = sizeof(struct mali_c55_params_awb_config),
	},
	[MALI_C55_PARAM_BLOCK_AWB_GAINS_AEXP] = {
		.size = sizeof(struct mali_c55_params_awb_gains),
	},
	[MALI_C55_PARAM_MESH_SHADING_CONFIG] = {
		.size = sizeof(struct mali_c55_params_mesh_shading_config),
	},
	[MALI_C55_PARAM_MESH_SHADING_SELECTION] = {
		.size = sizeof(struct mali_c55_params_mesh_shading_selection),
	},
};

static_assert(ARRAY_SIZE(mali_c55_params_handlers) ==
	      ARRAY_SIZE(mali_c55_params_block_types_info));

static int mali_c55_params_enum_fmt_meta_out(struct file *file, void *fh,
					     struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	if (f->mbus_code && f->mbus_code != MEDIA_BUS_FMT_METADATA_FIXED)
		return -EINVAL;

	f->pixelformat = V4L2_META_FMT_MALI_C55_PARAMS;

	return 0;
}

static int mali_c55_params_g_fmt_meta_out(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	static const struct v4l2_meta_format mfmt = {
		.dataformat = V4L2_META_FMT_MALI_C55_PARAMS,
		.buffersize = v4l2_isp_params_buffer_size(MALI_C55_PARAMS_MAX_SIZE),
	};

	f->fmt.meta = mfmt;

	return 0;
}

static int mali_c55_params_querycap(struct file *file,
				    void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, MALI_C55_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "ARM Mali-C55 ISP", sizeof(cap->card));

	return 0;
}

static const struct v4l2_ioctl_ops mali_c55_params_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_out = mali_c55_params_enum_fmt_meta_out,
	.vidioc_g_fmt_meta_out = mali_c55_params_g_fmt_meta_out,
	.vidioc_s_fmt_meta_out = mali_c55_params_g_fmt_meta_out,
	.vidioc_try_fmt_meta_out = mali_c55_params_g_fmt_meta_out,
	.vidioc_querycap = mali_c55_params_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations mali_c55_params_v4l2_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int
mali_c55_params_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			    unsigned int *num_planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	if (*num_planes && *num_planes > 1)
		return -EINVAL;

	if (sizes[0] && sizes[0] < v4l2_isp_params_buffer_size(MALI_C55_PARAMS_MAX_SIZE))
		return -EINVAL;

	*num_planes = 1;

	if (!sizes[0])
		sizes[0] = v4l2_isp_params_buffer_size(MALI_C55_PARAMS_MAX_SIZE);

	return 0;
}

static int mali_c55_params_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_params_buf *buf = to_mali_c55_params_buf(vbuf);

	buf->config = kvmalloc(v4l2_isp_params_buffer_size(MALI_C55_PARAMS_MAX_SIZE),
			       GFP_KERNEL);
	if (!buf->config)
		return -ENOMEM;

	return 0;
}

static void mali_c55_params_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_params_buf *buf = to_mali_c55_params_buf(vbuf);

	kvfree(buf->config);
	buf->config = NULL;
}

static int mali_c55_params_buf_prepare(struct vb2_buffer *vb)
{
	struct mali_c55_params *params = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_params_buf *buf = to_mali_c55_params_buf(vbuf);
	struct v4l2_isp_params_buffer *config = vb2_plane_vaddr(vb, 0);
	struct mali_c55 *mali_c55 = params->mali_c55;
	int ret;

	ret = v4l2_isp_params_validate_buffer_size(mali_c55->dev, vb,
			v4l2_isp_params_buffer_size(MALI_C55_PARAMS_MAX_SIZE));
	if (ret)
		return ret;

	/*
	 * Copy the parameters buffer provided by userspace to the internal
	 * scratch buffer. This protects against the chance of userspace making
	 * changed to the buffer content whilst the driver processes it.
	 */

	memcpy(buf->config, config, v4l2_isp_params_buffer_size(MALI_C55_PARAMS_MAX_SIZE));

	return v4l2_isp_params_validate_buffer(mali_c55->dev, vb, buf->config,
					       mali_c55_params_block_types_info,
					       ARRAY_SIZE(mali_c55_params_block_types_info));
}

static void mali_c55_params_buf_queue(struct vb2_buffer *vb)
{
	struct mali_c55_params *params = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_params_buf *buf = to_mali_c55_params_buf(vbuf);

	spin_lock(&params->buffers.lock);
	list_add_tail(&buf->queue, &params->buffers.queue);
	spin_unlock(&params->buffers.lock);
}

static void mali_c55_params_return_buffers(struct mali_c55_params *params,
					   enum vb2_buffer_state state)
{
	struct mali_c55_params_buf *buf, *tmp;

	guard(spinlock)(&params->buffers.lock);

	list_for_each_entry_safe(buf, tmp, &params->buffers.queue, queue) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static int mali_c55_params_start_streaming(struct vb2_queue *q,
					   unsigned int count)
{
	struct mali_c55_params *params = vb2_get_drv_priv(q);
	struct mali_c55 *mali_c55 = params->mali_c55;
	int ret;

	ret = pm_runtime_resume_and_get(mali_c55->dev);
	if (ret)
		goto err_return_buffers;

	ret = video_device_pipeline_alloc_start(&params->vdev);
	if (ret)
		goto err_pm_put;

	if (mali_c55_pipeline_ready(mali_c55)) {
		ret = v4l2_subdev_enable_streams(&mali_c55->isp.sd,
						 MALI_C55_ISP_PAD_SOURCE_VIDEO,
						 BIT(0));
		if (ret < 0)
			goto err_stop_pipeline;
	}

	return 0;

err_stop_pipeline:
	video_device_pipeline_stop(&params->vdev);
err_pm_put:
	pm_runtime_put_autosuspend(mali_c55->dev);
err_return_buffers:
	mali_c55_params_return_buffers(params, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void mali_c55_params_stop_streaming(struct vb2_queue *q)
{
	struct mali_c55_params *params = vb2_get_drv_priv(q);
	struct mali_c55 *mali_c55 = params->mali_c55;
	struct mali_c55_isp *isp = &mali_c55->isp;

	if (mali_c55_pipeline_ready(mali_c55)) {
		if (v4l2_subdev_is_streaming(&isp->sd))
			v4l2_subdev_disable_streams(&isp->sd,
						    MALI_C55_ISP_PAD_SOURCE_VIDEO,
						    BIT(0));
	}

	video_device_pipeline_stop(&params->vdev);
	mali_c55_params_return_buffers(params, VB2_BUF_STATE_ERROR);
	pm_runtime_put_autosuspend(params->mali_c55->dev);
}

static const struct vb2_ops mali_c55_params_vb2_ops = {
	.queue_setup = mali_c55_params_queue_setup,
	.buf_init = mali_c55_params_buf_init,
	.buf_cleanup = mali_c55_params_buf_cleanup,
	.buf_queue = mali_c55_params_buf_queue,
	.buf_prepare = mali_c55_params_buf_prepare,
	.start_streaming = mali_c55_params_start_streaming,
	.stop_streaming = mali_c55_params_stop_streaming,
};

void mali_c55_params_write_config(struct mali_c55 *mali_c55)
{
	struct mali_c55_params *params = &mali_c55->params;
	struct v4l2_isp_params_buffer *config;
	struct mali_c55_params_buf *buf;
	size_t block_offset = 0;
	size_t max_offset;

	spin_lock(&params->buffers.lock);

	buf = list_first_entry_or_null(&params->buffers.queue,
				       struct mali_c55_params_buf, queue);
	if (buf)
		list_del(&buf->queue);
	spin_unlock(&params->buffers.lock);

	if (!buf)
		return;

	buf->vb.sequence = mali_c55->isp.frame_sequence;
	config = buf->config;

	max_offset = config->data_size;

	/*
	 * Walk the list of parameter blocks and process them. No validation is
	 * done here, as the contents of the config buffer are already checked
	 * when the buffer is queued.
	 */
	while (max_offset && block_offset < max_offset) {
		union mali_c55_params_block block;
		mali_c55_params_handler handler;

		block.data = &config->data[block_offset];

		/* We checked the array index already in .buf_queue() */
		handler = mali_c55_params_handlers[block.header->type];
		handler(mali_c55, block);

		block_offset += block.header->size;
	}

	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

void mali_c55_unregister_params(struct mali_c55 *mali_c55)
{
	struct mali_c55_params *params = &mali_c55->params;

	if (!video_is_registered(&params->vdev))
		return;

	vb2_video_unregister_device(&params->vdev);
	media_entity_cleanup(&params->vdev.entity);
	mutex_destroy(&params->lock);
}

int mali_c55_register_params(struct mali_c55 *mali_c55)
{
	struct mali_c55_params *params = &mali_c55->params;
	struct video_device *vdev = &params->vdev;
	struct vb2_queue *vb2q = &params->queue;
	int ret;

	mutex_init(&params->lock);
	INIT_LIST_HEAD(&params->buffers.queue);
	spin_lock_init(&params->buffers.lock);

	params->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&params->vdev.entity, 1, &params->pad);
	if (ret)
		goto err_destroy_mutex;

	vb2q->type = V4L2_BUF_TYPE_META_OUTPUT;
	vb2q->io_modes = VB2_MMAP | VB2_DMABUF;
	vb2q->drv_priv = params;
	vb2q->mem_ops = &vb2_dma_contig_memops;
	vb2q->ops = &mali_c55_params_vb2_ops;
	vb2q->buf_struct_size = sizeof(struct mali_c55_params_buf);
	vb2q->min_queued_buffers = 1;
	vb2q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2q->lock = &params->lock;
	vb2q->dev = mali_c55->dev;

	ret = vb2_queue_init(vb2q);
	if (ret) {
		dev_err(mali_c55->dev, "params vb2 queue init failed\n");
		goto err_cleanup_entity;
	}

	strscpy(params->vdev.name, "mali-c55 3a params",
		sizeof(params->vdev.name));
	vdev->release = video_device_release_empty;
	vdev->fops = &mali_c55_params_v4l2_fops;
	vdev->ioctl_ops = &mali_c55_params_v4l2_ioctl_ops;
	vdev->lock = &params->lock;
	vdev->v4l2_dev = &mali_c55->v4l2_dev;
	vdev->queue = &params->queue;
	vdev->device_caps = V4L2_CAP_META_OUTPUT | V4L2_CAP_STREAMING |
			    V4L2_CAP_IO_MC;
	vdev->vfl_dir = VFL_DIR_TX;
	video_set_drvdata(vdev, params);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(mali_c55->dev,
			"failed to register params video device\n");
		goto err_release_vb2q;
	}

	params->mali_c55 = mali_c55;

	return 0;

err_release_vb2q:
	vb2_queue_release(vb2q);
err_cleanup_entity:
	media_entity_cleanup(&params->vdev.entity);
err_destroy_mutex:
	mutex_destroy(&params->lock);

	return ret;
}
