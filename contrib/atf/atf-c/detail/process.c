/* Copyright (c) 2007 The NetBSD Foundation, Inc.
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
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atf-c/defs.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/error.h"

/* This prototype is not in the header file because this is a private
 * function; however, we need to access it during testing. */
atf_error_t atf_process_status_init(atf_process_status_t *, int);

/* ---------------------------------------------------------------------
 * The "stream_prepare" auxiliary type.
 * --------------------------------------------------------------------- */

struct stream_prepare {
    const atf_process_stream_t *m_sb;

    bool m_pipefds_ok;
    int m_pipefds[2];
};
typedef struct stream_prepare stream_prepare_t;

static
atf_error_t
stream_prepare_init(stream_prepare_t *sp, const atf_process_stream_t *sb)
{
    atf_error_t err;

    const int type = atf_process_stream_type(sb);

    sp->m_sb = sb;
    sp->m_pipefds_ok = false;

    if (type == atf_process_stream_type_capture) {
        if (pipe(sp->m_pipefds) == -1)
            err = atf_libc_error(errno, "Failed to create pipe");
        else {
            err = atf_no_error();
            sp->m_pipefds_ok = true;
        }
    } else
        err = atf_no_error();

    return err;
}

static
void
stream_prepare_fini(stream_prepare_t *sp)
{
    if (sp->m_pipefds_ok) {
        close(sp->m_pipefds[0]);
        close(sp->m_pipefds[1]);
    }
}

/* ---------------------------------------------------------------------
 * The "atf_process_stream" type.
 * --------------------------------------------------------------------- */

const int atf_process_stream_type_capture = 1;
const int atf_process_stream_type_connect = 2;
const int atf_process_stream_type_inherit = 3;
const int atf_process_stream_type_redirect_fd = 4;
const int atf_process_stream_type_redirect_path = 5;

static
bool
stream_is_valid(const atf_process_stream_t *sb)
{
    return (sb->m_type == atf_process_stream_type_capture) ||
           (sb->m_type == atf_process_stream_type_connect) ||
           (sb->m_type == atf_process_stream_type_inherit) ||
           (sb->m_type == atf_process_stream_type_redirect_fd) ||
           (sb->m_type == atf_process_stream_type_redirect_path);
}

atf_error_t
atf_process_stream_init_capture(atf_process_stream_t *sb)
{
    sb->m_type = atf_process_stream_type_capture;

    POST(stream_is_valid(sb));
    return atf_no_error();
}

atf_error_t
atf_process_stream_init_connect(atf_process_stream_t *sb,
                                const int src_fd, const int tgt_fd)
{
    PRE(src_fd >= 0);
    PRE(tgt_fd >= 0);
    PRE(src_fd != tgt_fd);

    sb->m_type = atf_process_stream_type_connect;
    sb->m_src_fd = src_fd;
    sb->m_tgt_fd = tgt_fd;

    POST(stream_is_valid(sb));
    return atf_no_error();
}

atf_error_t
atf_process_stream_init_inherit(atf_process_stream_t *sb)
{
    sb->m_type = atf_process_stream_type_inherit;

    POST(stream_is_valid(sb));
    return atf_no_error();
}

atf_error_t
atf_process_stream_init_redirect_fd(atf_process_stream_t *sb,
                                    const int fd)
{
    sb->m_type = atf_process_stream_type_redirect_fd;
    sb->m_fd = fd;

    POST(stream_is_valid(sb));
    return atf_no_error();
}

atf_error_t
atf_process_stream_init_redirect_path(atf_process_stream_t *sb,
                                      const atf_fs_path_t *path)
{
    sb->m_type = atf_process_stream_type_redirect_path;
    sb->m_path = path;

    POST(stream_is_valid(sb));
    return atf_no_error();
}

void
atf_process_stream_fini(atf_process_stream_t *sb)
{
    PRE(stream_is_valid(sb));
}

int
atf_process_stream_type(const atf_process_stream_t *sb)
{
    PRE(stream_is_valid(sb));

    return sb->m_type;
}

/* ---------------------------------------------------------------------
 * The "atf_process_status" type.
 * --------------------------------------------------------------------- */

