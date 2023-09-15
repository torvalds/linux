// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-12-*   x86-64 compatibility mode signal handling by Andi Kleen
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/compat.h>
#include <linux/binfmts.h>
#include <linux/syscalls.h>
#include <asm/ucontext.h>
#include <linux/uaccess.h>
#include <asm/fpu/signal.h>
#include <asm/ptrace.h>
#include <asm/user32.h>
#include <uapi/asm/sigcontext.h>
#include <asm/proto.h>
#include <asm/vdso.h>
#include <asm/sigframe.h>
#include <asm/sighandling.h>
#include <asm/smap.h>
#include <asm/gsseg.h>

#ifdef CONFIG_IA32_EMULATION
#include <asm/ia32_unistd.h>

static inline void reload_segments(struct sigcontext_32 *sc)
{
	unsigned int cur;

	savesegment(gs, cur);
	if ((sc->gs | 0x03) != cur)
		load_gs_index(sc->gs | 0x03);
	savesegment(fs, cur);
	if ((sc->fs | 0x03) != cur)
		loadsegment(fs, sc->fs | 0x03);
	savesegment(ds, cur);
	if ((sc->ds | 0x03) != cur)
		loadsegment(ds, sc->ds | 0x03);
	savesegment(es, cur);
	if ((sc->es | 0x03) != cur)
		loadsegment(es, sc->es | 0x03);
}

#define sigset32_t			compat_sigset_t
#define siginfo32_t			compat_siginfo_t
#define restore_altstack32		compat_restore_altstack
#define unsafe_save_altstack32		unsafe_compat_save_altstack

#else

#define sigset32_t			sigset_t
#define siginfo32_t			siginfo_t
#define __NR_ia32_sigreturn		__NR_sigreturn
#define __NR_ia32_rt_sigreturn		__NR_rt_sigreturn
#define restore_altstack32		restore_altstack
#define unsafe_save_altstack32		unsafe_save_altstack
#define __copy_siginfo_to_user32	copy_siginfo_to_user

#endif

/*
 * Do a signal return; undo the signal stack.
 */
static bool ia32_restore_sigcontext(struct pt_regs *regs,
				    struct sigcontext_32 __user *usc)
{
	struct sigcontext_32 sc;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (unlikely(copy_from_user(&sc, usc, sizeof(sc))))
		return false;

	/* Get only the ia32 registers. */
	regs->bx = sc.bx;
	regs->cx = sc.cx;
	regs->dx = sc.dx;
	regs->si = sc.si;
	regs->di = sc.di;
	regs->bp = sc.bp;
	regs->ax = sc.ax;
	regs->sp = sc.sp;
	regs->ip = sc.ip;

	/* Get CS/SS and force CPL3 */
	regs->cs = sc.cs | 0x03;
	regs->ss = sc.ss | 0x03;

	regs->flags = (regs->flags & ~FIX_EFLAGS) | (sc.flags & FIX_EFLAGS);
	/* disable syscall checks */
	regs->orig_ax = -1;

#ifdef CONFIG_IA32_EMULATION
	/*
	 * Reload fs and gs if they have changed in the signal
	 * handler.  This does not handle long fs/gs base changes in
	 * the handler, but does not clobber them at least in the
	 * normal case.
	 */
	reload_segments(&sc);
#else
	loadsegment(gs, sc.gs);
	regs->fs = sc.fs;
	regs->es = sc.es;
	regs->ds = sc.ds;
#endif

	return fpu__restore_sig(compat_ptr(sc.fpstate), 1);
}

SYSCALL32_DEFINE0(sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe_ia32 __user *frame = (struct sigframe_ia32 __user *)(regs->sp-8);
	sigset_t set;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || __get_user(((__u32 *)&set)[1], &frame->extramask[0]))
		goto badframe;

	set_current_blocked(&set);

	if (!ia32_restore_sigcontext(regs, &frame->sc))
		goto badframe;
	return regs->ax;

badframe:
	signal_fault(regs, frame, "32bit sigreturn");
	return 0;
}

