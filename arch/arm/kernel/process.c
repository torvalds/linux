/*
 *  linux/arch/arm/kernel/process.c
 *
 *  Copyright (C) 1996-2000 Russell King - Converted to ARM.
 *  Original Copyright (C) 1995  Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdarg.h>

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/elfcore.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/hw_breakpoint.h>
#include <linux/leds.h>

#include <asm/processor.h>
#include <asm/thread_notify.h>
#include <asm/stacktrace.h>
#include <asm/system_misc.h>
#include <asm/mach/time.h>
#include <asm/tls.h>
#include <asm/vdso.h>

#ifdef CONFIG_CC_STACKPROTECTOR
#include <linux/stackprotector.h>
unsigned long __stack_chk_guard __read_mostly;
EXPORT_SYMBOL(__stack_chk_guard);
#endif

static const char *processor_modes[] __maybe_unused = {
  "USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" , "UK4_26" , "UK5_26" , "UK6_26" , "UK7_26" ,
  "UK8_26" , "UK9_26" , "UK10_26", "UK11_26", "UK12_26", "UK13_26", "UK14_26", "UK15_26",
  "USER_32", "FIQ_32" , "IRQ_32" , "SVC_32" , "UK4_32" , "UK5_32" , "MON_32" , "ABT_32" ,
  "UK8_32" , "UK9_32" , "HYP_32", "UND_32" , "UK12_32", "UK13_32", "UK14_32", "SYS_32"
};

static const char *isa_modes[] __maybe_unused = {
  "ARM" , "Thumb" , "Jazelle", "ThumbEE"
};

/*
 * This is our default idle handler.
 */

void (*arm_pm_idle)(void);

/*
 * Called from the core idle loop.
 */

void arch_cpu_idle(void)
{
	if (arm_pm_idle)
		arm_pm_idle();
	else
		cpu_do_idle();
	local_irq_enable();
}

void arch_cpu_idle_prepare(void)
{
	local_fiq_enable();
}

void arch_cpu_idle_enter(void)
{
	ledtrig_cpu(CPU_LED_IDLE_START);
#ifdef CONFIG_PL310_ERRATA_769419
	wmb();
#endif
}

void arch_cpu_idle_exit(void)
{
	ledtrig_cpu(CPU_LED_IDLE_END);
}

void __show_regs(struct pt_regs *regs)
{
	unsigned long flags;
	char buf[64];
#ifndef CONFIG_CPU_V7M
	unsigned int domain, fs;
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	/*
	 * Get the domain register for the parent context. In user
	 * mode, we don't save the DACR, so lets use what it should
	 * be. For other modes, we place it after the pt_regs struct.
	 */
	if (user_mode(regs)) {
		domain = DACR_UACCESS_ENABLE;
		fs = get_fs();
	} else {
		domain = to_svc_pt_regs(regs)->dacr;
		fs = to_svc_pt_regs(regs)->addr_limit;
	}
#else
	domain = get_domain();
	fs = get_fs();
#endif
#endif

	show_regs_print_info(KERN_DEFAULT);

	print_symbol("PC is at %s\n", instruction_pointer(regs));
	print_symbol("LR is at %s\n", regs->ARM_lr);
	printk("pc : [<%08lx>]    lr : [<%08lx>]    psr: %08lx\n"
	       "sp : %08lx  ip : %08lx  fp : %08lx\n",
		regs->ARM_pc, regs->ARM_lr, regs->ARM_cpsr,
		regs->ARM_sp, regs->ARM_ip, regs->ARM_fp);
	printk("r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->ARM_r10, regs->ARM_r9,
		regs->ARM_r8);
	printk("r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->ARM_r7, regs->ARM_r6,
		regs->ARM_r5, regs->ARM_r4);
	printk("r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->ARM_r3, regs->ARM_r2,
		regs->ARM_r1, regs->ARM_r0);

	flags = regs->ARM_cpsr;
	buf[0] = flags & PSR_N_BIT ? 'N' : 'n';
	buf[1] = flags & PSR_Z_BIT ? 'Z' : 'z';
	buf[2] = flags & PSR_C_BIT ? 'C' : 'c';
	buf[3] = flags & PSR_V_BIT ? 'V' : 'v';
	buf[4] = '\0';

#ifndef CONFIG_CPU_V7M
	{
		const char *segment;

		if ((domain & domain_mask(DOMAIN_USER)) ==
		    domain_val(DOMAIN_USER, DOMAIN_NOACCESS))
			segment = "none";
		else if (fs == get_ds())
			segment = "kernel";
		else
			segment = "user";

		printk("Flags: %s  IRQs o%s  FIQs o%s  Mode %s  ISA %s  Segment %s\n",
			buf, interrupts_enabled(regs) ? "n" : "ff",
			fast_interrupts_enabled(regs) ? "n" : "ff",
			processor_modes[processor_mode(regs)],
			isa_modes[isa_mode(regs)], segment);
	}
#else
	printk("xPSR: %08lx\n", regs->ARM_cpsr);
#endif

#ifdef CONFIG_CPU_CP15
	{
		unsigned int ctrl;

		buf[0] = '\0';
#ifdef CONFIG_CPU_CP15_MMU
		{
			unsigned int transbase;
			asm("mrc p15, 0, %0, c2, c0\n\t"
			    : "=r" (transbase));
			snprintf(buf, sizeof(buf), "  Table: %08x  DAC: %08x",
				transbase, domain);
		}
#endif
		asm("mrc p15, 0, %0, c1, c0\n" : "=r" (ctrl));

		printk("Control: %08x%s\n", ctrl, buf);
	}
#endif
}

