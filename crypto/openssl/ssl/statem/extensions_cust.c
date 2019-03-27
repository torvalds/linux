/*
 * Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Custom extension utility functions */

#include <openssl/ct.h>
#include "../ssl_locl.h"
#include "internal/cryptlib.h"
#include "statem_locl.h"

typedef struct {
    void *add_arg;
    custom_ext_add_cb add_cb;
    custom_ext_free_cb free_cb;
} custom_ext_add_cb_wrap;

typedef struct {
    void *parse_arg;
    custom_ext_parse_cb parse_cb;
} custom_ext_parse_cb_wrap;

/*
 * Provide thin wrapper callbacks which convert new style arguments to old style
 */
static int custom_ext_add_old_cb_wrap(SSL *s, unsigned int ext_type,
                                      unsigned int context,
                                      const unsigned char **out,
                                      size_t *outlen, X509 *x, size_t chainidx,
                                      int *al, void *add_arg)
{
    custom_ext_add_cb_wrap *add_cb_wrap = (custom_ext_add_cb_wrap *)add_arg;

    if (add_cb_wrap->add_cb == NULL)
        return 1;

    return add_cb_wrap->add_cb(s, ext_type, out, outlen, al,
                               add_cb_wrap->add_arg);
}

static void custom_ext_free_old_cb_wrap(SSL *s, unsigned int ext_type,
                                        unsigned int context,
                                        const unsigned char *out, void *add_arg)
{
    custom_ext_add_cb_wrap *add_cb_wrap = (custom_ext_add_cb_wrap *)add_arg;

    if (add_cb_wrap->free_cb == NULL)
        return;

    add_cb_wrap->free_cb(s, ext_type, out, add_cb_wrap->add_arg);
}

static int custom_ext_parse_old_cb_wrap(SSL *s, unsigned int ext_type,
                                        unsigned int context,
                                        const unsigned char *in,
                                        size_t inlen, X509 *x, size_t chainidx,
                                        int *al, void *parse_arg)
{
    custom_ext_parse_cb_wrap *parse_cb_wrap =
        (custom_ext_parse_cb_wrap *)parse_arg;

    if (parse_cb_wrap->parse_cb == NULL)
        return 1;

    return parse_cb_wrap->parse_cb(s, ext_type, in, inlen, al,
                                   parse_cb_wrap->parse_arg);
}

/*
 * Find a custom extension from the list. The |role| param is there to
 * support the legacy API where custom extensions for client and server could
 * be set independently on the same SSL_CTX. It is set to ENDPOINT_SERVER if we
 * are trying to find a method relevant to the server, ENDPOINT_CLIENT for the
 * client, or ENDPOINT_BOTH for either
 */
custom_ext_method *custom_ext_find(const custom_ext_methods *exts,
                                   ENDPOINT role, unsigned int ext_type,
                                   size_t *idx)
{
    size_t i;
    custom_ext_method *meth = exts->meths;

    for (i = 0; i < exts->meths_count; i++, meth++) {
        if (ext_type == meth->ext_type
                && (role == ENDPOINT_BOTH || role == meth->role
                    || meth->role == ENDPOINT_BOTH)) {
            if (idx != NULL)
                *idx = i;
            return meth;
        }
    }
    return NULL;
}

/*
 * Initialise custom extensions flags to indicate neither sent nor received.
 */
void custom_ext_init(custom_ext_methods *exts)
{
    size_t i;
    custom_ext_method *meth = exts->meths;

    for (i = 0; i < exts->meths_count; i++, meth++)
        meth->ext_flags = 0;
}

/* Pass received custom extension data to the application for parsing. */
int custom_ext_parse(SSL *s, unsigned int context, unsigned int ext_type,
                     const unsigned char *ext_data, size_t ext_size, X509 *x,
                     size_t chainidx)
{
    int al;
    custom_ext_methods *exts = &s->cert->custext;
    custom_ext_method *meth;
    ENDPOINT role = ENDPOINT_BOTH;

    if ((context & (SSL_EXT_CLIENT_HELLO | SSL_EXT_TLS1_2_SERVER_HELLO)) != 0)
        role = s->server ? ENDPOINT_SERVER : ENDPOINT_CLIENT;

    meth = custom_ext_find(exts, role, ext_type, NULL);
    /* If not found return success */
    if (!meth)
        return 1;

    /* Check if extension is defined for our protocol. If not, skip */
    if (!extension_is_relevant(s, meth->context, context))
        return 1;

    if ((context & (SSL_EXT_TLS1_2_SERVER_HELLO
                    | SSL_EXT_TLS1_3_SERVER_HELLO
                    | SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS)) != 0) {
        /*
         * If it's ServerHello or EncryptedExtensions we can't have any
         * extensions not sent in ClientHello.
         */
        if ((meth->ext_flags & SSL_EXT_FLAG_SENT) == 0) {
            SSLfatal(s, TLS1_AD_UNSUPPORTED_EXTENSION, SSL_F_CUSTOM_EXT_PARSE,
                     SSL_R_BAD_EXTENSION);
            return 0;
        }
    }

    /*
     * Extensions received in the ClientHello are marked with the
     * SSL_EXT_FLAG_RECEIVED. This is so we know to add the equivalent
     * extensions in the ServerHello/EncryptedExtensions message
     */
    if ((context & SSL_EXT_CLIENT_HELLO) != 0)
        meth->ext_flags |= SSL_EXT_FLAG_RECEIVED;

    /* If no parse function set return success */
    if (!meth->parse_cb)
        return 1;

    if (meth->parse_cb(s, ext_type, context, ext_data, ext_size, x, chainidx,
                       &al, meth->parse_arg) <= 0) {
        SSLfatal(s, al, SSL_F_CUSTOM_EXT_PARSE, SSL_R_BAD_EXTENSION);
        return 0;
    }

    return 1;
}

