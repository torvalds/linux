#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#include <asm/msr-index.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/ioctl.h>

#define X86_IOC_RDMSR_REGS	_IOWR('c', 0xA0, __u32[8])
#define X86_IOC_WRMSR_REGS	_IOWR('c', 0xA1, __u32[8])

#ifdef __KERNEL__

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/cpumask.h>

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

static inline unsigned long long native_read_tscp(unsigned int *aux)
{
	unsigned long low, high;
	asm volatile(".byte 0x0f,0x01,0xf9"
		     : "=a" (low), "=d" (high), "=c" (*aux));
	return low | ((u64)high << 32);
}

/*
 * both i386 and x86_64 returns 64-bit value in edx:eax, but gcc's "A"
 * constraint has different meanings. For i386, "A" means exactly
 * edx:eax, while for x86_64 it doesn't mean rdx:rax or edx:eax. Instead,
 * it means rax *or* rdx.
 */
#ifdef CONFIG_X86_64
#define DECLARE_ARGS(val, low, high)	unsigned low, high
#define EAX_EDX_VAL(val, low, high)	((low) | ((u64)(high) << 32))
#define EAX_EDX_ARGS(val, low, high)	"a" (low), "d" (high)
#define EAX_EDX_RET(val, low, high)	"=a" (low), "=d" (high)
#else
#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_ARGS(val, low, high)	"A" (val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)
#endif

static inline unsigned long long native_read_msr(unsigned int msr)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdmsr" : EAX_EDX_RET(val, low, high) : "c" (msr));
	return EAX_EDX_VAL(val, low, high);
}

static inline unsigned long long native_read_msr_safe(unsigned int msr,
						      int *err)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("2: rdmsr ; xor %[err],%[err]\n"
		     "1:\n\t"
		     ".section .fixup,\"ax\"\n\t"
		     "3:  mov %[fault],%[err] ; jmp 1b\n\t"
		     ".previous\n\t"
		     _ASM_EXTABLE(2b, 3b)
		     : [err] "=r" (*err), EAX_EDX_RET(val, low, high)
		     : "c" (msr), [fault] "i" (-EIO));
	return EAX_EDX_VAL(val, low, high);
}

static inline void native_write_msr(unsigned int msr,
				    unsigned low, unsigned high)
{
	asm volatile("wrmsr" : : "c" (msr), "a"(low), "d" (high) : "memory");
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
	return err;
}

extern unsigned long long native_read_tsc(void);

extern int rdmsr_safe_regs(u32 regs[8]);
extern int wrmsr_safe_regs(u32 regs[8]);

static __always_inline unsigned long long __native_read_tsc(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

static inline unsigned long long native_read_pmc(int counter)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdpmc" : EAX_EDX_RET(val, low, high) : "c" (counter));
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

#define rdmsr(msr, val1, val2)					\
do {								\
	u64 __val = native_read_msr((msr));			\
	(void)((val1) = (u32)__val);				\
	(void)((val2) = (u32)(__val >> 32));			\
} while (0)

static inline void wrmsr(unsigned msr, unsigned low, unsigned high)
{
	native_write_msr(msr, low, high);
}

#define rdmsrl(msr, val)			\
	((val) = native_read_msr((msr)))

#define wrmsrl(msr, val)						\
	native_write_msr((msr), (u32)((u64)(val)), (u32)((u64)(val) >> 32))

/* wrmsr with exception handling */
static inline int wrmsr_safe(unsigned msr, unsigned low, unsigned high)
{
	return native_write_msr_safe(msr, low, high);
}

/* rdmsr with exception handling */
#define rdmsr_safe(msr, p1, p2)					\
({								\
	int __err;						\
	u64 __val = native_read_msr_safe((msr), &__err);	\
	(*p1) = (u32)__val;					\
	(*p2) = (u32)(__val >> 32);				\
	__err;							\
})

static inline int rdmsrl_safe(unsigned msr, unsigned long long *p)
{
	int err;

	*p = native_read_msr_safe(msr, &err);
	return err;
}

#define rdtscl(low)						\
	((low) = (u32)__native_read_tsc())

#define rdtscll(val)						\
	((val) = __native_read_tsc())

#define rdpmc(counter, low, high)			\
do {							\
	u64 _l = native_read_pmc((counter));		\
	(low)  = (u32)_l;				\
	(high) = (u32)(_l >> 32);			\
} while (0)

#define rdpmcl(counter, val) ((val) = native_read_pmc(counter))

#define rdtscp(low, high, aux)					\
do {                                                            \
	unsigned long long _val = native_read_tscp(&(aux));     \
	(low) = (u32)_val;                                      \
	(high) = (u32)(_val >> 32);                             \
} while (0)

#define rdtscpll(val, aux) (val) = native_read_tscp(&(aux))

#endif	/* !CONFIG_PARAVIRT */

#define wrmsrl_safe(msr, val) wrmsr_safe((msr), (u32)(val),		\
					     (u32)((val) >> 32))

#define write_tsc(val1, val2) wrmsr(MSR_IA32_TSC, (val1), (val2))

#define write_rdtscp_aux(val) wrmsr(MSR_TSC_AUX, (val), 0)

struct msr *msrs_alloc(void);
void msrs_free(struct msr *msrs);

#ifdef CONFIG_SMP
int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
void rdmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr *msrs);
void wrmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr *msrs);
int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
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
static inline int rdmsr_safe_regs_on_cpu(unsigned int cpu, u32 regs[8])
{
	return rdmsr_safe_regs(regs);
}
static inline int wrmsr_safe_regs_on_cpu(unsigned int cpu, u32 regs[8])
{
	return wrmsr_safe_regs(regs);
}
#endif  /* CONFIG_SMP */
#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_MSR_H */
