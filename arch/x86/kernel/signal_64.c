// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen SuSE Labs
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>

#include <asm/ucontext.h>
#include <asm/fpu/signal.h>
#include <asm/sighandling.h>

#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>

/*
 * If regs->ss will cause an IRET fault, change it.  Otherwise leave it
 * alone.  Using this generally makes no sense unless
 * user_64bit_mode(regs) would return true.
 */
static void force_valid_ss(struct pt_regs *regs)
{
	u32 ar;
	asm volatile ("lar %[old_ss], %[ar]\n\t"
		      "jz 1f\n\t"		/* If invalid: */
		      "xorl %[ar], %[ar]\n\t"	/* set ar = 0 */
		      "1:"
		      : [ar] "=r" (ar)
		      : [old_ss] "rm" ((u16)regs->ss));

	/*
	 * For a valid 64-bit user context, we need DPL 3, type
	 * read-write data or read-write exp-down data, and S and P
	 * set.  We can't use VERW because VERW doesn't check the
	 * P bit.
	 */
	ar &= AR_DPL_MASK | AR_S | AR_P | AR_TYPE_MASK;
	if (ar != (AR_DPL3 | AR_S | AR_P | AR_TYPE_RWDATA) &&
	    ar != (AR_DPL3 | AR_S | AR_P | AR_TYPE_RWDATA_EXPDOWN))
		regs->ss = __USER_DS;
}

static bool restore_sigcontext(struct pt_regs *regs,
			       struct sigcontext __user *usc,
			       unsigned long uc_flags)
{
	struct sigcontext sc;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (copy_from_user(&sc, usc, offsetof(struct sigcontext, reserved1)))
		return false;

	regs->bx = sc.bx;
	regs->cx = sc.cx;
	regs->dx = sc.dx;
	regs->si = sc.si;
	regs->di = sc.di;
	regs->bp = sc.bp;
	regs->ax = sc.ax;
	regs->sp = sc.sp;
	regs->ip = sc.ip;
	regs->r8 = sc.r8;
	regs->r9 = sc.r9;
	regs->r10 = sc.r10;
	regs->r11 = sc.r11;
	regs->r12 = sc.r12;
	regs->r13 = sc.r13;
	regs->r14 = sc.r14;
	regs->r15 = sc.r15;

	/* Get CS/SS and force CPL3 */
	regs->cs = sc.cs | 0x03;
	regs->ss = sc.ss | 0x03;

	regs->flags = (regs->flags & ~FIX_EFLAGS) | (sc.flags & FIX_EFLAGS);
	/* disable syscall checks */
	regs->orig_ax = -1;

	/*
	 * Fix up SS if needed for the benefit of old DOSEMU and
	 * CRIU.
	 */
	if (unlikely(!(uc_flags & UC_STRICT_RESTORE_SS) && user_64bit_mode(regs)))
		force_valid_ss(regs);

	return fpu__restore_sig((void __user *)sc.fpstate, 0);
}

static __always_inline int
__unsafe_setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
		     struct pt_regs *regs, unsigned long mask)
{
	unsafe_put_user(regs->di, &sc->di, Efault);
	unsafe_put_user(regs->si, &sc->si, Efault);
	unsafe_put_user(regs->bp, &sc->bp, Efault);
	unsafe_put_user(regs->sp, &sc->sp, Efault);
	unsafe_put_user(regs->bx, &sc->bx, Efault);
	unsafe_put_user(regs->dx, &sc->dx, Efault);
	unsafe_put_user(regs->cx, &sc->cx, Efault);
	unsafe_put_user(regs->ax, &sc->ax, Efault);
	unsafe_put_user(regs->r8, &sc->r8, Efault);
	unsafe_put_user(regs->r9, &sc->r9, Efault);
	unsafe_put_user(regs->r10, &sc->r10, Efault);
	unsafe_put_user(regs->r11, &sc->r11, Efault);
	unsafe_put_user(regs->r12, &sc->r12, Efault);
	unsafe_put_user(regs->r13, &sc->r13, Efault);
	unsafe_put_user(regs->r14, &sc->r14, Efault);
	unsafe_put_user(regs->r15, &sc->r15, Efault);

