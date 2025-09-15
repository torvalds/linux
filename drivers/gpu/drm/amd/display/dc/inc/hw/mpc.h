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

enum mpcc_movable_cm_location {
	MPCC_MOVABLE_CM_LOCATION_BEFORE,
	MPCC_MOVABLE_CM_LOCATION_AFTER,
};

enum MCM_LUT_XABLE {
	MCM_LUT_DISABLE,
	MCM_LUT_DISABLED = MCM_LUT_DISABLE,
	MCM_LUT_ENABLE,
	MCM_LUT_ENABLED = MCM_LUT_ENABLE,
};

enum MCM_LUT_ID {
	MCM_LUT_3DLUT,
	MCM_LUT_1DLUT,
	MCM_LUT_SHAPER
};

struct mpc_fl_3dlut_config {
	bool enabled;
	uint16_t width;
	bool select_lut_bank_a;
	uint16_t bit_depth;
	int hubp_index;
	uint16_t bias;
	uint16_t scale;
};

union mcm_lut_params {
	const struct pwl_params *pwl;
	const struct tetrahedral_params *lut3d;
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
	enum mpcc_gamut_remap_id mpcc_gamut_remap_block_id;
};

struct mpc_rmcm_regs {
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
	struct mpc_rmcm_regs rmcm_regs;
};

/**
 * struct mpc_funcs - funcs
 */
struct mpc_funcs {
	/**
	* @read_mpcc_state:
	*
	* Read register content from given MPCC physical instance.
	*
	* Parameters:
	*
	* - [in/out] mpc - MPC context
	* - [in] mpcc_instance - MPC context instance
	* - [in] mpcc_state - MPC context state
	*
	* Return:
	*
	* void
	*/
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

	/**
	* @mpc_init_single_inst:
	*
	* Initialize given MPCC physical instance.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] mpcc_id - The MPCC physical instance to be initialized.
	*/
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

	/**
	* @get_mpcc_for_dpp_from_secondary:
	*
	* Find, if it exists, a MPCC from a given 'secondary' MPC tree that
	* is associated with specified plane.
	*
	* Parameters:
	* - [in/out] tree - MPC tree structure to search for plane.
	* - [in] dpp_id - DPP to be searched.
	*
	* Return:
	*
	* struct mpcc* - pointer to plane or NULL if no plane found.
	*/
	struct mpcc* (*get_mpcc_for_dpp_from_secondary)(
			struct mpc_tree *tree,
			int dpp_id);

	/**
	* @get_mpcc_for_dpp:
	*
	* Find, if it exists, a MPCC from a given MPC tree that
	* is associated with specified plane.
	*
	* Parameters:
	* - [in/out] tree - MPC tree structure to search for plane.
	* - [in] dpp_id - DPP to be searched.
	*
	* Return:
	*
	* struct mpcc* - pointer to plane or NULL if no plane found.
	*/
	struct mpcc* (*get_mpcc_for_dpp)(
			struct mpc_tree *tree,
			int dpp_id);

	/**
	* @wait_for_idle:
	*
	* Wait for a MPCC in MPC context to enter idle state.
	*
	* Parameters:
	* - [in/out] mpc - MPC Context.
	* - [in] id - MPCC to wait for idle state.
	*
	* Return:
	*
	* void
	*/
	void (*wait_for_idle)(struct mpc *mpc, int id);

	/**
	* @assert_mpcc_idle_before_connect:
	*
	* Assert if MPCC in MPC context is in idle state.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] id - MPCC to assert idle state.
	*
	* Return:
	*
	* void
	*/
	void (*assert_mpcc_idle_before_connect)(struct mpc *mpc, int mpcc_id);

	/**
	* @init_mpcc_list_from_hw:
	*
	* Iterate through the MPCC array from a given MPC context struct
	* and configure each MPCC according to its registers' values.
	*
	* Parameters:
	* - [in/out] mpc - MPC context to initialize MPCC array.
	* - [in/out] tree - MPC tree structure containing MPCC contexts to initialize.
	*
	* Return:
	*
	* void
	*/
	void (*init_mpcc_list_from_hw)(
		struct mpc *mpc,
		struct mpc_tree *tree);