/*
 * Request custom extension data from the application and add to the return
 * buffer.
 */
int custom_ext_add(SSL *s, int context, WPACKET *pkt, X509 *x, size_t chainidx,
                   int maxversion)
{
    custom_ext_methods *exts = &s->cert->custext;
    custom_ext_method *meth;
    size_t i;
    int al;

    for (i = 0; i < exts->meths_count; i++) {
        const unsigned char *out = NULL;
        size_t outlen = 0;

        meth = exts->meths + i;

        if (!should_add_extension(s, meth->context, context, maxversion))
            continue;

        if ((context & (SSL_EXT_TLS1_2_SERVER_HELLO
                        | SSL_EXT_TLS1_3_SERVER_HELLO
                        | SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS
                        | SSL_EXT_TLS1_3_CERTIFICATE
                        | SSL_EXT_TLS1_3_HELLO_RETRY_REQUEST)) != 0) {
            /* Only send extensions present in ClientHello. */
            if (!(meth->ext_flags & SSL_EXT_FLAG_RECEIVED))
                continue;
        }
        /*
         * We skip it if the callback is absent - except for a ClientHello where
         * we add an empty extension.
         */
        if ((context & SSL_EXT_CLIENT_HELLO) == 0 && meth->add_cb == NULL)
            continue;

        if (meth->add_cb != NULL) {
            int cb_retval = meth->add_cb(s, meth->ext_type, context, &out,
                                         &outlen, x, chainidx, &al,
                                         meth->add_arg);

            if (cb_retval < 0) {
                SSLfatal(s, al, SSL_F_CUSTOM_EXT_ADD, SSL_R_CALLBACK_FAILED);
                return 0;       /* error */
            }
            if (cb_retval == 0)
                continue;       /* skip this extension */
        }

        if (!WPACKET_put_bytes_u16(pkt, meth->ext_type)
                || !WPACKET_start_sub_packet_u16(pkt)
                || (outlen > 0 && !WPACKET_memcpy(pkt, out, outlen))
                || !WPACKET_close(pkt)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_CUSTOM_EXT_ADD,
                     ERR_R_INTERNAL_ERROR);
            return 0;
        }
        if ((context & SSL_EXT_CLIENT_HELLO) != 0) {
            /*
             * We can't send duplicates: code logic should prevent this.
             */
            if (!ossl_assert((meth->ext_flags & SSL_EXT_FLAG_SENT) == 0)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_CUSTOM_EXT_ADD,
                         ERR_R_INTERNAL_ERROR);
                return 0;
            }
            /*
             * Indicate extension has been sent: this is both a sanity check to
             * ensure we don't send duplicate extensions and indicates that it
             * is not an error if the extension is present in ServerHello.
             */
            meth->ext_flags |= SSL_EXT_FLAG_SENT;
        }
        if (meth->free_cb != NULL)
            meth->free_cb(s, meth->ext_type, context, out, meth->add_arg);
    }
    return 1;
}

/* Copy the flags from src to dst for any extensions that exist in both */
int custom_exts_copy_flags(custom_ext_methods *dst,
                           const custom_ext_methods *src)
{
    size_t i;
    custom_ext_method *methsrc = src->meths;

    for (i = 0; i < src->meths_count; i++, methsrc++) {
        custom_ext_method *methdst = custom_ext_find(dst, methsrc->role,
                                                     methsrc->ext_type, NULL);

        if (methdst == NULL)
            continue;

        methdst->ext_flags = methsrc->ext_flags;
    }

    return 1;
}

