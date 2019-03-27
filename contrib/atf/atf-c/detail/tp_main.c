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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atf-c/detail/dynstr.h"
#include "atf-c/detail/env.h"
#include "atf-c/detail/fs.h"
#include "atf-c/detail/map.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/error.h"
#include "atf-c/tc.h"
#include "atf-c/tp.h"
#include "atf-c/utils.h"

#if defined(HAVE_GNU_GETOPT)
#   define GETOPT_POSIX "+"
#else
#   define GETOPT_POSIX ""
#endif

static const char *progname = NULL;

/* This prototype is provided by macros.h during instantiation of the test
 * program, so it can be kept private.  Don't know if that's the best idea
 * though. */
int atf_tp_main(int, char **, atf_error_t (*)(atf_tp_t *));

enum tc_part {
    BODY,
    CLEANUP,
};

/* ---------------------------------------------------------------------
 * The "usage" and "user" error types.
 * --------------------------------------------------------------------- */

#define FREE_FORM_ERROR(name) \
    struct name ## _error_data { \
        char m_what[2048]; \
    }; \
    \
    static \
    void \
    name ## _format(const atf_error_t err, char *buf, size_t buflen) \
    { \
        const struct name ## _error_data *data; \
        \
        PRE(atf_error_is(err, #name)); \
        \
        data = atf_error_data(err); \
        snprintf(buf, buflen, "%s", data->m_what); \
    } \
    \
    static \
    atf_error_t \
    name ## _error(const char *fmt, ...) \
    { \
        atf_error_t err; \
        struct name ## _error_data data; \
        va_list ap; \
        \
        va_start(ap, fmt); \
        vsnprintf(data.m_what, sizeof(data.m_what), fmt, ap); \
        va_end(ap); \
        \
        err = atf_error_new(#name, &data, sizeof(data), name ## _format); \
        \
        return err; \
    }

FREE_FORM_ERROR(usage);
FREE_FORM_ERROR(user);

/* ---------------------------------------------------------------------
 * Printing functions.
 * --------------------------------------------------------------------- */

static
void
print_error(const atf_error_t err)
{
    char buf[4096];

    PRE(atf_is_error(err));

    atf_error_format(err, buf, sizeof(buf));
    fprintf(stderr, "%s: ERROR: %s\n", progname, buf);

    if (atf_error_is(err, "usage"))
        fprintf(stderr, "%s: See atf-test-program(1) for usage details.\n",
                progname);
}

static
void
print_warning(const char *message)
{
    fprintf(stderr, "%s: WARNING: %s\n", progname, message);
}

/* ---------------------------------------------------------------------
 * Options handling.
 * --------------------------------------------------------------------- */

struct params {
    bool m_do_list;
    atf_fs_path_t m_srcdir;
    char *m_tcname;
    enum tc_part m_tcpart;
    atf_fs_path_t m_resfile;
    atf_map_t m_config;
};

static
atf_error_t
argv0_to_dir(const char *argv0, atf_fs_path_t *dir)
{
    atf_error_t err;
    atf_fs_path_t temp;

    err = atf_fs_path_init_fmt(&temp, "%s", argv0);
    if (atf_is_error(err))
        goto out;

    err = atf_fs_path_branch_path(&temp, dir);

    atf_fs_path_fini(&temp);
out:
    return err;
}

static
atf_error_t
params_init(struct params *p, const char *argv0)
{
    atf_error_t err;

    p->m_do_list = false;
    p->m_tcname = NULL;
    p->m_tcpart = BODY;

    err = argv0_to_dir(argv0, &p->m_srcdir);
    if (atf_is_error(err))
        return err;

    err = atf_fs_path_init_fmt(&p->m_resfile, "/dev/stdout");
    if (atf_is_error(err)) {
        atf_fs_path_fini(&p->m_srcdir);
        return err;
    }

    err = atf_map_init(&p->m_config);
    if (atf_is_error(err)) {
        atf_fs_path_fini(&p->m_resfile);
        atf_fs_path_fini(&p->m_srcdir);
        return err;
    }

    return err;
}

static
void
params_fini(struct params *p)
{
    atf_map_fini(&p->m_config);
    atf_fs_path_fini(&p->m_resfile);
    atf_fs_path_fini(&p->m_srcdir);
    if (p->m_tcname != NULL)
        free(p->m_tcname);
}

static
atf_error_t
parse_vflag(char *arg, atf_map_t *config)
{
    atf_error_t err;
    char *split;

    split = strchr(arg, '=');
    if (split == NULL) {
        err = usage_error("-v requires an argument of the form var=value");
        goto out;
    }

    *split = '\0';
    split++;

    err = atf_map_insert(config, arg, split, false);

out:
    return err;
}

static
atf_error_t
replace_path_param(atf_fs_path_t *param, const char *value)
{
    atf_error_t err;
    atf_fs_path_t temp;

    err = atf_fs_path_init_fmt(&temp, "%s", value);
    if (!atf_is_error(err)) {
        atf_fs_path_fini(param);
        *param = temp;
    }

    return err;
}

/* ---------------------------------------------------------------------
 * Test case listing.
 * --------------------------------------------------------------------- */

static
void
list_tcs(const atf_tp_t *tp)
{
    const atf_tc_t *const *tcs;
    const atf_tc_t *const *tcsptr;

    printf("Content-Type: application/X-atf-tp; version=\"1\"\n\n");

    tcs = atf_tp_get_tcs(tp);
    INV(tcs != NULL);  /* Should be checked. */
    for (tcsptr = tcs; *tcsptr != NULL; tcsptr++) {
        const atf_tc_t *tc = *tcsptr;
        char **vars = atf_tc_get_md_vars(tc);
        char **ptr;

        INV(vars != NULL);  /* Should be checked. */

        if (tcsptr != tcs)  /* Not first. */
            printf("\n");

        for (ptr = vars; *ptr != NULL; ptr += 2) {
            if (strcmp(*ptr, "ident") == 0) {
                printf("ident: %s\n", *(ptr + 1));
                break;
            }
        }

        for (ptr = vars; *ptr != NULL; ptr += 2) {
            if (strcmp(*ptr, "ident") != 0) {
                printf("%s: %s\n", *ptr, *(ptr + 1));
            }
        }

        atf_utils_free_charpp(vars);
    }
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

static
atf_error_t
handle_tcarg(const char *tcarg, char **tcname, enum tc_part *tcpart)
{
    atf_error_t err;

    err = atf_no_error();

    *tcname = strdup(tcarg);
    if (*tcname == NULL) {
        err = atf_no_memory_error();
        goto out;
    }

    char *delim = strchr(*tcname, ':');
    if (delim != NULL) {
        *delim = '\0';

        delim++;
        if (strcmp(delim, "body") == 0) {
            *tcpart = BODY;
        } else if (strcmp(delim, "cleanup") == 0) {
            *tcpart = CLEANUP;
        } else {
            err = usage_error("Invalid test case part `%s'", delim);
            goto out;
        }
    }

out:
    return err;
}

static
atf_error_t
process_params(int argc, char **argv, struct params *p)
{
    atf_error_t err;
    int ch;
    int old_opterr;

    err = params_init(p, argv[0]);
    if (atf_is_error(err))
        goto out;

    old_opterr = opterr;
    opterr = 0;
    while (!atf_is_error(err) &&
           (ch = getopt(argc, argv, GETOPT_POSIX ":lr:s:v:")) != -1) {
        switch (ch) {
        case 'l':
            p->m_do_list = true;
            break;

        case 'r':
            err = replace_path_param(&p->m_resfile, optarg);
            break;

        case 's':
            err = replace_path_param(&p->m_srcdir, optarg);
            break;

        case 'v':
            err = parse_vflag(optarg, &p->m_config);
            break;

        case ':':
            err = usage_error("Option -%c requires an argument.", optopt);
            break;

        case '?':
        default:
            err = usage_error("Unknown option -%c.", optopt);
        }
    }
    argc -= optind;
    argv += optind;

    /* Clear getopt state just in case the test wants to use it. */
    opterr = old_opterr;
    optind = 1;
#if defined(HAVE_OPTRESET)
    optreset = 1;
#endif

    if (!atf_is_error(err)) {
        if (p->m_do_list) {
            if (argc > 0)
                err = usage_error("Cannot provide test case names with -l");
        } else {
            if (argc == 0)
                err = usage_error("Must provide a test case name");
            else if (argc == 1)
                err = handle_tcarg(argv[0], &p->m_tcname, &p->m_tcpart);
            else if (argc > 1) {
                err = usage_error("Cannot provide more than one test case "
                                  "name");
            }
        }
    }

    if (atf_is_error(err))
        params_fini(p);

out:
    return err;
}

static
atf_error_t
srcdir_strip_libtool(atf_fs_path_t *srcdir)
{
    atf_error_t err;
    atf_fs_path_t parent;

    err = atf_fs_path_branch_path(srcdir, &parent);
    if (atf_is_error(err))
        goto out;

    atf_fs_path_fini(srcdir);
    *srcdir = parent;

    INV(!atf_is_error(err));
out:
    return err;
}

static
atf_error_t
handle_srcdir(struct params *p)
{
    atf_error_t err;
    atf_dynstr_t leafname;
    atf_fs_path_t exe, srcdir;
    bool b;

    err = atf_fs_path_copy(&srcdir, &p->m_srcdir);
    if (atf_is_error(err))
        goto out;

    if (!atf_fs_path_is_absolute(&srcdir)) {
        atf_fs_path_t srcdirabs;

        err = atf_fs_path_to_absolute(&srcdir, &srcdirabs);
        if (atf_is_error(err))
            goto out_srcdir;

        atf_fs_path_fini(&srcdir);
        srcdir = srcdirabs;
    }

    err = atf_fs_path_leaf_name(&srcdir, &leafname);
    if (atf_is_error(err))
        goto out_srcdir;
    else {
        const bool libs = atf_equal_dynstr_cstring(&leafname, ".libs");
        atf_dynstr_fini(&leafname);

        if (libs) {
            err = srcdir_strip_libtool(&srcdir);
            if (atf_is_error(err))
                goto out;
        }
    }

    err = atf_fs_path_copy(&exe, &srcdir);
    if (atf_is_error(err))
        goto out_srcdir;

    err = atf_fs_path_append_fmt(&exe, "%s", progname);
    if (atf_is_error(err))
        goto out_exe;

    err = atf_fs_exists(&exe, &b);
    if (!atf_is_error(err)) {
        if (b) {
            err = atf_map_insert(&p->m_config, "srcdir",
                                 strdup(atf_fs_path_cstring(&srcdir)), true);
        } else {
            err = user_error("Cannot find the test program in the source "
                             "directory `%s'", atf_fs_path_cstring(&srcdir));
        }
    }

out_exe:
    atf_fs_path_fini(&exe);
out_srcdir:
    atf_fs_path_fini(&srcdir);
out:
    return err;
}

static
atf_error_t
run_tc(const atf_tp_t *tp, struct params *p, int *exitcode)
{
    atf_error_t err;

    err = atf_no_error();

    if (!atf_tp_has_tc(tp, p->m_tcname)) {
        err = usage_error("Unknown test case `%s'", p->m_tcname);
        goto out;
    }

    if (!atf_env_has("__RUNNING_INSIDE_ATF_RUN") || strcmp(atf_env_get(
        "__RUNNING_INSIDE_ATF_RUN"), "internal-yes-value") != 0)
    {
        print_warning("Running test cases outside of kyua(1) is unsupported");
        print_warning("No isolation nor timeout control is being applied; you "
                      "may get unexpected failures; see atf-test-case(4)");
    }

    switch (p->m_tcpart) {
    case BODY:
        err = atf_tp_run(tp, p->m_tcname, atf_fs_path_cstring(&p->m_resfile));
        if (atf_is_error(err)) {
            /* TODO: Handle error */
            *exitcode = EXIT_FAILURE;
            atf_error_free(err);
        } else {
            *exitcode = EXIT_SUCCESS;
        }

        break;

    case CLEANUP:
        err = atf_tp_cleanup(tp, p->m_tcname);
        if (atf_is_error(err)) {
            /* TODO: Handle error */
            *exitcode = EXIT_FAILURE;
            atf_error_free(err);
        } else {
            *exitcode = EXIT_SUCCESS;
        }

        break;

    default:
        UNREACHABLE;
    }

    INV(!atf_is_error(err));
out:
    return err;
}

static
atf_error_t
controlled_main(int argc, char **argv,
                atf_error_t (*add_tcs_hook)(atf_tp_t *),
                int *exitcode)
{
    atf_error_t err;
    struct params p;
    atf_tp_t tp;
    char **raw_config;

    err = process_params(argc, argv, &p);
    if (atf_is_error(err))
        goto out;

    err = handle_srcdir(&p);
    if (atf_is_error(err))
        goto out_p;

    raw_config = atf_map_to_charpp(&p.m_config);
    if (raw_config == NULL) {
        err = atf_no_memory_error();
        goto out_p;
    }
    err = atf_tp_init(&tp, (const char* const*)raw_config);
    atf_utils_free_charpp(raw_config);
    if (atf_is_error(err))
        goto out_p;

    err = add_tcs_hook(&tp);
    if (atf_is_error(err))
        goto out_tp;

    if (p.m_do_list) {
        list_tcs(&tp);
        INV(!atf_is_error(err));
        *exitcode = EXIT_SUCCESS;
    } else {
        err = run_tc(&tp, &p, exitcode);
    }

out_tp:
    atf_tp_fini(&tp);
out_p:
    params_fini(&p);
out:
    return err;
}

int
atf_tp_main(int argc, char **argv, atf_error_t (*add_tcs_hook)(atf_tp_t *))
{
    atf_error_t err;
    int exitcode;

    progname = strrchr(argv[0], '/');
    if (progname == NULL)
        progname = argv[0];
    else
        progname++;

    /* Libtool workaround: if running from within the source tree (binaries
     * that are not installed yet), skip the "lt-" prefix added to files in
     * the ".libs" directory to show the real (not temporary) name. */
    if (strncmp(progname, "lt-", 3) == 0)
        progname += 3;

    exitcode = EXIT_FAILURE; /* Silence GCC warning. */
    err = controlled_main(argc, argv, add_tcs_hook, &exitcode);
    if (atf_is_error(err)) {
        print_error(err);
        atf_error_free(err);
        exitcode = EXIT_FAILURE;
    }

    return exitcode;
}
