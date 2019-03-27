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

#include "atf-c/tc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atf-c/defs.h"
#include "atf-c/detail/env.h"
#include "atf-c/detail/fs.h"
#include "atf-c/detail/map.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/detail/text.h"
#include "atf-c/error.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

enum expect_type {
    EXPECT_PASS,
    EXPECT_FAIL,
    EXPECT_EXIT,
    EXPECT_SIGNAL,
    EXPECT_DEATH,
    EXPECT_TIMEOUT,
};

struct context {
    const atf_tc_t *tc;
    const char *resfile;
    size_t fail_count;

    enum expect_type expect;
    atf_dynstr_t expect_reason;
    size_t expect_previous_fail_count;
    size_t expect_fail_count;
    int expect_exitcode;
    int expect_signo;
};

static void context_init(struct context *, const atf_tc_t *, const char *);
static void check_fatal_error(atf_error_t);
static void report_fatal_error(const char *, ...)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static atf_error_t write_resfile(const int, const char *, const int,
                                 const atf_dynstr_t *);
static void create_resfile(const char *, const char *, const int,
                           atf_dynstr_t *);
static void error_in_expect(struct context *, const char *, ...)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void validate_expect(struct context *);
static void expected_failure(struct context *, atf_dynstr_t *)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void fail_requirement(struct context *, atf_dynstr_t *)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void fail_check(struct context *, atf_dynstr_t *);
static void pass(struct context *)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void skip(struct context *, atf_dynstr_t *)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void format_reason_ap(atf_dynstr_t *, const char *, const size_t,
                             const char *, va_list);
static void format_reason_fmt(atf_dynstr_t *, const char *, const size_t,
                              const char *, ...);
static void errno_test(struct context *, const char *, const size_t,
                       const int, const char *, const bool,
                       void (*)(struct context *, atf_dynstr_t *));
static atf_error_t check_prog_in_dir(const char *, void *);
static atf_error_t check_prog(struct context *, const char *);

static void
context_init(struct context *ctx, const atf_tc_t *tc, const char *resfile)
{
    ctx->tc = tc;
    ctx->resfile = resfile;
    ctx->fail_count = 0;
    ctx->expect = EXPECT_PASS;
    check_fatal_error(atf_dynstr_init(&ctx->expect_reason));
    ctx->expect_previous_fail_count = 0;
    ctx->expect_fail_count = 0;
    ctx->expect_exitcode = 0;
    ctx->expect_signo = 0;
}

static void
check_fatal_error(atf_error_t err)
{
    if (atf_is_error(err)) {
        char buf[1024];
        atf_error_format(err, buf, sizeof(buf));
        fprintf(stderr, "FATAL ERROR: %s\n", buf);
        atf_error_free(err);
        abort();
    }
}

static void
report_fatal_error(const char *msg, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    abort();
}

/** Writes to a results file.
 *
 * The results file is supposed to be already open.
 *
 * This function returns an error code instead of exiting in case of error
 * because the caller needs to clean up the reason object before terminating.
 */
static atf_error_t
write_resfile(const int fd, const char *result, const int arg,
              const atf_dynstr_t *reason)
{
    static char NL[] = "\n", CS[] = ": ";
    char buf[64];
    const char *r;
    struct iovec iov[5];
    ssize_t ret;
    int count = 0;

    INV(arg == -1 || reason != NULL);

#define UNCONST(a) ((void *)(unsigned long)(const void *)(a))
    iov[count].iov_base = UNCONST(result);
    iov[count++].iov_len = strlen(result);

    if (reason != NULL) {
        if (arg != -1) {
            iov[count].iov_base = buf;
            iov[count++].iov_len = snprintf(buf, sizeof(buf), "(%d)", arg);
        }

        iov[count].iov_base = CS;
        iov[count++].iov_len = sizeof(CS) - 1;

        r = atf_dynstr_cstring(reason);
        iov[count].iov_base = UNCONST(r);
        iov[count++].iov_len = strlen(r);
    }
#undef UNCONST

    iov[count].iov_base = NL;
    iov[count++].iov_len = sizeof(NL) - 1;

    while ((ret = writev(fd, iov, count)) == -1 && errno == EINTR)
        continue; /* Retry. */
    if (ret != -1)
        return atf_no_error();

    return atf_libc_error(
        errno, "Failed to write results file; result %s, reason %s", result,
        reason == NULL ? "null" : atf_dynstr_cstring(reason));
}

