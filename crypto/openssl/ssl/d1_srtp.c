/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * DTLS code by Eric Rescorla <ekr@rtfm.com>
 *
 * Copyright (C) 2006, Network Resonance, Inc. Copyright (C) 2011, RTFM, Inc.
 */

#include <stdio.h>
#include <openssl/objects.h>
#include "ssl_locl.h"

#ifndef OPENSSL_NO_SRTP

static SRTP_PROTECTION_PROFILE srtp_known_profiles[] = {
    {
     "SRTP_AES128_CM_SHA1_80",
     SRTP_AES128_CM_SHA1_80,
     },
    {
     "SRTP_AES128_CM_SHA1_32",
     SRTP_AES128_CM_SHA1_32,
     },
    {
     "SRTP_AEAD_AES_128_GCM",
     SRTP_AEAD_AES_128_GCM,
     },
    {
     "SRTP_AEAD_AES_256_GCM",
     SRTP_AEAD_AES_256_GCM,
     },
    {0}
};

static int find_profile_by_name(char *profile_name,
                                SRTP_PROTECTION_PROFILE **pptr, size_t len)
{
    SRTP_PROTECTION_PROFILE *p;

    p = srtp_known_profiles;
    while (p->name) {
        if ((len == strlen(p->name))
            && strncmp(p->name, profile_name, len) == 0) {
            *pptr = p;
            return 0;
        }

        p++;
    }

    return 1;
}

static int ssl_ctx_make_profiles(const char *profiles_string,
                                 STACK_OF(SRTP_PROTECTION_PROFILE) **out)
{
    STACK_OF(SRTP_PROTECTION_PROFILE) *profiles;

    char *col;
    char *ptr = (char *)profiles_string;
    SRTP_PROTECTION_PROFILE *p;

    if ((profiles = sk_SRTP_PROTECTION_PROFILE_new_null()) == NULL) {
        SSLerr(SSL_F_SSL_CTX_MAKE_PROFILES,
               SSL_R_SRTP_COULD_NOT_ALLOCATE_PROFILES);
        return 1;
    }

    do {
        col = strchr(ptr, ':');

        if (!find_profile_by_name(ptr, &p, col ? (size_t)(col - ptr)
                                               : strlen(ptr))) {
            if (sk_SRTP_PROTECTION_PROFILE_find(profiles, p) >= 0) {
                SSLerr(SSL_F_SSL_CTX_MAKE_PROFILES,
                       SSL_R_BAD_SRTP_PROTECTION_PROFILE_LIST);
                goto err;
            }

            if (!sk_SRTP_PROTECTION_PROFILE_push(profiles, p)) {
                SSLerr(SSL_F_SSL_CTX_MAKE_PROFILES,
                       SSL_R_SRTP_COULD_NOT_ALLOCATE_PROFILES);
                goto err;
            }
        } else {
            SSLerr(SSL_F_SSL_CTX_MAKE_PROFILES,
                   SSL_R_SRTP_UNKNOWN_PROTECTION_PROFILE);
            goto err;
        }

        if (col)
            ptr = col + 1;
    } while (col);

    sk_SRTP_PROTECTION_PROFILE_free(*out);

    *out = profiles;

    return 0;
 err:
    sk_SRTP_PROTECTION_PROFILE_free(profiles);
    return 1;
}

int SSL_CTX_set_tlsext_use_srtp(SSL_CTX *ctx, const char *profiles)
{
    return ssl_ctx_make_profiles(profiles, &ctx->srtp_profiles);
}

int SSL_set_tlsext_use_srtp(SSL *s, const char *profiles)
{
    return ssl_ctx_make_profiles(profiles, &s->srtp_profiles);
}

STACK_OF(SRTP_PROTECTION_PROFILE) *SSL_get_srtp_profiles(SSL *s)
{
    if (s != NULL) {
        if (s->srtp_profiles != NULL) {
            return s->srtp_profiles;
        } else if ((s->ctx != NULL) && (s->ctx->srtp_profiles != NULL)) {
            return s->ctx->srtp_profiles;
        }
    }

    return NULL;
}

SRTP_PROTECTION_PROFILE *SSL_get_selected_srtp_profile(SSL *s)
{
    return s->srtp_profile;
}
#endif
