/* Copyright (c) 2009 The NetBSD Foundation, Inc.
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

#include "atf-c/build.h"

#include <stdlib.h>
#include <string.h>

#include "atf-c/detail/env.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/detail/text.h"
#include "atf-c/error.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
atf_error_t
append_config_var(const char *var, const char *default_value, atf_list_t *argv)
{
    atf_error_t err;
    atf_list_t words;

    err = atf_text_split(atf_env_get_with_default(var, default_value),
                         " ", &words);
    if (atf_is_error(err))
        goto out;

    atf_list_append_list(argv, &words);

out:
    return err;
}

static
atf_error_t
append_arg1(const char *arg, atf_list_t *argv)
{
    return atf_list_append(argv, strdup(arg), true);
}

static
atf_error_t
append_arg2(const char *flag, const char *arg, atf_list_t *argv)
{
    atf_error_t err;

    err = append_arg1(flag, argv);
    if (!atf_is_error(err))
        err = append_arg1(arg, argv);

    return err;
}

static
atf_error_t
append_optargs(const char *const optargs[], atf_list_t *argv)
{
    atf_error_t err;

    err = atf_no_error();
    while (*optargs != NULL && !atf_is_error(err)) {
        err = append_arg1(strdup(*optargs), argv);
        optargs++;
    }

    return err;
}

static
atf_error_t
append_src_out(const char *src, const char *obj, atf_list_t *argv)
{
    atf_error_t err;

    err = append_arg2("-o", obj, argv);
    if (atf_is_error(err))
        goto out;

    err = append_arg1("-c", argv);
    if (atf_is_error(err))
        goto out;

    err = append_arg1(src, argv);

out:
    return err;
}

static
atf_error_t
list_to_array(const atf_list_t *l, char ***ap)
{
    atf_error_t err;
    char **a;

    a = (char **)malloc((atf_list_size(l) + 1) * sizeof(char *));
    if (a == NULL)
        err = atf_no_memory_error();
    else {
        char **aiter;
        atf_list_citer_t liter;

        aiter = a;
        atf_list_for_each_c(liter, l) {
            *aiter = strdup((const char *)atf_list_citer_data(liter));
            aiter++;
        }
        *aiter = NULL;

        err = atf_no_error();
    }
    *ap = a; /* Shut up warnings in the caller about uninitialized *ap. */

    return err;
}

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

atf_error_t
atf_build_c_o(const char *sfile,
              const char *ofile,
              const char *const optargs[],
              char ***argv)
{
    atf_error_t err;
    atf_list_t argv_list;

    err = atf_list_init(&argv_list);
    if (atf_is_error(err))
        goto out;

    err = append_config_var("ATF_BUILD_CC", ATF_BUILD_CC, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = append_config_var("ATF_BUILD_CPPFLAGS", ATF_BUILD_CPPFLAGS,
                            &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = append_config_var("ATF_BUILD_CFLAGS", ATF_BUILD_CFLAGS, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    if (optargs != NULL) {
        err = append_optargs(optargs, &argv_list);
        if (atf_is_error(err))
            goto out_list;
    }

    err = append_src_out(sfile, ofile, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = list_to_array(&argv_list, argv);
    if (atf_is_error(err))
        goto out_list;

out_list:
    atf_list_fini(&argv_list);
out:
    return err;
}

atf_error_t
atf_build_cpp(const char *sfile,
              const char *ofile,
              const char *const optargs[],
              char ***argv)
{
    atf_error_t err;
    atf_list_t argv_list;

    err = atf_list_init(&argv_list);
    if (atf_is_error(err))
        goto out;

    err = append_config_var("ATF_BUILD_CPP", ATF_BUILD_CPP, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = append_config_var("ATF_BUILD_CPPFLAGS", ATF_BUILD_CPPFLAGS,
                            &argv_list);
    if (atf_is_error(err))
        goto out_list;

    if (optargs != NULL) {
        err = append_optargs(optargs, &argv_list);
        if (atf_is_error(err))
            goto out_list;
    }

    err = append_arg2("-o", ofile, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = append_arg1(sfile, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = list_to_array(&argv_list, argv);
    if (atf_is_error(err))
        goto out_list;

out_list:
    atf_list_fini(&argv_list);
out:
    return err;
}

atf_error_t
atf_build_cxx_o(const char *sfile,
                const char *ofile,
                const char *const optargs[],
                char ***argv)
{
    atf_error_t err;
    atf_list_t argv_list;

    err = atf_list_init(&argv_list);
    if (atf_is_error(err))
        goto out;

    err = append_config_var("ATF_BUILD_CXX", ATF_BUILD_CXX, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = append_config_var("ATF_BUILD_CPPFLAGS", ATF_BUILD_CPPFLAGS,
                            &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = append_config_var("ATF_BUILD_CXXFLAGS", ATF_BUILD_CXXFLAGS,
                            &argv_list);
    if (atf_is_error(err))
        goto out_list;

    if (optargs != NULL) {
        err = append_optargs(optargs, &argv_list);
        if (atf_is_error(err))
            goto out_list;
    }

    err = append_src_out(sfile, ofile, &argv_list);
    if (atf_is_error(err))
        goto out_list;

    err = list_to_array(&argv_list, argv);
    if (atf_is_error(err))
        goto out_list;

out_list:
    atf_list_fini(&argv_list);
out:
    return err;
}
