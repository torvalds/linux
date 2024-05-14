/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_HYP_NVHE_CLOCK_H
#define __ARM64_KVM_HYP_NVHE_CLOCK_H
#include <linux/types.h>

#include <asm/kvm_hyp.h>

#ifdef CONFIG_TRACING
void trace_clock_update(struct kvm_nvhe_clock_data *data);
u64 trace_clock(void);
#else
static inline void trace_clock_update(struct kvm_nvhe_clock_data *data) { }
static inline u64 trace_clock(void) { return 0; }
#endif
#endif
