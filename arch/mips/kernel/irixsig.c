/*
 * irixsig.c: WHEEE, IRIX signals!  YOW, am I compatible or what?!?!
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997 - 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2000 Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/ptrace.h>

#include <asm/ptrace.h>
#include <asm/uaccess.h>

#undef DEBUG_SIG

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

typedef struct {
	unsigned long sig[4];
} irix_sigset_t;

struct sigctx_irix5 {
	u32 rmask, cp0_status;
	u64 pc;
	u64 regs[32];
	u64 fpregs[32];
	u32 usedfp, fpcsr, fpeir, sstk_flags;
	u64 hi, lo;
	u64 cp0_cause, cp0_badvaddr, _unused0;
	irix_sigset_t sigset;
	u64 weird_fpu_thing;
	u64 _unused1[31];
};

#ifdef DEBUG_SIG
/* Debugging */
static inline void dump_irix5_sigctx(struct sigctx_irix5 *c)
{
	int i;

	printk("misc: rmask[%08lx] status[%08lx] pc[%08lx]\n",
	       (unsigned long) c->rmask,
	       (unsigned long) c->cp0_status,
	       (unsigned long) c->pc);
	printk("regs: ");
	for(i = 0; i < 16; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->regs[i]);
	printk("\nregs: ");
	for(i = 16; i < 32; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->regs[i]);
	printk("\nfpregs: ");
	for(i = 0; i < 16; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->fpregs[i]);
	printk("\nfpregs: ");
	for(i = 16; i < 32; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->fpregs[i]);
	printk("misc: usedfp[%d] fpcsr[%08lx] fpeir[%08lx] stk_flgs[%08lx]\n",
	       (int) c->usedfp, (unsigned long) c->fpcsr,
	       (unsigned long) c->fpeir, (unsigned long) c->sstk_flags);
	printk("misc: hi[%08lx] lo[%08lx] cause[%08lx] badvaddr[%08lx]\n",
	       (unsigned long) c->hi, (unsigned long) c->lo,
	       (unsigned long) c->cp0_cause, (unsigned long) c->cp0_badvaddr);
	printk("misc: sigset<0>[%08lx] sigset<1>[%08lx] sigset<2>[%08lx] "
	       "sigset<3>[%08lx]\n", (unsigned long) c->sigset.sig[0],
	       (unsigned long) c->sigset.sig[1],
	       (unsigned long) c->sigset.sig[2],
	       (unsigned long) c->sigset.sig[3]);
}
#endif

static int setup_irix_frame(struct k_sigaction *ka, struct pt_regs *regs,
			    int signr, sigset_t *oldmask)
{
	struct sigctx_irix5 __user *ctx;
	unsigned long sp;
	int error, i;

	sp = regs->regs[29];
	sp -= sizeof(struct sigctx_irix5);
	sp &= ~(0xf);
	ctx = (struct sigctx_irix5 __user *) sp;
	if (!access_ok(VERIFY_WRITE, ctx, sizeof(*ctx)))
		goto segv_and_exit;

	error = __put_user(0, &ctx->weird_fpu_thing);
	error |= __put_user(~(0x00000001), &ctx->rmask);
	error |= __put_user(0, &ctx->regs[0]);
	for(i = 1; i < 32; i++)
		error |= __put_user((u64) regs->regs[i], &ctx->regs[i]);

	error |= __put_user((u64) regs->hi, &ctx->hi);
	error |= __put_user((u64) regs->lo, &ctx->lo);
	error |= __put_user((u64) regs->cp0_epc, &ctx->pc);
	error |= __put_user(!!used_math(), &ctx->usedfp);
	error |= __put_user((u64) regs->cp0_cause, &ctx->cp0_cause);
	error |= __put_user((u64) regs->cp0_badvaddr, &ctx->cp0_badvaddr);

	error |= __put_user(0, &ctx->sstk_flags); /* XXX sigstack unimp... todo... */

	error |= __copy_to_user(&ctx->sigset, oldmask, sizeof(irix_sigset_t)) ? -EFAULT : 0;

	if (error)
		goto segv_and_exit;

#ifdef DEBUG_SIG
	dump_irix5_sigctx(ctx);
#endif

	regs->regs[4] = (unsigned long) signr;
	regs->regs[5] = 0; /* XXX sigcode XXX */
	regs->regs[6] = regs->regs[29] = sp;
	regs->regs[7] = (unsigned long) ka->sa.sa_handler;
	regs->regs[25] = regs->cp0_epc = (unsigned long) ka->sa_restorer;

	return 1;

segv_and_exit:
	force_sigsegv(signr, current);
	return 0;
}

