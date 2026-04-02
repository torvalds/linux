/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(KVM_ARM64_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define KVM_ARM64_TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

#endif /* KVM_ARM64_TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
