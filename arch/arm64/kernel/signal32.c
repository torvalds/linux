/*
 * Based on arch/arm/kernel/signal.c
 *
 * Copyright (C) 1995-2009 Russell King
 * Copyright (C) 2012 ARM Ltd.
 * Modified by Will Deacon <will.deacon@arm.com>
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

#include <linux/compat.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/ratelimit.h>

#include <asm/fpsimd.h>
#include <asm/signal32.h>
#include <asm/uaccess.h>
#include <asm/unistd32.h>

struct compat_sigaction {
	compat_uptr_t			sa_handler;
	compat_ulong_t			sa_flags;
	compat_uptr_t			sa_restorer;
	compat_sigset_t			sa_mask;
};

struct compat_old_sigaction {
	compat_uptr_t			sa_handler;
	compat_old_sigset_t		sa_mask;
	compat_ulong_t			sa_flags;
	compat_uptr_t			sa_restorer;
};

typedef struct compat_sigaltstack {
	compat_uptr_t			ss_sp;
	int				ss_flags;
	compat_size_t			ss_size;
} compat_stack_t;

struct compat_sigcontext {
	/* We always set these two fields to 0 */
	compat_ulong_t			trap_no;
	compat_ulong_t			error_code;

	compat_ulong_t			oldmask;
	compat_ulong_t			arm_r0;
	compat_ulong_t			arm_r1;
	compat_ulong_t			arm_r2;
	compat_ulong_t			arm_r3;
	compat_ulong_t			arm_r4;
	compat_ulong_t			arm_r5;
	compat_ulong_t			arm_r6;
	compat_ulong_t			arm_r7;
	compat_ulong_t			arm_r8;
	compat_ulong_t			arm_r9;
	compat_ulong_t			arm_r10;
	compat_ulong_t			arm_fp;
	compat_ulong_t			arm_ip;
	compat_ulong_t			arm_sp;
	compat_ulong_t			arm_lr;
	compat_ulong_t			arm_pc;
	compat_ulong_t			arm_cpsr;
	compat_ulong_t			fault_address;
};

struct compat_ucontext {
	compat_ulong_t			uc_flags;
	struct compat_ucontext		*uc_link;
	compat_stack_t			uc_stack;
	struct compat_sigcontext	uc_mcontext;
	compat_sigset_t			uc_sigmask;
	int		__unused[32 - (sizeof (compat_sigset_t) / sizeof (int))];
	compat_ulong_t	uc_regspace[128] __attribute__((__aligned__(8)));
};

struct compat_vfp_sigframe {
	compat_ulong_t	magic;
	compat_ulong_t	size;
	struct compat_user_vfp {
		compat_u64	fpregs[32];
		compat_ulong_t	fpscr;
	} ufp;
	struct compat_user_vfp_exc {
		compat_ulong_t	fpexc;
		compat_ulong_t	fpinst;
		compat_ulong_t	fpinst2;
	} ufp_exc;
} __attribute__((__aligned__(8)));

#define VFP_MAGIC		0x56465001
#define VFP_STORAGE_SIZE	sizeof(struct compat_vfp_sigframe)

struct compat_aux_sigframe {
	struct compat_vfp_sigframe	vfp;

	/* Something that isn't a valid magic number for any coprocessor.  */
	unsigned long			end_magic;
} __attribute__((__aligned__(8)));

struct compat_sigframe {
	struct compat_ucontext	uc;
	compat_ulong_t		retcode[2];
};

struct compat_rt_sigframe {
	struct compat_siginfo info;
	struct compat_sigframe sig;
};

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/*
 * For ARM syscalls, the syscall number has to be loaded into r7.
 * We do not support an OABI userspace.
 */
#define MOV_R7_NR_SIGRETURN	(0xe3a07000 | __NR_compat_sigreturn)
#define SVC_SYS_SIGRETURN	(0xef000000 | __NR_compat_sigreturn)
#define MOV_R7_NR_RT_SIGRETURN	(0xe3a07000 | __NR_compat_rt_sigreturn)
#define SVC_SYS_RT_SIGRETURN	(0xef000000 | __NR_compat_rt_sigreturn)

/*
 * For Thumb syscalls, we also pass the syscall number via r7. We therefore
 * need two 16-bit instructions.
 */
#define SVC_THUMB_SIGRETURN	(((0xdf00 | __NR_compat_sigreturn) << 16) | \
				   0x2700 | __NR_compat_sigreturn)
