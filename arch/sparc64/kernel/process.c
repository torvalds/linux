/*  $Id: process.c,v 1.131 2002/02/09 19:49:30 davem Exp $
 *  arch/sparc64/kernel/process.c
 *
 *  Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996       Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997, 1998 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/compat.h>
#include <linux/tick.h>
#include <linux/init.h>
#include <linux/cpu.h>

#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/pstate.h>
#include <asm/elf.h>
#include <asm/fpumacro.h>
#include <asm/head.h>
#include <asm/cpudata.h>
#include <asm/mmu_context.h>
#include <asm/unistd.h>
#include <asm/hypervisor.h>
#include <asm/sstate.h>

/* #define VERBOSE_SHOWREGS */

static void sparc64_yield(int cpu)
{
	if (tlb_type != hypervisor)
		return;

	clear_thread_flag(TIF_POLLING_NRFLAG);
	smp_mb__after_clear_bit();

	while (!need_resched() && !cpu_is_offline(cpu)) {
		unsigned long pstate;

		/* Disable interrupts. */
		__asm__ __volatile__(
			"rdpr %%pstate, %0\n\t"
			"andn %0, %1, %0\n\t"
			"wrpr %0, %%g0, %%pstate"
			: "=&r" (pstate)
			: "i" (PSTATE_IE));

		if (!need_resched() && !cpu_is_offline(cpu))
			sun4v_cpu_yield();

		/* Re-enable interrupts. */
		__asm__ __volatile__(
			"rdpr %%pstate, %0\n\t"
			"or %0, %1, %0\n\t"
			"wrpr %0, %%g0, %%pstate"
			: "=&r" (pstate)
			: "i" (PSTATE_IE));
	}

	set_thread_flag(TIF_POLLING_NRFLAG);
}

/* The idle loop on sparc64. */
void cpu_idle(void)
{
	int cpu = smp_processor_id();

	set_thread_flag(TIF_POLLING_NRFLAG);

	while(1) {
		tick_nohz_stop_sched_tick();

		while (!need_resched() && !cpu_is_offline(cpu))
			sparc64_yield(cpu);

		tick_nohz_restart_sched_tick();

		preempt_enable_no_resched();

#ifdef CONFIG_HOTPLUG_CPU
		if (cpu_is_offline(cpu))
			cpu_play_dead();
#endif

		schedule();
		preempt_disable();
	}
}

extern char reboot_command [];

extern void (*prom_palette)(int);
extern void (*prom_keyboard)(void);

void machine_halt(void)
{
	sstate_halt();
	if (prom_palette)
		prom_palette (1);
	if (prom_keyboard)
		prom_keyboard();
	prom_halt();
	panic("Halt failed!");
}

void machine_alt_power_off(void)
{
	sstate_poweroff();
	if (prom_palette)
		prom_palette(1);
	if (prom_keyboard)
		prom_keyboard();
	prom_halt_power_off();
	panic("Power-off failed!");
}

void machine_restart(char * cmd)
{
	char *p;
	
	sstate_reboot();
	p = strchr (reboot_command, '\n');
	if (p) *p = 0;
	if (prom_palette)
		prom_palette (1);
	if (prom_keyboard)
		prom_keyboard();
	if (cmd)
		prom_reboot(cmd);
	if (*reboot_command)
		prom_reboot(reboot_command);
	prom_reboot("");
	panic("Reboot failed!");
}

#ifdef CONFIG_COMPAT
static void show_regwindow32(struct pt_regs *regs)
{
	struct reg_window32 __user *rw;
	struct reg_window32 r_w;
	mm_segment_t old_fs;
	
	__asm__ __volatile__ ("flushw");
	rw = compat_ptr((unsigned)regs->u_regs[14]);
	old_fs = get_fs();
	set_fs (USER_DS);
	if (copy_from_user (&r_w, rw, sizeof(r_w))) {
		set_fs (old_fs);
		return;
	}

	set_fs (old_fs);			
	printk("l0: %08x l1: %08x l2: %08x l3: %08x "
	       "l4: %08x l5: %08x l6: %08x l7: %08x\n",
	       r_w.locals[0], r_w.locals[1], r_w.locals[2], r_w.locals[3],
	       r_w.locals[4], r_w.locals[5], r_w.locals[6], r_w.locals[7]);
	printk("i0: %08x i1: %08x i2: %08x i3: %08x "
	       "i4: %08x i5: %08x i6: %08x i7: %08x\n",
	       r_w.ins[0], r_w.ins[1], r_w.ins[2], r_w.ins[3],
	       r_w.ins[4], r_w.ins[5], r_w.ins[6], r_w.ins[7]);
}
#else
#define show_regwindow32(regs)	do { } while (0)
#endif

