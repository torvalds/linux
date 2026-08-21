// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "alternate_pstate_shared_lib.h"
#define CEILING(x, y) (((x + y) - 1)) / (y)
#define FLOOR(x, y) (((x) / (y)) * (y))
#define ROUND_UP(num, N) (((num) + (N) - 1) / (N)) * (N) // rounds up to nearest multiple of N
#define ROUND_DOWN(num, N) ((num) / (N)) * (N)
#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif
#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

/**
 * ***********************************************************************************************************
 * in_circular_range: Helper function to check if a value is within the range of two numbers (circular range)
 *
 * @return: True if value is within the range of [start, end], where start and end can be a circular range
 *          (i.e., start > end)
 * ***********************************************************************************************************
 */
bool in_circular_range(uint32_t start, uint32_t end, uint32_t value)
{
	if (start <= end) {
		return value >= start && value <= end;
	} else {
		// The range wraps around the buffer.
		return value >= start || value <= end;
	}
}

static bool ranges_overlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2)
{
	return in_circular_range(start1, end1, start2) || in_circular_range(start1, end1, end2) ||
		in_circular_range(start2, end2, start1) || in_circular_range(start2, end2, end1);
}

static bool in_circular_range_excl_end(uint32_t start, uint32_t end, uint32_t value)
{
	if (start <= end) {
		return value >= start && value < end;
	} else {
		// The range wraps around the buffer.
		return value >= start || value < end;
	}
}

static uint32_t convert_swaths_to_lines(
	uint16_t swath_height,
	uint32_t num_swaths)
{
	return swath_height * num_swaths;
}

static uint32_t convert_swath_idx_to_line(
	uint16_t swath_height,
	uint32_t swath_index,
	uint16_t viewport_start,
	uint16_t viewport_size,
	uint8_t access_direction)
{
	if (access_direction == 0) // access_direction == 0 means top down or bottom up
		return FLOOR(swath_height * swath_index + viewport_start, swath_height);
	else
		return FLOOR(viewport_start + viewport_size - swath_height * swath_index - 1, swath_height);
}

static uint32_t count_vstartups(uint32_t vstartup, uint32_t current_frame_count, uint32_t current_line_count,
	uint32_t target_frame_count, uint32_t target_line_count)
{
	uint32_t vstartup_count = 0;

	if (current_frame_count == target_frame_count) {
		if (current_line_count < vstartup && target_line_count > vstartup)
			vstartup_count++;
	} else {
		vstartup_count = target_frame_count > current_frame_count ?
			target_frame_count - current_frame_count - 1 : MAX_FRAME_COUNT + target_frame_count - current_frame_count;
		if (current_line_count < vstartup)
			vstartup_count++;
		if (target_line_count >= vstartup)
			vstartup_count++;
	}
	return vstartup_count;
}

/**
 * *******************************************************************************************************************
 * apply_svp1_workaround: Apply workaround required for scenario where svp0_height_next != 0 && svp1_height_next == 0
 *
 * The workaround requires:
 * - svp1_start_line = svp0_start_line_next + svp0_height_next for access_direction == 0
 * - svp1_start_line = svp0_start_line_next - svp0_height_next for access_direction == 1
 * - From DCN IP team: Do not clamp any value from this calculation, just used the calculated value in svp1_start_line as is
 * ******************************************************************************************************************
 */
static inline void apply_svp1_workaround(
	uint32_t svp0_start_sw_idx_next,
	uint32_t svp0_num_swaths_next,
	uint32_t svp1_num_swaths_next,
	uint16_t viewport_start,
	uint16_t viewport_size,
	uint16_t swath_height,
	uint8_t access_direction,
	/* output */
	uint16_t *svp1_start_line)
{
	if (svp0_num_swaths_next != 0 && svp1_num_swaths_next == 0)
		*svp1_start_line = (uint16_t)convert_swath_idx_to_line(swath_height, svp0_start_sw_idx_next + svp0_num_swaths_next, viewport_start, viewport_size, access_direction);
}

/**
 * ***********************************************************************************************************
 * calculate_start_line_and_height_from_swath: Takes output from calculate_hubp_start_end_lines and calculates
 *                                             hardware programming values (SVP0/1_START_LINE, SVP0/1_HEIGHT)
 *
 * This function takes the output from calculate_hubp_start_end_lines and calculates the values for START_LINE
 * and HEIGHT that will be used to program directly into hardware.
 * ***********************************************************************************************************
 */
static void calculate_start_line_and_height_from_swath(
	uint32_t svp0_start_sw_idx_curr,
	uint32_t svp0_num_swaths_curr,
	uint32_t svp0_start_sw_idx_next,
	uint32_t svp0_num_swaths_next,
	uint32_t svp1_start_sw_idx_curr,
	uint32_t svp1_num_swaths_curr,
	uint32_t svp1_start_sw_idx_next,
	uint32_t svp1_num_swaths_next,
	uint16_t rec_start_dst,
	uint16_t svp0_start_dst,
	uint32_t svp0_frame_count,
	uint32_t program_go_line,
	uint32_t program_go_frame_count,
	uint16_t viewport_start,
	uint16_t viewport_size,
	uint8_t access_direction,
	uint16_t vstartup,
	uint16_t swath_height,
	/* output */
	uint16_t *svp0_start_line,
	uint16_t *svp0_height,
	uint16_t *svp0_height_next,
	uint16_t *svp1_start_line,
	uint16_t *svp1_height,
	uint16_t *svp1_height_next)
{
	*svp0_height = (uint16_t)convert_swaths_to_lines(swath_height, svp0_num_swaths_curr);
	*svp0_height_next = (uint16_t)convert_swaths_to_lines(swath_height, svp0_num_swaths_next);
	if (*svp0_height != 0)
		*svp0_start_line = (uint16_t)convert_swath_idx_to_line(swath_height, svp0_start_sw_idx_curr, viewport_start, viewport_size, access_direction);
	else if (*svp0_height_next != 0)
		*svp0_start_line = (uint16_t)convert_swath_idx_to_line(swath_height, svp0_start_sw_idx_next, viewport_start, viewport_size, access_direction);
	else
		*svp0_start_line = 0;

	*svp1_height = (uint16_t)convert_swaths_to_lines(swath_height, svp1_num_swaths_curr);
	*svp1_height_next = (uint16_t)convert_swaths_to_lines(swath_height, svp1_num_swaths_next);
	if (*svp1_height != 0)
		*svp1_start_line = (uint16_t)convert_swath_idx_to_line(swath_height, svp1_start_sw_idx_curr, viewport_start, viewport_size, access_direction);
	else if (*svp1_height_next != 0)
		*svp1_start_line = (uint16_t)convert_swath_idx_to_line(swath_height, svp1_start_sw_idx_next, viewport_start, viewport_size, access_direction);
	else
		*svp1_start_line = 0;

	if (*svp0_height == 0 && *svp0_height_next == 0 && *svp1_height == 0 && *svp1_height_next == 0) {
		if (count_vstartups(vstartup, program_go_frame_count, program_go_line, svp0_frame_count, svp0_start_dst) > 0)
			*svp0_height_next = MAX_SUBVP_HEIGHT; // Special case only needs to assign SVP0
		if (svp0_start_dst > rec_start_dst)
			*svp0_start_line = MAX_SUBVP_START_LINE; // Special case only needs to assign SVP0
	}
	apply_svp1_workaround(
		svp0_start_sw_idx_next,
		svp0_num_swaths_next,
		svp1_num_swaths_next,
		viewport_start,
		viewport_size,
		swath_height,
		access_direction,
		svp1_start_line);
}

