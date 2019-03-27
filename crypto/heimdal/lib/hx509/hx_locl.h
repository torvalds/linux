/*
 * Copyright (c) 2004 - 2006 Kungliga Tekniska Högskolan
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

/* $Id$ */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <assert.h>
#include <stdarg.h>
#include <err.h>
#include <limits.h>

#include <roken.h>

#include <getarg.h>
#include <base64.h>
#include <hex.h>
#include <com_err.h>
#include <parse_units.h>
#include <parse_bytes.h>

#include <krb5-types.h>

#include <rfc2459_asn1.h>
#include <cms_asn1.h>
#include <pkcs8_asn1.h>
#include <pkcs9_asn1.h>
#include <pkcs12_asn1.h>
#include <ocsp_asn1.h>
#include <pkcs10_asn1.h>
#include <asn1_err.h>
#include <pkinit_asn1.h>

#include <der.h>

#define HC_DEPRECATED_CRYPTO
#include "crypto-headers.h"

struct hx509_keyset_ops;
struct hx509_collector;
struct hx509_generate_private_context;
typedef struct hx509_path hx509_path;

#include <hx509.h>

typedef void (*_hx509_cert_release_func)(struct hx509_cert_data *, void *);


#include "sel.h"

#include <hx509-private.h>
#include <hx509_err.h>

struct hx509_peer_info {
    hx509_cert cert;
    AlgorithmIdentifier *val;
    size_t len;
};

#define HX509_CERTS_FIND_SERIALNUMBER		1
#define HX509_CERTS_FIND_ISSUER			2
#define HX509_CERTS_FIND_SUBJECT		4
#define HX509_CERTS_FIND_ISSUER_KEY_ID		8
#define HX509_CERTS_FIND_SUBJECT_KEY_ID		16

struct hx509_name_data {
    Name der_name;
};

struct hx509_path {
    size_t len;
    hx509_cert *val;
};

struct hx509_query_data {
    int match;
#define HX509_QUERY_FIND_ISSUER_CERT		0x000001
#define HX509_QUERY_MATCH_SERIALNUMBER		0x000002
#define HX509_QUERY_MATCH_ISSUER_NAME		0x000004
#define HX509_QUERY_MATCH_SUBJECT_NAME		0x000008
#define HX509_QUERY_MATCH_SUBJECT_KEY_ID	0x000010
#define HX509_QUERY_MATCH_ISSUER_ID		0x000020
#define HX509_QUERY_PRIVATE_KEY			0x000040
#define HX509_QUERY_KU_ENCIPHERMENT		0x000080
#define HX509_QUERY_KU_DIGITALSIGNATURE		0x000100
#define HX509_QUERY_KU_KEYCERTSIGN		0x000200
#define HX509_QUERY_KU_CRLSIGN			0x000400
#define HX509_QUERY_KU_NONREPUDIATION		0x000800
#define HX509_QUERY_KU_KEYAGREEMENT		0x001000
#define HX509_QUERY_KU_DATAENCIPHERMENT		0x002000
#define HX509_QUERY_ANCHOR			0x004000
#define HX509_QUERY_MATCH_CERTIFICATE		0x008000
#define HX509_QUERY_MATCH_LOCAL_KEY_ID		0x010000
#define HX509_QUERY_NO_MATCH_PATH		0x020000
#define HX509_QUERY_MATCH_FRIENDLY_NAME		0x040000
#define HX509_QUERY_MATCH_FUNCTION		0x080000
#define HX509_QUERY_MATCH_KEY_HASH_SHA1		0x100000
#define HX509_QUERY_MATCH_TIME			0x200000
#define HX509_QUERY_MATCH_EKU			0x400000
#define HX509_QUERY_MATCH_EXPR			0x800000
#define HX509_QUERY_MASK			0xffffff
    Certificate *subject;
    Certificate *certificate;
    heim_integer *serial;
    heim_octet_string *subject_id;
    heim_octet_string *local_key_id;
    Name *issuer_name;
    Name *subject_name;
    hx509_path *path;
    char *friendlyname;
    int (*cmp_func)(hx509_context, hx509_cert, void *);
    void *cmp_func_ctx;
    heim_octet_string *keyhash_sha1;
    time_t timenow;
    heim_oid *eku;
    struct hx_expr *expr;
};

struct hx509_keyset_ops {
    const char *name;
    int flags;
    int (*init)(hx509_context, hx509_certs, void **,
		int, const char *, hx509_lock);
    int (*store)(hx509_context, hx509_certs, void *, int, hx509_lock);
    int (*free)(hx509_certs, void *);
    int (*add)(hx509_context, hx509_certs, void *, hx509_cert);
    int (*query)(hx509_context, hx509_certs, void *,
		 const hx509_query *, hx509_cert *);
    int (*iter_start)(hx509_context, hx509_certs, void *, void **);
    int (*iter)(hx509_context, hx509_certs, void *, void *, hx509_cert *);
    int (*iter_end)(hx509_context, hx509_certs, void *, void *);
    int (*printinfo)(hx509_context, hx509_certs,
		     void *, int (*)(void *, const char *), void *);
    int (*getkeys)(hx509_context, hx509_certs, void *, hx509_private_key **);
    int (*addkey)(hx509_context, hx509_certs, void *, hx509_private_key);
};

struct _hx509_password {
    size_t len;
    char **val;
};

extern hx509_lock _hx509_empty_lock;

struct hx509_context_data {
    struct hx509_keyset_ops **ks_ops;
    int ks_num_ops;
    int flags;
#define HX509_CTX_VERIFY_MISSING_OK	1
    int ocsp_time_diff;
#define HX509_DEFAULT_OCSP_TIME_DIFF	(5*60)
    hx509_error error;
    struct et_list *et_list;
    char *querystat;
    hx509_certs default_trust_anchors;
};

/* _hx509_calculate_path flag field */
#define HX509_CALCULATE_PATH_NO_ANCHOR 1

/* environment */
struct hx509_env_data {
    enum { env_string, env_list } type;
    char *name;
    struct hx509_env_data *next;
    union {
	char *string;
	struct hx509_env_data *list;
    } u;
};


extern const AlgorithmIdentifier * _hx509_crypto_default_sig_alg;
extern const AlgorithmIdentifier * _hx509_crypto_default_digest_alg;
extern const AlgorithmIdentifier * _hx509_crypto_default_secret_alg;

/*
 * Configurable options
 */

#ifdef __APPLE__
#define HX509_DEFAULT_ANCHORS "KEYCHAIN:system-anchors"
#endif
