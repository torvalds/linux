// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <wait.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <init.h>
#include <os.h>

void stack_protections(unsigned long address)
{
	if (mprotect((void *) address, UM_THREAD_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC) < 0)
		panic("protecting stack failed, errno = %d", errno);
}

int raw(int fd)
{
	struct termios tt;
	int err;

	CATCH_EINTR(err = tcgetattr(fd, &tt));
	if (err < 0)
		return -errno;

	cfmakeraw(&tt);

	CATCH_EINTR(err = tcsetattr(fd, TCSADRAIN, &tt));
	if (err < 0)
		return -errno;

	/*
	 * XXX tcsetattr could have applied only some changes
	 * (and cfmakeraw() is a set of changes)
	 */
	return 0;
}

void setup_machinename(char *machine_out)
{
	struct utsname host;

	uname(&host);
#ifdef UML_CONFIG_UML_X86
# ifndef UML_CONFIG_64BIT
	if (!strcmp(host.machine, "x86_64")) {
		strcpy(machine_out, "i686");
		return;
	}
# else
	if (!strcmp(host.machine, "i686")) {
		strcpy(machine_out, "x86_64");
		return;
	}
# endif
#endif
	strcpy(machine_out, host.machine);
}

void setup_hostinfo(char *buf, int len)
{
	struct utsname host;

	uname(&host);
	snprintf(buf, len, "%s %s %s %s %s", host.sysname, host.nodename,
		 host.release, host.version, host.machine);
}

/*
 * We cannot use glibc's abort(). It makes use of tgkill() which
 * has no effect within UML's kernel threads.
 * After that glibc would execute an invalid instruction to kill
 * the calling process and UML crashes with SIGSEGV.
 */
static inline void __attribute__ ((noreturn)) uml_abort(void)
{
	sigset_t sig;

	fflush(NULL);

	if (!sigemptyset(&sig) && !sigaddset(&sig, SIGABRT))
		sigprocmask(SIG_UNBLOCK, &sig, 0);

	for (;;)
		if (kill(getpid(), SIGABRT) < 0)
			exit(127);
}

/*
 * UML helper threads must not handle SIGWINCH/INT/TERM
 */
void os_fix_helper_signals(void)
{
	signal(SIGWINCH, SIG_IGN);
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}

void os_dump_core(void)
{
	int pid;

	signal(SIGSEGV, SIG_DFL);

	/*
	 * We are about to SIGTERM this entire process group to ensure that
	 * nothing is around to run after the kernel exits.  The
	 * kernel wants to abort, not die through SIGTERM, so we
	 * ignore it here.
	 */

	signal(SIGTERM, SIG_IGN);
	kill(0, SIGTERM);
	/*
	 * Most of the other processes associated with this UML are
	 * likely sTopped, so give them a SIGCONT so they see the
	 * SIGTERM.
	 */
	kill(0, SIGCONT);

	/*
	 * Now, having sent signals to everyone but us, make sure they
	 * die by ptrace.  Processes can survive what's been done to
	 * them so far - the mechanism I understand is receiving a
	 * SIGSEGV and segfaulting immediately upon return.  There is
	 * always a SIGSEGV pending, and (I'm guessing) signals are
	 * processed in numeric order so the SIGTERM (signal 15 vs
	 * SIGSEGV being signal 11) is never handled.
	 *
	 * Run a waitpid loop until we get some kind of error.
	 * Hopefully, it's ECHILD, but there's not a lot we can do if
	 * it's something else.  Tell os_kill_ptraced_process not to
	 * wait for the child to report its death because there's
	 * nothing reasonable to do if that fails.
	 */

	while ((pid = waitpid(-1, NULL, WNOHANG | __WALL)) > 0)
		os_kill_ptraced_process(pid, 0);

	uml_abort();
}

void um_early_printk(const char *s, unsigned int n)
{
	printf("%.*s", n, s);
}

static int quiet_info;

static int __init quiet_cmd_param(char *str, int *add)
{
	quiet_info = 1;
	return 0;
}

__uml_setup("quiet", quiet_cmd_param,
"quiet\n"
"    Turns off information messages during boot.\n\n");

void os_info(const char *fmt, ...)
{
	va_list list;

	if (quiet_info)
		return;

	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);
}

void os_warn(const char *fmt, ...)
{
	va_list list;

	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);
}
