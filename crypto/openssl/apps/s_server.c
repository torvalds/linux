/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 * Copyright 2005 Nokia. All rights reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
/* Included before async.h to avoid some warnings */
# include <windows.h>
#endif

#include <openssl/e_os2.h>
#include <openssl/async.h>
#include <openssl/ssl.h>

#ifndef OPENSSL_NO_SOCK

/*
 * With IPv6, it looks like Digital has mixed up the proper order of
 * recursive header file inclusion, resulting in the compiler complaining
 * that u_int isn't defined, but only if _POSIX_C_SOURCE is defined, which is
 * needed to have fileno() declared correctly...  So let's define u_int
 */
#if defined(OPENSSL_SYS_VMS_DECC) && !defined(__U_INT)
# define __U_INT
typedef unsigned int u_int;
#endif

#include <openssl/bn.h>
#include "apps.h"
#include "progs.h"
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/ocsp.h>
#ifndef OPENSSL_NO_DH
# include <openssl/dh.h>
#endif
#ifndef OPENSSL_NO_RSA
# include <openssl/rsa.h>
#endif
#ifndef OPENSSL_NO_SRP
# include <openssl/srp.h>
#endif
#include "s_apps.h"
#include "timeouts.h"
#ifdef CHARSET_EBCDIC
#include <openssl/ebcdic.h>
#endif
#include "internal/sockets.h"

static int not_resumable_sess_cb(SSL *s, int is_forward_secure);
static int sv_body(int s, int stype, int prot, unsigned char *context);
static int www_body(int s, int stype, int prot, unsigned char *context);
static int rev_body(int s, int stype, int prot, unsigned char *context);
static void close_accept_socket(void);
static int init_ssl_connection(SSL *s);
static void print_stats(BIO *bp, SSL_CTX *ctx);
static int generate_session_id(SSL *ssl, unsigned char *id,
                               unsigned int *id_len);
static void init_session_cache_ctx(SSL_CTX *sctx);
static void free_sessions(void);
#ifndef OPENSSL_NO_DH
static DH *load_dh_param(const char *dhfile);
#endif
static void print_connection_info(SSL *con);

static const int bufsize = 16 * 1024;
static int accept_socket = -1;

#define TEST_CERT       "server.pem"
#define TEST_CERT2      "server2.pem"

static int s_nbio = 0;
static int s_nbio_test = 0;
static int s_crlf = 0;
static SSL_CTX *ctx = NULL;
static SSL_CTX *ctx2 = NULL;
static int www = 0;

static BIO *bio_s_out = NULL;
static BIO *bio_s_msg = NULL;
static int s_debug = 0;
static int s_tlsextdebug = 0;
static int s_msg = 0;
static int s_quiet = 0;
static int s_ign_eof = 0;
static int s_brief = 0;

static char *keymatexportlabel = NULL;
static int keymatexportlen = 20;

static int async = 0;

static const char *session_id_prefix = NULL;

#ifndef OPENSSL_NO_DTLS
static int enable_timeouts = 0;
static long socket_mtu;
#endif

/*
 * We define this but make it always be 0 in no-dtls builds to simplify the
 * code.
 */
static int dtlslisten = 0;
static int stateless = 0;

static int early_data = 0;
static SSL_SESSION *psksess = NULL;

static char *psk_identity = "Client_identity";
char *psk_key = NULL;           /* by default PSK is not used */

#ifndef OPENSSL_NO_PSK
static unsigned int psk_server_cb(SSL *ssl, const char *identity,
                                  unsigned char *psk,
                                  unsigned int max_psk_len)
{
    long key_len = 0;
    unsigned char *key;

    if (s_debug)
        BIO_printf(bio_s_out, "psk_server_cb\n");
    if (identity == NULL) {
        BIO_printf(bio_err, "Error: client did not send PSK identity\n");
        goto out_err;
    }
    if (s_debug)
        BIO_printf(bio_s_out, "identity_len=%d identity=%s\n",
                   (int)strlen(identity), identity);

    /* here we could lookup the given identity e.g. from a database */
    if (strcmp(identity, psk_identity) != 0) {
        BIO_printf(bio_s_out, "PSK warning: client identity not what we expected"
                   " (got '%s' expected '%s')\n", identity, psk_identity);
    } else {
      if (s_debug)
        BIO_printf(bio_s_out, "PSK client identity found\n");
    }

    /* convert the PSK key to binary */
    key = OPENSSL_hexstr2buf(psk_key, &key_len);
    if (key == NULL) {
        BIO_printf(bio_err, "Could not convert PSK key '%s' to buffer\n",
                   psk_key);
        return 0;
    }
    if (key_len > (int)max_psk_len) {
        BIO_printf(bio_err,
                   "psk buffer of callback is too small (%d) for key (%ld)\n",
                   max_psk_len, key_len);
        OPENSSL_free(key);
        return 0;
    }

    memcpy(psk, key, key_len);
    OPENSSL_free(key);

    if (s_debug)
        BIO_printf(bio_s_out, "fetched PSK len=%ld\n", key_len);
    return key_len;
 out_err:
    if (s_debug)
        BIO_printf(bio_err, "Error in PSK server callback\n");
    (void)BIO_flush(bio_err);
    (void)BIO_flush(bio_s_out);
    return 0;
}
#endif

#define TLS13_AES_128_GCM_SHA256_BYTES  ((const unsigned char *)"\x13\x01")
#define TLS13_AES_256_GCM_SHA384_BYTES  ((const unsigned char *)"\x13\x02")

static int psk_find_session_cb(SSL *ssl, const unsigned char *identity,
                               size_t identity_len, SSL_SESSION **sess)
{
    SSL_SESSION *tmpsess = NULL;
    unsigned char *key;
    long key_len;
    const SSL_CIPHER *cipher = NULL;

    if (strlen(psk_identity) != identity_len
            || memcmp(psk_identity, identity, identity_len) != 0) {
        *sess = NULL;
        return 1;
    }

    if (psksess != NULL) {
        SSL_SESSION_up_ref(psksess);
        *sess = psksess;
        return 1;
    }

    key = OPENSSL_hexstr2buf(psk_key, &key_len);
    if (key == NULL) {
        BIO_printf(bio_err, "Could not convert PSK key '%s' to buffer\n",
                   psk_key);
        return 0;
    }

    /* We default to SHA256 */
    cipher = SSL_CIPHER_find(ssl, tls13_aes128gcmsha256_id);
    if (cipher == NULL) {
        BIO_printf(bio_err, "Error finding suitable ciphersuite\n");
        OPENSSL_free(key);
        return 0;
    }

    tmpsess = SSL_SESSION_new();
    if (tmpsess == NULL
            || !SSL_SESSION_set1_master_key(tmpsess, key, key_len)
            || !SSL_SESSION_set_cipher(tmpsess, cipher)
            || !SSL_SESSION_set_protocol_version(tmpsess, SSL_version(ssl))) {
        OPENSSL_free(key);
        return 0;
    }
    OPENSSL_free(key);
    *sess = tmpsess;

    return 1;
}

#ifndef OPENSSL_NO_SRP
/* This is a context that we pass to callbacks */
typedef struct srpsrvparm_st {
    char *login;
    SRP_VBASE *vb;
    SRP_user_pwd *user;
} srpsrvparm;
static srpsrvparm srp_callback_parm;

/*
 * This callback pretends to require some asynchronous logic in order to
 * obtain a verifier. When the callback is called for a new connection we
 * return with a negative value. This will provoke the accept etc to return
 * with an LOOKUP_X509. The main logic of the reinvokes the suspended call
 * (which would normally occur after a worker has finished) and we set the
 * user parameters.
 */
static int ssl_srp_server_param_cb(SSL *s, int *ad, void *arg)
{
    srpsrvparm *p = (srpsrvparm *) arg;
    int ret = SSL3_AL_FATAL;

    if (p->login == NULL && p->user == NULL) {
        p->login = SSL_get_srp_username(s);
        BIO_printf(bio_err, "SRP username = \"%s\"\n", p->login);
        return -1;
    }

    if (p->user == NULL) {
        BIO_printf(bio_err, "User %s doesn't exist\n", p->login);
        goto err;
    }

    if (SSL_set_srp_server_param
        (s, p->user->N, p->user->g, p->user->s, p->user->v,
         p->user->info) < 0) {
        *ad = SSL_AD_INTERNAL_ERROR;
        goto err;
    }
    BIO_printf(bio_err,
               "SRP parameters set: username = \"%s\" info=\"%s\" \n",
               p->login, p->user->info);
    ret = SSL_ERROR_NONE;

 err:
    SRP_user_pwd_free(p->user);
    p->user = NULL;
    p->login = NULL;
    return ret;
}

#endif

static int local_argc = 0;
static char **local_argv;

#ifdef CHARSET_EBCDIC
static int ebcdic_new(BIO *bi);
static int ebcdic_free(BIO *a);
static int ebcdic_read(BIO *b, char *out, int outl);
static int ebcdic_write(BIO *b, const char *in, int inl);
static long ebcdic_ctrl(BIO *b, int cmd, long num, void *ptr);
static int ebcdic_gets(BIO *bp, char *buf, int size);
static int ebcdic_puts(BIO *bp, const char *str);

# define BIO_TYPE_EBCDIC_FILTER  (18|0x0200)
static BIO_METHOD *methods_ebcdic = NULL;

/* This struct is "unwarranted chumminess with the compiler." */
typedef struct {
    size_t alloced;
    char buff[1];
} EBCDIC_OUTBUFF;

static const BIO_METHOD *BIO_f_ebcdic_filter()
{
    if (methods_ebcdic == NULL) {
        methods_ebcdic = BIO_meth_new(BIO_TYPE_EBCDIC_FILTER,
                                      "EBCDIC/ASCII filter");
        if (methods_ebcdic == NULL
            || !BIO_meth_set_write(methods_ebcdic, ebcdic_write)
            || !BIO_meth_set_read(methods_ebcdic, ebcdic_read)
            || !BIO_meth_set_puts(methods_ebcdic, ebcdic_puts)
            || !BIO_meth_set_gets(methods_ebcdic, ebcdic_gets)
            || !BIO_meth_set_ctrl(methods_ebcdic, ebcdic_ctrl)
            || !BIO_meth_set_create(methods_ebcdic, ebcdic_new)
            || !BIO_meth_set_destroy(methods_ebcdic, ebcdic_free))
            return NULL;
    }
    return methods_ebcdic;
}

static int ebcdic_new(BIO *bi)
{
    EBCDIC_OUTBUFF *wbuf;

    wbuf = app_malloc(sizeof(*wbuf) + 1024, "ebcdic wbuf");
    wbuf->alloced = 1024;
    wbuf->buff[0] = '\0';

    BIO_set_data(bi, wbuf);
    BIO_set_init(bi, 1);
    return 1;
}

static int ebcdic_free(BIO *a)
{
    EBCDIC_OUTBUFF *wbuf;

    if (a == NULL)
        return 0;
    wbuf = BIO_get_data(a);
    OPENSSL_free(wbuf);
    BIO_set_data(a, NULL);
    BIO_set_init(a, 0);

    return 1;
}

static int ebcdic_read(BIO *b, char *out, int outl)
{
    int ret = 0;
    BIO *next = BIO_next(b);

    if (out == NULL || outl == 0)
        return 0;
    if (next == NULL)
        return 0;

    ret = BIO_read(next, out, outl);
    if (ret > 0)
        ascii2ebcdic(out, out, ret);
    return ret;
}

static int ebcdic_write(BIO *b, const char *in, int inl)
{
    EBCDIC_OUTBUFF *wbuf;
    BIO *next = BIO_next(b);
    int ret = 0;
    int num;

    if ((in == NULL) || (inl <= 0))
        return 0;
    if (next == NULL)
        return 0;

    wbuf = (EBCDIC_OUTBUFF *) BIO_get_data(b);

    if (inl > (num = wbuf->alloced)) {
        num = num + num;        /* double the size */
        if (num < inl)
            num = inl;
        OPENSSL_free(wbuf);
        wbuf = app_malloc(sizeof(*wbuf) + num, "grow ebcdic wbuf");

        wbuf->alloced = num;
        wbuf->buff[0] = '\0';

        BIO_set_data(b, wbuf);
    }

    ebcdic2ascii(wbuf->buff, in, inl);

    ret = BIO_write(next, wbuf->buff, inl);

    return ret;
}

static long ebcdic_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    long ret;
    BIO *next = BIO_next(b);

    if (next == NULL)
        return 0;
    switch (cmd) {
    case BIO_CTRL_DUP:
        ret = 0L;
        break;
    default:
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    }
    return ret;
}

static int ebcdic_gets(BIO *bp, char *buf, int size)
{
    int i, ret = 0;
    BIO *next = BIO_next(bp);

    if (next == NULL)
        return 0;
/*      return(BIO_gets(bp->next_bio,buf,size));*/
    for (i = 0; i < size - 1; ++i) {
        ret = ebcdic_read(bp, &buf[i], 1);
        if (ret <= 0)
            break;
        else if (buf[i] == '\n') {
            ++i;
            break;
        }
    }
    if (i < size)
        buf[i] = '\0';
    return (ret < 0 && i == 0) ? ret : i;
}

static int ebcdic_puts(BIO *bp, const char *str)
{
    if (BIO_next(bp) == NULL)
        return 0;
    return ebcdic_write(bp, str, strlen(str));
}
#endif

/* This is a context that we pass to callbacks */
typedef struct tlsextctx_st {
    char *servername;
    BIO *biodebug;
    int extension_error;
} tlsextctx;

static int ssl_servername_cb(SSL *s, int *ad, void *arg)
{
    tlsextctx *p = (tlsextctx *) arg;
    const char *servername = SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);

    if (servername != NULL && p->biodebug != NULL) {
        const char *cp = servername;
        unsigned char uc;

        BIO_printf(p->biodebug, "Hostname in TLS extension: \"");
        while ((uc = *cp++) != 0)
            BIO_printf(p->biodebug,
                       isascii(uc) && isprint(uc) ? "%c" : "\\x%02x", uc);
        BIO_printf(p->biodebug, "\"\n");
    }

    if (p->servername == NULL)
        return SSL_TLSEXT_ERR_NOACK;

    if (servername != NULL) {
        if (strcasecmp(servername, p->servername))
            return p->extension_error;
        if (ctx2 != NULL) {
            BIO_printf(p->biodebug, "Switching server context.\n");
            SSL_set_SSL_CTX(s, ctx2);
        }
    }
    return SSL_TLSEXT_ERR_OK;
}

/* Structure passed to cert status callback */
typedef struct tlsextstatusctx_st {
    int timeout;
    /* File to load OCSP Response from (or NULL if no file) */
    char *respin;
    /* Default responder to use */
    char *host, *path, *port;
    int use_ssl;
    int verbose;
} tlsextstatusctx;

static tlsextstatusctx tlscstatp = { -1 };

#ifndef OPENSSL_NO_OCSP

/*
 * Helper function to get an OCSP_RESPONSE from a responder. This is a
 * simplified version. It examines certificates each time and makes one OCSP
 * responder query for each request. A full version would store details such as
 * the OCSP certificate IDs and minimise the number of OCSP responses by caching
 * them until they were considered "expired".
 */
