/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include "ptrace_user.h"
#include "uml-config.h"
#include "kern_constants.h"
#include "chan_user.h"
#include "init.h"
#include "user.h"
#include "debug.h"
#include "kern_util.h"
#include "tt.h"
#include "sysdep/thread.h"
#include "os.h"

extern int debugger_pid;
extern int debugger_fd;
extern int debugger_parent;

int detach(int pid, int sig)
{
	return(ptrace(PTRACE_DETACH, pid, 0, sig));
}

int attach(int pid)
{
	int err;

	err = ptrace(PTRACE_ATTACH, pid, 0, 0);
	if(err < 0) return(-errno);
	else return(err);
}

int cont(int pid)
{
	return(ptrace(PTRACE_CONT, pid, 0, 0));
}

#ifdef UML_CONFIG_PT_PROXY

int debugger_signal(int status, pid_t pid)
{
	return(debugger_proxy(status, pid));
}

void child_signal(pid_t pid, int status)
{
	child_proxy(pid, status);
}

static void gdb_announce(char *dev_name, int dev)
{
	printf("gdb assigned device '%s'\n", dev_name);
}

static struct chan_opts opts = {
	.announce  	= gdb_announce,
	.xterm_title 	= "UML kernel debugger",
	.raw 		= 0,
	.tramp_stack 	= 0,
	.in_kernel  	= 0,
};

/* Accessed by the tracing thread, which automatically serializes access */
static void *xterm_data;
static int xterm_fd;

extern void *xterm_init(char *, int, struct chan_opts *);
extern int xterm_open(int, int, int, void *, char **);
extern void xterm_close(int, void *);

int open_gdb_chan(void)
{
	char stack[UM_KERN_PAGE_SIZE], *dummy;

	opts.tramp_stack = (unsigned long) stack;
	xterm_data = xterm_init("", 0, &opts);
	xterm_fd = xterm_open(1, 1, 1, xterm_data, &dummy);
	return(xterm_fd);
}

static void exit_debugger_cb(void *unused)
{
	if(debugger_pid != -1){
		if(gdb_pid != -1){
			fake_child_exit();
			gdb_pid = -1;
		}
		else kill_child_dead(debugger_pid);
		debugger_pid = -1;
		if(debugger_parent != -1)
			detach(debugger_parent, SIGINT);
	}
	if(xterm_data != NULL) xterm_close(xterm_fd, xterm_data);
}

static void exit_debugger(void)
{
	initial_thread_cb(exit_debugger_cb, NULL);
}

__uml_exitcall(exit_debugger);

struct gdb_data {
	char *str;
	int err;
};

extern char *linux_prog;

static void config_gdb_cb(void *arg)
{
	struct gdb_data *data = arg;
	void *task;
	int pid;

	data->err = -1;
	if(debugger_pid != -1) exit_debugger_cb(NULL);
	if(!strncmp(data->str, "pid,", strlen("pid,"))){
		data->str += strlen("pid,");
		pid = strtoul(data->str, NULL, 0);
		task = cpu_tasks[0].task;
		debugger_pid = attach_debugger(TASK_EXTERN_PID(task), pid, 0);
		if(debugger_pid != -1){
			data->err = 0;
			gdb_pid = pid;
		}
		return;
	}
	data->err = 0;
	debugger_pid = start_debugger(linux_prog, 0, 0, &debugger_fd);
	init_proxy(debugger_pid, 0, 0);
}

int gdb_config(char *str, char **error_out)
{
	struct gdb_data data;

	if(*str++ != '=') return(-1);
	data.str = str;
	initial_thread_cb(config_gdb_cb, &data);
	return(data.err);
}

void remove_gdb_cb(void *unused)
{
	exit_debugger_cb(NULL);
}

int gdb_remove(int unused, char **error_out)
{
	initial_thread_cb(remove_gdb_cb, NULL);
        return 0;
}

void signal_usr1(int sig)
{
	if(debugger_pid != -1){
		printf("The debugger is already running\n");
		return;
	}
	debugger_pid = start_debugger(linux_prog, 0, 0, &debugger_fd);
	init_proxy(debugger_pid, 0, 0);
}

int init_ptrace_proxy(int idle_pid, int startup, int stop)
{
	int pid, status;

	pid = start_debugger(linux_prog, startup, stop, &debugger_fd);
	status = wait_for_stop(idle_pid, SIGSTOP, PTRACE_CONT, NULL);
 	if(pid < 0){
		cont(idle_pid);
		return(-1);
	}
	init_proxy(pid, 1, status);
	return(pid);
}

int attach_debugger(int idle_pid, int pid, int stop)
{
	int status = 0, err;

	err = attach(pid);
	if(err < 0){
		printf("Failed to attach pid %d, errno = %d\n", pid, -err);
		return(-1);
	}
	if(stop) status = wait_for_stop(idle_pid, SIGSTOP, PTRACE_CONT, NULL);
	init_proxy(pid, 1, status);
	return(pid);
}

#ifdef notdef /* Put this back in when it does something useful */
static int __init uml_gdb_init_setup(char *line, int *add)
{
	gdb_init = uml_strdup(line);
	return 0;
}

__uml_setup("gdb=", uml_gdb_init_setup, 
"gdb=<channel description>\n\n"
);
#endif

static int __init uml_gdb_pid_setup(char *line, int *add)
{
	gdb_pid = strtoul(line, NULL, 0);
	*add = 0;
	return 0;
}

__uml_setup("gdb-pid=", uml_gdb_pid_setup, 
"gdb-pid=<pid>\n"
"    gdb-pid is used to attach an external debugger to UML.  This may be\n"
"    an already-running gdb or a debugger-like process like strace.\n\n"
);

#else

int debugger_signal(int status, pid_t pid){ return(0); }
void child_signal(pid_t pid, int status){ }
int init_ptrace_proxy(int idle_pid, int startup, int stop)
{
	printf("debug requested when CONFIG_PT_PROXY is off\n");
	kill_child_dead(idle_pid);
	exit(1);
}

void signal_usr1(int sig)
{
	printf("debug requested when CONFIG_PT_PROXY is off\n");
}

int attach_debugger(int idle_pid, int pid, int stop)
{
	printf("attach_debugger called when CONFIG_PT_PROXY "
	       "is off\n");
	return(-1);
}

int config_gdb(char *str)
{
	return(-1);
}

int remove_gdb(void)
{
	return(-1);
}

int init_parent_proxy(int pid)
{
	return(-1);
}

void debugger_parent_signal(int status, int pid)
{
}

#endif

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
