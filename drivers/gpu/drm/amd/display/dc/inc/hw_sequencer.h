/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DC_HW_SEQUENCER_H__
#define __DC_HW_SEQUENCER_H__
#include "dc_types.h"
#include "clock_source.h"
#include "inc/hw/timing_generator.h"
#include "inc/hw/opp.h"
#include "inc/hw/link_encoder.h"
#include "core_status.h"

struct pipe_ctx;
struct dc_state;
struct dc_stream_status;
struct dc_writeback_info;
struct dchub_init_data;
struct dc_static_screen_params;
struct resource_pool;
struct dc_phy_addr_space_config;
struct dc_virtual_addr_space_config;
struct dpp;
struct dce_hwseq;
struct link_resource;
struct dc_dmub_cmd;

struct subvp_pipe_control_lock_fast_params {
	struct dc *dc;
	bool lock;
	struct pipe_ctx *pipe_ctx;
};

struct pipe_control_lock_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	bool lock;
};

struct set_flip_control_gsl_params {
	struct pipe_ctx *pipe_ctx;
	bool flip_immediate;
};

struct program_triplebuffer_params {
	const struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	bool enableTripleBuffer;
};

struct update_plane_addr_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_input_transfer_func_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	struct dc_plane_state *plane_state;
};

struct program_gamut_remap_params {
	struct pipe_ctx *pipe_ctx;
};

struct program_manual_trigger_params {
	struct pipe_ctx *pipe_ctx;
};

struct send_dmcub_cmd_params {
	struct dc_context *ctx;
	union dmub_rb_cmd *cmd;
	enum dm_dmub_wait_type wait_type;
};

struct setup_dpp_params {
	struct pipe_ctx *pipe_ctx;
};

struct program_bias_and_scale_params {
	struct pipe_ctx *pipe_ctx;
};

struct set_output_transfer_func_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	const struct dc_stream_state *stream;
};

struct update_visual_confirm_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	int mpcc_id;
};

struct power_on_mpc_mem_pwr_params {
	struct mpc *mpc;
	int mpcc_id;
	bool power_on;
};

struct set_output_csc_params {
	struct mpc *mpc;
	int opp_id;
	const uint16_t *regval;
	enum mpc_output_csc_mode ocsc_mode;
};

struct set_ocsc_default_params {
	struct mpc *mpc;
	int opp_id;
	enum dc_color_space color_space;
	enum mpc_output_csc_mode ocsc_mode;
};

union block_sequence_params {
	struct update_plane_addr_params update_plane_addr_params;
	struct subvp_pipe_control_lock_fast_params subvp_pipe_control_lock_fast_params;
	struct pipe_control_lock_params pipe_control_lock_params;
	struct set_flip_control_gsl_params set_flip_control_gsl_params;
	struct program_triplebuffer_params program_triplebuffer_params;
	struct set_input_transfer_func_params set_input_transfer_func_params;
	struct program_gamut_remap_params program_gamut_remap_params;
	struct program_manual_trigger_params program_manual_trigger_params;
	struct send_dmcub_cmd_params send_dmcub_cmd_params;
	struct setup_dpp_params setup_dpp_params;
	struct program_bias_and_scale_params program_bias_and_scale_params;
	struct set_output_transfer_func_params set_output_transfer_func_params;
	struct update_visual_confirm_params update_visual_confirm_params;
	struct power_on_mpc_mem_pwr_params power_on_mpc_mem_pwr_params;
	struct set_output_csc_params set_output_csc_params;
	struct set_ocsc_default_params set_ocsc_default_params;
};

