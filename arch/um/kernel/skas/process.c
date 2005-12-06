/* 
 * Copyright (C) 2002- 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/time.h>
#include <asm/unistd.h>
#include <asm/types.h>
#include "user.h"
#include "ptrace_user.h"
#include "time_user.h"
#include "sysdep/ptrace.h"
#include "user_util.h"
#include "kern_util.h"
#include "skas.h"
#include "stub-data.h"
#include "mm_id.h"
#include "sysdep/sigcontext.h"
#include "sysdep/stub.h"
#include "os.h"
#include "proc_mm.h"
#include "skas_ptrace.h"
#include "chan_user.h"
#include "signal_user.h"
#include "registers.h"
#include "mem.h"
#include "uml-config.h"
#include "process.h"

int is_skas_winch(int pid, int fd, void *data)
{
        if(pid != os_getpgrp())
		return(0);

	register_winch_irq(-1, fd, -1, data);
	return(1);
}

void wait_stub_done(int pid, int sig, char * fname)
{
        int n, status, err;

        do {
                if ( sig != -1 ) {
                        err = ptrace(PTRACE_CONT, pid, 0, sig);
                        if(err)
                                panic("%s : continue failed, errno = %d\n",
                                      fname, errno);
                }
                sig = 0;

                CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
        } while((n >= 0) && WIFSTOPPED(status) &&
                ((WSTOPSIG(status) == SIGVTALRM) ||
		 /* running UML inside a detached screen can cause
		  * SIGWINCHes
		  */
		 (WSTOPSIG(status) == SIGWINCH)));

        if((n < 0) || !WIFSTOPPED(status) ||
           (WSTOPSIG(status) != SIGUSR1 && WSTOPSIG(status) != SIGTRAP)){
		unsigned long regs[FRAME_SIZE];
		if(ptrace(PTRACE_GETREGS, pid, 0, regs) < 0)
			printk("Failed to get registers from stub, "
			       "errno = %d\n", errno);
		else {
			int i;

			printk("Stub registers -\n");
			for(i = 0; i < FRAME_SIZE; i++)
				printk("\t%d - %lx\n", i, regs[i]);
		}
                panic("%s : failed to wait for SIGUSR1/SIGTRAP, "
                      "pid = %d, n = %d, errno = %d, status = 0x%x\n",
                      fname, pid, n, errno, status);
        }
}

void get_skas_faultinfo(int pid, struct faultinfo * fi)
{
        int err;

        if(ptrace_faultinfo){
                err = ptrace(PTRACE_FAULTINFO, pid, 0, fi);
                if(err)
                        panic("get_skas_faultinfo - PTRACE_FAULTINFO failed, "
                              "errno = %d\n", errno);

                /* Special handling for i386, which has different structs */
                if (sizeof(struct ptrace_faultinfo) < sizeof(struct faultinfo))
                        memset((char *)fi + sizeof(struct ptrace_faultinfo), 0,
                               sizeof(struct faultinfo) -
                               sizeof(struct ptrace_faultinfo));
        }
        else {
                wait_stub_done(pid, SIGSEGV, "get_skas_faultinfo");

                /* faultinfo is prepared by the stub-segv-handler at start of
                 * the stub stack page. We just have to copy it.
                 */
                memcpy(fi, (void *)current_stub_stack(), sizeof(*fi));
        }
}

static void handle_segv(int pid, union uml_pt_regs * regs)
{
        get_skas_faultinfo(pid, &regs->skas.faultinfo);
        segv(regs->skas.faultinfo, 0, 1, NULL);
}

/*To use the same value of using_sysemu as the caller, ask it that value (in local_using_sysemu)*/
static void handle_trap(int pid, union uml_pt_regs *regs, int local_using_sysemu)
{
	int err, status;

	/* Mark this as a syscall */
	UPT_SYSCALL_NR(regs) = PT_SYSCALL_NR(regs->skas.regs);

	if (!local_using_sysemu)
	{
		err = ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_NR_OFFSET, __NR_getpid);
		if(err < 0)
			panic("handle_trap - nullifying syscall failed errno = %d\n",
			      errno);

		err = ptrace(PTRACE_SYSCALL, pid, 0, 0);
		if(err < 0)
			panic("handle_trap - continuing to end of syscall failed, "
			      "errno = %d\n", errno);

		CATCH_EINTR(err = waitpid(pid, &status, WUNTRACED));
		if((err < 0) || !WIFSTOPPED(status) ||
		   (WSTOPSIG(status) != SIGTRAP + 0x80))
			panic("handle_trap - failed to wait at end of syscall, "
			      "errno = %d, status = %d\n", errno, status);
	}

	handle_syscall(regs);
}

extern int __syscall_stub_start;
int stub_code_fd = -1;
__u64 stub_code_offset;