void show_regs(struct pt_regs * regs)
{
	__show_regs(regs);
	dump_stack();
}

ATOMIC_NOTIFIER_HEAD(thread_notify_head);

EXPORT_SYMBOL_GPL(thread_notify_head);

/*
 * Free current thread data structures etc..
 */
void exit_thread(struct task_struct *tsk)
{
	thread_notify(THREAD_NOTIFY_EXIT, task_thread_info(tsk));
}

void flush_thread(void)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = current;

	flush_ptrace_hw_breakpoint(tsk);

	memset(thread->used_cp, 0, sizeof(thread->used_cp));
	memset(&tsk->thread.debug, 0, sizeof(struct debug_info));
	memset(&thread->fpstate, 0, sizeof(union fp_state));

	flush_tls();

	thread_notify(THREAD_NOTIFY_FLUSH, thread);
}

void release_thread(struct task_struct *dead_task)
{
}

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

int
copy_thread(unsigned long clone_flags, unsigned long stack_start,
	    unsigned long stk_sz, struct task_struct *p)
{
	struct thread_info *thread = task_thread_info(p);
	struct pt_regs *childregs = task_pt_regs(p);

	memset(&thread->cpu_context, 0, sizeof(struct cpu_context_save));

#ifdef CONFIG_CPU_USE_DOMAINS
	/*
	 * Copy the initial value of the domain access control register
	 * from the current thread: thread->addr_limit will have been
	 * copied from the current thread via setup_thread_stack() in
	 * kernel/fork.c
	 */
	thread->cpu_domain = get_domain();
#endif

	if (likely(!(p->flags & PF_KTHREAD))) {
		*childregs = *current_pt_regs();
		childregs->ARM_r0 = 0;
		if (stack_start)
			childregs->ARM_sp = stack_start;
	} else {
		memset(childregs, 0, sizeof(struct pt_regs));
		thread->cpu_context.r4 = stk_sz;
		thread->cpu_context.r5 = stack_start;
		childregs->ARM_cpsr = SVC_MODE;
	}
	thread->cpu_context.pc = (unsigned long)ret_from_fork;
	thread->cpu_context.sp = (unsigned long)childregs;

	clear_ptrace_hw_breakpoint(p);

	if (clone_flags & CLONE_SETTLS)
		thread->tp_value[0] = childregs->ARM_r3;
	thread->tp_value[1] = get_tpuser();

	thread_notify(THREAD_NOTIFY_COPY, thread);

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
int dump_fpu (struct pt_regs *regs, struct user_fp *fp)
{
	struct thread_info *thread = current_thread_info();
	int used_math = thread->used_cp[1] | thread->used_cp[2];

	if (used_math)
		memcpy(fp, &thread->fpstate.soft, sizeof (*fp));

	return used_math != 0;
}
EXPORT_SYMBOL(dump_fpu);

unsigned long get_wchan(struct task_struct *p)
{
	struct stackframe frame;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	frame.fp = thread_saved_fp(p);
	frame.sp = thread_saved_sp(p);
	frame.lr = 0;			/* recovered from the stack */
	frame.pc = thread_saved_pc(p);
	stack_page = (unsigned long)task_stack_page(p);
	do {
		if (frame.sp < stack_page ||
		    frame.sp >= stack_page + THREAD_SIZE ||
		    unwind_frame(&frame) < 0)
			return 0;
		if (!in_sched_functions(frame.pc))
			return frame.pc;
	} while (count ++ < 16);
	return 0;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long range_end = mm->brk + 0x02000000;
	return randomize_range(mm->brk, range_end, 0) ? : mm->brk;
}

#ifdef CONFIG_MMU
#ifdef CONFIG_KUSER_HELPERS
/*
 * The vectors page is always readable from user space for the
 * atomic helpers. Insert it into the gate_vma so that it is visible
 * through ptrace and /proc/<pid>/mem.
 */
static struct vm_area_struct gate_vma = {
	.vm_start	= 0xffff0000,
	.vm_end		= 0xffff0000 + PAGE_SIZE,
	.vm_flags	= VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYEXEC,
};

static int __init gate_vma_init(void)
{
	gate_vma.vm_page_prot = PAGE_READONLY_EXEC;
	return 0;
}
arch_initcall(gate_vma_init);

struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
	return &gate_vma;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	return (addr >= gate_vma.vm_start) && (addr < gate_vma.vm_end);
}

