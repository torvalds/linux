/* Copyright (c) 2008 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "atf-c/detail/process.h"

#include <sys/types.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/defs.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/detail/test_helpers.h"

atf_error_t atf_process_status_init(atf_process_status_t *, int);

/* ---------------------------------------------------------------------
 * Auxiliary functions for testing of 'atf_process_fork'.
 * --------------------------------------------------------------------- */

/*
 * Testing of atf_process_fork is quite messy.  We want to be able to test
 * all the possible combinations of stdout and stderr behavior to ensure
 * that the streams are manipulated correctly.
 *
 * To do this, the do_fork function is a wrapper for atf_process_fork that
 * issues stream-specific hooks before fork, while the child is running and
 * after the child terminates.  We then provide test cases that just call
 * do_fork with different hooks.
 *
 * The hooks are described by base_stream, and we then have one *_stream
 * type for ever possible stream behavior.
 */

enum out_type { stdout_type, stderr_type };

struct base_stream {
    void (*init)(void *);
    void (*process)(void *, atf_process_child_t *);
    void (*fini)(void *);

    /* m_sb is initialized by subclasses that need it, but all consumers
     * must use m_sb_ptr, which may or may not point to m_sb.  This allows
     * us to test the interface with a NULL value, which triggers a
     * default behavior. */
    atf_process_stream_t m_sb;
    atf_process_stream_t *m_sb_ptr;
    enum out_type m_type;
};
#define BASE_STREAM(ihook, phook, fhook, type) \
    { .init = ihook, \
      .process = phook, \
      .fini = fhook, \
      .m_type = type }

static
void
check_file(const enum out_type type)
{
    switch (type) {
    case stdout_type:
        ATF_CHECK(atf_utils_grep_file("stdout: msg", "stdout"));
        ATF_CHECK(!atf_utils_grep_file("stderr: msg", "stdout"));
        break;
    case stderr_type:
        ATF_CHECK(atf_utils_grep_file("stderr: msg", "stderr"));
        ATF_CHECK(!atf_utils_grep_file("stdout: msg", "stderr"));
        break;
    default:
        UNREACHABLE;
    }
}

struct capture_stream {
    struct base_stream m_base;

    char *m_msg;
};
#define CAPTURE_STREAM(type) \
    { .m_base = BASE_STREAM(capture_stream_init, \
                            capture_stream_process, \
                            capture_stream_fini, \
                            type) }

static
void
capture_stream_init(void *v)
{
    struct capture_stream *s = v;

    s->m_base.m_sb_ptr = &s->m_base.m_sb;
    RE(atf_process_stream_init_capture(&s->m_base.m_sb));
    s->m_msg = NULL;
}

static
void
capture_stream_process(void *v, atf_process_child_t *c)
{
    struct capture_stream *s = v;

    switch (s->m_base.m_type) {
    case stdout_type:
        s->m_msg = atf_utils_readline(atf_process_child_stdout(c));
        break;
    case stderr_type:
        s->m_msg = atf_utils_readline(atf_process_child_stderr(c));
        break;
    default:
        UNREACHABLE;
    }
}

static
void
capture_stream_fini(void *v)
{
    struct capture_stream *s = v;

    switch (s->m_base.m_type) {
    case stdout_type:
        ATF_CHECK(atf_utils_grep_string("stdout: msg", s->m_msg));
        ATF_CHECK(!atf_utils_grep_string("stderr: msg", s->m_msg));
        break;
    case stderr_type:
        ATF_CHECK(!atf_utils_grep_string("stdout: msg", s->m_msg));
        ATF_CHECK(atf_utils_grep_string("stderr: msg", s->m_msg));
        break;
    default:
        UNREACHABLE;
    }

    free(s->m_msg);
    atf_process_stream_fini(&s->m_base.m_sb);
}

struct connect_stream {
    struct base_stream m_base;

    int m_fd;
};
#define CONNECT_STREAM(type) \
    { .m_base = BASE_STREAM(connect_stream_init, \
                            NULL, \
                            connect_stream_fini, \
                            type) }