static void show_regwindow(struct pt_regs *regs)
{
	struct reg_window __user *rw;
	struct reg_window *rwk;
	struct reg_window r_w;
	mm_segment_t old_fs;

	if ((regs->tstate & TSTATE_PRIV) || !(test_thread_flag(TIF_32BIT))) {
		__asm__ __volatile__ ("flushw");
		rw = (struct reg_window __user *)
			(regs->u_regs[14] + STACK_BIAS);
		rwk = (struct reg_window *)
			(regs->u_regs[14] + STACK_BIAS);
		if (!(regs->tstate & TSTATE_PRIV)) {
			old_fs = get_fs();
			set_fs (USER_DS);
			if (copy_from_user (&r_w, rw, sizeof(r_w))) {
				set_fs (old_fs);
				return;
			}
			rwk = &r_w;
			set_fs (old_fs);			
		}
	} else {
		show_regwindow32(regs);
		return;
	}
	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n",
	       rwk->locals[0], rwk->locals[1], rwk->locals[2], rwk->locals[3]);
	printk("l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       rwk->locals[4], rwk->locals[5], rwk->locals[6], rwk->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n",
	       rwk->ins[0], rwk->ins[1], rwk->ins[2], rwk->ins[3]);
	printk("i4: %016lx i5: %016lx i6: %016lx i7: %016lx\n",
	       rwk->ins[4], rwk->ins[5], rwk->ins[6], rwk->ins[7]);
	if (regs->tstate & TSTATE_PRIV)
		print_symbol("I7: <%s>\n", rwk->ins[7]);
}

void show_stackframe(struct sparc_stackf *sf)
{
	unsigned long size;
	unsigned long *stk;
	int i;

	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n"
	       "l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3],
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n"
	       "i4: %016lx i5: %016lx fp: %016lx ret_pc: %016lx\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3],
	       sf->ins[4], sf->ins[5], (unsigned long)sf->fp, sf->callers_pc);
	printk("sp: %016lx x0: %016lx x1: %016lx x2: %016lx\n"
	       "x3: %016lx x4: %016lx x5: %016lx xx: %016lx\n",
	       (unsigned long)sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME_SZ;
	stk = (unsigned long *)((unsigned long)sf + STACKFRAME_SZ);
	i = 0;
	do {
		printk("s%d: %016lx\n", i++, *stk++);
	} while ((size -= sizeof(unsigned long)));
}

