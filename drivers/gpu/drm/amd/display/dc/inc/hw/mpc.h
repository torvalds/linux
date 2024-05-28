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

/**
 * DOC: overview
 *
 * Multiple Pipe/Plane Combiner (MPC) is a component in the hardware pipeline
 * that performs blending of multiple planes, using global and per-pixel alpha.
 * It also performs post-blending color correction operations according to the
 * hardware capabilities, such as color transformation matrix and gamma 1D and
 * 3D LUT.
 *
 * MPC receives output from all DPP pipes and combines them to multiple outputs
 * supporting "M MPC inputs -> N MPC outputs" flexible composition
 * architecture. It features:
 *
 * - Programmable blending structure to allow software controlled blending and
 *   cascading;
 * - Programmable window location of each DPP in active region of display;
 * - Combining multiple DPP pipes in one active region when a single DPP pipe
 *   cannot process very large surface;
 * - Combining multiple DPP from different SLS with blending;
 * - Stereo formats from single DPP in top-bottom or side-by-side modes;
 * - Stereo formats from 2 DPPs;
 * - Alpha blending of multiple layers from different DPP pipes;
 * - Programmable background color;
 */

#ifndef __DC_MPCC_H__
#define __DC_MPCC_H__

#include "dc_hw_types.h"
#include "hw_shared.h"
#include "transform.h"

#define MAX_MPCC 6
#define MAX_OPP 6

#define MAX_DWB		2

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

/**
 * enum mpcc_alpha_blend_mode - define the alpha blend mode regarding pixel
 * alpha and plane alpha values
 */
enum mpcc_alpha_blend_mode {
	/**
	 * @MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA: per pixel alpha using DPP
	 * alpha value
	 */
	MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA,
	/**
	 * @MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN: per
	 * pixel alpha using DPP alpha value multiplied by a global gain (plane
	 * alpha)
	 */
	MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN,
	/**
	 * @MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA: global alpha value, ignores
	 * pixel alpha and consider only plane alpha
	 */
	MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA
};

/**
 * struct mpcc_blnd_cfg - MPCC blending configuration
 */
struct mpcc_blnd_cfg {
	/**
	 * @black_color: background color.
	 */
	struct tg_color black_color;

	/**
	 * @alpha_mode: alpha blend mode (MPCC_ALPHA_BLND_MODE).
	 */
	enum mpcc_alpha_blend_mode alpha_mode;

	/**
	 * @pre_multiplied_alpha:
	 * Whether pixel color values were pre-multiplied by the alpha channel
	 * (MPCC_ALPHA_MULTIPLIED_MODE).
	 */
	bool pre_multiplied_alpha;

	/**
	 * @global_gain: Used when blend mode considers both pixel alpha and plane.
	 */
	int global_gain;

	/**
	 * @global_alpha: Plane alpha value.
	 */
	int global_alpha;

	/**
	 * @overlap_only: Whether overlapping of different planes is allowed.
	 */
	bool overlap_only;

	/* MPCC top/bottom gain settings */

	/**
	 * @bottom_gain_mode: Blend mode for bottom gain setting.
	 */
	int bottom_gain_mode;

	/**
	 * @background_color_bpc: Background color for bpc.
	 */
	int background_color_bpc;

	/**
	 * @top_gain: Top gain setting.
	 */
	int top_gain;

	/**
	 * @bottom_inside_gain: Blend mode for bottom inside.
	 */
	int bottom_inside_gain;

	/**
	 * @bottom_outside_gain: Blend mode for bottom outside.
	 */
	int bottom_outside_gain;
};

struct mpc_grph_gamut_adjustment {
	struct fixed31_32 temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	enum graphics_gamut_adjust_type gamut_adjust_type;
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

struct mpc_denorm_clamp {
	int clamp_max_r_cr;
	int clamp_min_r_cr;
	int clamp_max_g_y;
	int clamp_min_g_y;
	int clamp_max_b_cb;
	int clamp_min_b_cb;
};

struct mpc_dwb_flow_control {
	int flow_ctrl_mode;
	int flow_ctrl_cnt0;
	int flow_ctrl_cnt1;
};

/**
 * struct mpcc - MPCC connection and blending configuration for a single MPCC instance.
 *
 * This struct is used as a node in an MPC tree.
 */
struct mpcc {
	/**
	 * @mpcc_id: MPCC physical instance.
	 */
	int mpcc_id;

	/**
	 * @dpp_id: DPP input to this MPCC
	 */
	int dpp_id;