static
void
connect_stream_init(void *v)
{
    struct connect_stream *s = v;
    int src_fd;

    switch (s->m_base.m_type) {
    case stdout_type:
        src_fd = STDOUT_FILENO;
        s->m_fd = open("stdout", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        break;
    case stderr_type:
        src_fd = STDERR_FILENO;
        s->m_fd = open("stderr", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        break;
    default:
        UNREACHABLE;
        src_fd = -1;
    }
    ATF_REQUIRE(s->m_fd != -1);

    s->m_base.m_sb_ptr = &s->m_base.m_sb;
    RE(atf_process_stream_init_connect(&s->m_base.m_sb, src_fd, s->m_fd));
}

static
void
connect_stream_fini(void *v)
{
    struct connect_stream *s = v;

    ATF_REQUIRE(close(s->m_fd) != -1);

    atf_process_stream_fini(&s->m_base.m_sb);

    check_file(s->m_base.m_type);
}

struct inherit_stream {
    struct base_stream m_base;
    int m_fd;

    int m_old_fd;
};
#define INHERIT_STREAM(type) \
    { .m_base = BASE_STREAM(inherit_stream_init, \
                            NULL, \
                            inherit_stream_fini, \
                            type) }

static
void
inherit_stream_init(void *v)
{
    struct inherit_stream *s = v;
    const char *name;

    s->m_base.m_sb_ptr = &s->m_base.m_sb;
    RE(atf_process_stream_init_inherit(&s->m_base.m_sb));

    switch (s->m_base.m_type) {
    case stdout_type:
        s->m_fd = STDOUT_FILENO;
        name = "stdout";
        break;
    case stderr_type:
        s->m_fd = STDERR_FILENO;
        name = "stderr";
        break;
    default:
        UNREACHABLE;
        name = NULL;
    }

    s->m_old_fd = dup(s->m_fd);
    ATF_REQUIRE(s->m_old_fd != -1);
    ATF_REQUIRE(close(s->m_fd) != -1);
    ATF_REQUIRE_EQ(open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644),
                   s->m_fd);
}

static
void
inherit_stream_fini(void *v)
{
    struct inherit_stream *s = v;

    ATF_REQUIRE(dup2(s->m_old_fd, s->m_fd) != -1);
    ATF_REQUIRE(close(s->m_old_fd) != -1);

    atf_process_stream_fini(&s->m_base.m_sb);

    check_file(s->m_base.m_type);
}

#define default_stream inherit_stream
#define DEFAULT_STREAM(type) \
    { .m_base = BASE_STREAM(default_stream_init, \
                            NULL, \
                            default_stream_fini, \
                            type) }

static
void
default_stream_init(void *v)
{
    struct inherit_stream *s = v;

    inherit_stream_init(v);
    s->m_base.m_sb_ptr = NULL;
}

static
void
default_stream_fini(void *v)
{
    inherit_stream_fini(v);
}

struct redirect_fd_stream {
    struct base_stream m_base;

    int m_fd;
};
#define REDIRECT_FD_STREAM(type) \
    { .m_base = BASE_STREAM(redirect_fd_stream_init, \
                            NULL, \
                            redirect_fd_stream_fini, \
                            type) }

static
void
redirect_fd_stream_init(void *v)
{
    struct redirect_fd_stream *s = v;

    switch (s->m_base.m_type) {
    case stdout_type:
        s->m_fd = open("stdout", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        break;
    case stderr_type:
        s->m_fd = open("stderr", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        break;
    default:
        UNREACHABLE;
    }
    ATF_REQUIRE(s->m_fd != -1);

    s->m_base.m_sb_ptr = &s->m_base.m_sb;
    RE(atf_process_stream_init_redirect_fd(&s->m_base.m_sb, s->m_fd));
}

static
void
redirect_fd_stream_fini(void *v)
{
    struct redirect_fd_stream *s = v;

    ATF_REQUIRE(close(s->m_fd) != -1);

    atf_process_stream_fini(&s->m_base.m_sb);

    check_file(s->m_base.m_type);
}

struct redirect_path_stream {
    struct base_stream m_base;

    atf_fs_path_t m_path;
};
#define REDIRECT_PATH_STREAM(type) \
    { .m_base = BASE_STREAM(redirect_path_stream_init, \
                            NULL, \
                            redirect_path_stream_fini, \
                            type) }

static
void
redirect_path_stream_init(void *v)
{
    struct redirect_path_stream *s = v;

    switch (s->m_base.m_type) {
    case stdout_type:
        RE(atf_fs_path_init_fmt(&s->m_path, "stdout"));
        break;
    case stderr_type:
        RE(atf_fs_path_init_fmt(&s->m_path, "stderr"));
        break;
    default:
        UNREACHABLE;
    }

    s->m_base.m_sb_ptr = &s->m_base.m_sb;
    RE(atf_process_stream_init_redirect_path(&s->m_base.m_sb, &s->m_path));
}

static
void
redirect_path_stream_fini(void *v)
{
    struct redirect_path_stream *s = v;

    atf_process_stream_fini(&s->m_base.m_sb);

    atf_fs_path_fini(&s->m_path);

    check_file(s->m_base.m_type);
}

static void child_print(void *) ATF_DEFS_ATTRIBUTE_NORETURN;

struct child_print_data {
    const char *m_msg;
};

static
void
child_print(void *v)
{
    struct child_print_data *cpd = v;

    fprintf(stdout, "stdout: %s\n", cpd->m_msg);
    fprintf(stderr, "stderr: %s\n", cpd->m_msg);

    exit(EXIT_SUCCESS);
}

static
void
do_fork(const struct base_stream *outfs, void *out,
        const struct base_stream *errfs, void *err)
{
    atf_process_child_t child;
    atf_process_status_t status;
    struct child_print_data cpd = { "msg" };

    outfs->init(out);
    errfs->init(err);

    RE(atf_process_fork(&child, child_print, outfs->m_sb_ptr,
                        errfs->m_sb_ptr, &cpd));
    if (outfs->process != NULL)
        outfs->process(out, &child);
    if (errfs->process != NULL)
        errfs->process(err, &child);
    RE(atf_process_child_wait(&child, &status));

    outfs->fini(out);
    errfs->fini(err);

    atf_process_status_fini(&status);
}

/* ---------------------------------------------------------------------
 * Test cases for the "stream" type.
 * --------------------------------------------------------------------- */

ATF_TC(stream_init_capture);
ATF_TC_HEAD(stream_init_capture, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the "
                      "atf_process_stream_init_capture function");
}
ATF_TC_BODY(stream_init_capture, tc)
{
    atf_process_stream_t sb;

    RE(atf_process_stream_init_capture(&sb));

    ATF_CHECK_EQ(atf_process_stream_type(&sb),
                 atf_process_stream_type_capture);

    atf_process_stream_fini(&sb);
}

ATF_TC(stream_init_connect);
ATF_TC_HEAD(stream_init_connect, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the "
                      "atf_process_stream_init_connect function");
}
ATF_TC_BODY(stream_init_connect, tc)
{
    atf_process_stream_t sb;

    RE(atf_process_stream_init_connect(&sb, 1, 2));

    ATF_CHECK_EQ(atf_process_stream_type(&sb),
                 atf_process_stream_type_connect);

    atf_process_stream_fini(&sb);
}

ATF_TC(stream_init_inherit);
ATF_TC_HEAD(stream_init_inherit, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the "
                      "atf_process_stream_init_inherit function");
}
ATF_TC_BODY(stream_init_inherit, tc)
{
    atf_process_stream_t sb;

    RE(atf_process_stream_init_inherit(&sb));

    ATF_CHECK_EQ(atf_process_stream_type(&sb),
                 atf_process_stream_type_inherit);

    atf_process_stream_fini(&sb);
}

