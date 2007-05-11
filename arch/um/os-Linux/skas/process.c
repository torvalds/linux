/*
 * Copyright (C) 2002- 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include "ptrace_user.h"
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <asm/types.h>
#include "user.h"
#include "sysdep/ptrace.h"
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
#include "registers.h"
#include "mem.h"
#include "uml-config.h"
#include "process.h"
#include "longjmp.h"
#include "kern_constants.h"
#include "as-layout.h"

int is_skas_winch(int pid, int fd, void *data)
{
	if(pid != os_getpgrp())
		return(0);

	register_winch_irq(-1, fd, -1, data);
	return(1);
}

static int ptrace_dump_regs(int pid)
{
        unsigned long regs[MAX_REG_NR];
        int i;

        if(ptrace(PTRACE_GETREGS, pid, 0, regs) < 0)
                return -errno;
        else {
                printk("Stub registers -\n");
                for(i = 0; i < ARRAY_SIZE(regs); i++)
                        printk("\t%d - %lx\n", i, regs[i]);
        }

        return 0;
}

/*
 * Signals that are OK to receive in the stub - we'll just continue it.
 * SIGWINCH will happen when UML is inside a detached screen.
 */
#define STUB_SIG_MASK ((1 << SIGVTALRM) | (1 << SIGWINCH))

/* Signals that the stub will finish with - anything else is an error */
#define STUB_DONE_MASK ((1 << SIGUSR1) | (1 << SIGTRAP))

void wait_stub_done(int pid)
{
	int n, status, err;

	while(1){
		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
		if((n < 0) || !WIFSTOPPED(status))
			goto bad_wait;

		if(((1 << WSTOPSIG(status)) & STUB_SIG_MASK) == 0)
			break;

		err = ptrace(PTRACE_CONT, pid, 0, 0);
		if(err)
			panic("wait_stub_done : continue failed, errno = %d\n",
			      errno);
	}

	if(((1 << WSTOPSIG(status)) & STUB_DONE_MASK) != 0)
		return;

bad_wait:
	err = ptrace_dump_regs(pid);
	if(err)
		printk("Failed to get registers from stub, errno = %d\n", -err);
	panic("wait_stub_done : failed to wait for SIGUSR1/SIGTRAP, pid = %d, "
	      "n = %d, errno = %d, status = 0x%x\n", pid, n, errno, status);
}

extern unsigned long current_stub_stack(void);

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
		err = ptrace(PTRACE_CONT, pid, 0, SIGSEGV);
		if(err)
			panic("Failed to continue stub, pid = %d, errno = %d\n",
			      pid, errno);
		wait_stub_done(pid);

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
		err = ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_NR_OFFSET,
			     __NR_getpid);
		if(err < 0)
			panic("handle_trap - nullifying syscall failed errno = %d\n",
			      errno);

		err = ptrace(PTRACE_SYSCALL, pid, 0, 0);
		if(err < 0)
			panic("handle_trap - continuing to end of syscall failed, "
			      "errno = %d\n", errno);

		CATCH_EINTR(err = waitpid(pid, &status, WUNTRACED));
		if((err < 0) || !WIFSTOPPED(status) ||
		   (WSTOPSIG(status) != SIGTRAP + 0x80)){
                        err = ptrace_dump_regs(pid);
                        if(err)
                                printk("Failed to get registers from process, "
                                       "errno = %d\n", -err);
			panic("handle_trap - failed to wait at end of syscall, "
			      "errno = %d, status = %d\n", errno, status);
                }
	}

	handle_syscall(regs);
}

extern int __syscall_stub_start;