static int inline
setup_irix_rt_frame(struct k_sigaction * ka, struct pt_regs *regs,
               int signr, sigset_t *oldmask, siginfo_t *info)
{
	printk("Aiee: setup_tr_frame wants to be written");
	do_exit(SIGSEGV);
}

static inline int handle_signal(unsigned long sig, siginfo_t *info,
	struct k_sigaction *ka, sigset_t *oldset, struct pt_regs * regs)
{
	int ret;

	switch(regs->regs[0]) {
	case ERESTARTNOHAND:
		regs->regs[2] = EINTR;
		break;
	case ERESTARTSYS:
		if(!(ka->sa.sa_flags & SA_RESTART)) {
			regs->regs[2] = EINTR;
			break;
		}
	/* fallthrough */
	case ERESTARTNOINTR:		/* Userland will reload $v0.  */
		regs->cp0_epc -= 8;
	}

	regs->regs[0] = 0;		/* Don't deal with this again.  */

	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_irix_rt_frame(ka, regs, sig, oldset, info);
	else
		ret = setup_irix_frame(ka, regs, sig, oldset);

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked,sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	return ret;
}

asmlinkage int do_irix_signal(sigset_t *oldset, struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which is why we may in certain
	 * cases get here from kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (try_to_freeze())
		goto no_signal;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0)
		return handle_signal(signr, &info, &ka, oldset, regs);

no_signal:
	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (regs->regs[0]) {
		if (regs->regs[2] == ERESTARTNOHAND ||
		    regs->regs[2] == ERESTARTSYS ||
		    regs->regs[2] == ERESTARTNOINTR) {
			regs->cp0_epc -= 8;
		}
	}
	return 0;
}

asmlinkage void
irix_sigreturn(struct pt_regs *regs)
{
	struct sigctx_irix5 __user *context, *magic;
	unsigned long umask, mask;
	u64 *fregs;
	u32 usedfp;
	int error, sig, i, base = 0;
	sigset_t blocked;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	if (regs->regs[2] == 1000)
		base = 1;

	context = (struct sigctx_irix5 __user *) regs->regs[base + 4];
	magic = (struct sigctx_irix5 __user *) regs->regs[base + 5];
	sig = (int) regs->regs[base + 6];
#ifdef DEBUG_SIG
	printk("[%s:%d] IRIX sigreturn(scp[%p],ucp[%p],sig[%d])\n",
	       current->comm, current->pid, context, magic, sig);
#endif
	if (!context)
		context = magic;
	if (!access_ok(VERIFY_READ, context, sizeof(struct sigctx_irix5)))
		goto badframe;

#ifdef DEBUG_SIG
	dump_irix5_sigctx(context);
#endif

	error = __get_user(regs->cp0_epc, &context->pc);
	error |= __get_user(umask, &context->rmask);

	mask = 2;
	for (i = 1; i < 32; i++, mask <<= 1) {
		if (umask & mask)
			error |= __get_user(regs->regs[i], &context->regs[i]);
	}
	error |= __get_user(regs->hi, &context->hi);
	error |= __get_user(regs->lo, &context->lo);

	error |= __get_user(usedfp, &context->usedfp);
	if ((umask & 1) && usedfp) {
		fregs = (u64 *) &current->thread.fpu;

		for(i = 0; i < 32; i++)
			error |= __get_user(fregs[i], &context->fpregs[i]);
		error |= __get_user(current->thread.fpu.hard.fcr31, &context->fpcsr);
	}

	/* XXX do sigstack crapola here... XXX */

	error |= __copy_from_user(&blocked, &context->sigset, sizeof(blocked)) ? -EFAULT : 0;

	if (error)
		goto badframe;

	sigdelsetmask(&blocked, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = blocked;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29,%0\n\t"
		"j\tsyscall_exit"
		:/* no outputs */
		:"r" (&regs));
		/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

struct sigact_irix5 {
	int flags;
	void (*handler)(int);
	u32 sigset[4];
	int _unused0[2];
};

#ifdef DEBUG_SIG
static inline void dump_sigact_irix5(struct sigact_irix5 *p)
{
	printk("<f[%d] hndlr[%08lx] msk[%08lx]>", p->flags,
	       (unsigned long) p->handler,
	       (unsigned long) p->sigset[0]);
}
#endif

asmlinkage int
irix_sigaction(int sig, const struct sigaction __user *act,
	      struct sigaction __user *oact, void __user *trampoline)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

#ifdef DEBUG_SIG
	printk(" (%d,%s,%s,%08lx) ", sig, (!new ? "0" : "NEW"),
	       (!old ? "0" : "OLD"), trampoline);
	if(new) {
		dump_sigact_irix5(new); printk(" ");
	}
#endif
	if (act) {
		sigset_t mask;
		int err;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err = __get_user(new_ka.sa.sa_handler, &act->sa_handler);
		err |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);

		err |= __copy_from_user(&mask, &act->sa_mask, sizeof(sigset_t)) ? -EFAULT : 0;
		if (err)
			return err;

		/*
		 * Hmmm... methinks IRIX libc always passes a valid trampoline
		 * value for all invocations of sigaction.  Will have to
		 * investigate.  POSIX POSIX, die die die...
		 */
		new_ka.sa_restorer = trampoline;
	}