void show_stackframe32(struct sparc_stackf32 *sf)
{
	unsigned long size;
	unsigned *stk;
	int i;

	printk("l0: %08x l1: %08x l2: %08x l3: %08x\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3]);
	printk("l4: %08x l5: %08x l6: %08x l7: %08x\n",
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %08x i1: %08x i2: %08x i3: %08x\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3]);
	printk("i4: %08x i5: %08x fp: %08x ret_pc: %08x\n",
	       sf->ins[4], sf->ins[5], sf->fp, sf->callers_pc);
	printk("sp: %08x x0: %08x x1: %08x x2: %08x\n"
	       "x3: %08x x4: %08x x5: %08x xx: %08x\n",
	       sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME32_SZ;
	stk = (unsigned *)((unsigned long)sf + STACKFRAME32_SZ);
	i = 0;
	do {
		printk("s%d: %08x\n", i++, *stk++);
	} while ((size -= sizeof(unsigned)));
}

#ifdef CONFIG_SMP
static DEFINE_SPINLOCK(regdump_lock);
#endif

void __show_regs(struct pt_regs * regs)
{
#ifdef CONFIG_SMP
	unsigned long flags;

	/* Protect against xcall ipis which might lead to livelock on the lock */
	__asm__ __volatile__("rdpr      %%pstate, %0\n\t"
			     "wrpr      %0, %1, %%pstate"
			     : "=r" (flags)
			     : "i" (PSTATE_IE));
	spin_lock(&regdump_lock);
#endif
	printk("TSTATE: %016lx TPC: %016lx TNPC: %016lx Y: %08x    %s\n", regs->tstate,
	       regs->tpc, regs->tnpc, regs->y, print_tainted());
	print_symbol("TPC: <%s>\n", regs->tpc);
	printk("g0: %016lx g1: %016lx g2: %016lx g3: %016lx\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %016lx g5: %016lx g6: %016lx g7: %016lx\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %016lx o1: %016lx o2: %016lx o3: %016lx\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %016lx o5: %016lx sp: %016lx ret_pc: %016lx\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
	print_symbol("RPC: <%s>\n", regs->u_regs[15]);
	show_regwindow(regs);
#ifdef CONFIG_SMP
	spin_unlock(&regdump_lock);
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (flags));
#endif
}

#ifdef VERBOSE_SHOWREGS
static void idump_from_user (unsigned int *pc)
{
	int i;
	int code;
	
	if((((unsigned long) pc) & 3))
		return;
	
	pc -= 3;
	for(i = -3; i < 6; i++) {
		get_user(code, pc);
		printk("%c%08x%c",i?' ':'<',code,i?' ':'>');
		pc++;
	}
	printk("\n");
}
#endif

void show_regs(struct pt_regs *regs)
{
#ifdef VERBOSE_SHOWREGS
	extern long etrap, etraptl1;
#endif
	__show_regs(regs);
#if 0
#ifdef CONFIG_SMP
	{
		extern void smp_report_regs(void);

		smp_report_regs();
	}
#endif
#endif

#ifdef VERBOSE_SHOWREGS	
	if (regs->tpc >= &etrap && regs->tpc < &etraptl1 &&
	    regs->u_regs[14] >= (long)current - PAGE_SIZE &&
	    regs->u_regs[14] < (long)current + 6 * PAGE_SIZE) {
		printk ("*********parent**********\n");
		__show_regs((struct pt_regs *)(regs->u_regs[14] + PTREGS_OFF));
		idump_from_user(((struct pt_regs *)(regs->u_regs[14] + PTREGS_OFF))->tpc);
		printk ("*********endpar**********\n");
	}
#endif
}

void show_regs32(struct pt_regs32 *regs)
{
	printk("PSR: %08x PC: %08x NPC: %08x Y: %08x    %s\n", regs->psr,
	       regs->pc, regs->npc, regs->y, print_tainted());
	printk("g0: %08x g1: %08x g2: %08x g3: %08x ",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %08x g5: %08x g6: %08x g7: %08x\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %08x o1: %08x o2: %08x o3: %08x ",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %08x o5: %08x sp: %08x ret_pc: %08x\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
}

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct thread_info *ti = task_thread_info(tsk);
	unsigned long ret = 0xdeadbeefUL;
	
	if (ti && ti->ksp) {
		unsigned long *sp;
		sp = (unsigned long *)(ti->ksp + STACK_BIAS);
		if (((unsigned long)sp & (sizeof(long) - 1)) == 0UL &&
		    sp[14]) {
			unsigned long *fp;
			fp = (unsigned long *)(sp[14] + STACK_BIAS);
			if (((unsigned long)fp & (sizeof(long) - 1)) == 0UL)
				ret = fp[15];
		}
	}
	return ret;
}

/* Free current thread data structures etc.. */
void exit_thread(void)
{
	struct thread_info *t = current_thread_info();

	if (t->utraps) {
		if (t->utraps[0] < 2)
			kfree (t->utraps);
		else
			t->utraps[0]--;
	}

	if (test_and_clear_thread_flag(TIF_PERFCTR)) {
		t->user_cntd0 = t->user_cntd1 = NULL;
		t->pcr_reg = 0;
		write_pcr(0);
	}
}

void flush_thread(void)
{
	struct thread_info *t = current_thread_info();
	struct mm_struct *mm;

	if (test_ti_thread_flag(t, TIF_ABI_PENDING)) {
		clear_ti_thread_flag(t, TIF_ABI_PENDING);
		if (test_ti_thread_flag(t, TIF_32BIT))
			clear_ti_thread_flag(t, TIF_32BIT);
		else
			set_ti_thread_flag(t, TIF_32BIT);
	}

	mm = t->task->mm;
	if (mm)
		tsb_context_switch(mm);

	set_thread_wsaved(0);

	/* Turn off performance counters if on. */
	if (test_and_clear_thread_flag(TIF_PERFCTR)) {
		t->user_cntd0 = t->user_cntd1 = NULL;
		t->pcr_reg = 0;
		write_pcr(0);
	}

	/* Clear FPU register state. */
	t->fpsaved[0] = 0;
	
	if (get_thread_current_ds() != ASI_AIUS)
		set_fs(USER_DS);

	/* Init new signal delivery disposition. */
	clear_thread_flag(TIF_NEWSIGNALS);
}

/* It's a bit more tricky when 64-bit tasks are involved... */
static unsigned long clone_stackframe(unsigned long csp, unsigned long psp)
{
	unsigned long fp, distance, rval;

	if (!(test_thread_flag(TIF_32BIT))) {
		csp += STACK_BIAS;
		psp += STACK_BIAS;
		__get_user(fp, &(((struct reg_window __user *)psp)->ins[6]));
		fp += STACK_BIAS;
	} else
		__get_user(fp, &(((struct reg_window32 __user *)psp)->ins[6]));

	/* Now 8-byte align the stack as this is mandatory in the
	 * Sparc ABI due to how register windows work.  This hides
	 * the restriction from thread libraries etc.  -DaveM
	 */
	csp &= ~7UL;

	distance = fp - psp;
	rval = (csp - distance);
	if (copy_in_user((void __user *) rval, (void __user *) psp, distance))
		rval = 0;
	else if (test_thread_flag(TIF_32BIT)) {
		if (put_user(((u32)csp),
			     &(((struct reg_window32 __user *)rval)->ins[6])))
			rval = 0;
	} else {
		if (put_user(((u64)csp - STACK_BIAS),
			     &(((struct reg_window __user *)rval)->ins[6])))
			rval = 0;
		else
			rval = rval - STACK_BIAS;
	}

	return rval;
}

/* Standard stuff. */
static inline void shift_window_buffer(int first_win, int last_win,
				       struct thread_info *t)
{
	int i;

	for (i = first_win; i < last_win; i++) {
		t->rwbuf_stkptrs[i] = t->rwbuf_stkptrs[i+1];
		memcpy(&t->reg_window[i], &t->reg_window[i+1],
		       sizeof(struct reg_window));
	}
}

void synchronize_user_stack(void)
{
	struct thread_info *t = current_thread_info();
	unsigned long window;

	flush_user_windows();
	if ((window = get_thread_wsaved()) != 0) {
		int winsize = sizeof(struct reg_window);
		int bias = 0;

		if (test_thread_flag(TIF_32BIT))
			winsize = sizeof(struct reg_window32);
		else
			bias = STACK_BIAS;

		window -= 1;
		do {
			unsigned long sp = (t->rwbuf_stkptrs[window] + bias);
			struct reg_window *rwin = &t->reg_window[window];

			if (!copy_to_user((char __user *)sp, rwin, winsize)) {
				shift_window_buffer(window, get_thread_wsaved() - 1, t);
				set_thread_wsaved(get_thread_wsaved() - 1);
			}
		} while (window--);
	}
}

static void stack_unaligned(unsigned long sp)
{
	siginfo_t info;

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void __user *) sp;
	info.si_trapno = 0;
	force_sig_info(SIGBUS, &info, current);
}