/** Creates a results file.
 *
 * The input reason is released in all cases.
 *
 * An error in this function is considered to be fatal, hence why it does
 * not return any error code.
 */
static void
create_resfile(const char *resfile, const char *result, const int arg,
               atf_dynstr_t *reason)
{
    atf_error_t err;

    if (strcmp("/dev/stdout", resfile) == 0) {
        err = write_resfile(STDOUT_FILENO, result, arg, reason);
    } else if (strcmp("/dev/stderr", resfile) == 0) {
        err = write_resfile(STDERR_FILENO, result, arg, reason);
    } else {
        const int fd = open(resfile, O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1) {
            err = atf_libc_error(errno, "Cannot create results file '%s'",
                                 resfile);
        } else {
            err = write_resfile(fd, result, arg, reason);
            close(fd);
        }
    }

    if (reason != NULL)
        atf_dynstr_fini(reason);

    check_fatal_error(err);
}

/** Fails a test case if validate_expect fails. */
static void
error_in_expect(struct context *ctx, const char *fmt, ...)
{
    atf_dynstr_t reason;
    va_list ap;

    va_start(ap, fmt);
    format_reason_ap(&reason, NULL, 0, fmt, ap);
    va_end(ap);

    ctx->expect = EXPECT_PASS;  /* Ensure fail_requirement really fails. */
    fail_requirement(ctx, &reason);
}

/** Ensures that the "expect" state is correct.
 *
 * Call this function before modifying the current value of expect.
 */
static void
validate_expect(struct context *ctx)
{
    if (ctx->expect == EXPECT_DEATH) {
        error_in_expect(ctx, "Test case was expected to terminate abruptly "
            "but it continued execution");
    } else if (ctx->expect == EXPECT_EXIT) {
        error_in_expect(ctx, "Test case was expected to exit cleanly but it "
            "continued execution");
    } else if (ctx->expect == EXPECT_FAIL) {
        if (ctx->expect_fail_count == ctx->expect_previous_fail_count)
            error_in_expect(ctx, "Test case was expecting a failure but none "
                "were raised");
        else
            INV(ctx->expect_fail_count > ctx->expect_previous_fail_count);
    } else if (ctx->expect == EXPECT_PASS) {
        /* Nothing to validate. */
    } else if (ctx->expect == EXPECT_SIGNAL) {
        error_in_expect(ctx, "Test case was expected to receive a termination "
            "signal but it continued execution");
    } else if (ctx->expect == EXPECT_TIMEOUT) {
        error_in_expect(ctx, "Test case was expected to hang but it continued "
            "execution");
    } else
        UNREACHABLE;
}

static void
expected_failure(struct context *ctx, atf_dynstr_t *reason)
{
    check_fatal_error(atf_dynstr_prepend_fmt(reason, "%s: ",
        atf_dynstr_cstring(&ctx->expect_reason)));
    create_resfile(ctx->resfile, "expected_failure", -1, reason);
    exit(EXIT_SUCCESS);
}

static void
fail_requirement(struct context *ctx, atf_dynstr_t *reason)
{
    if (ctx->expect == EXPECT_FAIL) {
        expected_failure(ctx, reason);
    } else if (ctx->expect == EXPECT_PASS) {
        create_resfile(ctx->resfile, "failed", -1, reason);
        exit(EXIT_FAILURE);
    } else {
        error_in_expect(ctx, "Test case raised a failure but was not "
            "expecting one; reason was %s", atf_dynstr_cstring(reason));
    }
    UNREACHABLE;
}