static int get_ocsp_resp_from_responder(SSL *s, tlsextstatusctx *srctx,
                                        OCSP_RESPONSE **resp)
{
    char *host = NULL, *port = NULL, *path = NULL;
    int use_ssl;
    STACK_OF(OPENSSL_STRING) *aia = NULL;
    X509 *x = NULL;
    X509_STORE_CTX *inctx = NULL;
    X509_OBJECT *obj;
    OCSP_REQUEST *req = NULL;
    OCSP_CERTID *id = NULL;
    STACK_OF(X509_EXTENSION) *exts;
    int ret = SSL_TLSEXT_ERR_NOACK;
    int i;

    /* Build up OCSP query from server certificate */
    x = SSL_get_certificate(s);
    aia = X509_get1_ocsp(x);
    if (aia != NULL) {
        if (!OCSP_parse_url(sk_OPENSSL_STRING_value(aia, 0),
                            &host, &port, &path, &use_ssl)) {
            BIO_puts(bio_err, "cert_status: can't parse AIA URL\n");
            goto err;
        }
        if (srctx->verbose)
            BIO_printf(bio_err, "cert_status: AIA URL: %s\n",
                       sk_OPENSSL_STRING_value(aia, 0));
    } else {
        if (srctx->host == NULL) {
            BIO_puts(bio_err,
                     "cert_status: no AIA and no default responder URL\n");
            goto done;
        }
        host = srctx->host;
        path = srctx->path;
        port = srctx->port;
        use_ssl = srctx->use_ssl;
    }

    inctx = X509_STORE_CTX_new();
    if (inctx == NULL)
        goto err;
    if (!X509_STORE_CTX_init(inctx,
                             SSL_CTX_get_cert_store(SSL_get_SSL_CTX(s)),
                             NULL, NULL))
        goto err;
    obj = X509_STORE_CTX_get_obj_by_subject(inctx, X509_LU_X509,
                                            X509_get_issuer_name(x));
    if (obj == NULL) {
        BIO_puts(bio_err, "cert_status: Can't retrieve issuer certificate.\n");
        goto done;
    }
    id = OCSP_cert_to_id(NULL, x, X509_OBJECT_get0_X509(obj));
    X509_OBJECT_free(obj);
    if (id == NULL)
        goto err;
    req = OCSP_REQUEST_new();
    if (req == NULL)
        goto err;
    if (!OCSP_request_add0_id(req, id))
        goto err;
    id = NULL;
    /* Add any extensions to the request */
    SSL_get_tlsext_status_exts(s, &exts);
    for (i = 0; i < sk_X509_EXTENSION_num(exts); i++) {
        X509_EXTENSION *ext = sk_X509_EXTENSION_value(exts, i);
        if (!OCSP_REQUEST_add_ext(req, ext, -1))
            goto err;
    }
    *resp = process_responder(req, host, path, port, use_ssl, NULL,
                             srctx->timeout);
    if (*resp == NULL) {
        BIO_puts(bio_err, "cert_status: error querying responder\n");
        goto done;
    }

    ret = SSL_TLSEXT_ERR_OK;
    goto done;

 err:
    ret = SSL_TLSEXT_ERR_ALERT_FATAL;
 done:
    /*
     * If we parsed aia we need to free; otherwise they were copied and we
     * don't
     */
    if (aia != NULL) {
        OPENSSL_free(host);
        OPENSSL_free(path);
        OPENSSL_free(port);
        X509_email_free(aia);
    }
    OCSP_CERTID_free(id);
    OCSP_REQUEST_free(req);
    X509_STORE_CTX_free(inctx);
    return ret;
}

/*
 * Certificate Status callback. This is called when a client includes a
 * certificate status request extension. The response is either obtained from a
 * file, or from an OCSP responder.
 */
static int cert_status_cb(SSL *s, void *arg)
{
    tlsextstatusctx *srctx = arg;
    OCSP_RESPONSE *resp = NULL;
    unsigned char *rspder = NULL;
    int rspderlen;
    int ret = SSL_TLSEXT_ERR_ALERT_FATAL;

    if (srctx->verbose)
        BIO_puts(bio_err, "cert_status: callback called\n");

    if (srctx->respin != NULL) {
        BIO *derbio = bio_open_default(srctx->respin, 'r', FORMAT_ASN1);
        if (derbio == NULL) {
            BIO_puts(bio_err, "cert_status: Cannot open OCSP response file\n");
            goto err;
        }
        resp = d2i_OCSP_RESPONSE_bio(derbio, NULL);
        BIO_free(derbio);
        if (resp == NULL) {
            BIO_puts(bio_err, "cert_status: Error reading OCSP response\n");
            goto err;
        }
    } else {
        ret = get_ocsp_resp_from_responder(s, srctx, &resp);
        if (ret != SSL_TLSEXT_ERR_OK)
            goto err;
    }

    rspderlen = i2d_OCSP_RESPONSE(resp, &rspder);
    if (rspderlen <= 0)
        goto err;

    SSL_set_tlsext_status_ocsp_resp(s, rspder, rspderlen);
    if (srctx->verbose) {
        BIO_puts(bio_err, "cert_status: ocsp response sent:\n");
        OCSP_RESPONSE_print(bio_err, resp, 2);
    }

    ret = SSL_TLSEXT_ERR_OK;

 err:
    if (ret != SSL_TLSEXT_ERR_OK)
        ERR_print_errors(bio_err);

    OCSP_RESPONSE_free(resp);

    return ret;
}
#endif

#ifndef OPENSSL_NO_NEXTPROTONEG
/* This is the context that we pass to next_proto_cb */
typedef struct tlsextnextprotoctx_st {
    unsigned char *data;
    size_t len;
} tlsextnextprotoctx;

static int next_proto_cb(SSL *s, const unsigned char **data,
                         unsigned int *len, void *arg)
{
    tlsextnextprotoctx *next_proto = arg;

    *data = next_proto->data;
    *len = next_proto->len;

    return SSL_TLSEXT_ERR_OK;
}
#endif                         /* ndef OPENSSL_NO_NEXTPROTONEG */

/* This the context that we pass to alpn_cb */
typedef struct tlsextalpnctx_st {
    unsigned char *data;
    size_t len;
} tlsextalpnctx;

static int alpn_cb(SSL *s, const unsigned char **out, unsigned char *outlen,
                   const unsigned char *in, unsigned int inlen, void *arg)
{
    tlsextalpnctx *alpn_ctx = arg;

    if (!s_quiet) {
        /* We can assume that |in| is syntactically valid. */
        unsigned int i;
        BIO_printf(bio_s_out, "ALPN protocols advertised by the client: ");
        for (i = 0; i < inlen;) {
            if (i)
                BIO_write(bio_s_out, ", ", 2);
            BIO_write(bio_s_out, &in[i + 1], in[i]);
            i += in[i] + 1;
        }
        BIO_write(bio_s_out, "\n", 1);
    }

    if (SSL_select_next_proto
        ((unsigned char **)out, outlen, alpn_ctx->data, alpn_ctx->len, in,
         inlen) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    if (!s_quiet) {
        BIO_printf(bio_s_out, "ALPN protocols selected: ");
        BIO_write(bio_s_out, *out, *outlen);
        BIO_write(bio_s_out, "\n", 1);
    }

    return SSL_TLSEXT_ERR_OK;
}

static int not_resumable_sess_cb(SSL *s, int is_forward_secure)
{
    /* disable resumption for sessions with forward secure ciphers */
    return is_forward_secure;
}

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP, OPT_ENGINE,
    OPT_4, OPT_6, OPT_ACCEPT, OPT_PORT, OPT_UNIX, OPT_UNLINK, OPT_NACCEPT,
    OPT_VERIFY, OPT_NAMEOPT, OPT_UPPER_V_VERIFY, OPT_CONTEXT, OPT_CERT, OPT_CRL,
    OPT_CRL_DOWNLOAD, OPT_SERVERINFO, OPT_CERTFORM, OPT_KEY, OPT_KEYFORM,
    OPT_PASS, OPT_CERT_CHAIN, OPT_DHPARAM, OPT_DCERTFORM, OPT_DCERT,
    OPT_DKEYFORM, OPT_DPASS, OPT_DKEY, OPT_DCERT_CHAIN, OPT_NOCERT,
    OPT_CAPATH, OPT_NOCAPATH, OPT_CHAINCAPATH, OPT_VERIFYCAPATH, OPT_NO_CACHE,
    OPT_EXT_CACHE, OPT_CRLFORM, OPT_VERIFY_RET_ERROR, OPT_VERIFY_QUIET,
    OPT_BUILD_CHAIN, OPT_CAFILE, OPT_NOCAFILE, OPT_CHAINCAFILE,
    OPT_VERIFYCAFILE, OPT_NBIO, OPT_NBIO_TEST, OPT_IGN_EOF, OPT_NO_IGN_EOF,
    OPT_DEBUG, OPT_TLSEXTDEBUG, OPT_STATUS, OPT_STATUS_VERBOSE,
    OPT_STATUS_TIMEOUT, OPT_STATUS_URL, OPT_STATUS_FILE, OPT_MSG, OPT_MSGFILE,
    OPT_TRACE, OPT_SECURITY_DEBUG, OPT_SECURITY_DEBUG_VERBOSE, OPT_STATE,
    OPT_CRLF, OPT_QUIET, OPT_BRIEF, OPT_NO_DHE,
    OPT_NO_RESUME_EPHEMERAL, OPT_PSK_IDENTITY, OPT_PSK_HINT, OPT_PSK,
    OPT_PSK_SESS, OPT_SRPVFILE, OPT_SRPUSERSEED, OPT_REV, OPT_WWW,
    OPT_UPPER_WWW, OPT_HTTP, OPT_ASYNC, OPT_SSL_CONFIG,
    OPT_MAX_SEND_FRAG, OPT_SPLIT_SEND_FRAG, OPT_MAX_PIPELINES, OPT_READ_BUF,
    OPT_SSL3, OPT_TLS1_3, OPT_TLS1_2, OPT_TLS1_1, OPT_TLS1, OPT_DTLS, OPT_DTLS1,
    OPT_DTLS1_2, OPT_SCTP, OPT_TIMEOUT, OPT_MTU, OPT_LISTEN, OPT_STATELESS,
    OPT_ID_PREFIX, OPT_SERVERNAME, OPT_SERVERNAME_FATAL,
    OPT_CERT2, OPT_KEY2, OPT_NEXTPROTONEG, OPT_ALPN,
    OPT_SRTP_PROFILES, OPT_KEYMATEXPORT, OPT_KEYMATEXPORTLEN,
    OPT_KEYLOG_FILE, OPT_MAX_EARLY, OPT_RECV_MAX_EARLY, OPT_EARLY_DATA,
    OPT_S_NUM_TICKETS, OPT_ANTI_REPLAY, OPT_NO_ANTI_REPLAY, OPT_SCTP_LABEL_BUG,
    OPT_R_ENUM,
    OPT_S_ENUM,
    OPT_V_ENUM,
    OPT_X_ENUM
} OPTION_CHOICE;

const OPTIONS s_server_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"port", OPT_PORT, 'p',
     "TCP/IP port to listen on for connections (default is " PORT ")"},
    {"accept", OPT_ACCEPT, 's',
     "TCP/IP optional host and port to listen on for connections (default is *:" PORT ")"},
#ifdef AF_UNIX
    {"unix", OPT_UNIX, 's', "Unix domain socket to accept on"},
#endif
    {"4", OPT_4, '-', "Use IPv4 only"},
    {"6", OPT_6, '-', "Use IPv6 only"},
#ifdef AF_UNIX
    {"unlink", OPT_UNLINK, '-', "For -unix, unlink existing socket first"},
#endif
    {"context", OPT_CONTEXT, 's', "Set session ID context"},
    {"verify", OPT_VERIFY, 'n', "Turn on peer certificate verification"},
    {"Verify", OPT_UPPER_V_VERIFY, 'n',
     "Turn on peer certificate verification, must have a cert"},
    {"cert", OPT_CERT, '<', "Certificate file to use; default is " TEST_CERT},
    {"nameopt", OPT_NAMEOPT, 's', "Various certificate name options"},
    {"naccept", OPT_NACCEPT, 'p', "Terminate after #num connections"},
    {"serverinfo", OPT_SERVERINFO, 's',
     "PEM serverinfo file for certificate"},
    {"certform", OPT_CERTFORM, 'F',
     "Certificate format (PEM or DER) PEM default"},
    {"key", OPT_KEY, 's',
     "Private Key if not in -cert; default is " TEST_CERT},
    {"keyform", OPT_KEYFORM, 'f',
     "Key format (PEM, DER or ENGINE) PEM default"},
    {"pass", OPT_PASS, 's', "Private key file pass phrase source"},
    {"dcert", OPT_DCERT, '<',
     "Second certificate file to use (usually for DSA)"},
    {"dhparam", OPT_DHPARAM, '<', "DH parameters file to use"},
    {"dcertform", OPT_DCERTFORM, 'F',
     "Second certificate format (PEM or DER) PEM default"},
    {"dkey", OPT_DKEY, '<',
     "Second private key file to use (usually for DSA)"},
    {"dkeyform", OPT_DKEYFORM, 'F',
     "Second key format (PEM, DER or ENGINE) PEM default"},
    {"dpass", OPT_DPASS, 's', "Second private key file pass phrase source"},
    {"nbio_test", OPT_NBIO_TEST, '-', "Test with the non-blocking test bio"},
    {"crlf", OPT_CRLF, '-', "Convert LF from terminal into CRLF"},
    {"debug", OPT_DEBUG, '-', "Print more output"},
    {"msg", OPT_MSG, '-', "Show protocol messages"},
    {"msgfile", OPT_MSGFILE, '>',
     "File to send output of -msg or -trace, instead of stdout"},
    {"state", OPT_STATE, '-', "Print the SSL states"},
    {"CAfile", OPT_CAFILE, '<', "PEM format file of CA's"},
    {"CApath", OPT_CAPATH, '/', "PEM format directory of CA's"},
    {"no-CAfile", OPT_NOCAFILE, '-',
     "Do not load the default certificates file"},
    {"no-CApath", OPT_NOCAPATH, '-',
     "Do not load certificates from the default certificates directory"},
    {"nocert", OPT_NOCERT, '-', "Don't use any certificates (Anon-DH)"},
    {"quiet", OPT_QUIET, '-', "No server output"},
    {"no_resume_ephemeral", OPT_NO_RESUME_EPHEMERAL, '-',
     "Disable caching and tickets if ephemeral (EC)DH is used"},
    {"www", OPT_WWW, '-', "Respond to a 'GET /' with a status page"},
    {"WWW", OPT_UPPER_WWW, '-', "Respond to a 'GET with the file ./path"},
    {"servername", OPT_SERVERNAME, 's',
     "Servername for HostName TLS extension"},
    {"servername_fatal", OPT_SERVERNAME_FATAL, '-',
     "mismatch send fatal alert (default warning alert)"},
    {"cert2", OPT_CERT2, '<',
     "Certificate file to use for servername; default is" TEST_CERT2},
    {"key2", OPT_KEY2, '<',
     "-Private Key file to use for servername if not in -cert2"},
    {"tlsextdebug", OPT_TLSEXTDEBUG, '-',
     "Hex dump of all TLS extensions received"},
    {"HTTP", OPT_HTTP, '-', "Like -WWW but ./path includes HTTP headers"},
    {"id_prefix", OPT_ID_PREFIX, 's',
     "Generate SSL/TLS session IDs prefixed by arg"},
    OPT_R_OPTIONS,
    {"keymatexport", OPT_KEYMATEXPORT, 's',
     "Export keying material using label"},
    {"keymatexportlen", OPT_KEYMATEXPORTLEN, 'p',
     "Export len bytes of keying material (default 20)"},
    {"CRL", OPT_CRL, '<', "CRL file to use"},
    {"crl_download", OPT_CRL_DOWNLOAD, '-',
     "Download CRL from distribution points"},
    {"cert_chain", OPT_CERT_CHAIN, '<',
     "certificate chain file in PEM format"},
    {"dcert_chain", OPT_DCERT_CHAIN, '<',
     "second certificate chain file in PEM format"},
    {"chainCApath", OPT_CHAINCAPATH, '/',
     "use dir as certificate store path to build CA certificate chain"},
    {"verifyCApath", OPT_VERIFYCAPATH, '/',
     "use dir as certificate store path to verify CA certificate"},
    {"no_cache", OPT_NO_CACHE, '-', "Disable session cache"},
    {"ext_cache", OPT_EXT_CACHE, '-',
     "Disable internal cache, setup and use external cache"},
    {"CRLform", OPT_CRLFORM, 'F', "CRL format (PEM or DER) PEM is default"},
    {"verify_return_error", OPT_VERIFY_RET_ERROR, '-',
     "Close connection on verification error"},
    {"verify_quiet", OPT_VERIFY_QUIET, '-',
     "No verify output except verify errors"},
    {"build_chain", OPT_BUILD_CHAIN, '-', "Build certificate chain"},
    {"chainCAfile", OPT_CHAINCAFILE, '<',
     "CA file for certificate chain (PEM format)"},
    {"verifyCAfile", OPT_VERIFYCAFILE, '<',
     "CA file for certificate verification (PEM format)"},
    {"ign_eof", OPT_IGN_EOF, '-', "ignore input eof (default when -quiet)"},
    {"no_ign_eof", OPT_NO_IGN_EOF, '-', "Do not ignore input eof"},