ATF_TC(stream_init_redirect_fd);
ATF_TC_HEAD(stream_init_redirect_fd, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the "
                      "atf_process_stream_init_redirect_fd function");
}
ATF_TC_BODY(stream_init_redirect_fd, tc)
{
    atf_process_stream_t sb;

    RE(atf_process_stream_init_redirect_fd(&sb, 1));

    ATF_CHECK_EQ(atf_process_stream_type(&sb),
                 atf_process_stream_type_redirect_fd);

    atf_process_stream_fini(&sb);
}

ATF_TC(stream_init_redirect_path);
ATF_TC_HEAD(stream_init_redirect_path, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the "
                      "atf_process_stream_init_redirect_path function");
}
ATF_TC_BODY(stream_init_redirect_path, tc)
{
    atf_process_stream_t sb;
    atf_fs_path_t path;

    RE(atf_fs_path_init_fmt(&path, "foo"));
    RE(atf_process_stream_init_redirect_path(&sb, &path));

    ATF_CHECK_EQ(atf_process_stream_type(&sb),
                 atf_process_stream_type_redirect_path);

    atf_process_stream_fini(&sb);
    atf_fs_path_fini(&path);
}

/* ---------------------------------------------------------------------
 * Test cases for the "status" type.
 * --------------------------------------------------------------------- */

static void child_exit_success(void) ATF_DEFS_ATTRIBUTE_NORETURN;
static void child_exit_failure(void) ATF_DEFS_ATTRIBUTE_NORETURN;
static void child_sigkill(void) ATF_DEFS_ATTRIBUTE_NORETURN;
static void child_sigquit(void) ATF_DEFS_ATTRIBUTE_NORETURN;
static void child_sigterm(void) ATF_DEFS_ATTRIBUTE_NORETURN;

void
child_exit_success(void)
{
    exit(EXIT_SUCCESS);
}

void
child_exit_failure(void)
{
    exit(EXIT_FAILURE);
}

void
child_sigkill(void)
{
    kill(getpid(), SIGKILL);
    abort();
}

void
child_sigquit(void)
{
    kill(getpid(), SIGQUIT);
    abort();
}

void
child_sigterm(void)
{
    kill(getpid(), SIGTERM);
    abort();
}

