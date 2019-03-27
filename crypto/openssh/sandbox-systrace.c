/* $OpenBSD: sandbox-systrace.c,v 1.18 2015/10/02 01:39:26 deraadt Exp $ */
/*
 * Copyright (c) 2011 Damien Miller <djm@mindrot.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#ifdef SANDBOX_SYSTRACE

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <dev/systrace.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "atomicio.h"
#include "log.h"
#include "ssh-sandbox.h"
#include "xmalloc.h"

struct sandbox_policy {
	int syscall;
	int action;
};

/* Permitted syscalls in preauth. Unlisted syscalls get SYSTR_POLICY_KILL */
static const struct sandbox_policy preauth_policy[] = {
	{ SYS_exit, SYSTR_POLICY_PERMIT },
#ifdef SYS_kbind
	{ SYS_kbind, SYSTR_POLICY_PERMIT },
#endif

	{ SYS_getpid, SYSTR_POLICY_PERMIT },
	{ SYS_getpgid, SYSTR_POLICY_PERMIT },
	{ SYS_clock_gettime, SYSTR_POLICY_PERMIT },
	{ SYS_gettimeofday, SYSTR_POLICY_PERMIT },
	{ SYS_nanosleep, SYSTR_POLICY_PERMIT },
	{ SYS_sigprocmask, SYSTR_POLICY_PERMIT },

#ifdef SYS_getentropy
	/* OpenBSD 5.6 and newer use getentropy(2) to seed arc4random(3). */
	{ SYS_getentropy, SYSTR_POLICY_PERMIT },
#else
	/* Previous releases used sysctl(3)'s kern.arnd variable. */
	{ SYS___sysctl, SYSTR_POLICY_PERMIT },
#endif
#ifdef SYS_sendsyslog
	{ SYS_sendsyslog, SYSTR_POLICY_PERMIT },
#endif

	{ SYS_madvise, SYSTR_POLICY_PERMIT },
	{ SYS_mmap, SYSTR_POLICY_PERMIT },
	{ SYS_mprotect, SYSTR_POLICY_PERMIT },
	{ SYS_mquery, SYSTR_POLICY_PERMIT },
	{ SYS_munmap, SYSTR_POLICY_PERMIT },

	{ SYS_poll, SYSTR_POLICY_PERMIT },
	{ SYS_select, SYSTR_POLICY_PERMIT },
	{ SYS_read, SYSTR_POLICY_PERMIT },
	{ SYS_write, SYSTR_POLICY_PERMIT },
	{ SYS_shutdown, SYSTR_POLICY_PERMIT },
	{ SYS_close, SYSTR_POLICY_PERMIT },

	{ SYS_open, SYSTR_POLICY_NEVER },

	{ -1, -1 }
};

struct ssh_sandbox {
	int systrace_fd;
	pid_t child_pid;
	void (*osigchld)(int);
};

struct ssh_sandbox *
ssh_sandbox_init(struct monitor *monitor)
{
	struct ssh_sandbox *box;

	debug3("%s: preparing systrace sandbox", __func__);
	box = xcalloc(1, sizeof(*box));
	box->systrace_fd = -1;
	box->child_pid = 0;
	box->osigchld = signal(SIGCHLD, SIG_IGN);

	return box;
}

void
ssh_sandbox_child(struct ssh_sandbox *box)
{
	debug3("%s: ready", __func__);
	signal(SIGCHLD, box->osigchld);
	if (kill(getpid(), SIGSTOP) != 0)
		fatal("%s: kill(%d, SIGSTOP)", __func__, getpid());
	debug3("%s: started", __func__);
}

