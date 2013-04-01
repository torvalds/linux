/*
 * arch/sh/kernel/signal_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003 - 2008  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/tracehook.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/fpu.h>

#define REG_RET 9
#define REG_ARG1 2
#define REG_ARG2 3
#define REG_ARG3 4
#define REG_SP 15
#define REG_PR 18
#define REF_REG_RET regs->regs[REG_RET]
#define REF_REG_SP regs->regs[REG_SP]
#define DEREF_REG_PR regs->regs[REG_PR]

#define DEBUG_SIG 0

static void
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
		struct pt_regs * regs);

static inline void
handle_syscall_restart(struct pt_regs *regs, struct sigaction *sa)
{
	/* If we're not from a syscall, bail out */
	if (regs->syscall_nr < 0)
		return;

	/* check for system call restart.. */
	switch (regs->regs[REG_RET]) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
		no_system_call_restart:
			regs->regs[REG_RET] = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(sa->sa_flags & SA_RESTART))
				goto no_system_call_restart;
		/* fallthrough */
		case -ERESTARTNOINTR:
			/* Decode syscall # */
			regs->regs[REG_RET] = regs->syscall_nr;
			regs->pc -= 4;
			break;
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
static void do_signal(struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return;

	signr = get_signal_to_deliver(&info, &ka, regs, 0);
	if (signr > 0) {
		handle_syscall_restart(regs, &ka.sa);

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &info, &ka, regs);
		return;
	}

	/* Did we come from a system call? */
	if (regs->syscall_nr >= 0) {
		/* Restart the system call - no handlers present */
		switch (regs->regs[REG_RET]) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			/* Decode Syscall # */
			regs->regs[REG_RET] = regs->syscall_nr;
			regs->pc -= 4;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->regs[REG_RET] = __NR_restart_syscall;
			regs->pc -= 4;
			break;
		}
	}

	/* No signal to deliver -- put the saved sigmask back */
	restore_saved_sigmask();
}

/*
 * Do a signal return; undo the signal stack.
 */
struct sigframe {
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	long long retcode[2];
};

struct rt_sigframe {
	struct siginfo __user *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	long long retcode[2];
};

#ifdef CONFIG_SH_FPU
static inline int
restore_sigcontext_fpu(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;
	int fpvalid;

	err |= __get_user (fpvalid, &sc->sc_fpvalid);
	conditional_used_math(fpvalid);
	if (! fpvalid)
		return err;

	if (current == last_task_used_math) {
		last_task_used_math = NULL;
		regs->sr |= SR_FD;
	}

	err |= __copy_from_user(&current->thread.xstate->hardfpu, &sc->sc_fpregs[0],
				(sizeof(long long) * 32) + (sizeof(int) * 1));

	return err;
}

static inline int
setup_sigcontext_fpu(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;
	int fpvalid;

	fpvalid = !!used_math();
	err |= __put_user(fpvalid, &sc->sc_fpvalid);
	if (! fpvalid)
		return err;

	if (current == last_task_used_math) {
		enable_fpu();
		save_fpu(current);
		disable_fpu();
		last_task_used_math = NULL;
		regs->sr |= SR_FD;
	}

	err |= __copy_to_user(&sc->sc_fpregs[0], &current->thread.xstate->hardfpu,
			      (sizeof(long long) * 32) + (sizeof(int) * 1));
	clear_used_math();

	return err;
}
#else
static inline int
restore_sigcontext_fpu(struct pt_regs *regs, struct sigcontext __user *sc)
{
	return 0;
}
static inline int
setup_sigcontext_fpu(struct pt_regs *regs, struct sigcontext __user *sc)
{
	return 0;
}
#endif

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc, long long *r2_p)
{
	unsigned int err = 0;
        unsigned long long current_sr, new_sr;
#define SR_MASK 0xffff8cfd

#define COPY(x)		err |= __get_user(regs->x, &sc->sc_##x)

	COPY(regs[0]);	COPY(regs[1]);	COPY(regs[2]);	COPY(regs[3]);
	COPY(regs[4]);	COPY(regs[5]);	COPY(regs[6]);	COPY(regs[7]);
	COPY(regs[8]);	COPY(regs[9]);  COPY(regs[10]);	COPY(regs[11]);
	COPY(regs[12]);	COPY(regs[13]);	COPY(regs[14]);	COPY(regs[15]);
	COPY(regs[16]);	COPY(regs[17]);	COPY(regs[18]);	COPY(regs[19]);
	COPY(regs[20]);	COPY(regs[21]);	COPY(regs[22]);	COPY(regs[23]);
	COPY(regs[24]);	COPY(regs[25]);	COPY(regs[26]);	COPY(regs[27]);
	COPY(regs[28]);	COPY(regs[29]);	COPY(regs[30]);	COPY(regs[31]);
	COPY(regs[32]);	COPY(regs[33]);	COPY(regs[34]);	COPY(regs[35]);
	COPY(regs[36]);	COPY(regs[37]);	COPY(regs[38]);	COPY(regs[39]);
	COPY(regs[40]);	COPY(regs[41]);	COPY(regs[42]);	COPY(regs[43]);
	COPY(regs[44]);	COPY(regs[45]);	COPY(regs[46]);	COPY(regs[47]);
	COPY(regs[48]);	COPY(regs[49]);	COPY(regs[50]);	COPY(regs[51]);
	COPY(regs[52]);	COPY(regs[53]);	COPY(regs[54]);	COPY(regs[55]);
	COPY(regs[56]);	COPY(regs[57]);	COPY(regs[58]);	COPY(regs[59]);
	COPY(regs[60]);	COPY(regs[61]);	COPY(regs[62]);
	COPY(tregs[0]);	COPY(tregs[1]);	COPY(tregs[2]);	COPY(tregs[3]);
	COPY(tregs[4]);	COPY(tregs[5]);	COPY(tregs[6]);	COPY(tregs[7]);

        /* Prevent the signal handler manipulating SR in a way that can
           crash the kernel. i.e. only allow S, Q, M, PR, SZ, FR to be
           modified */
        current_sr = regs->sr;
        err |= __get_user(new_sr, &sc->sc_sr);
        regs->sr &= SR_MASK;
        regs->sr |= (new_sr & ~SR_MASK);

	COPY(pc);

#undef COPY

	/* Must do this last in case it sets regs->sr.fd (i.e. after rest of sr
	 * has been restored above.) */
	err |= restore_sigcontext_fpu(regs, sc);

	regs->syscall_nr = -1;		/* disable syscall checks */
	err |= __get_user(*r2_p, &sc->sc_regs[REG_RET]);
	return err;
}