/**
 * ***********************************************************************************************************
 * get_swath_deadlines: Function that produces an array of dst lines associated with each swath deadline
 *
 * This function returns an array of dst lines associated with each swath deadline, where the index of the array
 * represents the swath index. The array is populated based on display config parameters.
 *
 * Currently this function calculates the deadlines internally. Once the deadline schedule is calculated by DML
 * then this function will calculate the deadlines based on the input deadline schedule.
 *
 * @output:
 * swath_array - array of dst lines associated with each swath deadline
 * array_size - size of the swath_array, which is equal to the total number of swaths
 * ***********************************************************************************************************
 */
void get_swath_deadlines(struct get_swath_deadlines_params *p)
{
	uint32_t i;
	uint16_t swath_height = p->chroma_plane ? p->alternate_static_state->swath_height_c[p->plane_index] : p->alternate_static_state->swath_height[p->plane_index];
	uint16_t viewport_start = p->chroma_plane ? p->alternate_static_state->viewport_start_c[p->plane_index] : p->alternate_static_state->viewport_start[p->plane_index];
	uint16_t viewport_size = p->chroma_plane ? p->alternate_static_state->viewport_size_c[p->plane_index] : p->alternate_static_state->viewport_size[p->plane_index];
	uint16_t vblank_end = p->base->vblank_end;
	uint16_t prefetch_swaths = p->chroma_plane ? p->alternate_static_state->prefetch_swaths_c[p->plane_index] : p->alternate_static_state->prefetch_swaths[p->plane_index];
	uint16_t total_swaths = p->chroma_plane ? p->alternate_static_state->total_swaths_c[p->plane_index] : p->alternate_static_state->total_swaths[p->plane_index];
	uint32_t prefetch_start_x1000 = (get_prefetch_start_line_x1000(p->vtotal, vblank_end, p->rec_y_start, p->alternate_static_state->dst_y_prefetch_x1000[p->plane_index], p->alternate_static_state->config[p->plane_index].bits.prefetch_relative_vblank, p->alternate_static_state->dst_y_after_scaler[p->plane_index]));
	uint16_t pre_hdl_delta_x1000 = p->chroma_plane ? p->alternate_static_state->pre_hdl_delta_c_x1000[p->plane_index] : p->alternate_static_state->pre_hdl_delta_x1000[p->plane_index];
	uint16_t rec_hdl_delta_x1000 = p->chroma_plane ? p->alternate_static_state->rec_hdl_delta_c_x1000[p->plane_index] : p->alternate_static_state->rec_hdl_delta_x1000[p->plane_index];
	uint32_t pre_first_hdl_x1000 = (prefetch_start_x1000 + p->alternate_static_state->dst_y_per_vm_vblank_x1000[p->plane_index] +
												2 * p->alternate_static_state->dst_y_per_row_vblank_x1000[p->plane_index] +
												pre_hdl_delta_x1000) % (p->vtotal * 1000);
	uint16_t vinit = p->chroma_plane ? p->alternate_static_state->vinit_prefill_c[p->plane_index] : p->alternate_static_state->vinit_prefill[p->plane_index];
	uint16_t vratio_x1000 = p->chroma_plane ? p->alternate_static_state->vratio_c_x1000[p->plane_index] : p->alternate_static_state->vratio_x1000[p->plane_index];
	uint32_t rec_first_hdl_x1000 = p->alternate_static_state->config[p->plane_index].bits.access_direction ?
		(viewport_start + viewport_size - vinit - swath_height - FLOOR(viewport_start + viewport_size - vinit, swath_height)) * 1000 * 1000 / vratio_x1000 + (p->base->vblank_end - p->alternate_static_state->dst_y_after_scaler[p->plane_index] + p->rec_y_start) * 1000 + rec_hdl_delta_x1000 :
		(FLOOR((viewport_start + vinit - 1), swath_height) - viewport_start - vinit) * 1000 * 1000 / vratio_x1000 + (p->base->vblank_end - p->alternate_static_state->dst_y_after_scaler[p->plane_index] + p->rec_y_start) * 1000 + rec_hdl_delta_x1000; // Should this be CEILING?

	for (i = 0; i < total_swaths; i++) {
		if (i < prefetch_swaths)
			p->swath_array[i] = (uint16_t)((pre_first_hdl_x1000 + pre_hdl_delta_x1000 * i) / 1000);
		else
			p->swath_array[i] = (uint16_t)((rec_first_hdl_x1000 + rec_hdl_delta_x1000 * (i - prefetch_swaths)) / 1000);
	}
	*p->array_size = total_swaths;
}

/**
 * ***********************************************************************************************************
 * calculate_swath_deadline_dst: Helper function that calculates hdl dst position given a swath index
 *
 * @return: dst line associated with the swath deadline
 * ***********************************************************************************************************
 */
static uint32_t calculate_swath_deadline_dst(
	uint32_t swath_index,
	uint32_t prefetch_swaths,
	uint32_t pre_first_hdl_x1000,
	uint32_t rec_first_hdl_x1000,
	uint32_t pre_hdl_delta_x1000,
	uint32_t rec_hdl_delta_x1000)
{
	uint32_t dst_line;

	if (swath_index < prefetch_swaths)
		dst_line = (swath_index * pre_hdl_delta_x1000 + pre_first_hdl_x1000) / 1000;
	else
		dst_line = ((swath_index - prefetch_swaths) * rec_hdl_delta_x1000 + rec_first_hdl_x1000) / 1000;

	return dst_line;
}

/**
 * ***********************************************************************************************************
 * is_dst_in_next_frame: Helper function which determines if a given dst line is in the "next" frame
 *
 * This function determines if a given dst line is in the "next" frame. If the target frame count is not
 * equal to the current frame count, the dst line is considered in the next frame no matter which line it
 * is on. If frame counts are equal, dst line is considered next frame if the current line < frame_boundary,
 * and dst_line >= frame_boundary.
 *
 * @input:
 * - curr_otg_line: current otg line
 * - curr_frame_count: current otg frame count
 * - dst_line: otg line that want to determine is part of "next" frame
 * - target_frame_count: target frame count that is used to determine of dst_line is in "next" frame
 * - frame_boundary: line boundary which determines current or next frame
 *
 * @return: True if dst line is considered part of "next" frame, false otherwise
 * ***********************************************************************************************************
 */
static bool is_dst_in_next_frame(
	uint32_t curr_otg_line,
	uint32_t curr_frame_count,
	uint32_t dst_line,
	uint32_t target_frame_count,
	uint16_t frame_boundary)
{
	if (count_vstartups(frame_boundary, curr_frame_count, curr_otg_line, target_frame_count, dst_line) > 0)
		return true;

	return false;
}

