/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "user.h"
#include "sysdep/ptrace.h"
#include "sigcontext.h"
#include "sysdep/sigcontext.h"
#include "os.h"
#include "signal_user.h"
#include "user_util.h"
#include "mem_user.h"
#include "process.h"
#include "kern_util.h"
#include "chan_user.h"
#include "ptrace_user.h"
#include "irq_user.h"
#include "mode.h"
#include "tt.h"

static int tracer_winch[2];

int is_tracer_winch(int pid, int fd, void *data)
{
	if(pid != os_getpgrp())
		return(0);

	register_winch_irq(tracer_winch[0], fd, -1, data);
	return(1);
}

static void tracer_winch_handler(int sig)
{
	int n;
	char c = 1;

	n = os_write_file(tracer_winch[1], &c, sizeof(c));
	if(n != sizeof(c))
		printk("tracer_winch_handler - write failed, err = %d\n", -n);
}

/* Called only by the tracing thread during initialization */

static void setup_tracer_winch(void)
{
	int err;

	err = os_pipe(tracer_winch, 1, 1);
	if(err < 0){
		printk("setup_tracer_winch : os_pipe failed, err = %d\n", -err);
		return;
	}
	signal(SIGWINCH, tracer_winch_handler);
}

void attach_process(int pid)
{
	if((ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) ||
	   (ptrace(PTRACE_CONT, pid, 0, 0) < 0))
		tracer_panic("OP_FORK failed to attach pid");
	wait_for_stop(pid, SIGSTOP, PTRACE_CONT, NULL);
	if (ptrace(PTRACE_OLDSETOPTIONS, pid, 0, (void *)PTRACE_O_TRACESYSGOOD) < 0)
		tracer_panic("OP_FORK: PTRACE_SETOPTIONS failed, errno = %d", errno);
	if(ptrace(PTRACE_CONT, pid, 0, 0) < 0)
		tracer_panic("OP_FORK failed to continue process");
}

void tracer_panic(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n");
	while(1) pause();
}

static void tracer_segv(int sig, struct sigcontext sc)
{
        struct faultinfo fi;
        GET_FAULTINFO_FROM_SC(fi, &sc);
	printf("Tracing thread segfault at address 0x%lx, ip 0x%lx\n",
               FAULT_ADDRESS(fi), SC_IP(&sc));
	while(1)
		pause();
}

/* Changed early in boot, and then only read */
int debug = 0;
int debug_stop = 1;
int debug_parent = 0;
int honeypot = 0;

static int signal_tramp(void *arg)
{
	int (*proc)(void *);

	if(honeypot && munmap((void *) (host_task_size - 0x10000000),
			      0x10000000)) 
		panic("Unmapping stack failed");
	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
		panic("ptrace PTRACE_TRACEME failed");
	os_stop_process(os_getpid());
	change_sig(SIGWINCH, 0);
	signal(SIGUSR1, SIG_IGN);
	change_sig(SIGCHLD, 0);
	signal(SIGSEGV, (__sighandler_t) sig_handler);
	set_cmdline("(idle thread)");
	set_init_pid(os_getpid());
	init_irq_signals(0);
	proc = arg;
	return((*proc)(NULL));
}

static void sleeping_process_signal(int pid, int sig)
{
	switch(sig){
	/* These two result from UML being ^Z-ed and bg-ed.  PTRACE_CONT is
	 * right because the process must be in the kernel already.
	 */
	case SIGCONT:
	case SIGTSTP:
		if(ptrace(PTRACE_CONT, pid, 0, sig) < 0)
			tracer_panic("sleeping_process_signal : Failed to "
				     "continue pid %d, signal = %d, "
				     "errno = %d\n", pid, sig, errno);
		break;

	/* This happens when the debugger (e.g. strace) is doing system call 
	 * tracing on the kernel.  During a context switch, the current task
	 * will be set to the incoming process and the outgoing process will
	 * hop into write and then read.  Since it's not the current process
	 * any more, the trace of those will land here.  So, we need to just 
	 * PTRACE_SYSCALL it.
	 */
	case (SIGTRAP + 0x80):
		if(ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0)
			tracer_panic("sleeping_process_signal : Failed to "
				     "PTRACE_SYSCALL pid %d, errno = %d\n",
				     pid, errno);
		break;
	case SIGSTOP:
		break;
	default:
		tracer_panic("sleeping process %d got unexpected "
			     "signal : %d\n", pid, sig);
		break;
	}
}

/* Accessed only by the tracing thread */
int debugger_pid = -1;
int debugger_parent = -1;
int debugger_fd = -1;
int gdb_pid = -1;

struct {
	int pid;
	int signal;
	unsigned long addr;
	struct timeval time;
} signal_record[1024][32];