#ifndef OPENSSL_NO_OCSP
    {"status", OPT_STATUS, '-', "Request certificate status from server"},
    {"status_verbose", OPT_STATUS_VERBOSE, '-',
     "Print more output in certificate status callback"},
    {"status_timeout", OPT_STATUS_TIMEOUT, 'n',
     "Status request responder timeout"},
    {"status_url", OPT_STATUS_URL, 's', "Status request fallback URL"},
    {"status_file", OPT_STATUS_FILE, '<',
     "File containing DER encoded OCSP Response"},
#endif
#ifndef OPENSSL_NO_SSL_TRACE
    {"trace", OPT_TRACE, '-', "trace protocol messages"},
#endif
    {"security_debug", OPT_SECURITY_DEBUG, '-',
     "Print output from SSL/TLS security framework"},
    {"security_debug_verbose", OPT_SECURITY_DEBUG_VERBOSE, '-',
     "Print more output from SSL/TLS security framework"},
    {"brief", OPT_BRIEF, '-',
     "Restrict output to brief summary of connection parameters"},
    {"rev", OPT_REV, '-',
     "act as a simple test server which just sends back with the received text reversed"},
    {"async", OPT_ASYNC, '-', "Operate in asynchronous mode"},
    {"ssl_config", OPT_SSL_CONFIG, 's',
     "Configure SSL_CTX using the configuration 'val'"},
    {"max_send_frag", OPT_MAX_SEND_FRAG, 'p', "Maximum Size of send frames "},
    {"split_send_frag", OPT_SPLIT_SEND_FRAG, 'p',
     "Size used to split data for encrypt pipelines"},
    {"max_pipelines", OPT_MAX_PIPELINES, 'p',
     "Maximum number of encrypt/decrypt pipelines to be used"},
    {"read_buf", OPT_READ_BUF, 'p',
     "Default read buffer size to be used for connections"},
    OPT_S_OPTIONS,
    OPT_V_OPTIONS,
    OPT_X_OPTIONS,
    {"nbio", OPT_NBIO, '-', "Use non-blocking IO"},
    {"psk_identity", OPT_PSK_IDENTITY, 's', "PSK identity to expect"},
#ifndef OPENSSL_NO_PSK
    {"psk_hint", OPT_PSK_HINT, 's', "PSK identity hint to use"},
#endif
    {"psk", OPT_PSK, 's', "PSK in hex (without 0x)"},
    {"psk_session", OPT_PSK_SESS, '<', "File to read PSK SSL session from"},
#ifndef OPENSSL_NO_SRP
    {"srpvfile", OPT_SRPVFILE, '<', "The verifier file for SRP"},
    {"srpuserseed", OPT_SRPUSERSEED, 's',
     "A seed string for a default user salt"},
#endif
#ifndef OPENSSL_NO_SSL3
    {"ssl3", OPT_SSL3, '-', "Just talk SSLv3"},
#endif
#ifndef OPENSSL_NO_TLS1
    {"tls1", OPT_TLS1, '-', "Just talk TLSv1"},
#endif
#ifndef OPENSSL_NO_TLS1_1
    {"tls1_1", OPT_TLS1_1, '-', "Just talk TLSv1.1"},
#endif
#ifndef OPENSSL_NO_TLS1_2
    {"tls1_2", OPT_TLS1_2, '-', "just talk TLSv1.2"},
#endif
#ifndef OPENSSL_NO_TLS1_3
    {"tls1_3", OPT_TLS1_3, '-', "just talk TLSv1.3"},
#endif
#ifndef OPENSSL_NO_DTLS
    {"dtls", OPT_DTLS, '-', "Use any DTLS version"},
    {"timeout", OPT_TIMEOUT, '-', "Enable timeouts"},
    {"mtu", OPT_MTU, 'p', "Set link layer MTU"},
    {"listen", OPT_LISTEN, '-',
     "Listen for a DTLS ClientHello with a cookie and then connect"},
#endif
    {"stateless", OPT_STATELESS, '-', "Require TLSv1.3 cookies"},
#ifndef OPENSSL_NO_DTLS1
    {"dtls1", OPT_DTLS1, '-', "Just talk DTLSv1"},
#endif
#ifndef OPENSSL_NO_DTLS1_2
    {"dtls1_2", OPT_DTLS1_2, '-', "Just talk DTLSv1.2"},
#endif
#ifndef OPENSSL_NO_SCTP
    {"sctp", OPT_SCTP, '-', "Use SCTP"},
    {"sctp_label_bug", OPT_SCTP_LABEL_BUG, '-', "Enable SCTP label length bug"},
#endif
#ifndef OPENSSL_NO_DH
    {"no_dhe", OPT_NO_DHE, '-', "Disable ephemeral DH"},
#endif
#ifndef OPENSSL_NO_NEXTPROTONEG
    {"nextprotoneg", OPT_NEXTPROTONEG, 's',
     "Set the advertised protocols for the NPN extension (comma-separated list)"},
#endif
#ifndef OPENSSL_NO_SRTP
    {"use_srtp", OPT_SRTP_PROFILES, 's',
     "Offer SRTP key management with a colon-separated profile list"},
#endif
    {"alpn", OPT_ALPN, 's',
     "Set the advertised protocols for the ALPN extension (comma-separated list)"},
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
#endif
    {"keylogfile", OPT_KEYLOG_FILE, '>', "Write TLS secrets to file"},
    {"max_early_data", OPT_MAX_EARLY, 'n',
     "The maximum number of bytes of early data as advertised in tickets"},
    {"recv_max_early_data", OPT_RECV_MAX_EARLY, 'n',
     "The maximum number of bytes of early data (hard limit)"},
    {"early_data", OPT_EARLY_DATA, '-', "Attempt to read early data"},
    {"num_tickets", OPT_S_NUM_TICKETS, 'n',
     "The number of TLSv1.3 session tickets that a server will automatically  issue" },
    {"anti_replay", OPT_ANTI_REPLAY, '-', "Switch on anti-replay protection (default)"},
    {"no_anti_replay", OPT_NO_ANTI_REPLAY, '-', "Switch off anti-replay protection"},
    {NULL, OPT_EOF, 0, NULL}
};

#define IS_PROT_FLAG(o) \
 (o == OPT_SSL3 || o == OPT_TLS1 || o == OPT_TLS1_1 || o == OPT_TLS1_2 \
  || o == OPT_TLS1_3 || o == OPT_DTLS || o == OPT_DTLS1 || o == OPT_DTLS1_2)

int s_server_main(int argc, char *argv[])
{
    ENGINE *engine = NULL;
    EVP_PKEY *s_key = NULL, *s_dkey = NULL;
    SSL_CONF_CTX *cctx = NULL;
    const SSL_METHOD *meth = TLS_server_method();
    SSL_EXCERT *exc = NULL;
    STACK_OF(OPENSSL_STRING) *ssl_args = NULL;
    STACK_OF(X509) *s_chain = NULL, *s_dchain = NULL;
    STACK_OF(X509_CRL) *crls = NULL;
    X509 *s_cert = NULL, *s_dcert = NULL;
    X509_VERIFY_PARAM *vpm = NULL;
    const char *CApath = NULL, *CAfile = NULL, *chCApath = NULL, *chCAfile = NULL;
    char *dpassarg = NULL, *dpass = NULL;
    char *passarg = NULL, *pass = NULL, *vfyCApath = NULL, *vfyCAfile = NULL;
    char *crl_file = NULL, *prog;
#ifdef AF_UNIX
    int unlink_unix_path = 0;
#endif
    do_server_cb server_cb;
    int vpmtouched = 0, build_chain = 0, no_cache = 0, ext_cache = 0;
#ifndef OPENSSL_NO_DH
    char *dhfile = NULL;
    int no_dhe = 0;
#endif
    int nocert = 0, ret = 1;
    int noCApath = 0, noCAfile = 0;
    int s_cert_format = FORMAT_PEM, s_key_format = FORMAT_PEM;
    int s_dcert_format = FORMAT_PEM, s_dkey_format = FORMAT_PEM;
    int rev = 0, naccept = -1, sdebug = 0;
    int socket_family = AF_UNSPEC, socket_type = SOCK_STREAM, protocol = 0;
    int state = 0, crl_format = FORMAT_PEM, crl_download = 0;
    char *host = NULL;
    char *port = BUF_strdup(PORT);
    unsigned char *context = NULL;
    OPTION_CHOICE o;
    EVP_PKEY *s_key2 = NULL;
    X509 *s_cert2 = NULL;
    tlsextctx tlsextcbp = { NULL, NULL, SSL_TLSEXT_ERR_ALERT_WARNING };
    const char *ssl_config = NULL;
    int read_buf_len = 0;
#ifndef OPENSSL_NO_NEXTPROTONEG
    const char *next_proto_neg_in = NULL;
    tlsextnextprotoctx next_proto = { NULL, 0 };
#endif
    const char *alpn_in = NULL;
    tlsextalpnctx alpn_ctx = { NULL, 0 };
#ifndef OPENSSL_NO_PSK
    /* by default do not send a PSK identity hint */
    char *psk_identity_hint = NULL;
#endif
    char *p;
#ifndef OPENSSL_NO_SRP
    char *srpuserseed = NULL;
    char *srp_verifier_file = NULL;
#endif
#ifndef OPENSSL_NO_SRTP
    char *srtp_profiles = NULL;
#endif
    int min_version = 0, max_version = 0, prot_opt = 0, no_prot_opt = 0;
    int s_server_verify = SSL_VERIFY_NONE;
    int s_server_session_id_context = 1; /* anything will do */
    const char *s_cert_file = TEST_CERT, *s_key_file = NULL, *s_chain_file = NULL;
    const char *s_cert_file2 = TEST_CERT2, *s_key_file2 = NULL;
    char *s_dcert_file = NULL, *s_dkey_file = NULL, *s_dchain_file = NULL;
#ifndef OPENSSL_NO_OCSP
    int s_tlsextstatus = 0;
#endif
    int no_resume_ephemeral = 0;
    unsigned int max_send_fragment = 0;
    unsigned int split_send_fragment = 0, max_pipelines = 0;
    const char *s_serverinfo_file = NULL;
    const char *keylog_file = NULL;
    int max_early_data = -1, recv_max_early_data = -1;
    char *psksessf = NULL;
#ifndef OPENSSL_NO_SCTP
    int sctp_label_bug = 0;
#endif

    /* Init of few remaining global variables */
    local_argc = argc;
    local_argv = argv;

    ctx = ctx2 = NULL;
    s_nbio = s_nbio_test = 0;
    www = 0;
    bio_s_out = NULL;
    s_debug = 0;
    s_msg = 0;
    s_quiet = 0;
    s_brief = 0;
    async = 0;

    cctx = SSL_CONF_CTX_new();
    vpm = X509_VERIFY_PARAM_new();
    if (cctx == NULL || vpm == NULL)
        goto end;
    SSL_CONF_CTX_set_flags(cctx,
                           SSL_CONF_FLAG_SERVER | SSL_CONF_FLAG_CMDLINE);

    prog = opt_init(argc, argv, s_server_options);
    while ((o = opt_next()) != OPT_EOF) {
        if (IS_PROT_FLAG(o) && ++prot_opt > 1) {
            BIO_printf(bio_err, "Cannot supply multiple protocol flags\n");
            goto end;
        }
        if (IS_NO_PROT_FLAG(o))
            no_prot_opt++;
        if (prot_opt == 1 && no_prot_opt) {
            BIO_printf(bio_err,
                       "Cannot supply both a protocol flag and '-no_<prot>'\n");
            goto end;
        }
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(s_server_options);
            ret = 0;
            goto end;

        case OPT_4:
#ifdef AF_UNIX
            if (socket_family == AF_UNIX) {
                OPENSSL_free(host); host = NULL;
                OPENSSL_free(port); port = NULL;
            }
#endif
            socket_family = AF_INET;
            break;
        case OPT_6:
            if (1) {
#ifdef AF_INET6
#ifdef AF_UNIX
                if (socket_family == AF_UNIX) {
                    OPENSSL_free(host); host = NULL;
                    OPENSSL_free(port); port = NULL;
                }
#endif
                socket_family = AF_INET6;
            } else {
#endif
                BIO_printf(bio_err, "%s: IPv6 domain sockets unsupported\n", prog);
                goto end;
            }
            break;
        case OPT_PORT:
#ifdef AF_UNIX
            if (socket_family == AF_UNIX) {
                socket_family = AF_UNSPEC;
            }
#endif
            OPENSSL_free(port); port = NULL;
            OPENSSL_free(host); host = NULL;
            if (BIO_parse_hostserv(opt_arg(), NULL, &port, BIO_PARSE_PRIO_SERV) < 1) {
                BIO_printf(bio_err,
                           "%s: -port argument malformed or ambiguous\n",
                           port);
                goto end;
            }
            break;
        case OPT_ACCEPT:
#ifdef AF_UNIX
            if (socket_family == AF_UNIX) {
                socket_family = AF_UNSPEC;
            }
#endif
            OPENSSL_free(port); port = NULL;
            OPENSSL_free(host); host = NULL;
            if (BIO_parse_hostserv(opt_arg(), &host, &port, BIO_PARSE_PRIO_SERV) < 1) {
                BIO_printf(bio_err,
                           "%s: -accept argument malformed or ambiguous\n",
                           port);
                goto end;
            }
            break;
#ifdef AF_UNIX
        case OPT_UNIX:
            socket_family = AF_UNIX;
            OPENSSL_free(host); host = BUF_strdup(opt_arg());
            OPENSSL_free(port); port = NULL;
            break;
        case OPT_UNLINK:
            unlink_unix_path = 1;
            break;
#endif
        case OPT_NACCEPT:
            naccept = atol(opt_arg());
            break;
        case OPT_VERIFY:
            s_server_verify = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
            verify_args.depth = atoi(opt_arg());
            if (!s_quiet)
                BIO_printf(bio_err, "verify depth is %d\n", verify_args.depth);
            break;
        case OPT_UPPER_V_VERIFY:
            s_server_verify =
                SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT |
                SSL_VERIFY_CLIENT_ONCE;
            verify_args.depth = atoi(opt_arg());
            if (!s_quiet)
                BIO_printf(bio_err,
                           "verify depth is %d, must return a certificate\n",
                           verify_args.depth);
            break;
        case OPT_CONTEXT:
            context = (unsigned char *)opt_arg();
            break;
        case OPT_CERT:
            s_cert_file = opt_arg();
            break;
        case OPT_NAMEOPT:
            if (!set_nameopt(opt_arg()))
                goto end;
            break;
        case OPT_CRL:
            crl_file = opt_arg();
            break;
        case OPT_CRL_DOWNLOAD:
            crl_download = 1;
            break;
        case OPT_SERVERINFO:
            s_serverinfo_file = opt_arg();
            break;
        case OPT_CERTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &s_cert_format))
                goto opthelp;
            break;
        case OPT_KEY:
            s_key_file = opt_arg();
            break;
        case OPT_KEYFORM:
            if (!opt_format(opt_arg(), OPT_FMT_ANY, &s_key_format))
                goto opthelp;
            break;
        case OPT_PASS:
            passarg = opt_arg();
            break;
        case OPT_CERT_CHAIN:
            s_chain_file = opt_arg();
            break;
        case OPT_DHPARAM:
#ifndef OPENSSL_NO_DH
            dhfile = opt_arg();
