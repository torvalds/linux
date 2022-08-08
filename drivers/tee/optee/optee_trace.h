/* SPDX-License-Identifier: GPL-2.0 */
/*
 * optee trace points
 *
 * Copyright (C) 2021 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM optee

#if !defined(_TRACE_OPTEE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OPTEE_H

#include <linux/arm-smccc.h>
#include <linux/tracepoint.h>
#include "optee_private.h"

TRACE_EVENT(optee_invoke_fn_begin,
	TP_PROTO(struct optee_rpc_param *param),
	TP_ARGS(param),

	TP_STRUCT__entry(
		__field(void *, param)
		__array(u32, args, 8)
	),

	TP_fast_assign(
		__entry->param = param;
		BUILD_BUG_ON(sizeof(*param) < sizeof(__entry->args));
		memcpy(__entry->args, param, sizeof(__entry->args));
	),

	TP_printk("param=%p (%x, %x, %x, %x, %x, %x, %x, %x)", __entry->param,
		  __entry->args[0], __entry->args[1], __entry->args[2],
		  __entry->args[3], __entry->args[4], __entry->args[5],
		  __entry->args[6], __entry->args[7])
);

TRACE_EVENT(optee_invoke_fn_end,
	TP_PROTO(struct optee_rpc_param *param, struct arm_smccc_res *res),
	TP_ARGS(param, res),

	TP_STRUCT__entry(
		__field(void *, param)
		__array(unsigned long, rets, 4)
	),

	TP_fast_assign(
		__entry->param = param;
		BUILD_BUG_ON(sizeof(*res) < sizeof(__entry->rets));
		memcpy(__entry->rets, res, sizeof(__entry->rets));
	),

	TP_printk("param=%p ret (%lx, %lx, %lx, %lx)", __entry->param,
		  __entry->rets[0], __entry->rets[1], __entry->rets[2],
		  __entry->rets[3])
);
#endif /* _TRACE_OPTEE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE optee_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