int signal_index[32];
int nsignals = 0;
int debug_trace = 0;
extern int io_nsignals, io_count, intr_count;

extern void signal_usr1(int sig);

int tracing_pid = -1;

int tracer(int (*init_proc)(void *), void *sp)
{
	void *task = NULL;
	int status, pid = 0, sig = 0, cont_type, tracing = 0, op = 0;
	int proc_id = 0, n, err, old_tracing = 0, strace = 0;
	int local_using_sysemu = 0;
#ifdef UML_CONFIG_SYSCALL_DEBUG
	unsigned long eip = 0;
	int last_index;
#endif
	signal(SIGPIPE, SIG_IGN);
	setup_tracer_winch();
	tracing_pid = os_getpid();
	printf("tracing thread pid = %d\n", tracing_pid);

	pid = clone(signal_tramp, sp, CLONE_FILES | SIGCHLD, init_proc);
	CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
	if(n < 0){
		printf("waitpid on idle thread failed, errno = %d\n", errno);
		exit(1);
	}
	if (ptrace(PTRACE_OLDSETOPTIONS, pid, 0, (void *)PTRACE_O_TRACESYSGOOD) < 0) {
		printf("Failed to PTRACE_SETOPTIONS for idle thread, errno = %d\n", errno);
		exit(1);
	}
	if((ptrace(PTRACE_CONT, pid, 0, 0) < 0)){
		printf("Failed to continue idle thread, errno = %d\n", errno);
		exit(1);
	}

	signal(SIGSEGV, (sighandler_t) tracer_segv);
	signal(SIGUSR1, signal_usr1);
	if(debug_trace){
		printf("Tracing thread pausing to be attached\n");
		stop();
	}
	if(debug){
		if(gdb_pid != -1) 
			debugger_pid = attach_debugger(pid, gdb_pid, 1);
		else debugger_pid = init_ptrace_proxy(pid, 1, debug_stop);
		if(debug_parent){
			debugger_parent = os_process_parent(debugger_pid);
			init_parent_proxy(debugger_parent);
			err = attach(debugger_parent);
			if(err){
				printf("Failed to attach debugger parent %d, "
				       "errno = %d\n", debugger_parent, -err);
				debugger_parent = -1;
			}
			else {
				if(ptrace(PTRACE_SYSCALL, debugger_parent, 
					  0, 0) < 0){
					printf("Failed to continue debugger "
					       "parent, errno = %d\n", errno);
					debugger_parent = -1;
				}
			}
		}
	}
	set_cmdline("(tracing thread)");
	while(1){
		CATCH_EINTR(pid = waitpid(-1, &status, WUNTRACED));
		if(pid <= 0){
			if(errno != ECHILD){
				printf("wait failed - errno = %d\n", errno);
			}
			continue;
		}
		if(pid == debugger_pid){
			int cont = 0;

			if(WIFEXITED(status) || WIFSIGNALED(status))
				debugger_pid = -1;
			/* XXX Figure out how to deal with gdb and SMP */
			else cont = debugger_signal(status, cpu_tasks[0].pid);
			if(cont == PTRACE_SYSCALL) strace = 1;
			continue;
		}
		else if(pid == debugger_parent){
			debugger_parent_signal(status, pid);
			continue;
		}
		nsignals++;
		if(WIFEXITED(status)) ;
#ifdef notdef
		{
			printf("Child %d exited with status %d\n", pid, 
			       WEXITSTATUS(status));
		}
#endif
		else if(WIFSIGNALED(status)){
			sig = WTERMSIG(status);
			if(sig != 9){
				printf("Child %d exited with signal %d\n", pid,
				       sig);
			}
		}
		else if(WIFSTOPPED(status)){
			proc_id = pid_to_processor_id(pid);
			sig = WSTOPSIG(status);
#ifdef UML_CONFIG_SYSCALL_DEBUG
			if(signal_index[proc_id] == 1024){
				signal_index[proc_id] = 0;
				last_index = 1023;
			}
			else last_index = signal_index[proc_id] - 1;
			if(((sig == SIGPROF) || (sig == SIGVTALRM) ||
			    (sig == SIGALRM)) &&
			   (signal_record[proc_id][last_index].signal == sig)&&
			   (signal_record[proc_id][last_index].pid == pid))
				signal_index[proc_id] = last_index;
			signal_record[proc_id][signal_index[proc_id]].pid = pid;
			gettimeofday(&signal_record[proc_id][signal_index[proc_id]].time, NULL);
			eip = ptrace(PTRACE_PEEKUSR, pid, PT_IP_OFFSET, 0);
			signal_record[proc_id][signal_index[proc_id]].addr = eip;
			signal_record[proc_id][signal_index[proc_id]++].signal = sig;
#endif
			if(proc_id == -1){
				sleeping_process_signal(pid, sig);
				continue;
			}

			task = cpu_tasks[proc_id].task;
			tracing = is_tracing(task);
			old_tracing = tracing;

			/* Assume: no syscall, when coming from user */
			if ( tracing )
				do_sigtrap(task);

			switch(sig){
			case SIGUSR1:
				sig = 0;
				op = do_proc_op(task, proc_id);
				switch(op){
				/*
				 * This is called when entering user mode; after
				 * this, we start intercepting syscalls.
				 *
				 * In fact, a process is started in kernel mode,
				 * so with is_tracing() == 0 (and that is reset
				 * when executing syscalls, since UML kernel has
				 * the right to do syscalls);
				 */
				case OP_TRACE_ON:
					arch_leave_kernel(task, pid);
					tracing = 1;
					break;
				case OP_REBOOT:
				case OP_HALT:
					unmap_physmem();
					kmalloc_ok = 0;
					os_kill_ptraced_process(pid, 0);
					/* Now let's reap remaining zombies */
					errno = 0;
					do {
						waitpid(-1, &status,
							WUNTRACED);
					} while (errno != ECHILD);
					return(op == OP_REBOOT);
				case OP_NONE:
					printf("Detaching pid %d\n", pid);
					detach(pid, SIGSTOP);
					continue;
				default:
					break;
				}
				/* OP_EXEC switches host processes on us,
				 * we want to continue the new one.
				 */
				pid = cpu_tasks[proc_id].pid;
				break;
			case (SIGTRAP + 0x80):
				if(!tracing && (debugger_pid != -1)){
					child_signal(pid, status & 0x7fff);
					continue;
				}
				tracing = 0;
				/* local_using_sysemu has been already set
				 * below, since if we are here, is_tracing() on
				 * the traced task was 1, i.e. the process had
				 * already run through one iteration of the
				 * loop which executed a OP_TRACE_ON request.*/
				do_syscall(task, pid, local_using_sysemu);
				sig = SIGUSR2;
				break;
			case SIGTRAP:
				if(!tracing && (debugger_pid != -1)){
					child_signal(pid, status);
					continue;
				}
				tracing = 0;
				break;
			case SIGPROF:
				if(tracing) sig = 0;
				break;
			case SIGCHLD:
			case SIGHUP:
				sig = 0;
				break;
			case SIGSEGV:
			case SIGIO:
			case SIGALRM:
			case SIGVTALRM:
			case SIGFPE:
			case SIGBUS:
			case SIGILL:
			case SIGWINCH:

			default:
				tracing = 0;
				break;
			}
			set_tracing(task, tracing);

			if(!tracing && old_tracing)
				arch_enter_kernel(task, pid);

			if(!tracing && (debugger_pid != -1) && (sig != 0) &&
				(sig != SIGALRM) && (sig != SIGVTALRM) &&
				(sig != SIGSEGV) && (sig != SIGTRAP) &&
				(sig != SIGUSR2) && (sig != SIGIO) &&
				(sig != SIGFPE)){
				child_signal(pid, status);
				continue;
			}

			local_using_sysemu = get_using_sysemu();

			if(tracing)
				cont_type = SELECT_PTRACE_OPERATION(local_using_sysemu,
				                                    singlestepping(task));
			else if((debugger_pid != -1) && strace)
				cont_type = PTRACE_SYSCALL;
			else
				cont_type = PTRACE_CONT;

			if(ptrace(cont_type, pid, 0, sig) != 0){
				tracer_panic("ptrace failed to continue "
					     "process - errno = %d\n", 
					     errno);
			}
		}
	}
	return(0);
}

static int __init uml_debug_setup(char *line, int *add)
{
	char *next;

	debug = 1;
	*add = 0;
	if(*line != '=') return(0);
	line++;

	while(line != NULL){
		next = strchr(line, ',');
		if(next) *next++ = '\0';
		
		if(!strcmp(line, "go"))	debug_stop = 0;
		else if(!strcmp(line, "parent")) debug_parent = 1;
		else printf("Unknown debug option : '%s'\n", line);

		line = next;
	}
	return(0);
}

__uml_setup("debug", uml_debug_setup,
"debug\n"
"    Starts up the kernel under the control of gdb. See the \n"
"    kernel debugging tutorial and the debugging session pages\n"
"    at http://user-mode-linux.sourceforge.net/ for more information.\n\n"
);

static int __init uml_debugtrace_setup(char *line, int *add)
{
	debug_trace = 1;
	return 0;
}
__uml_setup("debugtrace", uml_debugtrace_setup,
"debugtrace\n"
"    Causes the tracing thread to pause until it is attached by a\n"
"    debugger and continued.  This is mostly for debugging crashes\n"
"    early during boot, and should be pretty much obsoleted by\n"
"    the debug switch.\n\n"
);

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
