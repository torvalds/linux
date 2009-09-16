/*
 * arch/sh/kernel/ptrace_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003 - 2008  Paul Mundt
 *
 * Started from SH3/4 version:
 *   SuperH version:   Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
 *   Original x86 implementation:
 *	By Ross Biro 1/23/92
 *	edited by Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/syscalls.h>
#include <asm/fpu.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/* This mask defines the bits of the SR which the user is not allowed to
   change, which are everything except S, Q, M, PR, SZ, FR. */
#define SR_MASK      (0xffff8cfd)

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * This routine will get a word from the user area in the process kernel stack.
 */
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)(task->thread.uregs);
	stack += offset;
	return (*((int *)stack));
}

static inline unsigned long
get_fpu_long(struct task_struct *task, unsigned long addr)
{
	unsigned long tmp;
	struct pt_regs *regs;
	regs = (struct pt_regs*)((unsigned char *)task + THREAD_SIZE) - 1;

	if (!tsk_used_math(task)) {
		if (addr == offsetof(struct user_fpu_struct, fpscr)) {
			tmp = FPSCR_INIT;
		} else {
			tmp = 0xffffffffUL; /* matches initial value in fpu.c */
		}
		return tmp;
	}

	if (last_task_used_math == task) {
		enable_fpu();
		save_fpu(task, regs);
		disable_fpu();
		last_task_used_math = 0;
		regs->sr |= SR_FD;
	}

	tmp = ((long *)&task->thread.fpu)[addr / sizeof(unsigned long)];
	return tmp;
}

/*
 * This routine will put a word into the user area in the process kernel stack.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
				 unsigned long data)
{
	unsigned char *stack;

	stack = (unsigned char *)(task->thread.uregs);
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

static inline int
put_fpu_long(struct task_struct *task, unsigned long addr, unsigned long data)
{
	struct pt_regs *regs;

	regs = (struct pt_regs*)((unsigned char *)task + THREAD_SIZE) - 1;

	if (!tsk_used_math(task)) {
		fpinit(&task->thread.fpu.hard);
		set_stopped_child_used_math(task);
	} else if (last_task_used_math == task) {
		enable_fpu();
		save_fpu(task, regs);
		disable_fpu();
		last_task_used_math = 0;
		regs->sr |= SR_FD;
	}

	((long *)&task->thread.fpu)[addr / sizeof(unsigned long)] = data;
	return 0;
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = child->thread.uregs;

	regs->sr |= SR_SSTEP;	/* auto-resetting upon exception */
}

void user_disable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = child->thread.uregs;

	regs->sr &= ~SR_SSTEP;
}

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* PC, SR, SYSCALL */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->pc,
				  0, 3 * sizeof(unsigned long long));

	/* R1 -> R63 */
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  regs->regs,
					  offsetof(struct pt_regs, regs[0]),
					  63 * sizeof(unsigned long long));
	/* TR0 -> TR7 */
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  regs->tregs,
					  offsetof(struct pt_regs, tregs[0]),
					  8 * sizeof(unsigned long long));

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       sizeof(struct pt_regs), -1);

	return ret;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* PC, SR, SYSCALL */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->pc,
				 0, 3 * sizeof(unsigned long long));

	/* R1 -> R63 */
	if (!ret && count > 0)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 regs->regs,
					 offsetof(struct pt_regs, regs[0]),
					 63 * sizeof(unsigned long long));

	/* TR0 -> TR7 */
	if (!ret && count > 0)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 regs->tregs,
					 offsetof(struct pt_regs, tregs[0]),
					 8 * sizeof(unsigned long long));

	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						sizeof(struct pt_regs), -1);

	return ret;
}

#ifdef CONFIG_SH_FPU
int fpregs_get(struct task_struct *target,
	       const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       void *kbuf, void __user *ubuf)
{
	int ret;

	ret = init_fpu(target);
	if (ret)
		return ret;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.fpu.hard, 0, -1);
}

static int fpregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int ret;

	ret = init_fpu(target);
	if (ret)
		return ret;

	set_stopped_child_used_math(target);

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.fpu.hard, 0, -1);
}

static int fpregs_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	return tsk_used_math(target) ? regset->n : 0;
}
#endif

/*
 * These are our native regset flavours.
 */
enum sh_regset {
	REGSET_GENERAL,
#ifdef CONFIG_SH_FPU
	REGSET_FPU,
#endif
};

static const struct user_regset sh_regsets[] = {
	/*
	 * Format is:
	 *	PC, SR, SYSCALL,
	 *	R1 --> R63,
	 *	TR0 --> TR7,
	 */
	[REGSET_GENERAL] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(long long),
		.align		= sizeof(long long),
		.get		= genregs_get,
		.set		= genregs_set,
	},

#ifdef CONFIG_SH_FPU
	[REGSET_FPU] = {
		.core_note_type	= NT_PRFPREG,
		.n		= sizeof(struct user_fpu_struct) /
				  sizeof(long long),
		.size		= sizeof(long long),
		.align		= sizeof(long long),
		.get		= fpregs_get,
		.set		= fpregs_set,
		.active		= fpregs_active,
	},
#endif
};

