#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#include "msr-index.h"

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/cpumask.h>
#include <uapi/asm/msr.h>

struct msr {
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	};
};

struct msr_info {
	u32 msr_no;
	struct msr reg;
	struct msr *msrs;
	int err;
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
 * both i386 and x86_64 returns 64-bit value in edx:eax, but gcc's "A"
 * constraint has different meanings. For i386, "A" means exactly
 * edx:eax, while for x86_64 it doesn't mean rdx:rax or edx:eax. Instead,
 * it means rax *or* rdx.
 */
#ifdef CONFIG_X86_64
/* Using 64-bit values saves one instruction clearing the high half of low */
#define DECLARE_ARGS(val, low, high)	unsigned long low, high
#define EAX_EDX_VAL(val, low, high)	((low) | (high) << 32)
#define EAX_EDX_RET(val, low, high)	"=a" (low), "=d" (high)
#else
#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)
#endif

#ifdef CONFIG_TRACEPOINTS
/*
 * Be very careful with includes. This header is prone to include loops.
 */
#include <asm/atomic.h>
#include <linux/tracepoint-defs.h>

extern struct tracepoint __tracepoint_read_msr;
extern struct tracepoint __tracepoint_write_msr;
extern struct tracepoint __tracepoint_rdpmc;
#define msr_tracepoint_active(t) static_key_false(&(t).key)
extern void do_trace_write_msr(unsigned msr, u64 val, int failed);
extern void do_trace_read_msr(unsigned msr, u64 val, int failed);
extern void do_trace_rdpmc(unsigned msr, u64 val, int failed);
#else
#define msr_tracepoint_active(t) false
static inline void do_trace_write_msr(unsigned msr, u64 val, int failed) {}
static inline void do_trace_read_msr(unsigned msr, u64 val, int failed) {}
static inline void do_trace_rdpmc(unsigned msr, u64 val, int failed) {}
#endif

static inline unsigned long long native_read_msr(unsigned int msr)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("1: rdmsr\n"
		     "2:\n"
		     _ASM_EXTABLE_HANDLE(1b, 2b, ex_handler_rdmsr_unsafe)
		     : EAX_EDX_RET(val, low, high) : "c" (msr));
	if (msr_tracepoint_active(__tracepoint_read_msr))
		do_trace_read_msr(msr, EAX_EDX_VAL(val, low, high), 0);
	return EAX_EDX_VAL(val, low, high);
}

static inline unsigned long long native_read_msr_safe(unsigned int msr,
						      int *err)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("2: rdmsr ; xor %[err],%[err]\n"
		     "1:\n\t"
		     ".section .fixup,\"ax\"\n\t"
		     "3: mov %[fault],%[err]\n\t"
		     "xorl %%eax, %%eax\n\t"
		     "xorl %%edx, %%edx\n\t"
		     "jmp 1b\n\t"
		     ".previous\n\t"
		     _ASM_EXTABLE(2b, 3b)
		     : [err] "=r" (*err), EAX_EDX_RET(val, low, high)
		     : "c" (msr), [fault] "i" (-EIO));
	if (msr_tracepoint_active(__tracepoint_read_msr))
		do_trace_read_msr(msr, EAX_EDX_VAL(val, low, high), *err);
	return EAX_EDX_VAL(val, low, high);
}

/* Can be uninlined because referenced by paravirt */
static notrace inline void __native_write_msr_notrace(unsigned int msr,
					    unsigned low, unsigned high)
{
	asm volatile("1: wrmsr\n"
		     "2:\n"
		     _ASM_EXTABLE_HANDLE(1b, 2b, ex_handler_wrmsr_unsafe)
		     : : "c" (msr), "a"(low), "d" (high) : "memory");
}

/* Can be uninlined because referenced by paravirt */
static notrace inline void native_write_msr(unsigned int msr,
					    unsigned low, unsigned high)
{
	__native_write_msr_notrace(msr, low, high);
	if (msr_tracepoint_active(__tracepoint_write_msr))
		do_trace_write_msr(msr, ((u64)high << 32 | low), 0);
}

static inline void wrmsr_notrace(unsigned msr, unsigned low, unsigned high)
{
	__native_write_msr_notrace(msr, low, high);
}

/* Can be uninlined because referenced by paravirt */
notrace static inline int native_write_msr_safe(unsigned int msr,
					unsigned low, unsigned high)
{
	int err;
	asm volatile("2: wrmsr ; xor %[err],%[err]\n"
		     "1:\n\t"
		     ".section .fixup,\"ax\"\n\t"
		     "3:  mov %[fault],%[err] ; jmp 1b\n\t"
		     ".previous\n\t"
		     _ASM_EXTABLE(2b, 3b)
		     : [err] "=a" (err)
		     : "c" (msr), "0" (low), "d" (high),
		       [fault] "i" (-EIO)
		     : "memory");
	if (msr_tracepoint_active(__tracepoint_write_msr))
		do_trace_write_msr(msr, ((u64)high << 32 | low), err);
	return err;
}

extern int rdmsr_safe_regs(u32 regs[8]);
extern int wrmsr_safe_regs(u32 regs[8]);

