/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999, 2000  Niibe Yutaka
 */
#ifndef __ASM_SH_PTRACE_H
#define __ASM_SH_PTRACE_H


#include <linux/stringify.h>
#include <linux/stddef.h>
#include <linux/thread_info.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <uapi/asm/ptrace.h>

#define user_mode(regs)			(((regs)->sr & 0x40000000)==0)
#define kernel_stack_pointer(_regs)	((unsigned long)(_regs)->regs[15])

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->pc;
}
static inline void instruction_pointer_set(struct pt_regs *regs,
		unsigned long val)
{
	regs->pc = val;
}

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return regs->regs[14];
}

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->regs[15];
}

static inline void user_stack_pointer_set(struct pt_regs *regs,
		unsigned long val)
{
	regs->regs[15] = val;
}

#define arch_has_single_step()	(1)

/*
 * kprobe-based event tracer support
 */
struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REGS_OFFSET_NAME(num)	\
	{.name = __stringify(r##num), .offset = offsetof(struct pt_regs, regs[num])}
#define TREGS_OFFSET_NAME(num)	\
	{.name = __stringify(tr##num), .offset = offsetof(struct pt_regs, tregs[num])}
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

extern void ptrace_triggered(struct perf_event *bp,
		      struct perf_sample_data *data, struct pt_regs *regs);

#define task_pt_regs(task) \
	((struct pt_regs *) (task_stack_page(task) + THREAD_SIZE) - 1)

static inline unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = regs->pc;

	if (virt_addr_uncached(pc))
		return CAC_ADDR(pc);

	return pc;
}

#endif /* __ASM_SH_PTRACE_H */
