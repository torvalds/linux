/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace events for the ChromeOS Embedded Controller
 *
 * Copyright 2019 Google LLC.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cros_ec

#if !defined(_CROS_EC_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _CROS_EC_TRACE_H_

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_sensorhub.h>

#include <linux/tracepoint.h>

TRACE_EVENT(cros_ec_request_start,
	TP_PROTO(struct cros_ec_command *cmd),
	TP_ARGS(cmd),
	TP_STRUCT__entry(
		__field(uint32_t, version)
		__field(uint32_t, offset)
		__field(uint32_t, command)
		__field(uint32_t, outsize)
		__field(uint32_t, insize)
	),
	TP_fast_assign(
		__entry->version = cmd->version;
		__entry->offset = cmd->command / EC_CMD_PASSTHRU_OFFSET(1);
		__entry->command = cmd->command % EC_CMD_PASSTHRU_OFFSET(1);
		__entry->outsize = cmd->outsize;
		__entry->insize = cmd->insize;
	),
	TP_printk("version: %u, offset: %d, command: %s, outsize: %u, insize: %u",
		  __entry->version, __entry->offset,
		  __print_symbolic(__entry->command, EC_CMDS),
		  __entry->outsize, __entry->insize)
);

TRACE_EVENT(cros_ec_request_done,
	TP_PROTO(struct cros_ec_command *cmd, int retval),
	TP_ARGS(cmd, retval),
	TP_STRUCT__entry(
		__field(uint32_t, version)
		__field(uint32_t, offset)
		__field(uint32_t, command)
		__field(uint32_t, outsize)
		__field(uint32_t, insize)
		__field(uint32_t, result)
		__field(int, retval)
	),
	TP_fast_assign(
		__entry->version = cmd->version;
		__entry->offset = cmd->command / EC_CMD_PASSTHRU_OFFSET(1);
		__entry->command = cmd->command % EC_CMD_PASSTHRU_OFFSET(1);
		__entry->outsize = cmd->outsize;
		__entry->insize = cmd->insize;
		__entry->result = cmd->result;
		__entry->retval = retval;
	),
	TP_printk("version: %u, offset: %d, command: %s, outsize: %u, insize: %u, ec result: %s, retval: %u",
		  __entry->version, __entry->offset,
		  __print_symbolic(__entry->command, EC_CMDS),
		  __entry->outsize, __entry->insize,
		  __print_symbolic(__entry->result, EC_RESULT),
		  __entry->retval)
);

TRACE_EVENT(cros_ec_sensorhub_timestamp,
	    TP_PROTO(u32 ec_sample_timestamp, u32 ec_fifo_timestamp, s64 fifo_timestamp,
		     s64 current_timestamp, s64 current_time),
	TP_ARGS(ec_sample_timestamp, ec_fifo_timestamp, fifo_timestamp, current_timestamp,
		current_time),
	TP_STRUCT__entry(
		__field(u32, ec_sample_timestamp)
		__field(u32, ec_fifo_timestamp)
		__field(s64, fifo_timestamp)
		__field(s64, current_timestamp)
		__field(s64, current_time)
		__field(s64, delta)
	),
	TP_fast_assign(
		__entry->ec_sample_timestamp = ec_sample_timestamp;
		__entry->ec_fifo_timestamp = ec_fifo_timestamp;
		__entry->fifo_timestamp = fifo_timestamp;
		__entry->current_timestamp = current_timestamp;
		__entry->current_time = current_time;
		__entry->delta = current_timestamp - current_time;
	),
	TP_printk("ec_ts: %9u, ec_fifo_ts: %9u, fifo_ts: %12lld, curr_ts: %12lld, curr_time: %12lld, delta %12lld",
		  __entry->ec_sample_timestamp,
		__entry->ec_fifo_timestamp,
		__entry->fifo_timestamp,
		__entry->current_timestamp,
		__entry->current_time,
		__entry->delta
	)
);

TRACE_EVENT(cros_ec_sensorhub_data,
	    TP_PROTO(u32 ec_sensor_num, u32 ec_fifo_timestamp, s64 fifo_timestamp,
		     s64 current_timestamp, s64 current_time),
	TP_ARGS(ec_sensor_num, ec_fifo_timestamp, fifo_timestamp, current_timestamp, current_time),
	TP_STRUCT__entry(
		__field(u32, ec_sensor_num)
		__field(u32, ec_fifo_timestamp)
		__field(s64, fifo_timestamp)
		__field(s64, current_timestamp)
		__field(s64, current_time)
		__field(s64, delta)
	),
	TP_fast_assign(
		__entry->ec_sensor_num = ec_sensor_num;
		__entry->ec_fifo_timestamp = ec_fifo_timestamp;
		__entry->fifo_timestamp = fifo_timestamp;
		__entry->current_timestamp = current_timestamp;
		__entry->current_time = current_time;
		__entry->delta = current_timestamp - current_time;
	),
	TP_printk("ec_num: %4u, ec_fifo_ts: %9u, fifo_ts: %12lld, curr_ts: %12lld, curr_time: %12lld, delta %12lld",
		  __entry->ec_sensor_num,
		__entry->ec_fifo_timestamp,
		__entry->fifo_timestamp,
		__entry->current_timestamp,
		__entry->current_time,
		__entry->delta
	)
);

TRACE_EVENT(cros_ec_sensorhub_filter,
	    TP_PROTO(struct cros_ec_sensors_ts_filter_state *state, s64 dx, s64 dy),
	TP_ARGS(state, dx, dy),
	TP_STRUCT__entry(
		__field(s64, dx)
		__field(s64, dy)
		__field(s64, median_m)
		__field(s64, median_error)
		__field(s64, history_len)
		__field(s64, x)
		__field(s64, y)
	),
	TP_fast_assign(
		__entry->dx = dx;
		__entry->dy = dy;
		__entry->median_m = state->median_m;
		__entry->median_error = state->median_error;
		__entry->history_len = state->history_len;
		__entry->x = state->x_offset;
		__entry->y = state->y_offset;
	),
	TP_printk("dx: %12lld. dy: %12lld median_m: %12lld median_error: %12lld len: %lld x: %12lld y: %12lld",
		  __entry->dx,
		__entry->dy,
		__entry->median_m,
		__entry->median_error,
		__entry->history_len,
		__entry->x,
		__entry->y
	)
);


#endif /* _CROS_EC_TRACE_H_ */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cros_ec_trace

#include <trace/define_trace.h>
