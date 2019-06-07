// SPDX-License-Identifier: GPL-2.0
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
	BUILD_BUG_ON(NSIGILL  != 11);
	BUILD_BUG_ON(NSIGFPE  != 15);
	BUILD_BUG_ON(NSIGSEGV != 7);
	BUILD_BUG_ON(NSIGBUS  != 5);
	BUILD_BUG_ON(NSIGTRAP != 5);
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

	BUILD_BUG_ON(offsetof(siginfo_t, si_signo) != 0);
	BUILD_BUG_ON(offsetof(siginfo_t, si_errno) != 4);
	BUILD_BUG_ON(offsetof(siginfo_t, si_code)  != 8);

	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_signo) != 0);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_errno) != 4);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_code)  != 8);
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

	BUILD_BUG_ON(offsetof(siginfo_t, si_pid) != 0x10);
	BUILD_BUG_ON(offsetof(siginfo_t, si_uid) != 0x14);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_pid) != 0xC);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_uid) != 0x10);

	CHECK_CSI_OFFSET(_timer);
	CHECK_CSI_SIZE  (_timer, 3*sizeof(int));
	CHECK_SI_SIZE   (_timer, 6*sizeof(int));

	BUILD_BUG_ON(offsetof(siginfo_t, si_tid)     != 0x10);
	BUILD_BUG_ON(offsetof(siginfo_t, si_overrun) != 0x14);
	BUILD_BUG_ON(offsetof(siginfo_t, si_value)   != 0x18);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_tid)     != 0x0C);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_overrun) != 0x10);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_value)   != 0x14);

	CHECK_CSI_OFFSET(_rt);
	CHECK_CSI_SIZE  (_rt, 3*sizeof(int));
	CHECK_SI_SIZE   (_rt, 4*sizeof(int));

	BUILD_BUG_ON(offsetof(siginfo_t, si_pid)   != 0x10);
	BUILD_BUG_ON(offsetof(siginfo_t, si_uid)   != 0x14);
	BUILD_BUG_ON(offsetof(siginfo_t, si_value) != 0x18);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_pid)   != 0x0C);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_uid)   != 0x10);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_value) != 0x14);

	CHECK_CSI_OFFSET(_sigchld);
	CHECK_CSI_SIZE  (_sigchld, 5*sizeof(int));
	CHECK_SI_SIZE   (_sigchld, 8*sizeof(int));

	BUILD_BUG_ON(offsetof(siginfo_t, si_pid)    != 0x10);
	BUILD_BUG_ON(offsetof(siginfo_t, si_uid)    != 0x14);
	BUILD_BUG_ON(offsetof(siginfo_t, si_status) != 0x18);
	BUILD_BUG_ON(offsetof(siginfo_t, si_utime)  != 0x20);
	BUILD_BUG_ON(offsetof(siginfo_t, si_stime)  != 0x28);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_pid)    != 0x0C);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_uid)    != 0x10);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_status) != 0x14);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_utime)  != 0x18);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_stime)  != 0x1C);

#ifdef CONFIG_X86_X32_ABI
	CHECK_CSI_OFFSET(_sigchld_x32);
	CHECK_CSI_SIZE  (_sigchld_x32, 7*sizeof(int));
	/* no _sigchld_x32 in the generic siginfo_t */
	BUILD_BUG_ON(offsetof(compat_siginfo_t, _sifields._sigchld_x32._utime)  != 0x18);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, _sifields._sigchld_x32._stime)  != 0x20);
#endif

	CHECK_CSI_OFFSET(_sigfault);
	CHECK_CSI_SIZE  (_sigfault, 4*sizeof(int));
	CHECK_SI_SIZE   (_sigfault, 8*sizeof(int));

	BUILD_BUG_ON(offsetof(siginfo_t, si_addr) != 0x10);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_addr) != 0x0C);

	BUILD_BUG_ON(offsetof(siginfo_t, si_addr_lsb) != 0x18);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_addr_lsb) != 0x10);

	BUILD_BUG_ON(offsetof(siginfo_t, si_lower) != 0x20);
	BUILD_BUG_ON(offsetof(siginfo_t, si_upper) != 0x28);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_lower) != 0x14);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_upper) != 0x18);

	BUILD_BUG_ON(offsetof(siginfo_t, si_pkey) != 0x20);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_pkey) != 0x14);

	CHECK_CSI_OFFSET(_sigpoll);
	CHECK_CSI_SIZE  (_sigpoll, 2*sizeof(int));
	CHECK_SI_SIZE   (_sigpoll, 4*sizeof(int));

	BUILD_BUG_ON(offsetof(siginfo_t, si_band)   != 0x10);
	BUILD_BUG_ON(offsetof(siginfo_t, si_fd)     != 0x18);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_band) != 0x0C);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_fd)   != 0x10);

	CHECK_CSI_OFFSET(_sigsys);
	CHECK_CSI_SIZE  (_sigsys, 3*sizeof(int));
	CHECK_SI_SIZE   (_sigsys, 4*sizeof(int));

	BUILD_BUG_ON(offsetof(siginfo_t, si_call_addr) != 0x10);
	BUILD_BUG_ON(offsetof(siginfo_t, si_syscall)   != 0x18);
	BUILD_BUG_ON(offsetof(siginfo_t, si_arch)      != 0x1C);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_call_addr) != 0x0C);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_syscall)   != 0x10);
	BUILD_BUG_ON(offsetof(compat_siginfo_t, si_arch)      != 0x14);

	/* any new si_fields should be added here */
}

void sigaction_compat_abi(struct k_sigaction *act, struct k_sigaction *oact)
{
	signal_compat_build_tests();

	/* Don't leak in-kernel non-uapi flags to user-space */
	if (oact)
		oact->sa.sa_flags &= ~(SA_IA32_ABI | SA_X32_ABI);

	if (!act)
		return;

	/* Don't let flags to be set from userspace */
	act->sa.sa_flags &= ~(SA_IA32_ABI | SA_X32_ABI);

	if (in_ia32_syscall())
		act->sa.sa_flags |= SA_IA32_ABI;
	if (in_x32_syscall())
		act->sa.sa_flags |= SA_X32_ABI;
}
