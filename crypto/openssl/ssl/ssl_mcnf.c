/*
 * Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <openssl/conf.h>
#include <openssl/ssl.h>
#include "ssl_locl.h"
#include "internal/sslconf.h"

/* SSL library configuration module. */

void SSL_add_ssl_module(void)
{
    /* Do nothing. This will be added automatically by libcrypto */
}

static int ssl_do_config(SSL *s, SSL_CTX *ctx, const char *name, int system)
{
    SSL_CONF_CTX *cctx = NULL;
    size_t i, idx, cmd_count;
    int rv = 0;
    unsigned int flags;
    const SSL_METHOD *meth;
    const SSL_CONF_CMD *cmds;

    if (s == NULL && ctx == NULL) {
        SSLerr(SSL_F_SSL_DO_CONFIG, ERR_R_PASSED_NULL_PARAMETER);
        goto err;
    }

    if (name == NULL && system)
        name = "system_default";
    if (!conf_ssl_name_find(name, &idx)) {
        if (!system) {
            SSLerr(SSL_F_SSL_DO_CONFIG, SSL_R_INVALID_CONFIGURATION_NAME);
            ERR_add_error_data(2, "name=", name);
        }
        goto err;
    }
    cmds = conf_ssl_get(idx, &name, &cmd_count);
    cctx = SSL_CONF_CTX_new();
    if (cctx == NULL)
        goto err;
    flags = SSL_CONF_FLAG_FILE;
    if (!system)
        flags |= SSL_CONF_FLAG_CERTIFICATE | SSL_CONF_FLAG_REQUIRE_PRIVATE;
    if (s != NULL) {
        meth = s->method;
        SSL_CONF_CTX_set_ssl(cctx, s);
    } else {
        meth = ctx->method;
        SSL_CONF_CTX_set_ssl_ctx(cctx, ctx);
    }
    if (meth->ssl_accept != ssl_undefined_function)
        flags |= SSL_CONF_FLAG_SERVER;
    if (meth->ssl_connect != ssl_undefined_function)
        flags |= SSL_CONF_FLAG_CLIENT;
    SSL_CONF_CTX_set_flags(cctx, flags);
    for (i = 0; i < cmd_count; i++) {
        char *cmdstr, *arg;

        conf_ssl_get_cmd(cmds, i, &cmdstr, &arg);
        rv = SSL_CONF_cmd(cctx, cmdstr, arg);
        if (rv <= 0) {
            if (rv == -2)
                SSLerr(SSL_F_SSL_DO_CONFIG, SSL_R_UNKNOWN_COMMAND);
            else
                SSLerr(SSL_F_SSL_DO_CONFIG, SSL_R_BAD_VALUE);
            ERR_add_error_data(6, "section=", name, ", cmd=", cmdstr,
                               ", arg=", arg);
            goto err;
        }
    }
    rv = SSL_CONF_CTX_finish(cctx);
 err:
    SSL_CONF_CTX_free(cctx);
    return rv <= 0 ? 0 : 1;
}

int SSL_config(SSL *s, const char *name)
{
    return ssl_do_config(s, NULL, name, 0);
}

int SSL_CTX_config(SSL_CTX *ctx, const char *name)
{
    return ssl_do_config(NULL, ctx, name, 0);
}

void ssl_ctx_system_config(SSL_CTX *ctx)
{
    ssl_do_config(NULL, ctx, NULL, 1);
}