	/**
	* @set_denorm:
	*
	* Set corresponding OPP DENORM_CONTROL register value to specific denorm_mode
	* based on given color depth.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] opp_id - Corresponding OPP to update register.
	* - [in] output_depth - Arbitrary color depth to set denorm_mode.
	*
	* Return:
	*
	* void
	*/
	void (*set_denorm)(struct mpc *mpc,
			int opp_id,
			enum dc_color_depth output_depth);

	/**
	* @set_denorm_clamp:
	*
	* Set denorm clamp values on corresponding OPP DENORM CONTROL register.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] opp_id - Corresponding OPP to update register.
	* - [in] denorm_clamp - Arbitrary denorm clamp to be set.
	*
	* Return:
	*
	* void
	*/
	void (*set_denorm_clamp)(
			struct mpc *mpc,
			int opp_id,
			struct mpc_denorm_clamp denorm_clamp);

	/**
	* @set_output_csc:
	*
	* Set the Output Color Space Conversion matrix
	* with given values and mode.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] opp_id - Corresponding OPP to update register.
	* - [in] regval - Values to set in CSC matrix.
	* - [in] ocsc_mode - Mode to set CSC.
	*
	* Return:
	*
	* void
	*/
	void (*set_output_csc)(struct mpc *mpc,
			int opp_id,
			const uint16_t *regval,
			enum mpc_output_csc_mode ocsc_mode);

	/**
	* @set_ocsc_default:
	*
	* Set the Output Color Space Conversion matrix
	* to default values according to color space.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] opp_id - Corresponding OPP to update register.
	* - [in] color_space - OCSC color space.
	* - [in] ocsc_mode - Mode to set CSC.
	*
	* Return:
	*
	* void
	*
	*/
	void (*set_ocsc_default)(struct mpc *mpc,
			int opp_id,
			enum dc_color_space color_space,
			enum mpc_output_csc_mode ocsc_mode);

	/**
	* @set_output_gamma:
	*
	* Set Output Gamma with given curve parameters.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] mpcc_id - Corresponding MPC to update registers.
	* - [in] params - Parameters.
	*
	* Return:
	*
	* void
	*
	*/
	void (*set_output_gamma)(
			struct mpc *mpc,
			int mpcc_id,
			const struct pwl_params *params);
	/**
	* @power_on_mpc_mem_pwr:
	*
	* Power on/off memory LUT for given MPCC.
	* Powering on enables LUT to be updated.
	* Powering off allows entering low power mode.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] mpcc_id - MPCC to power on.
	* - [in] power_on
	*
	* Return:
	*
	* void
	*/
	void (*power_on_mpc_mem_pwr)(
			struct mpc *mpc,
			int mpcc_id,
			bool power_on);
	/**
	* @set_dwb_mux:
	*
	* Set corresponding Display Writeback mux
	* MPC register field to given MPCC id.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] dwb_id - DWB to be set.
	* - [in] mpcc_id - MPCC id to be stored in DWB mux register.
	*
	* Return:
	*
	* void
	*/
	void (*set_dwb_mux)(
			struct mpc *mpc,
			int dwb_id,
			int mpcc_id);

	/**
	* @disable_dwb_mux:
	*
	* Reset corresponding Display Writeback mux
	* MPC register field.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] dwb_id - DWB to be set.
	*
	* Return:
	*
	* void
	*/
	void (*disable_dwb_mux)(
		struct mpc *mpc,
		int dwb_id);

	/**
	* @is_dwb_idle:
	*
	* Check DWB status on MPC_DWB0_MUX_STATUS register field.
	* Return if it is null.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] dwb_id - DWB to be checked.
	*
	* Return:
	*
	* bool - wheter DWB is idle or not
	*/
	bool (*is_dwb_idle)(
		struct mpc *mpc,
		int dwb_id);

	/**
	* @set_out_rate_control:
	*
	* Set display output rate control.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] opp_id - OPP to be set.
	* - [in] enable
	* - [in] rate_2x_mode
	* - [in] flow_control
	*
	* Return:
	*
	* void
	*/
	void (*set_out_rate_control)(
		struct mpc *mpc,
		int opp_id,
		bool enable,
		bool rate_2x_mode,
		struct mpc_dwb_flow_control *flow_control);