SYSCALL32_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe_ia32 __user *frame;
	sigset_t set;

	frame = (struct rt_sigframe_ia32 __user *)(regs->sp - 4);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(*(__u64 *)&set, (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;

	set_current_blocked(&set);

	if (!ia32_restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack32(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "32bit rt sigreturn");
	return 0;
}

/*
 * Set up a signal frame.
 */

#define get_user_seg(seg)	({ unsigned int v; savesegment(seg, v); v; })

static __always_inline int
__unsafe_setup_sigcontext32(struct sigcontext_32 __user *sc,
			    void __user *fpstate,
			    struct pt_regs *regs, unsigned int mask)
{
	unsafe_put_user(get_user_seg(gs), (unsigned int __user *)&sc->gs, Efault);
#ifdef CONFIG_IA32_EMULATION
	unsafe_put_user(get_user_seg(fs), (unsigned int __user *)&sc->fs, Efault);
	unsafe_put_user(get_user_seg(ds), (unsigned int __user *)&sc->ds, Efault);
	unsafe_put_user(get_user_seg(es), (unsigned int __user *)&sc->es, Efault);
#else
	unsafe_put_user(regs->fs, (unsigned int __user *)&sc->fs, Efault);
	unsafe_put_user(regs->es, (unsigned int __user *)&sc->es, Efault);
	unsafe_put_user(regs->ds, (unsigned int __user *)&sc->ds, Efault);
#endif

	unsafe_put_user(regs->di, &sc->di, Efault);
	unsafe_put_user(regs->si, &sc->si, Efault);
	unsafe_put_user(regs->bp, &sc->bp, Efault);
	unsafe_put_user(regs->sp, &sc->sp, Efault);
	unsafe_put_user(regs->bx, &sc->bx, Efault);
	unsafe_put_user(regs->dx, &sc->dx, Efault);
	unsafe_put_user(regs->cx, &sc->cx, Efault);
	unsafe_put_user(regs->ax, &sc->ax, Efault);
	unsafe_put_user(current->thread.trap_nr, &sc->trapno, Efault);
	unsafe_put_user(current->thread.error_code, &sc->err, Efault);
	unsafe_put_user(regs->ip, &sc->ip, Efault);
	unsafe_put_user(regs->cs, (unsigned int __user *)&sc->cs, Efault);
	unsafe_put_user(regs->flags, &sc->flags, Efault);
	unsafe_put_user(regs->sp, &sc->sp_at_signal, Efault);
	unsafe_put_user(regs->ss, (unsigned int __user *)&sc->ss, Efault);

	unsafe_put_user(ptr_to_compat(fpstate), &sc->fpstate, Efault);

	/* non-iBCS2 extensions.. */
	unsafe_put_user(mask, &sc->oldmask, Efault);
	unsafe_put_user(current->thread.cr2, &sc->cr2, Efault);
	return 0;

Efault:
	return -EFAULT;
}

#define unsafe_put_sigcontext32(sc, fp, regs, set, label)		\
do {									\
	if (__unsafe_setup_sigcontext32(sc, fp, regs, set->sig[0]))	\
		goto label;						\
} while(0)

int ia32_setup_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset32_t *set = (sigset32_t *) sigmask_to_save();
	struct sigframe_ia32 __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	/* copy_to_user optimizes that into a single 8 byte store */
	static const struct {
		u16 poplmovl;
		u32 val;
		u16 int80;
	} __attribute__((packed)) code = {
		0xb858,		 /* popl %eax ; movl $...,%eax */
		__NR_ia32_sigreturn,
		0x80cd,		/* int $0x80 */
	};

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fp);

	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		restorer = ksig->ka.sa.sa_restorer;
	} else {
		/* Return stub is in 32bit vsyscall page */
		if (current->mm->context.vdso)
			restorer = current->mm->context.vdso +
				vdso_image_32.sym___kernel_sigreturn;
		else
			restorer = &frame->retcode;
	}

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(ksig->sig, &frame->sig, Efault);
	unsafe_put_sigcontext32(&frame->sc, fp, regs, set, Efault);
	unsafe_put_user(set->sig[1], &frame->extramask[0], Efault);
	unsafe_put_user(ptr_to_compat(restorer), &frame->pretcode, Efault);
	/*
	 * These are actually not used anymore, but left because some
	 * gdb versions depend on them as a marker.
	 */
	unsafe_put_user(*((u64 *)&code), (u64 __user *)frame->retcode, Efault);
	user_access_end();

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	/* Make -mregparm=3 work */
	regs->ax = ksig->sig;
	regs->dx = 0;
	regs->cx = 0;

