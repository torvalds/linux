/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_ENGINE
NON_EMPTY_TRANSLATION_UNIT
#else

# include "apps.h"
# include "progs.h"
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <openssl/err.h>
# include <openssl/engine.h>
# include <openssl/ssl.h>
# include <openssl/store.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_C, OPT_T, OPT_TT, OPT_PRE, OPT_POST,
    OPT_V = 100, OPT_VV, OPT_VVV, OPT_VVVV
} OPTION_CHOICE;

const OPTIONS engine_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] engine...\n"},
    {OPT_HELP_STR, 1, '-',
        "  engine... Engines to load\n"},
    {"help", OPT_HELP, '-', "Display this summary"},
    {"v", OPT_V, '-', "List 'control commands' For each specified engine"},
    {"vv", OPT_VV, '-', "Also display each command's description"},
    {"vvv", OPT_VVV, '-', "Also add the input flags for each command"},
    {"vvvv", OPT_VVVV, '-', "Also show internal input flags"},
    {"c", OPT_C, '-', "List the capabilities of specified engine"},
    {"t", OPT_T, '-', "Check that specified engine is available"},
    {"tt", OPT_TT, '-', "Display error trace for unavailable engines"},
    {"pre", OPT_PRE, 's', "Run command against the ENGINE before loading it"},
    {"post", OPT_POST, 's', "Run command against the ENGINE after loading it"},
    {OPT_MORE_STR, OPT_EOF, 1,
     "Commands are like \"SO_PATH:/lib/libdriver.so\""},
    {NULL}
};

static int append_buf(char **buf, int *size, const char *s)
{
    const int expand = 256;
    int len = strlen(s) + 1;
    char *p = *buf;

    if (p == NULL) {
        *size = ((len + expand - 1) / expand) * expand;
        p = *buf = app_malloc(*size, "engine buffer");
    } else {
        const int blen = strlen(p);

        if (blen > 0)
            len += 2 + blen;

        if (len > *size) {
            *size = ((len + expand - 1) / expand) * expand;
            p = OPENSSL_realloc(p, *size);
            if (p == NULL) {
                OPENSSL_free(*buf);
                *buf = NULL;
                return 0;
            }
            *buf = p;
        }

        if (blen > 0) {
            p += blen;
            *p++ = ',';
            *p++ = ' ';
        }
    }

    strcpy(p, s);
    return 1;
}

static int util_flags(BIO *out, unsigned int flags, const char *indent)
{
    int started = 0, err = 0;
    /* Indent before displaying input flags */
    BIO_printf(out, "%s%s(input flags): ", indent, indent);
    if (flags == 0) {
        BIO_printf(out, "<no flags>\n");
        return 1;
    }
    /*
     * If the object is internal, mark it in a way that shows instead of
     * having it part of all the other flags, even if it really is.
     */
    if (flags & ENGINE_CMD_FLAG_INTERNAL) {
        BIO_printf(out, "[Internal] ");
    }

    if (flags & ENGINE_CMD_FLAG_NUMERIC) {
        BIO_printf(out, "NUMERIC");
        started = 1;
    }
    /*
     * Now we check that no combinations of the mutually exclusive NUMERIC,
     * STRING, and NO_INPUT flags have been used. Future flags that can be
     * OR'd together with these would need to added after these to preserve
     * the testing logic.
     */
    if (flags & ENGINE_CMD_FLAG_STRING) {
        if (started) {
            BIO_printf(out, "|");
            err = 1;
        }
        BIO_printf(out, "STRING");
        started = 1;
    }
    if (flags & ENGINE_CMD_FLAG_NO_INPUT) {
        if (started) {
            BIO_printf(out, "|");
            err = 1;
        }
        BIO_printf(out, "NO_INPUT");
        started = 1;
    }
    /* Check for unknown flags */
    flags = flags & ~ENGINE_CMD_FLAG_NUMERIC &
        ~ENGINE_CMD_FLAG_STRING &
        ~ENGINE_CMD_FLAG_NO_INPUT & ~ENGINE_CMD_FLAG_INTERNAL;
    if (flags) {
        if (started)
            BIO_printf(out, "|");
        BIO_printf(out, "<0x%04X>", flags);
    }
    if (err)
        BIO_printf(out, "  <illegal flags!>");
    BIO_printf(out, "\n");
    return 1;
}

