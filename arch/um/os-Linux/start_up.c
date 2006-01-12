/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <fcntl.h>
#include <errno.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include <sys/types.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "signal_kern.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "irq_user.h"
#include "ptrace_user.h"
#include "mem_user.h"
#include "time_user.h"
#include "init.h"
#include "os.h"
#include "uml-config.h"
#include "choose-mode.h"
#include "mode.h"
#include "tempfile.h"
#include "kern_constants.h"

#ifdef UML_CONFIG_MODE_SKAS
#include "skas.h"
#include "skas_ptrace.h"
#include "registers.h"
#endif

static int ptrace_child(void *arg)
{
	int ret;
	int pid = os_getpid(), ppid = getppid();
	int sc_result;

	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0){
		perror("ptrace");
		os_kill_process(pid, 0);
	}
	os_stop_process(pid);

	/*This syscall will be intercepted by the parent. Don't call more than
	 * once, please.*/
	sc_result = os_getpid();

	if (sc_result == pid)
		ret = 1; /*Nothing modified by the parent, we are running
			   normally.*/
	else if (sc_result == ppid)
		ret = 0; /*Expected in check_ptrace and check_sysemu when they
			   succeed in modifying the stack frame*/
	else
		ret = 2; /*Serious trouble! This could be caused by a bug in
			   host 2.6 SKAS3/2.6 patch before release -V6, together
			   with a bug in the UML code itself.*/
	_exit(ret);
}

static int start_ptraced_child(void **stack_out)
{
	void *stack;
	unsigned long sp;
	int pid, n, status;

	stack = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(stack == MAP_FAILED)
		panic("check_ptrace : mmap failed, errno = %d", errno);
	sp = (unsigned long) stack + PAGE_SIZE - sizeof(void *);
	pid = clone(ptrace_child, (void *) sp, SIGCHLD, NULL);
	if(pid < 0)
		panic("start_ptraced_child : clone failed, errno = %d", errno);
	CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
	if(n < 0)
		panic("check_ptrace : clone failed, errno = %d", errno);
	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP))
		panic("check_ptrace : expected SIGSTOP, got status = %d",
		      status);

	*stack_out = stack;
	return(pid);
}

/* When testing for SYSEMU support, if it is one of the broken versions, we
 * must just avoid using sysemu, not panic, but only if SYSEMU features are
 * broken.
 * So only for SYSEMU features we test mustpanic, while normal host features
 * must work anyway!
 */
static int stop_ptraced_child(int pid, void *stack, int exitcode,
			      int mustpanic)
{
	int status, n, ret = 0;

	if(ptrace(PTRACE_CONT, pid, 0, 0) < 0)
		panic("check_ptrace : ptrace failed, errno = %d", errno);
	CATCH_EINTR(n = waitpid(pid, &status, 0));
	if(!WIFEXITED(status) || (WEXITSTATUS(status) != exitcode)) {
		int exit_with = WEXITSTATUS(status);
		if (exit_with == 2)
			printf("check_ptrace : child exited with status 2. "
			       "Serious trouble happening! Try updating your "
			       "host skas patch!\nDisabling SYSEMU support.");
		printf("check_ptrace : child exited with exitcode %d, while "
		      "expecting %d; status 0x%x", exit_with,
		      exitcode, status);
		if (mustpanic)
			panic("\n");
		else
			printf("\n");
		ret = -1;
	}

	if(munmap(stack, PAGE_SIZE) < 0)
		panic("check_ptrace : munmap failed, errno = %d", errno);
	return ret;
}

int ptrace_faultinfo = 1;
int ptrace_ldt = 1;
int proc_mm = 1;
int skas_needs_stub = 0;

static int __init skas0_cmd_param(char *str, int* add)
{
	ptrace_faultinfo = proc_mm = 0;
	return 0;
}

/* The two __uml_setup would conflict, without this stupid alias. */