static void
fail_check(struct context *ctx, atf_dynstr_t *reason)
{
    if (ctx->expect == EXPECT_FAIL) {
        fprintf(stderr, "*** Expected check failure: %s: %s\n",
            atf_dynstr_cstring(&ctx->expect_reason),
            atf_dynstr_cstring(reason));
        ctx->expect_fail_count++;
    } else if (ctx->expect == EXPECT_PASS) {
        fprintf(stderr, "*** Check failed: %s\n", atf_dynstr_cstring(reason));
        ctx->fail_count++;
    } else {
        error_in_expect(ctx, "Test case raised a failure but was not "
            "expecting one; reason was %s", atf_dynstr_cstring(reason));
    }

    atf_dynstr_fini(reason);
}

static void
pass(struct context *ctx)
{
    if (ctx->expect == EXPECT_FAIL) {
        error_in_expect(ctx, "Test case was expecting a failure but got "
            "a pass instead");
    } else if (ctx->expect == EXPECT_PASS) {
        create_resfile(ctx->resfile, "passed", -1, NULL);
        exit(EXIT_SUCCESS);
    } else {
        error_in_expect(ctx, "Test case asked to explicitly pass but was "
            "not expecting such condition");
    }
    UNREACHABLE;
}

static void
skip(struct context *ctx, atf_dynstr_t *reason)
{
    if (ctx->expect == EXPECT_PASS) {
        create_resfile(ctx->resfile, "skipped", -1, reason);
        exit(EXIT_SUCCESS);
    } else {
        error_in_expect(ctx, "Can only skip a test case when running in "
            "expect pass mode");
    }
    UNREACHABLE;
}

/** Formats a failure/skip reason message.
 *
 * The formatted reason is stored in out_reason.  out_reason is initialized
 * in this function and is supposed to be released by the caller.  In general,
 * the reason will eventually be fed to create_resfile, which will release
 * it.
 *
 * Errors in this function are fatal.  Rationale being: reasons are used to
 * create results files; if we can't format the reason correctly, the result
 * of the test program will be bogus.  So it's better to just exit with a
 * fatal error.
 */
static void
format_reason_ap(atf_dynstr_t *out_reason,
                 const char *source_file, const size_t source_line,
                 const char *reason, va_list ap)
{
    atf_error_t err;

    if (source_file != NULL) {
        err = atf_dynstr_init_fmt(out_reason, "%s:%zd: ", source_file,
                                  source_line);
    } else {
        PRE(source_line == 0);
        err = atf_dynstr_init(out_reason);
    }

    if (!atf_is_error(err)) {
        va_list ap2;
        va_copy(ap2, ap);
        err = atf_dynstr_append_ap(out_reason, reason, ap2);
        va_end(ap2);
    }

    check_fatal_error(err);
}

static void
format_reason_fmt(atf_dynstr_t *out_reason,
                  const char *source_file, const size_t source_line,
                  const char *reason, ...)
{
    va_list ap;

    va_start(ap, reason);
    format_reason_ap(out_reason, source_file, source_line, reason, ap);
    va_end(ap);
}

static void
errno_test(struct context *ctx, const char *file, const size_t line,
           const int exp_errno, const char *expr_str,
           const bool expr_result,
           void (*fail_func)(struct context *, atf_dynstr_t *))
{
    const int actual_errno = errno;

    if (expr_result) {
        if (exp_errno != actual_errno) {
            atf_dynstr_t reason;

            format_reason_fmt(&reason, file, line, "Expected errno %d, got %d, "
                "in %s", exp_errno, actual_errno, expr_str);
            fail_func(ctx, &reason);
        }
    } else {
        atf_dynstr_t reason;

        format_reason_fmt(&reason, file, line, "Expected true value in %s",
            expr_str);
        fail_func(ctx, &reason);
    }
}

struct prog_found_pair {
    const char *prog;
    bool found;
};

