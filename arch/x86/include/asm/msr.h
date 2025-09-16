/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#include "msr-index.h"

#ifndef __ASSEMBLER__

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/cpumask.h>
#include <uapi/asm/msr.h>
#include <asm/shared/msr.h>

#include <linux/types.h>
#include <linux/percpu.h>

struct msr_info {
	u32			msr_no;
	struct msr		reg;
	struct msr __percpu	*msrs;
	int			err;
};

struct msr_regs_info {
	u32 *regs;
	int err;
};

struct saved_msr {
	bool valid;
	struct msr_info info;
};

struct saved_msrs {
	unsigned int num;
	struct saved_msr *array;
};

/*
 * Be very careful with includes. This header is prone to include loops.
 */
#include <asm/atomic.h>
#include <linux/tracepoint-defs.h>

#ifdef CONFIG_TRACEPOINTS
DECLARE_TRACEPOINT(read_msr);
DECLARE_TRACEPOINT(write_msr);
DECLARE_TRACEPOINT(rdpmc);
extern void do_trace_write_msr(u32 msr, u64 val, int failed);
extern void do_trace_read_msr(u32 msr, u64 val, int failed);
extern void do_trace_rdpmc(u32 msr, u64 val, int failed);
#else
static inline void do_trace_write_msr(u32 msr, u64 val, int failed) {}
static inline void do_trace_read_msr(u32 msr, u64 val, int failed) {}
static inline void do_trace_rdpmc(u32 msr, u64 val, int failed) {}
#endif

/*
 * __rdmsr() and __wrmsr() are the two primitives which are the bare minimum MSR
 * accessors and should not have any tracing or other functionality piggybacking
 * on them - those are *purely* for accessing MSRs and nothing more. So don't even
 * think of extending them - you will be slapped with a stinking trout or a frozen
 * shark will reach you, wherever you are! You've been warned.
 */
static __always_inline u64 __rdmsr(u32 msr)
{
	EAX_EDX_DECLARE_ARGS(val, low, high);

	asm volatile("1: rdmsr\n"
		     "2:\n"
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_RDMSR)
		     : EAX_EDX_RET(val, low, high) : "c" (msr));

	return EAX_EDX_VAL(val, low, high);
}

static __always_inline void __wrmsrq(u32 msr, u64 val)
{
	asm volatile("1: wrmsr\n"
		     "2:\n"
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_WRMSR)
		     : : "c" (msr), "a" ((u32)val), "d" ((u32)(val >> 32)) : "memory");
}

#define native_rdmsr(msr, val1, val2)			\
do {							\
	u64 __val = __rdmsr((msr));			\
	(void)((val1) = (u32)__val);			\
	(void)((val2) = (u32)(__val >> 32));		\
} while (0)

static __always_inline u64 native_rdmsrq(u32 msr)
{
	return __rdmsr(msr);
}

#define native_wrmsr(msr, low, high)			\
	__wrmsrq((msr), (u64)(high) << 32 | (low))

#define native_wrmsrq(msr, val)				\
	__wrmsrq((msr), (val))

static inline u64 native_read_msr(u32 msr)
{
	u64 val;

	val = __rdmsr(msr);

	if (tracepoint_enabled(read_msr))
		do_trace_read_msr(msr, val, 0);

	return val;
}

static inline int native_read_msr_safe(u32 msr, u64 *p)
{
	int err;
	EAX_EDX_DECLARE_ARGS(val, low, high);

	asm volatile("1: rdmsr ; xor %[err],%[err]\n"
		     "2:\n\t"
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_RDMSR_SAFE, %[err])
		     : [err] "=r" (err), EAX_EDX_RET(val, low, high)
		     : "c" (msr));
	if (tracepoint_enabled(read_msr))
		do_trace_read_msr(msr, EAX_EDX_VAL(val, low, high), err);

	*p = EAX_EDX_VAL(val, low, high);

	return err;
}

/* Can be uninlined because referenced by paravirt */
static inline void notrace native_write_msr(u32 msr, u64 val)
{
	native_wrmsrq(msr, val);

	if (tracepoint_enabled(write_msr))
		do_trace_write_msr(msr, val, 0);
}

/* Can be uninlined because referenced by paravirt */
static inline int notrace native_write_msr_safe(u32 msr, u64 val)
{
	int err;

	asm volatile("1: wrmsr ; xor %[err],%[err]\n"
		     "2:\n\t"
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_WRMSR_SAFE, %[err])
		     : [err] "=a" (err)
		     : "c" (msr), "0" ((u32)val), "d" ((u32)(val >> 32))
		     : "memory");
	if (tracepoint_enabled(write_msr))
		do_trace_write_msr(msr, val, err);
	return err;
}

extern int rdmsr_safe_regs(u32 regs[8]);
extern int wrmsr_safe_regs(u32 regs[8]);

static inline u64 native_read_pmc(int counter)
{
	EAX_EDX_DECLARE_ARGS(val, low, high);

	asm volatile("rdpmc" : EAX_EDX_RET(val, low, high) : "c" (counter));
	if (tracepoint_enabled(rdpmc))
		do_trace_rdpmc(counter, EAX_EDX_VAL(val, low, high), 0);
	return EAX_EDX_VAL(val, low, high);
}

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/paravirt.h>
#else
#include <linux/errno.h>
/*
 * Access to machine-specific registers (available on 586 and better only)
 * Note: the rd* operations modify the parameters directly (without using
 * pointer indirection), this allows gcc to optimize better
 */