static int __init mode_skas0_cmd_param(char *str, int* add)
	__attribute__((alias("skas0_cmd_param")));

__uml_setup("skas0", skas0_cmd_param,
		"skas0\n"
		"    Disables SKAS3 usage, so that SKAS0 is used, unless \n"
	        "    you specify mode=tt.\n\n");

__uml_setup("mode=skas0", mode_skas0_cmd_param,
		"mode=skas0\n"
		"    Disables SKAS3 usage, so that SKAS0 is used, unless you \n"
		"    specify mode=tt. Note that this was recently added - on \n"
		"    older kernels you must use simply \"skas0\".\n\n");

static int force_sysemu_disabled = 0;

static int __init nosysemu_cmd_param(char *str, int* add)
{
	force_sysemu_disabled = 1;
	return 0;
}

__uml_setup("nosysemu", nosysemu_cmd_param,
"nosysemu\n"
"    Turns off syscall emulation patch for ptrace (SYSEMU) on.\n"
"    SYSEMU is a performance-patch introduced by Laurent Vivier. It changes\n"
"    behaviour of ptrace() and helps reducing host context switch rate.\n"
"    To make it working, you need a kernel patch for your host, too.\n"
"    See http://perso.wanadoo.fr/laurent.vivier/UML/ for further \n"
"    information.\n\n");

static void __init check_sysemu(void)
{
	void *stack;
 	int pid, n, status, count=0;

	printf("Checking syscall emulation patch for ptrace...");
	sysemu_supported = 0;
	pid = start_ptraced_child(&stack);

	if(ptrace(PTRACE_SYSEMU, pid, 0, 0) < 0)
		goto fail;

	CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
	if (n < 0)
		panic("check_sysemu : wait failed, errno = %d", errno);
	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGTRAP))
		panic("check_sysemu : expected SIGTRAP, "
		      "got status = %d", status);

	n = ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_RET_OFFSET,
		   os_getpid());
	if(n < 0)
		panic("check_sysemu : failed to modify system "
		      "call return, errno = %d", errno);

	if (stop_ptraced_child(pid, stack, 0, 0) < 0)
		goto fail_stopped;

	sysemu_supported = 1;
	printf("OK\n");
	set_using_sysemu(!force_sysemu_disabled);

	printf("Checking advanced syscall emulation patch for ptrace...");
	pid = start_ptraced_child(&stack);

	if(ptrace(PTRACE_OLDSETOPTIONS, pid, 0,
		  (void *) PTRACE_O_TRACESYSGOOD) < 0)
		panic("check_ptrace: PTRACE_OLDSETOPTIONS failed, errno = %d",
		      errno);

	while(1){
		count++;
		if(ptrace(PTRACE_SYSEMU_SINGLESTEP, pid, 0, 0) < 0)
			goto fail;
		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
		if(n < 0)
			panic("check_ptrace : wait failed, errno = %d", errno);
		if(WIFSTOPPED(status) && (WSTOPSIG(status) == (SIGTRAP|0x80))){
			if (!count)
				panic("check_ptrace : SYSEMU_SINGLESTEP "
				      "doesn't singlestep");
			n = ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_RET_OFFSET,
				   os_getpid());
			if(n < 0)
				panic("check_sysemu : failed to modify system "
				      "call return, errno = %d", errno);
			break;
		}
		else if(WIFSTOPPED(status) && (WSTOPSIG(status) == SIGTRAP))
			count++;
		else
			panic("check_ptrace : expected SIGTRAP or "
			      "(SIGTRAP|0x80), got status = %d", status);
	}
	if (stop_ptraced_child(pid, stack, 0, 0) < 0)
		goto fail_stopped;

	sysemu_supported = 2;
	printf("OK\n");

	if ( !force_sysemu_disabled )
		set_using_sysemu(sysemu_supported);
	return;

fail:
	stop_ptraced_child(pid, stack, 1, 0);
fail_stopped:
	printf("missing\n");
}

