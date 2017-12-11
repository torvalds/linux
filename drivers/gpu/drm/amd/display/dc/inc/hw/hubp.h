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

#ifndef __DAL_HUBP_H__
#define __DAL_HUBP_H__

#include "mem_input.h"

struct hubp {
	struct hubp_funcs *funcs;
	struct dc_context *ctx;
	struct dc_plane_address request_address;
	struct dc_plane_address current_address;
	int inst;
	int opp_id;
	int mpcc_id;
	struct dc_cursor_attributes curs_attr;
};


struct hubp_funcs {
	void (*hubp_setup)(
			struct hubp *hubp,
			struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
			struct _vcs_dpi_display_ttu_regs_st *ttu_regs,
			struct _vcs_dpi_display_rq_regs_st *rq_regs,
			struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest);

	void (*dcc_control)(struct hubp *hubp, bool enable,
			bool independent_64b_blks);
	void (*mem_program_viewport)(
			struct hubp *hubp,
			const struct rect *viewport,
			const struct rect *viewport_c);

	bool (*hubp_program_surface_flip_and_addr)(
		struct hubp *hubp,
		const struct dc_plane_address *address,
		bool flip_immediate);

	void (*hubp_program_pte_vm)(
		struct hubp *hubp,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation);

	void (*hubp_set_vm_system_aperture_settings)(
			struct hubp *hubp,
			struct vm_system_aperture_param *apt);

	void (*hubp_set_vm_context0_settings)(
			struct hubp *hubp,
			const struct vm_context0_param *vm0);

	void (*hubp_program_surface_config)(
		struct hubp *hubp,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		union plane_size *plane_size,
		enum dc_rotation_angle rotation,
		struct dc_plane_dcc_param *dcc,
		bool horizontal_mirror);

	bool (*hubp_is_flip_pending)(struct hubp *hubp);

	void (*hubp_update_dchub)(struct hubp *hubp,
				struct dchub_init_data *dh_data);

	void (*set_blank)(struct hubp *hubp, bool blank);
	void (*set_hubp_blank_en)(struct hubp *hubp, bool blank);

	void (*set_cursor_attributes)(
			struct hubp *hubp,
			const struct dc_cursor_attributes *attr);

	void (*set_cursor_position)(
			struct hubp *hubp,
			const struct dc_cursor_position *pos,
			const struct dc_cursor_mi_param *param);

};

#endif