static
int
fork_and_wait_child(void (*child_func)(void))
{
    pid_t pid;
    int status;

    pid = fork();
    ATF_REQUIRE(pid != -1);
    if (pid == 0) {
        status = 0; /* Silence compiler warnings */
        child_func();
        UNREACHABLE;
    } else {
        ATF_REQUIRE(waitpid(pid, &status, 0) != 0);
    }

    return status;
}

ATF_TC(status_exited);
ATF_TC_HEAD(status_exited, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the status type for processes "
                      "that exit cleanly");
}
ATF_TC_BODY(status_exited, tc)
{
    {
        const int rawstatus = fork_and_wait_child(child_exit_success);
        atf_process_status_t s;
        RE(atf_process_status_init(&s, rawstatus));
        ATF_CHECK(atf_process_status_exited(&s));
        ATF_CHECK_EQ(atf_process_status_exitstatus(&s), EXIT_SUCCESS);
        ATF_CHECK(!atf_process_status_signaled(&s));
        atf_process_status_fini(&s);
    }

    {
        const int rawstatus = fork_and_wait_child(child_exit_failure);
        atf_process_status_t s;
        RE(atf_process_status_init(&s, rawstatus));
        ATF_CHECK(atf_process_status_exited(&s));
        ATF_CHECK_EQ(atf_process_status_exitstatus(&s), EXIT_FAILURE);
        ATF_CHECK(!atf_process_status_signaled(&s));
        atf_process_status_fini(&s);
    }
}

ATF_TC(status_signaled);
ATF_TC_HEAD(status_signaled, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the status type for processes "
                      "that end due to a signal");
}
ATF_TC_BODY(status_signaled, tc)
{
    {
        const int rawstatus = fork_and_wait_child(child_sigkill);
        atf_process_status_t s;
        RE(atf_process_status_init(&s, rawstatus));
        ATF_CHECK(!atf_process_status_exited(&s));
        ATF_CHECK(atf_process_status_signaled(&s));
        ATF_CHECK_EQ(atf_process_status_termsig(&s), SIGKILL);
        ATF_CHECK(!atf_process_status_coredump(&s));
        atf_process_status_fini(&s);
    }

    {
        const int rawstatus = fork_and_wait_child(child_sigterm);
        atf_process_status_t s;
        RE(atf_process_status_init(&s, rawstatus));
        ATF_CHECK(!atf_process_status_exited(&s));
        ATF_CHECK(atf_process_status_signaled(&s));
        ATF_CHECK_EQ(atf_process_status_termsig(&s), SIGTERM);
        ATF_CHECK(!atf_process_status_coredump(&s));
        atf_process_status_fini(&s);
    }
}

ATF_TC(status_coredump);
ATF_TC_HEAD(status_coredump, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the status type for processes "
                      "that crash");
}
ATF_TC_BODY(status_coredump, tc)
{
    struct rlimit rl;
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &rl) == -1)
        atf_tc_skip("Cannot unlimit the core file size; check limits "
                    "manually");

#ifdef __FreeBSD__
	int coredump_enabled;
	size_t ce_len = sizeof(coredump_enabled);
	if (sysctlbyname("kern.coredump", &coredump_enabled, &ce_len, NULL,
	    0) == 0 && !coredump_enabled)
		atf_tc_skip("Coredumps disabled");
#endif

    const int rawstatus = fork_and_wait_child(child_sigquit);
    atf_process_status_t s;
    RE(atf_process_status_init(&s, rawstatus));
    ATF_CHECK(!atf_process_status_exited(&s));
    ATF_CHECK(atf_process_status_signaled(&s));
    ATF_CHECK_EQ(atf_process_status_termsig(&s), SIGQUIT);
    ATF_CHECK(atf_process_status_coredump(&s));
    atf_process_status_fini(&s);
}

/* ---------------------------------------------------------------------
 * Test cases for the "child" type.
 * --------------------------------------------------------------------- */

static void child_report_pid(void *) ATF_DEFS_ATTRIBUTE_NORETURN;

static
void
child_report_pid(void *v ATF_DEFS_ATTRIBUTE_UNUSED)
{
    const pid_t pid = getpid();
    if (write(STDOUT_FILENO, &pid, sizeof(pid)) != sizeof(pid))
        abort();
    fprintf(stderr, "Reporting %d to parent\n", (int)getpid());
    exit(EXIT_SUCCESS);
}