asmlinkage int sys_sigreturn(unsigned long r2, unsigned long r3,
				   unsigned long r4, unsigned long r5,
				   unsigned long r6, unsigned long r7,
				   struct pt_regs * regs)
{
	struct sigframe __user *frame = (struct sigframe __user *) (long) REF_REG_SP;
	sigset_t set;
	long long ret;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->sc, &ret))
		goto badframe;
	regs->pc -= 4;

	return (int) ret;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(unsigned long r2, unsigned long r3,
				unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs * regs)
{
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *) (long) REF_REG_SP;
	sigset_t set;
	long long ret;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &ret))
		goto badframe;
	regs->pc -= 4;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return (int) ret;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */
static int
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs,
		 unsigned long mask)
{
	int err = 0;

	/* Do this first, otherwise is this sets sr->fd, that value isn't preserved. */
	err |= setup_sigcontext_fpu(regs, sc);

#define COPY(x)		err |= __put_user(regs->x, &sc->sc_##x)

	COPY(regs[0]);	COPY(regs[1]);	COPY(regs[2]);	COPY(regs[3]);
	COPY(regs[4]);	COPY(regs[5]);	COPY(regs[6]);	COPY(regs[7]);
	COPY(regs[8]);	COPY(regs[9]);	COPY(regs[10]);	COPY(regs[11]);
	COPY(regs[12]);	COPY(regs[13]);	COPY(regs[14]);	COPY(regs[15]);
	COPY(regs[16]);	COPY(regs[17]);	COPY(regs[18]);	COPY(regs[19]);
	COPY(regs[20]);	COPY(regs[21]);	COPY(regs[22]);	COPY(regs[23]);
	COPY(regs[24]);	COPY(regs[25]);	COPY(regs[26]);	COPY(regs[27]);
	COPY(regs[28]);	COPY(regs[29]);	COPY(regs[30]);	COPY(regs[31]);
	COPY(regs[32]);	COPY(regs[33]);	COPY(regs[34]);	COPY(regs[35]);
	COPY(regs[36]);	COPY(regs[37]);	COPY(regs[38]);	COPY(regs[39]);
	COPY(regs[40]);	COPY(regs[41]);	COPY(regs[42]);	COPY(regs[43]);
	COPY(regs[44]);	COPY(regs[45]);	COPY(regs[46]);	COPY(regs[47]);
	COPY(regs[48]);	COPY(regs[49]);	COPY(regs[50]);	COPY(regs[51]);
	COPY(regs[52]);	COPY(regs[53]);	COPY(regs[54]);	COPY(regs[55]);
	COPY(regs[56]);	COPY(regs[57]);	COPY(regs[58]);	COPY(regs[59]);
	COPY(regs[60]);	COPY(regs[61]);	COPY(regs[62]);
	COPY(tregs[0]);	COPY(tregs[1]);	COPY(tregs[2]);	COPY(tregs[3]);
	COPY(tregs[4]);	COPY(tregs[5]);	COPY(tregs[6]);	COPY(tregs[7]);
	COPY(sr);	COPY(pc);

#undef COPY

	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void __user *)((sp - frame_size) & -8ul);
}

void sa_default_restorer(void);		/* See comments below */
void sa_default_rt_restorer(void);	/* See comments below */

