/*
 * Copyright (c) 2004 - 2007 Kungliga Tekniska Högskolan
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

#ifndef HEIMDAL_HX509_H
#define HEIMDAL_HX509_H 1

#include <rfc2459_asn1.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct hx509_cert_attribute_data *hx509_cert_attribute;
typedef struct hx509_cert_data *hx509_cert;
typedef struct hx509_certs_data *hx509_certs;
typedef struct hx509_context_data *hx509_context;
typedef struct hx509_crypto_data *hx509_crypto;
typedef struct hx509_lock_data *hx509_lock;
typedef struct hx509_name_data *hx509_name;
typedef struct hx509_private_key *hx509_private_key;
typedef struct hx509_private_key_ops hx509_private_key_ops;
typedef struct hx509_validate_ctx_data *hx509_validate_ctx;
typedef struct hx509_verify_ctx_data *hx509_verify_ctx;
typedef struct hx509_revoke_ctx_data *hx509_revoke_ctx;
typedef struct hx509_query_data hx509_query;
typedef void * hx509_cursor;
typedef struct hx509_request_data *hx509_request;
typedef struct hx509_error_data *hx509_error;
typedef struct hx509_peer_info *hx509_peer_info;
typedef struct hx509_ca_tbs *hx509_ca_tbs;
typedef struct hx509_env_data *hx509_env;
typedef struct hx509_crl *hx509_crl;

typedef void (*hx509_vprint_func)(void *, const char *, va_list);

enum {
    HX509_VHN_F_ALLOW_NO_MATCH = 1
};

enum {
    HX509_VALIDATE_F_VALIDATE = 1,
    HX509_VALIDATE_F_VERBOSE = 2
};

enum {
    HX509_CRYPTO_PADDING_PKCS7 = 0,
    HX509_CRYPTO_PADDING_NONE = 1
};

enum {
    HX509_KEY_FORMAT_GUESS = 0,
    HX509_KEY_FORMAT_DER = 1,
    HX509_KEY_FORMAT_WIN_BACKUPKEY = 2
};
typedef uint32_t hx509_key_format_t;

struct hx509_cert_attribute_data {
    heim_oid oid;
    heim_octet_string data;
};

typedef enum {
    HX509_PROMPT_TYPE_PASSWORD		= 0x1,	/* password, hidden */
    HX509_PROMPT_TYPE_QUESTION		= 0x2,	/* question, not hidden */
    HX509_PROMPT_TYPE_INFO		= 0x4	/* infomation, reply doesn't matter */
} hx509_prompt_type;

typedef struct hx509_prompt {
    const char *prompt;
    hx509_prompt_type type;
    heim_octet_string reply;
} hx509_prompt;

typedef int (*hx509_prompter_fct)(void *, const hx509_prompt *);

typedef struct hx509_octet_string_list {
    size_t len;
    heim_octet_string *val;
} hx509_octet_string_list;

typedef struct hx509_pem_header {
    struct hx509_pem_header *next;
    char *header;
    char *value;
} hx509_pem_header;

typedef int
(*hx509_pem_read_func)(hx509_context, const char *, const hx509_pem_header *,
		       const void *, size_t, void *ctx);

/*
 * Options passed to hx509_query_match_option.
 */
typedef enum {
    HX509_QUERY_OPTION_PRIVATE_KEY = 1,
    HX509_QUERY_OPTION_KU_ENCIPHERMENT = 2,
    HX509_QUERY_OPTION_KU_DIGITALSIGNATURE = 3,
    HX509_QUERY_OPTION_KU_KEYCERTSIGN = 4,
    HX509_QUERY_OPTION_END = 0xffff
} hx509_query_option;

/* flags to hx509_certs_init */
#define HX509_CERTS_CREATE				0x01
#define HX509_CERTS_UNPROTECT_ALL			0x02

/* flags to hx509_set_error_string */
#define HX509_ERROR_APPEND				0x01

/* flags to hx509_cms_unenvelope */
#define HX509_CMS_UE_DONT_REQUIRE_KU_ENCIPHERMENT	0x01
#define HX509_CMS_UE_ALLOW_WEAK				0x02

/* flags to hx509_cms_envelope_1 */
#define HX509_CMS_EV_NO_KU_CHECK			0x01
#define HX509_CMS_EV_ALLOW_WEAK				0x02
#define HX509_CMS_EV_ID_NAME				0x04

/* flags to hx509_cms_verify_signed */
#define HX509_CMS_VS_ALLOW_DATA_OID_MISMATCH		0x01
#define HX509_CMS_VS_NO_KU_CHECK			0x02
#define HX509_CMS_VS_ALLOW_ZERO_SIGNER			0x04
#define HX509_CMS_VS_NO_VALIDATE			0x08

/* selectors passed to hx509_crypto_select and hx509_crypto_available */
#define HX509_SELECT_ALL 0
#define HX509_SELECT_DIGEST 1
#define HX509_SELECT_PUBLIC_SIG 2
#define HX509_SELECT_PUBLIC_ENC 3
#define HX509_SELECT_SECRET_ENC 4

/* flags to hx509_ca_tbs_set_template */
#define HX509_CA_TEMPLATE_SUBJECT 1
#define HX509_CA_TEMPLATE_SERIAL 2
#define HX509_CA_TEMPLATE_NOTBEFORE 4
#define HX509_CA_TEMPLATE_NOTAFTER 8
#define HX509_CA_TEMPLATE_SPKI 16
#define HX509_CA_TEMPLATE_KU 32
#define HX509_CA_TEMPLATE_EKU 64

/* flags hx509_cms_create_signed* */
#define HX509_CMS_SIGNATURE_DETACHED			0x01
#define HX509_CMS_SIGNATURE_ID_NAME			0x02
#define HX509_CMS_SIGNATURE_NO_SIGNER			0x04
#define HX509_CMS_SIGNATURE_LEAF_ONLY			0x08
#define HX509_CMS_SIGNATURE_NO_CERTS			0x10

/* hx509_verify_hostname nametype */
typedef enum  {
    HX509_HN_HOSTNAME = 0,
    HX509_HN_DNSSRV
} hx509_hostname_type;

#include <hx509-protos.h>
#include <hx509_err.h>

#endif /* HEIMDAL_HX509_H */