ATF_TC(child_pid);
ATF_TC_HEAD(child_pid, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the correctness of the pid "
                      "stored in the child type");
}
ATF_TC_BODY(child_pid, tc)
{
    atf_process_stream_t outsb, errsb;
    atf_process_child_t child;
    atf_process_status_t status;
    pid_t pid;

    RE(atf_process_stream_init_capture(&outsb));
    RE(atf_process_stream_init_inherit(&errsb));

    RE(atf_process_fork(&child, child_report_pid, &outsb, &errsb, NULL));
    ATF_CHECK_EQ(read(atf_process_child_stdout(&child), &pid, sizeof(pid)),
                 sizeof(pid));
    printf("Expected PID: %d\n", (int)atf_process_child_pid(&child));
    printf("Actual PID: %d\n", (int)pid);
    ATF_CHECK_EQ(atf_process_child_pid(&child), pid);

    RE(atf_process_child_wait(&child, &status));
    atf_process_status_fini(&status);

    atf_process_stream_fini(&outsb);
    atf_process_stream_fini(&errsb);
}

static
void
child_loop(void *v ATF_DEFS_ATTRIBUTE_UNUSED)
{
    for (;;)
        sleep(1);
}

static
void
nop_signal(int sig ATF_DEFS_ATTRIBUTE_UNUSED)
{
}

static
void
child_spawn_loop_and_wait_eintr(void *v ATF_DEFS_ATTRIBUTE_UNUSED)
{
    atf_process_child_t child;
    atf_process_status_t status;
    struct sigaction sighup, old_sighup;

#define RE_ABORT(expr) \
    do { \
        atf_error_t _aux_err = expr; \
        if (atf_is_error(_aux_err)) { \
            atf_error_free(_aux_err); \
            abort(); \
        } \
    } while (0)

    {
        atf_process_stream_t outsb, errsb;

        RE_ABORT(atf_process_stream_init_capture(&outsb));
        RE_ABORT(atf_process_stream_init_inherit(&errsb));
        RE_ABORT(atf_process_fork(&child, child_loop, &outsb, &errsb, NULL));
        atf_process_stream_fini(&outsb);
        atf_process_stream_fini(&errsb);
    }

    sighup.sa_handler = nop_signal;
    sigemptyset(&sighup.sa_mask);
    sighup.sa_flags = 0;
    if (sigaction(SIGHUP, &sighup, &old_sighup) == -1)
        abort();

    printf("waiting\n");
    fflush(stdout);

    fprintf(stderr, "Child entering wait(2)\n");
    atf_error_t err = atf_process_child_wait(&child, &status);
    fprintf(stderr, "Child's wait(2) terminated\n");
    if (!atf_is_error(err)) {
        fprintf(stderr, "wait completed successfully (not interrupted)\n");
        abort();
    }
    if (!atf_error_is(err, "libc")) {
        fprintf(stderr, "wait did not raise libc_error\n");
        abort();
    }
    if (atf_libc_error_code(err) != EINTR) {
        fprintf(stderr, "libc_error is not EINTR\n");
        abort();
    }
    atf_error_free(err);

    sigaction(SIGHUP, &old_sighup, NULL);

    fprintf(stderr, "Child is killing subchild\n");
    kill(atf_process_child_pid(&child), SIGTERM);

    RE_ABORT(atf_process_child_wait(&child, &status));
    atf_process_status_fini(&status);

#undef RE_ABORT

    exit(EXIT_SUCCESS);
}

ATF_TC(child_wait_eintr);
ATF_TC_HEAD(child_wait_eintr, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the interruption of the wait "
                      "method by an external signal, and the return of "
                      "an EINTR error");
    atf_tc_set_md_var(tc, "timeout", "30");
}
ATF_TC_BODY(child_wait_eintr, tc)
{
    atf_process_child_t child;
    atf_process_status_t status;

    {
        atf_process_stream_t outsb, errsb;

        RE(atf_process_stream_init_capture(&outsb));
        RE(atf_process_stream_init_inherit(&errsb));
        RE(atf_process_fork(&child, child_spawn_loop_and_wait_eintr,
                            &outsb, &errsb, NULL));
        atf_process_stream_fini(&outsb);
        atf_process_stream_fini(&errsb);
    }

    {
        /* Wait until the child process performs the wait call.  This is
         * racy, because the message we get from it is sent *before*
         * doing the real system call... but I can't figure any other way
         * to do this. */
        char buf[16];
        printf("Waiting for child to issue wait(2)\n");
        ATF_REQUIRE(read(atf_process_child_stdout(&child), buf,
                         sizeof(buf)) > 0);
        sleep(1);
    }

    printf("Interrupting child's wait(2) call\n");
    kill(atf_process_child_pid(&child), SIGHUP);

    printf("Waiting for child's completion\n");
    RE(atf_process_child_wait(&child, &status));
    ATF_REQUIRE(atf_process_status_exited(&status));
    ATF_REQUIRE_EQ(atf_process_status_exitstatus(&status), EXIT_SUCCESS);
    atf_process_status_fini(&status);
}

