// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 2000 Hewlett-Packard Co, Linuxcare Inc.
 * Copyright (C) 2000 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 * Copyright (C) 2008-2016 Helge Deller <deller@gmx.de>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/user.h>
#include <linux/personality.h>
#include <linux/regset.h>
#include <linux/security.h>
#include <linux/seccomp.h>
#include <linux/compat.h>
#include <linux/signal.h>
#include <linux/audit.h>

#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>

/* PSW bits we allow the debugger to modify */
#define USER_PSW_BITS	(PSW_N | PSW_B | PSW_V | PSW_CB)

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/*
 * These are our native regset flavors.
 */
enum parisc_regset {
	REGSET_GENERAL,
	REGSET_FP
};

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
	clear_tsk_thread_flag(task, TIF_BLOCKSTEP);

	/* make sure the trap bits are not set */
	pa_psw(task)->r = 0;
	pa_psw(task)->t = 0;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

/*
 * The following functions are called by ptrace_resume() when
 * enabling or disabling single/block tracing.
 */
void user_disable_single_step(struct task_struct *task)
{
	ptrace_disable(task);
}

void user_enable_single_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_BLOCKSTEP);
	set_tsk_thread_flag(task, TIF_SINGLESTEP);

	if (pa_psw(task)->n) {
		/* Nullified, just crank over the queue. */
		task_regs(task)->iaoq[0] = task_regs(task)->iaoq[1];
		task_regs(task)->iasq[0] = task_regs(task)->iasq[1];
		task_regs(task)->iaoq[1] = task_regs(task)->iaoq[0] + 4;
		pa_psw(task)->n = 0;
		pa_psw(task)->x = 0;
		pa_psw(task)->y = 0;
		pa_psw(task)->z = 0;
		pa_psw(task)->b = 0;
		ptrace_disable(task);
		/* Don't wake up the task, but let the
		   parent know something happened. */
		force_sig_fault(SIGTRAP, TRAP_TRACE,
				(void __user *) (task_regs(task)->iaoq[0] & ~3),
				task);
		/* notify_parent(task, SIGCHLD); */
		return;
	}

	/* Enable recovery counter traps.  The recovery counter
	 * itself will be set to zero on a task switch.  If the
	 * task is suspended on a syscall then the syscall return
	 * path will overwrite the recovery counter with a suitable
	 * value such that it traps once back in user space.  We
	 * disable interrupts in the tasks PSW here also, to avoid
	 * interrupts while the recovery counter is decrementing.
	 */
	pa_psw(task)->r = 1;
	pa_psw(task)->t = 0;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

void user_enable_block_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
	set_tsk_thread_flag(task, TIF_BLOCKSTEP);

	/* Enable taken branch trap. */
	pa_psw(task)->r = 0;
	pa_psw(task)->t = 1;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long __user *datap = (unsigned long __user *)data;
	unsigned long tmp;
	long ret = -EIO;

	switch (request) {

	/* Read the word at location addr in the USER area.  For ptraced
	   processes, the kernel saves all regs on a syscall. */
	case PTRACE_PEEKUSR:
		if ((addr & (sizeof(unsigned long)-1)) ||
		     addr >= sizeof(struct pt_regs))
			break;
		tmp = *(unsigned long *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, datap);
		break;

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		if (addr == PT_PSW) {
			/* Allow writing to Nullify, Divide-step-correction,
			 * and carry/borrow bits.
			 * BEWARE, if you set N, and then single step, it won't
			 * stop on the nullified instruction.
			 */
			data &= USER_PSW_BITS;
			task_regs(child)->gr[0] &= ~USER_PSW_BITS;
			task_regs(child)->gr[0] |= data;
			ret = 0;
			break;
		}

		if ((addr & (sizeof(unsigned long)-1)) ||
		     addr >= sizeof(struct pt_regs))
			break;
		if ((addr >= PT_GR1 && addr <= PT_GR31) ||
				addr == PT_IAOQ0 || addr == PT_IAOQ1 ||
				(addr >= PT_FR0 && addr <= PT_FR31 + 4) ||
				addr == PT_SAR) {
			*(unsigned long *) ((char *) task_regs(child) + addr) = data;
			ret = 0;
		}
		break;

	case PTRACE_GETREGS:	/* Get all gp regs from the child. */
		return copy_regset_to_user(child,
					   task_user_regset_view(current),
					   REGSET_GENERAL,
					   0, sizeof(struct user_regs_struct),
					   datap);

	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child,
					     task_user_regset_view(current),
					     REGSET_GENERAL,
					     0, sizeof(struct user_regs_struct),
					     datap);

	case PTRACE_GETFPREGS:	/* Get the child FPU state. */
		return copy_regset_to_user(child,
					   task_user_regset_view(current),
					   REGSET_FP,
					   0, sizeof(struct user_fp_struct),
					   datap);

	case PTRACE_SETFPREGS:	/* Set the child FPU state. */
		return copy_regset_from_user(child,
					     task_user_regset_view(current),
					     REGSET_FP,
					     0, sizeof(struct user_fp_struct),
					     datap);

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}