void fault_in_user_windows(void)
{
	struct thread_info *t = current_thread_info();
	unsigned long window;
	int winsize = sizeof(struct reg_window);
	int bias = 0;

	if (test_thread_flag(TIF_32BIT))
		winsize = sizeof(struct reg_window32);
	else
		bias = STACK_BIAS;

	flush_user_windows();
	window = get_thread_wsaved();

	if (likely(window != 0)) {
		window -= 1;
		do {
			unsigned long sp = (t->rwbuf_stkptrs[window] + bias);
			struct reg_window *rwin = &t->reg_window[window];

			if (unlikely(sp & 0x7UL))
				stack_unaligned(sp);

			if (unlikely(copy_to_user((char __user *)sp,
						  rwin, winsize)))
				goto barf;
		} while (window--);
	}
	set_thread_wsaved(0);
	return;

barf:
	set_thread_wsaved(window + 1);
	do_exit(SIGILL);
}

asmlinkage long sparc_do_fork(unsigned long clone_flags,
			      unsigned long stack_start,
			      struct pt_regs *regs,
			      unsigned long stack_size)
{
	int __user *parent_tid_ptr, *child_tid_ptr;

#ifdef CONFIG_COMPAT
	if (test_thread_flag(TIF_32BIT)) {
		parent_tid_ptr = compat_ptr(regs->u_regs[UREG_I2]);
		child_tid_ptr = compat_ptr(regs->u_regs[UREG_I4]);
	} else
#endif
	{
		parent_tid_ptr = (int __user *) regs->u_regs[UREG_I2];
		child_tid_ptr = (int __user *) regs->u_regs[UREG_I4];
	}

	return do_fork(clone_flags, stack_start,
		       regs, stack_size,
		       parent_tid_ptr, child_tid_ptr);
}

