#include <linux/compat.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>

/*
 * The compat_siginfo_t structure and handing code is very easy
 * to break in several ways.  It must always be updated when new
 * updates are made to the main siginfo_t, and
 * copy_siginfo_to_user32() must be updated when the
 * (arch-independent) copy_siginfo_to_user() is updated.
 *
 * It is also easy to put a new member in the compat_siginfo_t
 * which has implicit alignment which can move internal structure
 * alignment around breaking the ABI.  This can happen if you,
 * for instance, put a plain 64-bit value in there.
 */
static inline void signal_compat_build_tests(void)
{
	int _sifields_offset = offsetof(compat_siginfo_t, _sifields);

	/*
	 * If adding a new si_code, there is probably new data in
	 * the siginfo.  Make sure folks bumping the si_code
	 * limits also have to look at this code.  Make sure any
	 * new fields are handled in copy_siginfo_to_user32()!
	 */
	BUILD_BUG_ON(NSIGILL  != 8);
	BUILD_BUG_ON(NSIGFPE  != 8);
	BUILD_BUG_ON(NSIGSEGV != 4);
	BUILD_BUG_ON(NSIGBUS  != 5);
	BUILD_BUG_ON(NSIGTRAP != 4);
	BUILD_BUG_ON(NSIGCHLD != 6);
	BUILD_BUG_ON(NSIGSYS  != 1);

	/* This is part of the ABI and can never change in size: */
	BUILD_BUG_ON(sizeof(compat_siginfo_t) != 128);
	/*
	 * The offsets of all the (unioned) si_fields are fixed
	 * in the ABI, of course.  Make sure none of them ever
	 * move and are always at the beginning:
	 */
	BUILD_BUG_ON(offsetof(compat_siginfo_t, _sifields) != 3 * sizeof(int));
#define CHECK_CSI_OFFSET(name)	  BUILD_BUG_ON(_sifields_offset != offsetof(compat_siginfo_t, _sifields.name))

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
#define CHECK_CSI_SIZE(name, size) BUILD_BUG_ON(size != sizeof(((compat_siginfo_t *)0)->_sifields.name))
#define CHECK_SI_SIZE(name, size) BUILD_BUG_ON(size != sizeof(((siginfo_t *)0)->_sifields.name))

	CHECK_CSI_OFFSET(_kill);
	CHECK_CSI_SIZE  (_kill, 2*sizeof(int));
	CHECK_SI_SIZE   (_kill, 2*sizeof(int));

	CHECK_CSI_OFFSET(_timer);
	CHECK_CSI_SIZE  (_timer, 5*sizeof(int));
	CHECK_SI_SIZE   (_timer, 6*sizeof(int));

	CHECK_CSI_OFFSET(_rt);
	CHECK_CSI_SIZE  (_rt, 3*sizeof(int));
	CHECK_SI_SIZE   (_rt, 4*sizeof(int));

	CHECK_CSI_OFFSET(_sigchld);
	CHECK_CSI_SIZE  (_sigchld, 5*sizeof(int));
	CHECK_SI_SIZE   (_sigchld, 8*sizeof(int));

	CHECK_CSI_OFFSET(_sigchld_x32);
	CHECK_CSI_SIZE  (_sigchld_x32, 7*sizeof(int));
	/* no _sigchld_x32 in the generic siginfo_t */

	CHECK_CSI_OFFSET(_sigfault);
	CHECK_CSI_SIZE  (_sigfault, 4*sizeof(int));
	CHECK_SI_SIZE   (_sigfault, 8*sizeof(int));

	CHECK_CSI_OFFSET(_sigpoll);
	CHECK_CSI_SIZE  (_sigpoll, 2*sizeof(int));
	CHECK_SI_SIZE   (_sigpoll, 4*sizeof(int));

	CHECK_CSI_OFFSET(_sigsys);
	CHECK_CSI_SIZE  (_sigsys, 3*sizeof(int));
	CHECK_SI_SIZE   (_sigsys, 4*sizeof(int));

	/* any new si_fields should be added here */
}

void sigaction_compat_abi(struct k_sigaction *act, struct k_sigaction *oact)
{
	/* Don't leak in-kernel non-uapi flags to user-space */
	if (oact)
		oact->sa.sa_flags &= ~(SA_IA32_ABI | SA_X32_ABI);

	if (!act)
		return;

	/* Don't let flags to be set from userspace */
	act->sa.sa_flags &= ~(SA_IA32_ABI | SA_X32_ABI);

	if (user_64bit_mode(current_pt_regs()))
		return;

	if (in_ia32_syscall())
		act->sa.sa_flags |= SA_IA32_ABI;
	if (in_x32_syscall())
		act->sa.sa_flags |= SA_X32_ABI;
}

