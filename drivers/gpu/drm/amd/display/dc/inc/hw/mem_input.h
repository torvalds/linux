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
#ifndef __DAL_MEM_INPUT_H__
#define __DAL_MEM_INPUT_H__

#include "dc.h"
#include "include/grph_object_id.h"

#include "dml/display_mode_structs.h"

struct dchub_init_data;
struct cstate_pstate_watermarks_st {
	uint32_t cstate_exit_ns;
	uint32_t cstate_enter_plus_exit_ns;
	uint32_t pstate_change_ns;
};

struct dcn_watermarks {
	uint32_t pte_meta_urgent_ns;
	uint32_t urgent_ns;
	struct cstate_pstate_watermarks_st cstate_pstate;
};

struct dcn_watermark_set {
	struct dcn_watermarks a;
	struct dcn_watermarks b;
	struct dcn_watermarks c;
	struct dcn_watermarks d;
};

struct dce_watermarks {
	int a_mark;
	int b_mark;
	int c_mark;
	int d_mark;
};

struct stutter_modes {
	bool enhanced;
	bool quad_dmif_buffer;
	bool watermark_nb_pstate;
};

struct mem_input {
	struct mem_input_funcs *funcs;
	struct dc_context *ctx;
	struct dc_plane_address request_address;
	struct dc_plane_address current_address;
	int inst;
	struct stutter_modes stutter_mode;
};

struct vm_system_aperture_param {
	PHYSICAL_ADDRESS_LOC sys_default;
	PHYSICAL_ADDRESS_LOC sys_low;
	PHYSICAL_ADDRESS_LOC sys_high;
};

struct vm_context0_param {
	PHYSICAL_ADDRESS_LOC pte_base;
	PHYSICAL_ADDRESS_LOC pte_start;
	PHYSICAL_ADDRESS_LOC pte_end;
	PHYSICAL_ADDRESS_LOC fault_default;
};

struct mem_input_funcs {
	void (*mem_input_setup)(
			struct mem_input *mem_input,
			struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
			struct _vcs_dpi_display_ttu_regs_st *ttu_regs,
			struct _vcs_dpi_display_rq_regs_st *rq_regs,
			struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest);

	void (*dcc_control)(struct mem_input *mem_input, bool enable,
			bool independent_64b_blks);
	void (*mem_program_viewport)(
			struct mem_input *mem_input,
			const struct rect *viewport,
			const struct rect *viewport_c);

	void (*mem_input_program_display_marks)(
		struct mem_input *mem_input,
		struct dce_watermarks nbp,
		struct dce_watermarks stutter,
		struct dce_watermarks urgent,
		uint32_t total_dest_line_time_ns);

	void (*mem_input_program_chroma_display_marks)(
			struct mem_input *mem_input,
			struct dce_watermarks nbp,
			struct dce_watermarks stutter,
			struct dce_watermarks urgent,
			uint32_t total_dest_line_time_ns);

	void (*allocate_mem_input)(
		struct mem_input *mem_input,
		uint32_t h_total,/* for current target */
		uint32_t v_total,/* for current target */
		uint32_t pix_clk_khz,/* for current target */
		uint32_t total_streams_num);

	void (*free_mem_input)(
		struct mem_input *mem_input,
		uint32_t paths_num);

	bool (*mem_input_program_surface_flip_and_addr)(
		struct mem_input *mem_input,
		const struct dc_plane_address *address,
		bool flip_immediate);

	void (*mem_input_program_pte_vm)(
		struct mem_input *mem_input,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation);

	void (*mem_input_set_vm_system_aperture_settings)(
			struct mem_input *mem_input,
			struct vm_system_aperture_param *apt);

	void (*mem_input_set_vm_context0_settings)(
			struct mem_input *mem_input,
			const struct vm_context0_param *vm0);

	void (*mem_input_program_surface_config)(
		struct mem_input *mem_input,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		union plane_size *plane_size,
		enum dc_rotation_angle rotation,
		struct dc_plane_dcc_param *dcc,
		bool horizontal_mirror);

	bool (*mem_input_is_flip_pending)(struct mem_input *mem_input);

	void (*mem_input_update_dchub)(struct mem_input *mem_input,
				struct dchub_init_data *dh_data);

	void (*set_blank)(struct mem_input *mi, bool blank);
	void (*set_hubp_blank_en)(struct mem_input *mi, bool blank);

	void (*set_cursor_attributes)(
			struct mem_input *mem_input,
			const struct dc_cursor_attributes *attr);

	void (*set_cursor_position)(
			struct mem_input *mem_input,
			const struct dc_cursor_position *pos,
			const struct dc_cursor_mi_param *param);

};

#endif
