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

/*
 *
 * DAL logger functionality
 *
 */

struct dal_logger *dal_logger_create(struct dc_context *ctx);

uint32_t dal_logger_destroy(struct dal_logger **logger);

void dm_logger_write(
		struct dal_logger *logger,
		enum dc_log_type log_type,
		const char *msg,
		...);

void dm_logger_append(
		struct log_entry *entry,
		const char *msg,
		...);

void dm_logger_open(
		struct dal_logger *logger,
		struct log_entry *entry,
		enum dc_log_type log_type);

void dm_logger_close(struct log_entry *entry);

void dc_conn_log(struct dc_context *ctx,
		const struct dc_link *link,
		uint8_t *hex_data,
		int hex_data_count,
		enum dc_log_type event,
		const char *msg,
		...);

void logger_write(struct dal_logger *logger,
		enum dc_log_type log_type,
		const char *msg,
		void *paralist);

void pre_surface_trace(
		const struct dc *dc,
		const struct dc_surface *const *surfaces,
		int surface_count);

void update_surface_trace(
		const struct dc *dc,
		const struct dc_surface_update *updates,
		int surface_count);

void post_surface_trace(const struct dc *dc);

void context_timing_trace(
		const struct dc *dc,
		struct resource_context *res_ctx);


/* Any function which is empty or have incomplete implementation should be
 * marked by this macro.
 * Note that the message will be printed exactly once for every function
 * it is used in order to avoid repeating of the same message. */
#define DAL_LOGGER_NOT_IMPL(fmt, ...) \
{ \
	static bool print_not_impl = true; \
\
	if (print_not_impl == true) { \
		print_not_impl = false; \
		dm_logger_write(ctx->logger, LOG_WARNING, \
		"DAL_NOT_IMPL: " fmt, ##__VA_ARGS__); \
	} \
}

/******************************************************************************
 * Convenience macros to save on typing.
 *****************************************************************************/

#define DC_ERROR(...) \
	dm_logger_write(dc_ctx->logger, LOG_ERROR, \
		__VA_ARGS__);

#define DC_SYNC_INFO(...) \
	dm_logger_write(dc_ctx->logger, LOG_SYNC, \
		__VA_ARGS__);


/* Connectivity log format:
 * [time stamp]   [drm] [Major_minor] [connector name] message.....
 * eg:
 * [   26.590965] [drm] [Conn_LKTN]	  [DP-1] HBRx4 pass VS=0, PE=0^
 * [   26.881060] [drm] [Conn_Mode]	  [DP-1] {2560x1080, 2784x1111@185580Khz}^
 */

#define CONN_DATA_DETECT(link, hex_data, hex_len, ...) \
		dc_conn_log(link->ctx, &link->public, hex_data, hex_len, \
				LOG_EVENT_DETECTION, ##__VA_ARGS__)

#define CONN_DATA_LINK_LOSS(link, hex_data, hex_len, ...) \
		dc_conn_log(link->ctx, &link->public, hex_data, hex_len, \
				LOG_EVENT_LINK_LOSS, ##__VA_ARGS__)

#define CONN_MSG_LT(link, ...) \
		dc_conn_log(link->ctx, &link->public, NULL, 0, \
				LOG_EVENT_LINK_TRAINING, ##__VA_ARGS__)

#define CONN_MSG_MODE(link, ...) \
		dc_conn_log(link->ctx, &link->public, NULL, 0, \
				LOG_EVENT_MODE_SET, ##__VA_ARGS__)

#endif /* __DAL_LOGGER_INTERFACE_H__ */