static int util_verbose(ENGINE *e, int verbose, BIO *out, const char *indent)
{
    static const int line_wrap = 78;
    int num;
    int ret = 0;
    char *name = NULL;
    char *desc = NULL;
    int flags;
    int xpos = 0;
    STACK_OF(OPENSSL_STRING) *cmds = NULL;
    if (!ENGINE_ctrl(e, ENGINE_CTRL_HAS_CTRL_FUNCTION, 0, NULL, NULL) ||
        ((num = ENGINE_ctrl(e, ENGINE_CTRL_GET_FIRST_CMD_TYPE,
                            0, NULL, NULL)) <= 0)) {
        return 1;
    }

    cmds = sk_OPENSSL_STRING_new_null();
    if (cmds == NULL)
        goto err;

    do {
        int len;
        /* Get the command input flags */
        if ((flags = ENGINE_ctrl(e, ENGINE_CTRL_GET_CMD_FLAGS, num,
                                 NULL, NULL)) < 0)
            goto err;
        if (!(flags & ENGINE_CMD_FLAG_INTERNAL) || verbose >= 4) {
            /* Get the command name */
            if ((len = ENGINE_ctrl(e, ENGINE_CTRL_GET_NAME_LEN_FROM_CMD, num,
                                   NULL, NULL)) <= 0)
                goto err;
            name = app_malloc(len + 1, "name buffer");
            if (ENGINE_ctrl(e, ENGINE_CTRL_GET_NAME_FROM_CMD, num, name,
                            NULL) <= 0)
                goto err;
            /* Get the command description */
            if ((len = ENGINE_ctrl(e, ENGINE_CTRL_GET_DESC_LEN_FROM_CMD, num,
                                   NULL, NULL)) < 0)
                goto err;
            if (len > 0) {
                desc = app_malloc(len + 1, "description buffer");
                if (ENGINE_ctrl(e, ENGINE_CTRL_GET_DESC_FROM_CMD, num, desc,
                                NULL) <= 0)
                    goto err;
            }
            /* Now decide on the output */
            if (xpos == 0)
                /* Do an indent */
                xpos = BIO_puts(out, indent);
            else
                /* Otherwise prepend a ", " */
                xpos += BIO_printf(out, ", ");
            if (verbose == 1) {
                /*
                 * We're just listing names, comma-delimited
                 */
                if ((xpos > (int)strlen(indent)) &&
                    (xpos + (int)strlen(name) > line_wrap)) {
                    BIO_printf(out, "\n");
                    xpos = BIO_puts(out, indent);
                }
                xpos += BIO_printf(out, "%s", name);
            } else {
                /* We're listing names plus descriptions */
                BIO_printf(out, "%s: %s\n", name,
                           (desc == NULL) ? "<no description>" : desc);
                /* ... and sometimes input flags */
                if ((verbose >= 3) && !util_flags(out, flags, indent))
                    goto err;
                xpos = 0;
            }
        }
        OPENSSL_free(name);
        name = NULL;
        OPENSSL_free(desc);
        desc = NULL;
        /* Move to the next command */
        num = ENGINE_ctrl(e, ENGINE_CTRL_GET_NEXT_CMD_TYPE, num, NULL, NULL);
    } while (num > 0);
    if (xpos > 0)
        BIO_printf(out, "\n");
    ret = 1;
 err:
    sk_OPENSSL_STRING_free(cmds);
    OPENSSL_free(name);
    OPENSSL_free(desc);
    return ret;
}