	unsafe_put_user(current->thread.trap_nr, &sc->trapno, Efault);
	unsafe_put_user(current->thread.error_code, &sc->err, Efault);
	unsafe_put_user(regs->ip, &sc->ip, Efault);
	unsafe_put_user(regs->flags, &sc->flags, Efault);
	unsafe_put_user(regs->cs, &sc->cs, Efault);
	unsafe_put_user(0, &sc->gs, Efault);
	unsafe_put_user(0, &sc->fs, Efault);
	unsafe_put_user(regs->ss, &sc->ss, Efault);

	unsafe_put_user(fpstate, (unsigned long __user *)&sc->fpstate, Efault);

	/* non-iBCS2 extensions.. */
	unsafe_put_user(mask, &sc->oldmask, Efault);
	unsafe_put_user(current->thread.cr2, &sc->cr2, Efault);
	return 0;
Efault:
	return -EFAULT;
}

#define unsafe_put_sigcontext(sc, fp, regs, set, label)			\
do {									\
	if (__unsafe_setup_sigcontext(sc, fp, regs, set->sig[0]))	\
		goto label;						\
} while(0);

#define unsafe_put_sigmask(set, frame, label) \
	unsafe_put_user(*(__u64 *)(set), \
			(__u64 __user *)&(frame)->uc.uc_sigmask, \
			label)

static unsigned long frame_uc_flags(struct pt_regs *regs)
{
	unsigned long flags;

	if (boot_cpu_has(X86_FEATURE_XSAVE))
		flags = UC_FP_XSTATE | UC_SIGCONTEXT_SS;
	else
		flags = UC_SIGCONTEXT_SS;

	if (likely(user_64bit_mode(regs)))
		flags |= UC_STRICT_RESTORE_SS;

	return flags;
}

int x64_setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *set = sigmask_to_save();
	struct rt_sigframe __user *frame;
	void __user *fp = NULL;
	unsigned long uc_flags;

	/* x86-64 should always use SA_RESTORER. */
	if (!(ksig->ka.sa.sa_flags & SA_RESTORER))
		return -EFAULT;

	frame = get_sigframe(ksig, regs, sizeof(struct rt_sigframe), &fp);
	uc_flags = frame_uc_flags(regs);

	if (setup_signal_shadow_stack(ksig))
		return -EFAULT;

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	/* Create the ucontext.  */
	unsafe_put_user(uc_flags, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	unsafe_put_user(ksig->ka.sa.sa_restorer, &frame->pretcode, Efault);
	unsafe_put_sigcontext(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_sigmask(set, frame, Efault);
	user_access_end();

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (copy_siginfo_to_user(&frame->info, &ksig->info))
			return -EFAULT;
	}

	/* Set up registers for signal handler */
	regs->di = ksig->sig;
	/* In case the signal handler was declared without prototypes */
	regs->ax = 0;

	/* This also works for non SA_SIGINFO handlers because they expect the
	   next argument after the signal number on the stack. */
	regs->si = (unsigned long)&frame->info;
	regs->dx = (unsigned long)&frame->uc;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	regs->sp = (unsigned long)frame;

	/*
	 * Set up the CS and SS registers to run signal handlers in
	 * 64-bit mode, even if the handler happens to be interrupting
	 * 32-bit or 16-bit code.
	 *
	 * SS is subtle.  In 64-bit mode, we don't need any particular
	 * SS descriptor, but we do need SS to be valid.  It's possible
	 * that the old SS is entirely bogus -- this can happen if the
	 * signal we're trying to deliver is #GP or #SS caused by a bad
	 * SS value.  We also have a compatibility issue here: DOSEMU
	 * relies on the contents of the SS register indicating the
	 * SS value at the time of the signal, even though that code in
	 * DOSEMU predates sigreturn's ability to restore SS.  (DOSEMU
	 * avoids relying on sigreturn to restore SS; instead it uses
	 * a trampoline.)  So we do our best: if the old SS was valid,
	 * we keep it.  Otherwise we replace it.
	 */
	regs->cs = __USER_CS;

	if (unlikely(regs->ss != __USER_DS))
		force_valid_ss(regs);

	return 0;

Efault:
	user_access_end();
	return -EFAULT;
}

