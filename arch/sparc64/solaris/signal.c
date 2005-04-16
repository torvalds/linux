/* $Id: signal.c,v 1.7 2000/09/05 21:44:54 davem Exp $
 * signal.c: Signal emulation for Solaris
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/types.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>

#include <asm/uaccess.h>
#include <asm/svr4.h>
#include <asm/string.h>

#include "conv.h"
#include "signal.h"

#define _S(nr) (1L<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

long linux_to_solaris_signals[] = {
        0,
	SOLARIS_SIGHUP,		SOLARIS_SIGINT,	
	SOLARIS_SIGQUIT,	SOLARIS_SIGILL,
	SOLARIS_SIGTRAP,	SOLARIS_SIGIOT,
	SOLARIS_SIGEMT,		SOLARIS_SIGFPE,
	SOLARIS_SIGKILL,	SOLARIS_SIGBUS,
	SOLARIS_SIGSEGV,	SOLARIS_SIGSYS,
	SOLARIS_SIGPIPE,	SOLARIS_SIGALRM,
	SOLARIS_SIGTERM,	SOLARIS_SIGURG,
	SOLARIS_SIGSTOP,	SOLARIS_SIGTSTP,
	SOLARIS_SIGCONT,	SOLARIS_SIGCLD,
	SOLARIS_SIGTTIN,	SOLARIS_SIGTTOU,
	SOLARIS_SIGPOLL,	SOLARIS_SIGXCPU,
	SOLARIS_SIGXFSZ,	SOLARIS_SIGVTALRM,
	SOLARIS_SIGPROF,	SOLARIS_SIGWINCH,
	SOLARIS_SIGUSR1,	SOLARIS_SIGUSR1,
	SOLARIS_SIGUSR2,	-1,
};

long solaris_to_linux_signals[] = {
        0,
        SIGHUP,		SIGINT,		SIGQUIT,	SIGILL,
        SIGTRAP,	SIGIOT,		SIGEMT,		SIGFPE,
        SIGKILL,	SIGBUS,		SIGSEGV,	SIGSYS,
        SIGPIPE,	SIGALRM,	SIGTERM,	SIGUSR1,
        SIGUSR2,	SIGCHLD,	-1,		SIGWINCH,
        SIGURG,		SIGPOLL,	SIGSTOP,	SIGTSTP,
        SIGCONT,	SIGTTIN,	SIGTTOU,	SIGVTALRM,
        SIGPROF,	SIGXCPU,	SIGXFSZ,        -1,
	-1,		-1,		-1,		-1,
	-1,		-1,		-1,		-1,
	-1,		-1,		-1,		-1,
};

static inline long mapsig(long sig)
{
	if ((unsigned long)sig > SOLARIS_NSIGNALS)
		return -EINVAL;
	return solaris_to_linux_signals[sig];
}

asmlinkage int solaris_kill(int pid, int sig)
{
	int (*sys_kill)(int,int) = 
		(int (*)(int,int))SYS(kill);
	int s = mapsig(sig);
	
	if (s < 0) return s;
	return sys_kill(pid, s);
}

static long sig_handler(int sig, u32 arg, int one_shot)
{
	struct sigaction sa, old;
	int ret;
	mm_segment_t old_fs = get_fs();
	int (*sys_sigaction)(int,struct sigaction __user *,struct sigaction __user *) = 
		(int (*)(int,struct sigaction __user *,struct sigaction __user *))SYS(sigaction);
	
	sigemptyset(&sa.sa_mask);
	sa.sa_restorer = NULL;
	sa.sa_handler = (__sighandler_t)A(arg);
	sa.sa_flags = 0;
	if (one_shot) sa.sa_flags = SA_ONESHOT | SA_NOMASK;
	set_fs (KERNEL_DS);
	ret = sys_sigaction(sig, (void __user *)&sa, (void __user *)&old);
	set_fs (old_fs);
	if (ret < 0) return ret;
	return (u32)(unsigned long)old.sa_handler;
}

static inline long solaris_signal(int sig, u32 arg)
{
	return sig_handler (sig, arg, 1);
}

static long solaris_sigset(int sig, u32 arg)
{
	if (arg != 2) /* HOLD */ {
		spin_lock_irq(&current->sighand->siglock);
		sigdelsetmask(&current->blocked, _S(sig));
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
		return sig_handler (sig, arg, 0);
	} else {
		spin_lock_irq(&current->sighand->siglock);
		sigaddsetmask(&current->blocked, (_S(sig) & ~_BLOCKABLE));
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
		return 0;
	}
}