/**
 * ***********************************************************************************************************
 * is_end_swath_in_next_frame: Helper function which determines if dst_line associated with an END swath
 *                             is in the "next" frame
 *
 * This function determines if a dst line associated with an END swath is in the "next" frame. END swaths
 * have special handling for determining current vs. next compared to regular swaths.
 *
 * If the dst line is beyond the boundary (prefetch start) of the next frame, it is considered part of "next".
 *
 * @input:
 * - curr_frame_count: current otg frame count
 * - target_frame_count: target frame count that is used to determine of dst_line is in "next" frame
 * - dst_line: otg line that want to determine is part of "next" frame
 * - frame_boundary: line boundary which determines current or next frame
 *
 * @return: True if dst line is considered part of "next" frame, false otherwise
 * ***********************************************************************************************************
 */
static bool is_end_swath_in_next_frame(
	uint32_t target_frame_count,
	uint32_t curr_frame_count,
	uint32_t curr_line,
	uint32_t dst_line,
	uint16_t frame_boundary,
	uint16_t vstartup)
{
	uint32_t vstartup_count = count_vstartups(vstartup, curr_frame_count, curr_line, target_frame_count, dst_line);

	if (vstartup_count == 1 && !in_circular_range(vstartup, frame_boundary, dst_line))
		return true;

	if (vstartup_count == 2)
		return true;

	return false;
}

/**
 * ***********************************************************************************************************
 * is_svp_dst_in_next_frame: Helper function that determines if a swath index (and dst line associated with it)
 *                           is in the current or next frame. The swath index could be a regular or END swath.
 *
 * @return: True if swath and associated dst line is considered part of "next" frame, false otherwise
 * ***********************************************************************************************************
 */
static bool is_svp_dst_in_next_frame(
	uint32_t swath_index,
	uint16_t pre_swath_incl_boundary,
	uint32_t prefetch_swaths,
	uint32_t pre_first_hdl_x1000,
	uint32_t rec_first_hdl_x1000,
	uint32_t pre_hdl_delta_x1000,
	uint32_t rec_hdl_delta_x1000,
	uint32_t curr_otg_line,
	uint32_t curr_frame_count,
	uint32_t dst_line, // not necessarily the target otg p-state position (coulud be vp0 end for example)
	uint32_t target_frame_count, // target frame for p-state
	uint16_t vstartup)
{
	uint32_t dst_deadline;

	dst_deadline = calculate_swath_deadline_dst(
		swath_index,
		prefetch_swaths,
		pre_first_hdl_x1000,
		rec_first_hdl_x1000,
		pre_hdl_delta_x1000,
		rec_hdl_delta_x1000);

	if ((swath_index < END_SWATH_REC && is_dst_in_next_frame(curr_otg_line, curr_frame_count, dst_deadline, target_frame_count, vstartup)) ||
		(swath_index >= END_SWATH_REC && is_end_swath_in_next_frame(target_frame_count, curr_frame_count, curr_otg_line, dst_line, pre_swath_incl_boundary, vstartup))) {
		return true;
	}
	return false;
}

/**
 * ***********************************************************************************************************
 * calculate_swath_idx_from_dst: Helper function that returns a swath index based on a dst line
 *
 * This function takes in a dst line, and calculates the next or previous swath deadline based on the input
 * dst line. The swath index returned is also marked with "current" or "next" (bit 31).
 *
 * @return: Swath index associated with the input dst line
 * ***********************************************************************************************************
 */
static uint32_t calculate_swath_idx_from_dst(
	uint32_t curr_otg_line,
	uint32_t curr_frame_count,
	uint32_t target_frame_count,
	uint16_t vstartup,
	uint32_t dst_line,
	uint16_t pre_swath_incl_boundary, // first pre hdl for svp0 end, prefetch_start for svp1 end
	uint32_t rec_swath_incl_boundary, // first rec hdl for svp0 end, earliest_rec_req for svp1 end
	uint16_t prefetch_swaths,
	uint16_t total_swaths,
	uint32_t pre_first_hdl_dst_x1000,
	uint16_t pre_last_hdl_dst,
	uint32_t earliest_rec_req,
	uint32_t rec_first_hdl_dst_x1000,
	uint16_t rec_last_hdl_dst,
	uint16_t pre_hdl_delta_x1000,
	uint16_t rec_hdl_delta_x1000,
	bool round_up,
	bool start)
{
	(void)earliest_rec_req;
	uint32_t swath_index, new_dst_line;
	uint32_t dst_line_x1000 = dst_line * 1000;

	if (in_circular_range(pre_swath_incl_boundary, pre_last_hdl_dst, dst_line)) {
		if (pre_hdl_delta_x1000 < 1000)
			round_up = false;
		swath_index = round_up ? CEILING((dst_line_x1000 < pre_first_hdl_dst_x1000 || dst_line > pre_last_hdl_dst ? 0 : dst_line_x1000 - pre_first_hdl_dst_x1000), pre_hdl_delta_x1000) :
								(dst_line_x1000 < pre_first_hdl_dst_x1000 || dst_line > pre_last_hdl_dst ? 0 : dst_line_x1000 - pre_first_hdl_dst_x1000) / pre_hdl_delta_x1000;
		if (!start && pre_hdl_delta_x1000 < 1000) {
			new_dst_line = dst_line_x1000 < pre_first_hdl_dst_x1000 || dst_line > pre_last_hdl_dst ? pre_first_hdl_dst_x1000 + 1000 : dst_line_x1000 + 1000;
			swath_index = (new_dst_line - pre_first_hdl_dst_x1000) / pre_hdl_delta_x1000 - 1;
			if (swath_index > (uint16_t)(prefetch_swaths - 1))
				swath_index = prefetch_swaths - 1;
		}
	} else if (in_circular_range(rec_swath_incl_boundary, rec_last_hdl_dst, dst_line) && total_swaths > prefetch_swaths) {
		if (rec_hdl_delta_x1000 < 1000)
			round_up = false;
		swath_index = round_up ? prefetch_swaths + CEILING((dst_line_x1000 < rec_first_hdl_dst_x1000 || dst_line > rec_last_hdl_dst ? 0 : dst_line_x1000 - rec_first_hdl_dst_x1000), rec_hdl_delta_x1000) :
								prefetch_swaths + (dst_line_x1000 < rec_first_hdl_dst_x1000 || dst_line > rec_last_hdl_dst ? 0 : dst_line_x1000 - rec_first_hdl_dst_x1000) / rec_hdl_delta_x1000;
		if (!start && rec_hdl_delta_x1000 < 1000 && swath_index < (uint16_t)(total_swaths - 1)) {
			new_dst_line = dst_line_x1000 < rec_first_hdl_dst_x1000 || dst_line > rec_last_hdl_dst ? rec_first_hdl_dst_x1000 + 1000 : dst_line_x1000 + 1000;
			swath_index = prefetch_swaths + (new_dst_line - rec_first_hdl_dst_x1000) / rec_hdl_delta_x1000 - 1;
			if (swath_index > (uint16_t)(total_swaths - 1))
				swath_index = total_swaths - 1;
		}
	} else {
		if (in_circular_range(rec_last_hdl_dst, pre_swath_incl_boundary, dst_line) && total_swaths > prefetch_swaths)
			swath_index = END_SWATH_REC;
		else
			swath_index = END_SWATH_PRE;
	}
	if (is_svp_dst_in_next_frame(swath_index, pre_swath_incl_boundary, prefetch_swaths, pre_first_hdl_dst_x1000, rec_first_hdl_dst_x1000,
		pre_hdl_delta_x1000, rec_hdl_delta_x1000, curr_otg_line, curr_frame_count, dst_line, target_frame_count, vstartup))
		swath_index |= NEXT_FRAME_MASK;

	return swath_index;
}