static atf_error_t
check_prog_in_dir(const char *dir, void *data)
{
    struct prog_found_pair *pf = data;
    atf_error_t err;

    if (pf->found)
        err = atf_no_error();
    else {
        atf_fs_path_t p;

        err = atf_fs_path_init_fmt(&p, "%s/%s", dir, pf->prog);
        if (atf_is_error(err))
            goto out_p;

        err = atf_fs_eaccess(&p, atf_fs_access_x);
        if (!atf_is_error(err))
            pf->found = true;
        else {
            atf_error_free(err);
            INV(!pf->found);
            err = atf_no_error();
        }

out_p:
        atf_fs_path_fini(&p);
    }

    return err;
}

static atf_error_t
check_prog(struct context *ctx, const char *prog)
{
    atf_error_t err;
    atf_fs_path_t p;

    err = atf_fs_path_init_fmt(&p, "%s", prog);
    if (atf_is_error(err))
        goto out;

    if (atf_fs_path_is_absolute(&p)) {
        err = atf_fs_eaccess(&p, atf_fs_access_x);
        if (atf_is_error(err)) {
            atf_dynstr_t reason;

            atf_error_free(err);
            atf_fs_path_fini(&p);
            format_reason_fmt(&reason, NULL, 0, "The required program %s could "
                "not be found", prog);
            skip(ctx, &reason);
        }
    } else {
        const char *path = atf_env_get("PATH");
        struct prog_found_pair pf;
        atf_fs_path_t bp;

        err = atf_fs_path_branch_path(&p, &bp);
        if (atf_is_error(err))
            goto out_p;

        if (strcmp(atf_fs_path_cstring(&bp), ".") != 0) {
            atf_fs_path_fini(&bp);
            atf_fs_path_fini(&p);

            report_fatal_error("Relative paths are not allowed when searching "
                "for a program (%s)", prog);
            UNREACHABLE;
        }

        pf.prog = prog;
        pf.found = false;
        err = atf_text_for_each_word(path, ":", check_prog_in_dir, &pf);
        if (atf_is_error(err))
            goto out_bp;

        if (!pf.found) {
            atf_dynstr_t reason;

            atf_fs_path_fini(&bp);
            atf_fs_path_fini(&p);
            format_reason_fmt(&reason, NULL, 0, "The required program %s could "
                "not be found in the PATH", prog);
            fail_requirement(ctx, &reason);
        }

out_bp:
        atf_fs_path_fini(&bp);
    }

out_p:
    atf_fs_path_fini(&p);
out:
    return err;
}

/* ---------------------------------------------------------------------
 * The "atf_tc" type.
 * --------------------------------------------------------------------- */

struct atf_tc_impl {
    const char *m_ident;

    atf_map_t m_vars;
    atf_map_t m_config;

    atf_tc_head_t m_head;
    atf_tc_body_t m_body;
    atf_tc_cleanup_t m_cleanup;
};

/*
 * Constructors/destructors.
 */

atf_error_t
atf_tc_init(atf_tc_t *tc, const char *ident, atf_tc_head_t head,
            atf_tc_body_t body, atf_tc_cleanup_t cleanup,
            const char *const *config)
{
    atf_error_t err;

    tc->pimpl = malloc(sizeof(struct atf_tc_impl));
    if (tc->pimpl == NULL) {
        err = atf_no_memory_error();
        goto err;
    }

    tc->pimpl->m_ident = ident;
    tc->pimpl->m_head = head;
    tc->pimpl->m_body = body;
    tc->pimpl->m_cleanup = cleanup;

    err = atf_map_init_charpp(&tc->pimpl->m_config, config);
    if (atf_is_error(err))
        goto err;

    err = atf_map_init(&tc->pimpl->m_vars);
    if (atf_is_error(err))
        goto err_vars;

    err = atf_tc_set_md_var(tc, "ident", ident);
    if (atf_is_error(err))
        goto err_map;

    if (cleanup != NULL) {
        err = atf_tc_set_md_var(tc, "has.cleanup", "true");
        if (atf_is_error(err))
            goto err_map;
    }

    /* XXX Should the head be able to return error codes? */
    if (tc->pimpl->m_head != NULL)
        tc->pimpl->m_head(tc);

    if (strcmp(atf_tc_get_md_var(tc, "ident"), ident) != 0) {
        report_fatal_error("Test case head modified the read-only 'ident' "
            "property");
        UNREACHABLE;
    }

    INV(!atf_is_error(err));
    return err;

err_map:
    atf_map_fini(&tc->pimpl->m_vars);
err_vars:
    atf_map_fini(&tc->pimpl->m_config);
err:
    return err;
}

