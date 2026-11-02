/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_HYP_NVHE_TRACE_H
#define __ARM64_KVM_HYP_NVHE_TRACE_H

#include <linux/trace_remote_event.h>

#include <asm/kvm_hcall.h>
#include <asm/kvm_hyptrace.h>

static inline pid_t __tracing_get_vcpu_pid(struct kvm_cpu_context *host_ctxt)
{
	struct kvm_vcpu *vcpu;

	if (!host_ctxt)
		host_ctxt = host_data_ptr(host_ctxt);

	vcpu = host_ctxt->__hyp_running_vcpu;

	return vcpu ? vcpu->arch.pid : 0;
}

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

/*
 * The trace_hyp_printk boilerplate is too fiddly to be declared with
 * HYP_EVENT():
 *
 * The string format is stored into a kernel-accessible ELF section. The
 * hypervisor only writes the format ID.
 *
 * The function has a variadic prototype. We have no easy way to know each
 * argument width so they must all cast to u64.
 */
#define REMOTE_EVENT_CUSTOM_PRINTK(...)

#define __TO_U64_0()
#define __TO_U64_1(x)			, (u64)(x)
#define __TO_U64_2(x, ...)		, (u64)(x) __TO_U64_1(__VA_ARGS__)
#define __TO_U64_3(x, ...)		, (u64)(x) __TO_U64_2(__VA_ARGS__)
#define __TO_U64_4(x, ...)		, (u64)(x) __TO_U64_3(__VA_ARGS__)
#define __TO_U64_5(x, ...)		, (u64)(x) __TO_U64_4(__VA_ARGS__)
#define __TO_U64_6(x, ...)		, (u64)(x) __TO_U64_5(__VA_ARGS__)
#define __TO_U64_7(x, ...)		, (u64)(x) __TO_U64_6(__VA_ARGS__)
#define __TO_U64_8(x, ...)		, (u64)(x) __TO_U64_7(__VA_ARGS__)

#define __TO_U64_X(N, ...)		CONCATENATE(__TO_U64_, N)(__VA_ARGS__)
#define __TO_U64(...)			__TO_U64_X(COUNT_ARGS(__VA_ARGS__), ##__VA_ARGS__)

REMOTE_EVENT_FORMAT(hyp_printk, HE_STRUCT(he_field(u16, fmt_id) he_field(u64, args[])));
extern struct hyp_event_id hyp_event_id_hyp_printk;

static __always_inline void __trace_hyp_printk(const struct hyp_string_fmt *fmt, int nr_args, ...)
{
	struct remote_event_format_hyp_printk *entry;
	va_list va;
	int i;

	if (!atomic_read(&hyp_event_id_hyp_printk.enabled))
		return;

	entry = tracing_reserve_entry(struct_size(entry, args, nr_args));
	if (!entry)
		return;

	entry->hdr.id = hyp_event_id_hyp_printk.id;
	entry->fmt_id = fmt - __hyp_string_fmts_start;

	va_start(va, nr_args);
	for (i = 0; i < nr_args; i++)
		entry->args[i] = va_arg(va, u64);
	va_end(va);

	tracing_commit_entry();
}

#define trace_hyp_printk(__fmt, __args...)						\
do {											\
	static const struct hyp_string_fmt __used __section("_hyp_string_fmts")		\
	__hyp_str_fmt  = {								\
		.fmt		= __fmt,						\
		.nr_args	= COUNT_ARGS(__args),					\
	};										\
	BUILD_BUG_ON(sizeof(__fmt) > HYP_STRING_FMT_MAX_SIZE);				\
	/* __TO_U64 prepends a comma if there are arguments */				\
	__trace_hyp_printk(&__hyp_str_fmt, COUNT_ARGS(__args) __TO_U64(__args));	\
} while (0)

int __tracing_load(void __kern *desc_va, size_t desc_size);
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
#define REMOTE_EVENT_CUSTOM_PRINTK(...)
#define trace_hyp_printk(fmt, args...) do { } while (0)

static inline int __tracing_load(void __kern *desc_va, size_t desc_size) { return -ENODEV; }
static inline void __tracing_unload(void) { }
static inline int __tracing_enable(bool enable) { return -ENODEV; }
static inline int __tracing_swap_reader(unsigned int cpu) { return -ENODEV; }
static inline void __tracing_update_clock(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc) { }
static inline int __tracing_reset(unsigned int cpu) { return -ENODEV; }
static inline int __tracing_enable_event(unsigned short id, bool enable)  { return -ENODEV; }
#endif
#endif