static int userspace_tramp(void *stack)
{
	void *addr;
	int err;

	ptrace(PTRACE_TRACEME, 0, 0, 0);

	init_new_thread_signals();
	err = set_interval(1);
	if(err)
		panic("userspace_tramp - setting timer failed, errno = %d\n",
		      err);

	if(!proc_mm){
		/* This has a pte, but it can't be mapped in with the usual
		 * tlb_flush mechanism because this is part of that mechanism
		 */
		int fd;
		__u64 offset;
		fd = phys_mapping(to_phys(&__syscall_stub_start), &offset);
		addr = mmap64((void *) UML_CONFIG_STUB_CODE, UM_KERN_PAGE_SIZE,
			      PROT_EXEC, MAP_FIXED | MAP_PRIVATE, fd, offset);
		if(addr == MAP_FAILED){
			printk("mapping mmap stub failed, errno = %d\n",
			       errno);
			exit(1);
		}

		if(stack != NULL){
			fd = phys_mapping(to_phys(stack), &offset);
			addr = mmap((void *) UML_CONFIG_STUB_DATA,
				    UM_KERN_PAGE_SIZE, PROT_READ | PROT_WRITE,
				    MAP_FIXED | MAP_SHARED, fd, offset);
			if(addr == MAP_FAILED){
				printk("mapping segfault stack failed, "
				       "errno = %d\n", errno);
				exit(1);
			}
		}
	}
	if(!ptrace_faultinfo && (stack != NULL)){
		struct sigaction sa;

		unsigned long v = UML_CONFIG_STUB_CODE +
				  (unsigned long) stub_segv_handler -
				  (unsigned long) &__syscall_stub_start;

		set_sigstack((void *) UML_CONFIG_STUB_DATA, UM_KERN_PAGE_SIZE);
		sigemptyset(&sa.sa_mask);
		sigaddset(&sa.sa_mask, SIGIO);
		sigaddset(&sa.sa_mask, SIGWINCH);
		sigaddset(&sa.sa_mask, SIGALRM);
		sigaddset(&sa.sa_mask, SIGVTALRM);
		sigaddset(&sa.sa_mask, SIGUSR1);
		sa.sa_flags = SA_ONSTACK;
		sa.sa_handler = (void *) v;
		sa.sa_restorer = NULL;
		if(sigaction(SIGSEGV, &sa, NULL) < 0)
			panic("userspace_tramp - setting SIGSEGV handler "
			      "failed - errno = %d\n", errno);
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
		panic("start_userspace : PTRACE_OLDSETOPTIONS failed, errno=%d\n",
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
			      pid, op, errno);

		CATCH_EINTR(err = waitpid(pid, &status, WUNTRACED));
		if(err < 0)
			panic("userspace - waitpid failed, errno = %d\n",
			      errno);

		regs->skas.is_user = 1;
		save_registers(pid, regs);
		UPT_SYSCALL_NR(regs) = -1; /* Assume: It's not a syscall */

		if(WIFSTOPPED(status)){
			int sig = WSTOPSIG(status);
		  	switch(sig){
			case SIGSEGV:
				if(PTRACE_FULL_FAULTINFO || !ptrace_faultinfo){
					get_skas_faultinfo(pid, &regs->skas.faultinfo);
					(*sig_info[SIGSEGV])(SIGSEGV, regs);
				}
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
				block_signals();
				(*sig_info[sig])(sig, regs);
				unblock_signals();
				break;
			default:
			        printk("userspace - child stopped with signal "
				       "%d\n", sig);
			}
			pid = userspace_pid[0];
			interrupt_end();

			/* Avoid -ERESTARTSYS handling in host */
			if(PT_SYSCALL_NR_OFFSET != PT_SYSCALL_RET_OFFSET)
				PT_SYSCALL_NR(regs->skas.regs) = -1;
		}
	}
}

static unsigned long thread_regs[MAX_REG_NR];
static unsigned long thread_fp_regs[HOST_FP_SIZE];

static int __init init_thread_regs(void)
{
	get_safe_registers(thread_regs, thread_fp_regs);
	/* Set parent's instruction pointer to start of clone-stub */
	thread_regs[REGS_IP_INDEX] = UML_CONFIG_STUB_CODE +
				(unsigned long) stub_clone_handler -
				(unsigned long) &__syscall_stub_start;
	thread_regs[REGS_SP_INDEX] = UML_CONFIG_STUB_DATA + PAGE_SIZE -
		sizeof(void *);
#ifdef __SIGNAL_FRAMESIZE
	thread_regs[REGS_SP_INDEX] -= __SIGNAL_FRAMESIZE;
#endif
	return 0;
}

__initcall(init_thread_regs);

int copy_context_skas0(unsigned long new_stack, int pid)
{
	int err;
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
				      .timer    = ((struct itimerval)
					            { { 0, 1000000 / hz() },
						      { 0, 1000000 / hz() }})});
	err = ptrace_setregs(pid, thread_regs);
	if(err < 0)
		panic("copy_context_skas0 : PTRACE_SETREGS failed, "
		      "pid = %d, errno = %d\n", pid, -err);

	err = ptrace_setfpregs(pid, thread_fp_regs);
	if(err < 0)
		panic("copy_context_skas0 : PTRACE_SETFPREGS failed, "
		      "pid = %d, errno = %d\n", pid, -err);

	/* set a well known return code for detection of child write failure */
	child_data->err = 12345678;

	/* Wait, until parent has finished its work: read child's pid from
	 * parent's stack, and check, if bad result.
	 */
	err = ptrace(PTRACE_CONT, pid, 0, 0);
	if(err)
		panic("Failed to continue new process, pid = %d, "
		      "errno = %d\n", pid, errno);
	wait_stub_done(pid);

	pid = data->err;
	if(pid < 0)
		panic("copy_context_skas0 - stub-parent reports error %d\n",
		      -pid);

	/* Wait, until child has finished too: read child's result from
	 * child's stack and check it.
	 */
	wait_stub_done(pid);
	if (child_data->err != UML_CONFIG_STUB_DATA)
		panic("copy_context_skas0 - stub-child reports error %ld\n",
		      child_data->err);

	if (ptrace(PTRACE_OLDSETOPTIONS, pid, NULL,
		   (void *)PTRACE_O_TRACESYSGOOD) < 0)
		panic("copy_context_skas0 : PTRACE_OLDSETOPTIONS failed, "
		      "errno = %d\n", errno);

	return pid;
}