/* ---------------------------------------------------------------------
 * Tests cases for the free functions.
 * --------------------------------------------------------------------- */

static
void
do_exec(const atf_tc_t *tc, const char *helper_name, atf_process_status_t *s,
        void (*prehook)(void))
{
    atf_fs_path_t process_helpers;
    const char *argv[3];

    get_process_helpers_path(tc, true, &process_helpers);

    argv[0] = atf_fs_path_cstring(&process_helpers);
    argv[1] = helper_name;
    argv[2] = NULL;
    printf("Executing %s %s\n", argv[0], argv[1]);

    RE(atf_process_exec_array(s, &process_helpers, argv, NULL, NULL, prehook));
    atf_fs_path_fini(&process_helpers);
}

static
void
check_line(int fd, const char *exp)
{
    char *line = atf_utils_readline(fd);
    ATF_CHECK(line != NULL);
    ATF_CHECK_STREQ_MSG(exp, line, "read: '%s', expected: '%s'", line, exp);
    free(line);
}

ATF_TC(exec_failure);
ATF_TC_HEAD(exec_failure, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests execing a command");
}
ATF_TC_BODY(exec_failure, tc)
{
    atf_process_status_t status;

    do_exec(tc, "exit-failure", &status, NULL);
    ATF_CHECK(atf_process_status_exited(&status));
    ATF_CHECK_EQ(atf_process_status_exitstatus(&status), EXIT_FAILURE);
    atf_process_status_fini(&status);
}

ATF_TC(exec_list);
ATF_TC_HEAD(exec_list, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests execing a command");
}
ATF_TC_BODY(exec_list, tc)
{
    atf_fs_path_t process_helpers;
    atf_list_t argv;
    atf_process_status_t status;

    RE(atf_list_init(&argv));

    get_process_helpers_path(tc, true, &process_helpers);
    atf_list_append(&argv, strdup(atf_fs_path_cstring(&process_helpers)), true);
    atf_list_append(&argv, strdup("echo"), true);
    atf_list_append(&argv, strdup("test-message"), true);
    {
        atf_fs_path_t outpath;
        atf_process_stream_t outsb;

        RE(atf_fs_path_init_fmt(&outpath, "stdout"));
        RE(atf_process_stream_init_redirect_path(&outsb, &outpath));
        RE(atf_process_exec_list(&status, &process_helpers, &argv, &outsb,
                                 NULL, NULL));
        atf_process_stream_fini(&outsb);
        atf_fs_path_fini(&outpath);
    }
    atf_list_fini(&argv);

    ATF_CHECK(atf_process_status_exited(&status));
    ATF_CHECK_EQ(atf_process_status_exitstatus(&status), EXIT_SUCCESS);

    {
        int fd = open("stdout", O_RDONLY);
        ATF_CHECK(fd != -1);
        check_line(fd, "test-message");
        close(fd);
    }

    atf_process_status_fini(&status);
    atf_fs_path_fini(&process_helpers);
}

static void
exit_early(void)
{
    exit(80);
}

ATF_TC(exec_prehook);
ATF_TC_HEAD(exec_prehook, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests execing a command with a prehook");
}
ATF_TC_BODY(exec_prehook, tc)
{
    atf_process_status_t status;

    do_exec(tc, "exit-success", &status, exit_early);
    ATF_CHECK(atf_process_status_exited(&status));
    ATF_CHECK_EQ(atf_process_status_exitstatus(&status), 80);
    atf_process_status_fini(&status);
}

ATF_TC(exec_success);
ATF_TC_HEAD(exec_success, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests execing a command");
}
ATF_TC_BODY(exec_success, tc)
{
    atf_process_status_t status;

    do_exec(tc, "exit-success", &status, NULL);
    ATF_CHECK(atf_process_status_exited(&status));
    ATF_CHECK_EQ(atf_process_status_exitstatus(&status), EXIT_SUCCESS);
    atf_process_status_fini(&status);
}

static const int exit_v_null = 1;
static const int exit_v_notnull = 2;

static
void
child_cookie(void *v)
{
    if (v == NULL)
        exit(exit_v_null);
    else
        exit(exit_v_notnull);

    UNREACHABLE;
}

