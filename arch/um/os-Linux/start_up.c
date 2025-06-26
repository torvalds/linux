// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <asm/ldt.h>
#include <asm/unistd.h>
#include <init.h>
#include <os.h>
#include <kern_util.h>
#include <mem_user.h>
#include <ptrace_user.h>
#include <stdbool.h>
#include <stub-data.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <sysdep/mcontext.h>
#include <sysdep/stub.h>
#include <registers.h>
#include <skas.h>
#include "internal.h"

static void ptrace_child(void)
{
	int ret;
	/* Calling os_getpid because some libcs cached getpid incorrectly */
	int pid = os_getpid(), ppid = getppid();
	int sc_result;

	if (change_sig(SIGWINCH, 0) < 0 ||
	    ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
		perror("ptrace");
		kill(pid, SIGKILL);
	}
	kill(pid, SIGSTOP);

	/*
	 * This syscall will be intercepted by the parent. Don't call more than
	 * once, please.
	 */
	sc_result = os_getpid();

	if (sc_result == pid)
		/* Nothing modified by the parent, we are running normally. */
		ret = 1;
	else if (sc_result == ppid)
		/*
		 * Expected in check_ptrace and check_sysemu when they succeed
		 * in modifying the stack frame
		 */
		ret = 0;
	else
		/* Serious trouble! This could be caused by a bug in host 2.6
		 * SKAS3/2.6 patch before release -V6, together with a bug in
		 * the UML code itself.
		 */
		ret = 2;

	exit(ret);
}

static void fatal_perror(const char *str)
{
	perror(str);
	exit(1);
}

static void fatal(char *fmt, ...)
{
	va_list list;

	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);

	exit(1);
}

static void non_fatal(char *fmt, ...)
{
	va_list list;

	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);
}

static int start_ptraced_child(void)
{
	int pid, n, status;

	fflush(stdout);

	pid = fork();
	if (pid == 0)
		ptrace_child();
	else if (pid < 0)
		fatal_perror("start_ptraced_child : fork failed");

	CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
	if (n < 0)
		fatal_perror("check_ptrace : waitpid failed");
	if (!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP))
		fatal("check_ptrace : expected SIGSTOP, got status = %d",
		      status);

	return pid;
}

static void stop_ptraced_child(int pid, int exitcode)
{
	int status, n;

	if (ptrace(PTRACE_CONT, pid, 0, 0) < 0)
		fatal_perror("stop_ptraced_child : ptrace failed");

	CATCH_EINTR(n = waitpid(pid, &status, 0));
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != exitcode)) {
		int exit_with = WEXITSTATUS(status);
		fatal("stop_ptraced_child : child exited with exitcode %d, "
		      "while expecting %d; status 0x%x\n", exit_with,
		      exitcode, status);
	}
}

static void __init check_sysemu(void)
{
	int pid, n, status, count=0;

	os_info("Checking syscall emulation for ptrace...");
	pid = start_ptraced_child();

	if ((ptrace(PTRACE_SETOPTIONS, pid, 0,
		   (void *) PTRACE_O_TRACESYSGOOD) < 0))
		fatal_perror("check_sysemu: PTRACE_SETOPTIONS failed");

	while (1) {
		count++;
		if (ptrace(PTRACE_SYSEMU_SINGLESTEP, pid, 0, 0) < 0)
			goto fail;
		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
		if (n < 0)
			fatal_perror("check_sysemu: wait failed");

		if (WIFSTOPPED(status) &&
		    (WSTOPSIG(status) == (SIGTRAP|0x80))) {
			if (!count) {
				non_fatal("check_sysemu: SYSEMU_SINGLESTEP "
					  "doesn't singlestep");
				goto fail;
			}
			n = ptrace(PTRACE_POKEUSER, pid, PT_SYSCALL_RET_OFFSET,
				   os_getpid());
			if (n < 0)
				fatal_perror("check_sysemu : failed to modify "
					     "system call return");
			break;
		}
		else if (WIFSTOPPED(status) && (WSTOPSIG(status) == SIGTRAP))
			count++;
		else {
			non_fatal("check_sysemu: expected SIGTRAP or "
				  "(SIGTRAP | 0x80), got status = %d\n",
				  status);
			goto fail;
		}
	}
	stop_ptraced_child(pid, 0);

	os_info("OK\n");

	return;

fail:
	stop_ptraced_child(pid, 1);
	fatal("missing\n");
}

