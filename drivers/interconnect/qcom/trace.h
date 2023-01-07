/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM interconnect_qcom

#if !defined(_TRACE_INTERCONNECT_QCOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTERCONNECT_QCOM_H

#include <soc/qcom/tcs.h>
#include <linux/tracepoint.h>

TRACE_EVENT(bcm_voter_commit,

	TP_PROTO(const char *rpmh_state, const struct tcs_cmd *cmd),

	TP_ARGS(rpmh_state, cmd),

	TP_STRUCT__entry(
		__string(state_name, rpmh_state)
		__field(u32, addr)
		__field(u32, data)
		__field(u32, wait)
	),

	TP_fast_assign(
		__assign_str(state_name, rpmh_state);
		__entry->addr = cmd->addr;
		__entry->data = cmd->data;
		__entry->wait = cmd->wait;
	),

	TP_printk("%s cmd_addr=0x%x cmd_data=0x%x cmd_wait=%u",
		  __get_str(state_name),
		  __entry->addr,
		  __entry->data,
		  __entry->wait)
);

#endif /* _TRACE_INTERCONNECT_QCOM_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
