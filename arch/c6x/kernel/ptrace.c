// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Updated for 2.6.34: Mark Salter <msalter@redhat.com>
 */
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/sched/task_stack.h>

#include <asm/cacheflush.h>

#define PT_REG_SIZE	  (sizeof(struct pt_regs))

/*
 * Called by kernel/ptrace.c when detaching.
 */
void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

/*
 * Get a register number from live pt_regs for the specified task.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	long *addr = (long *)task_pt_regs(task);

	if (regno == PT_TSR || regno == PT_CSR)
		return 0;

	return addr[regno];
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task,
			  int regno,
			  unsigned long data)
{
	unsigned long *addr = (unsigned long *)task_pt_regs(task);

	if (regno != PT_TSR && regno != PT_CSR)
		addr[regno] = data;

	return 0;
}

/* regset get/set implementations */

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	return membuf_write(&to, task_pt_regs(target), sizeof(struct pt_regs));
}

enum c6x_regset {
	REGSET_GPR,
};

static const struct user_regset c6x_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(u32),
		.align = sizeof(u32),
		.regset_get = gpr_get,
	},
};

static const struct user_regset_view user_c6x_native_view = {
	.name		= "tic6x",
	.e_machine	= EM_TI_C6000,
	.regsets	= c6x_regsets,
	.n		= ARRAY_SIZE(c6x_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_c6x_native_view;
}

/*
 * Perform ptrace request
 */
long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = 0;

	switch (request) {
		/*
		 * write the word at location addr.
		 */
	case PTRACE_POKETEXT:
		ret = generic_ptrace_pokedata(child, addr, data);
		if (ret == 0 && request == PTRACE_POKETEXT)
			flush_icache_range(addr, addr + 4);
		break;
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

/*
 * handle tracing of system call entry
 * - return the revised system call number or ULONG_MAX to cause ENOSYS
 */
asmlinkage unsigned long syscall_trace_entry(struct pt_regs *regs)
{
	if (tracehook_report_syscall_entry(regs))
		/* tracing decided this syscall should not happen, so
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in
		 * regs->orig_a4
		 */
		return ULONG_MAX;

	return regs->b0;
}

/*
 * handle tracing of system call exit
 */
asmlinkage void syscall_trace_exit(struct pt_regs *regs)
{
	tracehook_report_syscall_exit(regs, 0);
}