	/**
	* @set_gamut_remap:
	*
	* Set post-blending CTM for given MPCC.
	*
	* Parameters:
	* - [in] mpc - MPC context.
	* - [in] mpcc_id - MPCC to set gamut map.
	* - [in] adjust
	*
	* Return:
	*
	* void
	*/
	void (*set_gamut_remap)(
			struct mpc *mpc,
			int mpcc_id,
			const struct mpc_grph_gamut_adjustment *adjust);

	/**
	* @program_1dlut:
	*
	* Set 1 dimensional Lookup Table.
	*
	* Parameters:
	* - [in/out] mpc - MPC context
	* - [in] params - curve parameters for the LUT configuration
	* - [in] rmu_idx
	*
	* bool - wheter LUT was set (set with given parameters) or not (params is NULL and LUT is disabled).
	*/
	bool (*program_1dlut)(
			struct mpc *mpc,
			const struct pwl_params *params,
			uint32_t rmu_idx);

	/**
	* @program_shaper:
	*
	* Set shaper.
	*
	* Parameters:
	* - [in/out] mpc - MPC context
	* - [in] params - curve parameters to be set
	* - [in] rmu_idx
	*
	* Return:
	*
	* bool - wheter shaper was set (set with given parameters) or not (params is NULL and LUT is disabled).
	*/
	bool (*program_shaper)(
			struct mpc *mpc,
			const struct pwl_params *params,
			uint32_t rmu_idx);

	/**
	* @acquire_rmu:
	*
	* Set given MPCC to be multiplexed to given RMU unit.
	*
	* Parameters:
	* - [in/out] mpc - MPC context
	* - [in] mpcc_id - MPCC
	* - [in] rmu_idx - Given RMU unit to set MPCC to be multiplexed to.
	*
	* Return:
	*
	* unit32_t - rmu_idx if operation was successful, -1 else.
	*/
	uint32_t (*acquire_rmu)(struct mpc *mpc, int mpcc_id, int rmu_idx);

	/**
	* @program_3dlut:
	*
	* Set 3 dimensional Lookup Table.
	*
	* Parameters:
	* - [in/out] mpc - MPC context
	* - [in] params - tetrahedral parameters for the LUT configuration
	* - [in] rmu_idx
	*
	* bool - wheter LUT was set (set with given parameters) or not (params is NULL and LUT is disabled).
	*/
	bool (*program_3dlut)(
			struct mpc *mpc,
			const struct tetrahedral_params *params,
			int rmu_idx);

	/**
	* @release_rmu:
	*
	* For a given MPCC, release the RMU unit it muliplexes to.
	*
	* Parameters:
	* - [in/out] mpc - MPC context
	* - [in] mpcc_id - MPCC
	*
	* Return:
	*
	* int - a valid rmu_idx representing released RMU unit or -1 if there was no RMU unit to release.
	*/
	int (*release_rmu)(struct mpc *mpc, int mpcc_id);

	/**
	* @get_mpc_out_mux:
	*
	* Return MPC out mux.
	*
	* Parameters:
	* - [in] mpc - MPC context.
	* - [in] opp_id - OPP
	*
	* Return:
	*
	* unsigned int - Out Mux
	*/
	unsigned int (*get_mpc_out_mux)(
				struct mpc *mpc,
				int opp_id);

	/**
	* @set_bg_color:
	*
	* Find corresponding bottommost MPCC and
	* set its bg color.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] bg_color - background color to be set.
	* - [in] mpcc_id
	*
	* Return:
	*
	* void
	*/
	void (*set_bg_color)(struct mpc *mpc,
			struct tg_color *bg_color,
			int mpcc_id);

	/**
	* @set_mpc_mem_lp_mode:
	*
	* Set mpc_mem_lp_mode.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	*
	* Return:
	*
	* void
	*/

	void (*set_mpc_mem_lp_mode)(struct mpc *mpc);
	/**
	* @set_movable_cm_location:
	*
	* Set Movable CM Location.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] location
	* - [in] mpcc_id
	*
	* Return:
	*
	* void
	*/