#define SVC_THUMB_RT_SIGRETURN	(((0xdf00 | __NR_compat_rt_sigreturn) << 16) | \
				   0x2700 | __NR_compat_rt_sigreturn)

const compat_ulong_t aarch32_sigret_code[6] = {
	/*
	 * AArch32 sigreturn code.
	 * We don't construct an OABI SWI - instead we just set the imm24 field
	 * to the EABI syscall number so that we create a sane disassembly.
	 */
	MOV_R7_NR_SIGRETURN,    SVC_SYS_SIGRETURN,    SVC_THUMB_SIGRETURN,
	MOV_R7_NR_RT_SIGRETURN, SVC_SYS_RT_SIGRETURN, SVC_THUMB_RT_SIGRETURN,
};

static inline int put_sigset_t(compat_sigset_t __user *uset, sigset_t *set)
{
	compat_sigset_t	cset;

	cset.sig[0] = set->sig[0] & 0xffffffffull;
	cset.sig[1] = set->sig[0] >> 32;

	return copy_to_user(uset, &cset, sizeof(*uset));
}

static inline int get_sigset_t(sigset_t *set,
			       const compat_sigset_t __user *uset)
{
	compat_sigset_t s32;

	if (copy_from_user(&s32, uset, sizeof(*uset)))
		return -EFAULT;

	set->sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	return 0;
}

int copy_siginfo_to_user32(compat_siginfo_t __user *to, siginfo_t *from)
{
	int err;

	if (!access_ok(VERIFY_WRITE, to, sizeof(*to)))
		return -EFAULT;

	/* If you change siginfo_t structure, please be sure
	 * this code is fixed accordingly.
	 * It should never copy any pad contained in the structure
	 * to avoid security leaks, but must copy the generic
	 * 3 ints plus the relevant union member.
	 * This routine must convert siginfo from 64bit to 32bit as well
	 * at the same time.
	 */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	if (from->si_code < 0)
		err |= __copy_to_user(&to->_sifields._pad, &from->_sifields._pad,
				      SI_PAD_SIZE);
	else switch (from->si_code & __SI_MASK) {
	case __SI_KILL:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	case __SI_TIMER:
		 err |= __put_user(from->si_tid, &to->si_tid);
		 err |= __put_user(from->si_overrun, &to->si_overrun);
		 err |= __put_user((compat_uptr_t)(unsigned long)from->si_ptr,
				   &to->si_ptr);
		break;
	case __SI_POLL:
		err |= __put_user(from->si_band, &to->si_band);
		err |= __put_user(from->si_fd, &to->si_fd);
		break;
	case __SI_FAULT:
		err |= __put_user((compat_uptr_t)(unsigned long)from->si_addr,
				  &to->si_addr);
#ifdef BUS_MCEERR_AO
		/*
		 * Other callers might not initialize the si_lsb field,
		 * so check explicitely for the right codes here.
		 */
		if (from->si_code == BUS_MCEERR_AR || from->si_code == BUS_MCEERR_AO)
			err |= __put_user(from->si_addr_lsb, &to->si_addr_lsb);
#endif
		break;
	case __SI_CHLD:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_status, &to->si_status);
		err |= __put_user(from->si_utime, &to->si_utime);
		err |= __put_user(from->si_stime, &to->si_stime);
		break;
	case __SI_RT: /* This is not generated by the kernel as of now. */
	case __SI_MESGQ: /* But this is */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user((compat_uptr_t)(unsigned long)from->si_ptr, &to->si_ptr);
		break;
	default: /* this is just in case for now ... */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	}
	return err;
}

int copy_siginfo_from_user32(siginfo_t *to, compat_siginfo_t __user *from)
{
	memset(to, 0, sizeof *to);

	if (copy_from_user(to, from, __ARCH_SI_PREAMBLE_SIZE) ||
	    copy_from_user(to->_sifields._pad,
			   from->_sifields._pad, SI_PAD_SIZE))
		return -EFAULT;

	return 0;
}

/*
 * VFP save/restore code.
 */
