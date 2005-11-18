/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * 04/11/17 Ashok Raj	<ashok.raj@intel.com> Added CPU Hotplug Support
 *
 * 2005-10-07 Keith Owens <kaos@sgi.com>
 *	      Add notify_die() hooks.
 */
#define __KERNEL_SYSCALLS__	/* see <asm/unistd.h> */
#include <linux/config.h>

#include <linux/cpu.h>
#include <linux/pm.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/personality.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/thread_info.h>
#include <linux/unistd.h>
#include <linux/efi.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kprobes.h>

#include <asm/cpu.h>
#include <asm/delay.h>
#include <asm/elf.h>
#include <asm/ia32.h>
#include <asm/irq.h>
#include <asm/kdebug.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/unwind.h>
#include <asm/user.h>

#include "entry.h"

#ifdef CONFIG_PERFMON
# include <asm/perfmon.h>
#endif

#include "sigframe.h"

void (*ia64_mark_idle)(int);
static DEFINE_PER_CPU(unsigned int, cpu_idle_state);

unsigned long boot_option_idle_override = 0;
EXPORT_SYMBOL(boot_option_idle_override);

void
ia64_do_show_stack (struct unw_frame_info *info, void *arg)
{
	unsigned long ip, sp, bsp;
	char buf[128];			/* don't make it so big that it overflows the stack! */

	printk("\nCall Trace:\n");
	do {
		unw_get_ip(info, &ip);
		if (ip == 0)
			break;

		unw_get_sp(info, &sp);
		unw_get_bsp(info, &bsp);
		snprintf(buf, sizeof(buf),
			 " [<%016lx>] %%s\n"
			 "                                sp=%016lx bsp=%016lx\n",
			 ip, sp, bsp);
		print_symbol(buf, ip);
	} while (unw_unwind(info) >= 0);
}

void
show_stack (struct task_struct *task, unsigned long *sp)
{
	if (!task)
		unw_init_running(ia64_do_show_stack, NULL);
	else {
		struct unw_frame_info info;

		unw_init_from_blocked_task(&info, task);
		ia64_do_show_stack(&info, NULL);
	}
}

void
dump_stack (void)
{
	show_stack(NULL, NULL);
}

EXPORT_SYMBOL(dump_stack);

