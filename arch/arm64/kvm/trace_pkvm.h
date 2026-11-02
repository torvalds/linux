/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_PKVM_ARM64_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PKVM_ARM64_KVM_H

#include <linux/tracepoint.h>
#include <asm/kvm_pkvm.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

TRACE_DEFINE_ENUM(PKVM_HYP_NO_REQ);
TRACE_DEFINE_ENUM(PKVM_HYP_REQ_HYP_ALLOC);

#define PKVM_HYP_REQ_TYPES				\
	{ PKVM_HYP_NO_REQ, "NO_REQ" },			\
	{ PKVM_HYP_REQ_HYP_ALLOC, "HYP_ALLOC" }

TRACE_EVENT(kvm_handle_pkvm_hyp_req,
	TP_PROTO(struct pkvm_hyp_req *req, int ret),
	TP_ARGS(req, ret),

	TP_STRUCT__entry(
		__field(u8,	type)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->type = req->type;
		__entry->ret = ret;
	),

	TP_printk("type: %s ret: %d",
		  __print_symbolic(__entry->type, PKVM_HYP_REQ_TYPES),
		  __entry->ret)
);

#endif /* _TRACE_PKVM_ARM64_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_pkvm

/* This part must be outside protection */
#include <trace/define_trace.h>