/*
 * Do a signal return; undo the signal stack.
 */
SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t set;
	unsigned long uc_flags;

	frame = (struct rt_sigframe __user *)(regs->sp - sizeof(long));
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(*(__u64 *)&set, (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (!restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (restore_signal_shadow_stack())
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "rt_sigreturn");
	return 0;
}

#ifdef CONFIG_X86_X32_ABI
static int x32_copy_siginfo_to_user(struct compat_siginfo __user *to,
		const struct kernel_siginfo *from)
{
	struct compat_siginfo new;

	copy_siginfo_to_external32(&new, from);
	if (from->si_signo == SIGCHLD) {
		new._sifields._sigchld_x32._utime = from->si_utime;
		new._sifields._sigchld_x32._stime = from->si_stime;
	}
	if (copy_to_user(to, &new, sizeof(struct compat_siginfo)))
		return -EFAULT;
	return 0;
}

int copy_siginfo_to_user32(struct compat_siginfo __user *to,
			   const struct kernel_siginfo *from)
{
	if (in_x32_syscall())
		return x32_copy_siginfo_to_user(to, from);
	return __copy_siginfo_to_user32(to, from);
}

int x32_setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	compat_sigset_t *set = (compat_sigset_t *) sigmask_to_save();
	struct rt_sigframe_x32 __user *frame;
	unsigned long uc_flags;
	void __user *restorer;
	void __user *fp = NULL;

	if (!(ksig->ka.sa.sa_flags & SA_RESTORER))
		return -EFAULT;

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fp);

	uc_flags = frame_uc_flags(regs);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	/* Create the ucontext.  */
	unsafe_put_user(uc_flags, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_compat_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);
	unsafe_put_user(0, &frame->uc.uc__pad0, Efault);
	restorer = ksig->ka.sa.sa_restorer;
	unsafe_put_user(restorer, (unsigned long __user *)&frame->pretcode, Efault);
	unsafe_put_sigcontext(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_sigmask(set, frame, Efault);
	user_access_end();

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (x32_copy_siginfo_to_user(&frame->info, &ksig->info))
			return -EFAULT;
	}

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	/* We use the x32 calling convention here... */
	regs->di = ksig->sig;
	regs->si = (unsigned long) &frame->info;
	regs->dx = (unsigned long) &frame->uc;

	loadsegment(ds, __USER_DS);
	loadsegment(es, __USER_DS);

	regs->cs = __USER_CS;
	regs->ss = __USER_DS;

	return 0;

Efault:
	user_access_end();
	return -EFAULT;
}