ATF_TC(fork_cookie);
ATF_TC_HEAD(fork_cookie, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests forking a child, with "
                      "a null and non-null data cookie");
}
ATF_TC_BODY(fork_cookie, tc)
{
    atf_process_stream_t outsb, errsb;

    RE(atf_process_stream_init_inherit(&outsb));
    RE(atf_process_stream_init_inherit(&errsb));

    {
        atf_process_child_t child;
        atf_process_status_t status;

        RE(atf_process_fork(&child, child_cookie, &outsb, &errsb, NULL));
        RE(atf_process_child_wait(&child, &status));

        ATF_CHECK(atf_process_status_exited(&status));
        ATF_CHECK_EQ(atf_process_status_exitstatus(&status), exit_v_null);

        atf_process_status_fini(&status);
    }

    {
        atf_process_child_t child;
        atf_process_status_t status;
        int dummy_int;

        RE(atf_process_fork(&child, child_cookie, &outsb, &errsb, &dummy_int));
        RE(atf_process_child_wait(&child, &status));

        ATF_CHECK(atf_process_status_exited(&status));
        ATF_CHECK_EQ(atf_process_status_exitstatus(&status), exit_v_notnull);

        atf_process_status_fini(&status);
    }

    atf_process_stream_fini(&errsb);
    atf_process_stream_fini(&outsb);
}