static void util_do_cmds(ENGINE *e, STACK_OF(OPENSSL_STRING) *cmds,
                         BIO *out, const char *indent)
{
    int loop, res, num = sk_OPENSSL_STRING_num(cmds);

    if (num < 0) {
        BIO_printf(out, "[Error]: internal stack error\n");
        return;
    }
    for (loop = 0; loop < num; loop++) {
        char buf[256];
        const char *cmd, *arg;
        cmd = sk_OPENSSL_STRING_value(cmds, loop);
        res = 1;                /* assume success */
        /* Check if this command has no ":arg" */
        if ((arg = strstr(cmd, ":")) == NULL) {
            if (!ENGINE_ctrl_cmd_string(e, cmd, NULL, 0))
                res = 0;
        } else {
            if ((int)(arg - cmd) > 254) {
                BIO_printf(out, "[Error]: command name too long\n");
                return;
            }
            memcpy(buf, cmd, (int)(arg - cmd));
            buf[arg - cmd] = '\0';
            arg++;              /* Move past the ":" */
            /* Call the command with the argument */
            if (!ENGINE_ctrl_cmd_string(e, buf, arg, 0))
                res = 0;
        }
        if (res) {
            BIO_printf(out, "[Success]: %s\n", cmd);
        } else {
            BIO_printf(out, "[Failure]: %s\n", cmd);
            ERR_print_errors(out);
        }
    }
}

struct util_store_cap_data {
    ENGINE *engine;
    char **cap_buf;
    int *cap_size;
    int ok;
};
static void util_store_cap(const OSSL_STORE_LOADER *loader, void *arg)
{
    struct util_store_cap_data *ctx = arg;

    if (OSSL_STORE_LOADER_get0_engine(loader) == ctx->engine) {
        char buf[256];
        BIO_snprintf(buf, sizeof(buf), "STORE(%s)",
                     OSSL_STORE_LOADER_get0_scheme(loader));
        if (!append_buf(ctx->cap_buf, ctx->cap_size, buf))
            ctx->ok = 0;
    }
}