atf_error_t
atf_tc_init_pack(atf_tc_t *tc, const atf_tc_pack_t *pack,
                 const char *const *config)
{
    return atf_tc_init(tc, pack->m_ident, pack->m_head, pack->m_body,
                       pack->m_cleanup, config);
}

void
atf_tc_fini(atf_tc_t *tc)
{
    atf_map_fini(&tc->pimpl->m_vars);
    free(tc->pimpl);
}

/*
 * Getters.
 */

const char *
atf_tc_get_ident(const atf_tc_t *tc)
{
    return tc->pimpl->m_ident;
}

const char *
atf_tc_get_config_var(const atf_tc_t *tc, const char *name)
{
    const char *val;
    atf_map_citer_t iter;

    PRE(atf_tc_has_config_var(tc, name));
    iter = atf_map_find_c(&tc->pimpl->m_config, name);
    val = atf_map_citer_data(iter);
    INV(val != NULL);

    return val;
}

const char *
atf_tc_get_config_var_wd(const atf_tc_t *tc, const char *name,
                         const char *defval)
{
    const char *val;

    if (!atf_tc_has_config_var(tc, name))
        val = defval;
    else
        val = atf_tc_get_config_var(tc, name);

    return val;
}

bool
atf_tc_get_config_var_as_bool(const atf_tc_t *tc, const char *name)
{
    bool val;
    const char *strval;
    atf_error_t err;

    strval = atf_tc_get_config_var(tc, name);
    err = atf_text_to_bool(strval, &val);
    if (atf_is_error(err)) {
        atf_error_free(err);
        atf_tc_fail("Configuration variable %s does not have a valid "
                    "boolean value; found %s", name, strval);
    }

    return val;
}

bool
atf_tc_get_config_var_as_bool_wd(const atf_tc_t *tc, const char *name,
                                 const bool defval)
{
    bool val;

    if (!atf_tc_has_config_var(tc, name))
        val = defval;
    else
        val = atf_tc_get_config_var_as_bool(tc, name);

    return val;
}

long
atf_tc_get_config_var_as_long(const atf_tc_t *tc, const char *name)
{
    long val;
    const char *strval;
    atf_error_t err;

    strval = atf_tc_get_config_var(tc, name);
    err = atf_text_to_long(strval, &val);
    if (atf_is_error(err)) {
        atf_error_free(err);
        atf_tc_fail("Configuration variable %s does not have a valid "
                    "long value; found %s", name, strval);
    }

    return val;
}

long
atf_tc_get_config_var_as_long_wd(const atf_tc_t *tc, const char *name,
                                 const long defval)
{
    long val;

    if (!atf_tc_has_config_var(tc, name))
        val = defval;
    else
        val = atf_tc_get_config_var_as_long(tc, name);

    return val;
}

const char *
atf_tc_get_md_var(const atf_tc_t *tc, const char *name)
{
    const char *val;
    atf_map_citer_t iter;

    PRE(atf_tc_has_md_var(tc, name));
    iter = atf_map_find_c(&tc->pimpl->m_vars, name);
    val = atf_map_citer_data(iter);
    INV(val != NULL);

    return val;
}

char **
atf_tc_get_md_vars(const atf_tc_t *tc)
{
    return atf_map_to_charpp(&tc->pimpl->m_vars);
}

bool
atf_tc_has_config_var(const atf_tc_t *tc, const char *name)
{
    atf_map_citer_t end, iter;

    iter = atf_map_find_c(&tc->pimpl->m_config, name);
    end = atf_map_end_c(&tc->pimpl->m_config);
    return !atf_equal_map_citer_map_citer(iter, end);
}