static int compat_preserve_vfp_context(struct compat_vfp_sigframe __user *frame)
{
	struct fpsimd_state *fpsimd = &current->thread.fpsimd_state;
	compat_ulong_t magic = VFP_MAGIC;
	compat_ulong_t size = VFP_STORAGE_SIZE;
	compat_ulong_t fpscr, fpexc;
	int err = 0;

	/*
	 * Save the hardware registers to the fpsimd_state structure.
	 * Note that this also saves V16-31, which aren't visible
	 * in AArch32.
	 */
	fpsimd_save_state(fpsimd);

	/* Place structure header on the stack */
	__put_user_error(magic, &frame->magic, err);
	__put_user_error(size, &frame->size, err);

	/*
	 * Now copy the FP registers. Since the registers are packed,
	 * we can copy the prefix we want (V0-V15) as it is.
	 * FIXME: Won't work if big endian.
	 */
	err |= __copy_to_user(&frame->ufp.fpregs, fpsimd->vregs,
			      sizeof(frame->ufp.fpregs));

	/* Create an AArch32 fpscr from the fpsr and the fpcr. */
	fpscr = (fpsimd->fpsr & VFP_FPSCR_STAT_MASK) |
		(fpsimd->fpcr & VFP_FPSCR_CTRL_MASK);
	__put_user_error(fpscr, &frame->ufp.fpscr, err);

	/*
	 * The exception register aren't available so we fake up a
	 * basic FPEXC and zero everything else.
	 */
	fpexc = (1 << 30);
	__put_user_error(fpexc, &frame->ufp_exc.fpexc, err);
	__put_user_error(0, &frame->ufp_exc.fpinst, err);
	__put_user_error(0, &frame->ufp_exc.fpinst2, err);

	return err ? -EFAULT : 0;
}

static int compat_restore_vfp_context(struct compat_vfp_sigframe __user *frame)
{
	struct fpsimd_state fpsimd;
	compat_ulong_t magic = VFP_MAGIC;
	compat_ulong_t size = VFP_STORAGE_SIZE;
	compat_ulong_t fpscr;
	int err = 0;

	__get_user_error(magic, &frame->magic, err);
	__get_user_error(size, &frame->size, err);

	if (err)
		return -EFAULT;
	if (magic != VFP_MAGIC || size != VFP_STORAGE_SIZE)
		return -EINVAL;

	/*
	 * Copy the FP registers into the start of the fpsimd_state.
	 * FIXME: Won't work if big endian.
	 */
	err |= __copy_from_user(fpsimd.vregs, frame->ufp.fpregs,
				sizeof(frame->ufp.fpregs));

	/* Extract the fpsr and the fpcr from the fpscr */
	__get_user_error(fpscr, &frame->ufp.fpscr, err);
	fpsimd.fpsr = fpscr & VFP_FPSCR_STAT_MASK;
	fpsimd.fpcr = fpscr & VFP_FPSCR_CTRL_MASK;

	/*
	 * We don't need to touch the exception register, so
	 * reload the hardware state.
	 */
	if (!err) {
		preempt_disable();
		fpsimd_load_state(&fpsimd);
		preempt_enable();
	}

	return err ? -EFAULT : 0;
}

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int compat_sys_sigsuspend(int restart, compat_ulong_t oldmask,
				     compat_old_sigset_t mask)
{
	sigset_t blocked;

	siginitset(&current->blocked, mask);
	return sigsuspend(&blocked);
}

asmlinkage int compat_sys_sigaction(int sig,
				    const struct compat_old_sigaction __user *act,
				    struct compat_old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_old_sigset_t mask;
	compat_uptr_t handler, restorer;

	if (act) {
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;

		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_handler),
			       &oact->sa_handler) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_restorer),
			       &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}

asmlinkage int compat_sys_rt_sigaction(int sig,
				       const struct compat_sigaction __user *act,
				       struct compat_sigaction __user *oact,
				       compat_size_t sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		compat_uptr_t handler, restorer;

		ret = get_user(handler, &act->sa_handler);
		new_ka.sa.sa_handler = compat_ptr(handler);
		ret |= get_user(restorer, &act->sa_restorer);
		new_ka.sa.sa_restorer = compat_ptr(restorer);
		ret |= get_sigset_t(&new_ka.sa.sa_mask, &act->sa_mask);
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		if (ret)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
	if (!ret && oact) {
		ret = put_user(ptr_to_compat(old_ka.sa.sa_handler), &oact->sa_handler);
		ret |= put_sigset_t(&oact->sa_mask, &old_ka.sa.sa_mask);
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
	}
	return ret;
}