/* Copy table of custom extensions */
int custom_exts_copy(custom_ext_methods *dst, const custom_ext_methods *src)
{
    size_t i;
    int err = 0;

    if (src->meths_count > 0) {
        dst->meths =
            OPENSSL_memdup(src->meths,
                           sizeof(*src->meths) * src->meths_count);
        if (dst->meths == NULL)
            return 0;
        dst->meths_count = src->meths_count;

        for (i = 0; i < src->meths_count; i++) {
            custom_ext_method *methsrc = src->meths + i;
            custom_ext_method *methdst = dst->meths + i;

            if (methsrc->add_cb != custom_ext_add_old_cb_wrap)
                continue;

            /*
             * We have found an old style API wrapper. We need to copy the
             * arguments too.
             */

            if (err) {
                methdst->add_arg = NULL;
                methdst->parse_arg = NULL;
                continue;
            }

            methdst->add_arg = OPENSSL_memdup(methsrc->add_arg,
                                              sizeof(custom_ext_add_cb_wrap));
            methdst->parse_arg = OPENSSL_memdup(methsrc->parse_arg,
                                            sizeof(custom_ext_parse_cb_wrap));

            if (methdst->add_arg == NULL || methdst->parse_arg == NULL)
                err = 1;
        }
    }

    if (err) {
        custom_exts_free(dst);
        return 0;
    }

    return 1;
}

void custom_exts_free(custom_ext_methods *exts)
{
    size_t i;
    custom_ext_method *meth;

    for (i = 0, meth = exts->meths; i < exts->meths_count; i++, meth++) {
        if (meth->add_cb != custom_ext_add_old_cb_wrap)
            continue;

        /* Old style API wrapper. Need to free the arguments too */
        OPENSSL_free(meth->add_arg);
        OPENSSL_free(meth->parse_arg);
    }
    OPENSSL_free(exts->meths);
}

/* Return true if a client custom extension exists, false otherwise */
int SSL_CTX_has_client_custom_ext(const SSL_CTX *ctx, unsigned int ext_type)
{
    return custom_ext_find(&ctx->cert->custext, ENDPOINT_CLIENT, ext_type,
                           NULL) != NULL;
}

static int add_custom_ext_intern(SSL_CTX *ctx, ENDPOINT role,
                                 unsigned int ext_type,
                                 unsigned int context,
                                 SSL_custom_ext_add_cb_ex add_cb,
                                 SSL_custom_ext_free_cb_ex free_cb,
                                 void *add_arg,
                                 SSL_custom_ext_parse_cb_ex parse_cb,
                                 void *parse_arg)
{
    custom_ext_methods *exts = &ctx->cert->custext;
    custom_ext_method *meth, *tmp;

    /*
     * Check application error: if add_cb is not set free_cb will never be
     * called.
     */
    if (add_cb == NULL && free_cb != NULL)
        return 0;

#ifndef OPENSSL_NO_CT
    /*
     * We don't want applications registering callbacks for SCT extensions
     * whilst simultaneously using the built-in SCT validation features, as
     * these two things may not play well together.
     */
    if (ext_type == TLSEXT_TYPE_signed_certificate_timestamp
            && (context & SSL_EXT_CLIENT_HELLO) != 0
            && SSL_CTX_ct_is_enabled(ctx))
        return 0;
#endif

    /*
     * Don't add if extension supported internally, but make exception
     * for extension types that previously were not supported, but now are.
     */
    if (SSL_extension_supported(ext_type)
            && ext_type != TLSEXT_TYPE_signed_certificate_timestamp)
        return 0;

    /* Extension type must fit in 16 bits */
    if (ext_type > 0xffff)
        return 0;
    /* Search for duplicate */
    if (custom_ext_find(exts, role, ext_type, NULL))
        return 0;
    tmp = OPENSSL_realloc(exts->meths,
                          (exts->meths_count + 1) * sizeof(custom_ext_method));
    if (tmp == NULL)
        return 0;

    exts->meths = tmp;
    meth = exts->meths + exts->meths_count;
    memset(meth, 0, sizeof(*meth));
    meth->role = role;
    meth->context = context;
    meth->parse_cb = parse_cb;
    meth->add_cb = add_cb;
    meth->free_cb = free_cb;
    meth->ext_type = ext_type;
    meth->add_arg = add_arg;
    meth->parse_arg = parse_arg;
    exts->meths_count++;
    return 1;
}

