/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_HYP_NVHE_TRACE_H
#define __ARM64_KVM_HYP_NVHE_TRACE_H
#include <asm/kvm_hyptrace.h>

#ifdef CONFIG_NVHE_EL2_TRACING
void *tracing_reserve_entry(unsigned long length);
void tracing_commit_entry(void);

int __tracing_load(unsigned long desc_va, size_t desc_size);
void __tracing_unload(void);
int __tracing_enable(bool enable);
int __tracing_swap_reader(unsigned int cpu);
#else
static inline void *tracing_reserve_entry(unsigned long length) { return NULL; }
static inline void tracing_commit_entry(void) { }

static inline int __tracing_load(unsigned long desc_va, size_t desc_size) { return -ENODEV; }
static inline void __tracing_unload(void) { }
static inline int __tracing_enable(bool enable) { return -ENODEV; }
static inline int __tracing_swap_reader(unsigned int cpu) { return -ENODEV; }
#endif
#endif
