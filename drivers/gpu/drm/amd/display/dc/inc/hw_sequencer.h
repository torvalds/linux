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

enum pipe_gating_control {
	PIPE_GATING_CONTROL_DISABLE = 0,
	PIPE_GATING_CONTROL_ENABLE,
	PIPE_GATING_CONTROL_INIT
};

enum vline_select {
	VLINE0,
	VLINE1
};

struct dce_hwseq_wa {
	bool blnd_crtc_trigger;
	bool DEGVIDCN10_253;
	bool false_optc_underflow;
	bool DEGVIDCN10_254;
	bool DEGVIDCN21;
};

struct hwseq_wa_state {
	bool DEGVIDCN10_253_applied;
};

struct dce_hwseq {
	struct dc_context *ctx;
	const struct dce_hwseq_registers *regs;
	const struct dce_hwseq_shift *shifts;
	const struct dce_hwseq_mask *masks;
	struct dce_hwseq_wa wa;
	struct hwseq_wa_state wa_state;
};

struct pipe_ctx;
struct dc_state;
#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
struct dc_stream_status;
struct dc_writeback_info;
#endif
struct dchub_init_data;
struct dc_static_screen_events;
struct resource_pool;
struct resource_context;
struct stream_resource;
#ifdef CONFIG_DRM_AMD_DC_DCN2_0
struct dc_phy_addr_space_config;
struct dc_virtual_addr_space_config;
#endif
struct hubp;
struct dpp;

struct hw_sequencer_funcs {

	void (*disable_stream_gating)(struct dc *dc, struct pipe_ctx *pipe_ctx);

	void (*enable_stream_gating)(struct dc *dc, struct pipe_ctx *pipe_ctx);

	void (*init_hw)(struct dc *dc);

	void (*init_pipes)(struct dc *dc, struct dc_state *context);

	enum dc_status (*apply_ctx_to_hw)(
			struct dc *dc, struct dc_state *context);

	void (*reset_hw_ctx_wrap)(
			struct dc *dc, struct dc_state *context);

	void (*apply_ctx_for_surface)(
			struct dc *dc,
			const struct dc_stream_state *stream,
			int num_planes,
			struct dc_state *context);

	void (*program_gamut_remap)(
			struct pipe_ctx *pipe_ctx);

	void (*program_output_csc)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			enum dc_color_space colorspace,
			uint16_t *matrix,
			int opp_id);

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	void (*program_triplebuffer)(
		const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool enableTripleBuffer);
	void (*set_flip_control_gsl)(
		struct pipe_ctx *pipe_ctx,
		bool flip_immediate);
#endif

	void (*update_plane_addr)(
		const struct dc *dc,
		struct pipe_ctx *pipe_ctx);

	void (*plane_atomic_disconnect)(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx);

	void (*update_dchub)(
		struct dce_hwseq *hws,
		struct dchub_init_data *dh_data);

#ifdef CONFIG_DRM_AMD_DC_DCN2_0
	int (*init_sys_ctx)(
			struct dce_hwseq *hws,
			struct dc *dc,
			struct dc_phy_addr_space_config *pa_config);
	void (*init_vm_ctx)(
			struct dce_hwseq *hws,
			struct dc *dc,
			struct dc_virtual_addr_space_config *va_config,
			int vmid);
#endif
	void (*update_mpcc)(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx);

	void (*update_pending_status)(
			struct pipe_ctx *pipe_ctx);

	bool (*set_input_transfer_func)(
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state);

	bool (*set_output_transfer_func)(
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream);

	void (*power_down)(struct dc *dc);

	void (*enable_accelerated_mode)(struct dc *dc, struct dc_state *context);

	void (*enable_timing_synchronization)(
			struct dc *dc,
			int group_index,
			int group_size,
			struct pipe_ctx *grouped_pipes[]);

	void (*enable_per_frame_crtc_position_reset)(
			struct dc *dc,
			int group_size,
			struct pipe_ctx *grouped_pipes[]);

	void (*enable_display_pipe_clock_gating)(
					struct dc_context *ctx,
					bool clock_gating);

	bool (*enable_display_power_gating)(
					struct dc *dc,
					uint8_t controller_id,
					struct dc_bios *dcb,
					enum pipe_gating_control power_gating);

	void (*disable_plane)(struct dc *dc, struct pipe_ctx *pipe_ctx);

	void (*update_info_frame)(struct pipe_ctx *pipe_ctx);

	void (*send_immediate_sdp_message)(
				struct pipe_ctx *pipe_ctx,
				const uint8_t *custom_sdp_message,
				unsigned int sdp_message_size);

	void (*enable_stream)(struct pipe_ctx *pipe_ctx);

	void (*disable_stream)(struct pipe_ctx *pipe_ctx);

	void (*unblank_stream)(struct pipe_ctx *pipe_ctx,
			struct dc_link_settings *link_settings);

	void (*blank_stream)(struct pipe_ctx *pipe_ctx);

	void (*enable_audio_stream)(struct pipe_ctx *pipe_ctx);

	void (*disable_audio_stream)(struct pipe_ctx *pipe_ctx);

	void (*pipe_control_lock)(
				struct dc *dc,
				struct pipe_ctx *pipe,
				bool lock);

	void (*pipe_control_lock_global)(
				struct dc *dc,
				struct pipe_ctx *pipe,
				bool lock);
	void (*blank_pixel_data)(
			struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			bool blank);

	void (*prepare_bandwidth)(
			struct dc *dc,
			struct dc_state *context);
	void (*optimize_bandwidth)(
			struct dc *dc,
			struct dc_state *context);

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	bool (*update_bandwidth)(
			struct dc *dc,
			struct dc_state *context);
	void (*program_dmdata_engine)(struct pipe_ctx *pipe_ctx);
	bool (*dmdata_status_done)(struct pipe_ctx *pipe_ctx);