static inline long solaris_sighold(int sig)
{
	return solaris_sigset(sig, 2);
}

static inline long solaris_sigrelse(int sig)
{
	spin_lock_irq(&current->sighand->siglock);
	sigdelsetmask(&current->blocked, _S(sig));
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	return 0;
}

static inline long solaris_sigignore(int sig)
{
	return sig_handler(sig, (u32)(unsigned long)SIG_IGN, 0);
}

static inline long solaris_sigpause(int sig)
{
	printk ("Need to support solaris sigpause\n");
	return -ENOSYS;
}

asmlinkage long solaris_sigfunc(int sig, u32 arg)
{
	int func = sig & ~0xff;
	
	sig = mapsig(sig & 0xff); 
	if (sig < 0) return sig; 
	switch (func) {
	case 0: return solaris_signal(sig, arg); 
	case 0x100: return solaris_sigset(sig, arg); 
	case 0x200: return solaris_sighold(sig);
	case 0x400: return solaris_sigrelse(sig); 
	case 0x800: return solaris_sigignore(sig); 
	case 0x1000: return solaris_sigpause(sig);
	}
	return -EINVAL;
}

typedef struct {
	u32 __sigbits[4];
} sol_sigset_t;

static inline int mapin(u32 *p, sigset_t *q)
{
	int i;
	u32 x;
	int sig;
	
	sigemptyset(q);
	x = p[0];
	for (i = 1; i <= SOLARIS_NSIGNALS; i++) {
		if (x & 1) {
			sig = solaris_to_linux_signals[i];
			if (sig == -1)
				return -EINVAL;
			sigaddsetmask(q, (1L << (sig - 1)));
		}
		x >>= 1;
		if (i == 32)
			x = p[1];
	}
	return 0;
}

static inline int mapout(sigset_t *q, u32 *p)
{
	int i;
	int sig;
	
	p[0] = 0;
	p[1] = 0;
	for (i = 1; i <= 32; i++) {
		if (sigismember(q, sigmask(i))) {
			sig = linux_to_solaris_signals[i];
			if (sig == -1)
				return -EINVAL;
			if (sig > 32)
				p[1] |= 1L << (sig - 33);
			else
				p[0] |= 1L << (sig - 1);
		}
	}
	return 0;
}

asmlinkage int solaris_sigprocmask(int how, u32 in, u32 out)
{
	sigset_t in_s, *ins, out_s, *outs;
	mm_segment_t old_fs = get_fs();
	int ret;
	int (*sys_sigprocmask)(int,sigset_t __user *,sigset_t __user *) = 
		(int (*)(int,sigset_t __user *,sigset_t __user *))SYS(sigprocmask);
	
	ins = NULL; outs = NULL;
	if (in) {
		u32 tmp[2];
		
		if (copy_from_user (tmp, (void __user *)A(in), 2*sizeof(u32)))
			return -EFAULT;
		ins = &in_s;
		if (mapin (tmp, ins)) return -EINVAL;
	}
	if (out) outs = &out_s;
	set_fs (KERNEL_DS);
	ret = sys_sigprocmask((how == 3) ? SIG_SETMASK : how,
				(void __user *)ins, (void __user *)outs);
	set_fs (old_fs);
	if (ret) return ret;
	if (out) {
		u32 tmp[4];
		
		tmp[2] = 0; tmp[3] = 0;
		if (mapout (outs, tmp)) return -EINVAL;
		if (copy_to_user((void __user *)A(out), tmp, 4*sizeof(u32)))
			return -EFAULT;
	}
	return 0;
}