void
show_regs (struct pt_regs *regs)
{
	unsigned long ip = regs->cr_iip + ia64_psr(regs)->ri;

	print_modules();
	printk("\nPid: %d, CPU %d, comm: %20s\n", current->pid, smp_processor_id(), current->comm);
	printk("psr : %016lx ifs : %016lx ip  : [<%016lx>]    %s\n",
	       regs->cr_ipsr, regs->cr_ifs, ip, print_tainted());
	print_symbol("ip is at %s\n", ip);
	printk("unat: %016lx pfs : %016lx rsc : %016lx\n",
	       regs->ar_unat, regs->ar_pfs, regs->ar_rsc);
	printk("rnat: %016lx bsps: %016lx pr  : %016lx\n",
	       regs->ar_rnat, regs->ar_bspstore, regs->pr);
	printk("ldrs: %016lx ccv : %016lx fpsr: %016lx\n",
	       regs->loadrs, regs->ar_ccv, regs->ar_fpsr);
	printk("csd : %016lx ssd : %016lx\n", regs->ar_csd, regs->ar_ssd);
	printk("b0  : %016lx b6  : %016lx b7  : %016lx\n", regs->b0, regs->b6, regs->b7);
	printk("f6  : %05lx%016lx f7  : %05lx%016lx\n",
	       regs->f6.u.bits[1], regs->f6.u.bits[0],
	       regs->f7.u.bits[1], regs->f7.u.bits[0]);
	printk("f8  : %05lx%016lx f9  : %05lx%016lx\n",
	       regs->f8.u.bits[1], regs->f8.u.bits[0],
	       regs->f9.u.bits[1], regs->f9.u.bits[0]);
	printk("f10 : %05lx%016lx f11 : %05lx%016lx\n",
	       regs->f10.u.bits[1], regs->f10.u.bits[0],
	       regs->f11.u.bits[1], regs->f11.u.bits[0]);

	printk("r1  : %016lx r2  : %016lx r3  : %016lx\n", regs->r1, regs->r2, regs->r3);
	printk("r8  : %016lx r9  : %016lx r10 : %016lx\n", regs->r8, regs->r9, regs->r10);
	printk("r11 : %016lx r12 : %016lx r13 : %016lx\n", regs->r11, regs->r12, regs->r13);
	printk("r14 : %016lx r15 : %016lx r16 : %016lx\n", regs->r14, regs->r15, regs->r16);
	printk("r17 : %016lx r18 : %016lx r19 : %016lx\n", regs->r17, regs->r18, regs->r19);
	printk("r20 : %016lx r21 : %016lx r22 : %016lx\n", regs->r20, regs->r21, regs->r22);
	printk("r23 : %016lx r24 : %016lx r25 : %016lx\n", regs->r23, regs->r24, regs->r25);
	printk("r26 : %016lx r27 : %016lx r28 : %016lx\n", regs->r26, regs->r27, regs->r28);
	printk("r29 : %016lx r30 : %016lx r31 : %016lx\n", regs->r29, regs->r30, regs->r31);

	if (user_mode(regs)) {
		/* print the stacked registers */
		unsigned long val, *bsp, ndirty;
		int i, sof, is_nat = 0;

		sof = regs->cr_ifs & 0x7f;	/* size of frame */
		ndirty = (regs->loadrs >> 19);
		bsp = ia64_rse_skip_regs((unsigned long *) regs->ar_bspstore, ndirty);
		for (i = 0; i < sof; ++i) {
			get_user(val, (unsigned long __user *) ia64_rse_skip_regs(bsp, i));
			printk("r%-3u:%c%016lx%s", 32 + i, is_nat ? '*' : ' ', val,
			       ((i == sof - 1) || (i % 3) == 2) ? "\n" : " ");
		}
	} else
		show_stack(NULL, NULL);
}

void
do_notify_resume_user (sigset_t *oldset, struct sigscratch *scr, long in_syscall)
{
	if (fsys_mode(current, &scr->pt)) {
		/* defer signal-handling etc. until we return to privilege-level 0.  */
		if (!ia64_psr(&scr->pt)->lp)
			ia64_psr(&scr->pt)->lp = 1;
		return;
	}

#ifdef CONFIG_PERFMON
	if (current->thread.pfm_needs_checking)
		pfm_handle_work();
#endif

	/* deal with pending signal delivery */
	if (test_thread_flag(TIF_SIGPENDING))
		ia64_do_signal(oldset, scr, in_syscall);
}

static int pal_halt        = 1;
static int can_do_pal_halt = 1;

static int __init nohalt_setup(char * str)
{
	pal_halt = can_do_pal_halt = 0;
	return 1;
}
__setup("nohalt", nohalt_setup);

void
update_pal_halt_status(int status)
{
	can_do_pal_halt = pal_halt && status;
}

/*
 * We use this if we don't have any better idle routine..
 */
void
default_idle (void)
{
	local_irq_enable();
	while (!need_resched()) {
		if (can_do_pal_halt)
			safe_halt();
		else
			cpu_relax();
	}
}

#ifdef CONFIG_HOTPLUG_CPU
/* We don't actually take CPU down, just spin without interrupts. */
static inline void play_dead(void)
{
	extern void ia64_cpu_local_tick (void);
	unsigned int this_cpu = smp_processor_id();

	/* Ack it */
	__get_cpu_var(cpu_state) = CPU_DEAD;

	max_xtp();
	local_irq_disable();
	idle_task_exit();
	ia64_jump_to_sal(&sal_boot_rendez_state[this_cpu]);
	/*
	 * The above is a point of no-return, the processor is
	 * expected to be in SAL loop now.
	 */
	BUG();
}
#else
static inline void play_dead(void)
{
	BUG();
}
#endif /* CONFIG_HOTPLUG_CPU */