#ifdef CONFIG_IA32_EMULATION
	loadsegment(ds, __USER_DS);
	loadsegment(es, __USER_DS);
#else
	regs->ds = __USER_DS;
	regs->es = __USER_DS;
#endif

	regs->cs = __USER32_CS;
	regs->ss = __USER_DS;

	return 0;
Efault:
	user_access_end();
	return -EFAULT;
}

int ia32_setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset32_t *set = (sigset32_t *) sigmask_to_save();
	struct rt_sigframe_ia32 __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	/* unsafe_put_user optimizes that into a single 8 byte store */
	static const struct {
		u8 movl;
		u32 val;
		u16 int80;
		u8  pad;
	} __attribute__((packed)) code = {
		0xb8,
		__NR_ia32_rt_sigreturn,
		0x80cd,
		0,
	};

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fp);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(ksig->sig, &frame->sig, Efault);
	unsafe_put_user(ptr_to_compat(&frame->info), &frame->pinfo, Efault);
	unsafe_put_user(ptr_to_compat(&frame->uc), &frame->puc, Efault);

	/* Create the ucontext.  */
	if (static_cpu_has(X86_FEATURE_XSAVE))
		unsafe_put_user(UC_FP_XSTATE, &frame->uc.uc_flags, Efault);
	else
		unsafe_put_user(0, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_save_altstack32(&frame->uc.uc_stack, regs->sp, Efault);

	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;
	else
		restorer = current->mm->context.vdso +
			vdso_image_32.sym___kernel_rt_sigreturn;
	unsafe_put_user(ptr_to_compat(restorer), &frame->pretcode, Efault);

	/*
	 * Not actually used anymore, but left because some gdb
	 * versions need it.
	 */
	unsafe_put_user(*((u64 *)&code), (u64 __user *)frame->retcode, Efault);
	unsafe_put_sigcontext32(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_user(*(__u64 *)set, (__u64 __user *)&frame->uc.uc_sigmask, Efault);
	user_access_end();

	if (__copy_siginfo_to_user32(&frame->info, &ksig->info))
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	/* Make -mregparm=3 work */
	regs->ax = ksig->sig;
	regs->dx = (unsigned long) &frame->info;
	regs->cx = (unsigned long) &frame->uc;

#ifdef CONFIG_IA32_EMULATION
	loadsegment(ds, __USER_DS);
	loadsegment(es, __USER_DS);
#else
	regs->ds = __USER_DS;
	regs->es = __USER_DS;
#endif

	regs->cs = __USER32_CS;
	regs->ss = __USER_DS;

	return 0;
Efault:
	user_access_end();
	return -EFAULT;
}

/*
 * The siginfo_t structure and handing code is very easy
 * to break in several ways.  It must always be updated when new
 * updates are made to the main siginfo_t, and
 * copy_siginfo_to_user32() must be updated when the
 * (arch-independent) copy_siginfo_to_user() is updated.
 *
 * It is also easy to put a new member in the siginfo_t
 * which has implicit alignment which can move internal structure
 * alignment around breaking the ABI.  This can happen if you,
 * for instance, put a plain 64-bit value in there.
 */

/*
* If adding a new si_code, there is probably new data in
* the siginfo.  Make sure folks bumping the si_code
* limits also have to look at this code.  Make sure any
* new fields are handled in copy_siginfo_to_user32()!
*/
static_assert(NSIGILL  == 11);
static_assert(NSIGFPE  == 15);
static_assert(NSIGSEGV == 10);
static_assert(NSIGBUS  == 5);
static_assert(NSIGTRAP == 6);
static_assert(NSIGCHLD == 6);
static_assert(NSIGSYS  == 2);

/* This is part of the ABI and can never change in size: */
static_assert(sizeof(siginfo32_t) == 128);

/* This is a part of the ABI and can never change in alignment */
static_assert(__alignof__(siginfo32_t) == 4);

/*
* The offsets of all the (unioned) si_fields are fixed
* in the ABI, of course.  Make sure none of them ever
* move and are always at the beginning:
*/
static_assert(offsetof(siginfo32_t, _sifields) == 3 * sizeof(int));

static_assert(offsetof(siginfo32_t, si_signo) == 0);
static_assert(offsetof(siginfo32_t, si_errno) == 4);
static_assert(offsetof(siginfo32_t, si_code)  == 8);

/*
* Ensure that the size of each si_field never changes.
* If it does, it is a sign that the
* copy_siginfo_to_user32() code below needs to updated
* along with the size in the CHECK_SI_SIZE().
*
* We repeat this check for both the generic and compat
* siginfos.
*
* Note: it is OK for these to grow as long as the whole
* structure stays within the padding size (checked
* above).
*/

#define CHECK_SI_OFFSET(name)						\
	static_assert(offsetof(siginfo32_t, _sifields) ==		\
		      offsetof(siginfo32_t, _sifields.name))

#define CHECK_SI_SIZE(name, size)					\
	static_assert(sizeof_field(siginfo32_t, _sifields.name) == size)

CHECK_SI_OFFSET(_kill);
CHECK_SI_SIZE  (_kill, 2*sizeof(int));
static_assert(offsetof(siginfo32_t, si_pid) == 0xC);
static_assert(offsetof(siginfo32_t, si_uid) == 0x10);

CHECK_SI_OFFSET(_timer);
#ifdef CONFIG_COMPAT
/* compat_siginfo_t doesn't have si_sys_private */
CHECK_SI_SIZE  (_timer, 3*sizeof(int));
#else
CHECK_SI_SIZE  (_timer, 4*sizeof(int));
#endif
static_assert(offsetof(siginfo32_t, si_tid)     == 0x0C);
static_assert(offsetof(siginfo32_t, si_overrun) == 0x10);
static_assert(offsetof(siginfo32_t, si_value)   == 0x14);

CHECK_SI_OFFSET(_rt);
CHECK_SI_SIZE  (_rt, 3*sizeof(int));
static_assert(offsetof(siginfo32_t, si_pid)   == 0x0C);
static_assert(offsetof(siginfo32_t, si_uid)   == 0x10);
static_assert(offsetof(siginfo32_t, si_value) == 0x14);

CHECK_SI_OFFSET(_sigchld);
CHECK_SI_SIZE  (_sigchld, 5*sizeof(int));
static_assert(offsetof(siginfo32_t, si_pid)    == 0x0C);
static_assert(offsetof(siginfo32_t, si_uid)    == 0x10);
static_assert(offsetof(siginfo32_t, si_status) == 0x14);
static_assert(offsetof(siginfo32_t, si_utime)  == 0x18);
static_assert(offsetof(siginfo32_t, si_stime)  == 0x1C);

CHECK_SI_OFFSET(_sigfault);
CHECK_SI_SIZE  (_sigfault, 4*sizeof(int));
static_assert(offsetof(siginfo32_t, si_addr) == 0x0C);

static_assert(offsetof(siginfo32_t, si_trapno) == 0x10);

static_assert(offsetof(siginfo32_t, si_addr_lsb) == 0x10);

static_assert(offsetof(siginfo32_t, si_lower) == 0x14);
static_assert(offsetof(siginfo32_t, si_upper) == 0x18);

static_assert(offsetof(siginfo32_t, si_pkey) == 0x14);

static_assert(offsetof(siginfo32_t, si_perf_data) == 0x10);
static_assert(offsetof(siginfo32_t, si_perf_type) == 0x14);
static_assert(offsetof(siginfo32_t, si_perf_flags) == 0x18);

CHECK_SI_OFFSET(_sigpoll);
CHECK_SI_SIZE  (_sigpoll, 2*sizeof(int));
static_assert(offsetof(siginfo32_t, si_band) == 0x0C);
static_assert(offsetof(siginfo32_t, si_fd)   == 0x10);

CHECK_SI_OFFSET(_sigsys);
CHECK_SI_SIZE  (_sigsys, 3*sizeof(int));
static_assert(offsetof(siginfo32_t, si_call_addr) == 0x0C);
static_assert(offsetof(siginfo32_t, si_syscall)   == 0x10);
static_assert(offsetof(siginfo32_t, si_arch)      == 0x14);

/* any new si_fields should be added here */