enum block_sequence_func {
	DMUB_SUBVP_PIPE_CONTROL_LOCK_FAST = 0,
	OPTC_PIPE_CONTROL_LOCK,
	HUBP_SET_FLIP_CONTROL_GSL,
	HUBP_PROGRAM_TRIPLEBUFFER,
	HUBP_UPDATE_PLANE_ADDR,
	DPP_SET_INPUT_TRANSFER_FUNC,
	DPP_PROGRAM_GAMUT_REMAP,
	OPTC_PROGRAM_MANUAL_TRIGGER,
	DMUB_SEND_DMCUB_CMD,
	DPP_SETUP_DPP,
	DPP_PROGRAM_BIAS_AND_SCALE,
	DPP_SET_OUTPUT_TRANSFER_FUNC,
	MPC_UPDATE_VISUAL_CONFIRM,
	MPC_POWER_ON_MPC_MEM_PWR,
	MPC_SET_OUTPUT_CSC,
	MPC_SET_OCSC_DEFAULT,
};

struct block_sequence {
	union block_sequence_params params;
	enum block_sequence_func func;
};

struct hw_sequencer_funcs {
	void (*hardware_release)(struct dc *dc);
	/* Embedded Display Related */
	void (*edp_power_control)(struct dc_link *link, bool enable);
	void (*edp_wait_for_hpd_ready)(struct dc_link *link, bool power_up);
	void (*edp_wait_for_T12)(struct dc_link *link);

	/* Pipe Programming Related */
	void (*init_hw)(struct dc *dc);
	void (*power_down_on_boot)(struct dc *dc);
	void (*enable_accelerated_mode)(struct dc *dc,
			struct dc_state *context);
	enum dc_status (*apply_ctx_to_hw)(struct dc *dc,
			struct dc_state *context);
	void (*disable_plane)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	void (*disable_pixel_data)(struct dc *dc, struct pipe_ctx *pipe_ctx, bool blank);
	void (*apply_ctx_for_surface)(struct dc *dc,
			const struct dc_stream_state *stream,
			int num_planes, struct dc_state *context);
	void (*program_front_end_for_ctx)(struct dc *dc,
			struct dc_state *context);
	void (*wait_for_pending_cleared)(struct dc *dc,
			struct dc_state *context);
	void (*post_unlock_program_front_end)(struct dc *dc,
			struct dc_state *context);
	void (*update_plane_addr)(const struct dc *dc,
			struct pipe_ctx *pipe_ctx);
	void (*update_dchub)(struct dce_hwseq *hws,
			struct dchub_init_data *dh_data);
	void (*wait_for_mpcc_disconnect)(struct dc *dc,
			struct resource_pool *res_pool,
			struct pipe_ctx *pipe_ctx);
	void (*edp_backlight_control)(
			struct dc_link *link,
			bool enable);
	void (*program_triplebuffer)(const struct dc *dc,
		struct pipe_ctx *pipe_ctx, bool enableTripleBuffer);
	void (*update_pending_status)(struct pipe_ctx *pipe_ctx);
	void (*power_down)(struct dc *dc);
	void (*update_dsc_pg)(struct dc *dc, struct dc_state *context, bool safe_to_disable);

	/* Pipe Lock Related */
	void (*pipe_control_lock)(struct dc *dc,
			struct pipe_ctx *pipe, bool lock);
	void (*interdependent_update_lock)(struct dc *dc,
			struct dc_state *context, bool lock);
	void (*set_flip_control_gsl)(struct pipe_ctx *pipe_ctx,
			bool flip_immediate);
	void (*cursor_lock)(struct dc *dc, struct pipe_ctx *pipe, bool lock);

	/* Timing Related */
	void (*get_position)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct crtc_position *position);
	int (*get_vupdate_offset_from_vsync)(struct pipe_ctx *pipe_ctx);
	void (*calc_vupdate_position)(
			struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			uint32_t *start_line,
			uint32_t *end_line);
	void (*enable_per_frame_crtc_position_reset)(struct dc *dc,
			int group_size, struct pipe_ctx *grouped_pipes[]);
	void (*enable_timing_synchronization)(struct dc *dc,
			int group_index, int group_size,
			struct pipe_ctx *grouped_pipes[]);
	void (*enable_vblanks_synchronization)(struct dc *dc,
			int group_index, int group_size,
			struct pipe_ctx *grouped_pipes[]);
	void (*setup_periodic_interrupt)(struct dc *dc,
			struct pipe_ctx *pipe_ctx);
	void (*set_drr)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct dc_crtc_timing_adjust adjust);
	void (*set_static_screen_control)(struct pipe_ctx **pipe_ctx,
			int num_pipes,
			const struct dc_static_screen_params *events);