asmlinkage long do_sol_sigsuspend(u32 mask)
{
	sigset_t s;
	u32 tmp[2];
		
	if (copy_from_user (tmp, (sol_sigset_t __user *)A(mask), 2*sizeof(u32)))
		return -EFAULT;
	if (mapin (tmp, &s)) return -EINVAL;
	return (long)s.sig[0];
}

struct sol_sigaction {
	int	sa_flags;
	u32	sa_handler;
	u32	sa_mask[4];
	int	sa_resv[2];
};

asmlinkage int solaris_sigaction(int sig, u32 act, u32 old)
{
	u32 tmp, tmp2[4];
	struct sigaction s, s2;
	int ret;
	mm_segment_t old_fs = get_fs();
	struct sol_sigaction __user *p = (void __user *)A(old);
	int (*sys_sigaction)(int,struct sigaction __user *,struct sigaction __user *) = 
		(int (*)(int,struct sigaction __user *,struct sigaction __user *))SYS(sigaction);
	
	sig = mapsig(sig); 
	if (sig < 0) {
		/* We cheat a little bit for Solaris only signals */
		if (old && clear_user(p, sizeof(struct sol_sigaction)))
			return -EFAULT;
		return 0;
	}
	if (act) {
		if (get_user (tmp, &p->sa_flags))
			return -EFAULT;
		s.sa_flags = 0;
		if (tmp & SOLARIS_SA_ONSTACK) s.sa_flags |= SA_STACK;
		if (tmp & SOLARIS_SA_RESTART) s.sa_flags |= SA_RESTART;
		if (tmp & SOLARIS_SA_NODEFER) s.sa_flags |= SA_NOMASK;
		if (tmp & SOLARIS_SA_RESETHAND) s.sa_flags |= SA_ONESHOT;
		if (tmp & SOLARIS_SA_NOCLDSTOP) s.sa_flags |= SA_NOCLDSTOP;
		if (get_user (tmp, &p->sa_handler) ||
		    copy_from_user (tmp2, &p->sa_mask, 2*sizeof(u32)))
			return -EFAULT;
		s.sa_handler = (__sighandler_t)A(tmp);
		if (mapin (tmp2, &s.sa_mask)) return -EINVAL;
		s.sa_restorer = NULL;
	}
	set_fs(KERNEL_DS);
	ret = sys_sigaction(sig, act ? (void __user *)&s : NULL,
				 old ? (void __user *)&s2 : NULL);
	set_fs(old_fs);
	if (ret) return ret;
	if (old) {
		if (mapout (&s2.sa_mask, tmp2)) return -EINVAL;
		tmp = 0; tmp2[2] = 0; tmp2[3] = 0;
		if (s2.sa_flags & SA_STACK) tmp |= SOLARIS_SA_ONSTACK;
		if (s2.sa_flags & SA_RESTART) tmp |= SOLARIS_SA_RESTART;
		if (s2.sa_flags & SA_NOMASK) tmp |= SOLARIS_SA_NODEFER;
		if (s2.sa_flags & SA_ONESHOT) tmp |= SOLARIS_SA_RESETHAND;
		if (s2.sa_flags & SA_NOCLDSTOP) tmp |= SOLARIS_SA_NOCLDSTOP;
		if (put_user (tmp, &p->sa_flags) ||
		    __put_user ((u32)(unsigned long)s2.sa_handler, &p->sa_handler) ||
		    copy_to_user (&p->sa_mask, tmp2, 4*sizeof(u32)))
			return -EFAULT;
	}
	return 0;
}

asmlinkage int solaris_sigpending(int which, u32 set)
{
	sigset_t s;
	u32 tmp[4];
	switch (which) {
	case 1: /* sigpending */
		spin_lock_irq(&current->sighand->siglock);
		sigandsets(&s, &current->blocked, &current->pending.signal);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
		break;
	case 2: /* sigfillset - I just set signals which have linux equivalents */
		sigfillset(&s);
		break;
	default: return -EINVAL;
	}
	if (mapout (&s, tmp)) return -EINVAL;
	tmp[2] = 0; tmp[3] = 0;
	if (copy_to_user ((u32 __user *)A(set), tmp, sizeof(tmp)))
		return -EFAULT;
	return 0;
}