#ifdef CONFIG_COMPAT

/* This function is needed to translate 32 bit pt_regs offsets in to
 * 64 bit pt_regs offsets.  For example, a 32 bit gdb under a 64 bit kernel
 * will request offset 12 if it wants gr3, but the lower 32 bits of
 * the 64 bit kernels view of gr3 will be at offset 28 (3*8 + 4).
 * This code relies on a 32 bit pt_regs being comprised of 32 bit values
 * except for the fp registers which (a) are 64 bits, and (b) follow
 * the gr registers at the start of pt_regs.  The 32 bit pt_regs should
 * be half the size of the 64 bit pt_regs, plus 32*4 to allow for fr[]
 * being 64 bit in both cases.
 */

static compat_ulong_t translate_usr_offset(compat_ulong_t offset)
{
	if (offset < 0)
		return sizeof(struct pt_regs);
	else if (offset <= 32*4)	/* gr[0..31] */
		return offset * 2 + 4;
	else if (offset <= 32*4+32*8)	/* gr[0..31] + fr[0..31] */
		return offset + 32*4;
	else if (offset < sizeof(struct pt_regs)/2 + 32*4)
		return offset * 2 + 4 - 32*8;
	else
		return sizeof(struct pt_regs);
}

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t addr, compat_ulong_t data)
{
	compat_uint_t tmp;
	long ret = -EIO;

	switch (request) {

	case PTRACE_PEEKUSR:
		if (addr & (sizeof(compat_uint_t)-1))
			break;
		addr = translate_usr_offset(addr);
		if (addr >= sizeof(struct pt_regs))
			break;

		tmp = *(compat_uint_t *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, (compat_uint_t *) (unsigned long) data);
		break;

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		if (addr == PT_PSW) {
			/* Since PT_PSW==0, it is valid for 32 bit processes
			 * under 64 bit kernels as well.
			 */
			ret = arch_ptrace(child, request, addr, data);
		} else {
			if (addr & (sizeof(compat_uint_t)-1))
				break;
			addr = translate_usr_offset(addr);
			if (addr >= sizeof(struct pt_regs))
				break;
			if (addr >= PT_FR0 && addr <= PT_FR31 + 4) {
				/* Special case, fp regs are 64 bits anyway */
				*(__u64 *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			else if ((addr >= PT_GR1+4 && addr <= PT_GR31+4) ||
					addr == PT_IAOQ0+4 || addr == PT_IAOQ1+4 ||
					addr == PT_SAR+4) {
				/* Zero the top 32 bits */
				*(__u32 *) ((char *) task_regs(child) + addr - 4) = 0;
				*(__u32 *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
		}
		break;

	default:
		ret = compat_ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}
#endif

long do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE)) {
		int rc = tracehook_report_syscall_entry(regs);

		/*
		 * As tracesys_next does not set %r28 to -ENOSYS
		 * when %r20 is set to -1, initialize it here.
		 */
		regs->gr[28] = -ENOSYS;

		if (rc) {
			/*
			 * A nonzero return code from
			 * tracehook_report_syscall_entry() tells us
			 * to prevent the syscall execution.  Skip
			 * the syscall call and the syscall restart handling.
			 *
			 * Note that the tracer may also just change
			 * regs->gr[20] to an invalid syscall number,
			 * that is handled by tracesys_next.
			 */
			regs->gr[20] = -1UL;
			return -1;
		}
	}

	/* Do the secure computing check after ptrace. */
	if (secure_computing(NULL) == -1)
		return -1;

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->gr[20]);
#endif

#ifdef CONFIG_64BIT
	if (!is_compat_task())
		audit_syscall_entry(regs->gr[20], regs->gr[26], regs->gr[25],
				    regs->gr[24], regs->gr[23]);
	else
#endif
		audit_syscall_entry(regs->gr[20] & 0xffffffff,
			regs->gr[26] & 0xffffffff,
			regs->gr[25] & 0xffffffff,
			regs->gr[24] & 0xffffffff,
			regs->gr[23] & 0xffffffff);

	/*
	 * Sign extend the syscall number to 64bit since it may have been
	 * modified by a compat ptrace call
	 */
	return (int) ((u32) regs->gr[20]);
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	int stepping = test_thread_flag(TIF_SINGLESTEP) ||
		test_thread_flag(TIF_BLOCKSTEP);

	audit_syscall_exit(regs);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->gr[20]);
#endif

	if (stepping || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, stepping);
}