bool
atf_tc_has_md_var(const atf_tc_t *tc, const char *name)
{
    atf_map_citer_t end, iter;

    iter = atf_map_find_c(&tc->pimpl->m_vars, name);
    end = atf_map_end_c(&tc->pimpl->m_vars);
    return !atf_equal_map_citer_map_citer(iter, end);
}

/*
 * Modifiers.
 */

atf_error_t
atf_tc_set_md_var(atf_tc_t *tc, const char *name, const char *fmt, ...)
{
    atf_error_t err;
    char *value;
    va_list ap;

    va_start(ap, fmt);
    err = atf_text_format_ap(&value, fmt, ap);
    va_end(ap);

    if (!atf_is_error(err))
        err = atf_map_insert(&tc->pimpl->m_vars, name, value, true);
    else
        free(value);

    return err;
}

/* ---------------------------------------------------------------------
 * Free functions, as they should be publicly but they can't.
 * --------------------------------------------------------------------- */

static void _atf_tc_fail(struct context *, const char *, va_list)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void _atf_tc_fail_nonfatal(struct context *, const char *, va_list);
static void _atf_tc_fail_check(struct context *, const char *, const size_t,
    const char *, va_list);
static void _atf_tc_fail_requirement(struct context *, const char *,
    const size_t, const char *, va_list) ATF_DEFS_ATTRIBUTE_NORETURN;
static void _atf_tc_pass(struct context *) ATF_DEFS_ATTRIBUTE_NORETURN;
static void _atf_tc_require_prog(struct context *, const char *);
static void _atf_tc_skip(struct context *, const char *, va_list)
    ATF_DEFS_ATTRIBUTE_NORETURN;
static void _atf_tc_check_errno(struct context *, const char *, const size_t,
    const int, const char *, const bool);
static void _atf_tc_require_errno(struct context *, const char *, const size_t,
    const int, const char *, const bool);
static void _atf_tc_expect_pass(struct context *);
static void _atf_tc_expect_fail(struct context *, const char *, va_list);
static void _atf_tc_expect_exit(struct context *, const int, const char *,
    va_list);
static void _atf_tc_expect_signal(struct context *, const int, const char *,
    va_list);
static void _atf_tc_expect_death(struct context *, const char *,
    va_list);

static void
_atf_tc_fail(struct context *ctx, const char *fmt, va_list ap)
{
    va_list ap2;
    atf_dynstr_t reason;

    va_copy(ap2, ap);
    format_reason_ap(&reason, NULL, 0, fmt, ap2);
    va_end(ap2);

    fail_requirement(ctx, &reason);
    UNREACHABLE;
}

static void
_atf_tc_fail_nonfatal(struct context *ctx, const char *fmt, va_list ap)
{
    va_list ap2;
    atf_dynstr_t reason;

    va_copy(ap2, ap);
    format_reason_ap(&reason, NULL, 0, fmt, ap2);
    va_end(ap2);

    fail_check(ctx, &reason);
}

static void
_atf_tc_fail_check(struct context *ctx, const char *file, const size_t line,
                   const char *fmt, va_list ap)
{
    va_list ap2;
    atf_dynstr_t reason;

    va_copy(ap2, ap);
    format_reason_ap(&reason, file, line, fmt, ap2);
    va_end(ap2);

    fail_check(ctx, &reason);
}

static void
_atf_tc_fail_requirement(struct context *ctx, const char *file,
                         const size_t line, const char *fmt, va_list ap)
{
    va_list ap2;
    atf_dynstr_t reason;

    va_copy(ap2, ap);
    format_reason_ap(&reason, file, line, fmt, ap2);
    va_end(ap2);

    fail_requirement(ctx, &reason);
    UNREACHABLE;
}

static void
_atf_tc_pass(struct context *ctx)
{
    pass(ctx);
    UNREACHABLE;
}

