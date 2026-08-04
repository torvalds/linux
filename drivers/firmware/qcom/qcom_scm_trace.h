/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_scm

#if !defined(_TRACE_SCM_SMC_INTERFACE_H) || defined(TRACE_HEADER_MULTI_READ)

#define _TRACE_SCM_SMC_INTERFACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(scm_smc_request,

	TP_PROTO(unsigned long a0, const struct arm_smccc_args *smc),

	TP_ARGS(a0, smc),

	TP_STRUCT__entry(
		__field(u64, smc_id)
		__field(u8, svc_id)
		__field(u8, cmd_id)
		__field(u8, args_cnt)
		__dynamic_array(unsigned long, args,
				min_t(u8, (smc->args[1] & 0xF), (u8)6))
	),

	TP_fast_assign(
		__entry->smc_id = a0;
		__entry->svc_id = (smc->args[0] >> 8) & 0xFF;
		__entry->cmd_id = smc->args[0] & 0xFF;
		u8 n = min_t(u8, (smc->args[1] & 0xF), (u8)6);

		__entry->args_cnt = n;

		unsigned long *dst = __get_dynamic_array(args);

		for (int i = 0; i < n; i++)
			dst[i] = smc->args[2 + i];
	),

	TP_printk("smc_id:0x%08llx svc_id:0x%02x cmd_id:0x%02x args_cnt:%u args:%s",
		  __entry->smc_id, __entry->svc_id, __entry->cmd_id, __entry->args_cnt,
		  __print_dynamic_array(args, sizeof(unsigned long)))
);

TRACE_EVENT(scm_waitq_sleep,

	TP_PROTO(u32 wq_ctx, u32 smc_ctx),

	TP_ARGS(wq_ctx, smc_ctx),

	TP_STRUCT__entry(
		__field(u32, wq_ctx)
		__field(u32, smc_call_ctx)
	),

	TP_fast_assign(
		__entry->wq_ctx = wq_ctx;
		__entry->smc_call_ctx = smc_ctx;
	),

	TP_printk("wq_ctx:%u, smc_call_ctx:%u", __entry->wq_ctx, __entry->smc_call_ctx)
);

TRACE_EVENT(scm_waitq_resume,

	TP_PROTO(u32 smc_ctx),

	TP_ARGS(smc_ctx),

	TP_STRUCT__entry(
		__field(u32, smc_call_ctx)
	),

	TP_fast_assign(
		__entry->smc_call_ctx = smc_ctx;
	),

	TP_printk("smc_call_ctx:%u", __entry->smc_call_ctx)
);

TRACE_EVENT(scm_waitq_get_wq_ctx,

	TP_PROTO(u32 wq_ctx, u32 flags, u32 pending),

	TP_ARGS(wq_ctx, flags, pending),

	TP_STRUCT__entry(
		__field(u32, wq_ctx)
		__field(u32, flags)
		__field(u32, more_pending)
	),

	TP_fast_assign(
		__entry->wq_ctx = wq_ctx;
		__entry->flags = flags;
		__entry->more_pending = pending;
	),

	TP_printk("wq_ctx:%u, flags:%u, more_pending:%u",
		  __entry->wq_ctx, __entry->flags, __entry->more_pending)
);

TRACE_EVENT(scm_smc_done,

	TP_PROTO(int ret, u64 smc_id, struct arm_smccc_res *smc_res),

	TP_ARGS(ret, smc_id, smc_res),

	TP_STRUCT__entry(
		__field(int, ret)
		__field(u64, smc_id)
		__field(unsigned long, res)
		__field(unsigned long, res0)
		__field(unsigned long, res1)
		__field(unsigned long, res2)
	),

	TP_fast_assign(
		__entry->ret = ret;
		__entry->smc_id = smc_id;
		__entry->res = smc_res->a0;
		__entry->res0 = smc_res->a1;
		__entry->res1 = smc_res->a2;
		__entry->res2 = smc_res->a3;
	),

	TP_printk("smc_id:0x%08llx, ret:%d res_to_callee:0x%lx res0:0x%lx res1:0x%lx res2:0x%lx",
		  __entry->smc_id, __entry->ret, __entry->res,
		  __entry->res0, __entry->res1, __entry->res2)
);

#endif /* _TRACE_SCM_SMC_INTERFACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE qcom_scm_trace

#include <trace/define_trace.h>

