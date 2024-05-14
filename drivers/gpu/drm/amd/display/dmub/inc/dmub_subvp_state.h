/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef DMUB_SUBVP_STATE_H
#define DMUB_SUBVP_STATE_H

#include "dmub_cmd.h"

#define DMUB_SUBVP_INST0 0
#define DMUB_SUBVP_INST1 1
#define SUBVP_MAX_WATERMARK 0xFFFF

struct dmub_subvp_hubp_state {
	uint32_t CURSOR0_0_CURSOR_POSITION;
	uint32_t CURSOR0_0_CURSOR_HOT_SPOT;
	uint32_t CURSOR0_0_CURSOR_DST_OFFSET;
	uint32_t CURSOR0_0_CURSOR_SURFACE_ADDRESS_HIGH;
	uint32_t CURSOR0_0_CURSOR_SURFACE_ADDRESS;
	uint32_t CURSOR0_0_CURSOR_SIZE;
	uint32_t CURSOR0_0_CURSOR_CONTROL;
	uint32_t HUBPREQ0_CURSOR_SETTINGS;
	uint32_t HUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE_HIGH;
	uint32_t HUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_META_SURFACE_ADDRESS;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_C;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C;
	uint32_t HUBPREQ0_DCSURF_PRIMARY_META_SURFACE_ADDRESS_C;
};

enum subvp_error_code {
	DMUB_SUBVP_INVALID_STATE,
	DMUB_SUBVP_INVALID_TRANSITION,
};

enum subvp_state {
	DMUB_SUBVP_DISABLED,
	DMUB_SUBVP_IDLE,
	DMUB_SUBVP_TRY_ACQUIRE_LOCKS,
	DMUB_SUBVP_WAIT_FOR_LOCKS,
	DMUB_SUBVP_PRECONFIGURE,
	DMUB_SUBVP_PREPARE,
	DMUB_SUBVP_ENABLE,
	DMUB_SUBVP_SWITCHING,
	DMUB_SUBVP_END,
	DMUB_SUBVP_RESTORE,
};

/* Defines information for SUBVP to handle vertical interrupts. */
struct dmub_subvp_vertical_interrupt_event {
	/**
	 * @inst: Hardware instance of vertical interrupt.
	 */
	uint8_t otg_inst;

	/**
	 * @pad: Align structure to 4 byte boundary.
	 */
	uint8_t pad[3];

	enum subvp_state curr_state;
};

struct dmub_subvp_vertical_interrupt_state {
	/**
	 * @events: Event list.
	 */
	struct dmub_subvp_vertical_interrupt_event events[DMUB_MAX_STREAMS];
};

struct dmub_subvp_vline_interrupt_event {

	uint8_t hubp_inst;
	uint8_t pad[3];
};

struct dmub_subvp_vline_interrupt_state {
	struct dmub_subvp_vline_interrupt_event events[DMUB_MAX_PLANES];
};

struct dmub_subvp_interrupt_ctx {
	struct dmub_subvp_vertical_interrupt_state vertical_int;
	struct dmub_subvp_vline_interrupt_state vline_int;
};

struct dmub_subvp_pipe_state {
	uint32_t pix_clk_100hz;
	uint16_t main_vblank_start;
	uint16_t main_vblank_end;
	uint16_t mall_region_lines;
	uint16_t prefetch_lines;
	uint16_t prefetch_to_mall_start_lines;
	uint16_t processing_delay_lines;
	uint8_t main_pipe_index;
	uint8_t phantom_pipe_index;
	uint16_t htotal; // htotal for main / phantom pipe
	uint16_t vtotal;
	uint16_t optc_underflow_count;
	uint16_t hubp_underflow_count;
	uint8_t pad[2];
};

/**
 * struct dmub_subvp_vblank_drr_info - Store DRR state when handling
 * SubVP + VBLANK with DRR multi-display case.
 *
 * The info stored in this struct is only valid if drr_in_use = 1.
 */
struct dmub_subvp_vblank_drr_info {
	uint8_t drr_in_use;
	uint8_t drr_window_size_ms;	// DRR window size -- indicates largest VMIN/VMAX adjustment per frame
	uint16_t min_vtotal_supported;	// Min VTOTAL that supports switching in VBLANK
	uint16_t max_vtotal_supported;	// Max VTOTAL that can still support SubVP static scheduling requirements
	uint16_t prev_vmin;		// Store VMIN value before MCLK switch (used to restore after MCLK end)
	uint16_t prev_vmax;		// Store VMAX value before MCLK switch (used to restore after MCLK end)
	uint8_t use_ramping;		// Use ramping or not
	uint8_t pad[1];
};

struct dmub_subvp_vblank_pipe_info {
	uint32_t pix_clk_100hz;
	uint16_t vblank_start;
	uint16_t vblank_end;
	uint16_t vstartup_start;
	uint16_t vtotal;
	uint16_t htotal;
	uint8_t pipe_index;
	uint8_t pad[1];
	struct dmub_subvp_vblank_drr_info drr_info;	// DRR considered as part of SubVP + VBLANK case
};

enum subvp_switch_type {
	DMUB_SUBVP_ONLY, // Used for SubVP only, and SubVP + VACTIVE
	DMUB_SUBVP_AND_SUBVP, // 2 SubVP displays
	DMUB_SUBVP_AND_VBLANK,
	DMUB_SUBVP_AND_FPO,
};

/* SubVP state. */
struct dmub_subvp_state {
	struct dmub_subvp_pipe_state pipe_state[DMUB_MAX_SUBVP_STREAMS];
	struct dmub_subvp_interrupt_ctx int_ctx;
	struct dmub_subvp_vblank_pipe_info vblank_info;
	enum subvp_state state; // current state
	enum subvp_switch_type switch_type; // enum take up 4 bytes (?)
	uint8_t mclk_pending;
	uint8_t num_subvp_streams;
	uint8_t vertical_int_margin_us;
	uint8_t pstate_allow_width_us;
	uint32_t subvp_mclk_switch_count;
	uint32_t subvp_wait_lock_count;
	uint32_t driver_wait_lock_count;
	uint32_t subvp_vblank_frame_count;
	uint16_t watermark_a_cache;
	uint8_t pad[2];
};

#endif /* _DMUB_SUBVP_STATE_H_ */
