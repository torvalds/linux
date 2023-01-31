/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_H__
#define __DC_LINK_H__

/* FILE POLICY AND INTENDED USAGE:
 *
 * This header declares link functions exposed to dc. All functions must have
 * "link_" as prefix. For example link_run_my_function. This header is strictly
 * private in dc and should never be included in other header files. dc
 * components should include this header in their .c files in order to access
 * functions in link folder. This file should never include any header files in
 * link folder. If there is a need to expose a function declared in one of
 * header files in side link folder, you need to move the function declaration
 * into this file and prefix it with "link_".
 */
#include "core_types.h"
#include "dc_link.h"

struct gpio *link_get_hpd_gpio(struct dc_bios *dcb,
		struct graphics_object_id link_id,
		struct gpio_service *gpio_service);

struct ddc_service_init_data {
	struct graphics_object_id id;
	struct dc_context *ctx;
	struct dc_link *link;
	bool is_dpia_link;
};

struct ddc_service *link_create_ddc_service(
		struct ddc_service_init_data *ddc_init_data);

void link_destroy_ddc_service(struct ddc_service **ddc);

bool link_is_in_aux_transaction_mode(struct ddc_service *ddc);

bool link_query_ddc_data(
		struct ddc_service *ddc,
		uint32_t address,
		uint8_t *write_buf,
		uint32_t write_size,
		uint8_t *read_buf,
		uint32_t read_size);


/* Attempt to submit an aux payload, retrying on timeouts, defers, and busy
 * states as outlined in the DP spec.  Returns true if the request was
 * successful.
 *
 * NOTE: The function requires explicit mutex on DM side in order to prevent
 * potential race condition. DC components should call the dpcd read/write
 * function in dm_helpers in order to access dpcd safely
 */
bool link_aux_transfer_with_retries_no_mutex(struct ddc_service *ddc,
		struct aux_payload *payload);

uint32_t link_get_aux_defer_delay(struct ddc_service *ddc);

bool link_is_dp_128b_132b_signal(struct pipe_ctx *pipe_ctx);

enum dp_link_encoding link_dp_get_encoding_format(
		const struct dc_link_settings *link_settings);

bool link_decide_link_settings(
	struct dc_stream_state *stream,
	struct dc_link_settings *link_setting);

void link_dp_trace_set_edp_power_timestamp(struct dc_link *link,
		bool power_up);
uint64_t link_dp_trace_get_edp_poweron_timestamp(struct dc_link *link);
uint64_t link_dp_trace_get_edp_poweroff_timestamp(struct dc_link *link);

bool link_is_edp_ilr_optimization_required(struct dc_link *link,
		struct dc_crtc_timing *crtc_timing);

bool link_backlight_enable_aux(struct dc_link *link, bool enable);
void link_edp_add_delay_for_T9(struct dc_link *link);
bool link_edp_receiver_ready_T9(struct dc_link *link);
bool link_edp_receiver_ready_T7(struct dc_link *link);
bool link_power_alpm_dpcd_enable(struct dc_link *link, bool enable);
bool link_set_sink_vtotal_in_psr_active(const struct dc_link *link,
		uint16_t psr_vtotal_idle, uint16_t psr_vtotal_su);
void link_get_psr_residency(const struct dc_link *link, uint32_t *residency);

#endif /* __DC_LINK_HPD_H__ */
