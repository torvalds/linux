/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <krb5_locl.h>

/* These are stub functions for the standalone RFC3961 crypto library */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_context(krb5_context *context)
{
    krb5_context p;

    *context = NULL;

    /* should have a run_once */
    bindtextdomain(HEIMDAL_TEXTDOMAIN, HEIMDAL_LOCALEDIR);

    p = calloc(1, sizeof(*p));
    if(!p)
        return ENOMEM;

    p->mutex = malloc(sizeof(HEIMDAL_MUTEX));
    if (p->mutex == NULL) {
        free(p);
        return ENOMEM;
    }
    HEIMDAL_MUTEX_init(p->mutex);

    *context = p;
    return 0;
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_context(krb5_context context)
{
    krb5_clear_error_message(context);

    HEIMDAL_MUTEX_destroy(context->mutex);
    free(context->mutex);
    if (context->flags & KRB5_CTX_F_SOCKETS_INITIALIZED) {
        rk_SOCK_EXIT();
    }

    memset(context, 0, sizeof(*context));
    free(context);
}

krb5_boolean
_krb5_homedir_access(krb5_context context) {
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_log(krb5_context context,
         krb5_log_facility *fac,
         int level,
         const char *fmt,
         ...)
{
    return 0;
}

/* This function is currently just used to get the location of the EGD
 * socket. If we're not using an EGD, then we can just return NULL */

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_config_get_string (krb5_context context,
                        const krb5_config_section *c,
                        ...)
{
    return NULL;
}