asmlinkage int solaris_wait(u32 stat_loc)
{
	unsigned __user *p = (unsigned __user *)A(stat_loc);
	int (*sys_wait4)(pid_t,unsigned __user *, int, struct rusage __user *) =
		(int (*)(pid_t,unsigned __user *, int, struct rusage __user *))SYS(wait4);
	int ret, status;
	
	ret = sys_wait4(-1, p, WUNTRACED, NULL);
	if (ret >= 0 && stat_loc) {
		if (get_user (status, p))
			return -EFAULT;
		if (((status - 1) & 0xffff) < 0xff)
			status = linux_to_solaris_signals[status & 0x7f] & 0x7f;
		else if ((status & 0xff) == 0x7f)
			status = (linux_to_solaris_signals[(status >> 8) & 0xff] << 8) | 0x7f;
		if (__put_user (status, p))
			return -EFAULT;
	}
	return ret;
}

asmlinkage int solaris_waitid(int idtype, s32 pid, u32 info, int options)
{
	int (*sys_wait4)(pid_t,unsigned __user *, int, struct rusage __user *) =
		(int (*)(pid_t,unsigned __user *, int, struct rusage __user *))SYS(wait4);
	int opts, status, ret;
	
	switch (idtype) {
	case 0: /* P_PID */ break;
	case 1: /* P_PGID */ pid = -pid; break;
	case 7: /* P_ALL */ pid = -1; break;
	default: return -EINVAL;
	}
	opts = 0;
	if (options & SOLARIS_WUNTRACED) opts |= WUNTRACED;
	if (options & SOLARIS_WNOHANG) opts |= WNOHANG;
	current->state = TASK_RUNNING;
	ret = sys_wait4(pid, (unsigned int __user *)A(info), opts, NULL);
	if (ret < 0) return ret;
	if (info) {
		struct sol_siginfo __user *s = (void __user *)A(info);
	
		if (get_user (status, (unsigned int __user *)A(info)))
			return -EFAULT;

		if (__put_user (SOLARIS_SIGCLD, &s->si_signo) ||
		    __put_user (ret, &s->_data._proc._pid))
			return -EFAULT;

		switch (status & 0xff) {
		case 0: ret = SOLARIS_CLD_EXITED;
			status = (status >> 8) & 0xff;
			break;
		case 0x7f:
			status = (status >> 8) & 0xff;
			switch (status) {
			case SIGSTOP:
			case SIGTSTP: ret = SOLARIS_CLD_STOPPED;
			default: ret = SOLARIS_CLD_EXITED;
			}
			status = linux_to_solaris_signals[status];
			break;
		default:
			if (status & 0x80) ret = SOLARIS_CLD_DUMPED;
			else ret = SOLARIS_CLD_KILLED;
			status = linux_to_solaris_signals[status & 0x7f];
			break;
		}

		if (__put_user (ret, &s->si_code) ||
		    __put_user (status, &s->_data._proc._pdata._cld._status))
			return -EFAULT;
	}
	return 0;
}

extern int svr4_setcontext(svr4_ucontext_t *c, struct pt_regs *regs);
extern int svr4_getcontext(svr4_ucontext_t *c, struct pt_regs *regs);

asmlinkage int solaris_context(struct pt_regs *regs)
{
	switch ((unsigned)regs->u_regs[UREG_I0]) {
	case 0: /* getcontext */
		return svr4_getcontext((svr4_ucontext_t *)(long)(u32)regs->u_regs[UREG_I1], regs);
	case 1: /* setcontext */
		return svr4_setcontext((svr4_ucontext_t *)(long)(u32)regs->u_regs[UREG_I1], regs);
	default:
		return -EINVAL;

	}
}

asmlinkage int solaris_sigaltstack(u32 ss, u32 oss)
{
/* XXX Implement this soon */
	return 0;
}
