// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 *
 *  X86-64 port
 *	Andi Kleen.
 *
 *	CPU hotplug support - ashok.raj@intel.com
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/ptrace.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/prctl.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/ftrace.h>
#include <linux/syscalls.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/fpu/internal.h>
#include <asm/mmu_context.h>
#include <asm/prctl.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/ia32.h>
#include <asm/syscalls.h>
#include <asm/debugreg.h>
#include <asm/switch_to.h>
#include <asm/xen/hypervisor.h>
#include <asm/vdso.h>
#include <asm/resctrl_sched.h>
#include <asm/unistd.h>
#include <asm/fsgsbase.h>
#ifdef CONFIG_IA32_EMULATION
/* Not included via unistd.h */
#include <asm/unistd_32_ia32.h>
#endif

#include "process.h"

/* Prints also some state that isn't saved in the pt_regs */
void __show_regs(struct pt_regs *regs, enum show_regs_mode mode)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
	unsigned long d0, d1, d2, d3, d6, d7;
	unsigned int fsindex, gsindex;
	unsigned int ds, es;

	show_iret_regs(regs);

	if (regs->orig_ax != -1)
		pr_cont(" ORIG_RAX: %016lx\n", regs->orig_ax);
	else
		pr_cont("\n");

	printk(KERN_DEFAULT "RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       regs->ax, regs->bx, regs->cx);
	printk(KERN_DEFAULT "RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       regs->dx, regs->si, regs->di);
	printk(KERN_DEFAULT "RBP: %016lx R08: %016lx R09: %016lx\n",
	       regs->bp, regs->r8, regs->r9);
	printk(KERN_DEFAULT "R10: %016lx R11: %016lx R12: %016lx\n",
	       regs->r10, regs->r11, regs->r12);
	printk(KERN_DEFAULT "R13: %016lx R14: %016lx R15: %016lx\n",
	       regs->r13, regs->r14, regs->r15);

	if (mode == SHOW_REGS_SHORT)
		return;

	if (mode == SHOW_REGS_USER) {
		rdmsrl(MSR_FS_BASE, fs);
		rdmsrl(MSR_KERNEL_GS_BASE, shadowgs);
		printk(KERN_DEFAULT "FS:  %016lx GS:  %016lx\n",
		       fs, shadowgs);
		return;
	}

	asm("movl %%ds,%0" : "=r" (ds));
	asm("movl %%es,%0" : "=r" (es));
	asm("movl %%fs,%0" : "=r" (fsindex));
	asm("movl %%gs,%0" : "=r" (gsindex));

	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs);
	rdmsrl(MSR_KERNEL_GS_BASE, shadowgs);

	cr0 = read_cr0();
	cr2 = read_cr2();
	cr3 = __read_cr3();
	cr4 = __read_cr4();

	printk(KERN_DEFAULT "FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n",
	       fs, fsindex, gs, gsindex, shadowgs);
	printk(KERN_DEFAULT "CS:  %04lx DS: %04x ES: %04x CR0: %016lx\n", regs->cs, ds,
			es, cr0);
	printk(KERN_DEFAULT "CR2: %016lx CR3: %016lx CR4: %016lx\n", cr2, cr3,
			cr4);

	get_debugreg(d0, 0);
	get_debugreg(d1, 1);
	get_debugreg(d2, 2);
	get_debugreg(d3, 3);
	get_debugreg(d6, 6);
	get_debugreg(d7, 7);

	/* Only print out debug registers if they are in their non-default state. */
	if (!((d0 == 0) && (d1 == 0) && (d2 == 0) && (d3 == 0) &&
	    (d6 == DR6_RESERVED) && (d7 == 0x400))) {
		printk(KERN_DEFAULT "DR0: %016lx DR1: %016lx DR2: %016lx\n",
		       d0, d1, d2);
		printk(KERN_DEFAULT "DR3: %016lx DR6: %016lx DR7: %016lx\n",
		       d3, d6, d7);
	}

	if (boot_cpu_has(X86_FEATURE_OSPKE))
		printk(KERN_DEFAULT "PKRU: %08x\n", read_pkru());
}

void release_thread(struct task_struct *dead_task)
{
	WARN_ON(dead_task->mm);
}

