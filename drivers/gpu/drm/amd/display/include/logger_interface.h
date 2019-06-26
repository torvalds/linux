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

#ifndef __DAL_LOGGER_INTERFACE_H__
#define __DAL_LOGGER_INTERFACE_H__

#include "logger_types.h"

struct dc_context;
struct dc_link;
struct dc_surface_update;
struct resource_context;
struct dc_state;

/*
 *
 * DAL logger functionality
 *
 */

void dc_conn_log_hex_linux(const uint8_t *hex_data, int hex_data_count);

void pre_surface_trace(
		struct dc *dc,
		const struct dc_plane_state *const *plane_states,
		int surface_count);

void update_surface_trace(
		struct dc *dc,
		const struct dc_surface_update *updates,
		int surface_count);

void post_surface_trace(struct dc *dc);

void context_timing_trace(
		struct dc *dc,
		struct resource_context *res_ctx);

void context_clock_trace(
		struct dc *dc,
		struct dc_state *context);

/* Any function which is empty or have incomplete implementation should be
 * marked by this macro.
 * Note that the message will be printed exactly once for every function
 * it is used in order to avoid repeating of the same message. */

#define DAL_LOGGER_NOT_IMPL(fmt, ...) \
	do { \
		static bool print_not_impl = true; \
		if (print_not_impl == true) { \
			print_not_impl = false; \
			DRM_WARN("DAL_NOT_IMPL: " fmt, ##__VA_ARGS__); \
		} \
	} while (0)

/******************************************************************************
 * Convenience macros to save on typing.
 *****************************************************************************/

#define DC_ERROR(...) \
		do { \
			(void)(dc_ctx); \
			DC_LOG_ERROR(__VA_ARGS__); \
		} while (0)

#define DC_SYNC_INFO(...) \
		do { \
			(void)(dc_ctx); \
			DC_LOG_SYNC(__VA_ARGS__); \
		} while (0)

/* Connectivity log format:
 * [time stamp]   [drm] [Major_minor] [connector name] message.....
 * eg:
 * [   26.590965] [drm] [Conn_LKTN]	  [DP-1] HBRx4 pass VS=0, PE=0^
 * [   26.881060] [drm] [Conn_Mode]	  [DP-1] {2560x1080, 2784x1111@185580Khz}^
 */

#define CONN_DATA_DETECT(link, hex_data, hex_len, ...) \
		do { \
			(void)(link); \
			dc_conn_log_hex_linux(hex_data, hex_len); \
			DC_LOG_EVENT_DETECTION(__VA_ARGS__); \
		} while (0)

#define CONN_DATA_LINK_LOSS(link, hex_data, hex_len, ...) \
		do { \
			(void)(link); \
			dc_conn_log_hex_linux(hex_data, hex_len); \
			DC_LOG_EVENT_LINK_LOSS(__VA_ARGS__); \
		} while (0)

#define CONN_MSG_LT(link, ...) \
		do { \
			(void)(link); \
			DC_LOG_EVENT_LINK_TRAINING(__VA_ARGS__); \
		} while (0)

#define CONN_MSG_MODE(link, ...) \
		do { \
			(void)(link); \
			DC_LOG_EVENT_MODE_SET(__VA_ARGS__); \
		} while (0)

/*
 * Display Test Next logging
 */
#define DTN_INFO_BEGIN() \
	dm_dtn_log_begin(dc_ctx, log_ctx)

#define DTN_INFO(msg, ...) \
	dm_dtn_log_append_v(dc_ctx, log_ctx, msg, ##__VA_ARGS__)

#define DTN_INFO_END() \
	dm_dtn_log_end(dc_ctx, log_ctx)

#define PERFORMANCE_TRACE_START() \
	unsigned long long perf_trc_start_stmp = dm_get_timestamp(dc->ctx)

#define PERFORMANCE_TRACE_END() \
	do { \
		unsigned long long perf_trc_end_stmp = dm_get_timestamp(dc->ctx); \
		if (dc->debug.performance_trace) { \
			DC_LOG_PERF_TRACE("%s duration: %lld ticks\n", __func__, \
				perf_trc_end_stmp - perf_trc_start_stmp); \
		} \
	} while (0)

#define DISPLAY_STATS_BEGIN(entry) (void)(entry)

#define DISPLAY_STATS(msg, ...) DC_LOG_PERF_TRACE(msg, __VA_ARGS__)

#define DISPLAY_STATS_END(entry) (void)(entry)

#endif /* __DAL_LOGGER_INTERFACE_H__ */