#endif

	void (*set_drr)(struct pipe_ctx **pipe_ctx, int num_pipes,
			unsigned int vmin, unsigned int vmax,
			unsigned int vmid, unsigned int vmid_frame_number);

	void (*get_position)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct crtc_position *position);

	void (*set_static_screen_control)(struct pipe_ctx **pipe_ctx,
			int num_pipes, const struct dc_static_screen_events *events);

	enum dc_status (*enable_stream_timing)(
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context,
			struct dc *dc);

	void (*setup_stereo)(
			struct pipe_ctx *pipe_ctx,
			struct dc *dc);

	void (*set_avmute)(struct pipe_ctx *pipe_ctx, bool enable);

	void (*log_hw_state)(struct dc *dc,
		struct dc_log_buffer_ctx *log_ctx);
	void (*get_hw_state)(struct dc *dc, char *pBuf, unsigned int bufSize, unsigned int mask);
	void (*clear_status_bits)(struct dc *dc, unsigned int mask);

	void (*wait_for_mpcc_disconnect)(struct dc *dc,
			struct resource_pool *res_pool,
			struct pipe_ctx *pipe_ctx);

	void (*edp_power_control)(
			struct dc_link *link,
			bool enable);
	void (*edp_backlight_control)(
			struct dc_link *link,
			bool enable);
	void (*edp_wait_for_hpd_ready)(struct dc_link *link, bool power_up);

	void (*set_cursor_position)(struct pipe_ctx *pipe);
	void (*set_cursor_attribute)(struct pipe_ctx *pipe);
	void (*set_cursor_sdr_white_level)(struct pipe_ctx *pipe);

	void (*setup_periodic_interrupt)(struct pipe_ctx *pipe_ctx, enum vline_select vline);
	void (*setup_vupdate_interrupt)(struct pipe_ctx *pipe_ctx);
	bool (*did_underflow_occur)(struct dc *dc, struct pipe_ctx *pipe_ctx);

	void (*init_blank)(struct dc *dc, struct timing_generator *tg);
	void (*disable_vga)(struct dce_hwseq *hws);
	void (*bios_golden_init)(struct dc *dc);
	void (*plane_atomic_power_down)(struct dc *dc,
			struct dpp *dpp,
			struct hubp *hubp);

	void (*plane_atomic_disable)(
			struct dc *dc, struct pipe_ctx *pipe_ctx);

	void (*enable_power_gating_plane)(
		struct dce_hwseq *hws,
		bool enable);

	void (*dpp_pg_control)(
			struct dce_hwseq *hws,
			unsigned int dpp_inst,
			bool power_on);

	void (*hubp_pg_control)(
			struct dce_hwseq *hws,
			unsigned int hubp_inst,
			bool power_on);

	void (*dsc_pg_control)(
			struct dce_hwseq *hws,
			unsigned int dsc_inst,
			bool power_on);


#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	void (*update_odm)(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx);
	void (*program_all_writeback_pipes_in_tree)(
			struct dc *dc,
			const struct dc_stream_state *stream,
			struct dc_state *context);
	void (*update_writeback)(struct dc *dc,
			const struct dc_stream_status *stream_status,
			struct dc_writeback_info *wb_info);
	void (*enable_writeback)(struct dc *dc,
			const struct dc_stream_status *stream_status,
			struct dc_writeback_info *wb_info);
	void (*disable_writeback)(struct dc *dc,
			unsigned int dwb_pipe_inst);
#endif
	enum dc_status (*set_clock)(struct dc *dc,
			enum dc_clock_type clock_type,
			uint32_t clk_khz,
			uint32_t stepping);

	void (*get_clock)(struct dc *dc,
			enum dc_clock_type clock_type,
			struct dc_clock_config *clock_cfg);

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

#endif /* __DC_HW_SEQUENCER_H__ */
