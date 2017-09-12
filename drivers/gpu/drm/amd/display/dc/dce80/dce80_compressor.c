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

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"
#include "gmc/gmc_7_1_sh_mask.h"
#include "gmc/gmc_7_1_d.h"

#include "include/logger_interface.h"
#include "dce80_compressor.h"

#define DCP_REG(reg)\
	(reg + cp80->offsets.dcp_offset)
#define DMIF_REG(reg)\
	(reg + cp80->offsets.dmif_offset)

static const struct dce80_compressor_reg_offsets reg_offsets[] = {
{
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset = (mmDMIF_PG0_DPG_PIPE_DPM_CONTROL
					- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset = (mmDMIF_PG1_DPG_PIPE_DPM_CONTROL
					- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset = (mmDMIF_PG2_DPG_PIPE_DPM_CONTROL
					- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset = (mmDMIF_PG3_DPG_PIPE_DPM_CONTROL
					- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset = (mmDMIF_PG4_DPG_PIPE_DPM_CONTROL
					- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
},
{
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif_offset = (mmDMIF_PG5_DPG_PIPE_DPM_CONTROL
					- mmDMIF_PG0_DPG_PIPE_DPM_CONTROL),
}
};

static const uint32_t dce8_one_lpt_channel_max_resolution = 2048 * 1200;

enum fbc_idle_force {
	/* Bit 0 - Display registers updated */
	FBC_IDLE_FORCE_DISPLAY_REGISTER_UPDATE = 0x00000001,

	/* Bit 2 - FBC_GRPH_COMP_EN register updated */
	FBC_IDLE_FORCE_GRPH_COMP_EN = 0x00000002,
	/* Bit 3 - FBC_SRC_SEL register updated */
	FBC_IDLE_FORCE_SRC_SEL_CHANGE = 0x00000004,
	/* Bit 4 - FBC_MIN_COMPRESSION register updated */
	FBC_IDLE_FORCE_MIN_COMPRESSION_CHANGE = 0x00000008,
	/* Bit 5 - FBC_ALPHA_COMP_EN register updated */
	FBC_IDLE_FORCE_ALPHA_COMP_EN = 0x00000010,
	/* Bit 6 - FBC_ZERO_ALPHA_CHUNK_SKIP_EN register updated */
	FBC_IDLE_FORCE_ZERO_ALPHA_CHUNK_SKIP_EN = 0x00000020,
	/* Bit 7 - FBC_FORCE_COPY_TO_COMP_BUF register updated */
	FBC_IDLE_FORCE_FORCE_COPY_TO_COMP_BUF = 0x00000040,

	/* Bit 24 - Memory write to region 0 defined by MC registers. */
	FBC_IDLE_FORCE_MEMORY_WRITE_TO_REGION0 = 0x01000000,
	/* Bit 25 - Memory write to region 1 defined by MC registers */
	FBC_IDLE_FORCE_MEMORY_WRITE_TO_REGION1 = 0x02000000,
	/* Bit 26 - Memory write to region 2 defined by MC registers */
	FBC_IDLE_FORCE_MEMORY_WRITE_TO_REGION2 = 0x04000000,
	/* Bit 27 - Memory write to region 3 defined by MC registers. */
	FBC_IDLE_FORCE_MEMORY_WRITE_TO_REGION3 = 0x08000000,

	/* Bit 28 - Memory write from any client other than MCIF */
	FBC_IDLE_FORCE_MEMORY_WRITE_OTHER_THAN_MCIF = 0x10000000,
	/* Bit 29 - CG statics screen signal is inactive */
	FBC_IDLE_FORCE_CG_STATIC_SCREEN_IS_INACTIVE = 0x20000000,
};

static uint32_t lpt_size_alignment(struct dce80_compressor *cp80)
{
	/*LPT_ALIGNMENT (in bytes) = ROW_SIZE * #BANKS * # DRAM CHANNELS. */
	return cp80->base.raw_size * cp80->base.banks_num *
		cp80->base.dram_channels_num;
}

static uint32_t lpt_memory_control_config(struct dce80_compressor *cp80,
	uint32_t lpt_control)
{
	/*LPT MC Config */
	if (cp80->base.options.bits.LPT_MC_CONFIG == 1) {
		/* POSSIBLE VALUES for LPT NUM_PIPES (DRAM CHANNELS):
		 * 00 - 1 CHANNEL
		 * 01 - 2 CHANNELS
		 * 02 - 4 OR 6 CHANNELS
		 * (Only for discrete GPU, N/A for CZ)
		 * 03 - 8 OR 12 CHANNELS
		 * (Only for discrete GPU, N/A for CZ) */
		switch (cp80->base.dram_channels_num) {
		case 2:
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_PIPES);
			break;
		case 1:
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_PIPES);
			break;
		default:
			dm_logger_write(
				cp80->base.ctx->logger, LOG_WARNING,
				"%s: Invalid LPT NUM_PIPES!!!",
				__func__);
			break;
		}

		/* The mapping for LPT NUM_BANKS is in
		 * GRPH_CONTROL.GRPH_NUM_BANKS register field
		 * Specifies the number of memory banks for tiling
		 * purposes. Only applies to 2D and 3D tiling modes.
		 * POSSIBLE VALUES:
		 * 00 - DCP_GRPH_NUM_BANKS_2BANK: ADDR_SURF_2_BANK
		 * 01 - DCP_GRPH_NUM_BANKS_4BANK: ADDR_SURF_4_BANK
		 * 02 - DCP_GRPH_NUM_BANKS_8BANK: ADDR_SURF_8_BANK
		 * 03 - DCP_GRPH_NUM_BANKS_16BANK: ADDR_SURF_16_BANK */
		switch (cp80->base.banks_num) {
		case 16:
			set_reg_field_value(
				lpt_control,
				3,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		case 8:
			set_reg_field_value(
				lpt_control,
				2,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		case 4:
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		case 2:
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		default:
			dm_logger_write(
				cp80->base.ctx->logger, LOG_WARNING,
				"%s: Invalid LPT NUM_BANKS!!!",
				__func__);
			break;
		}

		/* The mapping is in DMIF_ADDR_CALC.
		 * ADDR_CONFIG_PIPE_INTERLEAVE_SIZE register field for
		 * Carrizo specifies the memory interleave per pipe.
		 * It effectively specifies the location of pipe bits in
		 * the memory address.
		 * POSSIBLE VALUES:
		 * 00 - ADDR_CONFIG_PIPE_INTERLEAVE_256B: 256 byte
		 * interleave
		 * 01 - ADDR_CONFIG_PIPE_INTERLEAVE_512B: 512 byte
		 * interleave
		 */
		switch (cp80->base.channel_interleave_size) {
		case 256: /*256B */
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_PIPE_INTERLEAVE_SIZE);
			break;
		case 512: /*512B */
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_PIPE_INTERLEAVE_SIZE);
			break;
		default:
			dm_logger_write(
				cp80->base.ctx->logger, LOG_WARNING,
				"%s: Invalid LPT INTERLEAVE_SIZE!!!",
				__func__);
			break;
		}

		/* The mapping for LOW_POWER_TILING_ROW_SIZE is in
		 * DMIF_ADDR_CALC.ADDR_CONFIG_ROW_SIZE register field
		 * for Carrizo. Specifies the size of dram row in bytes.
		 * This should match up with NOOFCOLS field in
		 * MC_ARB_RAMCFG (ROW_SIZE = 4 * 2 ^^ columns).
		 * This register DMIF_ADDR_CALC is not used by the
		 * hardware as it is only used for addrlib assertions.
		 * POSSIBLE VALUES:
		 * 00 - ADDR_CONFIG_1KB_ROW: Treat 1KB as DRAM row
		 * boundary
		 * 01 - ADDR_CONFIG_2KB_ROW: Treat 2KB as DRAM row
		 * boundary
		 * 02 - ADDR_CONFIG_4KB_ROW: Treat 4KB as DRAM row
		 * boundary */
		switch (cp80->base.raw_size) {
		case 4096: /*4 KB */
			set_reg_field_value(
				lpt_control,
				2,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_ROW_SIZE);
			break;
		case 2048:
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_ROW_SIZE);
			break;
		case 1024:
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_ROW_SIZE);
			break;
		default:
			dm_logger_write(
				cp80->base.ctx->logger, LOG_WARNING,
				"%s: Invalid LPT ROW_SIZE!!!",
				__func__);
			break;
		}
	} else {
		dm_logger_write(
			cp80->base.ctx->logger, LOG_WARNING,
			"%s: LPT MC Configuration is not provided",
			__func__);
	}

	return lpt_control;
}

static bool is_source_bigger_than_epanel_size(
	struct dce80_compressor *cp80,
	uint32_t source_view_width,
	uint32_t source_view_height)
{
	if (cp80->base.embedded_panel_h_size != 0 &&
		cp80->base.embedded_panel_v_size != 0 &&
		((source_view_width * source_view_height) >
		(cp80->base.embedded_panel_h_size *
			cp80->base.embedded_panel_v_size)))
		return true;

	return false;
}

static uint32_t align_to_chunks_number_per_line(
	struct dce80_compressor *cp80,
	uint32_t pixels)
{
	return 256 * ((pixels + 255) / 256);
}

static void wait_for_fbc_state_changed(
	struct dce80_compressor *cp80,
	bool enabled)
{
	uint8_t counter = 0;
	uint32_t addr = mmFBC_STATUS;
	uint32_t value;

	while (counter < 10) {
		value = dm_read_reg(cp80->base.ctx, addr);
		if (get_reg_field_value(
			value,
			FBC_STATUS,
			FBC_ENABLE_STATUS) == enabled)
			break;
		udelay(10);
		counter++;
	}

	if (counter == 10) {
		dm_logger_write(
			cp80->base.ctx->logger, LOG_WARNING,
			"%s: wait counter exceeded, changes to HW not applied",
			__func__);
	}
}

void dce80_compressor_power_up_fbc(struct compressor *compressor)
{
	uint32_t value;
	uint32_t addr;

	addr = mmFBC_CNTL;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(value, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
	set_reg_field_value(value, 1, FBC_CNTL, FBC_EN);
	set_reg_field_value(value, 2, FBC_CNTL, FBC_COHERENCY_MODE);
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

void dce80_compressor_enable_fbc(
	struct compressor *compressor,
	uint32_t paths_num,
	struct compr_addr_and_pitch_params *params)
{
	struct dce80_compressor *cp80 = TO_DCE80_COMPRESSOR(compressor);

	if (compressor->options.bits.FBC_SUPPORT &&
		(compressor->options.bits.DUMMY_BACKEND == 0) &&
		(!dce80_compressor_is_fbc_enabled_in_hw(compressor, NULL)) &&
		(!is_source_bigger_than_epanel_size(
			cp80,
			params->source_view_width,
			params->source_view_height))) {

		uint32_t addr;
		uint32_t value;

		/* Before enabling FBC first need to enable LPT if applicable
		 * LPT state should always be changed (enable/disable) while FBC
		 * is disabled */
		if (compressor->options.bits.LPT_SUPPORT && (paths_num < 2) &&
			(params->source_view_width *
				params->source_view_height <=
				dce8_one_lpt_channel_max_resolution)) {
			dce80_compressor_enable_lpt(compressor);
		}

		addr = mmFBC_CNTL;
		value = dm_read_reg(compressor->ctx, addr);
		set_reg_field_value(value, 1, FBC_CNTL, FBC_GRPH_COMP_EN);
		set_reg_field_value(
			value,
			params->inst,
			FBC_CNTL, FBC_SRC_SEL);
		dm_write_reg(compressor->ctx, addr, value);

		/* Keep track of enum controller_id FBC is attached to */
		compressor->is_enabled = true;
		compressor->attached_inst = params->inst;
		cp80->offsets = reg_offsets[params->inst - 1];

		/*Toggle it as there is bug in HW */
		set_reg_field_value(value, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
		dm_write_reg(compressor->ctx, addr, value);
		set_reg_field_value(value, 1, FBC_CNTL, FBC_GRPH_COMP_EN);
		dm_write_reg(compressor->ctx, addr, value);

		wait_for_fbc_state_changed(cp80, true);
	}
}

void dce80_compressor_disable_fbc(struct compressor *compressor)
{
	struct dce80_compressor *cp80 = TO_DCE80_COMPRESSOR(compressor);

	if (compressor->options.bits.FBC_SUPPORT &&
		dce80_compressor_is_fbc_enabled_in_hw(compressor, NULL)) {
		uint32_t reg_data;
		/* Turn off compression */
		reg_data = dm_read_reg(compressor->ctx, mmFBC_CNTL);
		set_reg_field_value(reg_data, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
		dm_write_reg(compressor->ctx, mmFBC_CNTL, reg_data);

		/* Reset enum controller_id to undefined */
		compressor->attached_inst = 0;
		compressor->is_enabled = false;

		/* Whenever disabling FBC make sure LPT is disabled if LPT
		 * supported */
		if (compressor->options.bits.LPT_SUPPORT)
			dce80_compressor_disable_lpt(compressor);

		wait_for_fbc_state_changed(cp80, false);
	}
}

bool dce80_compressor_is_fbc_enabled_in_hw(
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

	value = dm_read_reg(compressor->ctx, mmFBC_CNTL);
	if (get_reg_field_value(value, FBC_CNTL, FBC_GRPH_COMP_EN)) {
		if (inst != NULL)
			*inst =	compressor->attached_inst;
		return true;
	}

	return false;
}

bool dce80_compressor_is_lpt_enabled_in_hw(struct compressor *compressor)
{
	/* Check the hardware register */
	uint32_t value = dm_read_reg(compressor->ctx,
		mmLOW_POWER_TILING_CONTROL);

	return get_reg_field_value(
		value,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ENABLE);
}

void dce80_compressor_program_compressed_surface_address_and_pitch(
	struct compressor *compressor,
	struct compr_addr_and_pitch_params *params)
{
	struct dce80_compressor *cp80 = TO_DCE80_COMPRESSOR(compressor);
	uint32_t value = 0;
	uint32_t fbc_pitch = 0;
	uint32_t compressed_surf_address_low_part =
		compressor->compr_surface_address.addr.low_part;

	/* Clear content first. */
	dm_write_reg(
		compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS_HIGH),
		0);
	dm_write_reg(compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS), 0);

	if (compressor->options.bits.LPT_SUPPORT) {
		uint32_t lpt_alignment = lpt_size_alignment(cp80);

		if (lpt_alignment != 0) {
			compressed_surf_address_low_part =
				((compressed_surf_address_low_part
					+ (lpt_alignment - 1)) / lpt_alignment)
					* lpt_alignment;
		}
	}

	/* Write address, HIGH has to be first. */
	dm_write_reg(compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS_HIGH),
		compressor->compr_surface_address.addr.high_part);
	dm_write_reg(compressor->ctx,
		DCP_REG(mmGRPH_COMPRESS_SURFACE_ADDRESS),
		compressed_surf_address_low_part);

	fbc_pitch = align_to_chunks_number_per_line(
		cp80,
		params->source_view_width);

	if (compressor->min_compress_ratio == FBC_COMPRESS_RATIO_1TO1)
		fbc_pitch = fbc_pitch / 8;
	else
		dm_logger_write(
			compressor->ctx->logger, LOG_WARNING,
			"%s: Unexpected DCE8 compression ratio",
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

void dce80_compressor_disable_lpt(struct compressor *compressor)
{
	struct dce80_compressor *cp80 = TO_DCE80_COMPRESSOR(compressor);
	uint32_t value;
	uint32_t addr;
	uint32_t inx;

	/* Disable all pipes LPT Stutter */
	for (inx = 0; inx < 3; inx++) {
		value =
			dm_read_reg(
				compressor->ctx,
				DMIF_REG(mmDPG_PIPE_STUTTER_CONTROL_NONLPTCH));
		set_reg_field_value(
			value,
			0,
			DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
			STUTTER_ENABLE_NONLPTCH);
		dm_write_reg(
			compressor->ctx,
			DMIF_REG(mmDPG_PIPE_STUTTER_CONTROL_NONLPTCH),
			value);
	}

	/* Disable LPT */
	addr = mmLOW_POWER_TILING_CONTROL;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(
		value,
		0,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ENABLE);
	dm_write_reg(compressor->ctx, addr, value);

	/* Clear selection of Channel(s) containing Compressed Surface */
	addr = mmGMCON_LPT_TARGET;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(
		value,
		0xFFFFFFFF,
		GMCON_LPT_TARGET,
		STCTRL_LPT_TARGET);
	dm_write_reg(compressor->ctx, mmGMCON_LPT_TARGET, value);
}

void dce80_compressor_enable_lpt(struct compressor *compressor)
{
	struct dce80_compressor *cp80 = TO_DCE80_COMPRESSOR(compressor);
	uint32_t value;
	uint32_t addr;
	uint32_t value_control;
	uint32_t channels;

	/* Enable LPT Stutter from Display pipe */
	value = dm_read_reg(compressor->ctx,
		DMIF_REG(mmDPG_PIPE_STUTTER_CONTROL_NONLPTCH));
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
		STUTTER_ENABLE_NONLPTCH);
	dm_write_reg(compressor->ctx,
		DMIF_REG(mmDPG_PIPE_STUTTER_CONTROL_NONLPTCH), value);

	/* Selection of Channel(s) containing Compressed Surface: 0xfffffff
	 * will disable LPT.
	 * STCTRL_LPT_TARGETn corresponds to channel n. */
	addr = mmLOW_POWER_TILING_CONTROL;
	value_control = dm_read_reg(compressor->ctx, addr);
	channels = get_reg_field_value(value_control,
			LOW_POWER_TILING_CONTROL,
			LOW_POWER_TILING_MODE);

	addr = mmGMCON_LPT_TARGET;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(
		value,
		channels + 1, /* not mentioned in programming guide,
				but follow DCE8.1 */
		GMCON_LPT_TARGET,
		STCTRL_LPT_TARGET);
	dm_write_reg(compressor->ctx, addr, value);

	/* Enable LPT */
	addr = mmLOW_POWER_TILING_CONTROL;
	value = dm_read_reg(compressor->ctx, addr);
	set_reg_field_value(
		value,
		1,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ENABLE);
	dm_write_reg(compressor->ctx, addr, value);
}

void dce80_compressor_program_lpt_control(
	struct compressor *compressor,
	struct compr_addr_and_pitch_params *params)
{
	struct dce80_compressor *cp80 = TO_DCE80_COMPRESSOR(compressor);
	uint32_t rows_per_channel;
	uint32_t lpt_alignment;
	uint32_t source_view_width;
	uint32_t source_view_height;
	uint32_t lpt_control = 0;

	if (!compressor->options.bits.LPT_SUPPORT)
		return;

	lpt_control = dm_read_reg(compressor->ctx,
		mmLOW_POWER_TILING_CONTROL);

	/* POSSIBLE VALUES for Low Power Tiling Mode:
	 * 00 - Use channel 0
	 * 01 - Use Channel 0 and 1
	 * 02 - Use Channel 0,1,2,3
	 * 03 - reserved */
	switch (compressor->lpt_channels_num) {
	/* case 2:
	 * Use Channel 0 & 1 / Not used for DCE 11 */
	case 1:
		/*Use Channel 0 for LPT for DCE 11 */
		set_reg_field_value(
			lpt_control,
			0,
			LOW_POWER_TILING_CONTROL,
			LOW_POWER_TILING_MODE);
		break;
	default:
		dm_logger_write(
			compressor->ctx->logger, LOG_WARNING,
			"%s: Invalid selected DRAM channels for LPT!!!",
			__func__);
		break;
	}

	lpt_control = lpt_memory_control_config(cp80, lpt_control);

	/* Program LOW_POWER_TILING_ROWS_PER_CHAN field which depends on
	 * FBC compressed surface pitch.
	 * LOW_POWER_TILING_ROWS_PER_CHAN = Roundup ((Surface Height *
	 * Surface Pitch) / (Row Size * Number of Channels *
	 * Number of Banks)). */
	rows_per_channel = 0;
	lpt_alignment = lpt_size_alignment(cp80);
	source_view_width =
		align_to_chunks_number_per_line(
			cp80,
			params->source_view_width);
	source_view_height = (params->source_view_height + 1) & (~0x1);

	if (lpt_alignment != 0) {
		rows_per_channel = source_view_width * source_view_height * 4;
		rows_per_channel =
			(rows_per_channel % lpt_alignment) ?
				(rows_per_channel / lpt_alignment + 1) :
				rows_per_channel / lpt_alignment;
	}

	set_reg_field_value(
		lpt_control,
		rows_per_channel,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ROWS_PER_CHAN);

	dm_write_reg(compressor->ctx,
		mmLOW_POWER_TILING_CONTROL, lpt_control);
}

/*
 * DCE 11 Frame Buffer Compression Implementation
 */

void dce80_compressor_set_fbc_invalidation_triggers(
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
	 * valid for DCE8:
	 *     - bit  0 - display register updated
	 *     - bit 28 - memory write from any client except from MCIF
	 *     - bit 29 - CG static screen signal is inactive
	 * In addition, DCE8.1 also needs to set new DCE8.1 specific events
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
		fbc_trigger |
		FBC_IDLE_FORCE_GRPH_COMP_EN |
		FBC_IDLE_FORCE_SRC_SEL_CHANGE |
		FBC_IDLE_FORCE_MIN_COMPRESSION_CHANGE |
		FBC_IDLE_FORCE_ALPHA_COMP_EN |
		FBC_IDLE_FORCE_ZERO_ALPHA_CHUNK_SKIP_EN |
		FBC_IDLE_FORCE_FORCE_COPY_TO_COMP_BUF,
		FBC_IDLE_FORCE_CLEAR_MASK,
		FBC_IDLE_FORCE_CLEAR_MASK);
	dm_write_reg(compressor->ctx, addr, value);
}

bool dce80_compressor_construct(struct dce80_compressor *compressor,
	struct dc_context *ctx)
{
	struct dc_bios *bp = ctx->dc_bios;
	struct embedded_panel_info panel_info;

	compressor->base.options.bits.FBC_SUPPORT = true;
	compressor->base.options.bits.LPT_SUPPORT = true;
	 /* For DCE 11 always use one DRAM channel for LPT */
	compressor->base.lpt_channels_num = 1;
	compressor->base.options.bits.DUMMY_BACKEND = false;

	/* Check if this system has more than 1 DRAM channel; if only 1 then LPT
	 * should not be supported */
	if (compressor->base.memory_bus_width == 64)
		compressor->base.options.bits.LPT_SUPPORT = false;

	compressor->base.options.bits.CLK_GATING_DISABLED = false;

	compressor->base.ctx = ctx;
	compressor->base.embedded_panel_h_size = 0;
	compressor->base.embedded_panel_v_size = 0;
	compressor->base.memory_bus_width = ctx->asic_id.vram_width;
	compressor->base.allocated_size = 0;
	compressor->base.preferred_requested_size = 0;
	compressor->base.min_compress_ratio = FBC_COMPRESS_RATIO_INVALID;
	compressor->base.options.raw = 0;
	compressor->base.banks_num = 0;
	compressor->base.raw_size = 0;
	compressor->base.channel_interleave_size = 0;
	compressor->base.dram_channels_num = 0;
	compressor->base.lpt_channels_num = 0;
	compressor->base.attached_inst = 0;
	compressor->base.is_enabled = false;

	if (BP_RESULT_OK ==
			bp->funcs->get_embedded_panel_info(bp, &panel_info)) {
		compressor->base.embedded_panel_h_size =
			panel_info.lcd_timing.horizontal_addressable;
		compressor->base.embedded_panel_v_size =
			panel_info.lcd_timing.vertical_addressable;
	}
	return true;
}

struct compressor *dce80_compressor_create(struct dc_context *ctx)
{
	struct dce80_compressor *cp80 =
		dm_alloc(sizeof(struct dce80_compressor));

	if (!cp80)
		return NULL;

	if (dce80_compressor_construct(cp80, ctx))
		return &cp80->base;

	BREAK_TO_DEBUGGER();
	dm_free(cp80);
	return NULL;
}

void dce80_compressor_destroy(struct compressor **compressor)
{
	dm_free(TO_DCE80_COMPRESSOR(*compressor));
	*compressor = NULL;
}