/* XXX Implement SIG_SETMASK32 for IRIX compatibility */
	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		int err;

		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
			return -EFAULT;

		err = __put_user(old_ka.sa.sa_handler, &oact->sa_handler);
		err |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		err |= __copy_to_user(&oact->sa_mask, &old_ka.sa.sa_mask,
		               sizeof(sigset_t)) ? -EFAULT : 0;
		if (err)
			return -EFAULT;
	}

	return ret;
}

asmlinkage int irix_sigpending(irix_sigset_t __user *set)
{
	return do_sigpending(set, sizeof(*set));
}

asmlinkage int irix_sigprocmask(int how, irix_sigset_t __user *new,
	irix_sigset_t __user *old)
{
	sigset_t oldbits, newbits;

	if (new) {
		if (!access_ok(VERIFY_READ, new, sizeof(*new)))
			return -EFAULT;
		if (__copy_from_user(&newbits, new, sizeof(unsigned long)*4))
			return -EFAULT;
		sigdelsetmask(&newbits, ~_BLOCKABLE);

		spin_lock_irq(&current->sighand->siglock);
		oldbits = current->blocked;

		switch(how) {
		case 1:
			sigorsets(&newbits, &oldbits, &newbits);
			break;

		case 2:
			sigandsets(&newbits, &oldbits, &newbits);
			break;

		case 3:
			break;

		case 256:
			siginitset(&newbits, newbits.sig[0]);
			break;

		default:
			return -EINVAL;
		}
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
	if (old)
		return copy_to_user(old, &current->blocked,
		                  sizeof(unsigned long)*4) ? -EFAULT : 0;

	return 0;
}

asmlinkage int irix_sigsuspend(struct pt_regs *regs)
{
	sigset_t saveset, newset;
	sigset_t __user *uset;

	uset = (sigset_t __user *) regs->regs[4];
	if (copy_from_user(&newset, uset, sizeof(sigset_t)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->regs[2] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_irix_signal(&saveset, regs))
			return -EINTR;
	}
}

/* hate hate hate... */
struct irix5_siginfo {
	int sig, code, error;
	union {
		char unused[128 - (3 * 4)]; /* Safety net. */
		struct {
			int pid;
			union {
				int uid;
				struct {
					int utime, status, stime;
				} child;
			} procdata;
		} procinfo;

		unsigned long fault_addr;

		struct {
			int fd;
			long band;
		} fileinfo;

		unsigned long sigval;
	} stuff;
};

asmlinkage int irix_sigpoll_sys(unsigned long __user *set,
	struct irix5_siginfo __user *info, struct timespec __user *tp)
{
	long expire = MAX_SCHEDULE_TIMEOUT;
	sigset_t kset;
	int i, sig, error, timeo = 0;
	struct timespec ktp;

#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigpoll_sys(%p,%p,%p)\n",
	       current->comm, current->pid, set, info, tp);
#endif

	/* Must always specify the signal set. */
	if (!set)
		return -EINVAL;

	if (copy_from_user(&kset, set, sizeof(set)))
		return -EFAULT;

	if (info && clear_user(info, sizeof(*info))) {
		error = -EFAULT;
		goto out;
	}

	if (tp) {
		if (copy_from_user(&ktp, tp, sizeof(*tp)))
			return -EFAULT;

		if (!ktp.tv_sec && !ktp.tv_nsec)
			return -EINVAL;

		expire = timespec_to_jiffies(&ktp) +
		         (ktp.tv_sec || ktp.tv_nsec);
	}

	while(1) {
		long tmp = 0;

		expire = schedule_timeout_interruptible(expire);

		for (i=0; i<=4; i++)
			tmp |= (current->pending.signal.sig[i] & kset.sig[i]);

		if (tmp)
			break;
		if (!expire) {
			timeo = 1;
			break;
		}
		if (signal_pending(current))
			return -EINTR;
	}
	if (timeo)
		return -EAGAIN;

	for (sig = 1; i <= 65 /* IRIX_NSIG */; sig++) {
		if (sigismember (&kset, sig))
			continue;
		if (sigismember (&current->pending.signal, sig)) {
			/* XXX need more than this... */
			if (info)
				return copy_to_user(&info->sig, &sig, sizeof(sig));
			return 0;
		}
	}

	/* Should not get here, but do something sane if we do. */
	error = -EINTR;

out:
	return error;
}