/* Copy a Sparc thread.  The fork() return value conventions
 * under SunOS are nothing short of bletcherous:
 * Parent -->  %o0 == childs  pid, %o1 == 0
 * Child  -->  %o0 == parents pid, %o1 == 1
 */
int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct thread_info *t = task_thread_info(p);
	char *child_trap_frame;

	/* Calculate offset to stack_frame & pt_regs */
	child_trap_frame = task_stack_page(p) + (THREAD_SIZE - (TRACEREG_SZ+STACKFRAME_SZ));
	memcpy(child_trap_frame, (((struct sparc_stackf *)regs)-1), (TRACEREG_SZ+STACKFRAME_SZ));

	t->flags = (t->flags & ~((0xffUL << TI_FLAG_CWP_SHIFT) | (0xffUL << TI_FLAG_CURRENT_DS_SHIFT))) |
		(((regs->tstate + 1) & TSTATE_CWP) << TI_FLAG_CWP_SHIFT);
	t->new_child = 1;
	t->ksp = ((unsigned long) child_trap_frame) - STACK_BIAS;
	t->kregs = (struct pt_regs *)(child_trap_frame+sizeof(struct sparc_stackf));
	t->fpsaved[0] = 0;

	if (regs->tstate & TSTATE_PRIV) {
		/* Special case, if we are spawning a kernel thread from
		 * a userspace task (via KMOD, NFS, or similar) we must
		 * disable performance counters in the child because the
		 * address space and protection realm are changing.
		 */
		if (t->flags & _TIF_PERFCTR) {
			t->user_cntd0 = t->user_cntd1 = NULL;
			t->pcr_reg = 0;
			t->flags &= ~_TIF_PERFCTR;
		}
		t->kregs->u_regs[UREG_FP] = t->ksp;
		t->flags |= ((long)ASI_P << TI_FLAG_CURRENT_DS_SHIFT);
		flush_register_windows();
		memcpy((void *)(t->ksp + STACK_BIAS),
		       (void *)(regs->u_regs[UREG_FP] + STACK_BIAS),
		       sizeof(struct sparc_stackf));
		t->kregs->u_regs[UREG_G6] = (unsigned long) t;
		t->kregs->u_regs[UREG_G4] = (unsigned long) t->task;
	} else {
		if (t->flags & _TIF_32BIT) {
			sp &= 0x00000000ffffffffUL;
			regs->u_regs[UREG_FP] &= 0x00000000ffffffffUL;
		}
		t->kregs->u_regs[UREG_FP] = sp;
		t->flags |= ((long)ASI_AIUS << TI_FLAG_CURRENT_DS_SHIFT);
		if (sp != regs->u_regs[UREG_FP]) {
			unsigned long csp;

			csp = clone_stackframe(sp, regs->u_regs[UREG_FP]);
			if (!csp)
				return -EFAULT;
			t->kregs->u_regs[UREG_FP] = csp;
		}
		if (t->utraps)
			t->utraps[0]++;
	}

	/* Set the return value for the child. */
	t->kregs->u_regs[UREG_I0] = current->pid;
	t->kregs->u_regs[UREG_I1] = 1;

	/* Set the second return value for the parent. */
	regs->u_regs[UREG_I1] = 0;

	if (clone_flags & CLONE_SETTLS)
		t->kregs->u_regs[UREG_G7] = regs->u_regs[UREG_I3];

	return 0;
}

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be freed until both the parent and the child have exited.
 */
pid_t kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;

	/* If the parent runs before fn(arg) is called by the child,
	 * the input registers of this function can be clobbered.
	 * So we stash 'fn' and 'arg' into global registers which
	 * will not be modified by the parent.
	 */
	__asm__ __volatile__("mov %4, %%g2\n\t"	   /* Save FN into global */
			     "mov %5, %%g3\n\t"	   /* Save ARG into global */
			     "mov %1, %%g1\n\t"	   /* Clone syscall nr. */
			     "mov %2, %%o0\n\t"	   /* Clone flags. */
			     "mov 0, %%o1\n\t"	   /* usp arg == 0 */
			     "t 0x6d\n\t"	   /* Linux/Sparc clone(). */
			     "brz,a,pn %%o1, 1f\n\t" /* Parent, just return. */
			     " mov %%o0, %0\n\t"
			     "jmpl %%g2, %%o7\n\t"   /* Call the function. */
			     " mov %%g3, %%o0\n\t"   /* Set arg in delay. */
			     "mov %3, %%g1\n\t"
			     "t 0x6d\n\t"	   /* Linux/Sparc exit(). */
			     /* Notreached by child. */
			     "1:" :
			     "=r" (retval) :
			     "i" (__NR_clone), "r" (flags | CLONE_VM | CLONE_UNTRACED),
			     "i" (__NR_exit),  "r" (fn), "r" (arg) :
			     "g1", "g2", "g3", "o0", "o1", "memory", "cc");
	return retval;
}