	/**
	 * @mpcc_bot: Pointer to bottom layer MPCC. NULL when not connected.
	 */
	struct mpcc *mpcc_bot;

	/**
	 * @blnd_cfg: The blending configuration for this MPCC.
	 */
	struct mpcc_blnd_cfg blnd_cfg;

	/**
	 * @sm_cfg: stereo mix setting for this MPCC
	 */
	struct mpcc_sm_cfg sm_cfg;

	/**
	 * @shared_bottom:
	 *
	 * If MPCC output to both OPP and DWB endpoints, true. Otherwise, false.
	 */
	bool shared_bottom;
};

/**
 * struct mpc_tree - MPC tree represents all MPCC connections for a pipe.
 *
 *
 */
struct mpc_tree {
	/**
	 * @opp_id: The OPP instance that owns this MPC tree.
	 */
	int opp_id;

	/**
	 * @opp_list: the top MPCC layer of the MPC tree that outputs to OPP endpoint
	 */
	struct mpcc *opp_list;
};

struct mpc {
	const struct mpc_funcs *funcs;
	struct dc_context *ctx;

	struct mpcc mpcc_array[MAX_MPCC];
	struct pwl_params blender_params;
	bool cm_bypass_mode;
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
	uint32_t shaper_lut_mode;
	uint32_t lut3d_mode;
	uint32_t lut3d_bit_depth;
	uint32_t lut3d_size;
	uint32_t rgam_mode;
	uint32_t rgam_lut;
	struct mpc_grph_gamut_adjustment gamut_remap;
};

/**
 * struct mpc_funcs - funcs
 */
struct mpc_funcs {
	void (*read_mpcc_state)(
			struct mpc *mpc,
			int mpcc_inst,
			struct mpcc_state *s);

	/**
	 * @insert_plane:
	 *
	 * Insert DPP into MPC tree based on specified blending position.
	 * Only used for planes that are part of blending chain for OPP output
	 *
	 * Parameters:
	 *
	 * - [in/out] mpc  - MPC context.
	 * - [in/out] tree - MPC tree structure that plane will be added to.
	 * - [in] blnd_cfg - MPCC blending configuration for the new blending layer.
	 * - [in] sm_cfg   - MPCC stereo mix configuration for the new blending layer.
	 *                   stereo mix must disable for the very bottom layer of the tree config.
	 * - [in] insert_above_mpcc - Insert new plane above this MPCC.
	 *                          If NULL, insert as bottom plane.
	 * - [in] dpp_id  - DPP instance for the plane to be added.
	 * - [in] mpcc_id - The MPCC physical instance to use for blending.
	 *
	 * Return:
	 *
	 * struct mpcc* - MPCC that was added.
	 */
	struct mpcc* (*insert_plane)(
			struct mpc *mpc,
			struct mpc_tree *tree,
			struct mpcc_blnd_cfg *blnd_cfg,
			struct mpcc_sm_cfg *sm_cfg,
			struct mpcc *insert_above_mpcc,
			int dpp_id,
			int mpcc_id);

	/**
	 * @remove_mpcc:
	 *
	 * Remove a specified MPCC from the MPC tree.
	 *
	 * Parameters:
	 *
	 * - [in/out] mpc   - MPC context.
	 * - [in/out] tree  - MPC tree structure that plane will be removed from.
	 * - [in/out] mpcc  - MPCC to be removed from tree.
	 *
	 * Return:
	 *
	 * void
	 */
	void (*remove_mpcc)(
			struct mpc *mpc,
			struct mpc_tree *tree,
			struct mpcc *mpcc);

	/**
	 * @mpc_init:
	 *
	 * Reset the MPCC HW status by disconnecting all muxes.
	 *
	 * Parameters:
	 *
	 * - [in/out] mpc - MPC context.
	 *
	 * Return:
	 *
	 * void
	 */
	void (*mpc_init)(struct mpc *mpc);
	void (*mpc_init_single_inst)(
			struct mpc *mpc,
			unsigned int mpcc_id);

	/**
	 * @update_blending:
	 *
	 * Update the blending configuration for a specified MPCC.
	 *
	 * Parameters:
	 *
	 * - [in/out] mpc - MPC context.
	 * - [in] blnd_cfg - MPCC blending configuration.
	 * - [in] mpcc_id  - The MPCC physical instance.
	 *
	 * Return:
	 *
	 * void
	 */
	void (*update_blending)(
		struct mpc *mpc,
		struct mpcc_blnd_cfg *blnd_cfg,
		int mpcc_id);

