/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#include "core_types.h"
#include "logger.h"
#include "include/logger_interface.h"
#include "dm_helpers.h"

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

struct dc_signal_type_info {
	enum signal_type type;
	char name[MAX_NAME_LEN];
};

static const struct dc_signal_type_info signal_type_info_tbl[] = {
		{SIGNAL_TYPE_NONE,             "NC"},
		{SIGNAL_TYPE_DVI_SINGLE_LINK,  "DVI"},
		{SIGNAL_TYPE_DVI_DUAL_LINK,    "DDVI"},
		{SIGNAL_TYPE_HDMI_TYPE_A,      "HDMIA"},
		{SIGNAL_TYPE_LVDS,             "LVDS"},
		{SIGNAL_TYPE_RGB,              "VGA"},
		{SIGNAL_TYPE_DISPLAY_PORT,     "DP"},
		{SIGNAL_TYPE_DISPLAY_PORT_MST, "MST"},
		{SIGNAL_TYPE_EDP,              "eDP"},
		{SIGNAL_TYPE_VIRTUAL,          "Virtual"}
};

void dc_conn_log(struct dc_context *ctx,
		const struct dc_link *link,
		uint8_t *hex_data,
		int hex_data_count,
		enum dc_log_type event,
		const char *msg,
		...)
{
	int i;
	va_list args;
	struct log_entry entry = { 0 };
	enum signal_type signal;

	if (link->local_sink)
		signal = link->local_sink->sink_signal;
	else
		signal = link->connector_signal;

	if (link->type == dc_connection_mst_branch)
		signal = SIGNAL_TYPE_DISPLAY_PORT_MST;

	dm_logger_open(ctx->logger, &entry, event);

	for (i = 0; i < NUM_ELEMENTS(signal_type_info_tbl); i++)
		if (signal == signal_type_info_tbl[i].type)
			break;

	if (i == NUM_ELEMENTS(signal_type_info_tbl))
		goto fail;

	dm_logger_append(&entry, "[%s][ConnIdx:%d] ",
			signal_type_info_tbl[i].name,
			link->link_index);

	va_start(args, msg);
	entry.buf_offset += dm_log_to_buffer(
		&entry.buf[entry.buf_offset],
		LOG_MAX_LINE_SIZE - entry.buf_offset,
		msg, args);

	if (entry.buf[strlen(entry.buf) - 1] == '\n') {
		entry.buf[strlen(entry.buf) - 1] = '\0';
		entry.buf_offset--;
	}

	if (hex_data)
		for (i = 0; i < hex_data_count; i++)
			dm_logger_append(&entry, "%2.2X ", hex_data[i]);

	dm_logger_append(&entry, "^\n");
	dm_helpers_dc_conn_log(ctx, &entry, event);

fail:
	dm_logger_close(&entry);

	va_end(args);
}