void cpu_idle_wait(void)
{
	unsigned int cpu, this_cpu = get_cpu();
	cpumask_t map;

	set_cpus_allowed(current, cpumask_of_cpu(this_cpu));
	put_cpu();

	cpus_clear(map);
	for_each_online_cpu(cpu) {
		per_cpu(cpu_idle_state, cpu) = 1;
		cpu_set(cpu, map);
	}

	__get_cpu_var(cpu_idle_state) = 0;

	wmb();
	do {
		ssleep(1);
		for_each_online_cpu(cpu) {
			if (cpu_isset(cpu, map) && !per_cpu(cpu_idle_state, cpu))
				cpu_clear(cpu, map);
		}
		cpus_and(map, map, cpu_online_map);
	} while (!cpus_empty(map));
}
EXPORT_SYMBOL_GPL(cpu_idle_wait);

void __attribute__((noreturn))
cpu_idle (void)
{
	void (*mark_idle)(int) = ia64_mark_idle;
  	int cpu = smp_processor_id();

	/* endless idle loop with no priority at all */
	while (1) {
		if (can_do_pal_halt)
			clear_thread_flag(TIF_POLLING_NRFLAG);
		else
			set_thread_flag(TIF_POLLING_NRFLAG);

		if (!need_resched()) {
			void (*idle)(void);
#ifdef CONFIG_SMP
			min_xtp();
#endif
			if (__get_cpu_var(cpu_idle_state))
				__get_cpu_var(cpu_idle_state) = 0;

			rmb();
			if (mark_idle)
				(*mark_idle)(1);

			idle = pm_idle;
			if (!idle)
				idle = default_idle;
			(*idle)();
			if (mark_idle)
				(*mark_idle)(0);
#ifdef CONFIG_SMP
			normal_xtp();
#endif
		}
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
		check_pgt_cache();
		if (cpu_is_offline(cpu))
			play_dead();
	}
}

void
ia64_save_extra (struct task_struct *task)
{
#ifdef CONFIG_PERFMON
	unsigned long info;
#endif

	if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0)
		ia64_save_debug_regs(&task->thread.dbr[0]);

#ifdef CONFIG_PERFMON
	if ((task->thread.flags & IA64_THREAD_PM_VALID) != 0)
		pfm_save_regs(task);

	info = __get_cpu_var(pfm_syst_info);
	if (info & PFM_CPUINFO_SYST_WIDE)
		pfm_syst_wide_update_task(task, info, 0);
#endif

#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(ia64_task_regs(task)))
		ia32_save_state(task);
#endif
}

void
ia64_load_extra (struct task_struct *task)
{
#ifdef CONFIG_PERFMON
	unsigned long info;
#endif

	if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0)
		ia64_load_debug_regs(&task->thread.dbr[0]);

#ifdef CONFIG_PERFMON
	if ((task->thread.flags & IA64_THREAD_PM_VALID) != 0)
		pfm_load_regs(task);

	info = __get_cpu_var(pfm_syst_info);
	if (info & PFM_CPUINFO_SYST_WIDE) 
		pfm_syst_wide_update_task(task, info, 1);
#endif

#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(ia64_task_regs(task)))
		ia32_load_state(task);
#endif
}

/*
 * Copy the state of an ia-64 thread.
 *
 * We get here through the following  call chain:
 *
 *	from user-level:	from kernel:
 *
 *	<clone syscall>	        <some kernel call frames>
 *	sys_clone		   :
 *	do_fork			do_fork
 *	copy_thread		copy_thread
 *
 * This means that the stack layout is as follows:
 *
 *	+---------------------+ (highest addr)
 *	|   struct pt_regs    |
 *	+---------------------+
 *	| struct switch_stack |
 *	+---------------------+
 *	|                     |
 *	|    memory stack     |
 *	|                     | <-- sp (lowest addr)
 *	+---------------------+
 *
 * Observe that we copy the unat values that are in pt_regs and switch_stack.  Spilling an
 * integer to address X causes bit N in ar.unat to be set to the NaT bit of the register,
 * with N=(X & 0x1ff)/8.  Thus, copying the unat value preserves the NaT bits ONLY if the
 * pt_regs structure in the parent is congruent to that of the child, modulo 512.  Since
 * the stack is page aligned and the page size is at least 4KB, this is always the case,
 * so there is nothing to worry about.
 */
