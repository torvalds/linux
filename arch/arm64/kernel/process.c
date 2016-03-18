/*
 * Based on arch/arm/kernel/process.c
 *
 * Original Copyright (C) 1995  Linus Torvalds
 * Copyright (C) 1996-2000 Russell King - Converted to ARM.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdarg.h>

#include <linux/compat.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/user.h>
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
#include <linux/hw_breakpoint.h>
#include <linux/personality.h>
#include <linux/notifier.h>
#include <trace/events/power.h>

#include <asm/compat.h>
#include <asm/cacheflush.h>
#include <asm/fpsimd.h>
#include <asm/mmu_context.h>
#include <asm/processor.h>
#include <asm/stacktrace.h>

#ifdef CONFIG_CC_STACKPROTECTOR
#include <linux/stackprotector.h>
unsigned long __stack_chk_guard __read_mostly;
EXPORT_SYMBOL(__stack_chk_guard);
#endif

/*
 * Function pointers to optional machine specific functions
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL_GPL(pm_power_off);

void (*arm_pm_restart)(enum reboot_mode reboot_mode, const char *cmd);

/*
 * This is our default idle handler.
 */
void arch_cpu_idle(void)
{
	/*
	 * This should do all the clock switching and wait for interrupt
	 * tricks
	 */
	trace_cpu_idle_rcuidle(1, smp_processor_id());
	cpu_do_idle();
	local_irq_enable();
	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
}

#ifdef CONFIG_HOTPLUG_CPU
void arch_cpu_idle_dead(void)
{
       cpu_die();
}
#endif

/*
 * Called by kexec, immediately prior to machine_kexec().
 *
 * This must completely disable all secondary CPUs; simply causing those CPUs
 * to execute e.g. a RAM-based pin loop is not sufficient. This allows the
 * kexec'd kernel to use any and all RAM as it sees fit, without having to
 * avoid any code or data used by any SW CPU pin loop. The CPU hotplug
 * functionality embodied in disable_nonboot_cpus() to achieve this.
 */
void machine_shutdown(void)
{
	disable_nonboot_cpus();
}

/*
 * Halting simply requires that the secondary CPUs stop performing any
 * activity (executing tasks, handling interrupts). smp_send_stop()
 * achieves this.
 */
void machine_halt(void)
{
	local_irq_disable();
	smp_send_stop();
	while (1);
}

/*
 * Power-off simply requires that the secondary CPUs stop performing any
 * activity (executing tasks, handling interrupts). smp_send_stop()
 * achieves this. When the system power is turned off, it will take all CPUs
 * with it.
 */
void machine_power_off(void)
{
	local_irq_disable();
	smp_send_stop();
	if (pm_power_off)
		pm_power_off();
}

/*
 * Restart requires that the secondary CPUs stop performing any activity
 * while the primary CPU resets the system. Systems with multiple CPUs must
 * provide a HW restart implementation, to ensure that all CPUs reset at once.
 * This is required so that any code running after reset on the primary CPU
 * doesn't have to co-ordinate with other CPUs to ensure they aren't still
 * executing pre-reset code, and using RAM that the primary CPU's code wishes
 * to use. Implementing such co-ordination would be essentially impossible.
 */
void machine_restart(char *cmd)
{
	/* Disable interrupts first */
	local_irq_disable();
	smp_send_stop();

	/*
	 * UpdateCapsule() depends on the system being reset via
	 * ResetSystem().
	 */
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efi_reboot(reboot_mode, NULL);

	/* Now call the architecture specific reboot code. */
	if (arm_pm_restart)
		arm_pm_restart(reboot_mode, cmd);
	else
		do_kernel_restart(cmd);

	/*
	 * Whoops - the architecture was unable to reboot.
	 */
	printk("Reboot failed -- System halted\n");
	while (1);
}

/*
 * dump a block of kernel memory from around the given address
 */
static void show_data(unsigned long addr, int nbytes, const char *name)
{
	int	i, j;
	int	nlines;
	u32	*p;

	/*
	 * don't attempt to dump non-kernel addresses or
	 * values that are probably just small negative numbers
	 */
	if (addr < PAGE_OFFSET || addr > -256UL)
		return;

	printk("\n%s: %#lx:\n", name, addr);

	/*
	 * round address down to a 32 bit boundary
	 * and always dump a multiple of 32 bytes
	 */
	p = (u32 *)(addr & ~(sizeof(u32) - 1));
	nbytes += (addr & (sizeof(u32) - 1));
	nlines = (nbytes + 31) / 32;


	for (i = 0; i < nlines; i++) {
		/*
		 * just display low 16 bits of address to keep
		 * each line of the dump < 80 characters
		 */
		printk("%04lx ", (unsigned long)p & 0xffff);
		for (j = 0; j < 8; j++) {
			u32	data;
			if (probe_kernel_address(p, data)) {
				printk(" ********");
			} else {
				printk(" %08x", data);
			}
			++p;
		}
		printk("\n");
	}
}

static void show_extra_register_data(struct pt_regs *regs, int nbytes)
{
	mm_segment_t fs;
	unsigned int i;

	fs = get_fs();
	set_fs(KERNEL_DS);
	show_data(regs->pc - nbytes, nbytes * 2, "PC");
	show_data(regs->regs[30] - nbytes, nbytes * 2, "LR");
	show_data(regs->sp - nbytes, nbytes * 2, "SP");
	for (i = 0; i < 30; i++) {
		char name[4];
		snprintf(name, sizeof(name), "X%u", i);
		show_data(regs->regs[i] - nbytes, nbytes * 2, name);
	}
	set_fs(fs);
}