enum which_selector {
	FS,
	GS
};

/*
 * Saves the FS or GS base for an outgoing thread if FSGSBASE extensions are
 * not available.  The goal is to be reasonably fast on non-FSGSBASE systems.
 * It's forcibly inlined because it'll generate better code and this function
 * is hot.
 */
static __always_inline void save_base_legacy(struct task_struct *prev_p,
					     unsigned short selector,
					     enum which_selector which)
{
	if (likely(selector == 0)) {
		/*
		 * On Intel (without X86_BUG_NULL_SEG), the segment base could
		 * be the pre-existing saved base or it could be zero.  On AMD
		 * (with X86_BUG_NULL_SEG), the segment base could be almost
		 * anything.
		 *
		 * This branch is very hot (it's hit twice on almost every
		 * context switch between 64-bit programs), and avoiding
		 * the RDMSR helps a lot, so we just assume that whatever
		 * value is already saved is correct.  This matches historical
		 * Linux behavior, so it won't break existing applications.
		 *
		 * To avoid leaking state, on non-X86_BUG_NULL_SEG CPUs, if we
		 * report that the base is zero, it needs to actually be zero:
		 * see the corresponding logic in load_seg_legacy.
		 */
	} else {
		/*
		 * If the selector is 1, 2, or 3, then the base is zero on
		 * !X86_BUG_NULL_SEG CPUs and could be anything on
		 * X86_BUG_NULL_SEG CPUs.  In the latter case, Linux
		 * has never attempted to preserve the base across context
		 * switches.
		 *
		 * If selector > 3, then it refers to a real segment, and
		 * saving the base isn't necessary.
		 */
		if (which == FS)
			prev_p->thread.fsbase = 0;
		else
			prev_p->thread.gsbase = 0;
	}
}

static __always_inline void save_fsgs(struct task_struct *task)
{
	savesegment(fs, task->thread.fsindex);
	savesegment(gs, task->thread.gsindex);
	save_base_legacy(task, task->thread.fsindex, FS);
	save_base_legacy(task, task->thread.gsindex, GS);
}

#if IS_ENABLED(CONFIG_KVM)
/*
 * While a process is running,current->thread.fsbase and current->thread.gsbase
 * may not match the corresponding CPU registers (see save_base_legacy()). KVM
 * wants an efficient way to save and restore FSBASE and GSBASE.
 * When FSGSBASE extensions are enabled, this will have to use RD{FS,GS}BASE.
 */
void save_fsgs_for_kvm(void)
{
	save_fsgs(current);
}
EXPORT_SYMBOL_GPL(save_fsgs_for_kvm);
#endif

static __always_inline void loadseg(enum which_selector which,
				    unsigned short sel)
{
	if (which == FS)
		loadsegment(fs, sel);
	else
		load_gs_index(sel);
}

static __always_inline void load_seg_legacy(unsigned short prev_index,
					    unsigned long prev_base,
					    unsigned short next_index,
					    unsigned long next_base,
					    enum which_selector which)
{
	if (likely(next_index <= 3)) {
		/*
		 * The next task is using 64-bit TLS, is not using this
		 * segment at all, or is having fun with arcane CPU features.
		 */
		if (next_base == 0) {
			/*
			 * Nasty case: on AMD CPUs, we need to forcibly zero
			 * the base.
			 */
			if (static_cpu_has_bug(X86_BUG_NULL_SEG)) {
				loadseg(which, __USER_DS);
				loadseg(which, next_index);
			} else {
				/*
				 * We could try to exhaustively detect cases
				 * under which we can skip the segment load,
				 * but there's really only one case that matters
				 * for performance: if both the previous and
				 * next states are fully zeroed, we can skip
				 * the load.
				 *
				 * (This assumes that prev_base == 0 has no
				 * false positives.  This is the case on
				 * Intel-style CPUs.)
				 */
				if (likely(prev_index | next_index | prev_base))
					loadseg(which, next_index);
			}
		} else {
			if (prev_index != next_index)
				loadseg(which, next_index);
			wrmsrl(which == FS ? MSR_FS_BASE : MSR_KERNEL_GS_BASE,
			       next_base);
		}
	} else {
		/*
		 * The next task is using a real segment.  Loading the selector
		 * is sufficient.
		 */
		loadseg(which, next_index);
	}
}