int
copy_thread (int nr, unsigned long clone_flags,
	     unsigned long user_stack_base, unsigned long user_stack_size,
	     struct task_struct *p, struct pt_regs *regs)
{
	extern char ia64_ret_from_clone, ia32_ret_from_clone;
	struct switch_stack *child_stack, *stack;
	unsigned long rbs, child_rbs, rbs_size;
	struct pt_regs *child_ptregs;
	int retval = 0;

#ifdef CONFIG_SMP
	/*
	 * For SMP idle threads, fork_by_hand() calls do_fork with
	 * NULL regs.
	 */
	if (!regs)
		return 0;
#endif

	stack = ((struct switch_stack *) regs) - 1;

	child_ptregs = (struct pt_regs *) ((unsigned long) p + IA64_STK_OFFSET) - 1;
	child_stack = (struct switch_stack *) child_ptregs - 1;

	/* copy parent's switch_stack & pt_regs to child: */
	memcpy(child_stack, stack, sizeof(*child_ptregs) + sizeof(*child_stack));

	rbs = (unsigned long) current + IA64_RBS_OFFSET;
	child_rbs = (unsigned long) p + IA64_RBS_OFFSET;
	rbs_size = stack->ar_bspstore - rbs;

	/* copy the parent's register backing store to the child: */
	memcpy((void *) child_rbs, (void *) rbs, rbs_size);

	if (likely(user_mode(child_ptregs))) {
		if ((clone_flags & CLONE_SETTLS) && !IS_IA32_PROCESS(regs))
			child_ptregs->r13 = regs->r16;	/* see sys_clone2() in entry.S */
		if (user_stack_base) {
			child_ptregs->r12 = user_stack_base + user_stack_size - 16;
			child_ptregs->ar_bspstore = user_stack_base;
			child_ptregs->ar_rnat = 0;
			child_ptregs->loadrs = 0;
		}
	} else {
		/*
		 * Note: we simply preserve the relative position of
		 * the stack pointer here.  There is no need to
		 * allocate a scratch area here, since that will have
		 * been taken care of by the caller of sys_clone()
		 * already.
		 */
		child_ptregs->r12 = (unsigned long) child_ptregs - 16; /* kernel sp */
		child_ptregs->r13 = (unsigned long) p;		/* set `current' pointer */
	}
	child_stack->ar_bspstore = child_rbs + rbs_size;
	if (IS_IA32_PROCESS(regs))
		child_stack->b0 = (unsigned long) &ia32_ret_from_clone;
	else
		child_stack->b0 = (unsigned long) &ia64_ret_from_clone;

	/* copy parts of thread_struct: */
	p->thread.ksp = (unsigned long) child_stack - 16;

	/* stop some PSR bits from being inherited.
	 * the psr.up/psr.pp bits must be cleared on fork but inherited on execve()
	 * therefore we must specify them explicitly here and not include them in
	 * IA64_PSR_BITS_TO_CLEAR.
	 */
	child_ptregs->cr_ipsr = ((child_ptregs->cr_ipsr | IA64_PSR_BITS_TO_SET)
				 & ~(IA64_PSR_BITS_TO_CLEAR | IA64_PSR_PP | IA64_PSR_UP));

	/*
	 * NOTE: The calling convention considers all floating point
	 * registers in the high partition (fph) to be scratch.  Since
	 * the only way to get to this point is through a system call,
	 * we know that the values in fph are all dead.  Hence, there
	 * is no need to inherit the fph state from the parent to the
	 * child and all we have to do is to make sure that
	 * IA64_THREAD_FPH_VALID is cleared in the child.
	 *
	 * XXX We could push this optimization a bit further by
	 * clearing IA64_THREAD_FPH_VALID on ANY system call.
	 * However, it's not clear this is worth doing.  Also, it
	 * would be a slight deviation from the normal Linux system
	 * call behavior where scratch registers are preserved across
	 * system calls (unless used by the system call itself).
	 */
#	define THREAD_FLAGS_TO_CLEAR	(IA64_THREAD_FPH_VALID | IA64_THREAD_DBG_VALID \
					 | IA64_THREAD_PM_VALID)
