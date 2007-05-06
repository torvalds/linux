/**********************************************************************
proxy.c

Copyright (C) 1999 Lars Brinkhoff.  See the file COPYING for licensing
terms and conditions.

Jeff Dike (jdike@karaya.com) : Modified for integration into uml
**********************************************************************/

/* XXX This file shouldn't refer to CONFIG_* */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include "ptrace_user.h"

#include "ptproxy.h"
#include "sysdep.h"
#include "wait.h"

#include "user.h"
#include "os.h"
#include "tempfile.h"

static int debugger_wait(debugger_state *debugger, int *status, int options,
			 int (*syscall)(debugger_state *debugger, pid_t child),
			 int (*normal_return)(debugger_state *debugger, 
					      pid_t unused),
			 int (*wait_return)(debugger_state *debugger, 
					    pid_t unused))
{
	if(debugger->real_wait){
		debugger->handle_trace = normal_return;
		syscall_continue(debugger->pid);
		debugger->real_wait = 0;
		return(1);
	}
	debugger->wait_status_ptr = status;
	debugger->wait_options = options;
	if((debugger->debugee != NULL) && debugger->debugee->event){
		syscall_continue(debugger->pid);
		wait_for_stop(debugger->pid, SIGTRAP, PTRACE_SYSCALL,
			      NULL);
		(*wait_return)(debugger, -1);
		return(0);
	}
	else if(debugger->wait_options & WNOHANG){
		syscall_cancel(debugger->pid, 0);
		debugger->handle_trace = syscall;
		return(0);
	}
	else {
		syscall_pause(debugger->pid);
		debugger->handle_trace = wait_return;
		debugger->waiting = 1;
	}
	return(1);
}

/*
 * Handle debugger trap, i.e. syscall.
 */

int debugger_syscall(debugger_state *debugger, pid_t child)
{
	long arg1, arg2, arg3, arg4, arg5, result;
	int syscall, ret = 0;

	syscall = get_syscall(debugger->pid, &arg1, &arg2, &arg3, &arg4, 
			      &arg5);

	switch(syscall){
	case __NR_execve:
		/* execve never returns */
		debugger->handle_trace = debugger_syscall; 
		break;

	case __NR_ptrace:
		if(debugger->debugee->pid != 0) arg2 = debugger->debugee->pid;
		if(!debugger->debugee->in_context) 
			child = debugger->debugee->pid;
		result = proxy_ptrace(debugger, arg1, arg2, arg3, arg4, child,
				      &ret);
		syscall_cancel(debugger->pid, result);
		debugger->handle_trace = debugger_syscall;
		return(ret);

#ifdef __NR_waitpid
	case __NR_waitpid:
#endif
	case __NR_wait4:
		if(!debugger_wait(debugger, (int *) arg2, arg3, 
				  debugger_syscall, debugger_normal_return, 
				  proxy_wait_return))
			return(0);
		break;

	case __NR_kill:
		if(!debugger->debugee->in_context) 
			child = debugger->debugee->pid;
		if(arg1 == debugger->debugee->pid){
			result = kill(child, arg2);
			syscall_cancel(debugger->pid, result);
			debugger->handle_trace = debugger_syscall;
			return(0);
		}
		else debugger->handle_trace = debugger_normal_return;
		break;

	default:
		debugger->handle_trace = debugger_normal_return;
	}

	syscall_continue(debugger->pid);
	return(0);
}

/* Used by the tracing thread */
static debugger_state parent;
static int parent_syscall(debugger_state *debugger, int pid);

int init_parent_proxy(int pid)
{
	parent = ((debugger_state) { .pid 		= pid,
				     .wait_options 	= 0,
				     .wait_status_ptr 	= NULL,
				     .waiting 		= 0,
				     .real_wait 	= 0,
				     .expecting_child 	= 0,
				     .handle_trace  	= parent_syscall,
				     .debugee 		= NULL } );
	return(0);
}

int parent_normal_return(debugger_state *debugger, pid_t unused)
{
	debugger->handle_trace = parent_syscall;
	syscall_continue(debugger->pid);
	return(0);
}

static int parent_syscall(debugger_state *debugger, int pid)
{
	long arg1, arg2, arg3, arg4, arg5;
	int syscall;

	syscall = get_syscall(pid, &arg1, &arg2, &arg3, &arg4, &arg5);
		
	if((syscall == __NR_wait4)
#ifdef __NR_waitpid
	   || (syscall == __NR_waitpid)
#endif
	){
		debugger_wait(&parent, (int *) arg2, arg3, parent_syscall,
			      parent_normal_return, parent_wait_return);
	}
	else ptrace(PTRACE_SYSCALL, pid, 0, 0);
	return(0);
}

int debugger_normal_return(debugger_state *debugger, pid_t unused)
{
	debugger->handle_trace = debugger_syscall;
	syscall_continue(debugger->pid);
	return(0);
}

void debugger_cancelled_return(debugger_state *debugger, int result)
{
	debugger->handle_trace = debugger_syscall;
	syscall_set_result(debugger->pid, result);
	syscall_continue(debugger->pid);
}

/* Used by the tracing thread */
static debugger_state debugger;
static debugee_state debugee;