static __always_inline void x86_fsgsbase_load(struct thread_struct *prev,
					      struct thread_struct *next)
{
	load_seg_legacy(prev->fsindex, prev->fsbase,
			next->fsindex, next->fsbase, FS);
	load_seg_legacy(prev->gsindex, prev->gsbase,
			next->gsindex, next->gsbase, GS);
}

static unsigned long x86_fsgsbase_read_task(struct task_struct *task,
					    unsigned short selector)
{
	unsigned short idx = selector >> 3;
	unsigned long base;

	if (likely((selector & SEGMENT_TI_MASK) == 0)) {
		if (unlikely(idx >= GDT_ENTRIES))
			return 0;

		/*
		 * There are no user segments in the GDT with nonzero bases
		 * other than the TLS segments.
		 */
		if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
			return 0;

		idx -= GDT_ENTRY_TLS_MIN;
		base = get_desc_base(&task->thread.tls_array[idx]);
	} else {
#ifdef CONFIG_MODIFY_LDT_SYSCALL
		struct ldt_struct *ldt;

		/*
		 * If performance here mattered, we could protect the LDT
		 * with RCU.  This is a slow path, though, so we can just
		 * take the mutex.
		 */
		mutex_lock(&task->mm->context.lock);
		ldt = task->mm->context.ldt;
		if (unlikely(idx >= ldt->nr_entries))
			base = 0;
		else
			base = get_desc_base(ldt->entries + idx);
		mutex_unlock(&task->mm->context.lock);
#else
		base = 0;
#endif
	}

	return base;
}

unsigned long x86_fsbase_read_task(struct task_struct *task)
{
	unsigned long fsbase;

	if (task == current)
		fsbase = x86_fsbase_read_cpu();
	else if (task->thread.fsindex == 0)
		fsbase = task->thread.fsbase;
	else
		fsbase = x86_fsgsbase_read_task(task, task->thread.fsindex);

	return fsbase;
}

unsigned long x86_gsbase_read_task(struct task_struct *task)
{
	unsigned long gsbase;

	if (task == current)
		gsbase = x86_gsbase_read_cpu_inactive();
	else if (task->thread.gsindex == 0)
		gsbase = task->thread.gsbase;
	else
		gsbase = x86_fsgsbase_read_task(task, task->thread.gsindex);

	return gsbase;
}

void x86_fsbase_write_task(struct task_struct *task, unsigned long fsbase)
{
	WARN_ON_ONCE(task == current);

	task->thread.fsbase = fsbase;
}

void x86_gsbase_write_task(struct task_struct *task, unsigned long gsbase)
{
	WARN_ON_ONCE(task == current);

	task->thread.gsbase = gsbase;
}

int copy_thread_tls(unsigned long clone_flags, unsigned long sp,
		unsigned long arg, struct task_struct *p, unsigned long tls)
{
	int err;
	struct pt_regs *childregs;
	struct fork_frame *fork_frame;
	struct inactive_task_frame *frame;
	struct task_struct *me = current;

	childregs = task_pt_regs(p);
	fork_frame = container_of(childregs, struct fork_frame, regs);
	frame = &fork_frame->frame;

	frame->bp = 0;
	frame->ret_addr = (unsigned long) ret_from_fork;
	p->thread.sp = (unsigned long) fork_frame;
	p->thread.io_bitmap_ptr = NULL;

	savesegment(gs, p->thread.gsindex);
	p->thread.gsbase = p->thread.gsindex ? 0 : me->thread.gsbase;
	savesegment(fs, p->thread.fsindex);
	p->thread.fsbase = p->thread.fsindex ? 0 : me->thread.fsbase;
	savesegment(es, p->thread.es);
	savesegment(ds, p->thread.ds);
	memset(p->thread.ptrace_bps, 0, sizeof(p->thread.ptrace_bps));

	if (unlikely(p->flags & PF_KTHREAD)) {
		/* kernel thread */
		memset(childregs, 0, sizeof(struct pt_regs));
		frame->bx = sp;		/* function */
		frame->r12 = arg;
		return 0;
	}
	frame->bx = 0;
	*childregs = *current_pt_regs();

	childregs->ax = 0;
	if (sp)
		childregs->sp = sp;

	err = -ENOMEM;
	if (unlikely(test_tsk_thread_flag(me, TIF_IO_BITMAP))) {
		p->thread.io_bitmap_ptr = kmemdup(me->thread.io_bitmap_ptr,
						  IO_BITMAP_BYTES, GFP_KERNEL);
		if (!p->thread.io_bitmap_ptr) {
			p->thread.io_bitmap_max = 0;
			return -ENOMEM;
		}
		set_tsk_thread_flag(p, TIF_IO_BITMAP);
	}

	/*
	 * Set a new TLS for the child thread?
	 */
	if (clone_flags & CLONE_SETTLS) {
#ifdef CONFIG_IA32_EMULATION
		if (in_ia32_syscall())
			err = do_set_thread_area(p, -1,
				(struct user_desc __user *)tls, 0);
		else
#endif
			err = do_arch_prctl_64(p, ARCH_SET_FS, tls);
		if (err)
			goto out;
	}
	err = 0;
out:
	if (err && p->thread.io_bitmap_ptr) {
		kfree(p->thread.io_bitmap_ptr);
		p->thread.io_bitmap_max = 0;
	}

	return err;
}