/**
 * ***********************************************************************************************************
 * populate_svp_params: Helper function that calculates start_idx and number of swaths for a single svp
 *
 * This function only calculates parameters for ONE of curr or next for a single svp. This means the caller
 * of this function MUST separate the swath indices to be within the span of a single frame (the swath indices
 * cannot span across frame boundaries).
 *
 * @output: svp_start_sw_idx - svp start swath index
 *			svp_num_swaths - number of swaths spanned by the svp
 * ***********************************************************************************************************
 */
static void populate_svp_params(
	uint16_t prefetch_swaths,
	uint16_t total_swaths,
	uint32_t svp_start_swath_idx,
	uint32_t svp_end_swath_idx,
	/* output */
	uint32_t *svp_start_sw_idx,
	uint32_t *svp_num_swaths)
{
	uint32_t start_idx = svp_start_swath_idx & SWATH_MASK;
	uint32_t end_idx = svp_end_swath_idx & SWATH_MASK;

	if (start_idx == END_SWATH_REC && end_idx == END_SWATH_PRE) {
		*svp_start_sw_idx = 0;
		*svp_num_swaths = prefetch_swaths;
	} else if (start_idx == END_SWATH_PRE && end_idx == END_SWATH_REC) {
		*svp_start_sw_idx = prefetch_swaths;
		*svp_num_swaths = total_swaths - prefetch_swaths;
	} else if (start_idx == END_SWATH_PRE) {
		*svp_start_sw_idx = prefetch_swaths;
		*svp_num_swaths = end_idx < END_SWATH_REC ? end_idx - prefetch_swaths + 1 : 0;
	} else if (start_idx == END_SWATH_REC) {
		*svp_start_sw_idx = 0;
		*svp_num_swaths = end_idx < END_SWATH_REC ? end_idx + 1 : 0;
	} else if (end_idx == END_SWATH_PRE) {
		*svp_start_sw_idx = start_idx;
		*svp_num_swaths = start_idx < prefetch_swaths ? prefetch_swaths - start_idx : 0;
	} else if (end_idx == END_SWATH_REC) {
		*svp_start_sw_idx = start_idx;
		*svp_num_swaths = start_idx < total_swaths ? total_swaths - start_idx : 0;
	} else {
		*svp_start_sw_idx = start_idx;
		*svp_num_swaths = end_idx - start_idx + 1;
	}
}

/**
 * ***********************************************************************************************************
 * populate_start_index_num_swaths_helper: Helper function that calculates svp params for current and next
 *
 * This function calculates the parameters used for a single svp (which includes both curr and next params).
 * This function is responsible for splitting up the start and end swath indices between frames when the
 * svp spans a frame boundary, and calling the helper which calculates parameters per curr / next frame.
 *
 * @output: svp_start_sw_idx_curr - svp start swath index for curr
 *			svp_num_swaths_curr - number of swaths spanned by the svp curr
 *			svp_start_sw_idx_next - svp start swath index for next
 *			svp_num_swaths_next - number of swaths spanned by the svp next
 * ***********************************************************************************************************
 */
static void populate_start_index_num_swaths_helper(
	uint32_t svp_start_swath_idx,
	uint32_t svp_end_swath_idx,
	uint16_t prefetch_swaths,
	uint16_t total_swaths,
	/* output */
	uint32_t *svp_start_sw_idx_curr,
	uint32_t *svp_num_swaths_curr,
	uint32_t *svp_start_sw_idx_next,
	uint32_t *svp_num_swaths_next)
{
	uint32_t svp_start_curr_idx = 0, svp_end_curr_idx = 0, svp_start_next_idx = 0, svp_end_next_idx = 0;
	bool start_in_curr = (svp_start_swath_idx & NEXT_FRAME_MASK) == 0;
	bool end_in_curr = (svp_end_swath_idx & NEXT_FRAME_MASK) == 0;

	if (!start_in_curr && end_in_curr) {
		svp_start_curr_idx = END_SWATH_REC;
		svp_end_curr_idx = END_SWATH_REC;
		svp_start_next_idx = END_SWATH_REC;
		svp_end_next_idx = END_SWATH_REC;
	} else if (start_in_curr && !end_in_curr) {
		svp_start_curr_idx = svp_start_swath_idx;
		svp_end_curr_idx = END_SWATH_REC;
		svp_start_next_idx = NEXT_FRAME_MASK;
		svp_end_next_idx = svp_end_swath_idx;
	} else if (start_in_curr && end_in_curr) {
		svp_start_curr_idx = svp_start_swath_idx;
		svp_end_curr_idx = svp_end_swath_idx;
	} else {
		svp_start_next_idx = svp_start_swath_idx;
		svp_end_next_idx = svp_end_swath_idx;
	}
	if (start_in_curr)
		populate_svp_params(prefetch_swaths, total_swaths, svp_start_curr_idx, svp_end_curr_idx, svp_start_sw_idx_curr, svp_num_swaths_curr);
	if (!end_in_curr) {
		populate_svp_params(prefetch_swaths, total_swaths, svp_start_next_idx, svp_end_next_idx, svp_start_sw_idx_next, svp_num_swaths_next);
	}
}

/**
 * ***********************************************************************************************************
 * calculate_start_index_num_swaths: Helper function that calculates svp params for both vp0 and vp1
 *
 * Calculates svp parameters in terms of start swath idx and number of swaths for both vp0 and vp1.
 * ***********************************************************************************************************
 */
static void calculate_start_index_num_swaths(
	uint32_t svp0_start_swath_idx,
	uint32_t svp0_end_swath_idx,
	uint32_t svp1_start_swath_idx,
	uint32_t svp1_end_swath_idx,
	uint16_t prefetch_swaths,
	uint16_t total_swaths,
	/* output */
	uint32_t *svp0_start_sw_idx_curr,
	uint32_t *svp0_num_swaths_curr,
	uint32_t *svp0_start_sw_idx_next,
	uint32_t *svp0_num_swaths_next,
	uint32_t *svp1_start_sw_idx_curr,
	uint32_t *svp1_num_swaths_curr,
	uint32_t *svp1_start_sw_idx_next,
	uint32_t *svp1_num_swaths_next)
{
	*svp0_start_sw_idx_curr = 0;
	*svp0_num_swaths_curr = 0;
	*svp0_start_sw_idx_next = 0;
	*svp0_num_swaths_next = 0;
	*svp1_start_sw_idx_curr = 0;
	*svp1_num_swaths_curr = 0;
	*svp1_start_sw_idx_next = 0;
	*svp1_num_swaths_next = 0;

	populate_start_index_num_swaths_helper(svp0_start_swath_idx, svp0_end_swath_idx, prefetch_swaths, total_swaths,
		svp0_start_sw_idx_curr, svp0_num_swaths_curr, svp0_start_sw_idx_next, svp0_num_swaths_next);
	populate_start_index_num_swaths_helper(svp1_start_swath_idx, svp1_end_swath_idx, prefetch_swaths, total_swaths,
		svp1_start_sw_idx_curr, svp1_num_swaths_curr, svp1_start_sw_idx_next, svp1_num_swaths_next);

	if (*svp0_num_swaths_curr == 0 && *svp0_num_swaths_next != 0)
		*svp0_start_sw_idx_curr = *svp0_start_sw_idx_next;
	if (*svp1_num_swaths_curr == 0 && *svp1_num_swaths_next != 0)
		*svp1_start_sw_idx_curr = *svp1_start_sw_idx_next;
}