/*
 * This is used only, if stub pages are needed, while proc_mm is
 * available. Opening /proc/mm creates a new mm_context, which lacks
 * the stub-pages. Thus, we map them using /proc/mm-fd
 */
void map_stub_pages(int fd, unsigned long code,
		    unsigned long data, unsigned long stack)
{
	struct proc_mm_op mmop;
	int n;
	__u64 code_offset;
	int code_fd = phys_mapping(to_phys((void *) &__syscall_stub_start),
				   &code_offset);

	mmop = ((struct proc_mm_op) { .op        = MM_MMAP,
				      .u         =
				      { .mmap    =
					{ .addr    = code,
					  .len     = PAGE_SIZE,
					  .prot    = PROT_EXEC,
					  .flags   = MAP_FIXED | MAP_PRIVATE,
					  .fd      = code_fd,
					  .offset  = code_offset
	} } });
	CATCH_EINTR(n = write(fd, &mmop, sizeof(mmop)));
	if(n != sizeof(mmop)){
		n = errno;
		printk("mmap args - addr = 0x%lx, fd = %d, offset = %llx\n",
		       code, code_fd, (unsigned long long) code_offset);
		panic("map_stub_pages : /proc/mm map for code failed, "
		      "err = %d\n", n);
	}

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
		CATCH_EINTR(n = write(fd, &mmop, sizeof(mmop)));
		if(n != sizeof(mmop))
			panic("map_stub_pages : /proc/mm map for data failed, "
			      "err = %d\n", errno);
	}
}

void new_thread(void *stack, jmp_buf *buf, void (*handler)(void))
{
	(*buf)[0].JB_IP = (unsigned long) handler;
	(*buf)[0].JB_SP = (unsigned long) stack + UM_THREAD_SIZE -
		sizeof(void *);
}

#define INIT_JMP_NEW_THREAD 0
#define INIT_JMP_CALLBACK 1
#define INIT_JMP_HALT 2
#define INIT_JMP_REBOOT 3

void switch_threads(jmp_buf *me, jmp_buf *you)
{
	if(UML_SETJMP(me) == 0)
		UML_LONGJMP(you, 1);
}

static jmp_buf initial_jmpbuf;

/* XXX Make these percpu */
static void (*cb_proc)(void *arg);
static void *cb_arg;
static jmp_buf *cb_back;

int start_idle_thread(void *stack, jmp_buf *switch_buf)
{
	int n;

	set_handler(SIGWINCH, (__sighandler_t) sig_handler,
		    SA_ONSTACK | SA_RESTART, SIGUSR1, SIGIO, SIGALRM,
		    SIGVTALRM, -1);

	/*
	 * Can't use UML_SETJMP or UML_LONGJMP here because they save
	 * and restore signals, with the possible side-effect of
	 * trying to handle any signals which came when they were
	 * blocked, which can't be done on this stack.
	 * Signals must be blocked when jumping back here and restored
	 * after returning to the jumper.
	 */
	n = setjmp(initial_jmpbuf);
	switch(n){
	case INIT_JMP_NEW_THREAD:
		(*switch_buf)[0].JB_IP = (unsigned long) new_thread_handler;
		(*switch_buf)[0].JB_SP = (unsigned long) stack +
			UM_THREAD_SIZE - sizeof(void *);
		break;
	case INIT_JMP_CALLBACK:
		(*cb_proc)(cb_arg);
		longjmp(*cb_back, 1);
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
	longjmp(*switch_buf, 1);
}

void initial_thread_cb_skas(void (*proc)(void *), void *arg)
{
	jmp_buf here;

	cb_proc = proc;
	cb_arg = arg;
	cb_back = &here;

	block_signals();
	if(UML_SETJMP(&here) == 0)
		UML_LONGJMP(&initial_jmpbuf, INIT_JMP_CALLBACK);
	unblock_signals();

	cb_proc = NULL;
	cb_arg = NULL;
	cb_back = NULL;
}

void halt_skas(void)
{
	block_signals();
	UML_LONGJMP(&initial_jmpbuf, INIT_JMP_HALT);
}

void reboot_skas(void)
{
	block_signals();
	UML_LONGJMP(&initial_jmpbuf, INIT_JMP_REBOOT);
}

void switch_mm_skas(struct mm_id *mm_idp)
{
	int err;

	/* FIXME: need cpu pid in switch_mm_skas */
	if(proc_mm){
		err = ptrace(PTRACE_SWITCH_MM, userspace_pid[0], 0,
			     mm_idp->u.mm_fd);
		if(err)
			panic("switch_mm_skas - PTRACE_SWITCH_MM failed, "
			      "errno = %d\n", errno);
	}
	else userspace_pid[0] = mm_idp->u.pid;
}
