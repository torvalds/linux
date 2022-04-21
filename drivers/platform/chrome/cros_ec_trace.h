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

#endif /* _CROS_EC_TRACE_H_ */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cros_ec_trace

#include <trace/define_trace.h>