void init_proxy (pid_t debugger_pid, int stopped, int status)
{
	debugger.pid = debugger_pid;
	debugger.handle_trace = debugger_syscall;
	debugger.debugee = &debugee;
	debugger.waiting = 0;
	debugger.real_wait = 0;
	debugger.expecting_child = 0;

	debugee.pid = 0;
	debugee.traced = 0;
	debugee.stopped = stopped;
	debugee.event = 0;
	debugee.zombie = 0;
	debugee.died = 0;
	debugee.wait_status = status;
	debugee.in_context = 1;
}

int debugger_proxy(int status, int pid)
{
	int ret = 0, sig;

	if(WIFSTOPPED(status)){
		sig = WSTOPSIG(status);
		if (sig == SIGTRAP)
			ret = (*debugger.handle_trace)(&debugger, pid);
						       
		else if(sig == SIGCHLD){
			if(debugger.expecting_child){
				ptrace(PTRACE_SYSCALL, debugger.pid, 0, sig);
				debugger.expecting_child = 0;
			}
			else if(debugger.waiting)
				real_wait_return(&debugger);
			else {
				ptrace(PTRACE_SYSCALL, debugger.pid, 0, sig);
				debugger.real_wait = 1;
			}
		}
		else ptrace(PTRACE_SYSCALL, debugger.pid, 0, sig);
	}
	else if(WIFEXITED(status)){
		tracer_panic("debugger (pid %d) exited with status %d", 
			     debugger.pid, WEXITSTATUS(status));
	}
	else if(WIFSIGNALED(status)){
		tracer_panic("debugger (pid %d) exited with signal %d", 
			     debugger.pid, WTERMSIG(status));
	}
	else {
		tracer_panic("proxy got unknown status (0x%x) on debugger "
			     "(pid %d)", status, debugger.pid);
	}
	return(ret);
}

void child_proxy(pid_t pid, int status)
{
	debugee.event = 1;
	debugee.wait_status = status;

	if(WIFSTOPPED(status)){
		debugee.stopped = 1;
		debugger.expecting_child = 1;
		kill(debugger.pid, SIGCHLD);
	}
	else if(WIFEXITED(status) || WIFSIGNALED(status)){
		debugee.zombie = 1;
		debugger.expecting_child = 1;
		kill(debugger.pid, SIGCHLD);
	}
	else panic("proxy got unknown status (0x%x) on child (pid %d)", 
		   status, pid);
}

void debugger_parent_signal(int status, int pid)
{
	int sig;

	if(WIFSTOPPED(status)){
		sig = WSTOPSIG(status);
		if(sig == SIGTRAP) (*parent.handle_trace)(&parent, pid);
		else ptrace(PTRACE_SYSCALL, pid, 0, sig);
	}
}

void fake_child_exit(void)
{
	int status, pid;

	child_proxy(1, W_EXITCODE(0, 0));
	while(debugger.waiting == 1){
		CATCH_EINTR(pid = waitpid(debugger.pid, &status, WUNTRACED));
		if(pid != debugger.pid){
			printk("fake_child_exit - waitpid failed, "
			       "errno = %d\n", errno);
			return;
		}
		debugger_proxy(status, debugger.pid);
	}
	CATCH_EINTR(pid = waitpid(debugger.pid, &status, WUNTRACED));
	if(pid != debugger.pid){
		printk("fake_child_exit - waitpid failed, "
		       "errno = %d\n", errno);
		return;
	}
	if(ptrace(PTRACE_DETACH, debugger.pid, 0, SIGCONT) < 0)
		printk("fake_child_exit - PTRACE_DETACH failed, errno = %d\n",
		       errno);
}

char gdb_init_string[] = 
"att 1 \n\
b panic \n\
b stop \n\
handle SIGWINCH nostop noprint pass \n\
";

int start_debugger(char *prog, int startup, int stop, int *fd_out)
{
	int slave, child;

	slave = open_gdb_chan();
	child = fork();
	if(child == 0){
		char *tempname = NULL;
		int fd;

	        if(setsid() < 0) perror("setsid");
		if((dup2(slave, 0) < 0) || (dup2(slave, 1) < 0) || 
		   (dup2(slave, 2) < 0)){
			printk("start_debugger : dup2 failed, errno = %d\n",
			       errno);
			exit(1);
		}
		if(ioctl(0, TIOCSCTTY, 0) < 0){
			printk("start_debugger : TIOCSCTTY failed, "
			       "errno = %d\n", errno);
			exit(1);
		}
		if(tcsetpgrp (1, os_getpid()) < 0){
			printk("start_debugger : tcsetpgrp failed, "
			       "errno = %d\n", errno);
#ifdef notdef
			exit(1);
#endif
		}
		fd = make_tempfile("/tmp/gdb_init-XXXXXX", &tempname, 0);
		if(fd < 0){
			printk("start_debugger : make_tempfile failed,"
			       "err = %d\n", -fd);
			exit(1);
		}
		os_write_file(fd, gdb_init_string, sizeof(gdb_init_string) - 1);
		if(startup){
			if(stop){
				os_write_file(fd, "b start_kernel\n",
				      strlen("b start_kernel\n"));
			}
			os_write_file(fd, "c\n", strlen("c\n"));
		}
		if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0){
			printk("start_debugger :  PTRACE_TRACEME failed, "
			       "errno = %d\n", errno);
			exit(1);
		}
		execlp("gdb", "gdb", "--command", tempname, prog, NULL);
		printk("start_debugger : exec of gdb failed, errno = %d\n",
		       errno);
	}
	if(child < 0){
		printk("start_debugger : fork for gdb failed, errno = %d\n",
		       errno);
		return(-1);
	}
	*fd_out = slave;
	return(child);
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
