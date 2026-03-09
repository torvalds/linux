/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_HYP_NVHE_TRACE_H
#define __ARM64_KVM_HYP_NVHE_TRACE_H

#include <linux/trace_remote_event.h>

#include <asm/kvm_hyptrace.h>

#define HE_PROTO(__args...)	__args
#define HE_ASSIGN(__args...)	__args
#define HE_STRUCT		RE_STRUCT
#define he_field		re_field

#ifdef CONFIG_NVHE_EL2_TRACING

#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)		\
	REMOTE_EVENT_FORMAT(__name, __struct);					\
	extern struct hyp_event_id hyp_event_id_##__name;			\
	static __always_inline void trace_##__name(__proto)			\
	{									\
		struct remote_event_format_##__name *__entry;			\
		size_t length = sizeof(*__entry);				\
										\
		if (!atomic_read(&hyp_event_id_##__name.enabled))		\
			return;							\
		__entry = tracing_reserve_entry(length);			\
		if (!__entry)							\
			return;							\
		__entry->hdr.id = hyp_event_id_##__name.id;			\
		__assign							\
		tracing_commit_entry();						\
	}

void *tracing_reserve_entry(unsigned long length);
void tracing_commit_entry(void);

int __tracing_load(unsigned long desc_va, size_t desc_size);
void __tracing_unload(void);
int __tracing_enable(bool enable);
int __tracing_swap_reader(unsigned int cpu);
void __tracing_update_clock(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc);
int __tracing_reset(unsigned int cpu);
int __tracing_enable_event(unsigned short id, bool enable);
#else
static inline void *tracing_reserve_entry(unsigned long length) { return NULL; }
static inline void tracing_commit_entry(void) { }
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)      \
	static inline void trace_##__name(__proto) {}

static inline int __tracing_load(unsigned long desc_va, size_t desc_size) { return -ENODEV; }
static inline void __tracing_unload(void) { }
static inline int __tracing_enable(bool enable) { return -ENODEV; }
static inline int __tracing_swap_reader(unsigned int cpu) { return -ENODEV; }
static inline void __tracing_update_clock(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc) { }
static inline int __tracing_reset(unsigned int cpu) { return -ENODEV; }
static inline int __tracing_enable_event(unsigned short id, bool enable)  { return -ENODEV; }
#endif
#endif
