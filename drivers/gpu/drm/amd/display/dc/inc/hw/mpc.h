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
#include "hw_shared.h"

#define MAX_MPCC 6
#define MAX_OPP 6

#if   defined(CONFIG_DRM_AMD_DC_DCN2_0)
#define MAX_DWB		1
#endif

enum mpc_output_csc_mode {
	MPC_OUTPUT_CSC_DISABLE = 0,
	MPC_OUTPUT_CSC_COEF_A,
	MPC_OUTPUT_CSC_COEF_B
};


enum mpcc_blend_mode {
	MPCC_BLEND_MODE_BYPASS,
	MPCC_BLEND_MODE_TOP_LAYER_PASSTHROUGH,
	MPCC_BLEND_MODE_TOP_LAYER_ONLY,
	MPCC_BLEND_MODE_TOP_BOT_BLENDING
};

enum mpcc_alpha_blend_mode {
	MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA,
	MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN,
	MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA
};

/*
 * MPCC blending configuration
 */
struct mpcc_blnd_cfg {
	struct tg_color black_color;	/* background color */
	enum mpcc_alpha_blend_mode alpha_mode;	/* alpha blend mode */
	bool pre_multiplied_alpha;	/* alpha pre-multiplied mode flag */
	int global_gain;
	int global_alpha;
	bool overlap_only;

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	/* MPCC top/bottom gain settings */
	int bottom_gain_mode;
	int background_color_bpc;
	int top_gain;
	int bottom_inside_gain;
	int bottom_outside_gain;
#endif
};

struct mpcc_sm_cfg {
	bool enable;
	/* 0-single plane,2-row subsampling,4-column subsampling,6-checkboard subsampling */
	int sm_mode;
	/* 0- disable frame alternate, 1- enable frame alternate */
	bool frame_alt;
	/* 0- disable field alternate, 1- enable field alternate */
	bool field_alt;
	/* 0-no force,2-force frame polarity from top,3-force frame polarity from bottom */
	int force_next_frame_porlarity;
	/* 0-no force,2-force field polarity from top,3-force field polarity from bottom */
	int force_next_field_polarity;
};

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
struct mpc_denorm_clamp {
	int clamp_max_r_cr;
	int clamp_min_r_cr;
	int clamp_max_g_y;
	int clamp_min_g_y;
	int clamp_max_b_cb;
	int clamp_min_b_cb;
};
#endif

/*
 * MPCC connection and blending configuration for a single MPCC instance.
 * This struct is used as a node in an MPC tree.
 */
struct mpcc {
	int mpcc_id;			/* MPCC physical instance */
	int dpp_id;			/* DPP input to this MPCC */
	struct mpcc *mpcc_bot;		/* pointer to bottom layer MPCC.  NULL when not connected */
	struct mpcc_blnd_cfg blnd_cfg;	/* The blending configuration for this MPCC */
	struct mpcc_sm_cfg sm_cfg;	/* stereo mix setting for this MPCC */
};

/*
 * MPC tree represents all MPCC connections for a pipe.
 */
struct mpc_tree {
	int opp_id;			/* The OPP instance that owns this MPC tree */
	struct mpcc *opp_list;		/* The top MPCC layer of the MPC tree that outputs to OPP endpoint */
};

struct mpc {
	const struct mpc_funcs *funcs;
	struct dc_context *ctx;

	struct mpcc mpcc_array[MAX_MPCC];
#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	struct pwl_params blender_params;
	bool cm_bypass_mode;
#endif
};

struct mpcc_state {
	uint32_t opp_id;
	uint32_t dpp_id;
	uint32_t bot_mpcc_id;
	uint32_t mode;
	uint32_t alpha_mode;
	uint32_t pre_multiplied_alpha;
	uint32_t overlap_only;
	uint32_t idle;
	uint32_t busy;
};

struct mpc_funcs {
	void (*read_mpcc_state)(
			struct mpc *mpc,
			int mpcc_inst,
			struct mpcc_state *s);

	/*
	 * Insert DPP into MPC tree based on specified blending position.
	 * Only used for planes that are part of blending chain for OPP output
	 *
	 * Parameters:
	 * [in/out] mpc		- MPC context.
	 * [in/out] tree	- MPC tree structure that plane will be added to.
	 * [in]	blnd_cfg	- MPCC blending configuration for the new blending layer.
	 * [in]	sm_cfg		- MPCC stereo mix configuration for the new blending layer.
	 *			  stereo mix must disable for the very bottom layer of the tree config.
	 * [in]	insert_above_mpcc - Insert new plane above this MPCC.  If NULL, insert as bottom plane.
	 * [in]	dpp_id		 - DPP instance for the plane to be added.
	 * [in]	mpcc_id		 - The MPCC physical instance to use for blending.
	 *
	 * Return:  struct mpcc* - MPCC that was added.
	 */
	struct mpcc* (*insert_plane)(
			struct mpc *mpc,
			struct mpc_tree *tree,
			struct mpcc_blnd_cfg *blnd_cfg,
			struct mpcc_sm_cfg *sm_cfg,
			struct mpcc *insert_above_mpcc,
			int dpp_id,
			int mpcc_id);

	/*
	 * Remove a specified MPCC from the MPC tree.
	 *
	 * Parameters:
	 * [in/out] mpc		- MPC context.
	 * [in/out] tree	- MPC tree structure that plane will be removed from.
	 * [in/out] mpcc	- MPCC to be removed from tree.
	 *
	 * Return:  void
	 */
	void (*remove_mpcc)(
			struct mpc *mpc,
			struct mpc_tree *tree,
			struct mpcc *mpcc);

	/*
	 * Reset the MPCC HW status by disconnecting all muxes.
	 *
	 * Parameters:
	 * [in/out] mpc		- MPC context.
	 *
	 * Return:  void
	 */
	void (*mpc_init)(struct mpc *mpc);

	/*
	 * Update the blending configuration for a specified MPCC.
	 *
	 * Parameters:
	 * [in/out] mpc		- MPC context.
	 * [in]     blnd_cfg	- MPCC blending configuration.
	 * [in]     mpcc_id	- The MPCC physical instance.
	 *
	 * Return:  void
	 */
	void (*update_blending)(
		struct mpc *mpc,
		struct mpcc_blnd_cfg *blnd_cfg,
		int mpcc_id);

	struct mpcc* (*get_mpcc_for_dpp)(
			struct mpc_tree *tree,
			int dpp_id);

	void (*wait_for_idle)(struct mpc *mpc, int id);

	void (*assert_mpcc_idle_before_connect)(struct mpc *mpc, int mpcc_id);

	void (*init_mpcc_list_from_hw)(
		struct mpc *mpc,
		struct mpc_tree *tree);

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	void (*set_denorm)(struct mpc *mpc,
			int opp_id,
			enum dc_color_depth output_depth);

	void (*set_denorm_clamp)(
			struct mpc *mpc,
			int opp_id,
			struct mpc_denorm_clamp denorm_clamp);

	void (*set_output_csc)(struct mpc *mpc,
			int opp_id,
			const uint16_t *regval,
			enum mpc_output_csc_mode ocsc_mode);

	void (*set_ocsc_default)(struct mpc *mpc,
			int opp_id,
			enum dc_color_space color_space,
			enum mpc_output_csc_mode ocsc_mode);

	void (*set_output_gamma)(
			struct mpc *mpc,
			int mpcc_id,
			const struct pwl_params *params);
#endif

};

#endif
