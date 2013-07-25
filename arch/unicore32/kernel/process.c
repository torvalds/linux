/*
 * linux/arch/unicore32/kernel/process.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdarg.h>

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/elfcore.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/gpio.h>
#include <linux/stacktrace.h>

#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/stacktrace.h>

#include "setup.h"

static const char * const processor_modes[] = {
	"UK00", "UK01", "UK02", "UK03", "UK04", "UK05", "UK06", "UK07",
	"UK08", "UK09", "UK0A", "UK0B", "UK0C", "UK0D", "UK0E", "UK0F",
	"USER", "REAL", "INTR", "PRIV", "UK14", "UK15", "UK16", "ABRT",
	"UK18", "UK19", "UK1A", "EXTN", "UK1C", "UK1D", "UK1E", "SUSR"
};

void arch_cpu_idle(void)
{
	cpu_do_idle();
	local_irq_enable();
}

void machine_halt(void)
{
	gpio_set_value(GPO_SOFT_OFF, 0);
}

/*
 * Function pointers to optional machine specific functions
 */
void (*pm_power_off)(void) = NULL;

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
	machine_halt();
}

void machine_restart(char *cmd)
{
	/* Disable interrupts first */
	local_irq_disable();

	/*
	 * Tell the mm system that we are going to reboot -
	 * we may need it to insert some 1:1 mappings so that
	 * soft boot works.
	 */
	setup_mm_for_reboot();

	/* Clean and invalidate caches */
	flush_cache_all();

	/* Turn off caching */
	cpu_proc_fin();

	/* Push out any further dirty data, and ensure cache is empty */
	flush_cache_all();

	/*
	 * Now handle reboot code.
	 */
	if (reboot_mode == REBOOT_SOFT) {
		/* Jump into ROM at address 0xffff0000 */
		cpu_reset(VECTORS_BASE);
	} else {
		writel(0x00002001, PM_PLLSYSCFG); /* cpu clk = 250M */
		writel(0x00100800, PM_PLLDDRCFG); /* ddr clk =  44M */
		writel(0x00002001, PM_PLLVGACFG); /* vga clk = 250M */

		/* Use on-chip reset capability */
		/* following instructions must be in one icache line */
		__asm__ __volatile__(
			"	.align 5\n\t"
			"	stw	%1, [%0]\n\t"
			"201:	ldw	r0, [%0]\n\t"
			"	cmpsub.a	r0, #0\n\t"
			"	bne	201b\n\t"
			"	stw	%3, [%2]\n\t"
			"	nop; nop; nop\n\t"
			/* prefetch 3 instructions at most */
			:
			: "r" (PM_PMCR),
			  "r" (PM_PMCR_CFBSYS | PM_PMCR_CFBDDR
				| PM_PMCR_CFBVGA),
			  "r" (RESETC_SWRR),
			  "r" (RESETC_SWRR_SRB)
			: "r0", "memory");
	}

	/*
	 * Whoops - the architecture was unable to reboot.
	 * Tell the user!
	 */
	mdelay(1000);
	printk(KERN_EMERG "Reboot failed -- System halted\n");
	do { } while (1);
}

void __show_regs(struct pt_regs *regs)
{
	unsigned long flags;
	char buf[64];

	show_regs_print_info(KERN_DEFAULT);
	print_symbol("PC is at %s\n", instruction_pointer(regs));
	print_symbol("LR is at %s\n", regs->UCreg_lr);
	printk(KERN_DEFAULT "pc : [<%08lx>]    lr : [<%08lx>]    psr: %08lx\n"
	       "sp : %08lx  ip : %08lx  fp : %08lx\n",
		regs->UCreg_pc, regs->UCreg_lr, regs->UCreg_asr,
		regs->UCreg_sp, regs->UCreg_ip, regs->UCreg_fp);
	printk(KERN_DEFAULT "r26: %08lx  r25: %08lx  r24: %08lx\n",
		regs->UCreg_26, regs->UCreg_25,
		regs->UCreg_24);
	printk(KERN_DEFAULT "r23: %08lx  r22: %08lx  r21: %08lx  r20: %08lx\n",
		regs->UCreg_23, regs->UCreg_22,
		regs->UCreg_21, regs->UCreg_20);
	printk(KERN_DEFAULT "r19: %08lx  r18: %08lx  r17: %08lx  r16: %08lx\n",
		regs->UCreg_19, regs->UCreg_18,
		regs->UCreg_17, regs->UCreg_16);
	printk(KERN_DEFAULT "r15: %08lx  r14: %08lx  r13: %08lx  r12: %08lx\n",
		regs->UCreg_15, regs->UCreg_14,
		regs->UCreg_13, regs->UCreg_12);
	printk(KERN_DEFAULT "r11: %08lx  r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->UCreg_11, regs->UCreg_10,
		regs->UCreg_09, regs->UCreg_08);
	printk(KERN_DEFAULT "r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->UCreg_07, regs->UCreg_06,
		regs->UCreg_05, regs->UCreg_04);
	printk(KERN_DEFAULT "r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->UCreg_03, regs->UCreg_02,
		regs->UCreg_01, regs->UCreg_00);

	flags = regs->UCreg_asr;
	buf[0] = flags & PSR_S_BIT ? 'S' : 's';
	buf[1] = flags & PSR_Z_BIT ? 'Z' : 'z';
	buf[2] = flags & PSR_C_BIT ? 'C' : 'c';
	buf[3] = flags & PSR_V_BIT ? 'V' : 'v';
	buf[4] = '\0';

	printk(KERN_DEFAULT "Flags: %s  INTR o%s  REAL o%s  Mode %s  Segment %s\n",
		buf, interrupts_enabled(regs) ? "n" : "ff",
		fast_interrupts_enabled(regs) ? "n" : "ff",
		processor_modes[processor_mode(regs)],
		segment_eq(get_fs(), get_ds()) ? "kernel" : "user");
	{
		unsigned int ctrl;

		buf[0] = '\0';
		{
			unsigned int transbase;
			asm("movc %0, p0.c2, #0\n"
			    : "=r" (transbase));
			snprintf(buf, sizeof(buf), "  Table: %08x", transbase);
		}
		asm("movc %0, p0.c1, #0\n" : "=r" (ctrl));

		printk(KERN_DEFAULT "Control: %08x%s\n", ctrl, buf);
	}
}

