// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <asm/elf.h>
#include <asm/proc-fns.h>
#include <asm/fpu.h>
#include <linux/ptrace.h>
#include <linux/reboot.h>

#if IS_ENABLED(CONFIG_LAZY_FPU)
struct task_struct *last_task_used_math;
#endif

extern void setup_mm_for_reboot(char mode);

extern inline void arch_reset(char mode)
{
	if (mode == 's') {
		/* Use cpu handler, jump to 0 */
		cpu_reset(0);
	}
}

void (*pm_power_off) (void);
EXPORT_SYMBOL(pm_power_off);

static char reboot_mode_nds32 = 'h';

int __init reboot_setup(char *str)
{
	reboot_mode_nds32 = str[0];
	return 1;
}

static int cpub_pwroff(void)
{
	return 0;
}

__setup("reboot=", reboot_setup);

void machine_halt(void)
{
	cpub_pwroff();
}

EXPORT_SYMBOL(machine_halt);

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

EXPORT_SYMBOL(machine_power_off);

void machine_restart(char *cmd)
{
	/*
	 * Clean and disable cache, and turn off interrupts
	 */
	cpu_proc_fin();

	/*
	 * Tell the mm system that we are going to reboot -
	 * we may need it to insert some 1:1 mappings so that
	 * soft boot works.
	 */
	setup_mm_for_reboot(reboot_mode_nds32);

	/* Execute kernel restart handler call chain */
	do_kernel_restart(cmd);

	/*
	 * Now call the architecture specific reboot code.
	 */
	arch_reset(reboot_mode_nds32);

	/*
	 * Whoops - the architecture was unable to reboot.
	 * Tell the user!
	 */
	mdelay(1000);
	pr_info("Reboot failed -- System halted\n");
	while (1) ;
}

EXPORT_SYMBOL(machine_restart);

void show_regs(struct pt_regs *regs)
{
	printk("PC is at %pS\n", (void *)instruction_pointer(regs));
	printk("LP is at %pS\n", (void *)regs->lp);
	pr_info("pc : [<%08lx>]    lp : [<%08lx>]    %s\n"
		"sp : %08lx  fp : %08lx  gp : %08lx\n",
		instruction_pointer(regs),
		regs->lp, print_tainted(), regs->sp, regs->fp, regs->gp);
	pr_info("r25: %08lx  r24: %08lx\n", regs->uregs[25], regs->uregs[24]);

	pr_info("r23: %08lx  r22: %08lx  r21: %08lx  r20: %08lx\n",
		regs->uregs[23], regs->uregs[22],
		regs->uregs[21], regs->uregs[20]);
	pr_info("r19: %08lx  r18: %08lx  r17: %08lx  r16: %08lx\n",
		regs->uregs[19], regs->uregs[18],
		regs->uregs[17], regs->uregs[16]);
	pr_info("r15: %08lx  r14: %08lx  r13: %08lx  r12: %08lx\n",
		regs->uregs[15], regs->uregs[14],
		regs->uregs[13], regs->uregs[12]);
	pr_info("r11: %08lx  r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->uregs[11], regs->uregs[10],
		regs->uregs[9], regs->uregs[8]);
	pr_info("r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->uregs[7], regs->uregs[6], regs->uregs[5], regs->uregs[4]);
	pr_info("r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->uregs[3], regs->uregs[2], regs->uregs[1], regs->uregs[0]);
	pr_info("  IRQs o%s  Segment %s\n",
		interrupts_enabled(regs) ? "n" : "ff",
		uaccess_kernel() ? "kernel" : "user");
}

EXPORT_SYMBOL(show_regs);

void exit_thread(struct task_struct *tsk)
{
#if defined(CONFIG_FPU) && defined(CONFIG_LAZY_FPU)
	if (last_task_used_math == tsk)
		last_task_used_math = NULL;
#endif
}

void flush_thread(void)
{
#if defined(CONFIG_FPU)
	clear_fpu(task_pt_regs(current));
	clear_used_math();
# ifdef CONFIG_LAZY_FPU
	if (last_task_used_math == current)
		last_task_used_math = NULL;
# endif
#endif
}

DEFINE_PER_CPU(struct task_struct *, __entry_task);

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");
int copy_thread(unsigned long clone_flags, unsigned long stack_start,
		unsigned long stk_sz, struct task_struct *p, unsigned long tls)
{
	struct pt_regs *childregs = task_pt_regs(p);

	memset(&p->thread.cpu_context, 0, sizeof(struct cpu_context));

	if (unlikely(p->flags & (PF_KTHREAD | PF_IO_WORKER))) {
		memset(childregs, 0, sizeof(struct pt_regs));
		/* kernel thread fn */
		p->thread.cpu_context.r6 = stack_start;
		/* kernel thread argument */
		p->thread.cpu_context.r7 = stk_sz;
	} else {
		*childregs = *current_pt_regs();
		if (stack_start)
			childregs->sp = stack_start;
		/* child get zero as ret. */
		childregs->uregs[0] = 0;
		childregs->osp = 0;
		if (clone_flags & CLONE_SETTLS)
			childregs->uregs[25] = tls;
	}
	/* cpu context switching  */
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
	p->thread.cpu_context.sp = (unsigned long)childregs;

#if IS_ENABLED(CONFIG_FPU)
	if (used_math()) {
# if !IS_ENABLED(CONFIG_LAZY_FPU)
		unlazy_fpu(current);
# else
		preempt_disable();
		if (last_task_used_math == current)
			save_fpu(current);
		preempt_enable();
# endif
		p->thread.fpu = current->thread.fpu;
		clear_fpu(task_pt_regs(p));
		set_stopped_child_used_math(p);
	}
#endif

#ifdef CONFIG_HWZOL
	childregs->lb = 0;
	childregs->le = 0;
	childregs->lc = 0;
#endif

	return 0;
}

#if IS_ENABLED(CONFIG_FPU)
struct task_struct *_switch_fpu(struct task_struct *prev, struct task_struct *next)
{
#if !IS_ENABLED(CONFIG_LAZY_FPU)
	unlazy_fpu(prev);
#endif
	if (!(next->flags & PF_KTHREAD))
		clear_fpu(task_pt_regs(next));
	return prev;
}
#endif

/*
 * fill in the fpe structure for a core dump...
 */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t * fpu)
{
	int fpvalid = 0;
#if IS_ENABLED(CONFIG_FPU)
	struct task_struct *tsk = current;

	fpvalid = tsk_used_math(tsk);
	if (fpvalid) {
		lose_fpu();
		memcpy(fpu, &tsk->thread.fpu, sizeof(*fpu));
	}
#endif
	return fpvalid;
}

EXPORT_SYMBOL(dump_fpu);

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, lr;
	unsigned long stack_start, stack_end;
	int count = 0;

	if (!p || p == current || task_is_running(p))
		return 0;

	if (IS_ENABLED(CONFIG_FRAME_POINTER)) {
		stack_start = (unsigned long)end_of_stack(p);
		stack_end = (unsigned long)task_stack_page(p) + THREAD_SIZE;

		fp = thread_saved_fp(p);
		do {
			if (fp < stack_start || fp > stack_end)
				return 0;
			lr = ((unsigned long *)fp)[0];
			if (!in_sched_functions(lr))
				return lr;
			fp = *(unsigned long *)(fp + 4);
		} while (count++ < 16);
	}
	return 0;
}

EXPORT_SYMBOL(get_wchan);