static int add_old_custom_ext(SSL_CTX *ctx, ENDPOINT role,
                              unsigned int ext_type,
                              unsigned int context,
                              custom_ext_add_cb add_cb,
                              custom_ext_free_cb free_cb,
                              void *add_arg,
                              custom_ext_parse_cb parse_cb, void *parse_arg)
{
    custom_ext_add_cb_wrap *add_cb_wrap
        = OPENSSL_malloc(sizeof(*add_cb_wrap));
    custom_ext_parse_cb_wrap *parse_cb_wrap
        = OPENSSL_malloc(sizeof(*parse_cb_wrap));
    int ret;

    if (add_cb_wrap == NULL || parse_cb_wrap == NULL) {
        OPENSSL_free(add_cb_wrap);
        OPENSSL_free(parse_cb_wrap);
        return 0;
    }

    add_cb_wrap->add_arg = add_arg;
    add_cb_wrap->add_cb = add_cb;
    add_cb_wrap->free_cb = free_cb;
    parse_cb_wrap->parse_arg = parse_arg;
    parse_cb_wrap->parse_cb = parse_cb;

    ret = add_custom_ext_intern(ctx, role, ext_type,
                                context,
                                custom_ext_add_old_cb_wrap,
                                custom_ext_free_old_cb_wrap,
                                add_cb_wrap,
                                custom_ext_parse_old_cb_wrap,
                                parse_cb_wrap);

    if (!ret) {
        OPENSSL_free(add_cb_wrap);
        OPENSSL_free(parse_cb_wrap);
    }

    return ret;
}

/* Application level functions to add the old custom extension callbacks */
int SSL_CTX_add_client_custom_ext(SSL_CTX *ctx, unsigned int ext_type,
                                  custom_ext_add_cb add_cb,
                                  custom_ext_free_cb free_cb,
                                  void *add_arg,
                                  custom_ext_parse_cb parse_cb, void *parse_arg)
{
    return add_old_custom_ext(ctx, ENDPOINT_CLIENT, ext_type,
                              SSL_EXT_TLS1_2_AND_BELOW_ONLY
                              | SSL_EXT_CLIENT_HELLO
                              | SSL_EXT_TLS1_2_SERVER_HELLO
                              | SSL_EXT_IGNORE_ON_RESUMPTION,
                              add_cb, free_cb, add_arg, parse_cb, parse_arg);
}

int SSL_CTX_add_server_custom_ext(SSL_CTX *ctx, unsigned int ext_type,
                                  custom_ext_add_cb add_cb,
                                  custom_ext_free_cb free_cb,
                                  void *add_arg,
                                  custom_ext_parse_cb parse_cb, void *parse_arg)
{
    return add_old_custom_ext(ctx, ENDPOINT_SERVER, ext_type,
                              SSL_EXT_TLS1_2_AND_BELOW_ONLY
                              | SSL_EXT_CLIENT_HELLO
                              | SSL_EXT_TLS1_2_SERVER_HELLO
                              | SSL_EXT_IGNORE_ON_RESUMPTION,
                              add_cb, free_cb, add_arg, parse_cb, parse_arg);
}

int SSL_CTX_add_custom_ext(SSL_CTX *ctx, unsigned int ext_type,
                           unsigned int context,
                           SSL_custom_ext_add_cb_ex add_cb,
                           SSL_custom_ext_free_cb_ex free_cb,
                           void *add_arg,
                           SSL_custom_ext_parse_cb_ex parse_cb, void *parse_arg)
{
    return add_custom_ext_intern(ctx, ENDPOINT_BOTH, ext_type, context, add_cb,
                                 free_cb, add_arg, parse_cb, parse_arg);
}

int SSL_extension_supported(unsigned int ext_type)
{
    switch (ext_type) {
        /* Internally supported extensions. */
    case TLSEXT_TYPE_application_layer_protocol_negotiation:
#ifndef OPENSSL_NO_EC
    case TLSEXT_TYPE_ec_point_formats:
    case TLSEXT_TYPE_supported_groups:
    case TLSEXT_TYPE_key_share:
#endif
#ifndef OPENSSL_NO_NEXTPROTONEG
    case TLSEXT_TYPE_next_proto_neg:
#endif
    case TLSEXT_TYPE_padding:
    case TLSEXT_TYPE_renegotiate:
    case TLSEXT_TYPE_max_fragment_length:
    case TLSEXT_TYPE_server_name:
    case TLSEXT_TYPE_session_ticket:
    case TLSEXT_TYPE_signature_algorithms:
#ifndef OPENSSL_NO_SRP
    case TLSEXT_TYPE_srp:
#endif
#ifndef OPENSSL_NO_OCSP
    case TLSEXT_TYPE_status_request:
#endif
#ifndef OPENSSL_NO_CT
    case TLSEXT_TYPE_signed_certificate_timestamp:
#endif
#ifndef OPENSSL_NO_SRTP
    case TLSEXT_TYPE_use_srtp:
#endif
    case TLSEXT_TYPE_encrypt_then_mac:
    case TLSEXT_TYPE_supported_versions:
    case TLSEXT_TYPE_extended_master_secret:
    case TLSEXT_TYPE_psk_kex_modes:
    case TLSEXT_TYPE_cookie:
    case TLSEXT_TYPE_early_data:
    case TLSEXT_TYPE_certificate_authorities:
    case TLSEXT_TYPE_psk:
    case TLSEXT_TYPE_post_handshake_auth:
        return 1;
    default:
        return 0;
    }
}