int compat_do_sigaltstack(compat_uptr_t compat_uss, compat_uptr_t compat_uoss,
			  compat_ulong_t sp)
{
	compat_stack_t __user *newstack = compat_ptr(compat_uss);
	compat_stack_t __user *oldstack = compat_ptr(compat_uoss);
	compat_uptr_t ss_sp;
	int ret;
	mm_segment_t old_fs;
	stack_t uss, uoss;

	/* Marshall the compat new stack into a stack_t */
	if (newstack) {
		if (get_user(ss_sp, &newstack->ss_sp) ||
		    __get_user(uss.ss_flags, &newstack->ss_flags) ||
		    __get_user(uss.ss_size, &newstack->ss_size))
			return -EFAULT;
		uss.ss_sp = compat_ptr(ss_sp);
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	/* The __user pointer casts are valid because of the set_fs() */
	ret = do_sigaltstack(
		newstack ? (stack_t __user *) &uss : NULL,
		oldstack ? (stack_t __user *) &uoss : NULL,
		(unsigned long)sp);
	set_fs(old_fs);

	/* Convert the old stack_t into a compat stack. */
	if (!ret && oldstack &&
		(put_user(ptr_to_compat(uoss.ss_sp), &oldstack->ss_sp) ||
		 __put_user(uoss.ss_flags, &oldstack->ss_flags) ||
		 __put_user(uoss.ss_size, &oldstack->ss_size)))
		return -EFAULT;
	return ret;
}

static int compat_restore_sigframe(struct pt_regs *regs,
				   struct compat_sigframe __user *sf)
{
	int err;
	sigset_t set;
	struct compat_aux_sigframe __user *aux;

	err = get_sigset_t(&set, &sf->uc.uc_sigmask);
	if (err == 0) {
		sigdelsetmask(&set, ~_BLOCKABLE);
		set_current_blocked(&set);
	}

	__get_user_error(regs->regs[0], &sf->uc.uc_mcontext.arm_r0, err);
	__get_user_error(regs->regs[1], &sf->uc.uc_mcontext.arm_r1, err);
	__get_user_error(regs->regs[2], &sf->uc.uc_mcontext.arm_r2, err);
	__get_user_error(regs->regs[3], &sf->uc.uc_mcontext.arm_r3, err);
	__get_user_error(regs->regs[4], &sf->uc.uc_mcontext.arm_r4, err);
	__get_user_error(regs->regs[5], &sf->uc.uc_mcontext.arm_r5, err);
	__get_user_error(regs->regs[6], &sf->uc.uc_mcontext.arm_r6, err);
	__get_user_error(regs->regs[7], &sf->uc.uc_mcontext.arm_r7, err);
	__get_user_error(regs->regs[8], &sf->uc.uc_mcontext.arm_r8, err);
	__get_user_error(regs->regs[9], &sf->uc.uc_mcontext.arm_r9, err);
	__get_user_error(regs->regs[10], &sf->uc.uc_mcontext.arm_r10, err);
	__get_user_error(regs->regs[11], &sf->uc.uc_mcontext.arm_fp, err);
	__get_user_error(regs->regs[12], &sf->uc.uc_mcontext.arm_ip, err);
	__get_user_error(regs->compat_sp, &sf->uc.uc_mcontext.arm_sp, err);
	__get_user_error(regs->compat_lr, &sf->uc.uc_mcontext.arm_lr, err);
	__get_user_error(regs->pc, &sf->uc.uc_mcontext.arm_pc, err);
	__get_user_error(regs->pstate, &sf->uc.uc_mcontext.arm_cpsr, err);

	/*
	 * Avoid compat_sys_sigreturn() restarting.
	 */
	regs->syscallno = ~0UL;

	err |= !valid_user_regs(&regs->user_regs);

	aux = (struct compat_aux_sigframe __user *) sf->uc.uc_regspace;
	if (err == 0)
		err |= compat_restore_vfp_context(&aux->vfp);

	return err;
}

asmlinkage int compat_sys_sigreturn(struct pt_regs *regs)
{
	struct compat_sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->compat_sp & 7)
		goto badframe;

	frame = (struct compat_sigframe __user *)regs->compat_sp;

	if (!access_ok(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;

	if (compat_restore_sigframe(regs, frame))
		goto badframe;

	return regs->regs[0];

badframe:
	if (show_unhandled_signals)
		pr_info_ratelimited("%s[%d]: bad frame in %s: pc=%08llx sp=%08llx\n",
				    current->comm, task_pid_nr(current), __func__,
				    regs->pc, regs->sp);
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int compat_sys_rt_sigreturn(struct pt_regs *regs)
{
	struct compat_rt_sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->compat_sp & 7)
		goto badframe;

	frame = (struct compat_rt_sigframe __user *)regs->compat_sp;

	if (!access_ok(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;

	if (compat_restore_sigframe(regs, &frame->sig))
		goto badframe;

	if (compat_do_sigaltstack(ptr_to_compat(&frame->sig.uc.uc_stack),
				 ptr_to_compat((void __user *)NULL),
				 regs->compat_sp) == -EFAULT)
		goto badframe;

	return regs->regs[0];

badframe:
	if (show_unhandled_signals)
		pr_info_ratelimited("%s[%d]: bad frame in %s: pc=%08llx sp=%08llx\n",
				    current->comm, task_pid_nr(current), __func__,
				    regs->pc, regs->sp);
	force_sig(SIGSEGV, current);
	return 0;
}

static void __user *compat_get_sigframe(struct k_sigaction *ka,
					struct pt_regs *regs,
					int framesize)
{
	compat_ulong_t sp = regs->compat_sp;
	void __user *frame;

	/*
	 * This is the X/Open sanctioned signal stack switching.
	 */
	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	frame = compat_ptr((compat_uptr_t)((sp - framesize) & ~7));

	/*
	 * Check that we can actually write to the signal frame.
	 */
	if (!access_ok(VERIFY_WRITE, frame, framesize))
		frame = NULL;

	return frame;
}

static void compat_setup_return(struct pt_regs *regs, struct k_sigaction *ka,
				compat_ulong_t __user *rc, void __user *frame,
				int usig)
{
	compat_ulong_t handler = ptr_to_compat(ka->sa.sa_handler);
	compat_ulong_t retcode;
	compat_ulong_t spsr = regs->pstate & ~PSR_f;
	int thumb;

	/* Check if the handler is written for ARM or Thumb */
	thumb = handler & 1;

	if (thumb) {
		spsr |= COMPAT_PSR_T_BIT;
		spsr &= ~COMPAT_PSR_IT_MASK;
	} else {
		spsr &= ~COMPAT_PSR_T_BIT;
	}

	if (ka->sa.sa_flags & SA_RESTORER) {
		retcode = ptr_to_compat(ka->sa.sa_restorer);
	} else {
		/* Set up sigreturn pointer */
		unsigned int idx = thumb << 1;

		if (ka->sa.sa_flags & SA_SIGINFO)
			idx += 3;

		retcode = AARCH32_VECTORS_BASE +
			  AARCH32_KERN_SIGRET_CODE_OFFSET +
			  (idx << 2) + thumb;
	}

	regs->regs[0]	= usig;
	regs->compat_sp	= ptr_to_compat(frame);
	regs->compat_lr	= retcode;
	regs->pc	= handler;
	regs->pstate	= spsr;
}

static int compat_setup_sigframe(struct compat_sigframe __user *sf,
				 struct pt_regs *regs, sigset_t *set)
{
	struct compat_aux_sigframe __user *aux;
	int err = 0;

	__put_user_error(regs->regs[0], &sf->uc.uc_mcontext.arm_r0, err);
	__put_user_error(regs->regs[1], &sf->uc.uc_mcontext.arm_r1, err);
	__put_user_error(regs->regs[2], &sf->uc.uc_mcontext.arm_r2, err);
	__put_user_error(regs->regs[3], &sf->uc.uc_mcontext.arm_r3, err);
	__put_user_error(regs->regs[4], &sf->uc.uc_mcontext.arm_r4, err);
	__put_user_error(regs->regs[5], &sf->uc.uc_mcontext.arm_r5, err);
	__put_user_error(regs->regs[6], &sf->uc.uc_mcontext.arm_r6, err);
	__put_user_error(regs->regs[7], &sf->uc.uc_mcontext.arm_r7, err);
	__put_user_error(regs->regs[8], &sf->uc.uc_mcontext.arm_r8, err);
	__put_user_error(regs->regs[9], &sf->uc.uc_mcontext.arm_r9, err);
	__put_user_error(regs->regs[10], &sf->uc.uc_mcontext.arm_r10, err);
	__put_user_error(regs->regs[11], &sf->uc.uc_mcontext.arm_fp, err);
	__put_user_error(regs->regs[12], &sf->uc.uc_mcontext.arm_ip, err);
	__put_user_error(regs->compat_sp, &sf->uc.uc_mcontext.arm_sp, err);
	__put_user_error(regs->compat_lr, &sf->uc.uc_mcontext.arm_lr, err);
	__put_user_error(regs->pc, &sf->uc.uc_mcontext.arm_pc, err);
	__put_user_error(regs->pstate, &sf->uc.uc_mcontext.arm_cpsr, err);

	__put_user_error((compat_ulong_t)0, &sf->uc.uc_mcontext.trap_no, err);
	__put_user_error((compat_ulong_t)0, &sf->uc.uc_mcontext.error_code, err);
	__put_user_error(current->thread.fault_address, &sf->uc.uc_mcontext.fault_address, err);
	__put_user_error(set->sig[0], &sf->uc.uc_mcontext.oldmask, err);

	err |= put_sigset_t(&sf->uc.uc_sigmask, set);

	aux = (struct compat_aux_sigframe __user *) sf->uc.uc_regspace;

	if (err == 0)
		err |= compat_preserve_vfp_context(&aux->vfp);
	__put_user_error(0, &aux->end_magic, err);

	return err;
}

/*
 * 32-bit signal handling routines called from signal.c
 */
int compat_setup_rt_frame(int usig, struct k_sigaction *ka, siginfo_t *info,
			  sigset_t *set, struct pt_regs *regs)
{
	struct compat_rt_sigframe __user *frame;
	compat_stack_t stack;
	int err = 0;

	frame = compat_get_sigframe(ka, regs, sizeof(*frame));

	if (!frame)
		return 1;

	err |= copy_siginfo_to_user32(&frame->info, info);

	__put_user_error(0, &frame->sig.uc.uc_flags, err);
	__put_user_error(NULL, &frame->sig.uc.uc_link, err);

	memset(&stack, 0, sizeof(stack));
	stack.ss_sp = (compat_uptr_t)current->sas_ss_sp;
	stack.ss_flags = sas_ss_flags(regs->compat_sp);
	stack.ss_size = current->sas_ss_size;
	err |= __copy_to_user(&frame->sig.uc.uc_stack, &stack, sizeof(stack));

	err |= compat_setup_sigframe(&frame->sig, regs, set);

	if (err == 0) {
		compat_setup_return(regs, ka, frame->sig.retcode, frame, usig);
		regs->regs[1] = (compat_ulong_t)(unsigned long)&frame->info;
		regs->regs[2] = (compat_ulong_t)(unsigned long)&frame->sig.uc;
	}

	return err;
}

int compat_setup_frame(int usig, struct k_sigaction *ka, sigset_t *set,
		       struct pt_regs *regs)
{
	struct compat_sigframe __user *frame;
	int err = 0;

	frame = compat_get_sigframe(ka, regs, sizeof(*frame));

	if (!frame)
		return 1;

	__put_user_error(0x5ac3c35a, &frame->uc.uc_flags, err);

	err |= compat_setup_sigframe(frame, regs, set);
	if (err == 0)
		compat_setup_return(regs, ka, frame->retcode, frame, usig);

	return err;
}

/*
 * RT signals don't have generic compat wrappers.
 * See arch/powerpc/kernel/signal_32.c
 */
asmlinkage int compat_sys_rt_sigprocmask(int how, compat_sigset_t __user *set,
					 compat_sigset_t __user *oset,
					 compat_size_t sigsetsize)
{
	sigset_t s;
	sigset_t __user *up;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set) {
		if (get_sigset_t(&s, set))
			return -EFAULT;
	}

	set_fs(KERNEL_DS);
	/* This is valid because of the set_fs() */
	up = (sigset_t __user *) &s;
	ret = sys_rt_sigprocmask(how, set ? up : NULL, oset ? up : NULL,
				 sigsetsize);
	set_fs(old_fs);
	if (ret)
		return ret;
	if (oset) {
		if (put_sigset_t(oset, &s))
			return -EFAULT;
	}
	return 0;
}

asmlinkage int compat_sys_rt_sigpending(compat_sigset_t __user *set,
					compat_size_t sigsetsize)
{
	sigset_t s;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	/* The __user pointer cast is valid because of the set_fs() */
	ret = sys_rt_sigpending((sigset_t __user *) &s, sigsetsize);
	set_fs(old_fs);
	if (!ret) {
		if (put_sigset_t(set, &s))
			return -EFAULT;
	}
	return ret;
}

asmlinkage int compat_sys_rt_sigqueueinfo(int pid, int sig,
					  compat_siginfo_t __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();

	ret = copy_siginfo_from_user32(&info, uinfo);
	if (unlikely(ret))
		return ret;

	set_fs (KERNEL_DS);
	/* The __user pointer cast is valid because of the set_fs() */
	ret = sys_rt_sigqueueinfo(pid, sig, (siginfo_t __user *) &info);
	set_fs (old_fs);
	return ret;
}

void compat_setup_restart_syscall(struct pt_regs *regs)
{
       regs->regs[7] = __NR_compat_restart_syscall;
}