static const struct user_regset_view user_sh64_native_view = {
	.name		= "sh64",
	.e_machine	= EM_SH,
	.regsets	= sh_regsets,
	.n		= ARRAY_SIZE(sh_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_sh64_native_view;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		if (addr < sizeof(struct pt_regs))
			tmp = get_stack_long(child, addr);
		else if ((addr >= offsetof(struct user, fpu)) &&
			 (addr <  offsetof(struct user, u_fpvalid))) {
			tmp = get_fpu_long(child, addr - offsetof(struct user, fpu));
		} else if (addr == offsetof(struct user, u_fpvalid)) {
			tmp = !!tsk_used_math(child);
		} else {
			break;
		}
		ret = put_user(tmp, (unsigned long *)data);
		break;
	}

	case PTRACE_POKEUSR:
                /* write the word at location addr in the USER area. We must
                   disallow any changes to certain SR bits or u_fpvalid, since
                   this could crash the kernel or result in a security
                   loophole. */
		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		if (addr < sizeof(struct pt_regs)) {
			/* Ignore change of top 32 bits of SR */
			if (addr == offsetof (struct pt_regs, sr)+4)
			{
				ret = 0;
				break;
			}
			/* If lower 32 bits of SR, ignore non-user bits */
			if (addr == offsetof (struct pt_regs, sr))
			{
				long cursr = get_stack_long(child, addr);
				data &= ~(SR_MASK);
				data |= (cursr & SR_MASK);
			}
			ret = put_stack_long(child, addr, data);
		}
		else if ((addr >= offsetof(struct user, fpu)) &&
			 (addr <  offsetof(struct user, u_fpvalid))) {
			ret = put_fpu_long(child, addr - offsetof(struct user, fpu), data);
		}
		break;

	case PTRACE_GETREGS:
		return copy_regset_to_user(child, &user_sh64_native_view,
					   REGSET_GENERAL,
					   0, sizeof(struct pt_regs),
					   (void __user *)data);
	case PTRACE_SETREGS:
		return copy_regset_from_user(child, &user_sh64_native_view,
					     REGSET_GENERAL,
					     0, sizeof(struct pt_regs),
					     (const void __user *)data);
#ifdef CONFIG_SH_FPU
	case PTRACE_GETFPREGS:
		return copy_regset_to_user(child, &user_sh64_native_view,
					   REGSET_FPU,
					   0, sizeof(struct user_fpu_struct),
					   (void __user *)data);
	case PTRACE_SETFPREGS:
		return copy_regset_from_user(child, &user_sh64_native_view,
					     REGSET_FPU,
					     0, sizeof(struct user_fpu_struct),
					     (const void __user *)data);
#endif
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage int sh64_ptrace(long request, long pid, long addr, long data)
{
#define WPC_DBRMODE 0x0d104008
	static int first_call = 1;

	lock_kernel();
	if (first_call) {
		/* Set WPC.DBRMODE to 0.  This makes all debug events get
		 * delivered through RESVEC, i.e. into the handlers in entry.S.
		 * (If the kernel was downloaded using a remote gdb, WPC.DBRMODE
		 * would normally be left set to 1, which makes debug events get
		 * delivered through DBRVEC, i.e. into the remote gdb's
		 * handlers.  This prevents ptrace getting them, and confuses
		 * the remote gdb.) */
		printk("DBRMODE set to 0 to permit native debugging\n");
		poke_real_address_q(WPC_DBRMODE, 0);
		first_call = 0;
	}
	unlock_kernel();

	return sys_ptrace(request, pid, addr, data);
}

static inline int audit_arch(void)
{
	int arch = EM_SH;

#ifdef CONFIG_64BIT
	arch |= __AUDIT_ARCH_64BIT;
#endif
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	arch |= __AUDIT_ARCH_LE;
#endif

	return arch;
}

asmlinkage long long do_syscall_trace_enter(struct pt_regs *regs)
{
	long long ret = 0;

	secure_computing(regs->regs[9]);

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in regs->regs[0].
		 */
		ret = -1LL;

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->regs[9]);

	if (unlikely(current->audit_context))
		audit_syscall_entry(audit_arch(), regs->regs[1],
				    regs->regs[2], regs->regs[3],
				    regs->regs[4], regs->regs[5]);

	return ret ?: regs->regs[9];
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	if (unlikely(current->audit_context))
		audit_syscall_exit(AUDITSC_RESULT(regs->regs[9]),
				   regs->regs[9]);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->regs[9]);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);
}

/* Called with interrupts disabled */
asmlinkage void do_single_step(unsigned long long vec, struct pt_regs *regs)
{
	/* This is called after a single step exception (DEBUGSS).
	   There is no need to change the PC, as it is a post-execution
	   exception, as entry.S does not do anything to the PC for DEBUGSS.
	   We need to clear the Single Step setting in SR to avoid
	   continually stepping. */
	local_irq_enable();
	regs->sr &= ~SR_SSTEP;
	force_sig(SIGTRAP, current);
}

/* Called with interrupts disabled */
asmlinkage void do_software_break_point(unsigned long long vec,
					struct pt_regs *regs)
{
	/* We need to forward step the PC, to counteract the backstep done
	   in signal.c. */
	local_irq_enable();
	force_sig(SIGTRAP, current);
	regs->pc += 4;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}