int in_gate_area_no_mm(unsigned long addr)
{
	return in_gate_area(NULL, addr);
}
#define is_gate_vma(vma)	((vma) == &gate_vma)
#else
#define is_gate_vma(vma)	0
#endif

const char *arch_vma_name(struct vm_area_struct *vma)
{
	return is_gate_vma(vma) ? "[vectors]" : NULL;
}

/* If possible, provide a placement hint at a random offset from the
 * stack for the sigpage and vdso pages.
 */
static unsigned long sigpage_addr(const struct mm_struct *mm,
				  unsigned int npages)
{
	unsigned long offset;
	unsigned long first;
	unsigned long last;
	unsigned long addr;
	unsigned int slots;

	first = PAGE_ALIGN(mm->start_stack);

	last = TASK_SIZE - (npages << PAGE_SHIFT);

	/* No room after stack? */
	if (first > last)
		return 0;

	/* Just enough room? */
	if (first == last)
		return first;

	slots = ((last - first) >> PAGE_SHIFT) + 1;

	offset = get_random_int() % slots;

	addr = first + (offset << PAGE_SHIFT);

	return addr;
}

static struct page *signal_page;
extern struct page *get_signal_page(void);

static const struct vm_special_mapping sigpage_mapping = {
	.name = "[sigpage]",
	.pages = &signal_page,
};

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long npages;
	unsigned long addr;
	unsigned long hint;
	int ret = 0;

	if (!signal_page)
		signal_page = get_signal_page();
	if (!signal_page)
		return -ENOMEM;

	npages = 1; /* for sigpage */
	npages += vdso_total_pages;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;
	hint = sigpage_addr(mm, npages);
	addr = get_unmapped_area(NULL, hint, npages << PAGE_SHIFT, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	vma = _install_special_mapping(mm, addr, PAGE_SIZE,
		VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
		&sigpage_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	mm->context.sigpage = addr;

	/* Unlike the sigpage, failure to install the vdso is unlikely
	 * to be fatal to the process, so no error check needed
	 * here.
	 */
	arm_install_vdso(mm, addr + PAGE_SIZE);

 up_fail:
	up_write(&mm->mmap_sem);
	return ret;
}
#endif
