/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_HYP_NVHE_CLOCK_H
#define __ARM64_KVM_HYP_NVHE_CLOCK_H
#include <linux/types.h>

#include <asm/kvm_hyp.h>

#ifdef CONFIG_NVHE_EL2_TRACING
void trace_clock_update(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc);
u64 trace_clock(void);
#else
static inline void
trace_clock_update(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc) { }
static inline u64 trace_clock(void) { return 0; }
#endif
#endif