atf_error_t
atf_process_status_init(atf_process_status_t *s, int status)
{
    s->m_status = status;

    return atf_no_error();
}

void
atf_process_status_fini(atf_process_status_t *s ATF_DEFS_ATTRIBUTE_UNUSED)
{
}

bool
atf_process_status_exited(const atf_process_status_t *s)
{
    int mutable_status = s->m_status;
    return WIFEXITED(mutable_status);
}

int
atf_process_status_exitstatus(const atf_process_status_t *s)
{
    PRE(atf_process_status_exited(s));
    int mutable_status = s->m_status;
    return WEXITSTATUS(mutable_status);
}

bool
atf_process_status_signaled(const atf_process_status_t *s)
{
    int mutable_status = s->m_status;
    return WIFSIGNALED(mutable_status);
}

int
atf_process_status_termsig(const atf_process_status_t *s)
{
    PRE(atf_process_status_signaled(s));
    int mutable_status = s->m_status;
    return WTERMSIG(mutable_status);
}

bool
atf_process_status_coredump(const atf_process_status_t *s)
{
    PRE(atf_process_status_signaled(s));
#if defined(WCOREDUMP)
    int mutable_status = s->m_status;
    return WCOREDUMP(mutable_status);
#else
    return false;
#endif
}

/* ---------------------------------------------------------------------
 * The "atf_process_child" type.
 * --------------------------------------------------------------------- */

static
atf_error_t
atf_process_child_init(atf_process_child_t *c)
{
    c->m_pid = 0;
    c->m_stdout = -1;
    c->m_stderr = -1;

    return atf_no_error();
}

static
void
atf_process_child_fini(atf_process_child_t *c)
{
    if (c->m_stdout != -1)
        close(c->m_stdout);
    if (c->m_stderr != -1)
        close(c->m_stderr);
}

atf_error_t
atf_process_child_wait(atf_process_child_t *c, atf_process_status_t *s)
{
    atf_error_t err;
    int status;

    if (waitpid(c->m_pid, &status, 0) == -1)
        err = atf_libc_error(errno, "Failed waiting for process %d",
                             c->m_pid);
    else {
        atf_process_child_fini(c);
        err = atf_process_status_init(s, status);
    }

    return err;
}

pid_t
atf_process_child_pid(const atf_process_child_t *c)
{
    return c->m_pid;
}

int
atf_process_child_stdout(atf_process_child_t *c)
{
    PRE(c->m_stdout != -1);
    return c->m_stdout;
}

int
atf_process_child_stderr(atf_process_child_t *c)
{
    PRE(c->m_stderr != -1);
    return c->m_stderr;
}

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

static
atf_error_t
safe_dup(const int oldfd, const int newfd)
{
    atf_error_t err;

    if (oldfd != newfd) {
        if (dup2(oldfd, newfd) == -1) {
            err = atf_libc_error(errno, "Could not allocate file descriptor");
        } else {
            close(oldfd);
            err = atf_no_error();
        }
    } else
        err = atf_no_error();

    return err;
}