#ifndef TRIM_FSFT
	bool (*optimize_timing_for_fsft)(struct dc *dc,
			struct dc_crtc_timing *timing,
			unsigned int max_input_rate_in_khz);
#endif

	/* Stream Related */
	void (*enable_stream)(struct pipe_ctx *pipe_ctx);
	void (*disable_stream)(struct pipe_ctx *pipe_ctx);
	void (*blank_stream)(struct pipe_ctx *pipe_ctx);
	void (*unblank_stream)(struct pipe_ctx *pipe_ctx,
			struct dc_link_settings *link_settings);

	/* Bandwidth Related */
	void (*prepare_bandwidth)(struct dc *dc, struct dc_state *context);
	bool (*update_bandwidth)(struct dc *dc, struct dc_state *context);
	void (*optimize_bandwidth)(struct dc *dc, struct dc_state *context);

	/* Infopacket Related */
	void (*set_avmute)(struct pipe_ctx *pipe_ctx, bool enable);
	void (*send_immediate_sdp_message)(
			struct pipe_ctx *pipe_ctx,
			const uint8_t *custom_sdp_message,
			unsigned int sdp_message_size);
	void (*update_info_frame)(struct pipe_ctx *pipe_ctx);
	void (*set_dmdata_attributes)(struct pipe_ctx *pipe);
	void (*program_dmdata_engine)(struct pipe_ctx *pipe_ctx);
	bool (*dmdata_status_done)(struct pipe_ctx *pipe_ctx);

	/* Cursor Related */
	void (*set_cursor_position)(struct pipe_ctx *pipe);
	void (*set_cursor_attribute)(struct pipe_ctx *pipe);
	void (*set_cursor_sdr_white_level)(struct pipe_ctx *pipe);

	/* Colour Related */
	void (*program_gamut_remap)(struct pipe_ctx *pipe_ctx);
	void (*program_output_csc)(struct dc *dc, struct pipe_ctx *pipe_ctx,
			enum dc_color_space colorspace,
			uint16_t *matrix, int opp_id);

	/* VM Related */
	int (*init_sys_ctx)(struct dce_hwseq *hws,
			struct dc *dc,
			struct dc_phy_addr_space_config *pa_config);
	void (*init_vm_ctx)(struct dce_hwseq *hws,
			struct dc *dc,
			struct dc_virtual_addr_space_config *va_config,
			int vmid);

	/* Writeback Related */
	void (*update_writeback)(struct dc *dc,
			struct dc_writeback_info *wb_info,
			struct dc_state *context);
	void (*enable_writeback)(struct dc *dc,
			struct dc_writeback_info *wb_info,
			struct dc_state *context);
	void (*disable_writeback)(struct dc *dc,
			unsigned int dwb_pipe_inst);

	bool (*mmhubbub_warmup)(struct dc *dc,
			unsigned int num_dwb,
			struct dc_writeback_info *wb_info);

	/* Clock Related */
	enum dc_status (*set_clock)(struct dc *dc,
			enum dc_clock_type clock_type,
			uint32_t clk_khz, uint32_t stepping);
	void (*get_clock)(struct dc *dc, enum dc_clock_type clock_type,
			struct dc_clock_config *clock_cfg);
	void (*optimize_pwr_state)(const struct dc *dc,
			struct dc_state *context);
	void (*exit_optimized_pwr_state)(const struct dc *dc,
			struct dc_state *context);

	/* Audio Related */
	void (*enable_audio_stream)(struct pipe_ctx *pipe_ctx);
	void (*disable_audio_stream)(struct pipe_ctx *pipe_ctx);

	/* Stereo 3D Related */
	void (*setup_stereo)(struct pipe_ctx *pipe_ctx, struct dc *dc);

	/* HW State Logging Related */
	void (*log_hw_state)(struct dc *dc, struct dc_log_buffer_ctx *log_ctx);
	void (*get_hw_state)(struct dc *dc, char *pBuf,
			unsigned int bufSize, unsigned int mask);
	void (*clear_status_bits)(struct dc *dc, unsigned int mask);

	bool (*set_backlight_level)(struct pipe_ctx *pipe_ctx,
			uint32_t backlight_pwm_u16_16,
			uint32_t frame_ramp);

	void (*set_abm_immediate_disable)(struct pipe_ctx *pipe_ctx);

	void (*set_pipe)(struct pipe_ctx *pipe_ctx);

	void (*enable_dp_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal,
			enum clock_source_id clock_source,
			const struct dc_link_settings *link_settings);
	void (*enable_tmds_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal,
			enum clock_source_id clock_source,
			enum dc_color_depth color_depth,
			uint32_t pixel_clock);
	void (*enable_lvds_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum clock_source_id clock_source,
			uint32_t pixel_clock);
	void (*disable_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal);

	void (*get_dcc_en_bits)(struct dc *dc, int *dcc_en_bits);

	/* Idle Optimization Related */
	bool (*apply_idle_power_optimizations)(struct dc *dc, bool enable);

	bool (*does_plane_fit_in_mall)(struct dc *dc, struct dc_plane_state *plane,
			struct dc_cursor_attributes *cursor_attr);

	bool (*is_abm_supported)(struct dc *dc,
			struct dc_state *context, struct dc_stream_state *stream);

	void (*set_disp_pattern_generator)(const struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			enum controller_dp_test_pattern test_pattern,
			enum controller_dp_color_space color_space,
			enum dc_color_depth color_depth,
			const struct tg_color *solid_color,
			int width, int height, int offset);

	void (*subvp_pipe_control_lock_fast)(union block_sequence_params *params);
	void (*z10_restore)(const struct dc *dc);
	void (*z10_save_init)(struct dc *dc);

	void (*update_visual_confirm_color)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			int mpcc_id);

	void (*update_phantom_vp_position)(struct dc *dc,
			struct dc_state *context,
			struct pipe_ctx *phantom_pipe);
	void (*apply_update_flags_for_phantom)(struct pipe_ctx *phantom_pipe);

	void (*commit_subvp_config)(struct dc *dc, struct dc_state *context);
	void (*enable_phantom_streams)(struct dc *dc, struct dc_state *context);
	void (*subvp_pipe_control_lock)(struct dc *dc,
			struct dc_state *context,
			bool lock,
			bool should_lock_all_pipes,
			struct pipe_ctx *top_pipe_to_program,
			bool subvp_prev_use);

};

