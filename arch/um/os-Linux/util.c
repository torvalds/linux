/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/param.h>
#include <sys/time.h>
#include "asm/types.h"
#include <ctype.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>
#include <stdarg.h>
#include <sched.h>
#include <termios.h>
#include <string.h>
#include "kern_util.h"
#include "user.h"
#include "mem_user.h"
#include "init.h"
#include "ptrace_user.h"
#include "uml-config.h"
#include "os.h"
#include "longjmp.h"
#include "kern_constants.h"

void stack_protections(unsigned long address)
{
	if(mprotect((void *) address, UM_THREAD_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC) < 0)
		panic("protecting stack failed, errno = %d", errno);
}

int raw(int fd)
{
	struct termios tt;
	int err;

	CATCH_EINTR(err = tcgetattr(fd, &tt));
	if(err < 0)
		return -errno;

	cfmakeraw(&tt);

	CATCH_EINTR(err = tcsetattr(fd, TCSADRAIN, &tt));
	if(err < 0)
		return -errno;

	/* XXX tcsetattr could have applied only some changes
	 * (and cfmakeraw() is a set of changes) */
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

int setjmp_wrapper(void (*proc)(void *, void *), ...)
{
	va_list args;
	jmp_buf buf;
	int n;

	n = UML_SETJMP(&buf);
	if(n == 0){
		va_start(args, proc);
		(*proc)(&buf, &args);
	}
	va_end(args);
	return n;
}

void os_dump_core(void)
{
	signal(SIGSEGV, SIG_DFL);
	abort();
}