/* This is here because of irix5_siginfo definition. */
#define IRIX_P_PID    0
#define IRIX_P_PGID   2
#define IRIX_P_ALL    7

extern int getrusage(struct task_struct *, int, struct rusage __user *);

#define W_EXITED     1
#define W_TRAPPED    2
#define W_STOPPED    4
#define W_CONT       8
#define W_NOHANG    64

#define W_MASK      (W_EXITED | W_TRAPPED | W_STOPPED | W_CONT | W_NOHANG)

asmlinkage int irix_waitsys(int type, int pid,
	struct irix5_siginfo __user *info, int options,
	struct rusage __user *ru)
{
	int flag, retval;
	DECLARE_WAITQUEUE(wait, current);
	struct task_struct *tsk;
	struct task_struct *p;
	struct list_head *_p;

	if (!info)
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, info, sizeof(*info)))
		return -EFAULT;

	if (ru)
		if (!access_ok(VERIFY_WRITE, ru, sizeof(*ru)))
			return -EFAULT;

	if (options & ~W_MASK)
		return -EINVAL;

	if (type != IRIX_P_PID && type != IRIX_P_PGID && type != IRIX_P_ALL)
		return -EINVAL;

	add_wait_queue(&current->signal->wait_chldexit, &wait);
repeat:
	flag = 0;
	current->state = TASK_INTERRUPTIBLE;
	read_lock(&tasklist_lock);
	tsk = current;
	list_for_each(_p,&tsk->children) {
		p = list_entry(_p,struct task_struct,sibling);
		if ((type == IRIX_P_PID) && p->pid != pid)
			continue;
		if ((type == IRIX_P_PGID) && process_group(p) != pid)
			continue;
		if ((p->exit_signal != SIGCHLD))
			continue;
		flag = 1;
		switch (p->state) {
		case TASK_STOPPED:
			if (!p->exit_code)
				continue;
			if (!(options & (W_TRAPPED|W_STOPPED)) &&
			    !(p->ptrace & PT_PTRACED))
				continue;
			read_unlock(&tasklist_lock);

			/* move to end of parent's list to avoid starvation */
			write_lock_irq(&tasklist_lock);
			remove_parent(p);
			add_parent(p, p->parent);
			write_unlock_irq(&tasklist_lock);
			retval = ru ? getrusage(p, RUSAGE_BOTH, ru) : 0;
			if (retval)
				goto end_waitsys;

			retval = __put_user(SIGCHLD, &info->sig);
			retval |= __put_user(0, &info->code);
			retval |= __put_user(p->pid, &info->stuff.procinfo.pid);
			retval |= __put_user((p->exit_code >> 8) & 0xff,
			           &info->stuff.procinfo.procdata.child.status);
			retval |= __put_user(p->utime, &info->stuff.procinfo.procdata.child.utime);
			retval |= __put_user(p->stime, &info->stuff.procinfo.procdata.child.stime);
			if (retval)
				goto end_waitsys;

			p->exit_code = 0;
			goto end_waitsys;

		case EXIT_ZOMBIE:
			current->signal->cutime += p->utime + p->signal->cutime;
			current->signal->cstime += p->stime + p->signal->cstime;
			if (ru != NULL)
				getrusage(p, RUSAGE_BOTH, ru);
			retval = __put_user(SIGCHLD, &info->sig);
			retval |= __put_user(1, &info->code);      /* CLD_EXITED */
			retval |= __put_user(p->pid, &info->stuff.procinfo.pid);
			retval |= __put_user((p->exit_code >> 8) & 0xff,
			           &info->stuff.procinfo.procdata.child.status);
			retval |= __put_user(p->utime,
			           &info->stuff.procinfo.procdata.child.utime);
			retval |= __put_user(p->stime,
			           &info->stuff.procinfo.procdata.child.stime);
			if (retval)
				return retval;

			if (p->real_parent != p->parent) {
				write_lock_irq(&tasklist_lock);
				remove_parent(p);
				p->parent = p->real_parent;
				add_parent(p, p->parent);
				do_notify_parent(p, SIGCHLD);
				write_unlock_irq(&tasklist_lock);
			} else
				release_task(p);
			goto end_waitsys;
		default:
			continue;
		}
		tsk = next_thread(tsk);
	}
	read_unlock(&tasklist_lock);
	if (flag) {
		retval = 0;
		if (options & W_NOHANG)
			goto end_waitsys;
		retval = -ERESTARTSYS;
		if (signal_pending(current))
			goto end_waitsys;
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		goto repeat;
	}
	retval = -ECHILD;