void color_space_to_black_color(
	const struct dc *dc,
	enum dc_color_space colorspace,
	struct tg_color *black_color);

bool hwss_wait_for_blank_complete(
		struct timing_generator *tg);

const uint16_t *find_color_matrix(
		enum dc_color_space color_space,
		uint32_t *array_size);

void get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void get_subvp_visual_confirm_color(
	struct dc *dc,
	struct dc_state *context,
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color);

void get_hdr_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
void get_mpctree_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
void get_surface_tile_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void get_mclk_switch_visual_confirm_color(
		struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void hwss_execute_sequence(struct dc *dc,
		struct block_sequence block_sequence[],
		int num_steps);

void hwss_build_fast_sequence(struct dc *dc,
		struct dc_dmub_cmd *dc_dmub_cmd,
		unsigned int dmub_cmd_count,
		struct block_sequence block_sequence[],
		int *num_steps,
		struct pipe_ctx *pipe_ctx);

void hwss_send_dmcub_cmd(union block_sequence_params *params);

void hwss_program_manual_trigger(union block_sequence_params *params);

void hwss_setup_dpp(union block_sequence_params *params);

void hwss_program_bias_and_scale(union block_sequence_params *params);

void hwss_power_on_mpc_mem_pwr(union block_sequence_params *params);

void hwss_set_output_csc(union block_sequence_params *params);

void hwss_set_ocsc_default(union block_sequence_params *params);

#endif /* __DC_HW_SEQUENCER_H__ */