static
atf_error_t
child_connect(const stream_prepare_t *sp, int procfd)
{
    atf_error_t err;
    const int type = atf_process_stream_type(sp->m_sb);

    if (type == atf_process_stream_type_capture) {
        close(sp->m_pipefds[0]);
        err = safe_dup(sp->m_pipefds[1], procfd);
    } else if (type == atf_process_stream_type_connect) {
        if (dup2(sp->m_sb->m_tgt_fd, sp->m_sb->m_src_fd) == -1)
            err = atf_libc_error(errno, "Cannot connect descriptor %d to %d",
                                 sp->m_sb->m_tgt_fd, sp->m_sb->m_src_fd);
        else
            err = atf_no_error();
    } else if (type == atf_process_stream_type_inherit) {
        err = atf_no_error();
    } else if (type == atf_process_stream_type_redirect_fd) {
        err = safe_dup(sp->m_sb->m_fd, procfd);
    } else if (type == atf_process_stream_type_redirect_path) {
        int aux = open(atf_fs_path_cstring(sp->m_sb->m_path),
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (aux == -1)
            err = atf_libc_error(errno, "Could not create %s",
                                 atf_fs_path_cstring(sp->m_sb->m_path));
        else {
            err = safe_dup(aux, procfd);
            if (atf_is_error(err))
                close(aux);
        }
    } else {
        UNREACHABLE;
        err = atf_no_error();
    }

    return err;
}

static
void
parent_connect(const stream_prepare_t *sp, int *fd)
{
    const int type = atf_process_stream_type(sp->m_sb);

    if (type == atf_process_stream_type_capture) {
        close(sp->m_pipefds[1]);
        *fd = sp->m_pipefds[0];
    } else if (type == atf_process_stream_type_connect) {
        /* Do nothing. */
    } else if (type == atf_process_stream_type_inherit) {
        /* Do nothing. */
    } else if (type == atf_process_stream_type_redirect_fd) {
        /* Do nothing. */
    } else if (type == atf_process_stream_type_redirect_path) {
        /* Do nothing. */
    } else {
        UNREACHABLE;
    }
}

static
atf_error_t
do_parent(atf_process_child_t *c,
          const pid_t pid,
          const stream_prepare_t *outsp,
          const stream_prepare_t *errsp)
{
    atf_error_t err;

    err = atf_process_child_init(c);
    if (atf_is_error(err))
        goto out;

    c->m_pid = pid;

    parent_connect(outsp, &c->m_stdout);
    parent_connect(errsp, &c->m_stderr);

out:
    return err;
}

static
void
do_child(void (*)(void *),
         void *,
         const stream_prepare_t *,
         const stream_prepare_t *) ATF_DEFS_ATTRIBUTE_NORETURN;

static
void
do_child(void (*start)(void *),
         void *v,
         const stream_prepare_t *outsp,
         const stream_prepare_t *errsp)
{
    atf_error_t err;

    err = child_connect(outsp, STDOUT_FILENO);
    if (atf_is_error(err))
        goto out;

    err = child_connect(errsp, STDERR_FILENO);
    if (atf_is_error(err))
        goto out;

    start(v);
    UNREACHABLE;

out:
    if (atf_is_error(err)) {
        char buf[1024];

        atf_error_format(err, buf, sizeof(buf));
        fprintf(stderr, "Unhandled error: %s\n", buf);
        atf_error_free(err);

        exit(EXIT_FAILURE);
    } else
        exit(EXIT_SUCCESS);
}

static
atf_error_t
fork_with_streams(atf_process_child_t *c,
                  void (*start)(void *),
                  const atf_process_stream_t *outsb,
                  const atf_process_stream_t *errsb,
                  void *v)
{
    atf_error_t err;
    stream_prepare_t outsp;
    stream_prepare_t errsp;
    pid_t pid;

    err = stream_prepare_init(&outsp, outsb);
    if (atf_is_error(err))
        goto out;

    err = stream_prepare_init(&errsp, errsb);
    if (atf_is_error(err))
        goto err_outpipe;

    pid = fork();
    if (pid == -1) {
        err = atf_libc_error(errno, "Failed to fork");
        goto err_errpipe;
    }

    if (pid == 0) {
        do_child(start, v, &outsp, &errsp);
        UNREACHABLE;
        abort();
        err = atf_no_error();
    } else {
        err = do_parent(c, pid, &outsp, &errsp);
        if (atf_is_error(err))
            goto err_errpipe;
    }

    goto out;

err_errpipe:
    stream_prepare_fini(&errsp);
err_outpipe:
    stream_prepare_fini(&outsp);

out:
    return err;
}

static
atf_error_t
init_stream_w_default(const atf_process_stream_t *usersb,
                      atf_process_stream_t *inheritsb,
                      const atf_process_stream_t **realsb)
{
    atf_error_t err;

    if (usersb == NULL) {
        err = atf_process_stream_init_inherit(inheritsb);
        if (!atf_is_error(err))
            *realsb = inheritsb;
    } else {
        err = atf_no_error();
        *realsb = usersb;
    }

    return err;
}

atf_error_t
atf_process_fork(atf_process_child_t *c,
                 void (*start)(void *),
                 const atf_process_stream_t *outsb,
                 const atf_process_stream_t *errsb,
                 void *v)
{
    atf_error_t err;
    atf_process_stream_t inherit_outsb, inherit_errsb;
    const atf_process_stream_t *real_outsb, *real_errsb;

    real_outsb = NULL;  /* Shut up GCC warning. */
    err = init_stream_w_default(outsb, &inherit_outsb, &real_outsb);
    if (atf_is_error(err))
        goto out;

    real_errsb = NULL;  /* Shut up GCC warning. */
    err = init_stream_w_default(errsb, &inherit_errsb, &real_errsb);
    if (atf_is_error(err))
        goto out_out;

    err = fork_with_streams(c, start, real_outsb, real_errsb, v);

    if (errsb == NULL)
        atf_process_stream_fini(&inherit_errsb);
out_out:
    if (outsb == NULL)
        atf_process_stream_fini(&inherit_outsb);
out:
    return err;
}

static
int
const_execvp(const char *file, const char *const *argv)
{
#define UNCONST(a) ((void *)(unsigned long)(const void *)(a))
    return execvp(file, UNCONST(argv));
#undef UNCONST
}

static
atf_error_t
list_to_array(const atf_list_t *l, const char ***ap)
{
    atf_error_t err;
    const char **a;

    a = (const char **)malloc((atf_list_size(l) + 1) * sizeof(const char *));
    if (a == NULL)
        err = atf_no_memory_error();
    else {
        const char **aiter;
        atf_list_citer_t liter;

        aiter = a;
        atf_list_for_each_c(liter, l) {
            *aiter = (const char *)atf_list_citer_data(liter);
            aiter++;
        }
        *aiter = NULL;

        err = atf_no_error();
        *ap = a;
    }

    return err;
}

struct exec_args {
    const atf_fs_path_t *m_prog;
    const char *const *m_argv;
    void (*m_prehook)(void);
};

static
void
do_exec(void *v)
{
    struct exec_args *ea = v;

    if (ea->m_prehook != NULL)
        ea->m_prehook();

    const int ret = const_execvp(atf_fs_path_cstring(ea->m_prog), ea->m_argv);
    const int errnocopy = errno;
    INV(ret == -1);
    fprintf(stderr, "exec(%s) failed: %s\n",
            atf_fs_path_cstring(ea->m_prog), strerror(errnocopy));
    exit(EXIT_FAILURE);
}

atf_error_t
atf_process_exec_array(atf_process_status_t *s,
                       const atf_fs_path_t *prog,
                       const char *const *argv,
                       const atf_process_stream_t *outsb,
                       const atf_process_stream_t *errsb,
                       void (*prehook)(void))
{
    atf_error_t err;
    atf_process_child_t c;
    struct exec_args ea = { prog, argv, prehook };

    PRE(outsb == NULL ||
        atf_process_stream_type(outsb) != atf_process_stream_type_capture);
    PRE(errsb == NULL ||
        atf_process_stream_type(errsb) != atf_process_stream_type_capture);

    err = atf_process_fork(&c, do_exec, outsb, errsb, &ea);
    if (atf_is_error(err))
        goto out;

again:
    err = atf_process_child_wait(&c, s);
    if (atf_is_error(err)) {
        INV(atf_error_is(err, "libc") && atf_libc_error_code(err) == EINTR);
        atf_error_free(err);
        goto again;
    }

out:
    return err;
}

atf_error_t
atf_process_exec_list(atf_process_status_t *s,
                      const atf_fs_path_t *prog,
                      const atf_list_t *argv,
                      const atf_process_stream_t *outsb,
                      const atf_process_stream_t *errsb,
                      void (*prehook)(void))
{
    atf_error_t err;
    const char **argv2;

    PRE(outsb == NULL ||
        atf_process_stream_type(outsb) != atf_process_stream_type_capture);
    PRE(errsb == NULL ||
        atf_process_stream_type(errsb) != atf_process_stream_type_capture);

    argv2 = NULL; /* Silence GCC warning. */
    err = list_to_array(argv, &argv2);
    if (atf_is_error(err))
        goto out;

    err = atf_process_exec_array(s, prog, argv2, outsb, errsb, prehook);

    free(argv2);
out:
    return err;
}