#	define THREAD_FLAGS_TO_SET	0
	p->thread.flags = ((current->thread.flags & ~THREAD_FLAGS_TO_CLEAR)
			   | THREAD_FLAGS_TO_SET);
	ia64_drop_fpu(p);	/* don't pick up stale state from a CPU's fph */
#ifdef CONFIG_IA32_SUPPORT
	/*
	 * If we're cloning an IA32 task then save the IA32 extra
	 * state from the current task to the new task
	 */
	if (IS_IA32_PROCESS(ia64_task_regs(current))) {
		ia32_save_state(p);
		if (clone_flags & CLONE_SETTLS)
			retval = ia32_clone_tls(p, child_ptregs);

		/* Copy partially mapped page list */
		if (!retval)
			retval = ia32_copy_partial_page_list(p, clone_flags);
	}
#endif

#ifdef CONFIG_PERFMON
	if (current->thread.pfm_context)
		pfm_inherit(p, child_ptregs);
#endif
	return retval;
}

static void
do_copy_task_regs (struct task_struct *task, struct unw_frame_info *info, void *arg)
{
	unsigned long mask, sp, nat_bits = 0, ip, ar_rnat, urbs_end, cfm;
	elf_greg_t *dst = arg;
	struct pt_regs *pt;
	char nat;
	int i;

	memset(dst, 0, sizeof(elf_gregset_t));	/* don't leak any kernel bits to user-level */

	if (unw_unwind_to_user(info) < 0)
		return;

	unw_get_sp(info, &sp);
	pt = (struct pt_regs *) (sp + 16);

	urbs_end = ia64_get_user_rbs_end(task, pt, &cfm);

	if (ia64_sync_user_rbs(task, info->sw, pt->ar_bspstore, urbs_end) < 0)
		return;

	ia64_peek(task, info->sw, urbs_end, (long) ia64_rse_rnat_addr((long *) urbs_end),
		  &ar_rnat);

	/*
	 * coredump format:
	 *	r0-r31
	 *	NaT bits (for r0-r31; bit N == 1 iff rN is a NaT)
	 *	predicate registers (p0-p63)
	 *	b0-b7
	 *	ip cfm user-mask
	 *	ar.rsc ar.bsp ar.bspstore ar.rnat
	 *	ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec
	 */

	/* r0 is zero */
	for (i = 1, mask = (1UL << i); i < 32; ++i) {
		unw_get_gr(info, i, &dst[i], &nat);
		if (nat)
			nat_bits |= mask;
		mask <<= 1;
	}
	dst[32] = nat_bits;
	unw_get_pr(info, &dst[33]);

	for (i = 0; i < 8; ++i)
		unw_get_br(info, i, &dst[34 + i]);

	unw_get_rp(info, &ip);
	dst[42] = ip + ia64_psr(pt)->ri;
	dst[43] = cfm;
	dst[44] = pt->cr_ipsr & IA64_PSR_UM;

	unw_get_ar(info, UNW_AR_RSC, &dst[45]);
	/*
	 * For bsp and bspstore, unw_get_ar() would return the kernel
	 * addresses, but we need the user-level addresses instead:
	 */
	dst[46] = urbs_end;	/* note: by convention PT_AR_BSP points to the end of the urbs! */
	dst[47] = pt->ar_bspstore;
	dst[48] = ar_rnat;
	unw_get_ar(info, UNW_AR_CCV, &dst[49]);
	unw_get_ar(info, UNW_AR_UNAT, &dst[50]);
	unw_get_ar(info, UNW_AR_FPSR, &dst[51]);
	dst[52] = pt->ar_pfs;	/* UNW_AR_PFS is == to pt->cr_ifs for interrupt frames */
	unw_get_ar(info, UNW_AR_LC, &dst[53]);
	unw_get_ar(info, UNW_AR_EC, &dst[54]);
	unw_get_ar(info, UNW_AR_CSD, &dst[55]);
	unw_get_ar(info, UNW_AR_SSD, &dst[56]);
}

