/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "assert_support.h"
#include "gp_device.h"

#ifndef __INLINE_GP_DEVICE__
#include "gp_device_private.h"
#endif /* __INLINE_GP_DEVICE__ */

void gp_device_get_state(
	const gp_device_ID_t		ID,
	gp_device_state_t			*state)
{
	assert(ID < N_GP_DEVICE_ID);
	assert(state != NULL);

	state->syncgen_enable = gp_device_reg_load(ID,
		_REG_GP_SYNCGEN_ENABLE_ADDR);
	state->syncgen_free_running = gp_device_reg_load(ID,
		_REG_GP_SYNCGEN_FREE_RUNNING_ADDR);
	state->syncgen_pause = gp_device_reg_load(ID,
		_REG_GP_SYNCGEN_PAUSE_ADDR);
	state->nr_frames = gp_device_reg_load(ID,
		_REG_GP_NR_FRAMES_ADDR);
	state->syngen_nr_pix = gp_device_reg_load(ID,
		_REG_GP_SYNGEN_NR_PIX_ADDR);
	state->syngen_nr_pix = gp_device_reg_load(ID,
		_REG_GP_SYNGEN_NR_PIX_ADDR);
	state->syngen_nr_lines = gp_device_reg_load(ID,
		_REG_GP_SYNGEN_NR_LINES_ADDR);
	state->syngen_hblank_cycles = gp_device_reg_load(ID,
		_REG_GP_SYNGEN_HBLANK_CYCLES_ADDR);
	state->syngen_vblank_cycles = gp_device_reg_load(ID,
		_REG_GP_SYNGEN_VBLANK_CYCLES_ADDR);
	state->isel_sof = gp_device_reg_load(ID,
		_REG_GP_ISEL_SOF_ADDR);
	state->isel_eof = gp_device_reg_load(ID,
		_REG_GP_ISEL_EOF_ADDR);
	state->isel_sol = gp_device_reg_load(ID,
		_REG_GP_ISEL_SOL_ADDR);
	state->isel_eol = gp_device_reg_load(ID,
		_REG_GP_ISEL_EOL_ADDR);
	state->isel_lfsr_enable = gp_device_reg_load(ID,
		_REG_GP_ISEL_LFSR_ENABLE_ADDR);
	state->isel_lfsr_enable_b = gp_device_reg_load(ID,
		_REG_GP_ISEL_LFSR_ENABLE_B_ADDR);
	state->isel_lfsr_reset_value = gp_device_reg_load(ID,
		_REG_GP_ISEL_LFSR_RESET_VALUE_ADDR);
	state->isel_tpg_enable = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_ENABLE_ADDR);
	state->isel_tpg_enable_b = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_ENABLE_B_ADDR);
	state->isel_hor_cnt_mask = gp_device_reg_load(ID,
		_REG_GP_ISEL_HOR_CNT_MASK_ADDR);
	state->isel_ver_cnt_mask = gp_device_reg_load(ID,
		_REG_GP_ISEL_VER_CNT_MASK_ADDR);
	state->isel_xy_cnt_mask = gp_device_reg_load(ID,
		_REG_GP_ISEL_XY_CNT_MASK_ADDR);
	state->isel_hor_cnt_delta = gp_device_reg_load(ID,
		_REG_GP_ISEL_HOR_CNT_DELTA_ADDR);
	state->isel_ver_cnt_delta = gp_device_reg_load(ID,
		_REG_GP_ISEL_VER_CNT_DELTA_ADDR);
	state->isel_tpg_mode = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_MODE_ADDR);
	state->isel_tpg_red1 = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_RED1_ADDR);
	state->isel_tpg_green1 = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_GREEN1_ADDR);
	state->isel_tpg_blue1 = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_BLUE1_ADDR);
	state->isel_tpg_red2 = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_RED2_ADDR);
	state->isel_tpg_green2 = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_GREEN2_ADDR);
	state->isel_tpg_blue2 = gp_device_reg_load(ID,
		_REG_GP_ISEL_TPG_BLUE2_ADDR);
	state->isel_ch_id = gp_device_reg_load(ID,
		_REG_GP_ISEL_CH_ID_ADDR);
	state->isel_fmt_type = gp_device_reg_load(ID,
		_REG_GP_ISEL_FMT_TYPE_ADDR);
	state->isel_data_sel = gp_device_reg_load(ID,
		_REG_GP_ISEL_DATA_SEL_ADDR);
	state->isel_sband_sel = gp_device_reg_load(ID,
		_REG_GP_ISEL_SBAND_SEL_ADDR);
	state->isel_sync_sel = gp_device_reg_load(ID,
		_REG_GP_ISEL_SYNC_SEL_ADDR);
	state->syncgen_hor_cnt = gp_device_reg_load(ID,
		_REG_GP_SYNCGEN_HOR_CNT_ADDR);
	state->syncgen_ver_cnt = gp_device_reg_load(ID,
		_REG_GP_SYNCGEN_VER_CNT_ADDR);
	state->syncgen_frame_cnt = gp_device_reg_load(ID,
		_REG_GP_SYNCGEN_FRAME_CNT_ADDR);
	state->soft_reset = gp_device_reg_load(ID,
		_REG_GP_SOFT_RESET_ADDR);
	return;
}
