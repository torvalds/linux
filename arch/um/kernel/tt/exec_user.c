/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include "kern_util.h"
#include "user.h"
#include "ptrace_user.h"
#include "os.h"

void do_exec(int old_pid, int new_pid)
{
	unsigned long regs[FRAME_SIZE];
	int err;

	if((ptrace(PTRACE_ATTACH, new_pid, 0, 0) < 0) ||
	   (ptrace(PTRACE_CONT, new_pid, 0, 0) < 0))
		tracer_panic("do_exec failed to attach proc - errno = %d",
			     errno);

	CATCH_EINTR(err = waitpid(new_pid, 0, WUNTRACED));
	if (err < 0)
		tracer_panic("do_exec failed to attach proc in waitpid - errno = %d",
			     errno);

	if(ptrace_getregs(old_pid, regs) < 0)
		tracer_panic("do_exec failed to get registers - errno = %d",
			     errno);

	os_kill_ptraced_process(old_pid, 0);

	if (ptrace(PTRACE_OLDSETOPTIONS, new_pid, 0, (void *)PTRACE_O_TRACESYSGOOD) < 0)
		tracer_panic("do_exec: PTRACE_SETOPTIONS failed, errno = %d", errno);

	if(ptrace_setregs(new_pid, regs) < 0)
		tracer_panic("do_exec failed to start new proc - errno = %d",
			     errno);
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
