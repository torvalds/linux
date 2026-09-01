/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef ALT_PSTATE_SHARED_H
#define ALT_PSTATE_SHARED_H

#include "dmub_cmd.h"

#define END_SWATH_REC 0xFFFF
#define END_SWATH_PRE 0xFFFFF
#define NEXT_FRAME_MASK 0x80000000
#define SWATH_MASK 0x7FFFFFFF
#define MAX_FRAME_COUNT 0xFFFFFF
#define PROGRAM_GO_IMMEDIATE 0xFFFFFFFF
#define MAX_SUBVP_HEIGHT 0xFFF
#define MAX_SUBVP_START_LINE 0xFFFF

struct get_swath_deadlines_params {
	/* inputs */
	struct dmub_fams2_cmd_stream_static_base_state *base;
	struct dmub_fams2_cmd_alternate_stream_static_state *alternate_static_state;
	uint8_t plane_index;
	uint16_t vtotal;
	uint16_t rec_y_start;
	bool chroma_plane;
	/* outputs */
	uint16_t *swath_array; // caller must allocate it's own memory for the output
	uint16_t *array_size;
};

struct calculate_hubp_start_end_lines_params {
	/* inputs */
	struct dmub_fams2_cmd_stream_static_base_state *base;
	struct dmub_fams2_cmd_alternate_stream_static_state *alternate_static_state;
	uint32_t current_otg_line;		// [dst line]
	uint32_t current_frame_count;	// reference frame count
	uint32_t otg_pstate_target;		// [dst line]
	uint32_t target_frame_count;	// target frame count that we expect to assert P-State allow
	uint16_t vtotal;
	uint16_t rec_y_start;
	uint8_t plane_index;
	uint8_t cursor_size;
	bool chroma_plane;

	/* outputs */
	uint16_t svp0_start_line;
	uint16_t svp0_height;
	uint16_t svp0_height_next;
	uint16_t svp1_start_line;
	uint16_t svp1_height;
	uint16_t svp1_height_next;
	uint8_t svp_position;
	uint32_t program_go_line;
	uint32_t program_go_frame_count;
	/* for debug */
	uint16_t svp0_start_dst_line;
	uint16_t svp0_end_dst_line;
	uint16_t svp1_start_dst_line;
	uint16_t svp1_end_dst_line;
};

struct calculate_copy_from_primary_params {
	/* inputs */
	uint32_t target_frame;
	uint32_t flip_pending;
	uint32_t flip_pending_clear_frame;
	/* outputs */
	bool copy_from_primary;
};

struct svp_params {
	uint16_t start_line;
	uint16_t height;
	uint16_t height_next;
};

struct calculate_lsdma_copy_params {
	/* inputs */
	struct dmub_fams2_cmd_stream_static_base_state *base;
	struct dmub_fams2_cmd_alternate_stream_static_state *alternate_static_state;
	uint8_t plane_index;
	struct svp_params svp[2]; // array of 2 for svp0 and svp1
	struct svp_params svp_c[2]; // array of 2 for svp0 and svp1

	/* outputs */
	struct lsdma_outputs out[2]; // array of 2 for svp0 and svp1
	struct lsdma_outputs out_c[2]; // array of 2 for svp0 and svp1
};

void calculate_lsdma_copy(struct calculate_lsdma_copy_params *p);

void calculate_copy_from_primary(struct calculate_copy_from_primary_params *p);

void get_swath_deadlines(struct get_swath_deadlines_params *p);

void calculate_hubp_start_end_lines(struct calculate_hubp_start_end_lines_params *p);

int32_t get_prefetch_start_line_x1000(uint32_t vtotal, uint16_t vblank_end, uint16_t recout_y, uint16_t dst_y_prefetch_x1000, uint8_t prefetch_relative_vblank, uint16_t dst_y_after_scaler);

int32_t get_prefetch_end_line(uint32_t vtotal, uint16_t vblank_end, uint16_t recout_y, uint8_t prefetch_relative_vblank, uint16_t dst_y_after_scaler);

uint16_t get_effective_vblank_start(uint16_t vblank_start, uint16_t vblank_end, uint16_t recout_y, uint16_t recout_height);

bool in_circular_range(uint32_t start, uint32_t end, uint32_t value);

#endif /* ALT_PSTATE_SHARED_H */