static void
_atf_tc_require_prog(struct context *ctx, const char *prog)
{
    check_fatal_error(check_prog(ctx, prog));
}

static void
_atf_tc_skip(struct context *ctx, const char *fmt, va_list ap)
{
    atf_dynstr_t reason;
    va_list ap2;

    va_copy(ap2, ap);
    format_reason_ap(&reason, NULL, 0, fmt, ap2);
    va_end(ap2);

    skip(ctx, &reason);
}

static void
_atf_tc_check_errno(struct context *ctx, const char *file, const size_t line,
                    const int exp_errno, const char *expr_str,
                    const bool expr_result)
{
    errno_test(ctx, file, line, exp_errno, expr_str, expr_result, fail_check);
}

static void
_atf_tc_require_errno(struct context *ctx, const char *file, const size_t line,
                      const int exp_errno, const char *expr_str,
                      const bool expr_result)
{
    errno_test(ctx, file, line, exp_errno, expr_str, expr_result,
        fail_requirement);
}

static void
_atf_tc_expect_pass(struct context *ctx)
{
    validate_expect(ctx);

    ctx->expect = EXPECT_PASS;
}

static void
_atf_tc_expect_fail(struct context *ctx, const char *reason, va_list ap)
{
    va_list ap2;

    validate_expect(ctx);

    ctx->expect = EXPECT_FAIL;
    atf_dynstr_fini(&ctx->expect_reason);
    va_copy(ap2, ap);
    check_fatal_error(atf_dynstr_init_ap(&ctx->expect_reason, reason, ap2));
    va_end(ap2);
    ctx->expect_previous_fail_count = ctx->expect_fail_count;
}

static void
_atf_tc_expect_exit(struct context *ctx, const int exitcode, const char *reason,
                    va_list ap)
{
    va_list ap2;
    atf_dynstr_t formatted;

    validate_expect(ctx);

    ctx->expect = EXPECT_EXIT;
    va_copy(ap2, ap);
    check_fatal_error(atf_dynstr_init_ap(&formatted, reason, ap2));
    va_end(ap2);

    create_resfile(ctx->resfile, "expected_exit", exitcode, &formatted);
}

static void
_atf_tc_expect_signal(struct context *ctx, const int signo, const char *reason,
                      va_list ap)
{
    va_list ap2;
    atf_dynstr_t formatted;

    validate_expect(ctx);

    ctx->expect = EXPECT_SIGNAL;
    va_copy(ap2, ap);
    check_fatal_error(atf_dynstr_init_ap(&formatted, reason, ap2));
    va_end(ap2);

    create_resfile(ctx->resfile, "expected_signal", signo, &formatted);
}

static void
_atf_tc_expect_death(struct context *ctx, const char *reason, va_list ap)
{
    va_list ap2;
    atf_dynstr_t formatted;

    validate_expect(ctx);

    ctx->expect = EXPECT_DEATH;
    va_copy(ap2, ap);
    check_fatal_error(atf_dynstr_init_ap(&formatted, reason, ap2));
    va_end(ap2);

    create_resfile(ctx->resfile, "expected_death", -1, &formatted);
}

static void
_atf_tc_expect_timeout(struct context *ctx, const char *reason, va_list ap)
{
    va_list ap2;
    atf_dynstr_t formatted;

    validate_expect(ctx);

    ctx->expect = EXPECT_TIMEOUT;
    va_copy(ap2, ap);
    check_fatal_error(atf_dynstr_init_ap(&formatted, reason, ap2));
    va_end(ap2);

    create_resfile(ctx->resfile, "expected_timeout", -1, &formatted);
}

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

static struct context Current;