	/**
	 * @cursor_lock:
	 *
	 * Lock cursor updates for the specified OPP. OPP defines the set of
	 * MPCC that are locked together for cursor.
	 *
	 * Parameters:
	 *
	 * - [in] mpc - MPC context.
	 * - [in] opp_id  - The OPP to lock cursor updates on
	 * - [in] lock - lock/unlock the OPP
	 *
	 * Return:
	 *
	 * void
	 */
	void (*cursor_lock)(
			struct mpc *mpc,
			int opp_id,
			bool lock);

	/**
	 * @insert_plane_to_secondary:
	 *
	 * Add DPP into secondary MPC tree based on specified blending
	 * position.  Only used for planes that are part of blending chain for
	 * DWB output
	 *
	 * Parameters:
	 *
	 * - [in/out] mpc  - MPC context.
	 * - [in/out] tree - MPC tree structure that plane will be added to.
	 * - [in] blnd_cfg - MPCC blending configuration for the new blending layer.
	 * - [in] sm_cfg   - MPCC stereo mix configuration for the new blending layer.
	 *	    stereo mix must disable for the very bottom layer of the tree config.
	 * - [in] insert_above_mpcc - Insert new plane above this MPCC.  If
	 *          NULL, insert as bottom plane.
	 * - [in] dpp_id - DPP instance for the plane to be added.
	 * - [in] mpcc_id - The MPCC physical instance to use for blending.
	 *
	 * Return:
	 *
	 * struct mpcc* - MPCC that was added.
	 */
	struct mpcc* (*insert_plane_to_secondary)(
			struct mpc *mpc,
			struct mpc_tree *tree,
			struct mpcc_blnd_cfg *blnd_cfg,
			struct mpcc_sm_cfg *sm_cfg,
			struct mpcc *insert_above_mpcc,
			int dpp_id,
			int mpcc_id);

	/**
	 * @remove_mpcc_from_secondary:
	 *
	 * Remove a specified DPP from the 'secondary' MPC tree.
	 *
	 * Parameters:
	 *
	 * - [in/out] mpc  - MPC context.
	 * - [in/out] tree - MPC tree structure that plane will be removed from.
	 * - [in]     mpcc - MPCC to be removed from tree.
	 *
	 * Return:
	 *
	 * void
	 */
	void (*remove_mpcc_from_secondary)(
			struct mpc *mpc,
			struct mpc_tree *tree,
			struct mpcc *mpcc);

	struct mpcc* (*get_mpcc_for_dpp_from_secondary)(
			struct mpc_tree *tree,
			int dpp_id);

	struct mpcc* (*get_mpcc_for_dpp)(
			struct mpc_tree *tree,
			int dpp_id);

	void (*wait_for_idle)(struct mpc *mpc, int id);

	void (*assert_mpcc_idle_before_connect)(struct mpc *mpc, int mpcc_id);

	void (*init_mpcc_list_from_hw)(
		struct mpc *mpc,
		struct mpc_tree *tree);

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
	void (*power_on_mpc_mem_pwr)(
			struct mpc *mpc,
			int mpcc_id,
			bool power_on);
	void (*set_dwb_mux)(
			struct mpc *mpc,
			int dwb_id,
			int mpcc_id);

	void (*disable_dwb_mux)(
		struct mpc *mpc,
		int dwb_id);

	bool (*is_dwb_idle)(
		struct mpc *mpc,
		int dwb_id);

	void (*set_out_rate_control)(
		struct mpc *mpc,
		int opp_id,
		bool enable,
		bool rate_2x_mode,
		struct mpc_dwb_flow_control *flow_control);

	void (*set_gamut_remap)(
			struct mpc *mpc,
			int mpcc_id,
			const struct mpc_grph_gamut_adjustment *adjust);

	bool (*program_1dlut)(
			struct mpc *mpc,
			const struct pwl_params *params,
			uint32_t rmu_idx);

	bool (*program_shaper)(
			struct mpc *mpc,
			const struct pwl_params *params,
			uint32_t rmu_idx);

	uint32_t (*acquire_rmu)(struct mpc *mpc, int mpcc_id, int rmu_idx);

	bool (*program_3dlut)(
			struct mpc *mpc,
			const struct tetrahedral_params *params,
			int rmu_idx);

	int (*release_rmu)(struct mpc *mpc, int mpcc_id);

	unsigned int (*get_mpc_out_mux)(
			struct mpc *mpc,
			int opp_id);

	void (*set_bg_color)(struct mpc *mpc,
			struct tg_color *bg_color,
			int mpcc_id);
	void (*set_mpc_mem_lp_mode)(struct mpc *mpc);
};

#endif
