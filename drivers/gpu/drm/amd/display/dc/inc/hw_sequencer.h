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

enum vline_select {
	VLINE0,
	VLINE1
};

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

struct hw_sequencer_funcs {
	/* Embedded Display Related */
	void (*edp_power_control)(struct dc_link *link, bool enable);
	void (*edp_wait_for_hpd_ready)(struct dc_link *link, bool power_up);

	/* Pipe Programming Related */
	void (*init_hw)(struct dc *dc);
	void (*enable_accelerated_mode)(struct dc *dc,
			struct dc_state *context);
	enum dc_status (*apply_ctx_to_hw)(struct dc *dc,
			struct dc_state *context);
	void (*disable_plane)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	void (*apply_ctx_for_surface)(struct dc *dc,
			const struct dc_stream_state *stream,
			int num_planes, struct dc_state *context);
	void (*program_front_end_for_ctx)(struct dc *dc,
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
	void (*program_triplebuffer)(const struct dc *dc,
		struct pipe_ctx *pipe_ctx, bool enableTripleBuffer);
	void (*update_pending_status)(struct pipe_ctx *pipe_ctx);

	/* Pipe Lock Related */
	void (*pipe_control_lock)(struct dc *dc,
			struct pipe_ctx *pipe, bool lock);
	void (*interdependent_update_lock)(struct dc *dc,
			struct dc_state *context, bool lock);
	void (*set_flip_control_gsl)(struct pipe_ctx *pipe_ctx,
			bool flip_immediate);

	/* Timing Related */
	void (*get_position)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct crtc_position *position);
	int (*get_vupdate_offset_from_vsync)(struct pipe_ctx *pipe_ctx);
	void (*enable_per_frame_crtc_position_reset)(struct dc *dc,
			int group_size, struct pipe_ctx *grouped_pipes[]);
	void (*enable_timing_synchronization)(struct dc *dc,
			int group_index, int group_size,
			struct pipe_ctx *grouped_pipes[]);
	void (*setup_periodic_interrupt)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			enum vline_select vline);
	void (*set_drr)(struct pipe_ctx **pipe_ctx, int num_pipes,
			unsigned int vmin, unsigned int vmax,
			unsigned int vmid, unsigned int vmid_frame_number);
	void (*set_static_screen_control)(struct pipe_ctx **pipe_ctx,
			int num_pipes,
			const struct dc_static_screen_params *events);

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