	void (*set_movable_cm_location)(struct mpc *mpc, enum mpcc_movable_cm_location location, int mpcc_id);
	/**
	* @update_3dlut_fast_load_select:
	*
	* Update 3D LUT fast load select.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] mpcc_id
	* - [in] hubp_idx
	*
	* Return:
	*
	* void
	*/

	void (*update_3dlut_fast_load_select)(struct mpc *mpc, int mpcc_id, int hubp_idx);

	/**
	* @populate_lut:
	*
	* Populate LUT with given tetrahedral parameters.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] id
	* - [in] params
	* - [in] lut_bank_a
	* - [in] mpcc_id
	*
	* Return:
	*
	* void
	*/
	void (*populate_lut)(struct mpc *mpc, const enum MCM_LUT_ID id, const union mcm_lut_params params,
			bool lut_bank_a, int mpcc_id);

	/**
	* @program_lut_read_write_control:
	*
	* Program LUT RW control.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] id
	* - [in] lut_bank_a
	* - [in] mpcc_id
	*
	* Return:
	*
	* void
	*/
	void (*program_lut_read_write_control)(struct mpc *mpc, const enum MCM_LUT_ID id, bool lut_bank_a, int mpcc_id);

	/**
	* @program_lut_mode:
	*
	* Program LUT mode.
	*
	* Parameters:
	* - [in/out] mpc - MPC context.
	* - [in] id
	* - [in] xable
	* - [in] lut_bank_a
	* - [in] mpcc_id
	*
	* Return:
	*
	* void
	*/
	void (*program_lut_mode)(struct mpc *mpc, const enum MCM_LUT_ID id, const enum MCM_LUT_XABLE xable,
			bool lut_bank_a, int mpcc_id);

	/**
	 * @mcm:
	 *
	 * MPC MCM new HW sequential programming functions
	 */
	struct {
		void (*program_3dlut_size)(struct mpc *mpc, uint32_t width, int mpcc_id);
		void (*program_bias_scale)(struct mpc *mpc, uint16_t bias, uint16_t scale, int mpcc_id);
		void (*program_bit_depth)(struct mpc *mpc, uint16_t bit_depth, int mpcc_id);
		bool (*is_config_supported)(uint32_t width);
		void (*program_lut_read_write_control)(struct mpc *mpc, const enum MCM_LUT_ID id,
			bool lut_bank_a, bool enabled, int mpcc_id);

		void (*populate_lut)(struct mpc *mpc, const union mcm_lut_params params,
			bool lut_bank_a, int mpcc_id);
	} mcm;

	/**
	 * @rmcm:
	 *
	 * MPC RMCM new HW sequential programming functions
	 */
	struct {
		void (*fl_3dlut_configure)(struct mpc *mpc, struct mpc_fl_3dlut_config *cfg, int mpcc_id);
		void (*enable_3dlut_fl)(struct mpc *mpc, bool enable, int mpcc_id);
		void (*update_3dlut_fast_load_select)(struct mpc *mpc, int mpcc_id, int hubp_idx);
		void (*program_lut_read_write_control)(struct mpc *mpc, const enum MCM_LUT_ID id,
			bool lut_bank_a, bool enabled, int mpcc_id);
		void (*program_lut_mode)(struct mpc *mpc, const enum MCM_LUT_XABLE xable,
			bool lut_bank_a, int mpcc_id);
		void (*program_3dlut_size)(struct mpc *mpc, uint32_t width, int mpcc_id);
		void (*program_bias_scale)(struct mpc *mpc, uint16_t bias, uint16_t scale, int mpcc_id);
		void (*program_bit_depth)(struct mpc *mpc, uint16_t bit_depth, int mpcc_id);
		bool (*is_config_supported)(uint32_t width);

		void (*power_on_shaper_3dlut)(struct mpc *mpc, uint32_t mpcc_id, bool power_on);
		void (*populate_lut)(struct mpc *mpc, const union mcm_lut_params params,
			bool lut_bank_a, int mpcc_id);
	} rmcm;
};

#endif