typedef struct {
	union {
		unsigned int	pr_regs[32];
		unsigned long	pr_dregs[16];
	} pr_fr;
	unsigned int __unused;
	unsigned int	pr_fsr;
	unsigned char	pr_qcnt;
	unsigned char	pr_q_entrysize;
	unsigned char	pr_en;
	unsigned int	pr_q[64];
} elf_fpregset_t32;

/*
 * fill in the fpu structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, elf_fpregset_t * fpregs)
{
	unsigned long *kfpregs = current_thread_info()->fpregs;
	unsigned long fprs = current_thread_info()->fpsaved[0];

	if (test_thread_flag(TIF_32BIT)) {
		elf_fpregset_t32 *fpregs32 = (elf_fpregset_t32 *)fpregs;

		if (fprs & FPRS_DL)
			memcpy(&fpregs32->pr_fr.pr_regs[0], kfpregs,
			       sizeof(unsigned int) * 32);
		else
			memset(&fpregs32->pr_fr.pr_regs[0], 0,
			       sizeof(unsigned int) * 32);
		fpregs32->pr_qcnt = 0;
		fpregs32->pr_q_entrysize = 8;
		memset(&fpregs32->pr_q[0], 0,
		       (sizeof(unsigned int) * 64));
		if (fprs & FPRS_FEF) {
			fpregs32->pr_fsr = (unsigned int) current_thread_info()->xfsr[0];
			fpregs32->pr_en = 1;
		} else {
			fpregs32->pr_fsr = 0;
			fpregs32->pr_en = 0;
		}
	} else {
		if(fprs & FPRS_DL)
			memcpy(&fpregs->pr_regs[0], kfpregs,
			       sizeof(unsigned int) * 32);
		else
			memset(&fpregs->pr_regs[0], 0,
			       sizeof(unsigned int) * 32);
		if(fprs & FPRS_DU)
			memcpy(&fpregs->pr_regs[16], kfpregs+16,
			       sizeof(unsigned int) * 32);
		else
			memset(&fpregs->pr_regs[16], 0,
			       sizeof(unsigned int) * 32);
		if(fprs & FPRS_FEF) {
			fpregs->pr_fsr = current_thread_info()->xfsr[0];
			fpregs->pr_gsr = current_thread_info()->gsr[0];
		} else {
			fpregs->pr_fsr = fpregs->pr_gsr = 0;
		}
		fpregs->pr_fprs = fprs;
	}
	return 1;
}

/*
 * sparc_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int sparc_execve(struct pt_regs *regs)
{
	int error, base = 0;
	char *filename;

	/* User register window flush is done by entry.S */

	/* Check for indirect call. */
	if (regs->u_regs[UREG_G1] == 0)
		base = 1;

	filename = getname((char __user *)regs->u_regs[base + UREG_I0]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename,
			  (char __user * __user *)
			  regs->u_regs[base + UREG_I1],
			  (char __user * __user *)
			  regs->u_regs[base + UREG_I2], regs);
	putname(filename);
	if (!error) {
		fprs_write(0);
		current_thread_info()->xfsr[0] = 0;
		current_thread_info()->fpsaved[0] = 0;
		regs->tstate &= ~TSTATE_PEF;
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
out:
	return error;
}

unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc, fp, bias = 0;
	unsigned long thread_info_base;
	struct reg_window *rw;
        unsigned long ret = 0;
	int count = 0; 

	if (!task || task == current ||
            task->state == TASK_RUNNING)
		goto out;

	thread_info_base = (unsigned long) task_stack_page(task);
	bias = STACK_BIAS;
	fp = task_thread_info(task)->ksp + bias;

	do {
		/* Bogus frame pointer? */
		if (fp < (thread_info_base + sizeof(struct thread_info)) ||
		    fp >= (thread_info_base + THREAD_SIZE))
			break;
		rw = (struct reg_window *) fp;
		pc = rw->ins[7];
		if (!in_sched_functions(pc)) {
			ret = pc;
			goto out;
		}
		fp = rw->ins[6] + bias;
	} while (++count < 16);

out:
	return ret;
}