#define TC_FORK_STREAMS(outlc, outuc, errlc, erruc) \
    ATF_TC(fork_out_ ## outlc ## _err_ ## errlc); \
    ATF_TC_HEAD(fork_out_ ## outlc ## _err_ ## errlc, tc) \
    { \
        atf_tc_set_md_var(tc, "descr", "Tests forking a child, with " \
                          "stdout " #outlc " and stderr " #errlc); \
    } \
    ATF_TC_BODY(fork_out_ ## outlc ## _err_ ## errlc, tc) \
    { \
        struct outlc ## _stream out = outuc ## _STREAM(stdout_type); \
        struct errlc ## _stream err = erruc ## _STREAM(stderr_type); \
        do_fork(&out.m_base, &out, &err.m_base, &err); \
    }

TC_FORK_STREAMS(capture, CAPTURE, capture, CAPTURE);
TC_FORK_STREAMS(capture, CAPTURE, connect, CONNECT);
TC_FORK_STREAMS(capture, CAPTURE, default, DEFAULT);
TC_FORK_STREAMS(capture, CAPTURE, inherit, INHERIT);
TC_FORK_STREAMS(capture, CAPTURE, redirect_fd, REDIRECT_FD);
TC_FORK_STREAMS(capture, CAPTURE, redirect_path, REDIRECT_PATH);
TC_FORK_STREAMS(connect, CONNECT, capture, CAPTURE);
TC_FORK_STREAMS(connect, CONNECT, connect, CONNECT);
TC_FORK_STREAMS(connect, CONNECT, default, DEFAULT);
TC_FORK_STREAMS(connect, CONNECT, inherit, INHERIT);
TC_FORK_STREAMS(connect, CONNECT, redirect_fd, REDIRECT_FD);
TC_FORK_STREAMS(connect, CONNECT, redirect_path, REDIRECT_PATH);
TC_FORK_STREAMS(default, DEFAULT, capture, CAPTURE);
TC_FORK_STREAMS(default, DEFAULT, connect, CONNECT);
TC_FORK_STREAMS(default, DEFAULT, default, DEFAULT);
TC_FORK_STREAMS(default, DEFAULT, inherit, INHERIT);
TC_FORK_STREAMS(default, DEFAULT, redirect_fd, REDIRECT_FD);
TC_FORK_STREAMS(default, DEFAULT, redirect_path, REDIRECT_PATH);
TC_FORK_STREAMS(inherit, INHERIT, capture, CAPTURE);
TC_FORK_STREAMS(inherit, INHERIT, connect, CONNECT);
TC_FORK_STREAMS(inherit, INHERIT, default, DEFAULT);
TC_FORK_STREAMS(inherit, INHERIT, inherit, INHERIT);
TC_FORK_STREAMS(inherit, INHERIT, redirect_fd, REDIRECT_FD);
TC_FORK_STREAMS(inherit, INHERIT, redirect_path, REDIRECT_PATH);
TC_FORK_STREAMS(redirect_fd, REDIRECT_FD, capture, CAPTURE);
TC_FORK_STREAMS(redirect_fd, REDIRECT_FD, connect, CONNECT);
TC_FORK_STREAMS(redirect_fd, REDIRECT_FD, default, DEFAULT);
TC_FORK_STREAMS(redirect_fd, REDIRECT_FD, inherit, INHERIT);
TC_FORK_STREAMS(redirect_fd, REDIRECT_FD, redirect_fd, REDIRECT_FD);
TC_FORK_STREAMS(redirect_fd, REDIRECT_FD, redirect_path, REDIRECT_PATH);
TC_FORK_STREAMS(redirect_path, REDIRECT_PATH, capture, CAPTURE);
TC_FORK_STREAMS(redirect_path, REDIRECT_PATH, connect, CONNECT);
TC_FORK_STREAMS(redirect_path, REDIRECT_PATH, default, DEFAULT);
TC_FORK_STREAMS(redirect_path, REDIRECT_PATH, inherit, INHERIT);
TC_FORK_STREAMS(redirect_path, REDIRECT_PATH, redirect_fd, REDIRECT_FD);
TC_FORK_STREAMS(redirect_path, REDIRECT_PATH, redirect_path, REDIRECT_PATH);

#undef TC_FORK_STREAMS

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Add the tests for the "stream" type. */
    ATF_TP_ADD_TC(tp, stream_init_capture);
    ATF_TP_ADD_TC(tp, stream_init_connect);
    ATF_TP_ADD_TC(tp, stream_init_inherit);
    ATF_TP_ADD_TC(tp, stream_init_redirect_fd);
    ATF_TP_ADD_TC(tp, stream_init_redirect_path);

    /* Add the tests for the "status" type. */
    ATF_TP_ADD_TC(tp, status_exited);
    ATF_TP_ADD_TC(tp, status_signaled);
    ATF_TP_ADD_TC(tp, status_coredump);

    /* Add the tests for the "child" type. */
    ATF_TP_ADD_TC(tp, child_pid);
    ATF_TP_ADD_TC(tp, child_wait_eintr);

    /* Add the tests for the free functions. */
    ATF_TP_ADD_TC(tp, exec_failure);
    ATF_TP_ADD_TC(tp, exec_list);
    ATF_TP_ADD_TC(tp, exec_prehook);
    ATF_TP_ADD_TC(tp, exec_success);
    ATF_TP_ADD_TC(tp, fork_cookie);
    ATF_TP_ADD_TC(tp, fork_out_capture_err_capture);
    ATF_TP_ADD_TC(tp, fork_out_capture_err_connect);
    ATF_TP_ADD_TC(tp, fork_out_capture_err_default);
    ATF_TP_ADD_TC(tp, fork_out_capture_err_inherit);
    ATF_TP_ADD_TC(tp, fork_out_capture_err_redirect_fd);
    ATF_TP_ADD_TC(tp, fork_out_capture_err_redirect_path);
    ATF_TP_ADD_TC(tp, fork_out_connect_err_capture);
    ATF_TP_ADD_TC(tp, fork_out_connect_err_connect);
    ATF_TP_ADD_TC(tp, fork_out_connect_err_default);
    ATF_TP_ADD_TC(tp, fork_out_connect_err_inherit);
    ATF_TP_ADD_TC(tp, fork_out_connect_err_redirect_fd);
    ATF_TP_ADD_TC(tp, fork_out_connect_err_redirect_path);
    ATF_TP_ADD_TC(tp, fork_out_default_err_capture);
    ATF_TP_ADD_TC(tp, fork_out_default_err_connect);
    ATF_TP_ADD_TC(tp, fork_out_default_err_default);
    ATF_TP_ADD_TC(tp, fork_out_default_err_inherit);
    ATF_TP_ADD_TC(tp, fork_out_default_err_redirect_fd);
    ATF_TP_ADD_TC(tp, fork_out_default_err_redirect_path);
    ATF_TP_ADD_TC(tp, fork_out_inherit_err_capture);
    ATF_TP_ADD_TC(tp, fork_out_inherit_err_connect);
    ATF_TP_ADD_TC(tp, fork_out_inherit_err_default);
    ATF_TP_ADD_TC(tp, fork_out_inherit_err_inherit);
    ATF_TP_ADD_TC(tp, fork_out_inherit_err_redirect_fd);
    ATF_TP_ADD_TC(tp, fork_out_inherit_err_redirect_path);
    ATF_TP_ADD_TC(tp, fork_out_redirect_fd_err_capture);
    ATF_TP_ADD_TC(tp, fork_out_redirect_fd_err_connect);
    ATF_TP_ADD_TC(tp, fork_out_redirect_fd_err_default);
    ATF_TP_ADD_TC(tp, fork_out_redirect_fd_err_inherit);
    ATF_TP_ADD_TC(tp, fork_out_redirect_fd_err_redirect_fd);
    ATF_TP_ADD_TC(tp, fork_out_redirect_fd_err_redirect_path);
    ATF_TP_ADD_TC(tp, fork_out_redirect_path_err_capture);
    ATF_TP_ADD_TC(tp, fork_out_redirect_path_err_connect);
    ATF_TP_ADD_TC(tp, fork_out_redirect_path_err_default);
    ATF_TP_ADD_TC(tp, fork_out_redirect_path_err_inherit);
    ATF_TP_ADD_TC(tp, fork_out_redirect_path_err_redirect_fd);
    ATF_TP_ADD_TC(tp, fork_out_redirect_path_err_redirect_path);

    return atf_no_error();
}