static int setup_frame(int sig, struct k_sigaction *ka,
		       sigset_t *set, struct pt_regs *regs)
{
	struct sigframe __user *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->regs[REG_SP], sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	/* Give up earlier as i386, in case */
	if (err)
		goto give_sigsegv;

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask)); }

	/* Give up earlier as i386, in case */
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		/*
		 * On SH5 all edited pointers are subject to NEFF
		 */
		DEREF_REG_PR = neff_sign_extend((unsigned long)
			ka->sa.sa_restorer | 0x1);
	} else {
		/*
		 * Different approach on SH5.
	         * . Endianness independent asm code gets placed in entry.S .
		 *   This is limited to four ASM instructions corresponding
		 *   to two long longs in size.
		 * . err checking is done on the else branch only
		 * . flush_icache_range() is called upon __put_user() only
		 * . all edited pointers are subject to NEFF
		 * . being code, linker turns ShMedia bit on, always
		 *   dereference index -1.
		 */
		DEREF_REG_PR = neff_sign_extend((unsigned long)
			frame->retcode | 0x01);

		if (__copy_to_user(frame->retcode,
			(void *)((unsigned long)sa_default_restorer & (~1)), 16) != 0)
			goto give_sigsegv;

		/* Cohere the trampoline with the I-cache. */
		flush_cache_sigtramp(DEREF_REG_PR-1);
	}

	/*
	 * Set up registers for signal handler.
	 * All edited pointers are subject to NEFF.
	 */
	regs->regs[REG_SP] = neff_sign_extend((unsigned long)frame);
	regs->regs[REG_ARG1] = signal; /* Arg for signal handler */

        /* FIXME:
           The glibc profiling support for SH-5 needs to be passed a sigcontext
           so it can retrieve the PC.  At some point during 2003 the glibc
           support was changed to receive the sigcontext through the 2nd
           argument, but there are still versions of libc.so in use that use
           the 3rd argument.  Until libc.so is stabilised, pass the sigcontext
           through both 2nd and 3rd arguments.
        */

	regs->regs[REG_ARG2] = (unsigned long long)(unsigned long)(signed long)&frame->sc;
	regs->regs[REG_ARG3] = (unsigned long long)(unsigned long)(signed long)&frame->sc;

	regs->pc = neff_sign_extend((unsigned long)ka->sa.sa_handler);

	set_fs(USER_DS);

	/* Broken %016Lx */
	pr_debug("SIG deliver (#%d,%s:%d): sp=%p pc=%08Lx%08Lx link=%08Lx%08Lx\n",
		 signal, current->comm, current->pid, frame,
		 regs->pc >> 32, regs->pc & 0xffffffff,
		 DEREF_REG_PR >> 32, DEREF_REG_PR & 0xffffffff);

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

static int setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			  sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->regs[REG_SP], sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);

	/* Give up earlier as i386, in case */
	if (err)
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, regs->regs[REG_SP]);
	err |= setup_sigcontext(&frame->uc.uc_mcontext,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Give up earlier as i386, in case */
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		/*
		 * On SH5 all edited pointers are subject to NEFF
		 */
		DEREF_REG_PR = neff_sign_extend((unsigned long)
			ka->sa.sa_restorer | 0x1);
	} else {
		/*
		 * Different approach on SH5.
	         * . Endianness independent asm code gets placed in entry.S .
		 *   This is limited to four ASM instructions corresponding
		 *   to two long longs in size.
		 * . err checking is done on the else branch only
		 * . flush_icache_range() is called upon __put_user() only
		 * . all edited pointers are subject to NEFF
		 * . being code, linker turns ShMedia bit on, always
		 *   dereference index -1.
		 */
		DEREF_REG_PR = neff_sign_extend((unsigned long)
			frame->retcode | 0x01);

		if (__copy_to_user(frame->retcode,
			(void *)((unsigned long)sa_default_rt_restorer & (~1)), 16) != 0)
			goto give_sigsegv;

		/* Cohere the trampoline with the I-cache. */
		flush_icache_range(DEREF_REG_PR-1, DEREF_REG_PR-1+15);
	}

	/*
	 * Set up registers for signal handler.
	 * All edited pointers are subject to NEFF.
	 */
	regs->regs[REG_SP] = neff_sign_extend((unsigned long)frame);
	regs->regs[REG_ARG1] = signal; /* Arg for signal handler */
	regs->regs[REG_ARG2] = (unsigned long long)(unsigned long)(signed long)&frame->info;
	regs->regs[REG_ARG3] = (unsigned long long)(unsigned long)(signed long)&frame->uc.uc_mcontext;
	regs->pc = neff_sign_extend((unsigned long)ka->sa.sa_handler);

	set_fs(USER_DS);

	pr_debug("SIG deliver (#%d,%s:%d): sp=%p pc=%08Lx%08Lx link=%08Lx%08Lx\n",
		 signal, current->comm, current->pid, frame,
		 regs->pc >> 32, regs->pc & 0xffffffff,
		 DEREF_REG_PR >> 32, DEREF_REG_PR & 0xffffffff);

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
		struct pt_regs * regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(sig, ka, info, oldset, regs);
	else
		ret = setup_frame(sig, ka, oldset, regs);

	if (ret)
		return;

	signal_delivered(sig, info, ka, regs,
			test_thread_flag(TIF_SINGLESTEP));
}

asmlinkage void do_notify_resume(struct pt_regs *regs, unsigned long thread_info_flags)
{
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}
