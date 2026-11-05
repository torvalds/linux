/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __DC_RMCM_H__
#define __DC_RMCM_H__

#include "dc_types.h"

#define MAX_RMCM 2

/* Detach values for the RMCM binding registers: no MPCC connected (MPC_RMCM_CNTL)
 * and no HUBP driving 3DLUT fast load (MPC_RMCM_3DLUT_FL_SEL).
 */
#define RMCM_MPCC_ID_NONE 0xF
#define RMCM_FL_HUBP_IDX_NONE 0xF

/* MPC_RMCM_CNTL has not been written by the driver yet, so it may still hold
 * whatever the boot state left there.
 */
#define RMCM_MPCC_ID_UNKNOWN (-1)

/* MCM_LUT_ID is defined in mpc.h */
enum MCM_LUT_ID;

union rmcm_lut_params {
	const struct pwl_params *pwl;
	const struct tetrahedral_params *lut3d;
};

struct rmcm_regs {
	uint32_t rmcm_3dlut_mem_pwr_state;
	uint32_t rmcm_3dlut_mem_pwr_force;
	uint32_t rmcm_3dlut_mem_pwr_dis;
	uint32_t rmcm_3dlut_mem_pwr_mode;
	uint32_t rmcm_3dlut_size;
	uint32_t rmcm_3dlut_mode;
	uint32_t rmcm_3dlut_mode_cur;
	uint32_t rmcm_3dlut_read_sel;
	uint32_t rmcm_3dlut_30bit_en;
	uint32_t rmcm_3dlut_wr_en_mask;
	uint32_t rmcm_3dlut_ram_sel;
	uint32_t rmcm_3dlut_out_norm_factor;
	uint32_t rmcm_3dlut_fl_sel;
	uint32_t rmcm_3dlut_out_offset_r;
	uint32_t rmcm_3dlut_out_scale_r;
	uint32_t rmcm_3dlut_fl_done;
	uint32_t rmcm_3dlut_fl_soft_underflow;
	uint32_t rmcm_3dlut_fl_hard_underflow;
	uint32_t rmcm_cntl;
	uint32_t rmcm_shaper_mem_pwr_state;
	uint32_t rmcm_shaper_mem_pwr_force;
	uint32_t rmcm_shaper_mem_pwr_dis;
	uint32_t rmcm_shaper_mem_pwr_mode;
	uint32_t rmcm_shaper_lut_mode;
	uint32_t rmcm_shaper_mode_cur;
	uint32_t rmcm_shaper_lut_write_en_mask;
	uint32_t rmcm_shaper_lut_write_sel;
	uint32_t rmcm_shaper_offset_b;
	uint32_t rmcm_shaper_scale_b;
	uint32_t rmcm_shaper_rama_exp_region_start_b;
	uint32_t rmcm_shaper_rama_exp_region_start_seg_b;
	uint32_t rmcm_shaper_rama_exp_region_end_b;
	uint32_t rmcm_shaper_rama_exp_region_end_base_b;
};

/**
 * struct rmcm_state - RMCM hardware state
 *
 * Captures the current hardware state of a Relocatable MCM instance.
 * Used for debugging, state readback, and hardware validation.
 */
struct rmcm_state {
	/**
	 * @regs: Register state snapshot
	 */
	struct rmcm_regs regs;
};

/**
 * struct rmcm - RMCM hardware instance
 *
 * Represents a single RMCM (Relocatable MCM) hardware instance.
 * DCN 4.2+ has 2 RMCM instances that are shared across streams.
 */
struct rmcm {
	struct dc_context *ctx;
	int inst;
	/* MPCC bound through MPC_RMCM_CNTL; MPC bypasses RMCM entirely when two
	 * instances are bound to the same MPCC.
	 */
	int mpcc_id;
	const struct rmcm_funcs *funcs;
};

/**
 * struct rmcm_funcs - RMCM hardware abstraction function pointers
 *
 * Function pointers for RMCM (Relocatable MCM) hardware operations.
 * These provide the interface for programming the shared cross-stream
 * color manager instances.
 */
struct rmcm_funcs {
	void (*get_lut_mode)(struct rmcm *rmcm,	const enum MCM_LUT_ID id, int rmcm_id, bool *enable, bool *lut_bank_a);
	void (*program_lut_mode)(struct rmcm *rmcm, const enum MCM_LUT_ID id,
		bool enable, bool lut_bank_a, const enum dc_cm_lut_size size,
		uint16_t bias, uint16_t scale, int rmcm_id);
	void (*populate_lut)(struct rmcm *rmcm, const enum MCM_LUT_ID id,
		const union rmcm_lut_params params, bool lut_bank_a, int rmcm_id);
	void (*program_lut_read_write_control)(struct rmcm *rmcm, const enum MCM_LUT_ID id,
		bool lut_bank_a, bool enabled, int rmcm_id);
	void (*update_3dlut_fast_load_select)(struct rmcm *rmcm, int rmcm_id, int hubp_idx);
	void (*get_3dlut_fast_load_status)(struct rmcm *rmcm, int rmcm_id,
		bool *done, bool *soft_underflow, bool *hard_underflow);
	void (*read_rmcm_state)(struct rmcm *rmcm, int rmcm_id, struct rmcm_state *s);
	void (*connect_mpcc)(struct rmcm *rmcm, int rmcm_id, int mpcc_id);
};

#endif // __DC_RMCM_H__
