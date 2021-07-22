// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/debug.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>

#include <asm/elf.h>
#include <abi/reg_ops.h>

struct cpuinfo_csky cpu_data[NR_CPUS];

#ifdef CONFIG_STACKPROTECTOR
#include <linux/stackprotector.h>
unsigned long __stack_chk_guard __read_mostly;
EXPORT_SYMBOL(__stack_chk_guard);
#endif

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

/*
 * Some archs flush debug and FPU info here
 */
void flush_thread(void){}

int copy_thread(unsigned long clone_flags,
		unsigned long usp,
		unsigned long kthread_arg,
		struct task_struct *p,
		unsigned long tls)
{
	struct switch_stack *childstack;
	struct pt_regs *childregs = task_pt_regs(p);

#ifdef CONFIG_CPU_HAS_FPU
	save_to_user_fp(&p->thread.user_fp);
#endif

	childstack = ((struct switch_stack *) childregs) - 1;
	memset(childstack, 0, sizeof(struct switch_stack));

	/* setup thread.sp for switch_to !!! */
	p->thread.sp = (unsigned long)childstack;

	if (unlikely(p->flags & (PF_KTHREAD | PF_IO_WORKER))) {
		memset(childregs, 0, sizeof(struct pt_regs));
		childstack->r15 = (unsigned long) ret_from_kernel_thread;
		childstack->r10 = kthread_arg;
		childstack->r9 = usp;
		childregs->sr = mfcr("psr");
	} else {
		*childregs = *(current_pt_regs());
		if (usp)
			childregs->usp = usp;
		if (clone_flags & CLONE_SETTLS)
			task_thread_info(p)->tp_value = childregs->tls
						      = tls;

		childregs->a0 = 0;
		childstack->r15 = (unsigned long) ret_from_fork;
	}

	return 0;
}

/* Fill in the fpu structure for a core dump.  */
int dump_fpu(struct pt_regs *regs, struct user_fp *fpu)
{
	memcpy(fpu, &current->thread.user_fp, sizeof(*fpu));
	return 1;
}
EXPORT_SYMBOL(dump_fpu);

int dump_task_regs(struct task_struct *tsk, elf_gregset_t *pr_regs)
{
	struct pt_regs *regs = task_pt_regs(tsk);

	/* NOTE: usp is error value. */
	ELF_CORE_COPY_REGS((*pr_regs), regs)

	return 1;
}

#ifndef CONFIG_CPU_PM_NONE
void arch_cpu_idle(void)
{
#ifdef CONFIG_CPU_PM_WAIT
	asm volatile("wait\n");
#endif

#ifdef CONFIG_CPU_PM_DOZE
	asm volatile("doze\n");
#endif

#ifdef CONFIG_CPU_PM_STOP
	asm volatile("stop\n");
#endif
	raw_local_irq_enable();
}
#endif