/**
 * ***********************************************************************************************************
 * calculate_hubp_start_end_lines: Function that calculates svp parameters given a target p-state line
 *
 * This function calculates the the svp parameters (vp0 and vp1 programming) in terms of swath index and number
 * of swaths given an otg p-state target. This function should be called per plane. There are 5 steps:
 *
 * 1. Calculate svp0_start_dst_line and svp0_end_dst_line.
 * 2. Calculate svp1_start_dst_line and svp1_end_dst_line
 * 3. Calculate svp0_swath_start_idx and svp0_swath_end_idx
 * 4. Calculate svp1_swath_start_idx and svp1_swath_end_idx
 * 5. Convert to start swath index and number of swaths (for height)
 * 6. Convert swath indices to START_LINE and HEIGHT
 *
 * ***********************************************************************************************************
 */
void calculate_hubp_start_end_lines(struct calculate_hubp_start_end_lines_params *p)
{
	uint16_t svp0_start_dst_line, svp0_end_dst_line, svp1_start_dst_line, svp1_end_dst_line;
	uint16_t pre_last_hdl_dst, rec_last_hdl_dst;
	uint32_t earliest_rec_req;
	uint32_t svp0_start_swath_idx, svp0_end_swath_idx, svp1_start_swath_idx, svp1_end_swath_idx;
	uint32_t svp0_end_frame_count, svp1_end_frame_count;
	uint32_t program_vp_otg_line;
	uint32_t program_vp_frame_count;
	uint32_t svp0_start_sw_idx_curr, svp0_num_swaths_curr, svp0_start_sw_idx_next, svp0_num_swaths_next,
				svp1_start_sw_idx_curr, svp1_num_swaths_curr, svp1_start_sw_idx_next, svp1_num_swaths_next;

	uint32_t svp0_dst_lines = p->alternate_static_state->svp0_dst_lines;
	uint32_t svp1_dst_lines = p->alternate_static_state->svp1_dst_lines;
	uint16_t vblank_end = p->base->vblank_end;
	uint16_t vstartup = p->alternate_static_state->vstartup_start > vblank_end ? vblank_end - p->alternate_static_state->vstartup_start + p->vtotal : vblank_end - p->alternate_static_state->vstartup_start;
	uint16_t vready = (vstartup + p->alternate_static_state->vready_offset_lines) % p->vtotal;
	uint16_t req_limit = p->alternate_static_state->svp_req_limit;
	uint16_t swath_height = p->chroma_plane ? p->alternate_static_state->swath_height_c[p->plane_index] : p->alternate_static_state->swath_height[p->plane_index];
	uint16_t viewport_start = p->chroma_plane ? p->alternate_static_state->viewport_start_c[p->plane_index] : p->alternate_static_state->viewport_start[p->plane_index];
	uint16_t viewport_size = p->chroma_plane ? p->alternate_static_state->viewport_size_c[p->plane_index] : p->alternate_static_state->viewport_size[p->plane_index];
	uint16_t prefetch_swaths = p->chroma_plane ? p->alternate_static_state->prefetch_swaths_c[p->plane_index] : p->alternate_static_state->prefetch_swaths[p->plane_index];
	uint16_t total_swaths = p->chroma_plane ? p->alternate_static_state->total_swaths_c[p->plane_index] : p->alternate_static_state->total_swaths[p->plane_index];
	uint32_t prefetch_start_x1000 = (get_prefetch_start_line_x1000(p->vtotal, vblank_end, p->rec_y_start, p->alternate_static_state->dst_y_prefetch_x1000[p->plane_index], p->alternate_static_state->config[p->plane_index].bits.prefetch_relative_vblank, p->alternate_static_state->dst_y_after_scaler[p->plane_index]));
	int32_t prefetch_end_line = get_prefetch_end_line(p->vtotal, vblank_end, p->rec_y_start, p->alternate_static_state->config[p->plane_index].bits.prefetch_relative_vblank, p->alternate_static_state->dst_y_after_scaler[p->plane_index]);
	uint16_t pre_hdl_delta_x1000 = p->chroma_plane ? p->alternate_static_state->pre_hdl_delta_c_x1000[p->plane_index] : p->alternate_static_state->pre_hdl_delta_x1000[p->plane_index];
	uint16_t rec_hdl_delta_x1000 = p->chroma_plane ? p->alternate_static_state->rec_hdl_delta_c_x1000[p->plane_index] : p->alternate_static_state->rec_hdl_delta_x1000[p->plane_index];
	uint32_t pre_first_hdl_x1000 = (prefetch_start_x1000 + p->alternate_static_state->dst_y_per_vm_vblank_x1000[p->plane_index] +
												2 * p->alternate_static_state->dst_y_per_row_vblank_x1000[p->plane_index] +
												pre_hdl_delta_x1000) % (p->vtotal * 1000);
	uint16_t vinit = p->chroma_plane ? p->alternate_static_state->vinit_prefill_c[p->plane_index] : p->alternate_static_state->vinit_prefill[p->plane_index];
	uint16_t vratio_x1000 = p->chroma_plane ? p->alternate_static_state->vratio_c_x1000[p->plane_index] : p->alternate_static_state->vratio_x1000[p->plane_index];
	uint32_t rec_first_hdl_x1000 = p->alternate_static_state->config[p->plane_index].bits.access_direction ?
		(viewport_start + viewport_size - vinit - swath_height - FLOOR(viewport_start + viewport_size - vinit, swath_height)) * 1000 * 1000 / vratio_x1000 + (p->base->vblank_end - p->alternate_static_state->dst_y_after_scaler[p->plane_index] + p->rec_y_start) * 1000 + rec_hdl_delta_x1000 :
		(FLOOR((viewport_start + vinit - 1), swath_height) - viewport_start - vinit) * 1000 * 1000 / vratio_x1000 + (p->base->vblank_end - p->alternate_static_state->dst_y_after_scaler[p->plane_index] + p->rec_y_start) * 1000 + rec_hdl_delta_x1000; // Should this be CEILING?
	uint32_t vstartup_count;
	bool in_vstartup_to_prefetch;

	pre_last_hdl_dst = (uint16_t)((pre_first_hdl_x1000 + pre_hdl_delta_x1000 * (prefetch_swaths - 1)) / 1000);
	rec_last_hdl_dst = (uint16_t)((rec_first_hdl_x1000 + rec_hdl_delta_x1000 * (total_swaths - prefetch_swaths - 1)) / 1000);
	earliest_rec_req = (uint16_t)((int32_t)(rec_first_hdl_x1000 / 1000 - req_limit) < 0 ? rec_first_hdl_x1000 / 1000 - req_limit + p->vtotal : rec_first_hdl_x1000 / 1000 - req_limit);

	if ((earliest_rec_req < prefetch_start_x1000 / 1000) || (req_limit > rec_first_hdl_x1000 / 1000 && prefetch_start_x1000 / 1000 < vblank_end))
		earliest_rec_req = prefetch_start_x1000 / 1000;

	/*
	 * ************************************************************************
	 * STEP 1 - Calculate svp0_start_dst_line and svp0_end_dst_line
	 * ************************************************************************
	 */
	svp0_start_dst_line = (uint16_t)p->otg_pstate_target;
	svp0_end_dst_line = (svp0_start_dst_line + svp0_dst_lines) % p->vtotal;
	svp0_end_frame_count = p->target_frame_count + ((svp0_start_dst_line + svp0_dst_lines) >= p->vtotal);

	/*
	 * ************************************************************************
	 * STEP 2 - Calculate svp1_start_dst_line and svp1_end_dst_line
	 * ************************************************************************
	 */
	svp1_start_dst_line = svp0_end_dst_line;
	svp1_end_dst_line = (svp1_start_dst_line + svp1_dst_lines) % p->vtotal;
	svp1_end_frame_count = svp0_end_frame_count + ((svp1_start_dst_line + svp1_dst_lines) >= p->vtotal);

	/* If the current otg position causes us to need to look 3+ frames ahead, then defer the programming
	 * to the next VREADY that allows for proper scheduling (this could result in using NEXT as opposed to
	 * CURR). This is scenario is possible if the current otg position is just before vstartup, and the
	 * [lead time + svp0 + svp1] time exceeds a frames length.
	 */
	vstartup_count = count_vstartups(vstartup, p->current_frame_count, p->current_otg_line, svp1_end_frame_count, svp1_end_dst_line);
	in_vstartup_to_prefetch = in_circular_range_excl_end(vstartup, prefetch_start_x1000 / 1000, svp1_end_dst_line);
	if ((vstartup_count > 2) || (vstartup_count == 2 && !in_vstartup_to_prefetch)) {
		program_vp_otg_line = vready;
		program_vp_frame_count = svp1_end_frame_count ? svp1_end_frame_count - 1 : MAX_FRAME_COUNT;
		if (vstartup > p->base->vblank_start && svp1_end_dst_line < p->base->vblank_start)
			program_vp_frame_count = program_vp_frame_count ? program_vp_frame_count - 1 : MAX_FRAME_COUNT;
		if (vready < vstartup)
			program_vp_frame_count = program_vp_frame_count == MAX_FRAME_COUNT ? 0 : program_vp_frame_count + 1;
		if (vstartup_count > 2 && in_vstartup_to_prefetch)
			program_vp_frame_count = program_vp_frame_count ? program_vp_frame_count - 1 : MAX_FRAME_COUNT;
	} else {
		/* There is a slim possibility that by the time the snapshot is taken in macroscheduling, we are within
		 * the "keepout" region of programming GO (being too close to vstartup). Special handling will not be
		 * added for this corner case here, since it should be handled at the state machine layer. Enough margin
		 * should be added such that if the current OTG position is very close to the keepout region at the state
		 * machine layer (but not in the keepout region), the programming will still be valid if the OTG gets into
		 * the keepout region by the time the programming is executed.
		 */
		program_vp_otg_line = p->current_otg_line;
		program_vp_frame_count = p->current_frame_count;
	}

	/*
	 * ************************************************************************
	 * STEP 3 - Calculate svp0_swath_start_idx and svp0_swath_end_idx
	 * ************************************************************************
	 */
	svp0_start_swath_idx = calculate_swath_idx_from_dst(
		program_vp_otg_line,
		program_vp_frame_count,
		p->target_frame_count,
		vstartup,
		svp0_start_dst_line,
		(uint16_t)(pre_first_hdl_x1000 / 1000), // must match with input param for svp0_end_swath_idx call to ensure end swath curr vs. next flag is calculated correctly
		earliest_rec_req,
		prefetch_swaths,
		total_swaths,
		pre_first_hdl_x1000,
		pre_last_hdl_dst,
		earliest_rec_req,
		rec_first_hdl_x1000,
		rec_last_hdl_dst,
		pre_hdl_delta_x1000,
		rec_hdl_delta_x1000,
		true,
		true);

	svp0_end_swath_idx = calculate_swath_idx_from_dst(
		program_vp_otg_line,
		program_vp_frame_count,
		svp0_end_frame_count,
		vstartup,
		svp0_end_dst_line,
		(uint16_t)(pre_first_hdl_x1000 / 1000),
		rec_first_hdl_x1000 / 1000,
		prefetch_swaths,
		total_swaths,
		pre_first_hdl_x1000,
		pre_last_hdl_dst,
		earliest_rec_req,
		rec_first_hdl_x1000,
		rec_last_hdl_dst,
		pre_hdl_delta_x1000,
		rec_hdl_delta_x1000,
		false,
		false);
	/*
	 * ************************************************************************
	 * STEP 4 - Calculate svp1_swath_start_idx and svp1_swath_end_idx
	 * ************************************************************************
	 */
	if ((svp0_end_swath_idx & SWATH_MASK) == END_SWATH_REC) {
		/* Special case where svp0_end_swath_idx is END_SWATH_REC but also in next frame.
		 * In this case svp1_start_swath_idx absolutely cannot be 0 because that would imply
		 * it is swath index 0 of the NEXT NEXT frame. */
		if ((svp0_end_swath_idx & NEXT_FRAME_MASK) != 0)
			svp1_start_swath_idx = svp0_end_swath_idx;
		else
			svp1_start_swath_idx = NEXT_FRAME_MASK;
	} else if ((svp0_end_swath_idx & SWATH_MASK) == END_SWATH_PRE) {
		svp1_start_swath_idx = prefetch_swaths | (NEXT_FRAME_MASK & svp0_end_swath_idx);
	} else {
		svp1_start_swath_idx = (svp0_end_swath_idx & SWATH_MASK) + 1;
		if (svp1_start_swath_idx >= total_swaths)
			svp1_start_swath_idx = END_SWATH_REC;
		svp1_start_swath_idx |= (NEXT_FRAME_MASK & svp0_end_swath_idx);
	}

	svp1_end_swath_idx = calculate_swath_idx_from_dst(
		program_vp_otg_line,
		program_vp_frame_count,
		svp1_end_frame_count,
		vstartup,
		svp1_end_dst_line,
		(uint16_t)(prefetch_start_x1000 / 1000),
		earliest_rec_req,
		prefetch_swaths,
		total_swaths,
		pre_first_hdl_x1000,
		pre_last_hdl_dst,
		earliest_rec_req,
		rec_first_hdl_x1000,
		rec_last_hdl_dst,
		pre_hdl_delta_x1000,
		rec_hdl_delta_x1000,
		true,
		false);

	/*
	 * ************************************************************************
	 * STEP 5 - Convert to start swath index and number of swaths (for height)
	 * ************************************************************************
	 */
	calculate_start_index_num_swaths(svp0_start_swath_idx,
		svp0_end_swath_idx, svp1_start_swath_idx, svp1_end_swath_idx, prefetch_swaths, total_swaths,
		&svp0_start_sw_idx_curr, &svp0_num_swaths_curr, &svp0_start_sw_idx_next, &svp0_num_swaths_next,
		&svp1_start_sw_idx_curr, &svp1_num_swaths_curr, &svp1_start_sw_idx_next, &svp1_num_swaths_next);

	/*
	 * ************************************************************************
	 * STEP 6 - Convert swath indices to START_LINE and HEIGHT
	 * ************************************************************************
	 */
	calculate_start_line_and_height_from_swath(
		svp0_start_sw_idx_curr,
		svp0_num_swaths_curr,
		svp0_start_sw_idx_next,
		svp0_num_swaths_next,
		svp1_start_sw_idx_curr,
		svp1_num_swaths_curr,
		svp1_start_sw_idx_next,
		svp1_num_swaths_next,
		p->base->vblank_end + p->rec_y_start,
		svp0_start_dst_line,
		p->target_frame_count,
		program_vp_otg_line,
		program_vp_frame_count,
		viewport_start,
		viewport_size,
		p->alternate_static_state->config[p->plane_index].bits.access_direction,
		vstartup,
		swath_height,
		&p->svp0_start_line,
		&p->svp0_height,
		&p->svp0_height_next,
		&p->svp1_start_line,
		&p->svp1_height,
		&p->svp1_height_next);

	p->svp0_start_dst_line = svp0_start_dst_line;
	p->svp0_end_dst_line = svp0_end_dst_line;
	p->svp1_start_dst_line = svp1_start_dst_line;
	p->svp1_end_dst_line = svp1_end_dst_line;
	p->svp_position = ranges_overlap(prefetch_start_x1000 / 1000, prefetch_end_line + p->cursor_size, svp0_start_dst_line, svp0_end_dst_line) ||
						ranges_overlap(prefetch_start_x1000 / 1000, prefetch_end_line + p->cursor_size, svp1_start_dst_line, svp1_end_dst_line);
	if (program_vp_otg_line == p->current_otg_line && program_vp_frame_count == p->current_frame_count) {
		p->program_go_line = PROGRAM_GO_IMMEDIATE;
		p->program_go_frame_count = PROGRAM_GO_IMMEDIATE;
	} else {
		p->program_go_line = program_vp_otg_line;
		p->program_go_frame_count = program_vp_frame_count;
	}
}