#endif
            break;
        case OPT_DCERTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &s_dcert_format))
                goto opthelp;
            break;
        case OPT_DCERT:
            s_dcert_file = opt_arg();
            break;
        case OPT_DKEYFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &s_dkey_format))
                goto opthelp;
            break;
        case OPT_DPASS:
            dpassarg = opt_arg();
            break;
        case OPT_DKEY:
            s_dkey_file = opt_arg();
            break;
        case OPT_DCERT_CHAIN:
            s_dchain_file = opt_arg();
            break;
        case OPT_NOCERT:
            nocert = 1;
            break;
        case OPT_CAPATH:
            CApath = opt_arg();
            break;
        case OPT_NOCAPATH:
            noCApath = 1;
            break;
        case OPT_CHAINCAPATH:
            chCApath = opt_arg();
            break;
        case OPT_VERIFYCAPATH:
            vfyCApath = opt_arg();
            break;
        case OPT_NO_CACHE:
            no_cache = 1;
            break;
        case OPT_EXT_CACHE:
            ext_cache = 1;
            break;
        case OPT_CRLFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &crl_format))
                goto opthelp;
            break;
        case OPT_S_CASES:
        case OPT_S_NUM_TICKETS:
        case OPT_ANTI_REPLAY:
        case OPT_NO_ANTI_REPLAY:
            if (ssl_args == NULL)
                ssl_args = sk_OPENSSL_STRING_new_null();
            if (ssl_args == NULL
                || !sk_OPENSSL_STRING_push(ssl_args, opt_flag())
                || !sk_OPENSSL_STRING_push(ssl_args, opt_arg())) {
                BIO_printf(bio_err, "%s: Memory allocation failure\n", prog);
                goto end;
            }
            break;
        case OPT_V_CASES:
            if (!opt_verify(o, vpm))
                goto end;
            vpmtouched++;
            break;
        case OPT_X_CASES:
            if (!args_excert(o, &exc))
                goto end;
            break;
        case OPT_VERIFY_RET_ERROR:
            verify_args.return_error = 1;
            break;
        case OPT_VERIFY_QUIET:
            verify_args.quiet = 1;
            break;
        case OPT_BUILD_CHAIN:
            build_chain = 1;
            break;
        case OPT_CAFILE:
            CAfile = opt_arg();
            break;
        case OPT_NOCAFILE:
            noCAfile = 1;
            break;
        case OPT_CHAINCAFILE:
            chCAfile = opt_arg();
            break;
        case OPT_VERIFYCAFILE:
            vfyCAfile = opt_arg();
            break;
        case OPT_NBIO:
            s_nbio = 1;
            break;
        case OPT_NBIO_TEST:
            s_nbio = s_nbio_test = 1;
            break;
        case OPT_IGN_EOF:
            s_ign_eof = 1;
            break;
        case OPT_NO_IGN_EOF:
            s_ign_eof = 0;
            break;
        case OPT_DEBUG:
            s_debug = 1;
            break;
        case OPT_TLSEXTDEBUG:
            s_tlsextdebug = 1;
            break;
        case OPT_STATUS:
#ifndef OPENSSL_NO_OCSP
            s_tlsextstatus = 1;
#endif
            break;
        case OPT_STATUS_VERBOSE:
#ifndef OPENSSL_NO_OCSP
            s_tlsextstatus = tlscstatp.verbose = 1;
#endif
            break;
        case OPT_STATUS_TIMEOUT:
#ifndef OPENSSL_NO_OCSP
            s_tlsextstatus = 1;
            tlscstatp.timeout = atoi(opt_arg());
#endif
            break;
        case OPT_STATUS_URL:
#ifndef OPENSSL_NO_OCSP
            s_tlsextstatus = 1;
            if (!OCSP_parse_url(opt_arg(),
                                &tlscstatp.host,
                                &tlscstatp.port,
                                &tlscstatp.path, &tlscstatp.use_ssl)) {
                BIO_printf(bio_err, "Error parsing URL\n");
                goto end;
            }
#endif
            break;
        case OPT_STATUS_FILE:
#ifndef OPENSSL_NO_OCSP
            s_tlsextstatus = 1;
            tlscstatp.respin = opt_arg();
#endif
            break;
        case OPT_MSG:
            s_msg = 1;
            break;
        case OPT_MSGFILE:
            bio_s_msg = BIO_new_file(opt_arg(), "w");
            break;
        case OPT_TRACE:
#ifndef OPENSSL_NO_SSL_TRACE
            s_msg = 2;
#endif
            break;
        case OPT_SECURITY_DEBUG:
            sdebug = 1;
            break;
        case OPT_SECURITY_DEBUG_VERBOSE:
            sdebug = 2;
            break;
        case OPT_STATE:
            state = 1;
            break;
        case OPT_CRLF:
            s_crlf = 1;
            break;
        case OPT_QUIET:
            s_quiet = 1;
            break;
        case OPT_BRIEF:
            s_quiet = s_brief = verify_args.quiet = 1;
            break;
        case OPT_NO_DHE:
#ifndef OPENSSL_NO_DH
            no_dhe = 1;
#endif
            break;
        case OPT_NO_RESUME_EPHEMERAL:
            no_resume_ephemeral = 1;
            break;
        case OPT_PSK_IDENTITY:
            psk_identity = opt_arg();
            break;
        case OPT_PSK_HINT:
#ifndef OPENSSL_NO_PSK
            psk_identity_hint = opt_arg();
#endif
            break;
        case OPT_PSK:
            for (p = psk_key = opt_arg(); *p; p++) {
                if (isxdigit(_UC(*p)))
                    continue;
                BIO_printf(bio_err, "Not a hex number '%s'\n", psk_key);
                goto end;
            }
            break;
        case OPT_PSK_SESS:
            psksessf = opt_arg();
            break;
        case OPT_SRPVFILE:
#ifndef OPENSSL_NO_SRP
            srp_verifier_file = opt_arg();
            if (min_version < TLS1_VERSION)
                min_version = TLS1_VERSION;
#endif
            break;
        case OPT_SRPUSERSEED:
#ifndef OPENSSL_NO_SRP
            srpuserseed = opt_arg();
            if (min_version < TLS1_VERSION)
                min_version = TLS1_VERSION;
#endif
            break;
        case OPT_REV:
            rev = 1;
            break;
        case OPT_WWW:
            www = 1;
            break;
        case OPT_UPPER_WWW:
            www = 2;
            break;
        case OPT_HTTP:
            www = 3;
            break;
        case OPT_SSL_CONFIG:
            ssl_config = opt_arg();
            break;
        case OPT_SSL3:
            min_version = SSL3_VERSION;
            max_version = SSL3_VERSION;
            break;
        case OPT_TLS1_3:
            min_version = TLS1_3_VERSION;
            max_version = TLS1_3_VERSION;
            break;
        case OPT_TLS1_2:
            min_version = TLS1_2_VERSION;
            max_version = TLS1_2_VERSION;
            break;
        case OPT_TLS1_1:
            min_version = TLS1_1_VERSION;
            max_version = TLS1_1_VERSION;
            break;
        case OPT_TLS1:
            min_version = TLS1_VERSION;
            max_version = TLS1_VERSION;
            break;
        case OPT_DTLS:
#ifndef OPENSSL_NO_DTLS
            meth = DTLS_server_method();
            socket_type = SOCK_DGRAM;
#endif
            break;
        case OPT_DTLS1:
#ifndef OPENSSL_NO_DTLS
            meth = DTLS_server_method();
            min_version = DTLS1_VERSION;
            max_version = DTLS1_VERSION;
            socket_type = SOCK_DGRAM;
#endif
            break;
        case OPT_DTLS1_2:
#ifndef OPENSSL_NO_DTLS
            meth = DTLS_server_method();
            min_version = DTLS1_2_VERSION;
            max_version = DTLS1_2_VERSION;
            socket_type = SOCK_DGRAM;
#endif
            break;
        case OPT_SCTP:
#ifndef OPENSSL_NO_SCTP
            protocol = IPPROTO_SCTP;
#endif
            break;
        case OPT_SCTP_LABEL_BUG:
#ifndef OPENSSL_NO_SCTP
            sctp_label_bug = 1;
#endif
            break;
        case OPT_TIMEOUT:
#ifndef OPENSSL_NO_DTLS
            enable_timeouts = 1;
#endif
            break;
        case OPT_MTU:
#ifndef OPENSSL_NO_DTLS
            socket_mtu = atol(opt_arg());
#endif
            break;
        case OPT_LISTEN:
#ifndef OPENSSL_NO_DTLS
            dtlslisten = 1;
#endif
            break;
        case OPT_STATELESS:
            stateless = 1;
            break;
        case OPT_ID_PREFIX:
            session_id_prefix = opt_arg();
            break;
        case OPT_ENGINE:
            engine = setup_engine(opt_arg(), 1);
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_SERVERNAME:
            tlsextcbp.servername = opt_arg();
            break;
        case OPT_SERVERNAME_FATAL:
            tlsextcbp.extension_error = SSL_TLSEXT_ERR_ALERT_FATAL;
            break;
        case OPT_CERT2:
            s_cert_file2 = opt_arg();
            break;
        case OPT_KEY2:
            s_key_file2 = opt_arg();
            break;
        case OPT_NEXTPROTONEG:
# ifndef OPENSSL_NO_NEXTPROTONEG
            next_proto_neg_in = opt_arg();
#endif
            break;
        case OPT_ALPN:
            alpn_in = opt_arg();
            break;
        case OPT_SRTP_PROFILES:
#ifndef OPENSSL_NO_SRTP
            srtp_profiles = opt_arg();
#endif
            break;
        case OPT_KEYMATEXPORT:
            keymatexportlabel = opt_arg();
            break;
        case OPT_KEYMATEXPORTLEN:
            keymatexportlen = atoi(opt_arg());
            break;
        case OPT_ASYNC:
            async = 1;
            break;
        case OPT_MAX_SEND_FRAG:
            max_send_fragment = atoi(opt_arg());
            break;
        case OPT_SPLIT_SEND_FRAG:
            split_send_fragment = atoi(opt_arg());
            break;
        case OPT_MAX_PIPELINES:
            max_pipelines = atoi(opt_arg());
            break;
        case OPT_READ_BUF:
            read_buf_len = atoi(opt_arg());
            break;
        case OPT_KEYLOG_FILE:
            keylog_file = opt_arg();
            break;
        case OPT_MAX_EARLY:
            max_early_data = atoi(opt_arg());
            if (max_early_data < 0) {
                BIO_printf(bio_err, "Invalid value for max_early_data\n");
                goto end;
            }
            break;
        case OPT_RECV_MAX_EARLY:
            recv_max_early_data = atoi(opt_arg());
            if (recv_max_early_data < 0) {
                BIO_printf(bio_err, "Invalid value for recv_max_early_data\n");
                goto end;
            }
            break;
        case OPT_EARLY_DATA:
            early_data = 1;
            if (max_early_data == -1)
                max_early_data = SSL3_RT_MAX_PLAIN_LENGTH;
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

#ifndef OPENSSL_NO_NEXTPROTONEG
    if (min_version == TLS1_3_VERSION && next_proto_neg_in != NULL) {
        BIO_printf(bio_err, "Cannot supply -nextprotoneg with TLSv1.3\n");
        goto opthelp;
    }
#endif
#ifndef OPENSSL_NO_DTLS
    if (www && socket_type == SOCK_DGRAM) {
        BIO_printf(bio_err, "Can't use -HTTP, -www or -WWW with DTLS\n");
        goto end;
    }

    if (dtlslisten && socket_type != SOCK_DGRAM) {
        BIO_printf(bio_err, "Can only use -listen with DTLS\n");
        goto end;
    }
#endif

    if (stateless && socket_type != SOCK_STREAM) {
        BIO_printf(bio_err, "Can only use --stateless with TLS\n");
        goto end;
    }

#ifdef AF_UNIX
    if (socket_family == AF_UNIX && socket_type != SOCK_STREAM) {
        BIO_printf(bio_err,
                   "Can't use unix sockets and datagrams together\n");
        goto end;
    }
#endif
    if (early_data && (www > 0 || rev)) {
        BIO_printf(bio_err,
                   "Can't use -early_data in combination with -www, -WWW, -HTTP, or -rev\n");
        goto end;
    }

#ifndef OPENSSL_NO_SCTP
    if (protocol == IPPROTO_SCTP) {
        if (socket_type != SOCK_DGRAM) {
            BIO_printf(bio_err, "Can't use -sctp without DTLS\n");
            goto end;
        }
        /* SCTP is unusual. It uses DTLS over a SOCK_STREAM protocol */
        socket_type = SOCK_STREAM;
    }
#endif

    if (!app_passwd(passarg, dpassarg, &pass, &dpass)) {
        BIO_printf(bio_err, "Error getting password\n");
        goto end;
    }

    if (s_key_file == NULL)
        s_key_file = s_cert_file;

    if (s_key_file2 == NULL)
        s_key_file2 = s_cert_file2;

    if (!load_excert(&exc))
        goto end;

    if (nocert == 0) {
        s_key = load_key(s_key_file, s_key_format, 0, pass, engine,
                         "server certificate private key file");
        if (s_key == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }

        s_cert = load_cert(s_cert_file, s_cert_format,
                           "server certificate file");

        if (s_cert == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }
        if (s_chain_file != NULL) {
            if (!load_certs(s_chain_file, &s_chain, FORMAT_PEM, NULL,
                            "server certificate chain"))
                goto end;
        }

        if (tlsextcbp.servername != NULL) {
            s_key2 = load_key(s_key_file2, s_key_format, 0, pass, engine,
                              "second server certificate private key file");
            if (s_key2 == NULL) {
                ERR_print_errors(bio_err);
                goto end;
            }

            s_cert2 = load_cert(s_cert_file2, s_cert_format,
                                "second server certificate file");

            if (s_cert2 == NULL) {
                ERR_print_errors(bio_err);
                goto end;
            }
        }
    }
#if !defined(OPENSSL_NO_NEXTPROTONEG)
    if (next_proto_neg_in) {
        next_proto.data = next_protos_parse(&next_proto.len, next_proto_neg_in);
        if (next_proto.data == NULL)
            goto end;
    }