static void
ssh_sandbox_parent(struct ssh_sandbox *box, pid_t child_pid,
    const struct sandbox_policy *allowed_syscalls)
{
	int dev_systrace, i, j, found, status;
	pid_t pid;
	struct systrace_policy policy;

	/* Wait for the child to send itself a SIGSTOP */
	debug3("%s: wait for child %ld", __func__, (long)child_pid);
	do {
		pid = waitpid(child_pid, &status, WUNTRACED);
	} while (pid == -1 && errno == EINTR);
	signal(SIGCHLD, box->osigchld);
	if (!WIFSTOPPED(status)) {
		if (WIFSIGNALED(status))
			fatal("%s: child terminated with signal %d",
			    __func__, WTERMSIG(status));
		if (WIFEXITED(status))
			fatal("%s: child exited with status %d",
			    __func__, WEXITSTATUS(status));
		fatal("%s: child not stopped", __func__);
	}
	debug3("%s: child %ld stopped", __func__, (long)child_pid);
	box->child_pid = child_pid;

	/* Set up systracing of child */
	if ((dev_systrace = open("/dev/systrace", O_RDONLY)) == -1)
		fatal("%s: open(\"/dev/systrace\"): %s", __func__,
		    strerror(errno));
	if (ioctl(dev_systrace, STRIOCCLONE, &box->systrace_fd) == -1)
		fatal("%s: ioctl(STRIOCCLONE, %d): %s", __func__,
		    dev_systrace, strerror(errno));
	close(dev_systrace);
	debug3("%s: systrace attach, fd=%d", __func__, box->systrace_fd);
	if (ioctl(box->systrace_fd, STRIOCATTACH, &child_pid) == -1)
		fatal("%s: ioctl(%d, STRIOCATTACH, %d): %s", __func__,
		    box->systrace_fd, child_pid, strerror(errno));

	/* Allocate and assign policy */
	memset(&policy, 0, sizeof(policy));
	policy.strp_op = SYSTR_POLICY_NEW;
	policy.strp_maxents = SYS_MAXSYSCALL;
	if (ioctl(box->systrace_fd, STRIOCPOLICY, &policy) == -1)
		fatal("%s: ioctl(%d, STRIOCPOLICY (new)): %s", __func__,
		    box->systrace_fd, strerror(errno));

	policy.strp_op = SYSTR_POLICY_ASSIGN;
	policy.strp_pid = box->child_pid;
	if (ioctl(box->systrace_fd, STRIOCPOLICY, &policy) == -1)
		fatal("%s: ioctl(%d, STRIOCPOLICY (assign)): %s",
		    __func__, box->systrace_fd, strerror(errno));

	/* Set per-syscall policy */
	for (i = 0; i < SYS_MAXSYSCALL; i++) {
		found = 0;
		for (j = 0; allowed_syscalls[j].syscall != -1; j++) {
			if (allowed_syscalls[j].syscall == i) {
				found = 1;
				break;
			}
		}
		policy.strp_op = SYSTR_POLICY_MODIFY;
		policy.strp_code = i;
		policy.strp_policy = found ?
		    allowed_syscalls[j].action : SYSTR_POLICY_KILL;
		if (found)
			debug3("%s: policy: enable syscall %d", __func__, i);
		if (ioctl(box->systrace_fd, STRIOCPOLICY, &policy) == -1)
			fatal("%s: ioctl(%d, STRIOCPOLICY (modify)): %s",
			    __func__, box->systrace_fd, strerror(errno));
	}

	/* Signal the child to start running */
	debug3("%s: start child %ld", __func__, (long)child_pid);
	if (kill(box->child_pid, SIGCONT) != 0)
		fatal("%s: kill(%d, SIGCONT)", __func__, box->child_pid);
}

void
ssh_sandbox_parent_finish(struct ssh_sandbox *box)
{
	/* Closing this before the child exits will terminate it */
	close(box->systrace_fd);

	free(box);
	debug3("%s: finished", __func__);
}

void
ssh_sandbox_parent_preauth(struct ssh_sandbox *box, pid_t child_pid)
{
	ssh_sandbox_parent(box, child_pid, preauth_policy);
}

#endif /* SANDBOX_SYSTRACE */