void calculate_copy_from_primary(struct calculate_copy_from_primary_params *p)
{
	p->copy_from_primary = false;
	if (!p->flip_pending)
		p->copy_from_primary = false;
	else {
		if (((p->target_frame - p->flip_pending_clear_frame) & MAX_FRAME_COUNT) < MAX_FRAME_COUNT / 2)
			p->copy_from_primary = true;
	}
}

static void populate_lsdma_start_end_lines(
	const struct dmub_fams2_cmd_alternate_stream_static_state *state,
	const struct svp_params *svp,
	uint8_t plane_index,
	uint8_t dir,
	bool chroma,
	uint16_t *start_line,
	uint16_t *end_line,
	uint16_t *start_line_next,
	uint16_t *end_line_next)
{
	bool curr = !(svp->height == 0 || svp->height == MAX_SUBVP_HEIGHT);
	bool next = !(svp->height_next == 0 || svp->height_next == MAX_SUBVP_HEIGHT);
	uint8_t swath_height = chroma ? state->swath_height_c[plane_index] : state->swath_height[plane_index];
	uint16_t viewport_start = chroma ? state->viewport_start_c[plane_index] : state->viewport_start[plane_index];
	uint16_t viewport_size = chroma ? state->viewport_size_c[plane_index] : state->viewport_size[plane_index];

	if (dir) {
		if (curr && !next) {
			*start_line = svp->start_line + swath_height - svp->height;
			*end_line = svp->start_line + swath_height;
		} else if (!curr && next) {
			*start_line_next = svp->start_line + swath_height - svp->height_next;
			*end_line_next = svp->start_line + swath_height;
		} else if (curr && next) {
			*end_line = ROUND_UP(viewport_start + viewport_size, swath_height);
			*start_line = *end_line - svp->height_next;
			*start_line_next = svp->start_line + swath_height - svp->height;
			*end_line_next = *start_line_next + svp->height;
		}
	} else {
		if (curr && !next) {
			*start_line = svp->start_line;
			*end_line = *start_line + svp->height;
		} else if (!curr && next) {
			*start_line_next = svp->start_line;
			*end_line_next = *start_line_next + svp->height_next;
		} else if (curr && next) {
			*start_line = svp->start_line;
			*end_line = *start_line + svp->height;
			*start_line_next = FLOOR(viewport_start, swath_height);
			*end_line_next = *start_line_next + svp->height_next;
		}
	}
}