end_waitsys:
	current->state = TASK_RUNNING;
	remove_wait_queue(&current->signal->wait_chldexit, &wait);

	return retval;
}

struct irix5_context {
	u32 flags;
	u32 link;
	u32 sigmask[4];
	struct { u32 sp, size, flags; } stack;
	int regs[36];
	u32 fpregs[32];
	u32 fpcsr;
	u32 _unused0;
	u32 _unused1[47];
	u32 weird_graphics_thing;
};

asmlinkage int irix_getcontext(struct pt_regs *regs)
{
	int error, i, base = 0;
	struct irix5_context __user *ctx;
	unsigned long flags;

	if (regs->regs[2] == 1000)
		base = 1;
	ctx = (struct irix5_context __user *) regs->regs[base + 4];

#ifdef DEBUG_SIG
	printk("[%s:%d] irix_getcontext(%p)\n",
	       current->comm, current->pid, ctx);
#endif

	if (!access_ok(VERIFY_WRITE, ctx, sizeof(*ctx)));
		return -EFAULT;

	error = __put_user(current->thread.irix_oldctx, &ctx->link);

	error |= __copy_to_user(&ctx->sigmask, &current->blocked, sizeof(irix_sigset_t)) ? -EFAULT : 0;

	/* XXX Do sigstack stuff someday... */
	error |= __put_user(0, &ctx->stack.sp);
	error |= __put_user(0, &ctx->stack.size);
	error |= __put_user(0, &ctx->stack.flags);

	error |= __put_user(0, &ctx->weird_graphics_thing);
	error |= __put_user(0, &ctx->regs[0]);
	for (i = 1; i < 32; i++)
		error |= __put_user(regs->regs[i], &ctx->regs[i]);
	error |= __put_user(regs->lo, &ctx->regs[32]);
	error |= __put_user(regs->hi, &ctx->regs[33]);
	error |= __put_user(regs->cp0_cause, &ctx->regs[34]);
	error |= __put_user(regs->cp0_epc, &ctx->regs[35]);

	flags = 0x0f;
	if (!used_math()) {
		flags &= ~(0x08);
	} else {
		/* XXX wheee... */
		printk("Wheee, no code for saving IRIX FPU context yet.\n");
	}
	error |= __put_user(flags, &ctx->flags);

	return error;
}