void __show_regs(struct pt_regs *regs)
{
	int i, top_reg;
	u64 lr, sp;

	if (compat_user_mode(regs)) {
		lr = regs->compat_lr;
		sp = regs->compat_sp;
		top_reg = 12;
	} else {
		lr = regs->regs[30];
		sp = regs->sp;
		top_reg = 29;
	}

	show_regs_print_info(KERN_DEFAULT);
	print_symbol("PC is at %s\n", instruction_pointer(regs));
	print_symbol("LR is at %s\n", lr);
	printk("pc : [<%016llx>] lr : [<%016llx>] pstate: %08llx\n",
	       regs->pc, lr, regs->pstate);
	printk("sp : %016llx\n", sp);
	for (i = top_reg; i >= 0; i--) {
		printk("x%-2d: %016llx ", i, regs->regs[i]);
		if (i % 2 == 0)
			printk("\n");
	}
	if (!user_mode(regs))
		show_extra_register_data(regs, 128);
	printk("\n");
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	__show_regs(regs);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

static void tls_thread_flush(void)
{
	asm ("msr tpidr_el0, xzr");

	if (is_compat_task()) {
		current->thread.tp_value = 0;

		/*
		 * We need to ensure ordering between the shadow state and the
		 * hardware state, so that we don't corrupt the hardware state
		 * with a stale shadow state during context switch.
		 */
		barrier();
		asm ("msr tpidrro_el0, xzr");
	}
}

void flush_thread(void)
{
	fpsimd_flush_thread();
	tls_thread_flush();
	flush_ptrace_hw_breakpoint(current);
}

void release_thread(struct task_struct *dead_task)
{
}

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	if (current->mm)
		fpsimd_preserve_current_state();
	*dst = *src;
	return 0;
}

asmlinkage void ret_from_fork(void) asm("ret_from_fork");

int copy_thread(unsigned long clone_flags, unsigned long stack_start,
		unsigned long stk_sz, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);

	memset(&p->thread.cpu_context, 0, sizeof(struct cpu_context));

	if (likely(!(p->flags & PF_KTHREAD))) {
		*childregs = *current_pt_regs();
		childregs->regs[0] = 0;

		/*
		 * Read the current TLS pointer from tpidr_el0 as it may be
		 * out-of-sync with the saved value.
		 */
		asm("mrs %0, tpidr_el0" : "=r" (*task_user_tls(p)));

		if (stack_start) {
			if (is_compat_thread(task_thread_info(p)))
				childregs->compat_sp = stack_start;
			/* 16-byte aligned stack mandatory on AArch64 */
			else if (stack_start & 15)
				return -EINVAL;
			else
				childregs->sp = stack_start;
		}

		/*
		 * If a TLS pointer was passed to clone (4th argument), use it
		 * for the new thread.
		 */
		if (clone_flags & CLONE_SETTLS)
			p->thread.tp_value = childregs->regs[3];
	} else {
		memset(childregs, 0, sizeof(struct pt_regs));
		childregs->pstate = PSR_MODE_EL1h;
		p->thread.cpu_context.x19 = stack_start;
		p->thread.cpu_context.x20 = stk_sz;
	}
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
	p->thread.cpu_context.sp = (unsigned long)childregs;

	ptrace_hw_copy_thread(p);

	return 0;
}

static void tls_thread_switch(struct task_struct *next)
{
	unsigned long tpidr, tpidrro;

	asm("mrs %0, tpidr_el0" : "=r" (tpidr));
	*task_user_tls(current) = tpidr;

	tpidr = *task_user_tls(next);
	tpidrro = is_compat_thread(task_thread_info(next)) ?
		  next->thread.tp_value : 0;

	asm(
	"	msr	tpidr_el0, %0\n"
	"	msr	tpidrro_el0, %1"
	: : "r" (tpidr), "r" (tpidrro));
}

/*
 * Thread switching.
 */
struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next)
{
	struct task_struct *last;

	fpsimd_thread_switch(next);
	tls_thread_switch(next);
	hw_breakpoint_thread_switch(next);
	contextidr_thread_switch(next);

	/*
	 * Complete any pending TLB or cache maintenance on this CPU in case
	 * the thread migrates to a different CPU.
	 */
	dsb(ish);

	/* the actual thread switch */
	last = cpu_switch_to(prev, next);

	return last;
}

unsigned long get_wchan(struct task_struct *p)
{
	struct stackframe frame;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	frame.fp = thread_saved_fp(p);
	frame.sp = thread_saved_sp(p);
	frame.pc = thread_saved_pc(p);
	stack_page = (unsigned long)task_stack_page(p);
	do {
		if (frame.sp < stack_page ||
		    frame.sp >= stack_page + THREAD_SIZE ||
		    unwind_frame(&frame))
			return 0;
		if (!in_sched_functions(frame.pc))
			return frame.pc;
	} while (count ++ < 16);
	return 0;
}

unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() & ~PAGE_MASK;
	return sp & ~0xf;
}

static unsigned long randomize_base(unsigned long base)
{
	unsigned long range_end = base + (STACK_RND_MASK << PAGE_SHIFT) + 1;
	return randomize_range(base, range_end, 0) ? : base;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	return randomize_base(mm->brk);
}