static uint8_t element_size_to_bytes_per_pixel(uint8_t element_size)
{
	uint8_t bytes_per_pixel = 0;

	switch (element_size) { // 0: 8bpp, 1: 16bpp, 2: 32bpp, 3: 64bpp, 4: 128bpp - Values supported by LSDMA Controller
	case 0:
		bytes_per_pixel = 1;
		break;
	case 1:
		bytes_per_pixel = 2;
		break;
	case 2:
		bytes_per_pixel = 4;
		break;
	case 3:
		bytes_per_pixel = 8;
		break;
	case 4:
		bytes_per_pixel = 16;
		break;
	}

	return bytes_per_pixel;
}

static void populate_lsdma(
	const struct dmub_fams2_cmd_alternate_stream_static_state *state,
	const struct dmub_rect16 *pipe_vp,
	const struct svp_params *svp,
	uint8_t plane_index,
	uint8_t pipe_idx,
	bool chroma,
	struct lsdma_outputs *out)
{
	// Common parameters
	uint16_t start_line      = 0;
	uint16_t end_line        = 0;
	uint16_t start_line_next = 0;
	uint16_t end_line_next   = 0;
	uint8_t dir              = state->config[plane_index].bits.access_direction;

	// Tiled specific parameters
	uint16_t block_256b_width;
	uint16_t macro_tile_width;
	uint16_t block_256b_height;
	uint8_t vert;

	// Linear specific parameters
	uint32_t pitch_byte_size;
	uint16_t surface_pitch;
	uint16_t round_bytes;
	uint16_t req_width;
	uint8_t bytes_per_element;
	uint8_t element_size;
	bool odd_req_width;

	populate_lsdma_start_end_lines(state, svp, plane_index, dir, chroma, &start_line, &end_line, &start_line_next, &end_line_next);