#define rdmsr(msr, low, high)					\
do {								\
	u64 __val = native_read_msr((msr));			\
	(void)((low) = (u32)__val);				\
	(void)((high) = (u32)(__val >> 32));			\
} while (0)

static inline void wrmsr(u32 msr, u32 low, u32 high)
{
	native_write_msr(msr, (u64)high << 32 | low);
}

#define rdmsrq(msr, val)			\
	((val) = native_read_msr((msr)))

static inline void wrmsrq(u32 msr, u64 val)
{
	native_write_msr(msr, val);
}

/* wrmsr with exception handling */
static inline int wrmsrq_safe(u32 msr, u64 val)
{
	return native_write_msr_safe(msr, val);
}

/* rdmsr with exception handling */
#define rdmsr_safe(msr, low, high)				\
({								\
	u64 __val;						\
	int __err = native_read_msr_safe((msr), &__val);	\
	(*low) = (u32)__val;					\
	(*high) = (u32)(__val >> 32);				\
	__err;							\
})

static inline int rdmsrq_safe(u32 msr, u64 *p)
{
	return native_read_msr_safe(msr, p);
}

static __always_inline u64 rdpmc(int counter)
{
	return native_read_pmc(counter);
}

#endif	/* !CONFIG_PARAVIRT_XXL */

/* Instruction opcode for WRMSRNS supported in binutils >= 2.40 */
#define ASM_WRMSRNS _ASM_BYTES(0x0f,0x01,0xc6)

/* Non-serializing WRMSR, when available.  Falls back to a serializing WRMSR. */
static __always_inline void wrmsrns(u32 msr, u64 val)
{
	/*
	 * WRMSR is 2 bytes.  WRMSRNS is 3 bytes.  Pad WRMSR with a redundant
	 * DS prefix to avoid a trailing NOP.
	 */
	asm volatile("1: " ALTERNATIVE("ds wrmsr", ASM_WRMSRNS, X86_FEATURE_WRMSRNS)
		     "2: " _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_WRMSR)
		     : : "c" (msr), "a" ((u32)val), "d" ((u32)(val >> 32)));
}

/*
 * Dual u32 version of wrmsrq_safe():
 */
static inline int wrmsr_safe(u32 msr, u32 low, u32 high)
{
	return wrmsrq_safe(msr, (u64)high << 32 | low);
}

struct msr __percpu *msrs_alloc(void);
void msrs_free(struct msr __percpu *msrs);
int msr_set_bit(u32 msr, u8 bit);
int msr_clear_bit(u32 msr, u8 bit);

#ifdef CONFIG_SMP
int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
int rdmsrq_on_cpu(unsigned int cpu, u32 msr_no, u64 *q);
int wrmsrq_on_cpu(unsigned int cpu, u32 msr_no, u64 q);
void rdmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr __percpu *msrs);
void wrmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr __percpu *msrs);
int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
int rdmsrq_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 *q);
int wrmsrq_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 q);
int rdmsr_safe_regs_on_cpu(unsigned int cpu, u32 regs[8]);
int wrmsr_safe_regs_on_cpu(unsigned int cpu, u32 regs[8]);
#else  /*  CONFIG_SMP  */
static inline int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	rdmsr(msr_no, *l, *h);
	return 0;
}
static inline int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	wrmsr(msr_no, l, h);
	return 0;
}
static inline int rdmsrq_on_cpu(unsigned int cpu, u32 msr_no, u64 *q)
{
	rdmsrq(msr_no, *q);
	return 0;
}
static inline int wrmsrq_on_cpu(unsigned int cpu, u32 msr_no, u64 q)
{
	wrmsrq(msr_no, q);
	return 0;
}
static inline void rdmsr_on_cpus(const struct cpumask *m, u32 msr_no,
				struct msr __percpu *msrs)
{
	rdmsr_on_cpu(0, msr_no, raw_cpu_ptr(&msrs->l), raw_cpu_ptr(&msrs->h));
}
static inline void wrmsr_on_cpus(const struct cpumask *m, u32 msr_no,
				struct msr __percpu *msrs)
{
	wrmsr_on_cpu(0, msr_no, raw_cpu_read(msrs->l), raw_cpu_read(msrs->h));
}
static inline int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no,
				    u32 *l, u32 *h)
{
	return rdmsr_safe(msr_no, l, h);
}
static inline int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	return wrmsr_safe(msr_no, l, h);
}
static inline int rdmsrq_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 *q)
{
	return rdmsrq_safe(msr_no, q);
}
static inline int wrmsrq_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 q)
{
	return wrmsrq_safe(msr_no, q);
}
static inline int rdmsr_safe_regs_on_cpu(unsigned int cpu, u32 regs[8])
{
	return rdmsr_safe_regs(regs);
}
static inline int wrmsr_safe_regs_on_cpu(unsigned int cpu, u32 regs[8])
{
	return wrmsr_safe_regs(regs);
}
#endif  /* CONFIG_SMP */

/* Compatibility wrappers: */
#define rdmsrl(msr, val) rdmsrq(msr, val)
#define wrmsrl(msr, val) wrmsrq(msr, val)
#define rdmsrl_on_cpu(cpu, msr, q) rdmsrq_on_cpu(cpu, msr, q)

#endif /* __ASSEMBLER__ */
#endif /* _ASM_X86_MSR_H */
