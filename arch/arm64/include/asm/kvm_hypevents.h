/* SPDX-License-Identifier: GPL-2.0 */

#if !defined(__ARM64_KVM_HYPEVENTS_H_) || defined(HYP_EVENT_MULTI_READ)
#define __ARM64_KVM_HYPEVENTS_H_

#ifdef __KVM_NVHE_HYPERVISOR__
#include <nvhe/trace.h>
#endif

#ifndef __HYP_ENTER_EXIT_REASON
#define __HYP_ENTER_EXIT_REASON
enum hyp_enter_exit_reason {
	HYP_REASON_SMC,
	HYP_REASON_HVC,
	HYP_REASON_PSCI,
	HYP_REASON_HOST_ABORT,
	HYP_REASON_GUEST_EXIT,
	HYP_REASON_ERET_HOST,
	HYP_REASON_ERET_GUEST,
	HYP_REASON_UNKNOWN	/* Must be last */
};
#endif

HYP_EVENT(hyp_enter,
	HE_PROTO(struct kvm_cpu_context *host_ctxt, u8 reason),
	HE_STRUCT(
		he_field(u8, reason)
		he_field(pid_t, vcpu)
	),
	HE_ASSIGN(
		__entry->reason = reason;
		__entry->vcpu = __tracing_get_vcpu_pid(host_ctxt);
	),
	HE_PRINTK("reason=%s vcpu=%d", __hyp_enter_exit_reason_str(__entry->reason), __entry->vcpu)
);

HYP_EVENT(hyp_exit,
	HE_PROTO(struct kvm_cpu_context *host_ctxt, u8 reason),
	HE_STRUCT(
		he_field(u8, reason)
		he_field(pid_t, vcpu)
	),
	HE_ASSIGN(
		__entry->reason = reason;
		__entry->vcpu = __tracing_get_vcpu_pid(host_ctxt);
	),
	HE_PRINTK("reason=%s vcpu=%d", __hyp_enter_exit_reason_str(__entry->reason), __entry->vcpu)
);
#endif
