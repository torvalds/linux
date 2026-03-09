/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_HYP_TRACE_H__
#define __ARM64_KVM_HYP_TRACE_H__

#ifdef CONFIG_NVHE_EL2_TRACING
int kvm_hyp_trace_init(void);
#else
static inline int kvm_hyp_trace_init(void) { return 0; }
#endif
#endif