static void
start_thread_common(struct pt_regs *regs, unsigned long new_ip,
		    unsigned long new_sp,
		    unsigned int _cs, unsigned int _ss, unsigned int _ds)
{
	WARN_ON_ONCE(regs != current_pt_regs());

	if (static_cpu_has(X86_BUG_NULL_SEG)) {
		/* Loading zero below won't clear the base. */
		loadsegment(fs, __USER_DS);
		load_gs_index(__USER_DS);
	}

	loadsegment(fs, 0);
	loadsegment(es, _ds);
	loadsegment(ds, _ds);
	load_gs_index(0);

	regs->ip		= new_ip;
	regs->sp		= new_sp;
	regs->cs		= _cs;
	regs->ss		= _ss;
	regs->flags		= X86_EFLAGS_IF;
	force_iret();
}

void
start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
{
	start_thread_common(regs, new_ip, new_sp,
			    __USER_CS, __USER_DS, 0);
}
EXPORT_SYMBOL_GPL(start_thread);

#ifdef CONFIG_COMPAT
void compat_start_thread(struct pt_regs *regs, u32 new_ip, u32 new_sp)
{
	start_thread_common(regs, new_ip, new_sp,
			    test_thread_flag(TIF_X32)
			    ? __USER_CS : __USER32_CS,
			    __USER_DS, __USER_DS);
}
#endif

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * This could still be optimized:
 * - fold all the options into a flag word and test it with a single test.
 * - could test fs/gs bitsliced
 *
 * Kprobes not supported here. Set the probe on schedule instead.
 * Function graph tracer not supported too.
 */
__visible __notrace_funcgraph struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread;
	struct thread_struct *next = &next_p->thread;
	struct fpu *prev_fpu = &prev->fpu;
	struct fpu *next_fpu = &next->fpu;
	int cpu = smp_processor_id();

	WARN_ON_ONCE(IS_ENABLED(CONFIG_DEBUG_ENTRY) &&
		     this_cpu_read(irq_count) != -1);

	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_prepare(prev_fpu, cpu);

	/* We must save %fs and %gs before load_TLS() because
	 * %fs and %gs may be cleared by load_TLS().
	 *
	 * (e.g. xen_load_tls())
	 */
	save_fsgs(prev_p);

	/*
	 * Load TLS before restoring any segments so that segment loads
	 * reference the correct GDT entries.
	 */
	load_TLS(next, cpu);

	/*
	 * Leave lazy mode, flushing any hypercalls made here.  This
	 * must be done after loading TLS entries in the GDT but before
	 * loading segments that might reference them.
	 */
	arch_end_context_switch(next_p);

	/* Switch DS and ES.
	 *
	 * Reading them only returns the selectors, but writing them (if
	 * nonzero) loads the full descriptor from the GDT or LDT.  The
	 * LDT for next is loaded in switch_mm, and the GDT is loaded
	 * above.
	 *
	 * We therefore need to write new values to the segment
	 * registers on every context switch unless both the new and old
	 * values are zero.
	 *
	 * Note that we don't need to do anything for CS and SS, as
	 * those are saved and restored as part of pt_regs.
	 */
	savesegment(es, prev->es);
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es);

	savesegment(ds, prev->ds);
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	x86_fsgsbase_load(prev, next);

	/*
	 * Switch the PDA and FPU contexts.
	 */
	this_cpu_write(current_task, next_p);
	this_cpu_write(cpu_current_top_of_stack, task_top_of_stack(next_p));

	switch_fpu_finish(next_fpu);

	/* Reload sp0. */
	update_task_stack(next_p);

	switch_to_extra(prev_p, next_p);

