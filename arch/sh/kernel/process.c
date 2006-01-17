/* $Id: process.c,v 1.28 2004/05/05 16:54:23 lethal Exp $
 *
 *  linux/arch/sh/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  SuperH version:  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/slab.h>
#include <linux/a.out.h>
#include <linux/ptrace.h>
#include <linux/platform.h>
#include <linux/kallsyms.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/elf.h>
#if defined(CONFIG_SH_HS7751RVOIP)
#include <asm/hs7751rvoip/hs7751rvoip.h>
#elif defined(CONFIG_SH_RTS7751R2D)
#include <asm/rts7751r2d/rts7751r2d.h>
#endif

static int hlt_counter=0;

int ubc_usercnt = 0;

#define HARD_IDLE_TIMEOUT (HZ / 3)

void disable_hlt(void)
{
	hlt_counter++;
}

EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	hlt_counter--;
}

EXPORT_SYMBOL(enable_hlt);

void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		if (hlt_counter) {
			while (!need_resched())
				cpu_relax();
		} else {
			while (!need_resched())
				cpu_sleep();
		}

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void machine_restart(char * __unused)
{

#ifdef CONFIG_KEXEC
	struct kimage *image;
	image = xchg(&kexec_image, 0);
	if (image) {
		machine_shutdown();
		machine_kexec(image);
	}
#endif

	/* SR.BL=1 and invoke address error to let CPU reset (manual reset) */
	asm volatile("ldc %0, sr\n\t"
		     "mov.l @%1, %0" : : "r" (0x10000000), "r" (0x80000001));
}

void machine_halt(void)
{
#if defined(CONFIG_SH_HS7751RVOIP)
	unsigned short value;

	value = ctrl_inw(PA_OUTPORTR);
	ctrl_outw((value & 0xffdf), PA_OUTPORTR);
#elif defined(CONFIG_SH_RTS7751R2D)
	ctrl_outw(0x0001, PA_POWOFF);
#endif
	while (1)
		cpu_sleep();
}

void machine_power_off(void)
{
#if defined(CONFIG_SH_HS7751RVOIP)
	unsigned short value;

	value = ctrl_inw(PA_OUTPORTR);
	ctrl_outw((value & 0xffdf), PA_OUTPORTR);
#elif defined(CONFIG_SH_RTS7751R2D)
	ctrl_outw(0x0001, PA_POWOFF);
#endif
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("Pid : %d, Comm: %20s\n", current->pid, current->comm);
	print_symbol("PC is at %s\n", regs->pc);
	printk("PC  : %08lx SP  : %08lx SR  : %08lx ",
	       regs->pc, regs->regs[15], regs->sr);
#ifdef CONFIG_MMU
	printk("TEA : %08x    ", ctrl_inl(MMU_TEA));
#else
	printk("                  ");
#endif
	printk("%s\n", print_tainted());

	printk("R0  : %08lx R1  : %08lx R2  : %08lx R3  : %08lx\n",
	       regs->regs[0],regs->regs[1],
	       regs->regs[2],regs->regs[3]);
	printk("R4  : %08lx R5  : %08lx R6  : %08lx R7  : %08lx\n",
	       regs->regs[4],regs->regs[5],
	       regs->regs[6],regs->regs[7]);
	printk("R8  : %08lx R9  : %08lx R10 : %08lx R11 : %08lx\n",
	       regs->regs[8],regs->regs[9],
	       regs->regs[10],regs->regs[11]);
	printk("R12 : %08lx R13 : %08lx R14 : %08lx\n",
	       regs->regs[12],regs->regs[13],
	       regs->regs[14]);
	printk("MACH: %08lx MACL: %08lx GBR : %08lx PR  : %08lx\n",
	       regs->mach, regs->macl, regs->gbr, regs->pr);

	/*
	 * If we're in kernel mode, dump the stack too..
	 */
	if (!user_mode(regs)) {
		extern void show_task(unsigned long *sp);
		unsigned long sp = regs->regs[15];

		show_task((unsigned long *)sp);
	}
}

