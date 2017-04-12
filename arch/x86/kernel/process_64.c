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
#include <asm/intel_rdt.h>

__visible DEFINE_PER_CPU(unsigned long, rsp_scratch);

/* Prints also some state that isn't saved in the pt_regs */
void __show_regs(struct pt_regs *regs, int all)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
	unsigned long d0, d1, d2, d3, d6, d7;
	unsigned int fsindex, gsindex;
	unsigned int ds, cs, es;

	printk(KERN_DEFAULT "RIP: %04lx:%pS\n", regs->cs & 0xffff,
		(void *)regs->ip);
	printk(KERN_DEFAULT "RSP: %04lx:%016lx EFLAGS: %08lx", regs->ss,
		regs->sp, regs->flags);
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

	asm("movl %%ds,%0" : "=r" (ds));
	asm("movl %%cs,%0" : "=r" (cs));
	asm("movl %%es,%0" : "=r" (es));
	asm("movl %%fs,%0" : "=r" (fsindex));
	asm("movl %%gs,%0" : "=r" (gsindex));

	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs);
	rdmsrl(MSR_KERNEL_GS_BASE, shadowgs);

	if (!all)
		return;

	cr0 = read_cr0();
	cr2 = read_cr2();
	cr3 = read_cr3();
	cr4 = __read_cr4();

	printk(KERN_DEFAULT "FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n",
	       fs, fsindex, gs, gsindex, shadowgs);
	printk(KERN_DEFAULT "CS:  %04x DS: %04x ES: %04x CR0: %016lx\n", cs, ds,
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
	if (dead_task->mm) {
#ifdef CONFIG_MODIFY_LDT_SYSCALL
		if (dead_task->mm->context.ldt) {
			pr_warn("WARNING: dead process %s still has LDT? <%p/%d>\n",
				dead_task->comm,
				dead_task->mm->context.ldt->entries,
				dead_task->mm->context.ldt->size);
			BUG();
		}
#endif
	}
}

int copy_thread_tls(unsigned long clone_flags, unsigned long sp,
		unsigned long arg, struct task_struct *p, unsigned long tls)
{
	int err;
	struct pt_regs *childregs;
	struct fork_frame *fork_frame;
	struct inactive_task_frame *frame;
	struct task_struct *me = current;

	p->thread.sp0 = (unsigned long)task_stack_page(p) + THREAD_SIZE;
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
			err = do_arch_prctl(p, ARCH_SET_FS, tls);
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
	struct tss_struct *tss = &per_cpu(cpu_tss, cpu);
	unsigned prev_fsindex, prev_gsindex;

	switch_fpu_prepare(prev_fpu, cpu);

	/* We must save %fs and %gs before load_TLS() because
	 * %fs and %gs may be cleared by load_TLS().
	 *
	 * (e.g. xen_load_tls())
	 */
	savesegment(fs, prev_fsindex);
	savesegment(gs, prev_gsindex);

	/*
	 * Load TLS before restoring any segments so that segment loads
	 * reference the correct GDT entries.
	 */
	load_TLS(next, cpu);

	/*
	 * Leave lazy mode, flushing any hypercalls made here.  This
	 * must be done after loading TLS entries in the GDT but before
	 * loading segments that might reference them, and and it must
	 * be done before fpu__restore(), so the TS bit is up to
	 * date.
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

	/*
	 * Switch FS and GS.
	 *
	 * These are even more complicated than DS and ES: they have
	 * 64-bit bases are that controlled by arch_prctl.  The bases
	 * don't necessarily match the selectors, as user code can do
	 * any number of things to cause them to be inconsistent.
	 *
	 * We don't promise to preserve the bases if the selectors are
	 * nonzero.  We also don't promise to preserve the base if the
	 * selector is zero and the base doesn't match whatever was
	 * most recently passed to ARCH_SET_FS/GS.  (If/when the
	 * FSGSBASE instructions are enabled, we'll need to offer
	 * stronger guarantees.)
	 *
	 * As an invariant,
	 * (fsbase != 0 && fsindex != 0) || (gsbase != 0 && gsindex != 0) is
	 * impossible.
	 */
	if (next->fsindex) {
		/* Loading a nonzero value into FS sets the index and base. */
		loadsegment(fs, next->fsindex);
	} else {
		if (next->fsbase) {
			/* Next index is zero but next base is nonzero. */
			if (prev_fsindex)
				loadsegment(fs, 0);
			wrmsrl(MSR_FS_BASE, next->fsbase);
		} else {
			/* Next base and index are both zero. */
			if (static_cpu_has_bug(X86_BUG_NULL_SEG)) {
				/*
				 * We don't know the previous base and can't
				 * find out without RDMSR.  Forcibly clear it.
				 */
				loadsegment(fs, __USER_DS);
				loadsegment(fs, 0);
			} else {
				/*
				 * If the previous index is zero and ARCH_SET_FS
				 * didn't change the base, then the base is
				 * also zero and we don't need to do anything.
				 */
				if (prev->fsbase || prev_fsindex)
					loadsegment(fs, 0);
			}
		}
	}
	/*
	 * Save the old state and preserve the invariant.
	 * NB: if prev_fsindex == 0, then we can't reliably learn the base
	 * without RDMSR because Intel user code can zero it without telling
	 * us and AMD user code can program any 32-bit value without telling
	 * us.
	 */
	if (prev_fsindex)
		prev->fsbase = 0;
	prev->fsindex = prev_fsindex;

	if (next->gsindex) {
		/* Loading a nonzero value into GS sets the index and base. */
		load_gs_index(next->gsindex);
	} else {
		if (next->gsbase) {
			/* Next index is zero but next base is nonzero. */
			if (prev_gsindex)
				load_gs_index(0);
			wrmsrl(MSR_KERNEL_GS_BASE, next->gsbase);
		} else {
			/* Next base and index are both zero. */
			if (static_cpu_has_bug(X86_BUG_NULL_SEG)) {
				/*
				 * We don't know the previous base and can't
				 * find out without RDMSR.  Forcibly clear it.
				 *
				 * This contains a pointless SWAPGS pair.
				 * Fixing it would involve an explicit check
				 * for Xen or a new pvop.
				 */
				load_gs_index(__USER_DS);
				load_gs_index(0);
			} else {
				/*
				 * If the previous index is zero and ARCH_SET_GS
				 * didn't change the base, then the base is
				 * also zero and we don't need to do anything.
				 */
				if (prev->gsbase || prev_gsindex)
					load_gs_index(0);
			}
		}
	}
	/*
	 * Save the old state and preserve the invariant.
	 * NB: if prev_gsindex == 0, then we can't reliably learn the base
	 * without RDMSR because Intel user code can zero it without telling
	 * us and AMD user code can program any 32-bit value without telling
	 * us.
	 */
	if (prev_gsindex)
		prev->gsbase = 0;
	prev->gsindex = prev_gsindex;

	switch_fpu_finish(next_fpu, cpu);

	/*
	 * Switch the PDA and FPU contexts.
	 */
	this_cpu_write(current_task, next_p);

	/* Reload esp0 and ss1.  This changes current_thread_info(). */
	load_sp0(tss, next);

	/*
	 * Now maybe reload the debug registers and handle I/O bitmaps
	 */
	if (unlikely(task_thread_info(next_p)->flags & _TIF_WORK_CTXSW_NEXT ||
		     task_thread_info(prev_p)->flags & _TIF_WORK_CTXSW_PREV))
		__switch_to_xtra(prev_p, next_p, tss);

#ifdef CONFIG_XEN
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
	intel_rdt_sched_in();

	return prev_p;
}

