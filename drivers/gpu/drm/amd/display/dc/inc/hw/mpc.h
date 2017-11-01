/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_MPCC_H__
#define __DC_MPCC_H__

#include "dc_hw_types.h"
#include "opp.h"

enum mpc_output_csc_mode {
	MPC_OUTPUT_CSC_DISABLE = 0,
	MPC_OUTPUT_CSC_COEF_A,
	MPC_OUTPUT_CSC_COEF_B
};

struct mpcc_cfg {
	int dpp_id;
	int opp_id;
	struct mpc_tree_cfg *tree_cfg;
	unsigned int z_index;

	struct tg_color black_color;
	bool per_pixel_alpha;
	bool pre_multiplied_alpha;
};

struct mpc {
	const struct mpc_funcs *funcs;
	struct dc_context *ctx;
};

struct mpc_funcs {
	int (*add)(struct mpc *mpc, struct mpcc_cfg *cfg);

	void (*remove)(struct mpc *mpc,
			struct mpc_tree_cfg *tree_cfg,
			int opp_id,
			int mpcc_inst);

	void (*wait_for_idle)(struct mpc *mpc, int id);

	void (*update_blend_mode)(struct mpc *mpc, struct mpcc_cfg *cfg);

	int (*get_opp_id)(struct mpc *mpc, int mpcc_id);

	void (*set_output_csc)(struct mpc *mpc,
			int opp_id,
			const struct out_csc_color_matrix *tbl_entry,
			enum mpc_output_csc_mode ocsc_mode);

	void (*set_ocsc_default)(struct mpc *mpc,
			int opp_id,
			enum dc_color_space color_space,
			enum mpc_output_csc_mode ocsc_mode);

};

#endif
