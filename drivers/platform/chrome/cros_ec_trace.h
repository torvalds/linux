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

DECLARE_EVENT_CLASS(cros_ec_cmd_class,
	TP_PROTO(struct cros_ec_command *cmd),
	TP_ARGS(cmd),
	TP_STRUCT__entry(
		__field(uint32_t, version)
		__field(uint32_t, command)
	),
	TP_fast_assign(
		__entry->version = cmd->version;
		__entry->command = cmd->command;
	),
	TP_printk("version: %u, command: %s", __entry->version,
		  __print_symbolic(__entry->command, EC_CMDS))
);


DEFINE_EVENT(cros_ec_cmd_class, cros_ec_cmd,
	TP_PROTO(struct cros_ec_command *cmd),
	TP_ARGS(cmd)
);


#endif /* _CROS_EC_TRACE_H_ */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cros_ec_trace

#include <trace/define_trace.h>