static void __init check_ptrace(void)
{
	int pid, syscall, n, status;

	os_info("Checking that ptrace can change system call numbers...");
	pid = start_ptraced_child();

	if ((ptrace(PTRACE_SETOPTIONS, pid, 0,
		   (void *) PTRACE_O_TRACESYSGOOD) < 0))
		fatal_perror("check_ptrace: PTRACE_SETOPTIONS failed");

	while (1) {
		if (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0)
			fatal_perror("check_ptrace : ptrace failed");

		CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
		if (n < 0)
			fatal_perror("check_ptrace : wait failed");

		if (!WIFSTOPPED(status) ||
		   (WSTOPSIG(status) != (SIGTRAP | 0x80)))
			fatal("check_ptrace : expected (SIGTRAP|0x80), "
			       "got status = %d", status);

		syscall = ptrace(PTRACE_PEEKUSER, pid, PT_SYSCALL_NR_OFFSET,
				 0);
		if (syscall == __NR_getpid) {
			n = ptrace(PTRACE_POKEUSER, pid, PT_SYSCALL_NR_OFFSET,
				   __NR_getppid);
			if (n < 0)
				fatal_perror("check_ptrace : failed to modify "
					     "system call");
			break;
		}
	}
	stop_ptraced_child(pid, 0);
	os_info("OK\n");
	check_sysemu();
}

extern unsigned long host_fp_size;
extern unsigned long exec_regs[MAX_REG_NR];
extern unsigned long *exec_fp_regs;

__initdata static struct stub_data *seccomp_test_stub_data;

static void __init sigsys_handler(int sig, siginfo_t *info, void *p)
{
	ucontext_t *uc = p;

	/* Stow away the location of the mcontext in the stack */
	seccomp_test_stub_data->mctx_offset = (unsigned long)&uc->uc_mcontext -
					      (unsigned long)&seccomp_test_stub_data->sigstack[0];

	/* Prevent libc from clearing memory (mctx_offset in particular) */
	syscall(__NR_exit, 0);
}

static int __init seccomp_helper(void *data)
{
	static struct sock_filter filter[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
			 offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clock_nanosleep, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
	};
	static struct sock_fprog prog = {
		.len = ARRAY_SIZE(filter),
		.filter = filter,
	};
	struct sigaction sa;

	/* close_range is needed for the stub */
	if (stub_syscall3(__NR_close_range, 1, ~0U, 0))
		exit(1);

	set_sigstack(seccomp_test_stub_data->sigstack,
			sizeof(seccomp_test_stub_data->sigstack));

	sa.sa_flags = SA_ONSTACK | SA_NODEFER | SA_SIGINFO;
	sa.sa_sigaction = (void *) sigsys_handler;
	sa.sa_restorer = NULL;
	if (sigaction(SIGSYS, &sa, NULL) < 0)
		exit(2);

	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
			SECCOMP_FILTER_FLAG_TSYNC, &prog) != 0)
		exit(3);

	sleep(0);

	/* Never reached. */
	_exit(4);
}

static bool __init init_seccomp(void)
{
	int pid;
	int status;
	int n;
	unsigned long sp;

	/*
	 * We check that we can install a seccomp filter and then exit(0)
	 * from a trapped syscall.
	 *
	 * Note that we cannot verify that no seccomp filter already exists
	 * for a syscall that results in the process/thread to be killed.
	 */

	os_info("Checking that seccomp filters can be installed...");

	seccomp_test_stub_data = mmap(0, sizeof(*seccomp_test_stub_data),
				      PROT_READ | PROT_WRITE,
				      MAP_SHARED | MAP_ANON, 0, 0);

	/* Use the syscall data area as stack, we just need something */
	sp = (unsigned long)&seccomp_test_stub_data->syscall_data +
	     sizeof(seccomp_test_stub_data->syscall_data) -
	     sizeof(void *);
	pid = clone(seccomp_helper, (void *)sp, CLONE_VFORK | CLONE_VM, NULL);

	if (pid < 0)
		fatal_perror("check_seccomp : clone failed");

	CATCH_EINTR(n = waitpid(pid, &status, __WCLONE));
	if (n < 0)
		fatal_perror("check_seccomp : waitpid failed");

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		struct uml_pt_regs *regs;
		unsigned long fp_size;
		int r;

		/* Fill in the host_fp_size from the mcontext. */
		regs = calloc(1, sizeof(struct uml_pt_regs));
		get_stub_state(regs, seccomp_test_stub_data, &fp_size);
		host_fp_size = fp_size;
		free(regs);

		/* Repeat with the correct size */
		regs = calloc(1, sizeof(struct uml_pt_regs) + host_fp_size);
		r = get_stub_state(regs, seccomp_test_stub_data, NULL);

		/* Store as the default startup registers */
		exec_fp_regs = malloc(host_fp_size);
		memcpy(exec_regs, regs->gp, sizeof(exec_regs));
		memcpy(exec_fp_regs, regs->fp, host_fp_size);

		munmap(seccomp_test_stub_data, sizeof(*seccomp_test_stub_data));

		free(regs);

		if (r) {
			os_info("failed to fetch registers: %d\n", r);
			return false;
		}

		os_info("OK\n");
		return true;
	}

	if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
		os_info("missing\n");
	else
		os_info("error\n");

	munmap(seccomp_test_stub_data, sizeof(*seccomp_test_stub_data));
	return false;
}


