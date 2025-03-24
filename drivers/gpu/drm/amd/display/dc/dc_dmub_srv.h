/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef _DMUB_DC_SRV_H_
#define _DMUB_DC_SRV_H_

#include "dm_services_types.h"
#include "dmub/dmub_srv.h"

struct dmub_srv;
struct dc;
struct pipe_ctx;
struct dc_crtc_timing_adjust;
struct dc_crtc_timing;
struct dc_state;
struct dc_surface_update;

struct dc_reg_helper_state {
	bool gather_in_progress;
	uint32_t same_addr_count;
	bool should_burst_write;
	union dmub_rb_cmd cmd_data;
	unsigned int reg_seq_count;
};

struct dc_dmub_srv {
	struct dmub_srv *dmub;
	struct dc_reg_helper_state reg_helper_offload;

	struct dc_context *ctx;
	void *dm;

	int32_t idle_exit_counter;
	union dmub_shared_state_ips_driver_signals driver_signals;
	bool idle_allowed;
	bool needs_idle_wake;
};

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_optimized_init_done(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_cmd_list_queue_execute(struct dc_dmub_srv *dc_dmub_srv,
		unsigned int count,
		union dmub_rb_cmd *cmd_list);

bool dc_dmub_srv_wait_for_idle(struct dc_dmub_srv *dc_dmub_srv,
		enum dm_dmub_wait_type wait_type,
		union dmub_rb_cmd *cmd_list);

bool dc_dmub_srv_cmd_run(struct dc_dmub_srv *dc_dmub_srv, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type);

bool dc_dmub_srv_cmd_run_list(struct dc_dmub_srv *dc_dmub_srv, unsigned int count, union dmub_rb_cmd *cmd_list, enum dm_dmub_wait_type wait_type);

bool dc_dmub_srv_notify_stream_mask(struct dc_dmub_srv *dc_dmub_srv,
				   unsigned int stream_mask);

bool dc_dmub_srv_is_restore_required(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_get_dmub_outbox0_msg(const struct dc *dc, struct dmcub_trace_buf_entry *entry);

void dc_dmub_trace_event_control(struct dc *dc, bool enable);

void dc_dmub_srv_drr_update_cmd(struct dc *dc, uint32_t tg_inst, uint32_t vtotal_min, uint32_t vtotal_max);

void dc_dmub_srv_set_drr_manual_trigger_cmd(struct dc *dc, uint32_t tg_inst);
bool dc_dmub_srv_p_state_delegate(struct dc *dc, bool enable_pstate, struct dc_state *context);

void dc_dmub_srv_query_caps_cmd(struct dc_dmub_srv *dc_dmub_srv);
void dc_dmub_srv_get_visual_confirm_color_cmd(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dc_dmub_srv_clear_inbox0_ack(struct dc_dmub_srv *dmub_srv);
void dc_dmub_srv_wait_for_inbox0_ack(struct dc_dmub_srv *dmub_srv);
void dc_dmub_srv_send_inbox0_cmd(struct dc_dmub_srv *dmub_srv, union dmub_inbox0_data_register data);

bool dc_dmub_srv_get_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv);

void dc_dmub_setup_subvp_dmub_command(struct dc *dc, struct dc_state *context, bool enable);
void dc_dmub_srv_log_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv);

void dc_send_update_cursor_info_to_dmu(struct pipe_ctx *pCtx, uint8_t pipe_idx);
bool dc_dmub_check_min_version(struct dmub_srv *srv);

void dc_dmub_srv_enable_dpia_trace(const struct dc *dc);
void dc_dmub_srv_subvp_save_surf_addr(const struct dc_dmub_srv *dc_dmub_srv, const struct dc_plane_address *addr, uint8_t subvp_index);

bool dc_dmub_srv_is_hw_pwr_up(struct dc_dmub_srv *dc_dmub_srv, bool wait);

void dc_dmub_srv_apply_idle_power_optimizations(const struct dc *dc, bool allow_idle);

/**
 * dc_dmub_srv_set_power_state() - Sets the power state for DMUB service.
 *
 * Controls whether messaging the DMCUB or interfacing with it via HW register
 * interaction is permittable.
 *
 * @dc_dmub_srv - The DC DMUB service pointer
 * @power_state - the DC power state
 */
void dc_dmub_srv_set_power_state(struct dc_dmub_srv *dc_dmub_srv, enum dc_acpi_cm_power_state power_state);

/**
 * dc_dmub_srv_notify_fw_dc_power_state() - Notifies firmware of the DC power state.
 *
 * Differs from dc_dmub_srv_set_power_state in that it needs to access HW in order
 * to message DMCUB of the state transition. Should come after the D0 exit and
 * before D3 set power state.
 *
 * @dc_dmub_srv - The DC DMUB service pointer
 * @power_state - the DC power state
 */
void dc_dmub_srv_notify_fw_dc_power_state(struct dc_dmub_srv *dc_dmub_srv,
					  enum dc_acpi_cm_power_state power_state);

/**
 * @dc_dmub_srv_should_detect() - Checks if link detection is required.
 *
 * While in idle power states we may need driver to manually redetect in
 * the case of a missing hotplug. Should be called from a polling timer.
 *
 * Return: true if redetection is required.
 */
bool dc_dmub_srv_should_detect(struct dc_dmub_srv *dc_dmub_srv);

/**
 * dc_wake_and_execute_dmub_cmd() - Wrapper for DMUB command execution.
 *
 * Refer to dc_wake_and_execute_dmub_cmd_list() for usage and limitations,
 * This function is a convenience wrapper for a single command execution.
 *
 * @ctx: DC context
 * @cmd: The command to send/receive
 * @wait_type: The wait behavior for the execution
 *
 * Return: true on command submission success, false otherwise
 */
bool dc_wake_and_execute_dmub_cmd(const struct dc_context *ctx, union dmub_rb_cmd *cmd,
				  enum dm_dmub_wait_type wait_type);

/**
 * dc_wake_and_execute_dmub_cmd_list() - Wrapper for DMUB command list execution.
 *
 * If the DMCUB hardware was asleep then it wakes the DMUB before
 * executing the command and attempts to re-enter if the command
 * submission was successful.
 *
 * This should be the preferred command submission interface provided
 * the DC lock is acquired.
 *
 * Entry/exit out of idle power optimizations would need to be
 * manually performed otherwise through dc_allow_idle_optimizations().
 *
 * @ctx: DC context
 * @count: Number of commands to send/receive
 * @cmd: Array of commands to send
 * @wait_type: The wait behavior for the execution
 *
 * Return: true on command submission success, false otherwise
 */
bool dc_wake_and_execute_dmub_cmd_list(const struct dc_context *ctx, unsigned int count,
				       union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type);

/**
 * dc_wake_and_execute_gpint()
 *
 * @ctx: DC context
 * @command_code: The command ID to send to DMCUB
 * @param: The parameter to message DMCUB
 * @response: Optional response out value - may be NULL.
 * @wait_type: The wait behavior for the execution
 */
bool dc_wake_and_execute_gpint(const struct dc_context *ctx, enum dmub_gpint_command command_code,
			       uint16_t param, uint32_t *response, enum dm_dmub_wait_type wait_type);

void dc_dmub_srv_fams2_update_config(struct dc *dc,
		struct dc_state *context,
		bool enable);
void dc_dmub_srv_fams2_drr_update(struct dc *dc,
		uint32_t tg_inst,
		uint32_t vtotal_min,
		uint32_t vtotal_max,
		uint32_t vtotal_mid,
		uint32_t vtotal_mid_frame_num,
		bool program_manual_trigger);
void dc_dmub_srv_fams2_passthrough_flip(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *stream,
		struct dc_surface_update *srf_updates,
		int surface_count);

/**
 * struct ips_residency_info - struct containing info from dmub_ips_residency_stats
 *
 * @ips_mode: The mode of IPS that the follow stats appertain to
 * @residency_percent: The percentage of time spent in given IPS mode in millipercent
 * @entry_counter: The number of entries made in to this IPS state
 * @total_active_time_us: uint32_t array of length 2 representing time in the given IPS mode
 *                        in microseconds. Index 0 is lower 32 bits, index 1 is upper 32 bits.
 * @total_inactive_time_us: uint32_t array of length 2 representing time outside the given IPS mode
 *                          in microseconds. Index 0 is lower 32 bits, index 1 is upper 32 bits.
 * @histogram: Histogram of given IPS state durations - bucket definitions in dmub_ips.c
 */
struct ips_residency_info {
	enum dmub_ips_mode ips_mode;
	unsigned int residency_percent;
	unsigned int entry_counter;
	unsigned int total_active_time_us[2];
	unsigned int total_inactive_time_us[2];
	unsigned int histogram[16];
};

/**
 * bool dc_dmub_srv_ips_residency_cntl() - Controls IPS residency measurement status
 *
 * @dc_dmub_srv: The DC DMUB service pointer
 * @start_measurement: Describes whether to start or stop measurement
 *
 * Return: true if GPINT was sent successfully, false otherwise
 */
bool dc_dmub_srv_ips_residency_cntl(struct dc_dmub_srv *dc_dmub_srv, bool start_measurement);

/**
 * bool dc_dmub_srv_ips_query_residency_info() - Queries DMCUB for residency info
 *
 * @dc_dmub_srv: The DC DMUB service pointer
 * @output: Output struct to copy the the residency info to
 */
void dc_dmub_srv_ips_query_residency_info(struct dc_dmub_srv *dc_dmub_srv, struct ips_residency_info *output);
#endif /* _DMUB_DC_SRV_H_ */
