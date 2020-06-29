/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

#ifndef __PIXELGEN_PRIVATE_H_INCLUDED__
#define __PIXELGEN_PRIVATE_H_INCLUDED__
#include "pixelgen_public.h"
#include "PixelGen_SysBlock_defs.h"
#include "device_access.h"	/* ia_css_device_load_uint32 */
#include "assert_support.h" /* assert */

/*****************************************************
 *
 * Native command interface (NCI).
 *
 *****************************************************/
/**
 * @brief Get the pixelgen state.
 * Refer to "pixelgen_public.h" for details.
 */
STORAGE_CLASS_PIXELGEN_C void pixelgen_ctrl_get_state(
    const pixelgen_ID_t ID,
    pixelgen_ctrl_state_t *state)
{
	state->com_enable =
	    pixelgen_ctrl_reg_load(ID, _PXG_COM_ENABLE_REG_IDX);
	state->prbs_rstval0 =
	    pixelgen_ctrl_reg_load(ID, _PXG_PRBS_RSTVAL_REG0_IDX);
	state->prbs_rstval1 =
	    pixelgen_ctrl_reg_load(ID, _PXG_PRBS_RSTVAL_REG1_IDX);
	state->syng_sid =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_SID_REG_IDX);
	state->syng_free_run =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_FREE_RUN_REG_IDX);
	state->syng_pause =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_PAUSE_REG_IDX);
	state->syng_nof_frames =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_NOF_FRAME_REG_IDX);
	state->syng_nof_pixels =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_NOF_PIXEL_REG_IDX);
	state->syng_nof_line =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_NOF_LINE_REG_IDX);
	state->syng_hblank_cyc =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_HBLANK_CYC_REG_IDX);
	state->syng_vblank_cyc =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_VBLANK_CYC_REG_IDX);
	state->syng_stat_hcnt =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_STAT_HCNT_REG_IDX);
	state->syng_stat_vcnt =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_STAT_VCNT_REG_IDX);
	state->syng_stat_fcnt =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_STAT_FCNT_REG_IDX);
	state->syng_stat_done =
	    pixelgen_ctrl_reg_load(ID, _PXG_SYNG_STAT_DONE_REG_IDX);
	state->tpg_mode =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_MODE_REG_IDX);
	state->tpg_hcnt_mask =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_HCNT_MASK_REG_IDX);
	state->tpg_vcnt_mask =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_VCNT_MASK_REG_IDX);
	state->tpg_xycnt_mask =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_XYCNT_MASK_REG_IDX);
	state->tpg_hcnt_delta =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_HCNT_DELTA_REG_IDX);
	state->tpg_vcnt_delta =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_VCNT_DELTA_REG_IDX);
	state->tpg_r1 =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_R1_REG_IDX);
	state->tpg_g1 =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_G1_REG_IDX);
	state->tpg_b1 =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_B1_REG_IDX);
	state->tpg_r2 =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_R2_REG_IDX);
	state->tpg_g2 =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_G2_REG_IDX);
	state->tpg_b2 =
	    pixelgen_ctrl_reg_load(ID, _PXG_TPG_B2_REG_IDX);
}

/**
 * @brief Dump the pixelgen state.
 * Refer to "pixelgen_public.h" for details.
 */