/*
 * Create a kernel thread
 */

/*
 * This is the mechanism for creating a new kernel thread.
 *
 */
extern void kernel_thread_helper(void);
__asm__(".align 5\n"
	"kernel_thread_helper:\n\t"
	"jsr	@r5\n\t"
	" nop\n\t"
	"mov.l	1f, r1\n\t"
	"jsr	@r1\n\t"
	" mov	r0, r4\n\t"
	".align 2\n\t"
	"1:.long do_exit");

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{	/* Don't use this in BL=1(cli).  Or else, CPU resets! */
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.regs[4] = (unsigned long) arg;
	regs.regs[5] = (unsigned long) fn;

	regs.pc = (unsigned long) kernel_thread_helper;
	regs.sr = (1 << 30);

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	if (current->thread.ubc_pc) {
		current->thread.ubc_pc = 0;
		ubc_usercnt -= 1;
	}
}

void flush_thread(void)
{
#if defined(CONFIG_SH_FPU)
	struct task_struct *tsk = current;
	/* Forget lazy FPU state */
	clear_fpu(tsk, task_pt_regs(tsk));
	clear_used_math();
#endif
}

void release_thread(struct task_struct *dead_task)
{
	/* do nothing */
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	int fpvalid = 0;

#if defined(CONFIG_SH_FPU)
	struct task_struct *tsk = current;

	fpvalid = !!tsk_used_math(tsk);
	if (fpvalid) {
		unlazy_fpu(tsk, regs);
		memcpy(fpu, &tsk->thread.fpu.hard, sizeof(*fpu));
	}
#endif

	return fpvalid;
}

/* 
 * Capture the user space registers if the task is not running (in user space)
 */
int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs ptregs;
	
	ptregs = *task_pt_regs(tsk);
	elf_core_copy_regs(regs, &ptregs);

	return 1;
}

int
dump_task_fpu (struct task_struct *tsk, elf_fpregset_t *fpu)
{
	int fpvalid = 0;

#if defined(CONFIG_SH_FPU)
	fpvalid = !!tsk_used_math(tsk);
	if (fpvalid) {
		unlazy_fpu(tsk, task_pt_regs(tsk));
		memcpy(fpu, &tsk->thread.fpu.hard, sizeof(*fpu));
	}
#endif

	return fpvalid;
}

asmlinkage void ret_from_fork(void);

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
#if defined(CONFIG_SH_FPU)
	struct task_struct *tsk = current;

	unlazy_fpu(tsk, regs);
	p->thread.fpu = tsk->thread.fpu;
	copy_to_stopped_child_used_math(p);
#endif

	childregs = task_pt_regs(p);
	*childregs = *regs;

	if (user_mode(regs)) {
		childregs->regs[15] = usp;
	} else {
		childregs->regs[15] = (unsigned long)task_stack_page(p) + THREAD_SIZE;
	}
        if (clone_flags & CLONE_SETTLS) {
		childregs->gbr = childregs->regs[0];
	}
	childregs->regs[0] = 0; /* Set return value for child */

	p->thread.sp = (unsigned long) childregs;
	p->thread.pc = (unsigned long) ret_from_fork;

	p->thread.ubc_pc = 0;

	return 0;
}