void show_regs(struct pt_regs *regs)
{
	printk(KERN_DEFAULT "\n");
	printk(KERN_DEFAULT "Pid: %d, comm: %20s\n",
			task_pid_nr(current), current->comm);
	__show_regs(regs);
	__backtrace();
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = current;

	memset(thread->used_cp, 0, sizeof(thread->used_cp));
	memset(&tsk->thread.debug, 0, sizeof(struct debug_info));
#ifdef CONFIG_UNICORE_FPU_F64
	memset(&thread->fpstate, 0, sizeof(struct fp_state));
#endif
}

void release_thread(struct task_struct *dead_task)
{
}

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");
asmlinkage void ret_from_kernel_thread(void) __asm__("ret_from_kernel_thread");

int
copy_thread(unsigned long clone_flags, unsigned long stack_start,
	    unsigned long stk_sz, struct task_struct *p)
{
	struct thread_info *thread = task_thread_info(p);
	struct pt_regs *childregs = task_pt_regs(p);

	memset(&thread->cpu_context, 0, sizeof(struct cpu_context_save));
	thread->cpu_context.sp = (unsigned long)childregs;
	if (unlikely(p->flags & PF_KTHREAD)) {
		thread->cpu_context.pc = (unsigned long)ret_from_kernel_thread;
		thread->cpu_context.r4 = stack_start;
		thread->cpu_context.r5 = stk_sz;
		memset(childregs, 0, sizeof(struct pt_regs));
	} else {
		thread->cpu_context.pc = (unsigned long)ret_from_fork;
		*childregs = *current_pt_regs();
		childregs->UCreg_00 = 0;
		if (stack_start)
			childregs->UCreg_sp = stack_start;

		if (clone_flags & CLONE_SETTLS)
			childregs->UCreg_16 = childregs->UCreg_03;
	}
	return 0;
}

/*
 * Fill in the task's elfregs structure for a core dump.
 */
int dump_task_regs(struct task_struct *t, elf_gregset_t *elfregs)
{
	elf_core_copy_regs(elfregs, task_pt_regs(t));
	return 1;
}

/*
 * fill in the fpe structure for a core dump...
 */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fp)
{
	struct thread_info *thread = current_thread_info();
	int used_math = thread->used_cp[1] | thread->used_cp[2];

#ifdef CONFIG_UNICORE_FPU_F64
	if (used_math)
		memcpy(fp, &thread->fpstate, sizeof(*fp));
#endif
	return used_math != 0;
}
EXPORT_SYMBOL(dump_fpu);

unsigned long get_wchan(struct task_struct *p)
{
	struct stackframe frame;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	frame.fp = thread_saved_fp(p);
	frame.sp = thread_saved_sp(p);
	frame.lr = 0;			/* recovered from the stack */
	frame.pc = thread_saved_pc(p);
	do {
		int ret = unwind_frame(&frame);
		if (ret < 0)
			return 0;
		if (!in_sched_functions(frame.pc))
			return frame.pc;
	} while ((count++) < 16);
	return 0;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long range_end = mm->brk + 0x02000000;
	return randomize_range(mm->brk, range_end, 0) ? : mm->brk;
}

/*
 * The vectors page is always readable from user space for the
 * atomic helpers and the signal restart code.  Let's declare a mapping
 * for it so it is visible through ptrace and /proc/<pid>/mem.
 */

int vectors_user_mapping(void)
{
	struct mm_struct *mm = current->mm;
	return install_special_mapping(mm, 0xffff0000, PAGE_SIZE,
				       VM_READ | VM_EXEC |
				       VM_MAYREAD | VM_MAYEXEC |
				       VM_DONTEXPAND | VM_DONTDUMP,
				       NULL);
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	return (vma->vm_start == 0xffff0000) ? "[vectors]" : NULL;
}