static void __init check_ptrace(void)
{
	void *stack;
	int pid, syscall, n, status;

	printf("Checking that ptrace can change system call numbers...");
	pid = start_ptraced_child(&stack);

	if(ptrace(PTRACE_OLDSETOPTIONS, pid, 0, (void *)PTRACE_O_TRACESYSGOOD) < 0)
		panic("check_ptrace: PTRACE_OLDSETOPTIONS failed, errno = %d", errno);

	while(1){
		if(ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0)
			panic("check_ptrace : ptrace failed, errno = %d",
			      errno);
		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
		if(n < 0)
			panic("check_ptrace : wait failed, errno = %d", errno);
		if(!WIFSTOPPED(status) || (WSTOPSIG(status) != (SIGTRAP|0x80)))
			panic("check_ptrace : expected (SIGTRAP|0x80), "
			      "got status = %d", status);

		syscall = ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_NR_OFFSET,
				 0);
		if(syscall == __NR_getpid){
			n = ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_NR_OFFSET,
				   __NR_getppid);
			if(n < 0)
				panic("check_ptrace : failed to modify system "
				      "call, errno = %d", errno);
			break;
		}
	}
	stop_ptraced_child(pid, stack, 0, 1);
	printf("OK\n");
	check_sysemu();
}

extern int create_tmp_file(unsigned long long len);

static void check_tmpexec(void)
{
	void *addr;
	int err, fd = create_tmp_file(UM_KERN_PAGE_SIZE);

	addr = mmap(NULL, UM_KERN_PAGE_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0);
	printf("Checking PROT_EXEC mmap in /tmp...");
	fflush(stdout);
	if(addr == MAP_FAILED){
		err = errno;
		perror("failed");
		if(err == EPERM)
			printf("/tmp must be not mounted noexec\n");
		exit(1);
	}
	printf("OK\n");
	munmap(addr, UM_KERN_PAGE_SIZE);

	close(fd);
}

void os_early_checks(void)
{
	check_ptrace();

	/* Need to check this early because mmapping happens before the
	 * kernel is running.
	 */
	check_tmpexec();
}

static int __init noprocmm_cmd_param(char *str, int* add)
{
	proc_mm = 0;
	return 0;
}

__uml_setup("noprocmm", noprocmm_cmd_param,
"noprocmm\n"
"    Turns off usage of /proc/mm, even if host supports it.\n"
"    To support /proc/mm, the host needs to be patched using\n"
"    the current skas3 patch.\n\n");

static int __init noptracefaultinfo_cmd_param(char *str, int* add)
{
	ptrace_faultinfo = 0;
	return 0;
}

__uml_setup("noptracefaultinfo", noptracefaultinfo_cmd_param,
"noptracefaultinfo\n"
"    Turns off usage of PTRACE_FAULTINFO, even if host supports\n"
"    it. To support PTRACE_FAULTINFO, the host needs to be patched\n"
"    using the current skas3 patch.\n\n");

static int __init noptraceldt_cmd_param(char *str, int* add)
{
	ptrace_ldt = 0;
	return 0;
}

__uml_setup("noptraceldt", noptraceldt_cmd_param,
"noptraceldt\n"
"    Turns off usage of PTRACE_LDT, even if host supports it.\n"
"    To support PTRACE_LDT, the host needs to be patched using\n"
"    the current skas3 patch.\n\n");

#ifdef UML_CONFIG_MODE_SKAS
static inline void check_skas3_ptrace_faultinfo(void)
{
	struct ptrace_faultinfo fi;
	void *stack;
	int pid, n;

	printf("  - PTRACE_FAULTINFO...");
	pid = start_ptraced_child(&stack);

	n = ptrace(PTRACE_FAULTINFO, pid, 0, &fi);
	if (n < 0) {
		ptrace_faultinfo = 0;
		if(errno == EIO)
			printf("not found\n");
		else
			perror("not found");
	}
	else {
		if (!ptrace_faultinfo)
			printf("found but disabled on command line\n");
		else
			printf("found\n");
	}

	init_registers(pid);
	stop_ptraced_child(pid, stack, 1, 1);
}