static int userspace_tramp(void *stack)
{
	void *addr;

	ptrace(PTRACE_TRACEME, 0, 0, 0);

	init_new_thread_signals(1);
	enable_timer();

	if(!proc_mm){
		/* This has a pte, but it can't be mapped in with the usual
		 * tlb_flush mechanism because this is part of that mechanism
		 */
		addr = mmap64((void *) UML_CONFIG_STUB_CODE, page_size(),
			      PROT_EXEC, MAP_FIXED | MAP_PRIVATE,
			      stub_code_fd, stub_code_offset);
		if(addr == MAP_FAILED){
			printk("mapping stub code failed, errno = %d\n",
			       errno);
			exit(1);
		}

		if(stack != NULL){
			int fd;
			__u64 offset;

			fd = phys_mapping(to_phys(stack), &offset);
			addr = mmap((void *) UML_CONFIG_STUB_DATA, page_size(),
				    PROT_READ | PROT_WRITE,
				    MAP_FIXED | MAP_SHARED, fd, offset);
			if(addr == MAP_FAILED){
				printk("mapping stub stack failed, "
				       "errno = %d\n", errno);
				exit(1);
			}
		}
	}
	if(!ptrace_faultinfo){
		unsigned long v = UML_CONFIG_STUB_CODE +
				  (unsigned long) stub_segv_handler -
				  (unsigned long) &__syscall_stub_start;

		set_sigstack((void *) UML_CONFIG_STUB_DATA, page_size());
		set_handler(SIGSEGV, (void *) v, SA_ONSTACK,
			    SIGIO, SIGWINCH, SIGALRM, SIGVTALRM,
			    SIGUSR1, -1);
	}

	os_stop_process(os_getpid());
	return(0);
}

/* Each element set once, and only accessed by a single processor anyway */
#undef NR_CPUS
#define NR_CPUS 1
int userspace_pid[NR_CPUS];

int start_userspace(unsigned long stub_stack)
{
	void *stack;
	unsigned long sp;
	int pid, status, n, flags;

	if ( stub_code_fd == -1 )
		stub_code_fd = phys_mapping(to_phys(&__syscall_stub_start),
					    &stub_code_offset);

	stack = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(stack == MAP_FAILED)
		panic("start_userspace : mmap failed, errno = %d", errno);
	sp = (unsigned long) stack + PAGE_SIZE - sizeof(void *);

	flags = CLONE_FILES | SIGCHLD;
	if(proc_mm) flags |= CLONE_VM;
	pid = clone(userspace_tramp, (void *) sp, flags, (void *) stub_stack);
	if(pid < 0)
		panic("start_userspace : clone failed, errno = %d", errno);

	do {
		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
		if(n < 0)
			panic("start_userspace : wait failed, errno = %d", 
			      errno);
	} while(WIFSTOPPED(status) && (WSTOPSIG(status) == SIGVTALRM));

	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP))
		panic("start_userspace : expected SIGSTOP, got status = %d",
		      status);

	if (ptrace(PTRACE_OLDSETOPTIONS, pid, NULL, (void *)PTRACE_O_TRACESYSGOOD) < 0)
		panic("start_userspace : PTRACE_SETOPTIONS failed, errno=%d\n",
		      errno);

	if(munmap(stack, PAGE_SIZE) < 0)
		panic("start_userspace : munmap failed, errno = %d\n", errno);

	return(pid);
}

void userspace(union uml_pt_regs *regs)
{
	int err, status, op, pid = userspace_pid[0];
	int local_using_sysemu; /*To prevent races if using_sysemu changes under us.*/

	while(1){
		restore_registers(pid, regs);

		/* Now we set local_using_sysemu to be used for one loop */
		local_using_sysemu = get_using_sysemu();

		op = SELECT_PTRACE_OPERATION(local_using_sysemu, singlestepping(NULL));

		err = ptrace(op, pid, 0, 0);
		if(err)
			panic("userspace - could not resume userspace process, "
			      "pid=%d, ptrace operation = %d, errno = %d\n",
			      op, errno);

		CATCH_EINTR(err = waitpid(pid, &status, WUNTRACED));
		if(err < 0)
			panic("userspace - waitpid failed, errno = %d\n", 
			      errno);

		regs->skas.is_user = 1;
		save_registers(pid, regs);
		UPT_SYSCALL_NR(regs) = -1; /* Assume: It's not a syscall */

		if(WIFSTOPPED(status)){
		  	switch(WSTOPSIG(status)){
			case SIGSEGV:
                                if(PTRACE_FULL_FAULTINFO || !ptrace_faultinfo)
                                        user_signal(SIGSEGV, regs, pid);
                                else handle_segv(pid, regs);
				break;
			case SIGTRAP + 0x80:
			        handle_trap(pid, regs, local_using_sysemu);
				break;
			case SIGTRAP:
				relay_signal(SIGTRAP, regs);
				break;
			case SIGIO:
			case SIGVTALRM:
			case SIGILL:
			case SIGBUS:
			case SIGFPE:
			case SIGWINCH:
                                user_signal(WSTOPSIG(status), regs, pid);
				break;
			default:
			        printk("userspace - child stopped with signal "
				       "%d\n", WSTOPSIG(status));
			}
			pid = userspace_pid[0];
			interrupt_end();

			/* Avoid -ERESTARTSYS handling in host */
			PT_SYSCALL_NR(regs->skas.regs) = -1;
		}
	}
}
#define INIT_JMP_NEW_THREAD 0
#define INIT_JMP_REMOVE_SIGSTACK 1
#define INIT_JMP_CALLBACK 2
#define INIT_JMP_HALT 3
#define INIT_JMP_REBOOT 4