#ifdef CONFIG_XEN_PV
	/*
	 * On Xen PV, IOPL bits in pt_regs->flags have no effect, and
	 * current_pt_regs()->flags may not match the current task's
	 * intended IOPL.  We need to switch it manually.
	 */
	if (unlikely(static_cpu_has(X86_FEATURE_XENPV) &&
		     prev->iopl != next->iopl))
		xen_set_iopl_mask(next->iopl);
#endif

	if (static_cpu_has_bug(X86_BUG_SYSRET_SS_ATTRS)) {
		/*
		 * AMD CPUs have a misfeature: SYSRET sets the SS selector but
		 * does not update the cached descriptor.  As a result, if we
		 * do SYSRET while SS is NULL, we'll end up in user mode with
		 * SS apparently equal to __USER_DS but actually unusable.
		 *
		 * The straightforward workaround would be to fix it up just
		 * before SYSRET, but that would slow down the system call
		 * fast paths.  Instead, we ensure that SS is never NULL in
		 * system call context.  We do this by replacing NULL SS
		 * selectors at every context switch.  SYSCALL sets up a valid
		 * SS, so the only way to get NULL is to re-enter the kernel
		 * from CPL 3 through an interrupt.  Since that can't happen
		 * in the same task as a running syscall, we are guaranteed to
		 * context switch between every interrupt vector entry and a
		 * subsequent SYSRET.
		 *
		 * We read SS first because SS reads are much faster than
		 * writes.  Out of caution, we force SS to __KERNEL_DS even if
		 * it previously had a different non-NULL value.
		 */
		unsigned short ss_sel;
		savesegment(ss, ss_sel);
		if (ss_sel != __KERNEL_DS)
			loadsegment(ss, __KERNEL_DS);
	}

	/* Load the Intel cache allocation PQR MSR. */
	resctrl_sched_in();

	return prev_p;
}

void set_personality_64bit(void)
{
	/* inherit personality from parent */

	/* Make sure to be in 64bit mode */
	clear_thread_flag(TIF_IA32);
	clear_thread_flag(TIF_ADDR32);
	clear_thread_flag(TIF_X32);
	/* Pretend that this comes from a 64bit execve */
	task_pt_regs(current)->orig_ax = __NR_execve;
	current_thread_info()->status &= ~TS_COMPAT;

	/* Ensure the corresponding mm is not marked. */
	if (current->mm)
		current->mm->context.ia32_compat = 0;

	/* TBD: overwrites user setup. Should have two bits.
	   But 64bit processes have always behaved this way,
	   so it's not too bad. The main problem is just that
	   32bit children are affected again. */
	current->personality &= ~READ_IMPLIES_EXEC;
}

static void __set_personality_x32(void)
{
#ifdef CONFIG_X86_X32
	clear_thread_flag(TIF_IA32);
	set_thread_flag(TIF_X32);
	if (current->mm)
		current->mm->context.ia32_compat = TIF_X32;
	current->personality &= ~READ_IMPLIES_EXEC;
	/*
	 * in_32bit_syscall() uses the presence of the x32 syscall bit
	 * flag to determine compat status.  The x86 mmap() code relies on
	 * the syscall bitness so set x32 syscall bit right here to make
	 * in_32bit_syscall() work during exec().
	 *
	 * Pretend to come from a x32 execve.
	 */
	task_pt_regs(current)->orig_ax = __NR_x32_execve | __X32_SYSCALL_BIT;
	current_thread_info()->status &= ~TS_COMPAT;
#endif
}