/*
 * regset functions.
 */

static int fpr_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_regs(target);
	__u64 *k = kbuf;
	__u64 __user *u = ubuf;
	__u64 reg;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < ELF_NFPREG; --count)
			*k++ = regs->fr[pos++];
	else
		for (; count > 0 && pos < ELF_NFPREG; --count)
			if (__put_user(regs->fr[pos++], u++))
				return -EFAULT;

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					ELF_NFPREG * sizeof(reg), -1);
}

static int fpr_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_regs(target);
	const __u64 *k = kbuf;
	const __u64 __user *u = ubuf;
	__u64 reg;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < ELF_NFPREG; --count)
			regs->fr[pos++] = *k++;
	else
		for (; count > 0 && pos < ELF_NFPREG; --count) {
			if (__get_user(reg, u++))
				return -EFAULT;
			regs->fr[pos++] = reg;
		}

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 ELF_NFPREG * sizeof(reg), -1);
}

#define RI(reg) (offsetof(struct user_regs_struct,reg) / sizeof(long))

static unsigned long get_reg(struct pt_regs *regs, int num)
{
	switch (num) {
	case RI(gr[0]) ... RI(gr[31]):	return regs->gr[num - RI(gr[0])];
	case RI(sr[0]) ... RI(sr[7]):	return regs->sr[num - RI(sr[0])];
	case RI(iasq[0]):		return regs->iasq[0];
	case RI(iasq[1]):		return regs->iasq[1];
	case RI(iaoq[0]):		return regs->iaoq[0];
	case RI(iaoq[1]):		return regs->iaoq[1];
	case RI(sar):			return regs->sar;
	case RI(iir):			return regs->iir;
	case RI(isr):			return regs->isr;
	case RI(ior):			return regs->ior;
	case RI(ipsw):			return regs->ipsw;
	case RI(cr27):			return regs->cr27;
	case RI(cr0):			return mfctl(0);
	case RI(cr24):			return mfctl(24);
	case RI(cr25):			return mfctl(25);
	case RI(cr26):			return mfctl(26);
	case RI(cr28):			return mfctl(28);
	case RI(cr29):			return mfctl(29);
	case RI(cr30):			return mfctl(30);
	case RI(cr31):			return mfctl(31);
	case RI(cr8):			return mfctl(8);
	case RI(cr9):			return mfctl(9);
	case RI(cr12):			return mfctl(12);
	case RI(cr13):			return mfctl(13);
	case RI(cr10):			return mfctl(10);
	case RI(cr15):			return mfctl(15);
	default:			return 0;
	}
}