int copy_context_skas0(unsigned long new_stack, int pid)
{
	int err;
	unsigned long regs[MAX_REG_NR];
	unsigned long current_stack = current_stub_stack();
	struct stub_data *data = (struct stub_data *) current_stack;
	struct stub_data *child_data = (struct stub_data *) new_stack;
	__u64 new_offset;
	int new_fd = phys_mapping(to_phys((void *)new_stack), &new_offset);

	/* prepare offset and fd of child's stack as argument for parent's
	 * and child's mmap2 calls
	 */
	*data = ((struct stub_data) { .offset	= MMAP_OFFSET(new_offset),
				      .fd	= new_fd,
				      .timer	= ((struct itimerval)
					           { { 0, 1000000 / hz() },
						     { 0, 1000000 / hz() }})});
	get_safe_registers(regs);

	/* Set parent's instruction pointer to start of clone-stub */
	regs[REGS_IP_INDEX] = UML_CONFIG_STUB_CODE +
				(unsigned long) stub_clone_handler -
				(unsigned long) &__syscall_stub_start;
	regs[REGS_SP_INDEX] = UML_CONFIG_STUB_DATA + PAGE_SIZE -
		sizeof(void *);
	err = ptrace_setregs(pid, regs);
	if(err < 0)
		panic("copy_context_skas0 : PTRACE_SETREGS failed, "
		      "pid = %d, errno = %d\n", pid, errno);

	/* set a well known return code for detection of child write failure */
	child_data->err = 12345678;

	/* Wait, until parent has finished its work: read child's pid from
	 * parent's stack, and check, if bad result.
	 */
	wait_stub_done(pid, 0, "copy_context_skas0");

	pid = data->err;
	if(pid < 0)
		panic("copy_context_skas0 - stub-parent reports error %d\n",
		      pid);

	/* Wait, until child has finished too: read child's result from
	 * child's stack and check it.
	 */
	wait_stub_done(pid, -1, "copy_context_skas0");
	if (child_data->err != UML_CONFIG_STUB_DATA)
		panic("copy_context_skas0 - stub-child reports error %d\n",
		      child_data->err);

	if (ptrace(PTRACE_OLDSETOPTIONS, pid, NULL,
		   (void *)PTRACE_O_TRACESYSGOOD) < 0)
		panic("copy_context_skas0 : PTRACE_SETOPTIONS failed, "
		      "errno = %d\n", errno);

	return pid;
}

/*
 * This is used only, if stub pages are needed, while proc_mm is
 * availabl. Opening /proc/mm creates a new mm_context, which lacks
 * the stub-pages. Thus, we map them using /proc/mm-fd
 */
void map_stub_pages(int fd, unsigned long code,
		    unsigned long data, unsigned long stack)
{
	struct proc_mm_op mmop;
	int n;

	mmop = ((struct proc_mm_op) { .op        = MM_MMAP,
				      .u         =
				      { .mmap    =
					{ .addr    = code,
					  .len     = PAGE_SIZE,
					  .prot    = PROT_EXEC,
					  .flags   = MAP_FIXED | MAP_PRIVATE,
					  .fd      = stub_code_fd,
					  .offset  = stub_code_offset
	} } });
	n = os_write_file(fd, &mmop, sizeof(mmop));
	if(n != sizeof(mmop))
		panic("map_stub_pages : /proc/mm map for code failed, "
		      "err = %d\n", -n);

	if ( stack ) {
		__u64 map_offset;
		int map_fd = phys_mapping(to_phys((void *)stack), &map_offset);
		mmop = ((struct proc_mm_op)
				{ .op        = MM_MMAP,
				  .u         =
				  { .mmap    =
				    { .addr    = data,
				      .len     = PAGE_SIZE,
				      .prot    = PROT_READ | PROT_WRITE,
				      .flags   = MAP_FIXED | MAP_SHARED,
				      .fd      = map_fd,
				      .offset  = map_offset
		} } });
		n = os_write_file(fd, &mmop, sizeof(mmop));
		if(n != sizeof(mmop))
			panic("map_stub_pages : /proc/mm map for data failed, "
			      "err = %d\n", -n);
	}
}

