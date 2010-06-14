#ifndef __ASM_SH_PTRACE_H
#define __ASM_SH_PTRACE_H

#include <linux/stringify.h>

/*
 * Copyright (C) 1999, 2000  Niibe Yutaka
 *
 */
#if defined(__SH5__)
struct pt_regs {
	unsigned long long pc;
	unsigned long long sr;
	long long syscall_nr;
	unsigned long long regs[63];
	unsigned long long tregs[8];
	unsigned long long pad[2];
};

#define MAX_REG_OFFSET		offsetof(struct pt_regs, tregs[7])
#define regs_return_value(regs)	((regs)->regs[3])

#define TREGS_OFFSET_NAME(num)	\
	{.name = __stringify(tr##num), .offset = offsetof(struct pt_regs, tregs[num])}

#else
/*
 * GCC defines register number like this:
 * -----------------------------
 *	 0 - 15 are integer registers
 *	17 - 22 are control/special registers
 *	24 - 39 fp registers
 *	40 - 47 xd registers
 *	48 -    fpscr register
 * -----------------------------
 *
 * We follows above, except:
 *	16 --- program counter (PC)
 *	22 --- syscall #
 *	23 --- floating point communication register
 */
#define REG_REG0	 0
#define REG_REG15	15

#define REG_PC		16

#define REG_PR		17
#define REG_SR		18
#define REG_GBR		19
#define REG_MACH	20
#define REG_MACL	21

#define REG_SYSCALL	22

#define REG_FPREG0	23
#define REG_FPREG15	38
#define REG_XFREG0	39
#define REG_XFREG15	54

#define REG_FPSCR	55
#define REG_FPUL	56

/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_regs {
	unsigned long regs[16];
	unsigned long pc;
	unsigned long pr;
	unsigned long sr;
	unsigned long gbr;
	unsigned long mach;
	unsigned long macl;
	long tra;
};

#define MAX_REG_OFFSET		offsetof(struct pt_regs, tra)
#define regs_return_value(regs)	((regs)->regs[0])

/*
 * This struct defines the way the DSP registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_dspregs {
	unsigned long	a1;
	unsigned long	a0g;
	unsigned long	a1g;
	unsigned long	m0;
	unsigned long	m1;
	unsigned long	a0;
	unsigned long	x0;
	unsigned long	x1;
	unsigned long	y0;
	unsigned long	y1;
	unsigned long	dsr;
	unsigned long	rs;
	unsigned long	re;
	unsigned long	mod;
};
#endif

#define PTRACE_GETREGS		12	/* General registers */
#define PTRACE_SETREGS		13

#define PTRACE_GETFPREGS	14	/* FPU registers */
#define PTRACE_SETFPREGS	15

#define PTRACE_GETFDPIC		31	/* get the ELF fdpic loadmap address */

#define PTRACE_GETFDPIC_EXEC	0	/* [addr] request the executable loadmap */
#define PTRACE_GETFDPIC_INTERP	1	/* [addr] request the interpreter loadmap */

#define	PTRACE_GETDSPREGS	55	/* DSP registers */
#define	PTRACE_SETDSPREGS	56

#define PT_TEXT_END_ADDR	240
#define PT_TEXT_ADDR		244	/* &(struct user)->start_code */
#define PT_DATA_ADDR		248	/* &(struct user)->start_data */
#define PT_TEXT_LEN		252

#ifdef __KERNEL__
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/system.h>

#define user_mode(regs)			(((regs)->sr & 0x40000000)==0)
#define user_stack_pointer(regs)	((unsigned long)(regs)->regs[15])
#define kernel_stack_pointer(regs)	((unsigned long)(regs)->regs[15])
#define instruction_pointer(regs)	((unsigned long)(regs)->pc)

extern void show_regs(struct pt_regs *);

#define arch_has_single_step()	(1)

/*
 * kprobe-based event tracer support
 */
#include <linux/stddef.h>
#include <linux/thread_info.h>

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REGS_OFFSET_NAME(num)	\
	{.name = __stringify(r##num), .offset = offsetof(struct pt_regs, regs[num])}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

/* Query offset/name of register from its name/offset */
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);

extern const struct pt_regs_offset regoffset_table[];

/**
 * regs_get_register() - get register value from its offset
 * @regs:	pt_regs from which register value is gotten.
 * @offset:	offset number of the register.
 *
 * regs_get_register returns the value of a register. The @offset is the
 * offset of the register in struct pt_regs address which specified by @regs.
 * If @offset is bigger than MAX_REG_OFFSET, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;
	return *(unsigned long *)((unsigned long)regs + offset);
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @addr:	address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
static inline int regs_within_kernel_stack(struct pt_regs *regs,
					   unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1)));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						      unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);
	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

struct perf_event;
struct perf_sample_data;

extern void ptrace_triggered(struct perf_event *bp, int nmi,
		      struct perf_sample_data *data, struct pt_regs *regs);

#define task_pt_regs(task) \
	((struct pt_regs *) (task_stack_page(task) + THREAD_SIZE) - 1)

static inline unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (virt_addr_uncached(pc))
		return CAC_ADDR(pc);

	return pc;
}
#endif /* __KERNEL__ */

#endif /* __ASM_SH_PTRACE_H */