	if (state->swizzle_mode[plane_index] != 0) {
		// Tiled mode
		vert              = state->config[plane_index].bits.vertical_access;
		block_256b_width  = chroma ? state->block_256b_width_c[plane_index] : state->block_256b_width[plane_index];
		macro_tile_width  = chroma ? state->macro_tile_width_c[plane_index] : state->macro_tile_width[plane_index];
		block_256b_height = chroma ? state->block_256b_height_c[plane_index] : state->block_256b_height[plane_index];

		out->src_x[0][pipe_idx]  = FLOOR(vert ? start_line : pipe_vp->x, block_256b_width);
		out->src_y[0][pipe_idx]  = FLOOR(vert ? pipe_vp->y : start_line, block_256b_height);
		out->dst_x[0][pipe_idx]  = 0;
		out->dst_y[0][pipe_idx]  = 0;
		out->width[0][pipe_idx]  = ROUND_UP(vert ? end_line : pipe_vp->x + pipe_vp->width, block_256b_width) - out->src_x[0][pipe_idx];
		out->height[0][pipe_idx] = ROUND_UP(vert ? pipe_vp->y + pipe_vp->height : end_line, block_256b_height) - out->src_y[0][pipe_idx];
		out->src_x[1][pipe_idx]  = vert ? FLOOR(start_line_next, block_256b_width) : out->src_x[0][pipe_idx];
		out->src_y[1][pipe_idx]  = vert ? out->src_y[0][pipe_idx] : FLOOR(start_line_next, block_256b_height);
		out->dst_x[1][pipe_idx]  = vert ? out->width[0][pipe_idx] : 0;
		out->dst_y[1][pipe_idx]  = vert ? 0 : out->height[0][pipe_idx];
		out->width[1][pipe_idx]  = vert ? ROUND_UP(end_line_next, block_256b_width) - out->src_x[1][pipe_idx] : out->width[0][pipe_idx];
		out->height[1][pipe_idx] = vert ? out->height[0][pipe_idx] : ROUND_UP(end_line_next, block_256b_height) - out->src_y[1][pipe_idx];
		out->dst_pitch[pipe_idx] = ROUND_UP(out->width[0][pipe_idx] + (vert ? out->width[1][pipe_idx] : 0), macro_tile_width);
	} else {
		// Linear mode
		element_size      = chroma ? state->element_size_c[plane_index] : state->element_size[plane_index];
		surface_pitch     = chroma ? state->surface_pitch_c[plane_index] : state->surface_pitch[plane_index];
		bytes_per_element = element_size_to_bytes_per_pixel(element_size);
		odd_req_width     = ((surface_pitch * bytes_per_element) % 256) != 0 ? true : false;
		req_width         = (uint16_t)((odd_req_width ? 128 : 256) / bytes_per_element);

		out->src_x[0][pipe_idx]  = FLOOR(pipe_vp->x, req_width);
		out->src_y[0][pipe_idx]  = start_line; // no rounding required
		out->dst_x[0][pipe_idx]  = 0;
		out->dst_y[0][pipe_idx]  = 0;
		out->width[0][pipe_idx]  = ROUND_UP(pipe_vp->x + pipe_vp->width, req_width) - out->src_x[0][pipe_idx];
		out->height[0][pipe_idx] = end_line - out->src_y[0][pipe_idx]; // no rounding required
		out->src_x[1][pipe_idx]  = out->src_x[0][pipe_idx];
		out->src_y[1][pipe_idx]  = start_line_next; // no rounding required
		out->dst_x[1][pipe_idx]  = 0;
		out->dst_y[1][pipe_idx]  = out->height[0][pipe_idx];
		out->width[1][pipe_idx]  = out->width[0][pipe_idx];
		out->height[1][pipe_idx] = end_line_next - out->src_y[1][pipe_idx];

		/* Note: If odd_req_width == true (not divisable by 256), then dst_pitch must be rounded up such that
		the total bytes (pitch * bytes_per_element) is divisible by 128 but not 256.
		Otherwise it must be rounded up such that it is divisible by 256.
		If out.dst_pitch already meets this requirement on assigning out.dst_pitch = out.width[0],
		*/
		pitch_byte_size = out->width[0][pipe_idx] * bytes_per_element;
		round_bytes     = odd_req_width ? 128 : 256;
		pitch_byte_size = ROUND_UP(pitch_byte_size, round_bytes);

		if (odd_req_width && (pitch_byte_size % 256) == 0)
			pitch_byte_size += 128;

		out->dst_pitch[pipe_idx] = (uint16_t)(pitch_byte_size / bytes_per_element);
	}
}

static void populate_copy_params(struct calculate_lsdma_copy_params *p, uint8_t svp_idx, uint8_t pipe_idx)
{
	struct dmub_fams2_cmd_alternate_stream_static_state *state = p->alternate_static_state;
	struct plane_pipe_rect *pipe_vp = &p->alternate_static_state->pipe_viewports[pipe_idx];

	populate_lsdma(state, &pipe_vp->luma, &p->svp[svp_idx], p->plane_index, pipe_idx, false, &p->out[svp_idx]);
	if (state->config[p->plane_index].bits.is_multi_planar) {
		populate_lsdma(state, &pipe_vp->chroma, &p->svp_c[svp_idx], p->plane_index, pipe_idx, true, &p->out_c[svp_idx]);
	}
}

void calculate_lsdma_copy(struct calculate_lsdma_copy_params *p)
{
	uint8_t pipe_mask = p->base->plane_pipe_masks[p->plane_index];
	uint8_t i, j;

	for (i = 0; i < 4; i++) {
		if ((pipe_mask & (1 << i))) {
			for (j = 0; j < 2; j++) {
				populate_copy_params(p, j, i);
			}
		}
	}
}

/**
 * ***********************************************************************************************************
 * get_prefetch_start_line: Helper to calculate prefetch start line
 *
 * To be used by FW to calculate and store the prefetch_start_line so it doesn't need to be computed
 * multiple times.
 *
 * @return: Calculates prefetch start given the input timing params
 * ***********************************************************************************************************
 */
int32_t get_prefetch_start_line_x1000(uint32_t vtotal, uint16_t vblank_end, uint16_t recout_y, uint16_t dst_y_prefetch_x1000, uint8_t prefetch_relative_vblank, uint16_t dst_y_after_scaler)
{
	int32_t prefetch_end_x1000 = get_prefetch_end_line(vtotal, vblank_end, recout_y, prefetch_relative_vblank, dst_y_after_scaler) * 1000;
	int32_t prefetch_start_line = prefetch_end_x1000 - dst_y_prefetch_x1000;

	/* Case where dst_y_prefetch starts in FP1: prefetch start line could change due to DRR */
	if (prefetch_start_line < 0)
		prefetch_start_line = vtotal * 1000 + prefetch_start_line;

	return prefetch_start_line;
}

/**
 * ***********************************************************************************************************
 * get_prefetch_end_line: Helper to calculate prefetch end line
 *
 * To be used by FW to calculate and store the prefetch_end_line so it doesn't need to be computed
 * multiple times.
 *
 * @return: Calculates prefetch end given the input timing params
 * ***********************************************************************************************************
 */
int32_t get_prefetch_end_line(uint32_t vtotal, uint16_t vblank_end, uint16_t recout_y, uint8_t prefetch_relative_vblank, uint16_t dst_y_after_scaler)
{
	if (prefetch_relative_vblank)
		return (vblank_end - dst_y_after_scaler) % vtotal;

	return (vblank_end + recout_y - dst_y_after_scaler) % vtotal;
}

/**
 * ***********************************************************************************************************
 * get_effective_vblank_start: Helper to calculate the effective vblank start
 *
 * To be used by FW to calculate and store the effective vblank start so it doesn't need to be computed
 * multiple times. The effective vblank start is the end of the recout in OTG timing space.
 *
 * @return: Calculates effective vblank start given the input timing params
 * ***********************************************************************************************************
 */
uint16_t get_effective_vblank_start(uint16_t vblank_start, uint16_t vblank_end, uint16_t recout_y, uint16_t recout_height)
{
	return vblank_start - ((vblank_start - vblank_end) - recout_y - recout_height);
}
