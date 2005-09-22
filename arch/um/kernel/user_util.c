/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <setjmp.h>
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
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "mem_user.h"
#include "init.h"
#include "helper.h"
#include "ptrace_user.h"
#include "uml-config.h"

void stop(void)
{
	while(1) sleep(1000000);
}

void stack_protections(unsigned long address)
{
	int prot = PROT_READ | PROT_WRITE | PROT_EXEC;

        if(mprotect((void *) address, page_size(), prot) < 0)
		panic("protecting stack failed, errno = %d", errno);
}

void task_protections(unsigned long address)
{
	unsigned long guard = address + page_size();
	unsigned long stack = guard + page_size();
	int prot = 0, pages;

#ifdef notdef
	if(mprotect((void *) stack, page_size(), prot) < 0)
		panic("protecting guard page failed, errno = %d", errno);
#endif
	pages = (1 << UML_CONFIG_KERNEL_STACK_ORDER) - 2;
	prot = PROT_READ | PROT_WRITE | PROT_EXEC;
	if(mprotect((void *) stack, pages * page_size(), prot) < 0)
		panic("protecting stack failed, errno = %d", errno);
}

int wait_for_stop(int pid, int sig, int cont_type, void *relay)
{
	sigset_t *relay_signals = relay;
	int status, ret;

	while(1){
		CATCH_EINTR(ret = waitpid(pid, &status, WUNTRACED));
		if((ret < 0) ||
		   !WIFSTOPPED(status) || (WSTOPSIG(status) != sig)){
			if(ret < 0){
				printk("wait failed, errno = %d\n",
				       errno);
			}
			else if(WIFEXITED(status)) 
				printk("process %d exited with status %d\n",
				       pid, WEXITSTATUS(status));
			else if(WIFSIGNALED(status))
				printk("process %d exited with signal %d\n",
				       pid, WTERMSIG(status));
			else if((WSTOPSIG(status) == SIGVTALRM) ||
				(WSTOPSIG(status) == SIGALRM) ||
				(WSTOPSIG(status) == SIGIO) ||
				(WSTOPSIG(status) == SIGPROF) ||
				(WSTOPSIG(status) == SIGCHLD) ||
				(WSTOPSIG(status) == SIGWINCH) ||
				(WSTOPSIG(status) == SIGINT)){
				ptrace(cont_type, pid, 0, WSTOPSIG(status));
				continue;
			}
			else if((relay_signals != NULL) &&
				sigismember(relay_signals, WSTOPSIG(status))){
				ptrace(cont_type, pid, 0, WSTOPSIG(status));
				continue;
			}
			else printk("process %d stopped with signal %d\n",
				    pid, WSTOPSIG(status));
			panic("wait_for_stop failed to wait for %d to stop "
			      "with %d\n", pid, sig);
		}
		return(status);
	}
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
	return(0);
}

void setup_machinename(char *machine_out)
{
	struct utsname host;

	uname(&host);
#if defined(UML_CONFIG_UML_X86) && !defined(UML_CONFIG_64BIT)
	if (!strcmp(host.machine, "x86_64")) {
		strcpy(machine_out, "i686");
		return;
	}
#endif
	strcpy(machine_out, host.machine);
}

char host_info[(_UTSNAME_LENGTH + 1) * 4 + _UTSNAME_NODENAME_LENGTH + 1];

void setup_hostinfo(void)
{
	struct utsname host;

	uname(&host);
	sprintf(host_info, "%s %s %s %s %s", host.sysname, host.nodename,
		host.release, host.version, host.machine);
}

int setjmp_wrapper(void (*proc)(void *, void *), ...)
{
        va_list args;
	sigjmp_buf buf;
	int n;

	n = sigsetjmp(buf, 1);
	if(n == 0){
		va_start(args, proc);
		(*proc)(&buf, &args);
	}
	va_end(args);
	return(n);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