static void __init check_coredump_limit(void)
{
	struct rlimit lim;
	int err = getrlimit(RLIMIT_CORE, &lim);

	if (err) {
		perror("Getting core dump limit");
		return;
	}

	os_info("Core dump limits :\n\tsoft - ");
	if (lim.rlim_cur == RLIM_INFINITY)
		os_info("NONE\n");
	else
		os_info("%llu\n", (unsigned long long)lim.rlim_cur);

	os_info("\thard - ");
	if (lim.rlim_max == RLIM_INFINITY)
		os_info("NONE\n");
	else
		os_info("%llu\n", (unsigned long long)lim.rlim_max);
}

void  __init get_host_cpu_features(
		void (*flags_helper_func)(char *line),
		void (*cache_helper_func)(char *line))
{
	FILE *cpuinfo;
	char *line = NULL;
	size_t len = 0;
	int done_parsing = 0;

	cpuinfo = fopen("/proc/cpuinfo", "r");
	if (cpuinfo == NULL) {
		os_info("Failed to get host CPU features\n");
	} else {
		while ((getline(&line, &len, cpuinfo)) != -1) {
			if (strstr(line, "flags")) {
				flags_helper_func(line);
				done_parsing++;
			}
			if (strstr(line, "cache_alignment")) {
				cache_helper_func(line);
				done_parsing++;
			}
			free(line);
			line = NULL;
			if (done_parsing > 1)
				break;
		}
		fclose(cpuinfo);
	}
}

static int seccomp_config __initdata;

static int __init uml_seccomp_config(char *line, int *add)
{
	*add = 0;

	if (strcmp(line, "off") == 0)
		seccomp_config = 0;
	else if (strcmp(line, "auto") == 0)
		seccomp_config = 1;
	else if (strcmp(line, "on") == 0)
		seccomp_config = 2;
	else
		fatal("Invalid seccomp option '%s', expected on/auto/off\n",
		      line);

	return 0;
}

__uml_setup("seccomp=", uml_seccomp_config,
"seccomp=<on/auto/off>\n"
"    Configure whether or not SECCOMP is used. With SECCOMP, userspace\n"
"    processes work collaboratively with the kernel instead of being\n"
"    traced using ptrace. All syscalls from the application are caught and\n"
"    redirected using a signal. This signal handler in turn is permitted to\n"
"    do the selected set of syscalls to communicate with the UML kernel and\n"
"    do the required memory management.\n"
"\n"
"    This method is overall faster than the ptrace based userspace, primarily\n"
"    because it reduces the number of context switches for (minor) page faults.\n"
"\n"
"    However, the SECCOMP filter is not (yet) restrictive enough to prevent\n"
"    userspace from reading and writing all physical memory. Userspace\n"
"    processes could also trick the stub into disabling SIGALRM which\n"
"    prevents it from being interrupted for scheduling purposes.\n"
"\n"
"    This is insecure and should only be used with a trusted userspace\n\n"
);

void __init os_early_checks(void)
{
	int pid;

	/* Print out the core dump limits early */
	check_coredump_limit();

	/* Need to check this early because mmapping happens before the
	 * kernel is running.
	 */
	check_tmpexec();

	if (seccomp_config) {
		if (init_seccomp()) {
			using_seccomp = 1;
			return;
		}

		if (seccomp_config == 2)
			fatal("SECCOMP userspace requested but not functional!\n");
	}

	using_seccomp = 0;
	check_ptrace();

	pid = start_ptraced_child();
	if (init_pid_registers(pid))
		fatal("Failed to initialize default registers");
	stop_ptraced_child(pid, 1);
}

int __init parse_iomem(char *str, int *add)
{
	struct iomem_region *new;
	struct stat64 buf;
	char *file, *driver;
	int fd, size;

	driver = str;
	file = strchr(str,',');
	if (file == NULL) {
		os_warn("parse_iomem : failed to parse iomem\n");
		goto out;
	}
	*file = '\0';
	file++;
	fd = open(file, O_RDWR, 0);
	if (fd < 0) {
		perror("parse_iomem - Couldn't open io file");
		goto out;
	}

	if (fstat64(fd, &buf) < 0) {
		perror("parse_iomem - cannot stat_fd file");
		goto out_close;
	}

	new = malloc(sizeof(*new));
	if (new == NULL) {
		perror("Couldn't allocate iomem_region struct");
		goto out_close;
	}

	size = (buf.st_size + UM_KERN_PAGE_SIZE) & ~(UM_KERN_PAGE_SIZE - 1);

	*new = ((struct iomem_region) { .next		= iomem_regions,
					.driver		= driver,
					.fd		= fd,
					.size		= size,
					.phys		= 0,
					.virt		= 0 });
	iomem_regions = new;
	iomem_size += new->size + UM_KERN_PAGE_SIZE;

	return 0;
 out_close:
	close(fd);
 out:
	return 1;
}
