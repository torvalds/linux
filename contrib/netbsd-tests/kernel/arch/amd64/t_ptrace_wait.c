/*	$NetBSD: t_ptrace_wait.c,v 1.11 2017/01/18 05:14:34 kamil Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_ptrace_wait.c,v 1.11 2017/01/18 05:14:34 kamil Exp $");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <machine/reg.h>
#include <x86/dbregs.h>
#include <err.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

#include "../../t_ptrace_wait.h"


#if defined(HAVE_GPREGS)
ATF_TC(regs1);
ATF_TC_HEAD(regs1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_GETREGS and iterate over General Purpose registers");
}

ATF_TC_BODY(regs1, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	struct reg r;

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Call GETREGS for the child process\n");
	ATF_REQUIRE(ptrace(PT_GETREGS, child, &r, 0) != -1);

	printf("RAX=%#" PRIxREGISTER "\n", r.regs[_REG_RAX]);
	printf("RBX=%#" PRIxREGISTER "\n", r.regs[_REG_RBX]);
	printf("RCX=%#" PRIxREGISTER "\n", r.regs[_REG_RCX]);
	printf("RDX=%#" PRIxREGISTER "\n", r.regs[_REG_RDX]);

	printf("RDI=%#" PRIxREGISTER "\n", r.regs[_REG_RDI]);
	printf("RSI=%#" PRIxREGISTER "\n", r.regs[_REG_RSI]);

	printf("GS=%#" PRIxREGISTER "\n", r.regs[_REG_GS]);
	printf("FS=%#" PRIxREGISTER "\n", r.regs[_REG_FS]);
	printf("ES=%#" PRIxREGISTER "\n", r.regs[_REG_ES]);
	printf("DS=%#" PRIxREGISTER "\n", r.regs[_REG_DS]);
	printf("CS=%#" PRIxREGISTER "\n", r.regs[_REG_CS]);
	printf("SS=%#" PRIxREGISTER "\n", r.regs[_REG_SS]);

	printf("RSP=%#" PRIxREGISTER "\n", r.regs[_REG_RSP]);
	printf("RIP=%#" PRIxREGISTER "\n", r.regs[_REG_RIP]);

	printf("RFLAGS=%#" PRIxREGISTER "\n", r.regs[_REG_RFLAGS]);

	printf("R8=%#" PRIxREGISTER "\n", r.regs[_REG_R8]);
	printf("R9=%#" PRIxREGISTER "\n", r.regs[_REG_R9]);
	printf("R10=%#" PRIxREGISTER "\n", r.regs[_REG_R10]);
	printf("R11=%#" PRIxREGISTER "\n", r.regs[_REG_R11]);
	printf("R12=%#" PRIxREGISTER "\n", r.regs[_REG_R12]);
	printf("R13=%#" PRIxREGISTER "\n", r.regs[_REG_R13]);
	printf("R14=%#" PRIxREGISTER "\n", r.regs[_REG_R14]);
	printf("R15=%#" PRIxREGISTER "\n", r.regs[_REG_R15]);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_count);
ATF_TC_HEAD(watchpoint_count, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and assert four available watchpoints");
}

ATF_TC_BODY(watchpoint_count, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	int N;

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Call GETREGS for the child process\n");
	ATF_REQUIRE((N = ptrace(PT_COUNT_WATCHPOINTS, child, NULL, 0)) != -1);
	printf("Reported %d watchpoints\n", N);

	ATF_REQUIRE_EQ_MSG(N, 4, "Expected 4 hw watchpoints - got %d", N);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_read);
ATF_TC_HEAD(watchpoint_read, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and assert four available watchpoints");
}

ATF_TC_BODY(watchpoint_read, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	int i, N;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Call GETREGS for the child process\n");
	ATF_REQUIRE((N = ptrace(PT_COUNT_WATCHPOINTS, child, NULL, 0)) != -1);

	ATF_REQUIRE_EQ_MSG(N, 4, "Expected 4 hw watchpoints - got %d", N);

	for (i = 0; i < N; i++) {
		printf("Before reading watchpoint %d\n", i);
		pw.pw_index = i;
		ATF_REQUIRE(ptrace(PT_READ_WATCHPOINT, child, &pw, len) != -1);

		printf("struct ptrace {\n");
		printf("\t.pw_index=%d\n", pw.pw_index);
		printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
		printf("\t.pw_type=%#x\n", pw.pw_type);
		printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
		printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
		printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
		printf("}\n");
	}

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_write_unmodified);
ATF_TC_HEAD(watchpoint_write_unmodified, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and assert functional write of "
	    "unmodified data");
}

ATF_TC_BODY(watchpoint_write_unmodified, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	int i, N;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Call GETREGS for the child process\n");
	ATF_REQUIRE((N = ptrace(PT_COUNT_WATCHPOINTS, child, NULL, 0)) != -1);

	ATF_REQUIRE_EQ_MSG(N, 4, "Expected 4 hw watchpoints - got %d", N);

	for (i = 0; i < N; i++) {
		printf("Before reading watchpoint %d\n", i);
		pw.pw_index = i;
		ATF_REQUIRE(ptrace(PT_READ_WATCHPOINT, child, &pw, len) != -1);

		printf("struct ptrace {\n");
		printf("\t.pw_index=%d\n", pw.pw_index);
		printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
		printf("\t.pw_type=%#x\n", pw.pw_type);
		printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
		printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
		printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
		printf("}\n");

		printf("Before writing watchpoint %d (unmodified)\n", i);
		ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len)
		    != -1);
	}

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_code0);
ATF_TC_HEAD(watchpoint_trap_code0, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test code trap with watchpoint 0");
}

ATF_TC_BODY(watchpoint_trap_code0, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 0;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("check_happy(%d)=%d\n", watchme, check_happy(watchme));

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_lwpid = 0;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = (void *)check_happy;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_EXECUTION;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 0);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	pw.pw_md.md_address = NULL;
	printf("Before writing watchpoint %d (disable it)\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_code1);
ATF_TC_HEAD(watchpoint_trap_code1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test code trap with watchpoint 1");
}

ATF_TC_BODY(watchpoint_trap_code1, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 1;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("check_happy(%d)=%d\n", watchme, check_happy(watchme));

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_lwpid = 0;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = (void *)check_happy;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_EXECUTION;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%d\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 1);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	pw.pw_md.md_address = NULL;
	printf("Before writing watchpoint %d (disable it)\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_code2);
ATF_TC_HEAD(watchpoint_trap_code2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test code trap with watchpoint 2");
}

ATF_TC_BODY(watchpoint_trap_code2, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 2;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("check_happy(%d)=%d\n", watchme, check_happy(watchme));

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_lwpid = 0;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = (void *)check_happy;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_EXECUTION;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 2);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	pw.pw_md.md_address = NULL;
	printf("Before writing watchpoint %d (disable it)\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_code3);
ATF_TC_HEAD(watchpoint_trap_code3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test code trap with watchpoint 3");
}

ATF_TC_BODY(watchpoint_trap_code3, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 3;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("check_happy(%d)=%d\n", watchme, check_happy(watchme));

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_lwpid = 0;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = (void *)check_happy;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_EXECUTION;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 3);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	pw.pw_md.md_address = NULL;
	printf("Before writing watchpoint %d (disable it)\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_write0);
ATF_TC_HEAD(watchpoint_trap_data_write0, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 0");
}

ATF_TC_BODY(watchpoint_trap_data_write0, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 0;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		++watchme;

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = 0;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_WRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 0);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_write1);
ATF_TC_HEAD(watchpoint_trap_data_write1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 1");
}

ATF_TC_BODY(watchpoint_trap_data_write1, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 1;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		++watchme;

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_WRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 1);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_write2);
ATF_TC_HEAD(watchpoint_trap_data_write2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 2");
}

ATF_TC_BODY(watchpoint_trap_data_write2, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 2;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		++watchme;

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_WRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 2);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif


#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_write3);
ATF_TC_HEAD(watchpoint_trap_data_write3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 3");
}

ATF_TC_BODY(watchpoint_trap_data_write3, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 3;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		++watchme;

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_WRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 3);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_rw0);
ATF_TC_HEAD(watchpoint_trap_data_rw0, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 0");
}

ATF_TC_BODY(watchpoint_trap_data_rw0, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 0;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("watchme=%d\n", watchme);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_READWRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 0);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_rw1);
ATF_TC_HEAD(watchpoint_trap_data_rw1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 1");
}

ATF_TC_BODY(watchpoint_trap_data_rw1, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 1;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("watchme=%d\n", watchme);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_READWRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 1);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_rw2);
ATF_TC_HEAD(watchpoint_trap_data_rw2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 2");
}

ATF_TC_BODY(watchpoint_trap_data_rw2, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 2;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("watchme=%d\n", watchme);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_READWRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 2);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
ATF_TC(watchpoint_trap_data_rw3);
ATF_TC_HEAD(watchpoint_trap_data_rw3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Call PT_COUNT_WATCHPOINTS and test write trap with watchpoint 3");
}

ATF_TC_BODY(watchpoint_trap_data_rw3, tc)
{
	const int exitval = 5;
	const int sigval = SIGSTOP;
	pid_t child, wpid;
#if defined(TWAIT_HAVE_STATUS)
	int status;
#endif
	const int i = 3;
	struct ptrace_watchpoint pw;
	int len = sizeof(pw);
	int watchme = 1234;
	struct ptrace_siginfo info;
	memset(&info, 0, sizeof(info));

	printf("Before forking process PID=%d\n", getpid());
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		printf("Before calling PT_TRACE_ME from child %d\n", getpid());
		FORKEE_ASSERT(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

		printf("Before raising %s from child\n", strsignal(sigval));
		FORKEE_ASSERT(raise(sigval) == 0);

		printf("watchme=%d\n", watchme);

		printf("Before exiting of the child process\n");
		_exit(exitval);
	}
	printf("Parent process PID=%d, child's PID=%d\n", getpid(), child);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, sigval);

	printf("Preparing code watchpoint trap %d\n", i);

	pw.pw_index = i;
	pw.pw_type = PTRACE_PW_TYPE_DBREGS;
	pw.pw_md.md_address = &watchme;
	pw.pw_md.md_condition = X86_HW_WATCHPOINT_DR7_CONDITION_DATA_READWRITE;
	pw.pw_md.md_length = X86_HW_WATCHPOINT_DR7_LENGTH_BYTE;

	printf("struct ptrace {\n");
	printf("\t.pw_index=%d\n", pw.pw_index);
	printf("\t.pw_lwpid=%d\n", pw.pw_lwpid);
	printf("\t.pw_type=%#x\n", pw.pw_type);
	printf("\t.pw_md.md_address=%p\n", pw.pw_md.md_address);
	printf("\t.pw_md.md_condition=%#x\n", pw.pw_md.md_condition);
	printf("\t.pw_md.md_length=%#x\n", pw.pw_md.md_length);
	printf("}\n");

	printf("Before writing watchpoint %d\n", i);
	ATF_REQUIRE(ptrace(PT_WRITE_WATCHPOINT, child, &pw, len) != -1);

	printf("Before resuming the child process where it left off "
	    "and without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_stopped(status, SIGTRAP);

	printf("Before calling ptrace(2) with PT_GET_SIGINFO for child\n");
	ATF_REQUIRE(ptrace(PT_GET_SIGINFO, child, &info, sizeof(info)) != -1);

	printf("Signal traced to lwpid=%d\n", info.psi_lwpid);
	printf("Signal properties: si_signo=%#x si_code=%#x si_errno=%#x\n",
	    info.psi_siginfo.si_signo, info.psi_siginfo.si_code,
	    info.psi_siginfo.si_errno);

	printf("Before checking siginfo_t\n");
	ATF_REQUIRE_EQ(info.psi_siginfo.si_signo, SIGTRAP);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_code, TRAP_HWWPT);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap2, 3);
	ATF_REQUIRE_EQ(info.psi_siginfo.si_trap3, X86_HW_WATCHPOINT_EVENT_FIRED);

	printf("Before resuming the child process where it left off and "
	    "without signal to be sent\n");
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (void *)1, 0) != -1);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_SUCCESS(wpid = TWAIT_GENERIC(child, &status, 0), child);

	validate_status_exited(status, exitval);

	printf("Before calling %s() for the child\n", TWAIT_FNAME);
	TWAIT_REQUIRE_FAILURE(ECHILD, wpid = TWAIT_GENERIC(child, &status, 0));
}
#endif

ATF_TP_ADD_TCS(tp)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	ATF_TP_ADD_TC_HAVE_GPREGS(tp, regs1);

	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_count);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_read);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_write_unmodified);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_code0);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_code1);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_code2);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_code3);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_write0);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_write1);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_write2);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_write3);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_rw0);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_rw1);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_rw2);
	ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(tp, watchpoint_trap_data_rw3);

	return atf_no_error();
}