void new_thread(void *stack, void **switch_buf_ptr, void **fork_buf_ptr,
		void (*handler)(int))
{
	unsigned long flags;
	sigjmp_buf switch_buf, fork_buf;

	*switch_buf_ptr = &switch_buf;
	*fork_buf_ptr = &fork_buf;

	/* Somewhat subtle - siglongjmp restores the signal mask before doing
	 * the longjmp.  This means that when jumping from one stack to another
	 * when the target stack has interrupts enabled, an interrupt may occur
	 * on the source stack.  This is bad when starting up a process because
	 * it's not supposed to get timer ticks until it has been scheduled.
	 * So, we disable interrupts around the sigsetjmp to ensure that
	 * they can't happen until we get back here where they are safe.
	 */
	flags = get_signals();
	block_signals();
	if(sigsetjmp(fork_buf, 1) == 0)
		new_thread_proc(stack, handler);

	remove_sigstack();

	set_signals(flags);
}

void thread_wait(void *sw, void *fb)
{
	sigjmp_buf buf, **switch_buf = sw, *fork_buf;

	*switch_buf = &buf;
	fork_buf = fb;
	if(sigsetjmp(buf, 1) == 0)
		siglongjmp(*fork_buf, INIT_JMP_REMOVE_SIGSTACK);
}

void switch_threads(void *me, void *next)
{
	sigjmp_buf my_buf, **me_ptr = me, *next_buf = next;
	
	*me_ptr = &my_buf;
	if(sigsetjmp(my_buf, 1) == 0)
		siglongjmp(*next_buf, 1);
}

static sigjmp_buf initial_jmpbuf;

/* XXX Make these percpu */
static void (*cb_proc)(void *arg);
static void *cb_arg;
static sigjmp_buf *cb_back;

int start_idle_thread(void *stack, void *switch_buf_ptr, void **fork_buf_ptr)
{
	sigjmp_buf **switch_buf = switch_buf_ptr;
	int n;

	set_handler(SIGWINCH, (__sighandler_t) sig_handler,
		    SA_ONSTACK | SA_RESTART, SIGUSR1, SIGIO, SIGALRM,
		    SIGVTALRM, -1);

	*fork_buf_ptr = &initial_jmpbuf;
	n = sigsetjmp(initial_jmpbuf, 1);
        switch(n){
        case INIT_JMP_NEW_THREAD:
                new_thread_proc((void *) stack, new_thread_handler);
                break;
        case INIT_JMP_REMOVE_SIGSTACK:
                remove_sigstack();
                break;
        case INIT_JMP_CALLBACK:
		(*cb_proc)(cb_arg);
		siglongjmp(*cb_back, 1);
                break;
        case INIT_JMP_HALT:
		kmalloc_ok = 0;
		return(0);
        case INIT_JMP_REBOOT:
		kmalloc_ok = 0;
		return(1);
        default:
                panic("Bad sigsetjmp return in start_idle_thread - %d\n", n);
	}
	siglongjmp(**switch_buf, 1);
}

void remove_sigstack(void)
{
	stack_t stack = ((stack_t) { .ss_flags	= SS_DISABLE,
				     .ss_sp	= NULL,
				     .ss_size	= 0 });

	if(sigaltstack(&stack, NULL) != 0)
		panic("disabling signal stack failed, errno = %d\n", errno);
}

void initial_thread_cb_skas(void (*proc)(void *), void *arg)
{
	sigjmp_buf here;

	cb_proc = proc;
	cb_arg = arg;
	cb_back = &here;

	block_signals();
	if(sigsetjmp(here, 1) == 0)
		siglongjmp(initial_jmpbuf, INIT_JMP_CALLBACK);
	unblock_signals();

	cb_proc = NULL;
	cb_arg = NULL;
	cb_back = NULL;
}

void halt_skas(void)
{
	block_signals();
	siglongjmp(initial_jmpbuf, INIT_JMP_HALT);
}

void reboot_skas(void)
{
	block_signals();
	siglongjmp(initial_jmpbuf, INIT_JMP_REBOOT);
}

void switch_mm_skas(struct mm_id *mm_idp)
{
	int err;

#warning need cpu pid in switch_mm_skas
	if(proc_mm){
		err = ptrace(PTRACE_SWITCH_MM, userspace_pid[0], 0,
			     mm_idp->u.mm_fd);
		if(err)
			panic("switch_mm_skas - PTRACE_SWITCH_MM failed, "
			      "errno = %d\n", errno);
	}
	else userspace_pid[0] = mm_idp->u.pid;
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