/**
 * rdtsc() - returns the current TSC without ordering constraints
 *
 * rdtsc() returns the result of RDTSC as a 64-bit integer.  The
 * only ordering constraint it supplies is the ordering implied by
 * "asm volatile": it will put the RDTSC in the place you expect.  The
 * CPU can and will speculatively execute that RDTSC, though, so the
 * results can be non-monotonic if compared on different CPUs.
 */
static __always_inline unsigned long long rdtsc(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

/**
 * rdtsc_ordered() - read the current TSC in program order
 *
 * rdtsc_ordered() returns the result of RDTSC as a 64-bit integer.
 * It is ordered like a load to a global in-memory counter.  It should
 * be impossible to observe non-monotonic rdtsc_unordered() behavior
 * across multiple CPUs as long as the TSC is synced.
 */
static __always_inline unsigned long long rdtsc_ordered(void)
{
	/*
	 * The RDTSC instruction is not ordered relative to memory
	 * access.  The Intel SDM and the AMD APM are both vague on this
	 * point, but empirically an RDTSC instruction can be
	 * speculatively executed before prior loads.  An RDTSC
	 * immediately after an appropriate barrier appears to be
	 * ordered as a normal load, that is, it provides the same
	 * ordering guarantees as reading from a global memory location
	 * that some other imaginary CPU is updating continuously with a
	 * time stamp.
	 */
	alternative_2("", "mfence", X86_FEATURE_MFENCE_RDTSC,
			  "lfence", X86_FEATURE_LFENCE_RDTSC);
	return rdtsc();
}

/* Deprecated, keep it for a cycle for easier merging: */
#define rdtscll(now)	do { (now) = rdtsc_ordered(); } while (0)

static inline unsigned long long native_read_pmc(int counter)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdpmc" : EAX_EDX_RET(val, low, high) : "c" (counter));
	if (msr_tracepoint_active(__tracepoint_rdpmc))
		do_trace_rdpmc(counter, EAX_EDX_VAL(val, low, high), 0);
	return EAX_EDX_VAL(val, low, high);
}

#ifdef CONFIG_PARAVIRT
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

static inline void wrmsr(unsigned msr, unsigned low, unsigned high)
{
	native_write_msr(msr, low, high);
}

#define rdmsrl(msr, val)			\
	((val) = native_read_msr((msr)))

static inline void wrmsrl(unsigned msr, u64 val)
{
	native_write_msr(msr, (u32)(val & 0xffffffffULL), (u32)(val >> 32));
}

/* wrmsr with exception handling */
static inline int wrmsr_safe(unsigned msr, unsigned low, unsigned high)
{
	return native_write_msr_safe(msr, low, high);
}

/* rdmsr with exception handling */
#define rdmsr_safe(msr, low, high)				\
({								\
	int __err;						\
	u64 __val = native_read_msr_safe((msr), &__err);	\
	(*low) = (u32)__val;					\
	(*high) = (u32)(__val >> 32);				\
	__err;							\
})

static inline int rdmsrl_safe(unsigned msr, unsigned long long *p)
{
	int err;

	*p = native_read_msr_safe(msr, &err);
	return err;
}

#define rdpmc(counter, low, high)			\
do {							\
	u64 _l = native_read_pmc((counter));		\
	(low)  = (u32)_l;				\
	(high) = (u32)(_l >> 32);			\
} while (0)

#define rdpmcl(counter, val) ((val) = native_read_pmc(counter))

#endif	/* !CONFIG_PARAVIRT */

/*
 * 64-bit version of wrmsr_safe():
 */
static inline int wrmsrl_safe(u32 msr, u64 val)
{
	return wrmsr_safe(msr, (u32)val,  (u32)(val >> 32));
}

#define write_tsc(low, high) wrmsr(MSR_IA32_TSC, (low), (high))

#define write_rdtscp_aux(val) wrmsr(MSR_TSC_AUX, (val), 0)

struct msr *msrs_alloc(void);
void msrs_free(struct msr *msrs);
int msr_set_bit(u32 msr, u8 bit);
int msr_clear_bit(u32 msr, u8 bit);

#ifdef CONFIG_SMP
int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
int rdmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 *q);
int wrmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 q);
void rdmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr *msrs);
void wrmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr *msrs);
int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
int rdmsrl_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 *q);
int wrmsrl_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 q);
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
static inline int rdmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 *q)
{
	rdmsrl(msr_no, *q);
	return 0;
}
static inline int wrmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 q)
{
	wrmsrl(msr_no, q);
	return 0;
}
static inline void rdmsr_on_cpus(const struct cpumask *m, u32 msr_no,
				struct msr *msrs)
{
       rdmsr_on_cpu(0, msr_no, &(msrs[0].l), &(msrs[0].h));
}
static inline void wrmsr_on_cpus(const struct cpumask *m, u32 msr_no,
				struct msr *msrs)
{
       wrmsr_on_cpu(0, msr_no, msrs[0].l, msrs[0].h);
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
static inline int rdmsrl_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 *q)
{
	return rdmsrl_safe(msr_no, q);
}
static inline int wrmsrl_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 q)
{
	return wrmsrl_safe(msr_no, q);
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
#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_MSR_H */