static void set_reg(struct pt_regs *regs, int num, unsigned long val)
{
	switch (num) {
	case RI(gr[0]): /*
			 * PSW is in gr[0].
			 * Allow writing to Nullify, Divide-step-correction,
			 * and carry/borrow bits.
			 * BEWARE, if you set N, and then single step, it won't
			 * stop on the nullified instruction.
			 */
			val &= USER_PSW_BITS;
			regs->gr[0] &= ~USER_PSW_BITS;
			regs->gr[0] |= val;
			return;
	case RI(gr[1]) ... RI(gr[31]):
			regs->gr[num - RI(gr[0])] = val;
			return;
	case RI(iaoq[0]):
	case RI(iaoq[1]):
			regs->iaoq[num - RI(iaoq[0])] = val;
			return;
	case RI(sar):	regs->sar = val;
			return;
	default:	return;
#if 0
	/* do not allow to change any of the following registers (yet) */
	case RI(sr[0]) ... RI(sr[7]):	return regs->sr[num - RI(sr[0])];
	case RI(iasq[0]):		return regs->iasq[0];
	case RI(iasq[1]):		return regs->iasq[1];
	case RI(iir):			return regs->iir;
	case RI(isr):			return regs->isr;
	case RI(ior):			return regs->ior;
	case RI(ipsw):			return regs->ipsw;
	case RI(cr27):			return regs->cr27;
        case cr0, cr24, cr25, cr26, cr27, cr28, cr29, cr30, cr31;
        case cr8, cr9, cr12, cr13, cr10, cr15;
#endif
	}
}

static int gpr_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_regs(target);
	unsigned long *k = kbuf;
	unsigned long __user *u = ubuf;
	unsigned long reg;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < ELF_NGREG; --count)
			*k++ = get_reg(regs, pos++);
	else
		for (; count > 0 && pos < ELF_NGREG; --count)
			if (__put_user(get_reg(regs, pos++), u++))
				return -EFAULT;
	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					ELF_NGREG * sizeof(reg), -1);
}

static int gpr_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_regs(target);
	const unsigned long *k = kbuf;
	const unsigned long __user *u = ubuf;
	unsigned long reg;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < ELF_NGREG; --count)
			set_reg(regs, pos++, *k++);
	else
		for (; count > 0 && pos < ELF_NGREG; --count) {
			if (__get_user(reg, u++))
				return -EFAULT;
			set_reg(regs, pos++, reg);
		}

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 ELF_NGREG * sizeof(reg), -1);
}

static const struct user_regset native_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.get = gpr_get, .set = gpr_set
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG, .n = ELF_NFPREG,
		.size = sizeof(__u64), .align = sizeof(__u64),
		.get = fpr_get, .set = fpr_set
	}
};

static const struct user_regset_view user_parisc_native_view = {
	.name = "parisc", .e_machine = ELF_ARCH, .ei_osabi = ELFOSABI_LINUX,
	.regsets = native_regsets, .n = ARRAY_SIZE(native_regsets)
};

#ifdef CONFIG_64BIT
#include <linux/compat.h>

static int gpr32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_regs(target);
	compat_ulong_t *k = kbuf;
	compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < ELF_NGREG; --count)
			*k++ = get_reg(regs, pos++);
	else
		for (; count > 0 && pos < ELF_NGREG; --count)
			if (__put_user((compat_ulong_t) get_reg(regs, pos++), u++))
				return -EFAULT;

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					ELF_NGREG * sizeof(reg), -1);
}

static int gpr32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_regs(target);
	const compat_ulong_t *k = kbuf;
	const compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < ELF_NGREG; --count)
			set_reg(regs, pos++, *k++);
	else
		for (; count > 0 && pos < ELF_NGREG; --count) {
			if (__get_user(reg, u++))
				return -EFAULT;
			set_reg(regs, pos++, reg);
		}

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 ELF_NGREG * sizeof(reg), -1);
}

/*
 * These are the regset flavors matching the 32bit native set.
 */