/* Tracing by user break controller.  */
static void
ubc_set_tracing(int asid, unsigned long pc)
{
	ctrl_outl(pc, UBC_BARA);

	/* We don't have any ASID settings for the SH-2! */
	if (cpu_data->type != CPU_SH7604)
		ctrl_outb(asid, UBC_BASRA);

	ctrl_outl(0, UBC_BAMRA);

	if (cpu_data->type == CPU_SH7729) {
		ctrl_outw(BBR_INST | BBR_READ | BBR_CPU, UBC_BBRA);
		ctrl_outl(BRCR_PCBA | BRCR_PCTE, UBC_BRCR);
	} else {
		ctrl_outw(BBR_INST | BBR_READ, UBC_BBRA);
		ctrl_outw(BRCR_PCBA, UBC_BRCR);
	}
}

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 */
struct task_struct *__switch_to(struct task_struct *prev, struct task_struct *next)
{
#if defined(CONFIG_SH_FPU)
	unlazy_fpu(prev, task_pt_regs(prev));
#endif

#ifdef CONFIG_PREEMPT
	{
		unsigned long flags;
		struct pt_regs *regs;

		local_irq_save(flags);
		regs = task_pt_regs(prev);
		if (user_mode(regs) && regs->regs[15] >= 0xc0000000) {
			int offset = (int)regs->regs[15];

			/* Reset stack pointer: clear critical region mark */
			regs->regs[15] = regs->regs[1];
			if (regs->pc < regs->regs[0])
				/* Go to rewind point */
				regs->pc = regs->regs[0] + offset;
		}
		local_irq_restore(flags);
	}
#endif

	/*
	 * Restore the kernel mode register
	 *   	k7 (r7_bank1)
	 */
	asm volatile("ldc	%0, r7_bank"
		     : /* no output */
		     : "r" (task_thread_info(next)));

#ifdef CONFIG_MMU
	/* If no tasks are using the UBC, we're done */
	if (ubc_usercnt == 0)
		/* If no tasks are using the UBC, we're done */;
	else if (next->thread.ubc_pc && next->mm) {
		ubc_set_tracing(next->mm->context & MMU_CONTEXT_ASID_MASK,
				next->thread.ubc_pc);
	} else {
		ctrl_outw(0, UBC_BBRA);
		ctrl_outw(0, UBC_BBRB);
	}
#endif

	return prev;
}

asmlinkage int sys_fork(unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7,
			struct pt_regs regs)
{
#ifdef CONFIG_MMU
	return do_fork(SIGCHLD, regs.regs[15], &regs, 0, NULL, NULL);
#else
	/* fork almost works, enough to trick you into looking elsewhere :-( */
	return -EINVAL;
#endif
}

asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 unsigned long parent_tidptr,
			 unsigned long child_tidptr,
			 struct pt_regs regs)
{
	if (!newsp)
		newsp = regs.regs[15];
	return do_fork(clone_flags, newsp, &regs, 0,
			(int __user *)parent_tidptr, (int __user *)child_tidptr);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage int sys_vfork(unsigned long r4, unsigned long r5,
			 unsigned long r6, unsigned long r7,
			 struct pt_regs regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.regs[15], &regs,
		       0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *ufilename, char **uargv,
			  char **uenvp, unsigned long r7,
			  struct pt_regs regs)
{
	int error;
	char *filename;

	filename = getname((char __user *)ufilename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename,
			  (char __user * __user *)uargv,
			  (char __user * __user *)uenvp,
			  &regs);
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
out:
	return error;
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long schedule_frame;
	unsigned long pc;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	/*
	 * The same comment as on the Alpha applies here, too ...
	 */
	pc = thread_saved_pc(p);
	if (in_sched_functions(pc)) {
		schedule_frame = ((unsigned long *)(long)p->thread.sp)[1];
		return (unsigned long)((unsigned long *)schedule_frame)[1];
	}
	return pc;
}

asmlinkage void break_point_trap(unsigned long r4, unsigned long r5,
				 unsigned long r6, unsigned long r7,
				 struct pt_regs regs)
{
	/* Clear tracing.  */
	ctrl_outw(0, UBC_BBRA);
	ctrl_outw(0, UBC_BBRB);
	current->thread.ubc_pc = 0;
	ubc_usercnt -= 1;

	force_sig(SIGTRAP, current);
}

asmlinkage void break_point_trap_software(unsigned long r4, unsigned long r5,
					  unsigned long r6, unsigned long r7,
					  struct pt_regs regs)
{
	regs.pc -= 2;
	force_sig(SIGTRAP, current);
}