static void __set_personality_ia32(void)
{
#ifdef CONFIG_IA32_EMULATION
	set_thread_flag(TIF_IA32);
	clear_thread_flag(TIF_X32);
	if (current->mm)
		current->mm->context.ia32_compat = TIF_IA32;
	current->personality |= force_personality32;
	/* Prepare the first "return" to user space */
	task_pt_regs(current)->orig_ax = __NR_ia32_execve;
	current_thread_info()->status |= TS_COMPAT;
#endif
}

void set_personality_ia32(bool x32)
{
	/* Make sure to be in 32bit mode */
	set_thread_flag(TIF_ADDR32);

	if (x32)
		__set_personality_x32();
	else
		__set_personality_ia32();
}
EXPORT_SYMBOL_GPL(set_personality_ia32);

#ifdef CONFIG_CHECKPOINT_RESTORE
static long prctl_map_vdso(const struct vdso_image *image, unsigned long addr)
{
	int ret;

	ret = map_vdso_once(image, addr);
	if (ret)
		return ret;

	return (long)image->size;
}
#endif

long do_arch_prctl_64(struct task_struct *task, int option, unsigned long arg2)
{
	int ret = 0;

	switch (option) {
	case ARCH_SET_GS: {
		if (unlikely(arg2 >= TASK_SIZE_MAX))
			return -EPERM;

		preempt_disable();
		/*
		 * ARCH_SET_GS has always overwritten the index
		 * and the base. Zero is the most sensible value
		 * to put in the index, and is the only value that
		 * makes any sense if FSGSBASE is unavailable.
		 */
		if (task == current) {
			loadseg(GS, 0);
			x86_gsbase_write_cpu_inactive(arg2);

			/*
			 * On non-FSGSBASE systems, save_base_legacy() expects
			 * that we also fill in thread.gsbase.
			 */
			task->thread.gsbase = arg2;

		} else {
			task->thread.gsindex = 0;
			x86_gsbase_write_task(task, arg2);
		}
		preempt_enable();
		break;
	}
	case ARCH_SET_FS: {
		/*
		 * Not strictly needed for %fs, but do it for symmetry
		 * with %gs
		 */
		if (unlikely(arg2 >= TASK_SIZE_MAX))
			return -EPERM;

		preempt_disable();
		/*
		 * Set the selector to 0 for the same reason
		 * as %gs above.
		 */
		if (task == current) {
			loadseg(FS, 0);
			x86_fsbase_write_cpu(arg2);

			/*
			 * On non-FSGSBASE systems, save_base_legacy() expects
			 * that we also fill in thread.fsbase.
			 */
			task->thread.fsbase = arg2;
		} else {
			task->thread.fsindex = 0;
			x86_fsbase_write_task(task, arg2);
		}
		preempt_enable();
		break;
	}
	case ARCH_GET_FS: {
		unsigned long base = x86_fsbase_read_task(task);

		ret = put_user(base, (unsigned long __user *)arg2);
		break;
	}
	case ARCH_GET_GS: {
		unsigned long base = x86_gsbase_read_task(task);

		ret = put_user(base, (unsigned long __user *)arg2);
		break;
	}

#ifdef CONFIG_CHECKPOINT_RESTORE
# ifdef CONFIG_X86_X32_ABI
	case ARCH_MAP_VDSO_X32:
		return prctl_map_vdso(&vdso_image_x32, arg2);
# endif
# if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
	case ARCH_MAP_VDSO_32:
		return prctl_map_vdso(&vdso_image_32, arg2);
# endif
	case ARCH_MAP_VDSO_64:
		return prctl_map_vdso(&vdso_image_64, arg2);
#endif

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	long ret;

	ret = do_arch_prctl_64(current, option, arg2);
	if (ret == -EINVAL)
		ret = do_arch_prctl_common(current, option, arg2);

	return ret;
}

#ifdef CONFIG_IA32_EMULATION
COMPAT_SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	return do_arch_prctl_common(current, option, arg2);
}
#endif

unsigned long KSTK_ESP(struct task_struct *task)
{
	return task_pt_regs(task)->sp;
}