COMPAT_SYSCALL_DEFINE0(x32_rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe_x32 __user *frame;
	sigset_t set;
	unsigned long uc_flags;

	frame = (struct rt_sigframe_x32 __user *)(regs->sp - 8);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (!restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (compat_restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "x32 rt_sigreturn");
	return 0;
}
#endif /* CONFIG_X86_X32_ABI */

#ifdef CONFIG_COMPAT
void sigaction_compat_abi(struct k_sigaction *act, struct k_sigaction *oact)
{
	if (!act)
		return;

	if (in_ia32_syscall())
		act->sa.sa_flags |= SA_IA32_ABI;
	if (in_x32_syscall())
		act->sa.sa_flags |= SA_X32_ABI;
}
#endif /* CONFIG_COMPAT */

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
static_assert(sizeof(siginfo_t) == 128);

/* This is a part of the ABI and can never change in alignment */
static_assert(__alignof__(siginfo_t) == 8);

/*
* The offsets of all the (unioned) si_fields are fixed
* in the ABI, of course.  Make sure none of them ever
* move and are always at the beginning:
*/
static_assert(offsetof(siginfo_t, si_signo) == 0);
static_assert(offsetof(siginfo_t, si_errno) == 4);
static_assert(offsetof(siginfo_t, si_code)  == 8);

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
	static_assert(offsetof(siginfo_t, _sifields) == 		\
		      offsetof(siginfo_t, _sifields.name))
#define CHECK_SI_SIZE(name, size)					\
	static_assert(sizeof_field(siginfo_t, _sifields.name) == size)

CHECK_SI_OFFSET(_kill);
CHECK_SI_SIZE  (_kill, 2*sizeof(int));
static_assert(offsetof(siginfo_t, si_pid) == 0x10);
static_assert(offsetof(siginfo_t, si_uid) == 0x14);

CHECK_SI_OFFSET(_timer);
CHECK_SI_SIZE  (_timer, 6*sizeof(int));
static_assert(offsetof(siginfo_t, si_tid)     == 0x10);
static_assert(offsetof(siginfo_t, si_overrun) == 0x14);
static_assert(offsetof(siginfo_t, si_value)   == 0x18);

CHECK_SI_OFFSET(_rt);
CHECK_SI_SIZE  (_rt, 4*sizeof(int));
static_assert(offsetof(siginfo_t, si_pid)   == 0x10);
static_assert(offsetof(siginfo_t, si_uid)   == 0x14);
static_assert(offsetof(siginfo_t, si_value) == 0x18);

CHECK_SI_OFFSET(_sigchld);
CHECK_SI_SIZE  (_sigchld, 8*sizeof(int));
static_assert(offsetof(siginfo_t, si_pid)    == 0x10);
static_assert(offsetof(siginfo_t, si_uid)    == 0x14);
static_assert(offsetof(siginfo_t, si_status) == 0x18);
static_assert(offsetof(siginfo_t, si_utime)  == 0x20);
static_assert(offsetof(siginfo_t, si_stime)  == 0x28);

#ifdef CONFIG_X86_X32_ABI
/* no _sigchld_x32 in the generic siginfo_t */
static_assert(sizeof_field(compat_siginfo_t, _sifields._sigchld_x32) ==
	      7*sizeof(int));
static_assert(offsetof(compat_siginfo_t, _sifields) ==
	      offsetof(compat_siginfo_t, _sifields._sigchld_x32));
static_assert(offsetof(compat_siginfo_t, _sifields._sigchld_x32._utime)  == 0x18);
static_assert(offsetof(compat_siginfo_t, _sifields._sigchld_x32._stime)  == 0x20);
#endif

CHECK_SI_OFFSET(_sigfault);
CHECK_SI_SIZE  (_sigfault, 8*sizeof(int));
static_assert(offsetof(siginfo_t, si_addr)	== 0x10);

static_assert(offsetof(siginfo_t, si_trapno)	== 0x18);

static_assert(offsetof(siginfo_t, si_addr_lsb)	== 0x18);

static_assert(offsetof(siginfo_t, si_lower)	== 0x20);
static_assert(offsetof(siginfo_t, si_upper)	== 0x28);

static_assert(offsetof(siginfo_t, si_pkey)	== 0x20);

static_assert(offsetof(siginfo_t, si_perf_data)	 == 0x18);
static_assert(offsetof(siginfo_t, si_perf_type)	 == 0x20);
static_assert(offsetof(siginfo_t, si_perf_flags) == 0x24);

CHECK_SI_OFFSET(_sigpoll);
CHECK_SI_SIZE  (_sigpoll, 4*sizeof(int));
static_assert(offsetof(siginfo_t, si_band) == 0x10);
static_assert(offsetof(siginfo_t, si_fd)   == 0x18);

CHECK_SI_OFFSET(_sigsys);
CHECK_SI_SIZE  (_sigsys, 4*sizeof(int));
static_assert(offsetof(siginfo_t, si_call_addr) == 0x10);
static_assert(offsetof(siginfo_t, si_syscall)   == 0x18);
static_assert(offsetof(siginfo_t, si_arch)      == 0x1C);

/* any new si_fields should be added here */
