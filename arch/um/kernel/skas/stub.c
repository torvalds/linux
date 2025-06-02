// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 */

#include <sysdep/stub.h>

#include <linux/futex.h>
#include <sys/socket.h>
#include <errno.h>

/*
 * Known security issues
 *
 * Userspace can jump to this address to execute *any* syscall that is
 * permitted by the stub. As we will return afterwards, it can do
 * whatever it likes, including:
 * - Tricking the kernel into handing out the memory FD
 * - Using this memory FD to read/write all physical memory
 * - Running in parallel to the kernel processing a syscall
 *   (possibly creating data races?)
 * - Blocking e.g. SIGALRM to avoid time based scheduling
 *
 * To avoid this, the permitted location for each syscall needs to be
 * checked for in the SECCOMP filter (which is reasonably simple). Also,
 * more care will need to go into considerations how the code might be
 * tricked by using a prepared stack (or even modifying the stack from
 * another thread in case SMP support is added).
 *
 * As for the SIGALRM, the best counter measure will be to check in the
 * kernel that the process is reporting back the SIGALRM in a timely
 * fashion.
 */
static __always_inline int syscall_handler(int fd_map[STUB_MAX_FDS])
{
	struct stub_data *d = get_stub_data();
	int i;
	unsigned long res;
	int fd;

	for (i = 0; i < d->syscall_data_len; i++) {
		struct stub_syscall *sc = &d->syscall_data[i];

		switch (sc->syscall) {
		case STUB_SYSCALL_MMAP:
			if (fd_map)
				fd = fd_map[sc->mem.fd];
			else
				fd = sc->mem.fd;

			res = stub_syscall6(STUB_MMAP_NR,
					    sc->mem.addr, sc->mem.length,
					    sc->mem.prot,
					    MAP_SHARED | MAP_FIXED,
					    fd, sc->mem.offset);
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
	syscall_handler(NULL);

	trap_myself();
}

void __section(".__syscall_stub")
stub_signal_interrupt(int sig, siginfo_t *info, void *p)
{
	struct stub_data *d = get_stub_data();
	char rcv_data;
	union {
		char data[CMSG_SPACE(sizeof(int) * STUB_MAX_FDS)];
		struct cmsghdr align;
	} ctrl = {};
	struct iovec iov = {
		.iov_base = &rcv_data,
		.iov_len = 1,
	};
	struct msghdr msghdr = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = &ctrl,
		.msg_controllen = sizeof(ctrl),
	};
	ucontext_t *uc = p;
	struct cmsghdr *fd_msg;
	int *fd_map;
	int num_fds;
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

	if (d->syscall_data_len) {
		/* Read passed FDs (if any) */
		do {
			res = stub_syscall3(__NR_recvmsg, 0, (unsigned long)&msghdr, 0);
		} while (res == -EINTR);

		/* We should never have a receive error (other than -EAGAIN) */
		if (res < 0 && res != -EAGAIN)
			stub_syscall1(__NR_exit_group, 1);

		/* Receive the FDs */
		num_fds = 0;
		fd_msg = msghdr.msg_control;
		fd_map = (void *)&CMSG_DATA(fd_msg);
		if (res == iov.iov_len && msghdr.msg_controllen > sizeof(struct cmsghdr))
			num_fds = (fd_msg->cmsg_len - CMSG_LEN(0)) / sizeof(int);

		/* Try running queued syscalls. */
		res = syscall_handler(fd_map);

		while (num_fds)
			stub_syscall2(__NR_close, fd_map[--num_fds], 0);
	} else {
		res = 0;
	}

	if (res < 0 || d->restart_wait) {
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
