/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "gmc/gmc_8_2_sh_mask.h"
#include "gmc/gmc_8_2_d.h"

#include "include/logger_interface.h"

#include "dce110_compressor.h"

#define DC_LOGGER \
		cp110->base.ctx->logger
#define DCP_REG(reg)\
	(reg + cp110->offsets.dcp_offset)
#define DMIF_REG(reg)\
	(reg + cp110->offsets.dmif_offset)

static const struct dce110_compressor_reg_offsets reg_offsets[] = {
{
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset =
		(mmDMIF_PG0_DPG_PIPE_DPM_CONTROL
			- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset =
		(mmDMIF_PG1_DPG_PIPE_DPM_CONTROL
			- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset =
		(mmDMIF_PG2_DPG_PIPE_DPM_CONTROL
			- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
}
};

static uint32_t align_to_chunks_number_per_line(uint32_t pixels)
{
	return 256 * ((pixels + 255) / 256);
}

static void reset_lb_on_vblank(struct compressor *compressor, uint32_t crtc_inst)
{
	uint32_t value;
	uint32_t frame_count;
	uint32_t status_pos;
	uint32_t retry = 0;
	struct dce110_compressor *cp110 = TO_DCE110_COMPRESSOR(compressor);

	cp110->offsets = reg_offsets[crtc_inst];

	status_pos = dm_read_reg(compressor->ctx, DCP_REG(mmCRTC_STATUS_POSITION));


	/* Only if CRTC is enabled and counter is moving we wait for one frame. */
	if (status_pos != dm_read_reg(compressor->ctx, DCP_REG(mmCRTC_STATUS_POSITION))) {
		/* Resetting LB on VBlank */
		value = dm_read_reg(compressor->ctx, DCP_REG(mmLB_SYNC_RESET_SEL));
		set_reg_field_value(value, 3, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL);
		set_reg_field_value(value, 1, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL2);
		dm_write_reg(compressor->ctx, DCP_REG(mmLB_SYNC_RESET_SEL), value);

		frame_count = dm_read_reg(compressor->ctx, DCP_REG(mmCRTC_STATUS_FRAME_COUNT));


		for (retry = 10000; retry > 0; retry--) {
			if (frame_count != dm_read_reg(compressor->ctx, DCP_REG(mmCRTC_STATUS_FRAME_COUNT)))
				break;
			udelay(10);
		}
		if (!retry)
			dm_error("Frame count did not increase for 100ms.\n");

		/* Resetting LB on VBlank */
		value = dm_read_reg(compressor->ctx, DCP_REG(mmLB_SYNC_RESET_SEL));
		set_reg_field_value(value, 2, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL);
		set_reg_field_value(value, 0, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL2);
		dm_write_reg(compressor->ctx, DCP_REG(mmLB_SYNC_RESET_SEL), value);
	}
}

static void wait_for_fbc_state_changed(
	struct dce110_compressor *cp110,
	bool enabled)
{
	uint32_t counter = 0;
	uint32_t addr = mmFBC_STATUS;
	uint32_t value;

	while (counter < 1000) {
		value = dm_read_reg(cp110->base.ctx, addr);
		if (get_reg_field_value(
			value,
			FBC_STATUS,
			FBC_ENABLE_STATUS) == enabled)
			break;
		udelay(100);
		counter++;
	}

	if (counter == 1000) {
		DC_LOG_WARNING("%s: wait counter exceeded, changes to HW not applied",
			__func__);
	} else {
		DC_LOG_SYNC("FBC status changed to %d", enabled);
	}


}

void dce110_compressor_power_up_fbc(struct compressor *compressor)
{
	uint32_t value;
	uint32_t addr;

	addr = mmFBC_CNTL;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(value, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
	set_reg_field_value(value, 1, FBC_CNTL, FBC_EN);
	set_reg_field_value(value, 2, FBC_CNTL, FBC_COHERENCY_MODE);
	if (compressor->options.bits.CLK_GATING_DISABLED == 1) {
		/* HW needs to do power measurement comparison. */
		set_reg_field_value(
			value,
			0,
			FBC_CNTL,
			FBC_COMP_CLK_GATE_EN);
	}
	dm_write_reg(compressor->ctx, addr, value);

	addr = mmFBC_COMP_MODE;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(value, 1, FBC_COMP_MODE, FBC_RLE_EN);
	set_reg_field_value(value, 1, FBC_COMP_MODE, FBC_DPCM4_RGB_EN);
	set_reg_field_value(value, 1, FBC_COMP_MODE, FBC_IND_EN);
	dm_write_reg(compressor->ctx, addr, value);

	addr = mmFBC_COMP_CNTL;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(value, 1, FBC_COMP_CNTL, FBC_DEPTH_RGB08_EN);
	dm_write_reg(compressor->ctx, addr, value);
	/*FBC_MIN_COMPRESSION 0 ==> 2:1 */
	/*                    1 ==> 4:1 */
	/*                    2 ==> 8:1 */
	/*                  0xF ==> 1:1 */
	set_reg_field_value(value, 0xF, FBC_COMP_CNTL, FBC_MIN_COMPRESSION);
	dm_write_reg(compressor->ctx, addr, value);
	compressor->min_compress_ratio = FBC_COMPRESS_RATIO_1TO1;

	value = 0;
	dm_write_reg(compressor->ctx, mmFBC_IND_LUT0, value);

	value = 0xFFFFFF;
	dm_write_reg(compressor->ctx, mmFBC_IND_LUT1, value);
}

void dce110_compressor_enable_fbc(
	struct compressor *compressor,
	struct compr_addr_and_pitch_params *params)
{
	struct dce110_compressor *cp110 = TO_DCE110_COMPRESSOR(compressor);

	if (compressor->options.bits.FBC_SUPPORT &&
		(!dce110_compressor_is_fbc_enabled_in_hw(compressor, NULL))) {

		uint32_t addr;
		uint32_t value, misc_value;

		addr = mmFBC_CNTL;
		value = dm_read_reg(compressor->ctx, addr);
		set_reg_field_value(value, 1, FBC_CNTL, FBC_GRPH_COMP_EN);
		/* params->inst is valid HW CRTC instance start from 0 */
		set_reg_field_value(
			value,
			params->inst,
			FBC_CNTL, FBC_SRC_SEL);
		dm_write_reg(compressor->ctx, addr, value);

		/* Keep track of enum controller_id FBC is attached to */
		compressor->is_enabled = true;
		/* attached_inst is SW CRTC instance start from 1
		 * 0 = CONTROLLER_ID_UNDEFINED means not attached crtc
		 */
		compressor->attached_inst = params->inst + CONTROLLER_ID_D0;

		/* Toggle it as there is bug in HW */
		set_reg_field_value(value, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
		dm_write_reg(compressor->ctx, addr, value);

		/* FBC usage with scatter & gather for dce110 */
		misc_value = dm_read_reg(compressor->ctx, mmFBC_MISC);

		set_reg_field_value(misc_value, 1,
				FBC_MISC, FBC_INVALIDATE_ON_ERROR);
		set_reg_field_value(misc_value, 1,
				FBC_MISC, FBC_DECOMPRESS_ERROR_CLEAR);
		set_reg_field_value(misc_value, 0x14,
				FBC_MISC, FBC_SLOW_REQ_INTERVAL);

		dm_write_reg(compressor->ctx, mmFBC_MISC, misc_value);

		/* Enable FBC */
		set_reg_field_value(value, 1, FBC_CNTL, FBC_GRPH_COMP_EN);
		dm_write_reg(compressor->ctx, addr, value);

		wait_for_fbc_state_changed(cp110, true);
	}
}

void dce110_compressor_disable_fbc(struct compressor *compressor)
{
	struct dce110_compressor *cp110 = TO_DCE110_COMPRESSOR(compressor);
	uint32_t crtc_inst = 0;

	if (compressor->options.bits.FBC_SUPPORT) {
		if (dce110_compressor_is_fbc_enabled_in_hw(compressor, &crtc_inst)) {
			uint32_t reg_data;
			/* Turn off compression */
			reg_data = dm_read_reg(compressor->ctx, mmFBC_CNTL);
			set_reg_field_value(reg_data, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
			dm_write_reg(compressor->ctx, mmFBC_CNTL, reg_data);

			/* Reset enum controller_id to undefined */
			compressor->attached_inst = 0;
			compressor->is_enabled = false;

			wait_for_fbc_state_changed(cp110, false);
		}

		/* Sync line buffer which fbc was attached to dce100/110 only */
		if (crtc_inst > CONTROLLER_ID_UNDEFINED && crtc_inst < CONTROLLER_ID_D3)
			reset_lb_on_vblank(compressor,
					crtc_inst - CONTROLLER_ID_D0);
	}
}

bool dce110_compressor_is_fbc_enabled_in_hw(
	struct compressor *compressor,
	uint32_t *inst)
{
	/* Check the hardware register */
	uint32_t value;

	value = dm_read_reg(compressor->ctx, mmFBC_STATUS);
	if (get_reg_field_value(value, FBC_STATUS, FBC_ENABLE_STATUS)) {
		if (inst != NULL)
			*inst = compressor->attached_inst;
		return true;
	}

	value = dm_read_reg(compressor->ctx, mmFBC_MISC);
	if (get_reg_field_value(value, FBC_MISC, FBC_STOP_ON_HFLIP_EVENT)) {
		value = dm_read_reg(compressor->ctx, mmFBC_CNTL);

		if (get_reg_field_value(value, FBC_CNTL, FBC_GRPH_COMP_EN)) {
			if (inst != NULL)
				*inst =
					compressor->attached_inst;
			return true;
		}
	}
	return false;
}


void dce110_compressor_program_compressed_surface_address_and_pitch(
	struct compressor *compressor,
	struct compr_addr_and_pitch_params *params)
{
	struct dce110_compressor *cp110 = TO_DCE110_COMPRESSOR(compressor);
	uint32_t value = 0;
	uint32_t fbc_pitch = 0;
	uint32_t compressed_surf_address_low_part =
		compressor->compr_surface_address.addr.low_part;

	cp110->offsets = reg_offsets[params->inst];

	/* Clear content first. */
	dm_write_reg(
		compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS_HIGH),
		0);
	dm_write_reg(compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS), 0);

	/* Write address, HIGH has to be first. */
	dm_write_reg(compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS_HIGH),
		compressor->compr_surface_address.addr.high_part);
	dm_write_reg(compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS),
		compressed_surf_address_low_part);

	fbc_pitch = align_to_chunks_number_per_line(params->source_view_width);

	if (compressor->min_compress_ratio == FBC_COMPRESS_RATIO_1TO1)
		fbc_pitch = fbc_pitch / 8;
	else
		DC_LOG_WARNING("%s: Unexpected DCE11 compression ratio",
			__func__);

	/* Clear content first. */
	dm_write_reg(compressor->ctx, DCP_REG(mmGRPH_COMPRESS_PITCH), 0);

	/* Write FBC Pitch. */
	set_reg_field_value(
		value,
		fbc_pitch,
		GRPH_COMPRESS_PITCH,
		GRPH_COMPRESS_PITCH);
	dm_write_reg(compressor->ctx, DCP_REG(mmGRPH_COMPRESS_PITCH), value);

}

void dce110_compressor_set_fbc_invalidation_triggers(
	struct compressor *compressor,
	uint32_t fbc_trigger)
{
	/* Disable region hit event, FBC_MEMORY_REGION_MASK = 0 (bits 16-19)
	 * for DCE 11 regions cannot be used - does not work with S/G
	 */
	uint32_t addr = mmFBC_CLIENT_REGION_MASK;
	uint32_t value = dm_read_reg(compressor->ctx, addr);

	set_reg_field_value(
		value,
		0,
		FBC_CLIENT_REGION_MASK,
		FBC_MEMORY_REGION_MASK);
	dm_write_reg(compressor->ctx, addr, value);

	/* Setup events when to clear all CSM entries (effectively marking
	 * current compressed data invalid)
	 * For DCE 11 CSM metadata 11111 means - "Not Compressed"
	 * Used as the initial value of the metadata sent to the compressor
	 * after invalidation, to indicate that the compressor should attempt
	 * to compress all chunks on the current pass.  Also used when the chunk
	 * is not successfully written to memory.
	 * When this CSM value is detected, FBC reads from the uncompressed
	 * buffer. Set events according to passed in value, these events are
	 * valid for DCE11:
	 *     - bit  0 - display register updated
	 *     - bit 28 - memory write from any client except from MCIF
	 *     - bit 29 - CG static screen signal is inactive
	 * In addition, DCE11.1 also needs to set new DCE11.1 specific events
	 * that are used to trigger invalidation on certain register changes,
	 * for example enabling of Alpha Compression may trigger invalidation of
	 * FBC once bit is set. These events are as follows:
	 *      - Bit 2 - FBC_GRPH_COMP_EN register updated
	 *      - Bit 3 - FBC_SRC_SEL register updated
	 *      - Bit 4 - FBC_MIN_COMPRESSION register updated
	 *      - Bit 5 - FBC_ALPHA_COMP_EN register updated
	 *      - Bit 6 - FBC_ZERO_ALPHA_CHUNK_SKIP_EN register updated
	 *      - Bit 7 - FBC_FORCE_COPY_TO_COMP_BUF register updated
	 */
	addr = mmFBC_IDLE_FORCE_CLEAR_MASK;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(
		value,
		fbc_trigger,
		FBC_IDLE_FORCE_CLEAR_MASK,
		FBC_IDLE_FORCE_CLEAR_MASK);
	dm_write_reg(compressor->ctx, addr, value);
}

struct compressor *dce110_compressor_create(struct dc_context *ctx)
{
	struct dce110_compressor *cp110 =
		kzalloc(sizeof(struct dce110_compressor), GFP_KERNEL);

	if (!cp110)
		return NULL;

	dce110_compressor_construct(cp110, ctx);
	return &cp110->base;
}

void dce110_compressor_destroy(struct compressor **compressor)
{
	kfree(TO_DCE110_COMPRESSOR(*compressor));
	*compressor = NULL;
}

bool dce110_get_required_compressed_surfacesize(struct fbc_input_info fbc_input_info,
						struct fbc_requested_compressed_size size)
{
	bool result = false;

	unsigned int max_x = FBC_MAX_X, max_y = FBC_MAX_Y;

	get_max_support_fbc_buffersize(&max_x, &max_y);

	if (fbc_input_info.dynamic_fbc_buffer_alloc == 0) {
		/*
		 * For DCE11 here use Max HW supported size:  HW Support up to 3840x2400 resolution
		 * or 18000 chunks.
		 */
		size.preferred_size = size.min_size = align_to_chunks_number_per_line(max_x) * max_y * 4;  /* (For FBC when LPT not supported). */
		size.preferred_size_alignment = size.min_size_alignment = 0x100;       /* For FBC when LPT not supported */
		size.bits.preferred_must_be_framebuffer_pool = 1;
		size.bits.min_must_be_framebuffer_pool = 1;

		result = true;
	}
	/*
	 * Maybe to add registry key support with optional size here to override above
	 * for debugging purposes
	 */

	return result;
}


void get_max_support_fbc_buffersize(unsigned int *max_x, unsigned int *max_y)
{
	*max_x = FBC_MAX_X;
	*max_y = FBC_MAX_Y;

	/* if (m_smallLocalFrameBufferMemory == 1)
	 * {
	 *	*max_x = FBC_MAX_X_SG;
	 *	*max_y = FBC_MAX_Y_SG;
	 * }
	 */
}


unsigned int controller_id_to_index(enum controller_id controller_id)
{
	unsigned int index = 0;

	switch (controller_id) {
	case CONTROLLER_ID_D0:
		index = 0;
		break;
	case CONTROLLER_ID_D1:
		index = 1;
		break;
	case CONTROLLER_ID_D2:
		index = 2;
		break;
	case CONTROLLER_ID_D3:
		index = 3;
		break;
	default:
		break;
	}
	return index;
}


static const struct compressor_funcs dce110_compressor_funcs = {
	.power_up_fbc = dce110_compressor_power_up_fbc,
	.enable_fbc = dce110_compressor_enable_fbc,
	.disable_fbc = dce110_compressor_disable_fbc,
	.set_fbc_invalidation_triggers = dce110_compressor_set_fbc_invalidation_triggers,
	.surface_address_and_pitch = dce110_compressor_program_compressed_surface_address_and_pitch,
	.is_fbc_enabled_in_hw = dce110_compressor_is_fbc_enabled_in_hw
};


void dce110_compressor_construct(struct dce110_compressor *compressor,
	struct dc_context *ctx)
{

	compressor->base.options.raw = 0;
	compressor->base.options.bits.FBC_SUPPORT = true;

	/* for dce 11 always use one dram channel for lpt */
	compressor->base.lpt_channels_num = 1;
	compressor->base.options.bits.DUMMY_BACKEND = false;

	/*
	 * check if this system has more than 1 dram channel; if only 1 then lpt
	 * should not be supported
	 */


	compressor->base.options.bits.CLK_GATING_DISABLED = false;

	compressor->base.ctx = ctx;
	compressor->base.embedded_panel_h_size = 0;
	compressor->base.embedded_panel_v_size = 0;
	compressor->base.memory_bus_width = ctx->asic_id.vram_width;
	compressor->base.allocated_size = 0;
	compressor->base.preferred_requested_size = 0;
	compressor->base.min_compress_ratio = FBC_COMPRESS_RATIO_INVALID;
	compressor->base.banks_num = 0;
	compressor->base.raw_size = 0;
	compressor->base.channel_interleave_size = 0;
	compressor->base.dram_channels_num = 0;
	compressor->base.lpt_channels_num = 0;
	compressor->base.attached_inst = CONTROLLER_ID_UNDEFINED;
	compressor->base.is_enabled = false;
	compressor->base.funcs = &dce110_compressor_funcs;

}

