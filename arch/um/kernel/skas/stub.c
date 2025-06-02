// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 */

#include <sysdep/stub.h>

#include <linux/futex.h>
#include <errno.h>

static __always_inline int syscall_handler(struct stub_data *d)
{
	int i;
	unsigned long res;

	for (i = 0; i < d->syscall_data_len; i++) {
		struct stub_syscall *sc = &d->syscall_data[i];

		switch (sc->syscall) {
		case STUB_SYSCALL_MMAP:
			res = stub_syscall6(STUB_MMAP_NR,
					    sc->mem.addr, sc->mem.length,
					    sc->mem.prot,
					    MAP_SHARED | MAP_FIXED,
					    sc->mem.fd, sc->mem.offset);
			if (res != sc->mem.addr) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
		case STUB_SYSCALL_MUNMAP:
			res = stub_syscall2(__NR_munmap,
					    sc->mem.addr, sc->mem.length);
			if (res) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
		default:
			d->err = -95; /* EOPNOTSUPP */
			d->syscall_data_len = i;
			return -1;
		}
	}

	d->err = 0;
	d->syscall_data_len = 0;

	return 0;
}

void __section(".__syscall_stub")
stub_syscall_handler(void)
{
	struct stub_data *d = get_stub_data();

	syscall_handler(d);

	trap_myself();
}

void __section(".__syscall_stub")
stub_signal_interrupt(int sig, siginfo_t *info, void *p)
{
	struct stub_data *d = get_stub_data();
	ucontext_t *uc = p;
	long res;

	d->signal = sig;
	d->si_offset = (unsigned long)info - (unsigned long)&d->sigstack[0];
	d->mctx_offset = (unsigned long)&uc->uc_mcontext - (unsigned long)&d->sigstack[0];

restart_wait:
	d->futex = FUTEX_IN_KERN;
	do {
		res = stub_syscall3(__NR_futex, (unsigned long)&d->futex,
				    FUTEX_WAKE, 1);
	} while (res == -EINTR);
	do {
		res = stub_syscall4(__NR_futex, (unsigned long)&d->futex,
				    FUTEX_WAIT, FUTEX_IN_KERN, 0);
	} while (res == -EINTR || d->futex == FUTEX_IN_KERN);

	if (res < 0 && res != -EAGAIN)
		stub_syscall1(__NR_exit_group, 1);

	/* Try running queued syscalls. */
	if (syscall_handler(d) < 0 || d->restart_wait) {
		/* Report SIGSYS if we restart. */
		d->signal = SIGSYS;
		d->restart_wait = 0;
		goto restart_wait;
	}

	/* Restore arch dependent state that is not part of the mcontext */
	stub_seccomp_restore_state(&d->arch_data);

	/* Return so that the host modified mcontext is restored. */
}

void __section(".__syscall_stub")
stub_signal_restorer(void)
{
	/* We must not have anything on the stack when doing rt_sigreturn */
	stub_syscall0(__NR_rt_sigreturn);
}