asmlinkage void irix_setcontext(struct pt_regs *regs)
{
	struct irix5_context __user *ctx;
	int err, base = 0;
	u32 flags;

	if (regs->regs[2] == 1000)
		base = 1;
	ctx = (struct irix5_context __user *) regs->regs[base + 4];

#ifdef DEBUG_SIG
	printk("[%s:%d] irix_setcontext(%p)\n",
	       current->comm, current->pid, ctx);
#endif

	if (!access_ok(VERIFY_READ, ctx, sizeof(*ctx)))
		goto segv_and_exit;

	err = __get_user(flags, &ctx->flags);
	if (flags & 0x02) {
		/* XXX sigstack garbage, todo... */
		printk("Wheee, cannot do sigstack stuff in setcontext\n");
	}

	if (flags & 0x04) {
		int i;

		/* XXX extra control block stuff... todo... */
		for (i = 1; i < 32; i++)
			err |= __get_user(regs->regs[i], &ctx->regs[i]);
		err |= __get_user(regs->lo, &ctx->regs[32]);
		err |= __get_user(regs->hi, &ctx->regs[33]);
		err |= __get_user(regs->cp0_epc, &ctx->regs[35]);
	}

	if (flags & 0x08)
		/* XXX fpu context, blah... */
		printk(KERN_ERR "Wheee, cannot restore FPU context yet...\n");

	err |= __get_user(current->thread.irix_oldctx, &ctx->link);
	if (err)
		goto segv_and_exit;

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29,%0\n\t"
		"j\tsyscall_exit"
		:/* no outputs */
		:"r" (&regs));
		/* Unreached */

segv_and_exit:
	force_sigsegv(SIGSEGV, current);
}

struct irix_sigstack {
	unsigned long sp;
	int status;
};

asmlinkage int irix_sigstack(struct irix_sigstack __user *new,
	struct irix_sigstack __user *old)
{
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigstack(%p,%p)\n",
	       current->comm, current->pid, new, old);
#endif
	if (new) {
		if (!access_ok(VERIFY_READ, new, sizeof(*new)))
			return -EFAULT;
	}

	if (old) {
		if (!access_ok(VERIFY_WRITE, old, sizeof(*old)))
			return -EFAULT;
	}

	return 0;
}

struct irix_sigaltstack { unsigned long sp; int size; int status; };

asmlinkage int irix_sigaltstack(struct irix_sigaltstack __user *new,
				struct irix_sigaltstack __user *old)
{
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigaltstack(%p,%p)\n",
	       current->comm, current->pid, new, old);
#endif
	if (new)
		if (!access_ok(VERIFY_READ, new, sizeof(*new)))
			return -EFAULT;

	if (old) {
		if (!access_ok(VERIFY_WRITE, old, sizeof(*old)))
			return -EFAULT;
	}

	return 0;
}

struct irix_procset {
	int cmd, ltype, lid, rtype, rid;
};

asmlinkage int irix_sigsendset(struct irix_procset __user *pset, int sig)
{
	if (!access_ok(VERIFY_READ, pset, sizeof(*pset)))
		return -EFAULT;
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigsendset([%d,%d,%d,%d,%d],%d)\n",
	       current->comm, current->pid,
	       pset->cmd, pset->ltype, pset->lid, pset->rtype, pset->rid,
	       sig);
#endif
	return -EINVAL;
}