#endif
    alpn_ctx.data = NULL;
    if (alpn_in) {
        alpn_ctx.data = next_protos_parse(&alpn_ctx.len, alpn_in);
        if (alpn_ctx.data == NULL)
            goto end;
    }

    if (crl_file != NULL) {
        X509_CRL *crl;
        crl = load_crl(crl_file, crl_format);
        if (crl == NULL) {
            BIO_puts(bio_err, "Error loading CRL\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        crls = sk_X509_CRL_new_null();
        if (crls == NULL || !sk_X509_CRL_push(crls, crl)) {
            BIO_puts(bio_err, "Error adding CRL\n");
            ERR_print_errors(bio_err);
            X509_CRL_free(crl);
            goto end;
        }
    }

    if (s_dcert_file != NULL) {

        if (s_dkey_file == NULL)
            s_dkey_file = s_dcert_file;

        s_dkey = load_key(s_dkey_file, s_dkey_format,
                          0, dpass, engine, "second certificate private key file");
        if (s_dkey == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }

        s_dcert = load_cert(s_dcert_file, s_dcert_format,
                            "second server certificate file");

        if (s_dcert == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }
        if (s_dchain_file != NULL) {
            if (!load_certs(s_dchain_file, &s_dchain, FORMAT_PEM, NULL,
                            "second server certificate chain"))
                goto end;
        }

    }

    if (bio_s_out == NULL) {
        if (s_quiet && !s_debug) {
            bio_s_out = BIO_new(BIO_s_null());
            if (s_msg && bio_s_msg == NULL)
                bio_s_msg = dup_bio_out(FORMAT_TEXT);
        } else {
            if (bio_s_out == NULL)
                bio_s_out = dup_bio_out(FORMAT_TEXT);
        }
    }
#if !defined(OPENSSL_NO_RSA) || !defined(OPENSSL_NO_DSA) || !defined(OPENSSL_NO_EC)
    if (nocert)
#endif
    {
        s_cert_file = NULL;
        s_key_file = NULL;
        s_dcert_file = NULL;
        s_dkey_file = NULL;
        s_cert_file2 = NULL;
        s_key_file2 = NULL;
    }

    ctx = SSL_CTX_new(meth);
    if (ctx == NULL) {
        ERR_print_errors(bio_err);
        goto end;
    }

    SSL_CTX_clear_mode(ctx, SSL_MODE_AUTO_RETRY);

    if (sdebug)
        ssl_ctx_security_debug(ctx, sdebug);

    if (!config_ctx(cctx, ssl_args, ctx))
        goto end;

    if (ssl_config) {
        if (SSL_CTX_config(ctx, ssl_config) == 0) {
            BIO_printf(bio_err, "Error using configuration \"%s\"\n",
                       ssl_config);
            ERR_print_errors(bio_err);
            goto end;
        }
    }

#ifndef OPENSSL_NO_SCTP
    if (protocol == IPPROTO_SCTP && sctp_label_bug == 1)
        SSL_CTX_set_mode(ctx, SSL_MODE_DTLS_SCTP_LABEL_LENGTH_BUG);
#endif

    if (min_version != 0
        && SSL_CTX_set_min_proto_version(ctx, min_version) == 0)
        goto end;
    if (max_version != 0
        && SSL_CTX_set_max_proto_version(ctx, max_version) == 0)
        goto end;

    if (session_id_prefix) {
        if (strlen(session_id_prefix) >= 32)
            BIO_printf(bio_err,
                       "warning: id_prefix is too long, only one new session will be possible\n");
        if (!SSL_CTX_set_generate_session_id(ctx, generate_session_id)) {
            BIO_printf(bio_err, "error setting 'id_prefix'\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        BIO_printf(bio_err, "id_prefix '%s' set.\n", session_id_prefix);
    }
    SSL_CTX_set_quiet_shutdown(ctx, 1);
    if (exc != NULL)
        ssl_ctx_set_excert(ctx, exc);

    if (state)
        SSL_CTX_set_info_callback(ctx, apps_ssl_info_callback);
    if (no_cache)
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    else if (ext_cache)
        init_session_cache_ctx(ctx);
    else
        SSL_CTX_sess_set_cache_size(ctx, 128);

    if (async) {
        SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);
    }

    if (max_send_fragment > 0
        && !SSL_CTX_set_max_send_fragment(ctx, max_send_fragment)) {
        BIO_printf(bio_err, "%s: Max send fragment size %u is out of permitted range\n",
                   prog, max_send_fragment);
        goto end;
    }

    if (split_send_fragment > 0
        && !SSL_CTX_set_split_send_fragment(ctx, split_send_fragment)) {
        BIO_printf(bio_err, "%s: Split send fragment size %u is out of permitted range\n",
                   prog, split_send_fragment);
        goto end;
    }
    if (max_pipelines > 0
        && !SSL_CTX_set_max_pipelines(ctx, max_pipelines)) {
        BIO_printf(bio_err, "%s: Max pipelines %u is out of permitted range\n",
                   prog, max_pipelines);
        goto end;
    }

    if (read_buf_len > 0) {
        SSL_CTX_set_default_read_buffer_len(ctx, read_buf_len);
    }
#ifndef OPENSSL_NO_SRTP
    if (srtp_profiles != NULL) {
        /* Returns 0 on success! */
        if (SSL_CTX_set_tlsext_use_srtp(ctx, srtp_profiles) != 0) {
            BIO_printf(bio_err, "Error setting SRTP profile\n");
            ERR_print_errors(bio_err);
            goto end;
        }
    }
#endif

    if (!ctx_set_verify_locations(ctx, CAfile, CApath, noCAfile, noCApath)) {
        ERR_print_errors(bio_err);
        goto end;
    }
    if (vpmtouched && !SSL_CTX_set1_param(ctx, vpm)) {
        BIO_printf(bio_err, "Error setting verify params\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    ssl_ctx_add_crls(ctx, crls, 0);

    if (!ssl_load_stores(ctx, vfyCApath, vfyCAfile, chCApath, chCAfile,
                         crls, crl_download)) {
        BIO_printf(bio_err, "Error loading store locations\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    if (s_cert2) {
        ctx2 = SSL_CTX_new(meth);
        if (ctx2 == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }
    }

    if (ctx2 != NULL) {
        BIO_printf(bio_s_out, "Setting secondary ctx parameters\n");

        if (sdebug)
            ssl_ctx_security_debug(ctx, sdebug);

        if (session_id_prefix) {
            if (strlen(session_id_prefix) >= 32)
                BIO_printf(bio_err,
                           "warning: id_prefix is too long, only one new session will be possible\n");
            if (!SSL_CTX_set_generate_session_id(ctx2, generate_session_id)) {
                BIO_printf(bio_err, "error setting 'id_prefix'\n");
                ERR_print_errors(bio_err);
                goto end;
            }
            BIO_printf(bio_err, "id_prefix '%s' set.\n", session_id_prefix);
        }
        SSL_CTX_set_quiet_shutdown(ctx2, 1);
        if (exc != NULL)
            ssl_ctx_set_excert(ctx2, exc);

        if (state)
            SSL_CTX_set_info_callback(ctx2, apps_ssl_info_callback);

        if (no_cache)
            SSL_CTX_set_session_cache_mode(ctx2, SSL_SESS_CACHE_OFF);
        else if (ext_cache)
            init_session_cache_ctx(ctx2);
        else
            SSL_CTX_sess_set_cache_size(ctx2, 128);

        if (async)
            SSL_CTX_set_mode(ctx2, SSL_MODE_ASYNC);

        if (!ctx_set_verify_locations(ctx2, CAfile, CApath, noCAfile,
                                      noCApath)) {
            ERR_print_errors(bio_err);
            goto end;
        }
        if (vpmtouched && !SSL_CTX_set1_param(ctx2, vpm)) {
            BIO_printf(bio_err, "Error setting verify params\n");
            ERR_print_errors(bio_err);
            goto end;
        }

        ssl_ctx_add_crls(ctx2, crls, 0);
        if (!config_ctx(cctx, ssl_args, ctx2))
            goto end;
    }
#ifndef OPENSSL_NO_NEXTPROTONEG
    if (next_proto.data)
        SSL_CTX_set_next_protos_advertised_cb(ctx, next_proto_cb,
                                              &next_proto);
#endif
    if (alpn_ctx.data)
        SSL_CTX_set_alpn_select_cb(ctx, alpn_cb, &alpn_ctx);

#ifndef OPENSSL_NO_DH
    if (!no_dhe) {
        DH *dh = NULL;

        if (dhfile != NULL)
            dh = load_dh_param(dhfile);
        else if (s_cert_file != NULL)
            dh = load_dh_param(s_cert_file);

        if (dh != NULL) {
            BIO_printf(bio_s_out, "Setting temp DH parameters\n");
        } else {
            BIO_printf(bio_s_out, "Using default temp DH parameters\n");
        }
        (void)BIO_flush(bio_s_out);

        if (dh == NULL) {
            SSL_CTX_set_dh_auto(ctx, 1);
        } else if (!SSL_CTX_set_tmp_dh(ctx, dh)) {
            BIO_puts(bio_err, "Error setting temp DH parameters\n");
            ERR_print_errors(bio_err);
            DH_free(dh);
            goto end;
        }

        if (ctx2 != NULL) {
            if (!dhfile) {
                DH *dh2 = load_dh_param(s_cert_file2);
                if (dh2 != NULL) {
                    BIO_printf(bio_s_out, "Setting temp DH parameters\n");
                    (void)BIO_flush(bio_s_out);

                    DH_free(dh);
                    dh = dh2;
                }
            }
            if (dh == NULL) {
                SSL_CTX_set_dh_auto(ctx2, 1);
            } else if (!SSL_CTX_set_tmp_dh(ctx2, dh)) {
                BIO_puts(bio_err, "Error setting temp DH parameters\n");
                ERR_print_errors(bio_err);
                DH_free(dh);
                goto end;
            }
        }
        DH_free(dh);
    }
#endif

    if (!set_cert_key_stuff(ctx, s_cert, s_key, s_chain, build_chain))
        goto end;

    if (s_serverinfo_file != NULL
        && !SSL_CTX_use_serverinfo_file(ctx, s_serverinfo_file)) {
        ERR_print_errors(bio_err);
        goto end;
    }

    if (ctx2 != NULL
        && !set_cert_key_stuff(ctx2, s_cert2, s_key2, NULL, build_chain))
        goto end;

    if (s_dcert != NULL) {
        if (!set_cert_key_stuff(ctx, s_dcert, s_dkey, s_dchain, build_chain))
            goto end;
    }

    if (no_resume_ephemeral) {
        SSL_CTX_set_not_resumable_session_callback(ctx,
                                                   not_resumable_sess_cb);

        if (ctx2 != NULL)
            SSL_CTX_set_not_resumable_session_callback(ctx2,
                                                       not_resumable_sess_cb);
    }
#ifndef OPENSSL_NO_PSK
    if (psk_key != NULL) {
        if (s_debug)
            BIO_printf(bio_s_out, "PSK key given, setting server callback\n");
        SSL_CTX_set_psk_server_callback(ctx, psk_server_cb);
    }

    if (!SSL_CTX_use_psk_identity_hint(ctx, psk_identity_hint)) {
        BIO_printf(bio_err, "error setting PSK identity hint to context\n");
        ERR_print_errors(bio_err);
        goto end;
    }
#endif
    if (psksessf != NULL) {
        BIO *stmp = BIO_new_file(psksessf, "r");

        if (stmp == NULL) {
            BIO_printf(bio_err, "Can't open PSK session file %s\n", psksessf);
            ERR_print_errors(bio_err);
            goto end;
        }
        psksess = PEM_read_bio_SSL_SESSION(stmp, NULL, 0, NULL);
        BIO_free(stmp);
        if (psksess == NULL) {
            BIO_printf(bio_err, "Can't read PSK session file %s\n", psksessf);
            ERR_print_errors(bio_err);
            goto end;
        }

    }

    if (psk_key != NULL || psksess != NULL)
        SSL_CTX_set_psk_find_session_callback(ctx, psk_find_session_cb);

    SSL_CTX_set_verify(ctx, s_server_verify, verify_callback);
    if (!SSL_CTX_set_session_id_context(ctx,
                                        (void *)&s_server_session_id_context,
                                        sizeof(s_server_session_id_context))) {
        BIO_printf(bio_err, "error setting session id context\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    /* Set DTLS cookie generation and verification callbacks */
    SSL_CTX_set_cookie_generate_cb(ctx, generate_cookie_callback);
    SSL_CTX_set_cookie_verify_cb(ctx, verify_cookie_callback);

    /* Set TLS1.3 cookie generation and verification callbacks */
    SSL_CTX_set_stateless_cookie_generate_cb(ctx, generate_stateless_cookie_callback);
    SSL_CTX_set_stateless_cookie_verify_cb(ctx, verify_stateless_cookie_callback);

    if (ctx2 != NULL) {
        SSL_CTX_set_verify(ctx2, s_server_verify, verify_callback);
        if (!SSL_CTX_set_session_id_context(ctx2,
                    (void *)&s_server_session_id_context,
                    sizeof(s_server_session_id_context))) {
            BIO_printf(bio_err, "error setting session id context\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        tlsextcbp.biodebug = bio_s_out;
        SSL_CTX_set_tlsext_servername_callback(ctx2, ssl_servername_cb);
        SSL_CTX_set_tlsext_servername_arg(ctx2, &tlsextcbp);
        SSL_CTX_set_tlsext_servername_callback(ctx, ssl_servername_cb);
        SSL_CTX_set_tlsext_servername_arg(ctx, &tlsextcbp);
    }

#ifndef OPENSSL_NO_SRP
    if (srp_verifier_file != NULL) {
        srp_callback_parm.vb = SRP_VBASE_new(srpuserseed);
        srp_callback_parm.user = NULL;
        srp_callback_parm.login = NULL;
        if ((ret =
             SRP_VBASE_init(srp_callback_parm.vb,
                            srp_verifier_file)) != SRP_NO_ERROR) {
            BIO_printf(bio_err,
                       "Cannot initialize SRP verifier file \"%s\":ret=%d\n",
                       srp_verifier_file, ret);
            goto end;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, verify_callback);
        SSL_CTX_set_srp_cb_arg(ctx, &srp_callback_parm);
        SSL_CTX_set_srp_username_callback(ctx, ssl_srp_server_param_cb);
    } else
#endif
    if (CAfile != NULL) {
        SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(CAfile));

        if (ctx2)
            SSL_CTX_set_client_CA_list(ctx2, SSL_load_client_CA_file(CAfile));
    }
#ifndef OPENSSL_NO_OCSP
    if (s_tlsextstatus) {
        SSL_CTX_set_tlsext_status_cb(ctx, cert_status_cb);
        SSL_CTX_set_tlsext_status_arg(ctx, &tlscstatp);
        if (ctx2) {
            SSL_CTX_set_tlsext_status_cb(ctx2, cert_status_cb);
            SSL_CTX_set_tlsext_status_arg(ctx2, &tlscstatp);
        }
    }
#endif
    if (set_keylog_file(ctx, keylog_file))
        goto end;

    if (max_early_data >= 0)
        SSL_CTX_set_max_early_data(ctx, max_early_data);
    if (recv_max_early_data >= 0)
        SSL_CTX_set_recv_max_early_data(ctx, recv_max_early_data);

    if (rev)
        server_cb = rev_body;
    else if (www)
        server_cb = www_body;
    else
        server_cb = sv_body;
#ifdef AF_UNIX
    if (socket_family == AF_UNIX
        && unlink_unix_path)
        unlink(host);
#endif
    do_server(&accept_socket, host, port, socket_family, socket_type, protocol,
              server_cb, context, naccept, bio_s_out);
    print_stats(bio_s_out, ctx);
    ret = 0;
 end:
    SSL_CTX_free(ctx);
    SSL_SESSION_free(psksess);
    set_keylog_file(NULL, NULL);
    X509_free(s_cert);
    sk_X509_CRL_pop_free(crls, X509_CRL_free);
    X509_free(s_dcert);
    EVP_PKEY_free(s_key);
    EVP_PKEY_free(s_dkey);
    sk_X509_pop_free(s_chain, X509_free);
    sk_X509_pop_free(s_dchain, X509_free);
    OPENSSL_free(pass);
    OPENSSL_free(dpass);
    OPENSSL_free(host);
    OPENSSL_free(port);
    X509_VERIFY_PARAM_free(vpm);
    free_sessions();
    OPENSSL_free(tlscstatp.host);
    OPENSSL_free(tlscstatp.port);
    OPENSSL_free(tlscstatp.path);
    SSL_CTX_free(ctx2);
    X509_free(s_cert2);
    EVP_PKEY_free(s_key2);
#ifndef OPENSSL_NO_NEXTPROTONEG
    OPENSSL_free(next_proto.data);
#endif
    OPENSSL_free(alpn_ctx.data);
    ssl_excert_free(exc);
    sk_OPENSSL_STRING_free(ssl_args);
    SSL_CONF_CTX_free(cctx);
    release_engine(engine);
    BIO_free(bio_s_out);
    bio_s_out = NULL;
    BIO_free(bio_s_msg);
    bio_s_msg = NULL;
#ifdef CHARSET_EBCDIC
    BIO_meth_free(methods_ebcdic);
#endif
    return ret;
}

static void print_stats(BIO *bio, SSL_CTX *ssl_ctx)
{
    BIO_printf(bio, "%4ld items in the session cache\n",
               SSL_CTX_sess_number(ssl_ctx));
    BIO_printf(bio, "%4ld client connects (SSL_connect())\n",
               SSL_CTX_sess_connect(ssl_ctx));
    BIO_printf(bio, "%4ld client renegotiates (SSL_connect())\n",
               SSL_CTX_sess_connect_renegotiate(ssl_ctx));
    BIO_printf(bio, "%4ld client connects that finished\n",
               SSL_CTX_sess_connect_good(ssl_ctx));
    BIO_printf(bio, "%4ld server accepts (SSL_accept())\n",
               SSL_CTX_sess_accept(ssl_ctx));
    BIO_printf(bio, "%4ld server renegotiates (SSL_accept())\n",
               SSL_CTX_sess_accept_renegotiate(ssl_ctx));
    BIO_printf(bio, "%4ld server accepts that finished\n",
               SSL_CTX_sess_accept_good(ssl_ctx));
    BIO_printf(bio, "%4ld session cache hits\n", SSL_CTX_sess_hits(ssl_ctx));
    BIO_printf(bio, "%4ld session cache misses\n",
               SSL_CTX_sess_misses(ssl_ctx));
    BIO_printf(bio, "%4ld session cache timeouts\n",
               SSL_CTX_sess_timeouts(ssl_ctx));
    BIO_printf(bio, "%4ld callback cache hits\n",
               SSL_CTX_sess_cb_hits(ssl_ctx));
    BIO_printf(bio, "%4ld cache full overflows (%ld allowed)\n",
               SSL_CTX_sess_cache_full(ssl_ctx),
               SSL_CTX_sess_get_cache_size(ssl_ctx));
}

static int sv_body(int s, int stype, int prot, unsigned char *context)
{
    char *buf = NULL;
    fd_set readfds;
    int ret = 1, width;
    int k, i;
    unsigned long l;
    SSL *con = NULL;
    BIO *sbio;
    struct timeval timeout;
#if !(defined(OPENSSL_SYS_WINDOWS) || defined(OPENSSL_SYS_MSDOS))
    struct timeval *timeoutp;
#endif
#ifndef OPENSSL_NO_DTLS
# ifndef OPENSSL_NO_SCTP
    int isdtls = (stype == SOCK_DGRAM || prot == IPPROTO_SCTP);
# else
    int isdtls = (stype == SOCK_DGRAM);
# endif
#endif

    buf = app_malloc(bufsize, "server buffer");
    if (s_nbio) {
        if (!BIO_socket_nbio(s, 1))
            ERR_print_errors(bio_err);
        else if (!s_quiet)
            BIO_printf(bio_err, "Turned on non blocking io\n");
    }

    con = SSL_new(ctx);
    if (con == NULL) {
        ret = -1;
        goto err;
    }

    if (s_tlsextdebug) {
        SSL_set_tlsext_debug_callback(con, tlsext_cb);
        SSL_set_tlsext_debug_arg(con, bio_s_out);
    }

    if (context != NULL
        && !SSL_set_session_id_context(con, context,
                                       strlen((char *)context))) {
        BIO_printf(bio_err, "Error setting session id context\n");
        ret = -1;
        goto err;
    }

    if (!SSL_clear(con)) {
        BIO_printf(bio_err, "Error clearing SSL connection\n");
        ret = -1;
        goto err;
    }
#ifndef OPENSSL_NO_DTLS
    if (isdtls) {
# ifndef OPENSSL_NO_SCTP
        if (prot == IPPROTO_SCTP)
            sbio = BIO_new_dgram_sctp(s, BIO_NOCLOSE);
        else
# endif
            sbio = BIO_new_dgram(s, BIO_NOCLOSE);

        if (enable_timeouts) {
            timeout.tv_sec = 0;
            timeout.tv_usec = DGRAM_RCV_TIMEOUT;
            BIO_ctrl(sbio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);

            timeout.tv_sec = 0;
            timeout.tv_usec = DGRAM_SND_TIMEOUT;
            BIO_ctrl(sbio, BIO_CTRL_DGRAM_SET_SEND_TIMEOUT, 0, &timeout);
        }

        if (socket_mtu) {
            if (socket_mtu < DTLS_get_link_min_mtu(con)) {
                BIO_printf(bio_err, "MTU too small. Must be at least %ld\n",
                           DTLS_get_link_min_mtu(con));
                ret = -1;
                BIO_free(sbio);
                goto err;
            }
            SSL_set_options(con, SSL_OP_NO_QUERY_MTU);
            if (!DTLS_set_link_mtu(con, socket_mtu)) {
                BIO_printf(bio_err, "Failed to set MTU\n");
                ret = -1;
                BIO_free(sbio);
                goto err;
            }
        } else
            /* want to do MTU discovery */
            BIO_ctrl(sbio, BIO_CTRL_DGRAM_MTU_DISCOVER, 0, NULL);

# ifndef OPENSSL_NO_SCTP
        if (prot != IPPROTO_SCTP)
# endif
            /* Turn on cookie exchange. Not necessary for SCTP */
            SSL_set_options(con, SSL_OP_COOKIE_EXCHANGE);
    } else
#endif
        sbio = BIO_new_socket(s, BIO_NOCLOSE);

    if (sbio == NULL) {
        BIO_printf(bio_err, "Unable to create BIO\n");
        ERR_print_errors(bio_err);
        goto err;
    }

    if (s_nbio_test) {
        BIO *test;

        test = BIO_new(BIO_f_nbio_test());
        sbio = BIO_push(test, sbio);
    }

    SSL_set_bio(con, sbio, sbio);
    SSL_set_accept_state(con);
    /* SSL_set_fd(con,s); */

    if (s_debug) {
        BIO_set_callback(SSL_get_rbio(con), bio_dump_callback);
        BIO_set_callback_arg(SSL_get_rbio(con), (char *)bio_s_out);
    }
    if (s_msg) {
#ifndef OPENSSL_NO_SSL_TRACE
        if (s_msg == 2)
            SSL_set_msg_callback(con, SSL_trace);
        else
#endif
            SSL_set_msg_callback(con, msg_cb);
        SSL_set_msg_callback_arg(con, bio_s_msg ? bio_s_msg : bio_s_out);
    }

    if (s_tlsextdebug) {
        SSL_set_tlsext_debug_callback(con, tlsext_cb);
        SSL_set_tlsext_debug_arg(con, bio_s_out);
    }

    if (early_data) {
        int write_header = 1, edret = SSL_READ_EARLY_DATA_ERROR;
        size_t readbytes;

        while (edret != SSL_READ_EARLY_DATA_FINISH) {
            for (;;) {
                edret = SSL_read_early_data(con, buf, bufsize, &readbytes);
                if (edret != SSL_READ_EARLY_DATA_ERROR)
                    break;

                switch (SSL_get_error(con, 0)) {
                case SSL_ERROR_WANT_WRITE:
                case SSL_ERROR_WANT_ASYNC:
                case SSL_ERROR_WANT_READ:
                    /* Just keep trying - busy waiting */
                    continue;
                default:
                    BIO_printf(bio_err, "Error reading early data\n");
                    ERR_print_errors(bio_err);
                    goto err;
                }
            }
            if (readbytes > 0) {
                if (write_header) {
                    BIO_printf(bio_s_out, "Early data received:\n");
                    write_header = 0;
                }
                raw_write_stdout(buf, (unsigned int)readbytes);
                (void)BIO_flush(bio_s_out);
            }
        }
        if (write_header) {
            if (SSL_get_early_data_status(con) == SSL_EARLY_DATA_NOT_SENT)
                BIO_printf(bio_s_out, "No early data received\n");
            else
                BIO_printf(bio_s_out, "Early data was rejected\n");
        } else {
            BIO_printf(bio_s_out, "\nEnd of early data\n");
        }
        if (SSL_is_init_finished(con))
            print_connection_info(con);
    }

    if (fileno_stdin() > s)
        width = fileno_stdin() + 1;
    else
        width = s + 1;
    for (;;) {
        int read_from_terminal;
        int read_from_sslcon;

        read_from_terminal = 0;
        read_from_sslcon = SSL_has_pending(con)
                           || (async && SSL_waiting_for_async(con));

        if (!read_from_sslcon) {
            FD_ZERO(&readfds);
#if !defined(OPENSSL_SYS_WINDOWS) && !defined(OPENSSL_SYS_MSDOS)
            openssl_fdset(fileno_stdin(), &readfds);
#endif
            openssl_fdset(s, &readfds);
            /*
             * Note: under VMS with SOCKETSHR the second parameter is
             * currently of type (int *) whereas under other systems it is
             * (void *) if you don't have a cast it will choke the compiler:
             * if you do have a cast then you can either go for (int *) or
             * (void *).
             */
#if defined(OPENSSL_SYS_WINDOWS) || defined(OPENSSL_SYS_MSDOS)
            /*
             * Under DOS (non-djgpp) and Windows we can't select on stdin:
             * only on sockets. As a workaround we timeout the select every
             * second and check for any keypress. In a proper Windows
             * application we wouldn't do this because it is inefficient.
             */
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            i = select(width, (void *)&readfds, NULL, NULL, &timeout);
            if (has_stdin_waiting())
                read_from_terminal = 1;
            if ((i < 0) || (!i && !read_from_terminal))
                continue;
#else
            if (SSL_is_dtls(con) && DTLSv1_get_timeout(con, &timeout))
                timeoutp = &timeout;
            else
                timeoutp = NULL;

            i = select(width, (void *)&readfds, NULL, NULL, timeoutp);

            if ((SSL_is_dtls(con)) && DTLSv1_handle_timeout(con) > 0)
                BIO_printf(bio_err, "TIMEOUT occurred\n");

            if (i <= 0)
                continue;
            if (FD_ISSET(fileno_stdin(), &readfds))
                read_from_terminal = 1;
#endif
            if (FD_ISSET(s, &readfds))
                read_from_sslcon = 1;
        }
        if (read_from_terminal) {
            if (s_crlf) {
                int j, lf_num;

                i = raw_read_stdin(buf, bufsize / 2);
                lf_num = 0;
                /* both loops are skipped when i <= 0 */
                for (j = 0; j < i; j++)
                    if (buf[j] == '\n')
                        lf_num++;
                for (j = i - 1; j >= 0; j--) {
                    buf[j + lf_num] = buf[j];
                    if (buf[j] == '\n') {
                        lf_num--;
                        i++;
                        buf[j + lf_num] = '\r';
                    }
                }
                assert(lf_num == 0);
            } else {
                i = raw_read_stdin(buf, bufsize);
            }

            if (!s_quiet && !s_brief) {
                if ((i <= 0) || (buf[0] == 'Q')) {
                    BIO_printf(bio_s_out, "DONE\n");
                    (void)BIO_flush(bio_s_out);
                    BIO_closesocket(s);
                    close_accept_socket();
                    ret = -11;
                    goto err;
                }
                if ((i <= 0) || (buf[0] == 'q')) {
                    BIO_printf(bio_s_out, "DONE\n");
                    (void)BIO_flush(bio_s_out);
                    if (SSL_version(con) != DTLS1_VERSION)
                        BIO_closesocket(s);
                    /*
                     * close_accept_socket(); ret= -11;
                     */
                    goto err;
                }
#ifndef OPENSSL_NO_HEARTBEATS
                if ((buf[0] == 'B') && ((buf[1] == '\n') || (buf[1] == '\r'))) {
                    BIO_printf(bio_err, "HEARTBEATING\n");
                    SSL_heartbeat(con);
                    i = 0;
                    continue;
                }
#endif
                if ((buf[0] == 'r') && ((buf[1] == '\n') || (buf[1] == '\r'))) {
                    SSL_renegotiate(con);
                    i = SSL_do_handshake(con);
                    printf("SSL_do_handshake -> %d\n", i);
                    i = 0;      /* 13; */
                    continue;
                }
                if ((buf[0] == 'R') && ((buf[1] == '\n') || (buf[1] == '\r'))) {
                    SSL_set_verify(con,
                                   SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE,
                                   NULL);
                    SSL_renegotiate(con);
                    i = SSL_do_handshake(con);
                    printf("SSL_do_handshake -> %d\n", i);
                    i = 0;      /* 13; */
                    continue;
                }
                if ((buf[0] == 'K' || buf[0] == 'k')
                        && ((buf[1] == '\n') || (buf[1] == '\r'))) {
                    SSL_key_update(con, buf[0] == 'K' ?
                                        SSL_KEY_UPDATE_REQUESTED
                                        : SSL_KEY_UPDATE_NOT_REQUESTED);
                    i = SSL_do_handshake(con);
                    printf("SSL_do_handshake -> %d\n", i);
                    i = 0;
                    continue;
                }
                if (buf[0] == 'c' && ((buf[1] == '\n') || (buf[1] == '\r'))) {
                    SSL_set_verify(con, SSL_VERIFY_PEER, NULL);
                    i = SSL_verify_client_post_handshake(con);
                    if (i == 0) {
                        printf("Failed to initiate request\n");
                        ERR_print_errors(bio_err);
                    } else {
                        i = SSL_do_handshake(con);
                        printf("SSL_do_handshake -> %d\n", i);
                        i = 0;
                    }
                    continue;
                }
                if (buf[0] == 'P') {
                    static const char *str = "Lets print some clear text\n";
                    BIO_write(SSL_get_wbio(con), str, strlen(str));
                }
                if (buf[0] == 'S') {
                    print_stats(bio_s_out, SSL_get_SSL_CTX(con));
                }
            }
#ifdef CHARSET_EBCDIC
            ebcdic2ascii(buf, buf, i);
#endif
            l = k = 0;
            for (;;) {
                /* should do a select for the write */
#ifdef RENEG
                static count = 0;
                if (++count == 100) {
                    count = 0;
                    SSL_renegotiate(con);
                }
#endif
                k = SSL_write(con, &(buf[l]), (unsigned int)i);
#ifndef OPENSSL_NO_SRP
                while (SSL_get_error(con, k) == SSL_ERROR_WANT_X509_LOOKUP) {
                    BIO_printf(bio_s_out, "LOOKUP renego during write\n");
                    SRP_user_pwd_free(srp_callback_parm.user);
                    srp_callback_parm.user =
                        SRP_VBASE_get1_by_user(srp_callback_parm.vb,
                                               srp_callback_parm.login);
                    if (srp_callback_parm.user)
                        BIO_printf(bio_s_out, "LOOKUP done %s\n",
                                   srp_callback_parm.user->info);
                    else
                        BIO_printf(bio_s_out, "LOOKUP not successful\n");
                    k = SSL_write(con, &(buf[l]), (unsigned int)i);
                }
#endif
                switch (SSL_get_error(con, k)) {
                case SSL_ERROR_NONE:
                    break;
                case SSL_ERROR_WANT_ASYNC:
                    BIO_printf(bio_s_out, "Write BLOCK (Async)\n");
                    (void)BIO_flush(bio_s_out);
                    wait_for_async(con);
                    break;
                case SSL_ERROR_WANT_WRITE:
                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_X509_LOOKUP:
                    BIO_printf(bio_s_out, "Write BLOCK\n");
                    (void)BIO_flush(bio_s_out);
                    break;
                case SSL_ERROR_WANT_ASYNC_JOB:
                    /*
                     * This shouldn't ever happen in s_server. Treat as an error
                     */
                case SSL_ERROR_SYSCALL:
                case SSL_ERROR_SSL:
                    BIO_printf(bio_s_out, "ERROR\n");
                    (void)BIO_flush(bio_s_out);
                    ERR_print_errors(bio_err);
                    ret = 1;
                    goto err;
                    /* break; */
                case SSL_ERROR_ZERO_RETURN:
                    BIO_printf(bio_s_out, "DONE\n");
                    (void)BIO_flush(bio_s_out);
                    ret = 1;
                    goto err;
                }
                if (k > 0) {
                    l += k;
                    i -= k;
                }
                if (i <= 0)
                    break;
            }
        }
        if (read_from_sslcon) {
            /*
             * init_ssl_connection handles all async events itself so if we're
             * waiting for async then we shouldn't go back into
             * init_ssl_connection
             */
            if ((!async || !SSL_waiting_for_async(con))
                    && !SSL_is_init_finished(con)) {
                i = init_ssl_connection(con);

                if (i < 0) {
                    ret = 0;
                    goto err;
                } else if (i == 0) {
                    ret = 1;
                    goto err;
                }
            } else {
 again:
                i = SSL_read(con, (char *)buf, bufsize);
#ifndef OPENSSL_NO_SRP
                while (SSL_get_error(con, i) == SSL_ERROR_WANT_X509_LOOKUP) {
                    BIO_printf(bio_s_out, "LOOKUP renego during read\n");
                    SRP_user_pwd_free(srp_callback_parm.user);
                    srp_callback_parm.user =
                        SRP_VBASE_get1_by_user(srp_callback_parm.vb,
                                               srp_callback_parm.login);
                    if (srp_callback_parm.user)
                        BIO_printf(bio_s_out, "LOOKUP done %s\n",
                                   srp_callback_parm.user->info);
                    else
                        BIO_printf(bio_s_out, "LOOKUP not successful\n");
                    i = SSL_read(con, (char *)buf, bufsize);
                }
#endif
                switch (SSL_get_error(con, i)) {
                case SSL_ERROR_NONE:
#ifdef CHARSET_EBCDIC
                    ascii2ebcdic(buf, buf, i);
#endif
                    raw_write_stdout(buf, (unsigned int)i);
                    (void)BIO_flush(bio_s_out);
                    if (SSL_has_pending(con))
                        goto again;
                    break;
                case SSL_ERROR_WANT_ASYNC:
                    BIO_printf(bio_s_out, "Read BLOCK (Async)\n");
                    (void)BIO_flush(bio_s_out);
                    wait_for_async(con);
                    break;
                case SSL_ERROR_WANT_WRITE:
                case SSL_ERROR_WANT_READ:
                    BIO_printf(bio_s_out, "Read BLOCK\n");
                    (void)BIO_flush(bio_s_out);
                    break;
                case SSL_ERROR_WANT_ASYNC_JOB:
                    /*
                     * This shouldn't ever happen in s_server. Treat as an error
                     */
                case SSL_ERROR_SYSCALL:
                case SSL_ERROR_SSL:
                    BIO_printf(bio_s_out, "ERROR\n");
                    (void)BIO_flush(bio_s_out);
                    ERR_print_errors(bio_err);
                    ret = 1;
                    goto err;
                case SSL_ERROR_ZERO_RETURN:
                    BIO_printf(bio_s_out, "DONE\n");
                    (void)BIO_flush(bio_s_out);
                    ret = 1;
                    goto err;
                }
            }
        }
    }
 err:
    if (con != NULL) {
        BIO_printf(bio_s_out, "shutting down SSL\n");
        SSL_set_shutdown(con, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
        SSL_free(con);
    }
    BIO_printf(bio_s_out, "CONNECTION CLOSED\n");
    OPENSSL_clear_free(buf, bufsize);
    return ret;
}

static void close_accept_socket(void)
{
    BIO_printf(bio_err, "shutdown accept socket\n");
    if (accept_socket >= 0) {
        BIO_closesocket(accept_socket);
    }
}

static int is_retryable(SSL *con, int i)
{
    int err = SSL_get_error(con, i);

    /* If it's not a fatal error, it must be retryable */
    return (err != SSL_ERROR_SSL)
           && (err != SSL_ERROR_SYSCALL)
           && (err != SSL_ERROR_ZERO_RETURN);
}

static int init_ssl_connection(SSL *con)
{
    int i;
    long verify_err;
    int retry = 0;

    if (dtlslisten || stateless) {
        BIO_ADDR *client = NULL;

        if (dtlslisten) {
            if ((client = BIO_ADDR_new()) == NULL) {
                BIO_printf(bio_err, "ERROR - memory\n");
                return 0;
            }
            i = DTLSv1_listen(con, client);
        } else {
            i = SSL_stateless(con);
        }
        if (i > 0) {
            BIO *wbio;
            int fd = -1;

            if (dtlslisten) {
                wbio = SSL_get_wbio(con);
                if (wbio) {
                    BIO_get_fd(wbio, &fd);
                }

                if (!wbio || BIO_connect(fd, client, 0) == 0) {
                    BIO_printf(bio_err, "ERROR - unable to connect\n");
                    BIO_ADDR_free(client);
                    return 0;
                }

                (void)BIO_ctrl_set_connected(wbio, client);
                BIO_ADDR_free(client);
                dtlslisten = 0;
            } else {
                stateless = 0;
            }
            i = SSL_accept(con);
        } else {
            BIO_ADDR_free(client);
        }
    } else {
        do {
            i = SSL_accept(con);

            if (i <= 0)
                retry = is_retryable(con, i);
#ifdef CERT_CB_TEST_RETRY
            {
                while (i <= 0
                        && SSL_get_error(con, i) == SSL_ERROR_WANT_X509_LOOKUP
                        && SSL_get_state(con) == TLS_ST_SR_CLNT_HELLO) {
                    BIO_printf(bio_err,
                               "LOOKUP from certificate callback during accept\n");
                    i = SSL_accept(con);
                    if (i <= 0)
                        retry = is_retryable(con, i);
                }
            }
#endif

#ifndef OPENSSL_NO_SRP
            while (i <= 0
                   && SSL_get_error(con, i) == SSL_ERROR_WANT_X509_LOOKUP) {
                BIO_printf(bio_s_out, "LOOKUP during accept %s\n",
                           srp_callback_parm.login);
                SRP_user_pwd_free(srp_callback_parm.user);
                srp_callback_parm.user =
                    SRP_VBASE_get1_by_user(srp_callback_parm.vb,
                                           srp_callback_parm.login);
                if (srp_callback_parm.user)
                    BIO_printf(bio_s_out, "LOOKUP done %s\n",
                               srp_callback_parm.user->info);
                else
                    BIO_printf(bio_s_out, "LOOKUP not successful\n");
                i = SSL_accept(con);
                if (i <= 0)
                    retry = is_retryable(con, i);
            }
#endif
        } while (i < 0 && SSL_waiting_for_async(con));
    }

    if (i <= 0) {
        if (((dtlslisten || stateless) && i == 0)
                || (!dtlslisten && !stateless && retry)) {
            BIO_printf(bio_s_out, "DELAY\n");
            return 1;
        }

        BIO_printf(bio_err, "ERROR\n");

        verify_err = SSL_get_verify_result(con);
        if (verify_err != X509_V_OK) {
            BIO_printf(bio_err, "verify error:%s\n",
                       X509_verify_cert_error_string(verify_err));
        }
        /* Always print any error messages */
        ERR_print_errors(bio_err);
        return 0;
    }

    print_connection_info(con);
    return 1;
}

static void print_connection_info(SSL *con)
{
    const char *str;
    X509 *peer;
    char buf[BUFSIZ];
#if !defined(OPENSSL_NO_NEXTPROTONEG)
    const unsigned char *next_proto_neg;
    unsigned next_proto_neg_len;
#endif
    unsigned char *exportedkeymat;
    int i;

    if (s_brief)
        print_ssl_summary(con);

    PEM_write_bio_SSL_SESSION(bio_s_out, SSL_get_session(con));

    peer = SSL_get_peer_certificate(con);
    if (peer != NULL) {
        BIO_printf(bio_s_out, "Client certificate\n");
        PEM_write_bio_X509(bio_s_out, peer);
        dump_cert_text(bio_s_out, peer);
        X509_free(peer);
        peer = NULL;
    }

    if (SSL_get_shared_ciphers(con, buf, sizeof(buf)) != NULL)
        BIO_printf(bio_s_out, "Shared ciphers:%s\n", buf);
    str = SSL_CIPHER_get_name(SSL_get_current_cipher(con));
    ssl_print_sigalgs(bio_s_out, con);
#ifndef OPENSSL_NO_EC
    ssl_print_point_formats(bio_s_out, con);
    ssl_print_groups(bio_s_out, con, 0);
#endif
    print_ca_names(bio_s_out, con);
    BIO_printf(bio_s_out, "CIPHER is %s\n", (str != NULL) ? str : "(NONE)");

#if !defined(OPENSSL_NO_NEXTPROTONEG)
    SSL_get0_next_proto_negotiated(con, &next_proto_neg, &next_proto_neg_len);
    if (next_proto_neg) {
        BIO_printf(bio_s_out, "NEXTPROTO is ");
        BIO_write(bio_s_out, next_proto_neg, next_proto_neg_len);
        BIO_printf(bio_s_out, "\n");
    }
#endif
#ifndef OPENSSL_NO_SRTP
    {
        SRTP_PROTECTION_PROFILE *srtp_profile
            = SSL_get_selected_srtp_profile(con);

        if (srtp_profile)
            BIO_printf(bio_s_out, "SRTP Extension negotiated, profile=%s\n",
                       srtp_profile->name);
    }
#endif
    if (SSL_session_reused(con))
        BIO_printf(bio_s_out, "Reused session-id\n");
    BIO_printf(bio_s_out, "Secure Renegotiation IS%s supported\n",
               SSL_get_secure_renegotiation_support(con) ? "" : " NOT");
    if ((SSL_get_options(con) & SSL_OP_NO_RENEGOTIATION))
        BIO_printf(bio_s_out, "Renegotiation is DISABLED\n");

    if (keymatexportlabel != NULL) {
        BIO_printf(bio_s_out, "Keying material exporter:\n");
        BIO_printf(bio_s_out, "    Label: '%s'\n", keymatexportlabel);
        BIO_printf(bio_s_out, "    Length: %i bytes\n", keymatexportlen);
        exportedkeymat = app_malloc(keymatexportlen, "export key");
        if (!SSL_export_keying_material(con, exportedkeymat,
                                        keymatexportlen,
                                        keymatexportlabel,
                                        strlen(keymatexportlabel),
                                        NULL, 0, 0)) {
            BIO_printf(bio_s_out, "    Error\n");
        } else {
            BIO_printf(bio_s_out, "    Keying material: ");
            for (i = 0; i < keymatexportlen; i++)
                BIO_printf(bio_s_out, "%02X", exportedkeymat[i]);
            BIO_printf(bio_s_out, "\n");
        }
        OPENSSL_free(exportedkeymat);
    }

    (void)BIO_flush(bio_s_out);
}

#ifndef OPENSSL_NO_DH
static DH *load_dh_param(const char *dhfile)
{
    DH *ret = NULL;
    BIO *bio;

    if ((bio = BIO_new_file(dhfile, "r")) == NULL)
        goto err;
    ret = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
 err:
    BIO_free(bio);
    return ret;
}
#endif

static int www_body(int s, int stype, int prot, unsigned char *context)
{
    char *buf = NULL;
    int ret = 1;
    int i, j, k, dot;
    SSL *con;
    const SSL_CIPHER *c;
    BIO *io, *ssl_bio, *sbio;
#ifdef RENEG
    int total_bytes = 0;
#endif
    int width;
    fd_set readfds;

    /* Set width for a select call if needed */
    width = s + 1;

    buf = app_malloc(bufsize, "server www buffer");
    io = BIO_new(BIO_f_buffer());
    ssl_bio = BIO_new(BIO_f_ssl());
    if ((io == NULL) || (ssl_bio == NULL))
        goto err;

    if (s_nbio) {
        if (!BIO_socket_nbio(s, 1))
            ERR_print_errors(bio_err);
        else if (!s_quiet)
            BIO_printf(bio_err, "Turned on non blocking io\n");
    }

    /* lets make the output buffer a reasonable size */
    if (!BIO_set_write_buffer_size(io, bufsize))
        goto err;

    if ((con = SSL_new(ctx)) == NULL)
        goto err;

    if (s_tlsextdebug) {
        SSL_set_tlsext_debug_callback(con, tlsext_cb);
        SSL_set_tlsext_debug_arg(con, bio_s_out);
    }

    if (context != NULL
        && !SSL_set_session_id_context(con, context,
                                       strlen((char *)context))) {
        SSL_free(con);
        goto err;
    }

    sbio = BIO_new_socket(s, BIO_NOCLOSE);
    if (s_nbio_test) {
        BIO *test;

        test = BIO_new(BIO_f_nbio_test());
        sbio = BIO_push(test, sbio);
    }
    SSL_set_bio(con, sbio, sbio);
    SSL_set_accept_state(con);

    /* No need to free |con| after this. Done by BIO_free(ssl_bio) */
    BIO_set_ssl(ssl_bio, con, BIO_CLOSE);
    BIO_push(io, ssl_bio);
#ifdef CHARSET_EBCDIC
    io = BIO_push(BIO_new(BIO_f_ebcdic_filter()), io);
#endif

    if (s_debug) {
        BIO_set_callback(SSL_get_rbio(con), bio_dump_callback);
        BIO_set_callback_arg(SSL_get_rbio(con), (char *)bio_s_out);
    }
    if (s_msg) {
#ifndef OPENSSL_NO_SSL_TRACE
        if (s_msg == 2)
            SSL_set_msg_callback(con, SSL_trace);
        else
#endif
            SSL_set_msg_callback(con, msg_cb);
        SSL_set_msg_callback_arg(con, bio_s_msg ? bio_s_msg : bio_s_out);
    }

    for (;;) {
        i = BIO_gets(io, buf, bufsize - 1);
        if (i < 0) {            /* error */
            if (!BIO_should_retry(io) && !SSL_waiting_for_async(con)) {
                if (!s_quiet)
                    ERR_print_errors(bio_err);
                goto err;
            } else {
                BIO_printf(bio_s_out, "read R BLOCK\n");
#ifndef OPENSSL_NO_SRP
                if (BIO_should_io_special(io)
                    && BIO_get_retry_reason(io) == BIO_RR_SSL_X509_LOOKUP) {
                    BIO_printf(bio_s_out, "LOOKUP renego during read\n");
                    SRP_user_pwd_free(srp_callback_parm.user);
                    srp_callback_parm.user =
                        SRP_VBASE_get1_by_user(srp_callback_parm.vb,
                                               srp_callback_parm.login);
                    if (srp_callback_parm.user)
                        BIO_printf(bio_s_out, "LOOKUP done %s\n",
                                   srp_callback_parm.user->info);
                    else
                        BIO_printf(bio_s_out, "LOOKUP not successful\n");
                    continue;
                }
#endif
#if !defined(OPENSSL_SYS_MSDOS)
                sleep(1);
#endif
                continue;
            }
        } else if (i == 0) {    /* end of input */
            ret = 1;
            goto end;
        }

        /* else we have data */
        if (((www == 1) && (strncmp("GET ", buf, 4) == 0)) ||
            ((www == 2) && (strncmp("GET /stats ", buf, 11) == 0))) {
            char *p;
            X509 *peer = NULL;
            STACK_OF(SSL_CIPHER) *sk;
            static const char *space = "                          ";

            if (www == 1 && strncmp("GET /reneg", buf, 10) == 0) {
                if (strncmp("GET /renegcert", buf, 14) == 0)
                    SSL_set_verify(con,
                                   SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE,
                                   NULL);
                i = SSL_renegotiate(con);
                BIO_printf(bio_s_out, "SSL_renegotiate -> %d\n", i);
                /* Send the HelloRequest */
                i = SSL_do_handshake(con);
                if (i <= 0) {
                    BIO_printf(bio_s_out, "SSL_do_handshake() Retval %d\n",
                               SSL_get_error(con, i));
                    ERR_print_errors(bio_err);
                    goto err;
                }
                /* Wait for a ClientHello to come back */
                FD_ZERO(&readfds);
                openssl_fdset(s, &readfds);
                i = select(width, (void *)&readfds, NULL, NULL, NULL);
                if (i <= 0 || !FD_ISSET(s, &readfds)) {
                    BIO_printf(bio_s_out,
                               "Error waiting for client response\n");
                    ERR_print_errors(bio_err);
                    goto err;
                }
                /*
                 * We're not actually expecting any data here and we ignore
                 * any that is sent. This is just to force the handshake that
                 * we're expecting to come from the client. If they haven't
                 * sent one there's not much we can do.
                 */
                BIO_gets(io, buf, bufsize - 1);
            }

            BIO_puts(io,
                     "HTTP/1.0 200 ok\r\nContent-type: text/html\r\n\r\n");
            BIO_puts(io, "<HTML><BODY BGCOLOR=\"#ffffff\">\n");
            BIO_puts(io, "<pre>\n");
            /* BIO_puts(io, OpenSSL_version(OPENSSL_VERSION)); */
            BIO_puts(io, "\n");
            for (i = 0; i < local_argc; i++) {
                const char *myp;
                for (myp = local_argv[i]; *myp; myp++)
                    switch (*myp) {
                    case '<':
                        BIO_puts(io, "&lt;");
                        break;
                    case '>':
                        BIO_puts(io, "&gt;");
                        break;
                    case '&':
                        BIO_puts(io, "&amp;");
                        break;
                    default:
                        BIO_write(io, myp, 1);
                        break;
                    }
                BIO_write(io, " ", 1);
            }
            BIO_puts(io, "\n");

            BIO_printf(io,
                       "Secure Renegotiation IS%s supported\n",
                       SSL_get_secure_renegotiation_support(con) ?
                       "" : " NOT");

            /*
             * The following is evil and should not really be done
             */
            BIO_printf(io, "Ciphers supported in s_server binary\n");
            sk = SSL_get_ciphers(con);
            j = sk_SSL_CIPHER_num(sk);
            for (i = 0; i < j; i++) {
                c = sk_SSL_CIPHER_value(sk, i);
                BIO_printf(io, "%-11s:%-25s ",
                           SSL_CIPHER_get_version(c), SSL_CIPHER_get_name(c));
                if ((((i + 1) % 2) == 0) && (i + 1 != j))
                    BIO_puts(io, "\n");
            }
            BIO_puts(io, "\n");
            p = SSL_get_shared_ciphers(con, buf, bufsize);
            if (p != NULL) {
                BIO_printf(io,
                           "---\nCiphers common between both SSL end points:\n");
                j = i = 0;
                while (*p) {
                    if (*p == ':') {
                        BIO_write(io, space, 26 - j);
                        i++;
                        j = 0;
                        BIO_write(io, ((i % 3) ? " " : "\n"), 1);
                    } else {
                        BIO_write(io, p, 1);
                        j++;
                    }
                    p++;
                }
                BIO_puts(io, "\n");
            }
            ssl_print_sigalgs(io, con);
#ifndef OPENSSL_NO_EC
            ssl_print_groups(io, con, 0);
#endif
            print_ca_names(io, con);
            BIO_printf(io, (SSL_session_reused(con)
                            ? "---\nReused, " : "---\nNew, "));
            c = SSL_get_current_cipher(con);
            BIO_printf(io, "%s, Cipher is %s\n",
                       SSL_CIPHER_get_version(c), SSL_CIPHER_get_name(c));
            SSL_SESSION_print(io, SSL_get_session(con));
            BIO_printf(io, "---\n");
            print_stats(io, SSL_get_SSL_CTX(con));
            BIO_printf(io, "---\n");
            peer = SSL_get_peer_certificate(con);
            if (peer != NULL) {
                BIO_printf(io, "Client certificate\n");
                X509_print(io, peer);
                PEM_write_bio_X509(io, peer);
                X509_free(peer);
                peer = NULL;
            } else {
                BIO_puts(io, "no client certificate available\n");
            }
            BIO_puts(io, "</pre></BODY></HTML>\r\n\r\n");
            break;
        } else if ((www == 2 || www == 3)
                   && (strncmp("GET /", buf, 5) == 0)) {
            BIO *file;
            char *p, *e;
            static const char *text =
                "HTTP/1.0 200 ok\r\nContent-type: text/plain\r\n\r\n";

            /* skip the '/' */
            p = &(buf[5]);

            dot = 1;
            for (e = p; *e != '\0'; e++) {
                if (e[0] == ' ')
                    break;

                switch (dot) {
                case 1:
                    dot = (e[0] == '.') ? 2 : 0;
                    break;
                case 2:
                    dot = (e[0] == '.') ? 3 : 0;
                    break;
                case 3:
                    dot = (e[0] == '/') ? -1 : 0;
                    break;
                }
                if (dot == 0)
                    dot = (e[0] == '/') ? 1 : 0;
            }
            dot = (dot == 3) || (dot == -1); /* filename contains ".."
                                              * component */

            if (*e == '\0') {
                BIO_puts(io, text);
                BIO_printf(io, "'%s' is an invalid file name\r\n", p);
                break;
            }
            *e = '\0';

            if (dot) {
                BIO_puts(io, text);
                BIO_printf(io, "'%s' contains '..' reference\r\n", p);
                break;
            }

            if (*p == '/') {
                BIO_puts(io, text);
                BIO_printf(io, "'%s' is an invalid path\r\n", p);
                break;
            }

            /* if a directory, do the index thang */
            if (app_isdir(p) > 0) {
                BIO_puts(io, text);
                BIO_printf(io, "'%s' is a directory\r\n", p);
                break;
            }

            if ((file = BIO_new_file(p, "r")) == NULL) {
                BIO_puts(io, text);
                BIO_printf(io, "Error opening '%s'\r\n", p);
                ERR_print_errors(io);
                break;
            }

            if (!s_quiet)
                BIO_printf(bio_err, "FILE:%s\n", p);

            if (www == 2) {
                i = strlen(p);
                if (((i > 5) && (strcmp(&(p[i - 5]), ".html") == 0)) ||
                    ((i > 4) && (strcmp(&(p[i - 4]), ".php") == 0)) ||
                    ((i > 4) && (strcmp(&(p[i - 4]), ".htm") == 0)))
                    BIO_puts(io,
                             "HTTP/1.0 200 ok\r\nContent-type: text/html\r\n\r\n");
                else
                    BIO_puts(io,
                             "HTTP/1.0 200 ok\r\nContent-type: text/plain\r\n\r\n");
            }
            /* send the file */
            for (;;) {
                i = BIO_read(file, buf, bufsize);
                if (i <= 0)
                    break;

#ifdef RENEG
                total_bytes += i;
                BIO_printf(bio_err, "%d\n", i);
                if (total_bytes > 3 * 1024) {
                    total_bytes = 0;
                    BIO_printf(bio_err, "RENEGOTIATE\n");
                    SSL_renegotiate(con);
                }
#endif

                for (j = 0; j < i;) {
#ifdef RENEG
                    static count = 0;
                    if (++count == 13) {
                        SSL_renegotiate(con);
                    }
#endif
                    k = BIO_write(io, &(buf[j]), i - j);
                    if (k <= 0) {
                        if (!BIO_should_retry(io)
                            && !SSL_waiting_for_async(con))
                            goto write_error;
                        else {
                            BIO_printf(bio_s_out, "rwrite W BLOCK\n");
                        }
                    } else {
                        j += k;
                    }
                }
            }
 write_error:
            BIO_free(file);
            break;
        }
    }

    for (;;) {
        i = (int)BIO_flush(io);
        if (i <= 0) {
            if (!BIO_should_retry(io))
                break;
        } else
            break;
    }
 end:
    /* make sure we re-use sessions */
    SSL_set_shutdown(con, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);

 err:
    OPENSSL_free(buf);
    BIO_free_all(io);
    return ret;
}

static int rev_body(int s, int stype, int prot, unsigned char *context)
{
    char *buf = NULL;
    int i;
    int ret = 1;
    SSL *con;
    BIO *io, *ssl_bio, *sbio;

    buf = app_malloc(bufsize, "server rev buffer");
    io = BIO_new(BIO_f_buffer());
    ssl_bio = BIO_new(BIO_f_ssl());
    if ((io == NULL) || (ssl_bio == NULL))
        goto err;

    /* lets make the output buffer a reasonable size */
    if (!BIO_set_write_buffer_size(io, bufsize))
        goto err;

    if ((con = SSL_new(ctx)) == NULL)
        goto err;

    if (s_tlsextdebug) {
        SSL_set_tlsext_debug_callback(con, tlsext_cb);
        SSL_set_tlsext_debug_arg(con, bio_s_out);
    }
    if (context != NULL
        && !SSL_set_session_id_context(con, context,
                                       strlen((char *)context))) {
        SSL_free(con);
        ERR_print_errors(bio_err);
        goto err;
    }

    sbio = BIO_new_socket(s, BIO_NOCLOSE);
    SSL_set_bio(con, sbio, sbio);
    SSL_set_accept_state(con);

    /* No need to free |con| after this. Done by BIO_free(ssl_bio) */
    BIO_set_ssl(ssl_bio, con, BIO_CLOSE);
    BIO_push(io, ssl_bio);
#ifdef CHARSET_EBCDIC
    io = BIO_push(BIO_new(BIO_f_ebcdic_filter()), io);
#endif

    if (s_debug) {
        BIO_set_callback(SSL_get_rbio(con), bio_dump_callback);
        BIO_set_callback_arg(SSL_get_rbio(con), (char *)bio_s_out);
    }
    if (s_msg) {
#ifndef OPENSSL_NO_SSL_TRACE
        if (s_msg == 2)
            SSL_set_msg_callback(con, SSL_trace);
        else
#endif
            SSL_set_msg_callback(con, msg_cb);
        SSL_set_msg_callback_arg(con, bio_s_msg ? bio_s_msg : bio_s_out);
    }

    for (;;) {
        i = BIO_do_handshake(io);
        if (i > 0)
            break;
        if (!BIO_should_retry(io)) {
            BIO_puts(bio_err, "CONNECTION FAILURE\n");
            ERR_print_errors(bio_err);
            goto end;
        }
#ifndef OPENSSL_NO_SRP
        if (BIO_should_io_special(io)
            && BIO_get_retry_reason(io) == BIO_RR_SSL_X509_LOOKUP) {
            BIO_printf(bio_s_out, "LOOKUP renego during accept\n");
            SRP_user_pwd_free(srp_callback_parm.user);
            srp_callback_parm.user =
                SRP_VBASE_get1_by_user(srp_callback_parm.vb,
                                       srp_callback_parm.login);
            if (srp_callback_parm.user)
                BIO_printf(bio_s_out, "LOOKUP done %s\n",
                           srp_callback_parm.user->info);
            else
                BIO_printf(bio_s_out, "LOOKUP not successful\n");
            continue;
        }
#endif
    }
    BIO_printf(bio_err, "CONNECTION ESTABLISHED\n");
    print_ssl_summary(con);

    for (;;) {
        i = BIO_gets(io, buf, bufsize - 1);
        if (i < 0) {            /* error */
            if (!BIO_should_retry(io)) {
                if (!s_quiet)
                    ERR_print_errors(bio_err);
                goto err;
            } else {
                BIO_printf(bio_s_out, "read R BLOCK\n");
#ifndef OPENSSL_NO_SRP
                if (BIO_should_io_special(io)
                    && BIO_get_retry_reason(io) == BIO_RR_SSL_X509_LOOKUP) {
                    BIO_printf(bio_s_out, "LOOKUP renego during read\n");
                    SRP_user_pwd_free(srp_callback_parm.user);
                    srp_callback_parm.user =
                        SRP_VBASE_get1_by_user(srp_callback_parm.vb,
                                               srp_callback_parm.login);
                    if (srp_callback_parm.user)
                        BIO_printf(bio_s_out, "LOOKUP done %s\n",
                                   srp_callback_parm.user->info);
                    else
                        BIO_printf(bio_s_out, "LOOKUP not successful\n");
                    continue;
                }
#endif
#if !defined(OPENSSL_SYS_MSDOS)
                sleep(1);
#endif
                continue;
            }
        } else if (i == 0) {    /* end of input */
            ret = 1;
            BIO_printf(bio_err, "CONNECTION CLOSED\n");
            goto end;
        } else {
            char *p = buf + i - 1;
            while (i && (*p == '\n' || *p == '\r')) {
                p--;
                i--;
            }
            if (!s_ign_eof && (i == 5) && (strncmp(buf, "CLOSE", 5) == 0)) {
                ret = 1;
                BIO_printf(bio_err, "CONNECTION CLOSED\n");
                goto end;
            }
            BUF_reverse((unsigned char *)buf, NULL, i);
            buf[i] = '\n';
            BIO_write(io, buf, i + 1);
            for (;;) {
                i = BIO_flush(io);
                if (i > 0)
                    break;
                if (!BIO_should_retry(io))
                    goto end;
            }
        }
    }
 end:
    /* make sure we re-use sessions */
    SSL_set_shutdown(con, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);

 err:

    OPENSSL_free(buf);
    BIO_free_all(io);
    return ret;
}

#define MAX_SESSION_ID_ATTEMPTS 10
static int generate_session_id(SSL *ssl, unsigned char *id,
                               unsigned int *id_len)
{
    unsigned int count = 0;
    do {
        if (RAND_bytes(id, *id_len) <= 0)
            return 0;
        /*
         * Prefix the session_id with the required prefix. NB: If our prefix
         * is too long, clip it - but there will be worse effects anyway, eg.
         * the server could only possibly create 1 session ID (ie. the
         * prefix!) so all future session negotiations will fail due to
         * conflicts.
         */
        memcpy(id, session_id_prefix,
               (strlen(session_id_prefix) < *id_len) ?
               strlen(session_id_prefix) : *id_len);
    }
    while (SSL_has_matching_session_id(ssl, id, *id_len) &&
           (++count < MAX_SESSION_ID_ATTEMPTS));
    if (count >= MAX_SESSION_ID_ATTEMPTS)
        return 0;
    return 1;
}

/*
 * By default s_server uses an in-memory cache which caches SSL_SESSION
 * structures without any serialisation. This hides some bugs which only
 * become apparent in deployed servers. By implementing a basic external
 * session cache some issues can be debugged using s_server.
 */

typedef struct simple_ssl_session_st {
    unsigned char *id;
    unsigned int idlen;
    unsigned char *der;
    int derlen;
    struct simple_ssl_session_st *next;
} simple_ssl_session;

static simple_ssl_session *first = NULL;

static int add_session(SSL *ssl, SSL_SESSION *session)
{
    simple_ssl_session *sess = app_malloc(sizeof(*sess), "get session");
    unsigned char *p;

    SSL_SESSION_get_id(session, &sess->idlen);
    sess->derlen = i2d_SSL_SESSION(session, NULL);
    if (sess->derlen < 0) {
        BIO_printf(bio_err, "Error encoding session\n");
        OPENSSL_free(sess);
        return 0;
    }

    sess->id = OPENSSL_memdup(SSL_SESSION_get_id(session, NULL), sess->idlen);
    sess->der = app_malloc(sess->derlen, "get session buffer");
    if (!sess->id) {
        BIO_printf(bio_err, "Out of memory adding to external cache\n");
        OPENSSL_free(sess->id);
        OPENSSL_free(sess->der);
        OPENSSL_free(sess);
        return 0;
    }
    p = sess->der;

    /* Assume it still works. */
    if (i2d_SSL_SESSION(session, &p) != sess->derlen) {
        BIO_printf(bio_err, "Unexpected session encoding length\n");
        OPENSSL_free(sess->id);
        OPENSSL_free(sess->der);
        OPENSSL_free(sess);
        return 0;
    }

    sess->next = first;
    first = sess;
    BIO_printf(bio_err, "New session added to external cache\n");
    return 0;
}

static SSL_SESSION *get_session(SSL *ssl, const unsigned char *id, int idlen,
                                int *do_copy)
{
    simple_ssl_session *sess;
    *do_copy = 0;
    for (sess = first; sess; sess = sess->next) {
        if (idlen == (int)sess->idlen && !memcmp(sess->id, id, idlen)) {
            const unsigned char *p = sess->der;
            BIO_printf(bio_err, "Lookup session: cache hit\n");
            return d2i_SSL_SESSION(NULL, &p, sess->derlen);
        }
    }
    BIO_printf(bio_err, "Lookup session: cache miss\n");
    return NULL;
}

static void del_session(SSL_CTX *sctx, SSL_SESSION *session)
{
    simple_ssl_session *sess, *prev = NULL;
    const unsigned char *id;
    unsigned int idlen;
    id = SSL_SESSION_get_id(session, &idlen);
    for (sess = first; sess; sess = sess->next) {
        if (idlen == sess->idlen && !memcmp(sess->id, id, idlen)) {
            if (prev)
                prev->next = sess->next;
            else
                first = sess->next;
            OPENSSL_free(sess->id);
            OPENSSL_free(sess->der);
            OPENSSL_free(sess);
            return;
        }
        prev = sess;
    }
}

static void init_session_cache_ctx(SSL_CTX *sctx)
{
    SSL_CTX_set_session_cache_mode(sctx,
                                   SSL_SESS_CACHE_NO_INTERNAL |
                                   SSL_SESS_CACHE_SERVER);
    SSL_CTX_sess_set_new_cb(sctx, add_session);
    SSL_CTX_sess_set_get_cb(sctx, get_session);
    SSL_CTX_sess_set_remove_cb(sctx, del_session);
}

static void free_sessions(void)
{
    simple_ssl_session *sess, *tsess;
    for (sess = first; sess;) {
        OPENSSL_free(sess->id);
        OPENSSL_free(sess->der);
        tsess = sess;
        sess = sess->next;
        OPENSSL_free(tsess);
    }
    first = NULL;
}

#endif                          /* OPENSSL_NO_SOCK */
