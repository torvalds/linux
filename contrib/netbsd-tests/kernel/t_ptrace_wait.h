/*	$NetBSD: t_ptrace_wait.h,v 1.7 2017/01/09 22:09:20 kamil Exp $	*/

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

/* Detect plain wait(2) use-case */
#if !defined(TWAIT_WAITPID) && \
    !defined(TWAIT_WAITID) && \
    !defined(TWAIT_WAIT3) && \
    !defined(TWAIT_WAIT4) && \
    !defined(TWAIT_WAIT6)
#define TWAIT_WAIT
#endif

/*
 * There are two classes of wait(2)-like functions:
 * - wait4(2)-like accepting pid_t, optional options parameter, struct rusage*
 * - wait6(2)-like accepting idtype_t, id_t, struct wrusage, mandatory options
 *
 * The TWAIT_FNAME value is to be used for convenience in debug messages.
 *
 * The TWAIT_GENERIC() macro is designed to reuse the same unmodified
 * code with as many wait(2)-like functions as possible.
 *
 * In a common use-case wait4(2) and wait6(2)-like function can work the almost
 * the same way, however there are few important differences:
 * wait6(2) must specify P_PID for idtype to match wpid from wait4(2).
 * To behave like wait4(2), wait6(2) the 'options' to wait must include
 * WEXITED|WTRUNCATED.
 *
 * There are two helper macros (they purpose it to mach more than one
 * wait(2)-like function):
 * The TWAIT_HAVE_STATUS - specifies whether a function can retrieve
 *                         status (as integer value).
 * The TWAIT_HAVE_PID    - specifies whether a function can request
 *                         exact process identifier
 * The TWAIT_HAVE_RUSAGE - specifies whether a function can request
 *                         the struct rusage value
 *
 */

#if defined(TWAIT_WAIT)
#	define TWAIT_FNAME			"wait"
#	define TWAIT_WAIT4TYPE(a,b,c,d)		wait((b))
#	define TWAIT_GENERIC(a,b,c)		wait((b))
#	define TWAIT_HAVE_STATUS		1
#elif defined(TWAIT_WAITPID)
#	define TWAIT_FNAME			"waitpid"
#	define TWAIT_WAIT4TYPE(a,b,c,d)		waitpid((a),(b),(c))
#	define TWAIT_GENERIC(a,b,c)		waitpid((a),(b),(c))
#	define TWAIT_HAVE_PID			1
#	define TWAIT_HAVE_STATUS		1
#elif defined(TWAIT_WAITID)
#	define TWAIT_FNAME			"waitid"
#	define TWAIT_GENERIC(a,b,c)		\
		waitid(P_PID,(a),NULL,(c)|WEXITED|WTRAPPED)
#	define TWAIT_WAIT6TYPE(a,b,c,d,e,f)	waitid((a),(b),(f),(d))
#	define TWAIT_HAVE_PID			1
#elif defined(TWAIT_WAIT3)
#	define TWAIT_FNAME			"wait3"
#	define TWAIT_WAIT4TYPE(a,b,c,d)		wait3((b),(c),(d))
#	define TWAIT_GENERIC(a,b,c)		wait3((b),(c),NULL)
#	define TWAIT_HAVE_STATUS		1
#	define TWAIT_HAVE_RUSAGE		1
#elif defined(TWAIT_WAIT4)
#	define TWAIT_FNAME			"wait4"
#	define TWAIT_WAIT4TYPE(a,b,c,d)		wait4((a),(b),(c),(d))
#	define TWAIT_GENERIC(a,b,c)		wait4((a),(b),(c),NULL)
#	define TWAIT_HAVE_PID			1
#	define TWAIT_HAVE_STATUS		1
#	define TWAIT_HAVE_RUSAGE		1
#elif defined(TWAIT_WAIT6)
#	define TWAIT_FNAME			"wait6"
#	define TWAIT_WAIT6TYPE(a,b,c,d,e,f)	wait6((a),(b),(c),(d),(e),(f))
#	define TWAIT_GENERIC(a,b,c)		\
		wait6(P_PID,(a),(b),(c)|WEXITED|WTRAPPED,NULL,NULL)
#	define TWAIT_HAVE_PID			1
#	define TWAIT_HAVE_STATUS		1
#endif