void set_personality_64bit(void)
{
	/* inherit personality from parent */

	/* Make sure to be in 64bit mode */
	clear_thread_flag(TIF_IA32);
	clear_thread_flag(TIF_ADDR32);
	clear_thread_flag(TIF_X32);

	/* Ensure the corresponding mm is not marked. */
	if (current->mm)
		current->mm->context.ia32_compat = 0;

	/* TBD: overwrites user setup. Should have two bits.
	   But 64bit processes have always behaved this way,
	   so it's not too bad. The main problem is just that
	   32bit childs are affected again. */
	current->personality &= ~READ_IMPLIES_EXEC;
}

void set_personality_ia32(bool x32)
{
	/* inherit personality from parent */

	/* Make sure to be in 32bit mode */
	set_thread_flag(TIF_ADDR32);

	/* Mark the associated mm as containing 32-bit tasks. */
	if (x32) {
		clear_thread_flag(TIF_IA32);
		set_thread_flag(TIF_X32);
		if (current->mm)
			current->mm->context.ia32_compat = TIF_X32;
		current->personality &= ~READ_IMPLIES_EXEC;
		/* in_compat_syscall() uses the presence of the x32
		   syscall bit flag to determine compat status */
		current->thread.status &= ~TS_COMPAT;
	} else {
		set_thread_flag(TIF_IA32);
		clear_thread_flag(TIF_X32);
		if (current->mm)
			current->mm->context.ia32_compat = TIF_IA32;
		current->personality |= force_personality32;
		/* Prepare the first "return" to user space */
		current->thread.status |= TS_COMPAT;
	}
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

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr)
{
	int ret = 0;
	int doit = task == current;
	int cpu;

	switch (code) {
	case ARCH_SET_GS:
		if (addr >= TASK_SIZE_MAX)
			return -EPERM;
		cpu = get_cpu();
		task->thread.gsindex = 0;
		task->thread.gsbase = addr;
		if (doit) {
			load_gs_index(0);
			ret = wrmsrl_safe(MSR_KERNEL_GS_BASE, addr);
		}
		put_cpu();
		break;
	case ARCH_SET_FS:
		/* Not strictly needed for fs, but do it for symmetry
		   with gs */
		if (addr >= TASK_SIZE_MAX)
			return -EPERM;
		cpu = get_cpu();
		task->thread.fsindex = 0;
		task->thread.fsbase = addr;
		if (doit) {
			/* set the selector to 0 to not confuse __switch_to */
			loadsegment(fs, 0);
			ret = wrmsrl_safe(MSR_FS_BASE, addr);
		}
		put_cpu();
		break;
	case ARCH_GET_FS: {
		unsigned long base;
		if (doit)
			rdmsrl(MSR_FS_BASE, base);
		else
			base = task->thread.fsbase;
		ret = put_user(base, (unsigned long __user *)addr);
		break;
	}
	case ARCH_GET_GS: {
		unsigned long base;
		if (doit)
			rdmsrl(MSR_KERNEL_GS_BASE, base);
		else
			base = task->thread.gsbase;
		ret = put_user(base, (unsigned long __user *)addr);
		break;
	}

#ifdef CONFIG_CHECKPOINT_RESTORE
# ifdef CONFIG_X86_X32_ABI
	case ARCH_MAP_VDSO_X32:
		return prctl_map_vdso(&vdso_image_x32, addr);
# endif
# if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
	case ARCH_MAP_VDSO_32:
		return prctl_map_vdso(&vdso_image_32, addr);
# endif
	case ARCH_MAP_VDSO_64:
		return prctl_map_vdso(&vdso_image_64, addr);
#endif

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

long sys_arch_prctl(int code, unsigned long addr)
{
	return do_arch_prctl(current, code, addr);
}

unsigned long KSTK_ESP(struct task_struct *task)
{
	return task_pt_regs(task)->sp;
}