STORAGE_CLASS_PIXELGEN_C void pixelgen_ctrl_dump_state(
    const pixelgen_ID_t ID,
    pixelgen_ctrl_state_t *state)
{
	ia_css_print("Pixel Generator ID %d Enable  0x%x\n", ID, state->com_enable);
	ia_css_print("Pixel Generator ID %d PRBS reset value 0 0x%x\n", ID,
		     state->prbs_rstval0);
	ia_css_print("Pixel Generator ID %d PRBS reset value 1 0x%x\n", ID,
		     state->prbs_rstval1);
	ia_css_print("Pixel Generator ID %d SYNC SID 0x%x\n", ID, state->syng_sid);
	ia_css_print("Pixel Generator ID %d syng free run 0x%x\n", ID,
		     state->syng_free_run);
	ia_css_print("Pixel Generator ID %d syng pause 0x%x\n", ID, state->syng_pause);
	ia_css_print("Pixel Generator ID %d syng no of frames 0x%x\n", ID,
		     state->syng_nof_frames);
	ia_css_print("Pixel Generator ID %d syng no of pixels 0x%x\n", ID,
		     state->syng_nof_pixels);
	ia_css_print("Pixel Generator ID %d syng no of line 0x%x\n", ID,
		     state->syng_nof_line);
	ia_css_print("Pixel Generator ID %d syng hblank cyc  0x%x\n", ID,
		     state->syng_hblank_cyc);
	ia_css_print("Pixel Generator ID %d syng vblank cyc  0x%x\n", ID,
		     state->syng_vblank_cyc);
	ia_css_print("Pixel Generator ID %d syng stat hcnt  0x%x\n", ID,
		     state->syng_stat_hcnt);
	ia_css_print("Pixel Generator ID %d syng stat vcnt  0x%x\n", ID,
		     state->syng_stat_vcnt);
	ia_css_print("Pixel Generator ID %d syng stat fcnt  0x%x\n", ID,
		     state->syng_stat_fcnt);
	ia_css_print("Pixel Generator ID %d syng stat done  0x%x\n", ID,
		     state->syng_stat_done);
	ia_css_print("Pixel Generator ID %d tpg modee  0x%x\n", ID, state->tpg_mode);
	ia_css_print("Pixel Generator ID %d tpg hcnt mask  0x%x\n", ID,
		     state->tpg_hcnt_mask);
	ia_css_print("Pixel Generator ID %d tpg hcnt mask  0x%x\n", ID,
		     state->tpg_hcnt_mask);
	ia_css_print("Pixel Generator ID %d tpg xycnt mask  0x%x\n", ID,
		     state->tpg_xycnt_mask);
	ia_css_print("Pixel Generator ID %d tpg hcnt delta  0x%x\n", ID,
		     state->tpg_hcnt_delta);
	ia_css_print("Pixel Generator ID %d tpg vcnt delta  0x%x\n", ID,
		     state->tpg_vcnt_delta);
	ia_css_print("Pixel Generator ID %d tpg r1 0x%x\n", ID, state->tpg_r1);
	ia_css_print("Pixel Generator ID %d tpg g1 0x%x\n", ID, state->tpg_g1);
	ia_css_print("Pixel Generator ID %d tpg b1 0x%x\n", ID, state->tpg_b1);
	ia_css_print("Pixel Generator ID %d tpg r2 0x%x\n", ID, state->tpg_r2);
	ia_css_print("Pixel Generator ID %d tpg g2 0x%x\n", ID, state->tpg_g2);
	ia_css_print("Pixel Generator ID %d tpg b2 0x%x\n", ID, state->tpg_b2);
}

/* end of NCI */
/*****************************************************
 *
 * Device level interface (DLI).
 *
 *****************************************************/
/**
 * @brief Load the register value.
 * Refer to "pixelgen_public.h" for details.
 */
STORAGE_CLASS_PIXELGEN_C hrt_data pixelgen_ctrl_reg_load(
    const pixelgen_ID_t ID,
    const hrt_address reg)
{
	assert(ID < N_PIXELGEN_ID);
	assert(PIXELGEN_CTRL_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(PIXELGEN_CTRL_BASE[ID] + reg * sizeof(
					     hrt_data));
}

/**
 * @brief Store a value to the register.
 * Refer to "pixelgen_ctrl_public.h" for details.
 */
STORAGE_CLASS_PIXELGEN_C void pixelgen_ctrl_reg_store(
    const pixelgen_ID_t ID,
    const hrt_address reg,
    const hrt_data value)
{
	assert(ID < N_PIXELGEN_ID);
	assert(PIXELGEN_CTRL_BASE[ID] != (hrt_address)-1);

	ia_css_device_store_uint32(PIXELGEN_CTRL_BASE[ID] + reg * sizeof(hrt_data),
				   value);
}

/* end of DLI */
#endif /* __PIXELGEN_PRIVATE_H_INCLUDED__ */