void
do_dump_task_fpu (struct task_struct *task, struct unw_frame_info *info, void *arg)
{
	elf_fpreg_t *dst = arg;
	int i;

	memset(dst, 0, sizeof(elf_fpregset_t));	/* don't leak any "random" bits */

	if (unw_unwind_to_user(info) < 0)
		return;

	/* f0 is 0.0, f1 is 1.0 */

	for (i = 2; i < 32; ++i)
		unw_get_fr(info, i, dst + i);

	ia64_flush_fph(task);
	if ((task->thread.flags & IA64_THREAD_FPH_VALID) != 0)
		memcpy(dst + 32, task->thread.fph, 96*16);
}

void
do_copy_regs (struct unw_frame_info *info, void *arg)
{
	do_copy_task_regs(current, info, arg);
}

void
do_dump_fpu (struct unw_frame_info *info, void *arg)
{
	do_dump_task_fpu(current, info, arg);
}

int
dump_task_regs(struct task_struct *task, elf_gregset_t *regs)
{
	struct unw_frame_info tcore_info;

	if (current == task) {
		unw_init_running(do_copy_regs, regs);
	} else {
		memset(&tcore_info, 0, sizeof(tcore_info));
		unw_init_from_blocked_task(&tcore_info, task);
		do_copy_task_regs(task, &tcore_info, regs);
	}
	return 1;
}

void
ia64_elf_core_copy_regs (struct pt_regs *pt, elf_gregset_t dst)
{
	unw_init_running(do_copy_regs, dst);
}

int
dump_task_fpu (struct task_struct *task, elf_fpregset_t *dst)
{
	struct unw_frame_info tcore_info;

	if (current == task) {
		unw_init_running(do_dump_fpu, dst);
	} else {
		memset(&tcore_info, 0, sizeof(tcore_info));
		unw_init_from_blocked_task(&tcore_info, task);
		do_dump_task_fpu(task, &tcore_info, dst);
	}
	return 1;
}

int
dump_fpu (struct pt_regs *pt, elf_fpregset_t dst)
{
	unw_init_running(do_dump_fpu, dst);
	return 1;	/* f0-f31 are always valid so we always return 1 */
}

long
sys_execve (char __user *filename, char __user * __user *argv, char __user * __user *envp,
	    struct pt_regs *regs)
{
	char *fname;
	int error;

	fname = getname(filename);
	error = PTR_ERR(fname);
	if (IS_ERR(fname))
		goto out;
	error = do_execve(fname, argv, envp, regs);
	putname(fname);
out:
	return error;
}

pid_t
kernel_thread (int (*fn)(void *), void *arg, unsigned long flags)
{
	extern void start_kernel_thread (void);
	unsigned long *helper_fptr = (unsigned long *) &start_kernel_thread;
	struct {
		struct switch_stack sw;
		struct pt_regs pt;
	} regs;

	memset(&regs, 0, sizeof(regs));
	regs.pt.cr_iip = helper_fptr[0];	/* set entry point (IP) */
	regs.pt.r1 = helper_fptr[1];		/* set GP */
	regs.pt.r9 = (unsigned long) fn;	/* 1st argument */
	regs.pt.r11 = (unsigned long) arg;	/* 2nd argument */
	/* Preserve PSR bits, except for bits 32-34 and 37-45, which we can't read.  */
	regs.pt.cr_ipsr = ia64_getreg(_IA64_REG_PSR) | IA64_PSR_BN;
	regs.pt.cr_ifs = 1UL << 63;		/* mark as valid, empty frame */
	regs.sw.ar_fpsr = regs.pt.ar_fpsr = ia64_getreg(_IA64_REG_AR_FPSR);
	regs.sw.ar_bspstore = (unsigned long) current + IA64_RBS_OFFSET;
	regs.sw.pr = (1 << PRED_KERNEL_STACK);
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs.pt, 0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

/* This gets called from kernel_thread() via ia64_invoke_thread_helper().  */
int
kernel_thread_helper (int (*fn)(void *), void *arg)
{
#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(ia64_task_regs(current))) {
		/* A kernel thread is always a 64-bit process. */
		current->thread.map_base  = DEFAULT_MAP_BASE;
		current->thread.task_size = DEFAULT_TASK_SIZE;
		ia64_set_kr(IA64_KR_IO_BASE, current->thread.old_iob);
		ia64_set_kr(IA64_KR_TSSD, current->thread.old_k1);
	}
#endif
	return (*fn)(arg);
}