atf_error_t
atf_tc_run(const atf_tc_t *tc, const char *resfile)
{
    context_init(&Current, tc, resfile);

    tc->pimpl->m_body(tc);

    validate_expect(&Current);

    if (Current.fail_count > 0) {
        atf_dynstr_t reason;

        format_reason_fmt(&reason, NULL, 0, "%d checks failed; see output for "
            "more details", Current.fail_count);
        fail_requirement(&Current, &reason);
    } else if (Current.expect_fail_count > 0) {
        atf_dynstr_t reason;

        format_reason_fmt(&reason, NULL, 0, "%d checks failed as expected; "
            "see output for more details", Current.expect_fail_count);
        expected_failure(&Current, &reason);
    } else {
        pass(&Current);
    }
    UNREACHABLE;
    return atf_no_error();
}

atf_error_t
atf_tc_cleanup(const atf_tc_t *tc)
{
    if (tc->pimpl->m_cleanup != NULL)
        tc->pimpl->m_cleanup(tc);
    return atf_no_error(); /* XXX */
}

/* ---------------------------------------------------------------------
 * Free functions that depend on Current.
 * --------------------------------------------------------------------- */

/*
 * All the functions below provide delegates to other internal functions
 * (prefixed by _) that take the current test case as an argument to
 * prevent them from accessing global state.  This is to keep the side-
 * effects of the internal functions clearer and easier to understand.
 *
 * The public API should never have hid the fact that it needs access to
 * the current test case (other than maybe in the macros), but changing it
 * is hard.  TODO: Revisit in the future.
 */

void
atf_tc_fail(const char *fmt, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, fmt);
    _atf_tc_fail(&Current, fmt, ap);
    va_end(ap);
}

void
atf_tc_fail_nonfatal(const char *fmt, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, fmt);
    _atf_tc_fail_nonfatal(&Current, fmt, ap);
    va_end(ap);
}

void
atf_tc_fail_check(const char *file, const size_t line, const char *fmt, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, fmt);
    _atf_tc_fail_check(&Current, file, line, fmt, ap);
    va_end(ap);
}

void
atf_tc_fail_requirement(const char *file, const size_t line,
                        const char *fmt, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, fmt);
    _atf_tc_fail_requirement(&Current, file, line, fmt, ap);
    va_end(ap);
}

void
atf_tc_pass(void)
{
    PRE(Current.tc != NULL);

    _atf_tc_pass(&Current);
}

void
atf_tc_require_prog(const char *prog)
{
    PRE(Current.tc != NULL);

    _atf_tc_require_prog(&Current, prog);
}

void
atf_tc_skip(const char *fmt, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, fmt);
    _atf_tc_skip(&Current, fmt, ap);
    va_end(ap);
}

void
atf_tc_check_errno(const char *file, const size_t line, const int exp_errno,
                   const char *expr_str, const bool expr_result)
{
    PRE(Current.tc != NULL);

    _atf_tc_check_errno(&Current, file, line, exp_errno, expr_str,
                        expr_result);
}

void
atf_tc_require_errno(const char *file, const size_t line, const int exp_errno,
                     const char *expr_str, const bool expr_result)
{
    PRE(Current.tc != NULL);

    _atf_tc_require_errno(&Current, file, line, exp_errno, expr_str,
                          expr_result);
}

void
atf_tc_expect_pass(void)
{
    PRE(Current.tc != NULL);

    _atf_tc_expect_pass(&Current);
}

void
atf_tc_expect_fail(const char *reason, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, reason);
    _atf_tc_expect_fail(&Current, reason, ap);
    va_end(ap);
}

void
atf_tc_expect_exit(const int exitcode, const char *reason, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, reason);
    _atf_tc_expect_exit(&Current, exitcode, reason, ap);
    va_end(ap);
}

void
atf_tc_expect_signal(const int signo, const char *reason, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, reason);
    _atf_tc_expect_signal(&Current, signo, reason, ap);
    va_end(ap);
}

void
atf_tc_expect_death(const char *reason, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, reason);
    _atf_tc_expect_death(&Current, reason, ap);
    va_end(ap);
}

void
atf_tc_expect_timeout(const char *reason, ...)
{
    va_list ap;

    PRE(Current.tc != NULL);

    va_start(ap, reason);
    _atf_tc_expect_timeout(&Current, reason, ap);
    va_end(ap);
}