/*
 * There are 3 groups of tests:
 * - TWAIT_GENERIC()	(wait, wait2, waitpid, wait3, wait4, wait6)
 * - TWAIT_WAIT4TYPE()	(wait2, waitpid, wait3, wait4)
 * - TWAIT_WAIT6TYPE()	(waitid, wait6)
 *
 * Tests only in the above categories are allowed. However some tests are not
 * possible in the context requested functionality to be verified, therefore
 * there are helper macros:
 * - TWAIT_HAVE_PID	(wait2, waitpid, waitid, wait4, wait6)
 * - TWAIT_HAVE_STATUS	(wait, wait2, waitpid, wait3, wait4, wait6)
 * - TWAIT_HAVE_RUSAGE	(wait3, wait4)
 * - TWAIT_HAVE_RETPID	(wait, wait2, waitpid, wait3, wait4, wait6)
 *
 * If there is an intention to test e.g. wait6(2) specific features in the
 * ptrace(2) context, find the most matching group and with #ifdefs reduce
 * functionality of less featured than wait6(2) interface (TWAIT_WAIT6TYPE).
 *
 * For clarity never use negative preprocessor checks, like:
 *     #if !defined(TWAIT_WAIT4)
 * always refer to checks for positive values.
 */

#define TEST_REQUIRE_EQ(x, y)						\
do {									\
	uintmax_t vx = (x);						\
	uintmax_t vy = (y);						\
	int ret = vx == vy;						\
	if (!ret)							\
		ATF_REQUIRE_EQ_MSG(vx, vy, "%s(%ju) == %s(%ju)", 	\
		    #x, vx, #y, vy);					\
} while (/*CONSTCOND*/0)

/*
 * A child process cannot call atf functions and expect them to magically
 * work like in the parent.
 * The printf(3) messaging from a child will not work out of the box as well
 * without estabilishing a communication protocol with its parent. To not
 * overcomplicate the tests - do not log from a child and use err(3)/errx(3)
 * wrapped with FORKEE_ASSERT()/FORKEE_ASSERTX() as that is guaranteed to work.
 */
#define FORKEE_ASSERT_EQ(x, y)						\
do {									\
	uintmax_t vx = (x);						\
	uintmax_t vy = (y);						\
	int ret = vx == vy;						\
	if (!ret)							\
		errx(EXIT_FAILURE, "%s:%d %s(): Assertion failed for: "	\
		    "%s(%ju) == %s(%ju)", __FILE__, __LINE__, __func__,	\
		    #x, vx, #y, vy);					\
} while (/*CONSTCOND*/0)

#define FORKEE_ASSERTX(x)						\
do {									\
	int ret = (x);							\
	if (!ret)							\
		errx(EXIT_FAILURE, "%s:%d %s(): Assertion failed for: %s",\
		     __FILE__, __LINE__, __func__, #x);			\
} while (/*CONSTCOND*/0)

#define FORKEE_ASSERT(x)						\
do {									\
	int ret = (x);							\
	if (!ret)							\
		err(EXIT_FAILURE, "%s:%d %s(): Assertion failed for: %s",\
		     __FILE__, __LINE__, __func__, #x);			\
} while (/*CONSTCOND*/0)

/*
 * Simplify logic for functions using general purpose registers add HAVE_GPREGS
 *
 * For platforms that do not implement all needed calls for simplicity assume
 * that they are unsupported at all.
 */
#if defined(PT_GETREGS)			\
    && defined(PT_SETREGS)		\
    && defined(PTRACE_REG_PC)		\
    && defined(PTRACE_REG_SET_PC)	\
    && defined(PTRACE_REG_SP)		\
    && defined(PTRACE_REG_INTRV)
#define HAVE_GPREGS
#endif

/* Add guards for floating point registers */
#if defined(PT_GETFPREGS)		\
    && defined(PT_SETFPREGS)
#define HAVE_FPREGS
#endif

/* Add guards for cpu debug registers */
#if defined(PT_GETDBREGS)		\
    && defined(PT_SETDBREGS)
#define HAVE_DBREGS
#endif

/*
 * If waitid(2) returns because one or more processes have a state change to
 * report, 0 is returned.  If an error is detected, a value of -1 is returned
 * and errno is set to indicate the error. If WNOHANG is specified and there
 * are no stopped, continued or exited children, 0 is returned.
 */
#if defined(TWAIT_WAITID)
#define TWAIT_REQUIRE_SUCCESS(a,b)	TEST_REQUIRE_EQ((a), 0)
#define TWAIT_REQUIRE_FAILURE(a,b)	ATF_REQUIRE_ERRNO((a),(b) == -1)
#define FORKEE_REQUIRE_SUCCESS(a,b)	FORKEE_ASSERT_EQ(a, 0)
#define FORKEE_REQUIRE_FAILURE(a,b)	\
	FORKEE_ASSERTX(((a) == errno) && ((b) == -1))
#else
#define TWAIT_REQUIRE_SUCCESS(a,b)	TEST_REQUIRE_EQ((a), (b))
#define TWAIT_REQUIRE_FAILURE(a,b)	ATF_REQUIRE_ERRNO((a),(b) == -1)
#define FORKEE_REQUIRE_SUCCESS(a,b)	FORKEE_ASSERT_EQ(a, b)
#define FORKEE_REQUIRE_FAILURE(a,b)	\
	FORKEE_ASSERTX(((a) == errno) && ((b) == -1))
#endif

/*
 * Helper tools to verify whether status reports exited value
 */
#if TWAIT_HAVE_STATUS
static void __used
validate_status_exited(int status, int expected)
{
        ATF_REQUIRE_MSG(WIFEXITED(status), "Reported !exited process");
        ATF_REQUIRE_MSG(!WIFCONTINUED(status), "Reported continued process");
        ATF_REQUIRE_MSG(!WIFSIGNALED(status), "Reported signaled process");
        ATF_REQUIRE_MSG(!WIFSTOPPED(status), "Reported stopped process");

	ATF_REQUIRE_EQ_MSG(WEXITSTATUS(status), expected,
	    "The process has exited with invalid value %d != %d",
	    WEXITSTATUS(status), expected);
}

static void __used
forkee_status_exited(int status, int expected)
{
	FORKEE_ASSERTX(WIFEXITED(status));
	FORKEE_ASSERTX(!WIFCONTINUED(status));
	FORKEE_ASSERTX(!WIFSIGNALED(status));
	FORKEE_ASSERTX(!WIFSTOPPED(status));

	FORKEE_ASSERT_EQ(WEXITSTATUS(status), expected);
}

static void __used
validate_status_continued(int status)
{
	ATF_REQUIRE_MSG(!WIFEXITED(status), "Reported exited process");
	ATF_REQUIRE_MSG(WIFCONTINUED(status), "Reported !continued process");
	ATF_REQUIRE_MSG(!WIFSIGNALED(status), "Reported signaled process");
	ATF_REQUIRE_MSG(!WIFSTOPPED(status), "Reported stopped process");
}

static void __used
forkee_status_continued(int status)
{
	FORKEE_ASSERTX(!WIFEXITED(status));
	FORKEE_ASSERTX(WIFCONTINUED(status));
	FORKEE_ASSERTX(!WIFSIGNALED(status));
	FORKEE_ASSERTX(!WIFSTOPPED(status));
}

static void __used
validate_status_signaled(int status, int expected_termsig, int expected_core)
{
	ATF_REQUIRE_MSG(!WIFEXITED(status), "Reported exited process");
	ATF_REQUIRE_MSG(!WIFCONTINUED(status), "Reported continued process");
	ATF_REQUIRE_MSG(WIFSIGNALED(status), "Reported !signaled process");
	ATF_REQUIRE_MSG(!WIFSTOPPED(status), "Reported stopped process");

	ATF_REQUIRE_EQ_MSG(WTERMSIG(status), expected_termsig,
	    "Unexpected signal received");

	ATF_REQUIRE_EQ_MSG(WCOREDUMP(status), expected_core,
	    "Unexpectedly core file %s generated", expected_core ? "not" : "");
}

static void __used
forkee_status_signaled(int status, int expected_termsig, int expected_core)
{
	FORKEE_ASSERTX(!WIFEXITED(status));
	FORKEE_ASSERTX(!WIFCONTINUED(status));
	FORKEE_ASSERTX(WIFSIGNALED(status));
	FORKEE_ASSERTX(!WIFSTOPPED(status));

	FORKEE_ASSERT_EQ(WTERMSIG(status), expected_termsig);
	FORKEE_ASSERT_EQ(WCOREDUMP(status), expected_core);
}

static void __used
validate_status_stopped(int status, int expected)
{
	ATF_REQUIRE_MSG(!WIFEXITED(status), "Reported exited process");
	ATF_REQUIRE_MSG(!WIFCONTINUED(status), "Reported continued process");
	ATF_REQUIRE_MSG(!WIFSIGNALED(status), "Reported signaled process");
	ATF_REQUIRE_MSG(WIFSTOPPED(status), "Reported !stopped process");

	char st[128], ex[128];
	strlcpy(st, strsignal(WSTOPSIG(status)), sizeof(st));
	strlcpy(ex, strsignal(expected), sizeof(ex));

	ATF_REQUIRE_EQ_MSG(WSTOPSIG(status), expected,
	    "Unexpected stop signal received [%s] != [%s]", st, ex);
}

static void __used
forkee_status_stopped(int status, int expected)
{
	FORKEE_ASSERTX(!WIFEXITED(status));
	FORKEE_ASSERTX(!WIFCONTINUED(status));
	FORKEE_ASSERTX(!WIFSIGNALED(status));
	FORKEE_ASSERTX(WIFSTOPPED(status));

	FORKEE_ASSERT_EQ(WSTOPSIG(status), expected);
}
#else
#define validate_status_exited(a,b)
#define forkee_status_exited(a,b)
#define validate_status_continued(a,b)
#define forkee_status_continued(a,b)
#define validate_status_signaled(a,b,c)
#define forkee_status_signaled(a,b,c)
#define validate_status_stopped(a,b)
#define forkee_status_stopped(a,b)
#endif

/* This function is currently designed to be run in the main/parent process */
static void __used
await_zombie(pid_t process)
{
	struct kinfo_proc2 p;
	size_t len = sizeof(p);

	const int name[] = {
		[0] = CTL_KERN,
		[1] = KERN_PROC2,
		[2] = KERN_PROC_PID,
		[3] = process,
		[4] = sizeof(p),
		[5] = 1
	};

	const size_t namelen = __arraycount(name);

	/* Await the process becoming a zombie */
	while(1) {
		ATF_REQUIRE(sysctl(name, namelen, &p, &len, NULL, 0) == 0);

		if (p.p_stat == LSZOMB)
			break;

		ATF_REQUIRE(usleep(1000) == 0);
	}
}

/* Happy number sequence -- this function is used to just consume cpu cycles */
#define	HAPPY_NUMBER	1

/* If n is not happy then its sequence ends in the cycle:
 * 4, 16, 37, 58, 89, 145, 42, 20, 4, ... */
#define	SAD_NUMBER	4

/* Calculate the sum of the squares of the digits of n */
static unsigned __used
dsum(unsigned n)
{
	unsigned sum, x;
	for (sum = 0; n; n /= 10) {
		x = n % 10;
		sum += x * x;
	}
	return sum;
}

/*
 * XXX: Disabled optimization is required to make tests for hardware assisted
 * traps in .text functional
 *
 * Tested with GCC 5.4 on NetBSD 7.99.47 amd64
 */
static int __used
#ifdef __clang__
__attribute__((__optnone__))
#else
__attribute__((__optimize__("O0")))
#endif
check_happy(unsigned n)
{
	for (;;) {
		unsigned total = dsum(n);

		if (total == HAPPY_NUMBER)
			return 1;
		if (total == SAD_NUMBER)
			return 0;

		n = total;
	}
}

#if defined(TWAIT_HAVE_PID)
#define ATF_TP_ADD_TC_HAVE_PID(a,b)	ATF_TP_ADD_TC(a,b)
#else
#define ATF_TP_ADD_TC_HAVE_PID(a,b)
#endif

#if defined(HAVE_GPREGS)
#define ATF_TP_ADD_TC_HAVE_GPREGS(a,b)	ATF_TP_ADD_TC(a,b)
#else
#define ATF_TP_ADD_TC_HAVE_GPREGS(a,b)
#endif

#if defined(HAVE_FPREGS)
#define ATF_TP_ADD_TC_HAVE_FPREGS(a,b)	ATF_TP_ADD_TC(a,b)
#else
#define ATF_TP_ADD_TC_HAVE_FPREGS(a,b)
#endif

#if defined(PT_STEP)
#define ATF_TP_ADD_TC_PT_STEP(a,b)	ATF_TP_ADD_TC(a,b)
#else
#define ATF_TP_ADD_TC_PT_STEP(a,b)
#endif

#if defined(__HAVE_PTRACE_WATCHPOINTS)
#define ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(a,b)	ATF_TP_ADD_TC(a,b)
#else
#define ATF_TP_ADD_TC_HAVE_PTRACE_WATCHPOINTS(a,b)
#endif