int __copy_siginfo_to_user32(compat_siginfo_t __user *to, const siginfo_t *from,
		bool x32_ABI)
{
	int err = 0;

	signal_compat_build_tests();

	if (!access_ok(VERIFY_WRITE, to, sizeof(compat_siginfo_t)))
		return -EFAULT;

	put_user_try {
		/* If you change siginfo_t structure, please make sure that
		   this code is fixed accordingly.
		   It should never copy any pad contained in the structure
		   to avoid security leaks, but must copy the generic
		   3 ints plus the relevant union member.  */
		put_user_ex(from->si_signo, &to->si_signo);
		put_user_ex(from->si_errno, &to->si_errno);
		put_user_ex((short)from->si_code, &to->si_code);

		if (from->si_code < 0) {
			put_user_ex(from->si_pid, &to->si_pid);
			put_user_ex(from->si_uid, &to->si_uid);
			put_user_ex(ptr_to_compat(from->si_ptr), &to->si_ptr);
		} else {
			/*
			 * First 32bits of unions are always present:
			 * si_pid === si_band === si_tid === si_addr(LS half)
			 */
			put_user_ex(from->_sifields._pad[0],
					  &to->_sifields._pad[0]);
			switch (from->si_code >> 16) {
			case __SI_FAULT >> 16:
				if (from->si_signo == SIGBUS &&
				    (from->si_code == BUS_MCEERR_AR ||
				     from->si_code == BUS_MCEERR_AO))
					put_user_ex(from->si_addr_lsb, &to->si_addr_lsb);

				if (from->si_signo == SIGSEGV) {
					if (from->si_code == SEGV_BNDERR) {
						compat_uptr_t lower = (unsigned long)&to->si_lower;
						compat_uptr_t upper = (unsigned long)&to->si_upper;
						put_user_ex(lower, &to->si_lower);
						put_user_ex(upper, &to->si_upper);
					}
					if (from->si_code == SEGV_PKUERR)
						put_user_ex(from->si_pkey, &to->si_pkey);
				}
				break;
			case __SI_SYS >> 16:
				put_user_ex(from->si_syscall, &to->si_syscall);
				put_user_ex(from->si_arch, &to->si_arch);
				break;
			case __SI_CHLD >> 16:
				if (!x32_ABI) {
					put_user_ex(from->si_utime, &to->si_utime);
					put_user_ex(from->si_stime, &to->si_stime);
				} else {
					put_user_ex(from->si_utime, &to->_sifields._sigchld_x32._utime);
					put_user_ex(from->si_stime, &to->_sifields._sigchld_x32._stime);
				}
				put_user_ex(from->si_status, &to->si_status);
				/* FALL THROUGH */
			default:
			case __SI_KILL >> 16:
				put_user_ex(from->si_uid, &to->si_uid);
				break;
			case __SI_POLL >> 16:
				put_user_ex(from->si_fd, &to->si_fd);
				break;
			case __SI_TIMER >> 16:
				put_user_ex(from->si_overrun, &to->si_overrun);
				put_user_ex(ptr_to_compat(from->si_ptr),
					    &to->si_ptr);
				break;
				 /* This is not generated by the kernel as of now.  */
			case __SI_RT >> 16:
			case __SI_MESGQ >> 16:
				put_user_ex(from->si_uid, &to->si_uid);
				put_user_ex(from->si_int, &to->si_int);
				break;
			}
		}
	} put_user_catch(err);

	return err;
}

/* from syscall's path, where we know the ABI */
int copy_siginfo_to_user32(compat_siginfo_t __user *to, const siginfo_t *from)
{
	return __copy_siginfo_to_user32(to, from, in_x32_syscall());
}

int copy_siginfo_from_user32(siginfo_t *to, compat_siginfo_t __user *from)
{
	int err = 0;
	u32 ptr32;

	if (!access_ok(VERIFY_READ, from, sizeof(compat_siginfo_t)))
		return -EFAULT;

	get_user_try {
		get_user_ex(to->si_signo, &from->si_signo);
		get_user_ex(to->si_errno, &from->si_errno);
		get_user_ex(to->si_code, &from->si_code);

		get_user_ex(to->si_pid, &from->si_pid);
		get_user_ex(to->si_uid, &from->si_uid);
		get_user_ex(ptr32, &from->si_ptr);
		to->si_ptr = compat_ptr(ptr32);
	} get_user_catch(err);

	return err;
}