static const struct user_regset compat_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS, .n = ELF_NGREG,
		.size = sizeof(compat_long_t), .align = sizeof(compat_long_t),
		.get = gpr32_get, .set = gpr32_set
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG, .n = ELF_NFPREG,
		.size = sizeof(__u64), .align = sizeof(__u64),
		.get = fpr_get, .set = fpr_set
	}
};

static const struct user_regset_view user_parisc_compat_view = {
	.name = "parisc", .e_machine = EM_PARISC, .ei_osabi = ELFOSABI_LINUX,
	.regsets = compat_regsets, .n = ARRAY_SIZE(compat_regsets)
};
#endif	/* CONFIG_64BIT */

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	BUILD_BUG_ON(sizeof(struct user_regs_struct)/sizeof(long) != ELF_NGREG);
	BUILD_BUG_ON(sizeof(struct user_fp_struct)/sizeof(__u64) != ELF_NFPREG);
#ifdef CONFIG_64BIT
	if (is_compat_task())
		return &user_parisc_compat_view;
#endif
	return &user_parisc_native_view;
}


/* HAVE_REGS_AND_STACK_ACCESS_API feature */

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r)    {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_INDEX(r,i) {.name = #r#i, .offset = offsetof(struct pt_regs, r[i])}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_INDEX(gr,0),
	REG_OFFSET_INDEX(gr,1),
	REG_OFFSET_INDEX(gr,2),
	REG_OFFSET_INDEX(gr,3),
	REG_OFFSET_INDEX(gr,4),
	REG_OFFSET_INDEX(gr,5),
	REG_OFFSET_INDEX(gr,6),
	REG_OFFSET_INDEX(gr,7),
	REG_OFFSET_INDEX(gr,8),
	REG_OFFSET_INDEX(gr,9),
	REG_OFFSET_INDEX(gr,10),
	REG_OFFSET_INDEX(gr,11),
	REG_OFFSET_INDEX(gr,12),
	REG_OFFSET_INDEX(gr,13),
	REG_OFFSET_INDEX(gr,14),
	REG_OFFSET_INDEX(gr,15),
	REG_OFFSET_INDEX(gr,16),
	REG_OFFSET_INDEX(gr,17),
	REG_OFFSET_INDEX(gr,18),
	REG_OFFSET_INDEX(gr,19),
	REG_OFFSET_INDEX(gr,20),
	REG_OFFSET_INDEX(gr,21),
	REG_OFFSET_INDEX(gr,22),
	REG_OFFSET_INDEX(gr,23),
	REG_OFFSET_INDEX(gr,24),
	REG_OFFSET_INDEX(gr,25),
	REG_OFFSET_INDEX(gr,26),
	REG_OFFSET_INDEX(gr,27),
	REG_OFFSET_INDEX(gr,28),
	REG_OFFSET_INDEX(gr,29),
	REG_OFFSET_INDEX(gr,30),
	REG_OFFSET_INDEX(gr,31),
	REG_OFFSET_INDEX(sr,0),
	REG_OFFSET_INDEX(sr,1),
	REG_OFFSET_INDEX(sr,2),
	REG_OFFSET_INDEX(sr,3),
	REG_OFFSET_INDEX(sr,4),
	REG_OFFSET_INDEX(sr,5),
	REG_OFFSET_INDEX(sr,6),
	REG_OFFSET_INDEX(sr,7),
	REG_OFFSET_INDEX(iasq,0),
	REG_OFFSET_INDEX(iasq,1),
	REG_OFFSET_INDEX(iaoq,0),
	REG_OFFSET_INDEX(iaoq,1),
	REG_OFFSET_NAME(cr27),
	REG_OFFSET_NAME(ksp),
	REG_OFFSET_NAME(kpc),
	REG_OFFSET_NAME(sar),
	REG_OFFSET_NAME(iir),
	REG_OFFSET_NAME(isr),
	REG_OFFSET_NAME(ior),
	REG_OFFSET_NAME(ipsw),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_query_register_name() - query register name from its offset
 * @offset:	the offset of a register in struct pt_regs.
 *
 * regs_query_register_name() returns the name of a register from its
 * offset in struct pt_regs. If the @offset is invalid, this returns NULL;
 */
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}