static inline void check_skas3_ptrace_ldt(void)
{
#ifdef PTRACE_LDT
	void *stack;
	int pid, n;
	unsigned char ldtbuf[40];
	struct ptrace_ldt ldt_op = (struct ptrace_ldt) {
		.func = 2, /* read default ldt */
		.ptr = ldtbuf,
		.bytecount = sizeof(ldtbuf)};

	printf("  - PTRACE_LDT...");
	pid = start_ptraced_child(&stack);

	n = ptrace(PTRACE_LDT, pid, 0, (unsigned long) &ldt_op);
	if (n < 0) {
		if(errno == EIO)
			printf("not found\n");
		else {
			perror("not found");
		}
		ptrace_ldt = 0;
	}
	else {
		if(ptrace_ldt)
			printf("found\n");
		else
			printf("found, but use is disabled\n");
	}

	stop_ptraced_child(pid, stack, 1, 1);
#else
	/* PTRACE_LDT might be disabled via cmdline option.
	 * We want to override this, else we might use the stub
	 * without real need
	 */
	ptrace_ldt = 1;
#endif
}

static inline void check_skas3_proc_mm(void)
{
	printf("  - /proc/mm...");
	if (os_access("/proc/mm", OS_ACC_W_OK) < 0) {
 		proc_mm = 0;
		printf("not found\n");
	}
	else {
		if (!proc_mm)
			printf("found but disabled on command line\n");
		else
			printf("found\n");
	}
}

int can_do_skas(void)
{
	printf("Checking for the skas3 patch in the host:\n");

	check_skas3_proc_mm();
	check_skas3_ptrace_faultinfo();
	check_skas3_ptrace_ldt();

	if(!proc_mm || !ptrace_faultinfo || !ptrace_ldt)
		skas_needs_stub = 1;

	return 1;
}
#else
int can_do_skas(void)
{
	return(0);
}
#endif

int have_devanon = 0;

/* Runs on boot kernel stack - already safe to use printk. */

void check_devanon(void)
{
	int fd;

	printk("Checking for /dev/anon on the host...");
	fd = open("/dev/anon", O_RDWR);
	if(fd < 0){
		printk("Not available (open failed with errno %d)\n", errno);
		return;
	}

	printk("OK\n");
	have_devanon = 1;
}

int __init parse_iomem(char *str, int *add)
{
	struct iomem_region *new;
	struct uml_stat buf;
	char *file, *driver;
	int fd, err, size;

	driver = str;
	file = strchr(str,',');
	if(file == NULL){
		printf("parse_iomem : failed to parse iomem\n");
		goto out;
	}
	*file = '\0';
	file++;
	fd = os_open_file(file, of_rdwr(OPENFLAGS()), 0);
	if(fd < 0){
		os_print_error(fd, "parse_iomem - Couldn't open io file");
		goto out;
	}

	err = os_stat_fd(fd, &buf);
	if(err < 0){
		os_print_error(err, "parse_iomem - cannot stat_fd file");
		goto out_close;
	}

	new = malloc(sizeof(*new));
	if(new == NULL){
		perror("Couldn't allocate iomem_region struct");
		goto out_close;
	}

	size = (buf.ust_size + UM_KERN_PAGE_SIZE) & ~(UM_KERN_PAGE_SIZE - 1);

	*new = ((struct iomem_region) { .next		= iomem_regions,
					.driver		= driver,
					.fd		= fd,
					.size		= size,
					.phys		= 0,
					.virt		= 0 });
	iomem_regions = new;
	iomem_size += new->size + UM_KERN_PAGE_SIZE;

	return(0);
 out_close:
	os_close_file(fd);
 out:
	return(1);
}