/*
 * Flush thread state.  This is called when a thread does an execve().
 */
void
flush_thread (void)
{
	/*
	 * Remove function-return probe instances associated with this task
	 * and put them back on the free list. Do not insert an exit probe for
	 * this function, it will be disabled by kprobe_flush_task if you do.
	 */
	kprobe_flush_task(current);

	/* drop floating-point and debug-register state if it exists: */
	current->thread.flags &= ~(IA64_THREAD_FPH_VALID | IA64_THREAD_DBG_VALID);
	ia64_drop_fpu(current);
	if (IS_IA32_PROCESS(ia64_task_regs(current)))
		ia32_drop_partial_page_list(current);
}

/*
 * Clean up state associated with current thread.  This is called when
 * the thread calls exit().
 */
void
exit_thread (void)
{

	/*
	 * Remove function-return probe instances associated with this task
	 * and put them back on the free list. Do not insert an exit probe for
	 * this function, it will be disabled by kprobe_flush_task if you do.
	 */
	kprobe_flush_task(current);

	ia64_drop_fpu(current);
#ifdef CONFIG_PERFMON
       /* if needed, stop monitoring and flush state to perfmon context */
	if (current->thread.pfm_context)
		pfm_exit_thread(current);

	/* free debug register resources */
	if (current->thread.flags & IA64_THREAD_DBG_VALID)
		pfm_release_debug_registers(current);
#endif
	if (IS_IA32_PROCESS(ia64_task_regs(current)))
		ia32_drop_partial_page_list(current);
}

unsigned long
get_wchan (struct task_struct *p)
{
	struct unw_frame_info info;
	unsigned long ip;
	int count = 0;

	/*
	 * Note: p may not be a blocked task (it could be current or
	 * another process running on some other CPU.  Rather than
	 * trying to determine if p is really blocked, we just assume
	 * it's blocked and rely on the unwind routines to fail
	 * gracefully if the process wasn't really blocked after all.
	 * --davidm 99/12/15
	 */
	unw_init_from_blocked_task(&info, p);
	do {
		if (unw_unwind(&info) < 0)
			return 0;
		unw_get_ip(&info, &ip);
		if (!in_sched_functions(ip))
			return ip;
	} while (count++ < 16);
	return 0;
}

void
cpu_halt (void)
{
	pal_power_mgmt_info_u_t power_info[8];
	unsigned long min_power;
	int i, min_power_state;

	if (ia64_pal_halt_info(power_info) != 0)
		return;

	min_power_state = 0;
	min_power = power_info[0].pal_power_mgmt_info_s.power_consumption;
	for (i = 1; i < 8; ++i)
		if (power_info[i].pal_power_mgmt_info_s.im
		    && power_info[i].pal_power_mgmt_info_s.power_consumption < min_power) {
			min_power = power_info[i].pal_power_mgmt_info_s.power_consumption;
			min_power_state = i;
		}

	while (1)
		ia64_pal_halt(min_power_state);
}

void
machine_restart (char *restart_cmd)
{
	(void) notify_die(DIE_MACHINE_RESTART, restart_cmd, NULL, 0, 0, 0);
	(*efi.reset_system)(EFI_RESET_WARM, 0, 0, NULL);
}

void
machine_halt (void)
{
	(void) notify_die(DIE_MACHINE_HALT, "", NULL, 0, 0, 0);
	cpu_halt();
}

void
machine_power_off (void)
{
	if (pm_power_off)
		pm_power_off();
	machine_halt();
}