int engine_main(int argc, char **argv)
{
    int ret = 1, i;
    int verbose = 0, list_cap = 0, test_avail = 0, test_avail_noise = 0;
    ENGINE *e;
    STACK_OF(OPENSSL_CSTRING) *engines = sk_OPENSSL_CSTRING_new_null();
    STACK_OF(OPENSSL_STRING) *pre_cmds = sk_OPENSSL_STRING_new_null();
    STACK_OF(OPENSSL_STRING) *post_cmds = sk_OPENSSL_STRING_new_null();
    BIO *out;
    const char *indent = "     ";
    OPTION_CHOICE o;
    char *prog;
    char *argv1;

    out = dup_bio_out(FORMAT_TEXT);
    if (engines == NULL || pre_cmds == NULL || post_cmds == NULL)
        goto end;

    /* Remember the original command name, parse/skip any leading engine
     * names, and then setup to parse the rest of the line as flags. */
    prog = argv[0];
    while ((argv1 = argv[1]) != NULL && *argv1 != '-') {
        sk_OPENSSL_CSTRING_push(engines, argv1);
        argc--;
        argv++;
    }
    argv[0] = prog;
    opt_init(argc, argv, engine_options);

    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(engine_options);
            ret = 0;
            goto end;
        case OPT_VVVV:
        case OPT_VVV:
        case OPT_VV:
        case OPT_V:
            /* Convert to an integer from one to four. */
            i = (int)(o - OPT_V) + 1;
            if (verbose < i)
                verbose = i;
            break;
        case OPT_C:
            list_cap = 1;
            break;
        case OPT_TT:
            test_avail_noise++;
            /* fall thru */
        case OPT_T:
            test_avail++;
            break;
        case OPT_PRE:
            sk_OPENSSL_STRING_push(pre_cmds, opt_arg());
            break;
        case OPT_POST:
            sk_OPENSSL_STRING_push(post_cmds, opt_arg());
            break;
        }
    }

    /* Allow any trailing parameters as engine names. */
    argc = opt_num_rest();
    argv = opt_rest();
    for ( ; *argv; argv++) {
        if (**argv == '-') {
            BIO_printf(bio_err, "%s: Cannot mix flags and engine names.\n",
                       prog);
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        }
        sk_OPENSSL_CSTRING_push(engines, *argv);
    }

    if (sk_OPENSSL_CSTRING_num(engines) == 0) {
        for (e = ENGINE_get_first(); e != NULL; e = ENGINE_get_next(e)) {
            sk_OPENSSL_CSTRING_push(engines, ENGINE_get_id(e));
        }
    }

    ret = 0;
    for (i = 0; i < sk_OPENSSL_CSTRING_num(engines); i++) {
        const char *id = sk_OPENSSL_CSTRING_value(engines, i);
        if ((e = ENGINE_by_id(id)) != NULL) {
            const char *name = ENGINE_get_name(e);
            /*
             * Do "id" first, then "name". Easier to auto-parse.
             */
            BIO_printf(out, "(%s) %s\n", id, name);
            util_do_cmds(e, pre_cmds, out, indent);
            if (strcmp(ENGINE_get_id(e), id) != 0) {
                BIO_printf(out, "Loaded: (%s) %s\n",
                           ENGINE_get_id(e), ENGINE_get_name(e));
            }
            if (list_cap) {
                int cap_size = 256;
                char *cap_buf = NULL;
                int k, n;
                const int *nids;
                ENGINE_CIPHERS_PTR fn_c;
                ENGINE_DIGESTS_PTR fn_d;
                ENGINE_PKEY_METHS_PTR fn_pk;

                if (ENGINE_get_RSA(e) != NULL
                    && !append_buf(&cap_buf, &cap_size, "RSA"))
                    goto end;
                if (ENGINE_get_DSA(e) != NULL
                    && !append_buf(&cap_buf, &cap_size, "DSA"))
                    goto end;
                if (ENGINE_get_DH(e) != NULL
                    && !append_buf(&cap_buf, &cap_size, "DH"))
                    goto end;
                if (ENGINE_get_RAND(e) != NULL
                    && !append_buf(&cap_buf, &cap_size, "RAND"))
                    goto end;

                fn_c = ENGINE_get_ciphers(e);
                if (fn_c == NULL)
                    goto skip_ciphers;
                n = fn_c(e, NULL, &nids, 0);
                for (k = 0; k < n; ++k)
                    if (!append_buf(&cap_buf, &cap_size, OBJ_nid2sn(nids[k])))
                        goto end;

 skip_ciphers:
                fn_d = ENGINE_get_digests(e);
                if (fn_d == NULL)
                    goto skip_digests;
                n = fn_d(e, NULL, &nids, 0);
                for (k = 0; k < n; ++k)
                    if (!append_buf(&cap_buf, &cap_size, OBJ_nid2sn(nids[k])))
                        goto end;

 skip_digests:
                fn_pk = ENGINE_get_pkey_meths(e);
                if (fn_pk == NULL)
                    goto skip_pmeths;
                n = fn_pk(e, NULL, &nids, 0);
                for (k = 0; k < n; ++k)
                    if (!append_buf(&cap_buf, &cap_size, OBJ_nid2sn(nids[k])))
                        goto end;
 skip_pmeths:
                {
                    struct util_store_cap_data store_ctx;

                    store_ctx.engine = e;
                    store_ctx.cap_buf = &cap_buf;
                    store_ctx.cap_size = &cap_size;
                    store_ctx.ok = 1;

                    OSSL_STORE_do_all_loaders(util_store_cap, &store_ctx);
                    if (!store_ctx.ok)
                        goto end;
                }
                if (cap_buf != NULL && (*cap_buf != '\0'))
                    BIO_printf(out, " [%s]\n", cap_buf);

                OPENSSL_free(cap_buf);
            }
            if (test_avail) {
                BIO_printf(out, "%s", indent);
                if (ENGINE_init(e)) {
                    BIO_printf(out, "[ available ]\n");
                    util_do_cmds(e, post_cmds, out, indent);
                    ENGINE_finish(e);
                } else {
                    BIO_printf(out, "[ unavailable ]\n");
                    if (test_avail_noise)
                        ERR_print_errors_fp(stdout);
                    ERR_clear_error();
                }
            }
            if ((verbose > 0) && !util_verbose(e, verbose, out, indent))
                goto end;
            ENGINE_free(e);
        } else {
            ERR_print_errors(bio_err);
            /* because exit codes above 127 have special meaning on Unix */
            if (++ret > 127)
                ret = 127;
        }
    }

 end:

    ERR_print_errors(bio_err);
    sk_OPENSSL_CSTRING_free(engines);
    sk_OPENSSL_STRING_free(pre_cmds);
    sk_OPENSSL_STRING_free(post_cmds);
    BIO_free_all(out);
    return ret;
}
#endif
