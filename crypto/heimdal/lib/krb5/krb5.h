/*
 * Copyright (c) 1997 - 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#ifndef __KRB5_H__
#define __KRB5_H__

#include <time.h>
#include <krb5-types.h>

#include <asn1_err.h>
#include <krb5_err.h>
#include <heim_err.h>
#include <k524_err.h>

#include <krb5_asn1.h>

/* name confusion with MIT */
#ifndef KRB5KDC_ERR_KEY_EXP
#define KRB5KDC_ERR_KEY_EXP KRB5KDC_ERR_KEY_EXPIRED
#endif

#ifdef _WIN32
#define KRB5_CALLCONV __stdcall
#else
#define KRB5_CALLCONV
#endif

/* simple constants */

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

typedef int krb5_boolean;

typedef int32_t krb5_error_code;

typedef int32_t krb5_kvno;

typedef uint32_t krb5_flags;

typedef void *krb5_pointer;
typedef const void *krb5_const_pointer;

struct krb5_crypto_data;
typedef struct krb5_crypto_data *krb5_crypto;

struct krb5_get_creds_opt_data;
typedef struct krb5_get_creds_opt_data *krb5_get_creds_opt;

struct krb5_digest_data;
typedef struct krb5_digest_data *krb5_digest;
struct krb5_ntlm_data;
typedef struct krb5_ntlm_data *krb5_ntlm;

struct krb5_pac_data;
typedef struct krb5_pac_data *krb5_pac;

typedef struct krb5_rd_req_in_ctx_data *krb5_rd_req_in_ctx;
typedef struct krb5_rd_req_out_ctx_data *krb5_rd_req_out_ctx;

typedef CKSUMTYPE krb5_cksumtype;

typedef Checksum krb5_checksum;

typedef ENCTYPE krb5_enctype;

typedef struct krb5_get_init_creds_ctx *krb5_init_creds_context;

typedef heim_octet_string krb5_data;

/* PKINIT related forward declarations */
struct ContentInfo;
struct krb5_pk_identity;
struct krb5_pk_cert;

/* krb5_enc_data is a mit compat structure */
typedef struct krb5_enc_data {
    krb5_enctype enctype;
    krb5_kvno kvno;
    krb5_data ciphertext;
} krb5_enc_data;

/* alternative names */
enum {
    ENCTYPE_NULL		= KRB5_ENCTYPE_NULL,
    ENCTYPE_DES_CBC_CRC		= KRB5_ENCTYPE_DES_CBC_CRC,
    ENCTYPE_DES_CBC_MD4		= KRB5_ENCTYPE_DES_CBC_MD4,
    ENCTYPE_DES_CBC_MD5		= KRB5_ENCTYPE_DES_CBC_MD5,
    ENCTYPE_DES3_CBC_MD5	= KRB5_ENCTYPE_DES3_CBC_MD5,
    ENCTYPE_OLD_DES3_CBC_SHA1	= KRB5_ENCTYPE_OLD_DES3_CBC_SHA1,
    ENCTYPE_SIGN_DSA_GENERATE	= KRB5_ENCTYPE_SIGN_DSA_GENERATE,
    ENCTYPE_ENCRYPT_RSA_PRIV	= KRB5_ENCTYPE_ENCRYPT_RSA_PRIV,
    ENCTYPE_ENCRYPT_RSA_PUB	= KRB5_ENCTYPE_ENCRYPT_RSA_PUB,
    ENCTYPE_DES3_CBC_SHA1	= KRB5_ENCTYPE_DES3_CBC_SHA1,
    ENCTYPE_AES128_CTS_HMAC_SHA1_96 = KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96,
    ENCTYPE_AES256_CTS_HMAC_SHA1_96 = KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96,
    ENCTYPE_ARCFOUR_HMAC	= KRB5_ENCTYPE_ARCFOUR_HMAC_MD5,
    ENCTYPE_ARCFOUR_HMAC_MD5	= KRB5_ENCTYPE_ARCFOUR_HMAC_MD5,
    ENCTYPE_ARCFOUR_HMAC_MD5_56	= KRB5_ENCTYPE_ARCFOUR_HMAC_MD5_56,
    ENCTYPE_ENCTYPE_PK_CROSS	= KRB5_ENCTYPE_ENCTYPE_PK_CROSS,
    ENCTYPE_DES_CBC_NONE	= KRB5_ENCTYPE_DES_CBC_NONE,
    ENCTYPE_DES3_CBC_NONE	= KRB5_ENCTYPE_DES3_CBC_NONE,
    ENCTYPE_DES_CFB64_NONE	= KRB5_ENCTYPE_DES_CFB64_NONE,
    ENCTYPE_DES_PCBC_NONE	= KRB5_ENCTYPE_DES_PCBC_NONE,
    ETYPE_NULL			= KRB5_ENCTYPE_NULL,
    ETYPE_DES_CBC_CRC		= KRB5_ENCTYPE_DES_CBC_CRC,
    ETYPE_DES_CBC_MD4		= KRB5_ENCTYPE_DES_CBC_MD4,
    ETYPE_DES_CBC_MD5		= KRB5_ENCTYPE_DES_CBC_MD5,
    ETYPE_DES3_CBC_MD5		= KRB5_ENCTYPE_DES3_CBC_MD5,
    ETYPE_OLD_DES3_CBC_SHA1	= KRB5_ENCTYPE_OLD_DES3_CBC_SHA1,
    ETYPE_SIGN_DSA_GENERATE	= KRB5_ENCTYPE_SIGN_DSA_GENERATE,
    ETYPE_ENCRYPT_RSA_PRIV	= KRB5_ENCTYPE_ENCRYPT_RSA_PRIV,
    ETYPE_ENCRYPT_RSA_PUB	= KRB5_ENCTYPE_ENCRYPT_RSA_PUB,
    ETYPE_DES3_CBC_SHA1		= KRB5_ENCTYPE_DES3_CBC_SHA1,
    ETYPE_AES128_CTS_HMAC_SHA1_96	= KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96,
    ETYPE_AES256_CTS_HMAC_SHA1_96	= KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96,
    ETYPE_ARCFOUR_HMAC_MD5	= KRB5_ENCTYPE_ARCFOUR_HMAC_MD5,
    ETYPE_ARCFOUR_HMAC_MD5_56	= KRB5_ENCTYPE_ARCFOUR_HMAC_MD5_56,
    ETYPE_ENCTYPE_PK_CROSS	= KRB5_ENCTYPE_ENCTYPE_PK_CROSS,
    ETYPE_ARCFOUR_MD4		= KRB5_ENCTYPE_ARCFOUR_MD4,
    ETYPE_ARCFOUR_HMAC_OLD	= KRB5_ENCTYPE_ARCFOUR_HMAC_OLD,
    ETYPE_ARCFOUR_HMAC_OLD_EXP	= KRB5_ENCTYPE_ARCFOUR_HMAC_OLD_EXP,
    ETYPE_DES_CBC_NONE		= KRB5_ENCTYPE_DES_CBC_NONE,
    ETYPE_DES3_CBC_NONE		= KRB5_ENCTYPE_DES3_CBC_NONE,
    ETYPE_DES_CFB64_NONE	= KRB5_ENCTYPE_DES_CFB64_NONE,
    ETYPE_DES_PCBC_NONE		= KRB5_ENCTYPE_DES_PCBC_NONE,
    ETYPE_DIGEST_MD5_NONE	= KRB5_ENCTYPE_DIGEST_MD5_NONE,
    ETYPE_CRAM_MD5_NONE		= KRB5_ENCTYPE_CRAM_MD5_NONE

};

/* PDU types */
typedef enum krb5_pdu {
    KRB5_PDU_ERROR = 0,
    KRB5_PDU_TICKET = 1,
    KRB5_PDU_AS_REQUEST = 2,
    KRB5_PDU_AS_REPLY = 3,
    KRB5_PDU_TGS_REQUEST = 4,
    KRB5_PDU_TGS_REPLY = 5,
    KRB5_PDU_AP_REQUEST = 6,
    KRB5_PDU_AP_REPLY = 7,
    KRB5_PDU_KRB_SAFE = 8,
    KRB5_PDU_KRB_PRIV = 9,
    KRB5_PDU_KRB_CRED = 10,
    KRB5_PDU_NONE = 11 /* See krb5_get_permitted_enctypes() */
} krb5_pdu;

typedef PADATA_TYPE krb5_preauthtype;

typedef enum krb5_key_usage {
    KRB5_KU_PA_ENC_TIMESTAMP = 1,
    /* AS-REQ PA-ENC-TIMESTAMP padata timestamp, encrypted with the
       client key (section 5.4.1) */
    KRB5_KU_TICKET = 2,
    /* AS-REP Ticket and TGS-REP Ticket (includes tgs session key or
       application session key), encrypted with the service key
       (section 5.4.2) */
    KRB5_KU_AS_REP_ENC_PART = 3,
    /* AS-REP encrypted part (includes tgs session key or application
       session key), encrypted with the client key (section 5.4.2) */
    KRB5_KU_TGS_REQ_AUTH_DAT_SESSION = 4,
    /* TGS-REQ KDC-REQ-BODY AuthorizationData, encrypted with the tgs
       session key (section 5.4.1) */
    KRB5_KU_TGS_REQ_AUTH_DAT_SUBKEY = 5,
    /* TGS-REQ KDC-REQ-BODY AuthorizationData, encrypted with the tgs
          authenticator subkey (section 5.4.1) */
    KRB5_KU_TGS_REQ_AUTH_CKSUM = 6,
    /* TGS-REQ PA-TGS-REQ padata AP-REQ Authenticator cksum, keyed
       with the tgs session key (sections 5.3.2, 5.4.1) */
    KRB5_KU_TGS_REQ_AUTH = 7,
    /* TGS-REQ PA-TGS-REQ padata AP-REQ Authenticator (includes tgs
       authenticator subkey), encrypted with the tgs session key
       (section 5.3.2) */
    KRB5_KU_TGS_REP_ENC_PART_SESSION = 8,
    /* TGS-REP encrypted part (includes application session key),
       encrypted with the tgs session key (section 5.4.2) */
    KRB5_KU_TGS_REP_ENC_PART_SUB_KEY = 9,
    /* TGS-REP encrypted part (includes application session key),
       encrypted with the tgs authenticator subkey (section 5.4.2) */
    KRB5_KU_AP_REQ_AUTH_CKSUM = 10,
    /* AP-REQ Authenticator cksum, keyed with the application session
       key (section 5.3.2) */
    KRB5_KU_AP_REQ_AUTH = 11,
    /* AP-REQ Authenticator (includes application authenticator
       subkey), encrypted with the application session key (section
       5.3.2) */
    KRB5_KU_AP_REQ_ENC_PART = 12,
    /* AP-REP encrypted part (includes application session subkey),
       encrypted with the application session key (section 5.5.2) */
    KRB5_KU_KRB_PRIV = 13,
    /* KRB-PRIV encrypted part, encrypted with a key chosen by the
       application (section 5.7.1) */
    KRB5_KU_KRB_CRED = 14,
    /* KRB-CRED encrypted part, encrypted with a key chosen by the
       application (section 5.8.1) */
    KRB5_KU_KRB_SAFE_CKSUM = 15,
    /* KRB-SAFE cksum, keyed with a key chosen by the application
       (section 5.6.1) */
    KRB5_KU_OTHER_ENCRYPTED = 16,
    /* Data which is defined in some specification outside of
       Kerberos to be encrypted using an RFC1510 encryption type. */
    KRB5_KU_OTHER_CKSUM = 17,
    /* Data which is defined in some specification outside of
       Kerberos to be checksummed using an RFC1510 checksum type. */
    KRB5_KU_KRB_ERROR = 18,
    /* Krb-error checksum */
    KRB5_KU_AD_KDC_ISSUED = 19,
    /* AD-KDCIssued checksum */
    KRB5_KU_MANDATORY_TICKET_EXTENSION = 20,
    /* Checksum for Mandatory Ticket Extensions */
    KRB5_KU_AUTH_DATA_TICKET_EXTENSION = 21,
    /* Checksum in Authorization Data in Ticket Extensions */
    KRB5_KU_USAGE_SEAL = 22,
    /* seal in GSSAPI krb5 mechanism */
    KRB5_KU_USAGE_SIGN = 23,
    /* sign in GSSAPI krb5 mechanism */
    KRB5_KU_USAGE_SEQ = 24,
    /* SEQ in GSSAPI krb5 mechanism */
    KRB5_KU_USAGE_ACCEPTOR_SEAL = 22,
    /* acceptor sign in GSSAPI CFX krb5 mechanism */
    KRB5_KU_USAGE_ACCEPTOR_SIGN = 23,
    /* acceptor seal in GSSAPI CFX krb5 mechanism */
    KRB5_KU_USAGE_INITIATOR_SEAL = 24,
    /* initiator sign in GSSAPI CFX krb5 mechanism */
    KRB5_KU_USAGE_INITIATOR_SIGN = 25,
    /* initiator seal in GSSAPI CFX krb5 mechanism */
    KRB5_KU_PA_SERVER_REFERRAL_DATA = 22,
    /* encrypted server referral data */
    KRB5_KU_SAM_CHECKSUM = 25,
    /* Checksum for the SAM-CHECKSUM field */
    KRB5_KU_SAM_ENC_TRACK_ID = 26,
    /* Encryption of the SAM-TRACK-ID field */
    KRB5_KU_PA_SERVER_REFERRAL = 26,
    /* Keyusage for the server referral in a TGS req */
    KRB5_KU_SAM_ENC_NONCE_SAD = 27,
    /* Encryption of the SAM-NONCE-OR-SAD field */
    KRB5_KU_PA_PKINIT_KX = 44,
    /* Encryption type of the kdc session contribution in pk-init */
    KRB5_KU_AS_REQ = 56,
    /* Checksum of over the AS-REQ send by the KDC in PA-REQ-ENC-PA-REP */
    KRB5_KU_DIGEST_ENCRYPT = -18,
    /* Encryption key usage used in the digest encryption field */
    KRB5_KU_DIGEST_OPAQUE = -19,
    /* Checksum key usage used in the digest opaque field */
    KRB5_KU_KRB5SIGNEDPATH = -21,
    /* Checksum key usage on KRB5SignedPath */
    KRB5_KU_CANONICALIZED_NAMES = -23
    /* Checksum key usage on PA-CANONICALIZED */
} krb5_key_usage;

typedef krb5_key_usage krb5_keyusage;

typedef enum krb5_salttype {
    KRB5_PW_SALT = KRB5_PADATA_PW_SALT,
    KRB5_AFS3_SALT = KRB5_PADATA_AFS3_SALT
}krb5_salttype;

typedef struct krb5_salt {
    krb5_salttype salttype;
    krb5_data saltvalue;
} krb5_salt;

typedef ETYPE_INFO krb5_preauthinfo;

typedef struct {
    krb5_preauthtype type;
    krb5_preauthinfo info; /* list of preauthinfo for this type */
} krb5_preauthdata_entry;

typedef struct krb5_preauthdata {
    unsigned len;
    krb5_preauthdata_entry *val;
}krb5_preauthdata;

typedef enum krb5_address_type {
    KRB5_ADDRESS_INET     =   2,
    KRB5_ADDRESS_NETBIOS  =  20,
    KRB5_ADDRESS_INET6    =  24,
    KRB5_ADDRESS_ADDRPORT = 256,
    KRB5_ADDRESS_IPPORT   = 257
} krb5_address_type;

enum {
  AP_OPTS_USE_SESSION_KEY = 1,
  AP_OPTS_MUTUAL_REQUIRED = 2,
  AP_OPTS_USE_SUBKEY = 4		/* library internal */
};

typedef HostAddress krb5_address;

typedef HostAddresses krb5_addresses;

typedef krb5_enctype krb5_keytype;

enum krb5_keytype_old {
    KEYTYPE_NULL	= ETYPE_NULL,
    KEYTYPE_DES		= ETYPE_DES_CBC_CRC,
    KEYTYPE_DES3	= ETYPE_OLD_DES3_CBC_SHA1,
    KEYTYPE_AES128	= ETYPE_AES128_CTS_HMAC_SHA1_96,
    KEYTYPE_AES256	= ETYPE_AES256_CTS_HMAC_SHA1_96,
    KEYTYPE_ARCFOUR	= ETYPE_ARCFOUR_HMAC_MD5,
    KEYTYPE_ARCFOUR_56	= ETYPE_ARCFOUR_HMAC_MD5_56
};

typedef EncryptionKey krb5_keyblock;

typedef AP_REQ krb5_ap_req;

struct krb5_cc_ops;

#ifdef _WIN32
#define KRB5_USE_PATH_TOKENS 1
#endif

#ifdef KRB5_USE_PATH_TOKENS
#define KRB5_DEFAULT_CCFILE_ROOT "%{TEMP}/krb5cc_"
#else
#define KRB5_DEFAULT_CCFILE_ROOT "/tmp/krb5cc_"
#endif

#define KRB5_DEFAULT_CCROOT "FILE:" KRB5_DEFAULT_CCFILE_ROOT

#define KRB5_ACCEPT_NULL_ADDRESSES(C) 					 \
    krb5_config_get_bool_default((C), NULL, TRUE, 			 \
				 "libdefaults", "accept_null_addresses", \
				 NULL)

typedef void *krb5_cc_cursor;
typedef struct krb5_cccol_cursor_data *krb5_cccol_cursor;

typedef struct krb5_ccache_data {
    const struct krb5_cc_ops *ops;
    krb5_data data;
}krb5_ccache_data;

typedef struct krb5_ccache_data *krb5_ccache;

typedef struct krb5_context_data *krb5_context;

typedef Realm krb5_realm;
typedef const char *krb5_const_realm; /* stupid language */

#define krb5_realm_length(r) strlen(r)
#define krb5_realm_data(r) (r)

typedef Principal krb5_principal_data;
typedef struct Principal *krb5_principal;
typedef const struct Principal *krb5_const_principal;
typedef struct Principals *krb5_principals;

typedef time_t krb5_deltat;
typedef time_t krb5_timestamp;

typedef struct krb5_times {
  krb5_timestamp authtime;
  krb5_timestamp starttime;
  krb5_timestamp endtime;
  krb5_timestamp renew_till;
} krb5_times;

typedef union {
    TicketFlags b;
    krb5_flags i;
} krb5_ticket_flags;

/* options for krb5_get_in_tkt() */
#define KDC_OPT_FORWARDABLE		(1 << 1)
#define KDC_OPT_FORWARDED		(1 << 2)
#define KDC_OPT_PROXIABLE		(1 << 3)
#define KDC_OPT_PROXY			(1 << 4)
#define KDC_OPT_ALLOW_POSTDATE		(1 << 5)
#define KDC_OPT_POSTDATED		(1 << 6)
#define KDC_OPT_RENEWABLE		(1 << 8)
#define KDC_OPT_REQUEST_ANONYMOUS	(1 << 14)
#define KDC_OPT_DISABLE_TRANSITED_CHECK	(1 << 26)
#define KDC_OPT_RENEWABLE_OK		(1 << 27)
#define KDC_OPT_ENC_TKT_IN_SKEY		(1 << 28)
#define KDC_OPT_RENEW			(1 << 30)
#define KDC_OPT_VALIDATE		(1 << 31)

typedef union {
    KDCOptions b;
    krb5_flags i;
} krb5_kdc_flags;

/* flags for krb5_verify_ap_req */

#define KRB5_VERIFY_AP_REQ_IGNORE_INVALID	(1 << 0)

#define KRB5_GC_CACHED			(1U << 0)
#define KRB5_GC_USER_USER		(1U << 1)
#define KRB5_GC_EXPIRED_OK		(1U << 2)
#define KRB5_GC_NO_STORE		(1U << 3)
#define KRB5_GC_FORWARDABLE		(1U << 4)
#define KRB5_GC_NO_TRANSIT_CHECK	(1U << 5)
#define KRB5_GC_CONSTRAINED_DELEGATION	(1U << 6)
#define KRB5_GC_CANONICALIZE		(1U << 7)

/* constants for compare_creds (and cc_retrieve_cred) */
#define KRB5_TC_DONT_MATCH_REALM	(1U << 31)
#define KRB5_TC_MATCH_KEYTYPE		(1U << 30)
#define KRB5_TC_MATCH_KTYPE		KRB5_TC_MATCH_KEYTYPE    /* MIT name */
#define KRB5_TC_MATCH_SRV_NAMEONLY	(1 << 29)
#define KRB5_TC_MATCH_FLAGS_EXACT	(1 << 28)
#define KRB5_TC_MATCH_FLAGS		(1 << 27)
#define KRB5_TC_MATCH_TIMES_EXACT	(1 << 26)
#define KRB5_TC_MATCH_TIMES		(1 << 25)
#define KRB5_TC_MATCH_AUTHDATA		(1 << 24)
#define KRB5_TC_MATCH_2ND_TKT		(1 << 23)
#define KRB5_TC_MATCH_IS_SKEY		(1 << 22)

/* constants for get_flags and set_flags */
#define KRB5_TC_OPENCLOSE 0x00000001
#define KRB5_TC_NOTICKET  0x00000002

typedef AuthorizationData krb5_authdata;

typedef KRB_ERROR krb5_error;

typedef struct krb5_creds {
    krb5_principal client;
    krb5_principal server;
    krb5_keyblock session;
    krb5_times times;
    krb5_data ticket;
    krb5_data second_ticket;
    krb5_authdata authdata;
    krb5_addresses addresses;
    krb5_ticket_flags flags;
} krb5_creds;

typedef struct krb5_cc_cache_cursor_data *krb5_cc_cache_cursor;

#define KRB5_CC_OPS_VERSION 3

typedef struct krb5_cc_ops {
    int version;
    const char *prefix;
    const char* (KRB5_CALLCONV * get_name)(krb5_context, krb5_ccache);
    krb5_error_code (KRB5_CALLCONV * resolve)(krb5_context, krb5_ccache *, const char *);
    krb5_error_code (KRB5_CALLCONV * gen_new)(krb5_context, krb5_ccache *);
    krb5_error_code (KRB5_CALLCONV * init)(krb5_context, krb5_ccache, krb5_principal);
    krb5_error_code (KRB5_CALLCONV * destroy)(krb5_context, krb5_ccache);
    krb5_error_code (KRB5_CALLCONV * close)(krb5_context, krb5_ccache);
    krb5_error_code (KRB5_CALLCONV * store)(krb5_context, krb5_ccache, krb5_creds*);
    krb5_error_code (KRB5_CALLCONV * retrieve)(krb5_context, krb5_ccache,
					       krb5_flags, const krb5_creds*, krb5_creds *);
    krb5_error_code (KRB5_CALLCONV * get_princ)(krb5_context, krb5_ccache, krb5_principal*);
    krb5_error_code (KRB5_CALLCONV * get_first)(krb5_context, krb5_ccache, krb5_cc_cursor *);
    krb5_error_code (KRB5_CALLCONV * get_next)(krb5_context, krb5_ccache,
					       krb5_cc_cursor*, krb5_creds*);
    krb5_error_code (KRB5_CALLCONV * end_get)(krb5_context, krb5_ccache, krb5_cc_cursor*);
    krb5_error_code (KRB5_CALLCONV * remove_cred)(krb5_context, krb5_ccache,
						  krb5_flags, krb5_creds*);
    krb5_error_code (KRB5_CALLCONV * set_flags)(krb5_context, krb5_ccache, krb5_flags);
    int (KRB5_CALLCONV * get_version)(krb5_context, krb5_ccache);
    krb5_error_code (KRB5_CALLCONV * get_cache_first)(krb5_context, krb5_cc_cursor *);
    krb5_error_code (KRB5_CALLCONV * get_cache_next)(krb5_context, krb5_cc_cursor,
						     krb5_ccache *);
    krb5_error_code (KRB5_CALLCONV * end_cache_get)(krb5_context, krb5_cc_cursor);
    krb5_error_code (KRB5_CALLCONV * move)(krb5_context, krb5_ccache, krb5_ccache);
    krb5_error_code (KRB5_CALLCONV * get_default_name)(krb5_context, char **);
    krb5_error_code (KRB5_CALLCONV * set_default)(krb5_context, krb5_ccache);
    krb5_error_code (KRB5_CALLCONV * lastchange)(krb5_context, krb5_ccache, krb5_timestamp *);
    krb5_error_code (KRB5_CALLCONV * set_kdc_offset)(krb5_context, krb5_ccache, krb5_deltat);
    krb5_error_code (KRB5_CALLCONV * get_kdc_offset)(krb5_context, krb5_ccache, krb5_deltat *);
} krb5_cc_ops;

struct krb5_log_facility;

struct krb5_config_binding {
    enum { krb5_config_string, krb5_config_list } type;
    char *name;
    struct krb5_config_binding *next;
    union {
	char *string;
	struct krb5_config_binding *list;
	void *generic;
    } u;
};

typedef struct krb5_config_binding krb5_config_binding;

typedef krb5_config_binding krb5_config_section;

typedef struct krb5_ticket {
    EncTicketPart ticket;
    krb5_principal client;
    krb5_principal server;
} krb5_ticket;

typedef Authenticator krb5_authenticator_data;

typedef krb5_authenticator_data *krb5_authenticator;

struct krb5_rcache_data;
typedef struct krb5_rcache_data *krb5_rcache;
typedef Authenticator krb5_donot_replay;

#define KRB5_STORAGE_HOST_BYTEORDER			0x01 /* old */
#define KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS	0x02
#define KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE		0x04
#define KRB5_STORAGE_KEYBLOCK_KEYTYPE_TWICE		0x08
#define KRB5_STORAGE_BYTEORDER_MASK			0x60
#define KRB5_STORAGE_BYTEORDER_BE			0x00 /* default */
#define KRB5_STORAGE_BYTEORDER_LE			0x20
#define KRB5_STORAGE_BYTEORDER_HOST			0x40
#define KRB5_STORAGE_CREDS_FLAGS_WRONG_BITORDER		0x80

struct krb5_storage_data;
typedef struct krb5_storage_data krb5_storage;

typedef struct krb5_keytab_entry {
    krb5_principal principal;
    krb5_kvno vno;
    krb5_keyblock keyblock;
    uint32_t timestamp;
    uint32_t flags;
    krb5_principals aliases;
} krb5_keytab_entry;

typedef struct krb5_kt_cursor {
    int fd;
    krb5_storage *sp;
    void *data;
} krb5_kt_cursor;

struct krb5_keytab_data;

typedef struct krb5_keytab_data *krb5_keytab;

#define KRB5_KT_PREFIX_MAX_LEN	30

struct krb5_keytab_data {
    const char *prefix;
    krb5_error_code (KRB5_CALLCONV * resolve)(krb5_context, const char*, krb5_keytab);
    krb5_error_code (KRB5_CALLCONV * get_name)(krb5_context, krb5_keytab, char*, size_t);
    krb5_error_code (KRB5_CALLCONV * close)(krb5_context, krb5_keytab);
    krb5_error_code (KRB5_CALLCONV * destroy)(krb5_context, krb5_keytab);
    krb5_error_code (KRB5_CALLCONV * get)(krb5_context, krb5_keytab, krb5_const_principal,
					  krb5_kvno, krb5_enctype, krb5_keytab_entry*);
    krb5_error_code (KRB5_CALLCONV * start_seq_get)(krb5_context, krb5_keytab, krb5_kt_cursor*);
    krb5_error_code (KRB5_CALLCONV * next_entry)(krb5_context, krb5_keytab,
						 krb5_keytab_entry*, krb5_kt_cursor*);
    krb5_error_code (KRB5_CALLCONV * end_seq_get)(krb5_context, krb5_keytab, krb5_kt_cursor*);
    krb5_error_code (KRB5_CALLCONV * add)(krb5_context, krb5_keytab, krb5_keytab_entry*);
    krb5_error_code (KRB5_CALLCONV * remove)(krb5_context, krb5_keytab, krb5_keytab_entry*);
    void *data;
    int32_t version;
};

typedef struct krb5_keytab_data krb5_kt_ops;

struct krb5_keytab_key_proc_args {
    krb5_keytab keytab;
    krb5_principal principal;
};

typedef struct krb5_keytab_key_proc_args krb5_keytab_key_proc_args;

typedef struct krb5_replay_data {
    krb5_timestamp timestamp;
    int32_t usec;
    uint32_t seq;
} krb5_replay_data;

/* flags for krb5_auth_con_setflags */
enum {
    KRB5_AUTH_CONTEXT_DO_TIME      		= 1,
    KRB5_AUTH_CONTEXT_RET_TIME     		= 2,
    KRB5_AUTH_CONTEXT_DO_SEQUENCE  		= 4,
    KRB5_AUTH_CONTEXT_RET_SEQUENCE 		= 8,
    KRB5_AUTH_CONTEXT_PERMIT_ALL   		= 16,
    KRB5_AUTH_CONTEXT_USE_SUBKEY   		= 32,
    KRB5_AUTH_CONTEXT_CLEAR_FORWARDED_CRED	= 64
};

/* flags for krb5_auth_con_genaddrs */
enum {
    KRB5_AUTH_CONTEXT_GENERATE_LOCAL_ADDR       = 1,
    KRB5_AUTH_CONTEXT_GENERATE_LOCAL_FULL_ADDR  = 3,
    KRB5_AUTH_CONTEXT_GENERATE_REMOTE_ADDR      = 4,
    KRB5_AUTH_CONTEXT_GENERATE_REMOTE_FULL_ADDR = 12
};

typedef struct krb5_auth_context_data {
    unsigned int flags;

    krb5_address *local_address;
    krb5_address *remote_address;
    int16_t local_port;
    int16_t remote_port;
    krb5_keyblock *keyblock;
    krb5_keyblock *local_subkey;
    krb5_keyblock *remote_subkey;

    uint32_t local_seqnumber;
    uint32_t remote_seqnumber;

    krb5_authenticator authenticator;

    krb5_pointer i_vector;

    krb5_rcache rcache;

    krb5_keytype keytype;	/* ¿requested key type ? */
    krb5_cksumtype cksumtype;	/* ¡requested checksum type! */

}krb5_auth_context_data, *krb5_auth_context;

typedef struct {
    KDC_REP kdc_rep;
    EncKDCRepPart enc_part;
    KRB_ERROR error;
} krb5_kdc_rep;

extern const char *heimdal_version, *heimdal_long_version;

typedef void (KRB5_CALLCONV * krb5_log_log_func_t)(const char*, const char*, void*);
typedef void (KRB5_CALLCONV * krb5_log_close_func_t)(void*);

typedef struct krb5_log_facility {
    char *program;
    int len;
    struct facility *val;
} krb5_log_facility;

typedef EncAPRepPart krb5_ap_rep_enc_part;

#define KRB5_RECVAUTH_IGNORE_VERSION 1

#define KRB5_SENDAUTH_VERSION "KRB5_SENDAUTH_V1.0"

#define KRB5_TGS_NAME_SIZE (6)
#define KRB5_TGS_NAME ("krbtgt")
#define KRB5_WELLKNOWN_NAME ("WELLKNOWN")
#define KRB5_ANON_NAME ("ANONYMOUS")
#define KRB5_DIGEST_NAME ("digest")

typedef enum {
    KRB5_PROMPT_TYPE_PASSWORD		= 0x1,
    KRB5_PROMPT_TYPE_NEW_PASSWORD	= 0x2,
    KRB5_PROMPT_TYPE_NEW_PASSWORD_AGAIN = 0x3,
    KRB5_PROMPT_TYPE_PREAUTH		= 0x4,
    KRB5_PROMPT_TYPE_INFO		= 0x5
} krb5_prompt_type;

typedef struct _krb5_prompt {
    const char *prompt;
    int hidden;
    krb5_data *reply;
    krb5_prompt_type type;
} krb5_prompt;

typedef int (KRB5_CALLCONV * krb5_prompter_fct)(krb5_context /*context*/,
						void * /*data*/,
						const char * /*name*/,
						const char * /*banner*/,
						int /*num_prompts*/,
						krb5_prompt /*prompts*/[]);
typedef krb5_error_code (KRB5_CALLCONV * krb5_key_proc)(krb5_context /*context*/,
							krb5_enctype /*type*/,
							krb5_salt /*salt*/,
							krb5_const_pointer /*keyseed*/,
							krb5_keyblock ** /*key*/);
typedef krb5_error_code (KRB5_CALLCONV * krb5_decrypt_proc)(krb5_context /*context*/,
							    krb5_keyblock * /*key*/,
							    krb5_key_usage /*usage*/,
							    krb5_const_pointer /*decrypt_arg*/,
							    krb5_kdc_rep * /*dec_rep*/);
typedef krb5_error_code (KRB5_CALLCONV * krb5_s2k_proc)(krb5_context /*context*/,
							krb5_enctype /*type*/,
							krb5_const_pointer /*keyseed*/,
							krb5_salt /*salt*/,
							krb5_data * /*s2kparms*/,
							krb5_keyblock ** /*key*/);

struct _krb5_get_init_creds_opt_private;

struct _krb5_get_init_creds_opt {
    krb5_flags flags;
    krb5_deltat tkt_life;
    krb5_deltat renew_life;
    int forwardable;
    int proxiable;
    int anonymous;
    krb5_enctype *etype_list;
    int etype_list_length;
    krb5_addresses *address_list;
    /* XXX the next three should not be used, as they may be
       removed later */
    krb5_preauthtype *preauth_list;
    int preauth_list_length;
    krb5_data *salt;
    struct _krb5_get_init_creds_opt_private *opt_private;
};

typedef struct _krb5_get_init_creds_opt krb5_get_init_creds_opt;

#define KRB5_GET_INIT_CREDS_OPT_TKT_LIFE	0x0001
#define KRB5_GET_INIT_CREDS_OPT_RENEW_LIFE	0x0002
#define KRB5_GET_INIT_CREDS_OPT_FORWARDABLE	0x0004
#define KRB5_GET_INIT_CREDS_OPT_PROXIABLE	0x0008
#define KRB5_GET_INIT_CREDS_OPT_ETYPE_LIST	0x0010
#define KRB5_GET_INIT_CREDS_OPT_ADDRESS_LIST	0x0020
#define KRB5_GET_INIT_CREDS_OPT_PREAUTH_LIST	0x0040
#define KRB5_GET_INIT_CREDS_OPT_SALT		0x0080 /* no supported */
#define KRB5_GET_INIT_CREDS_OPT_ANONYMOUS	0x0100
#define KRB5_GET_INIT_CREDS_OPT_DISABLE_TRANSITED_CHECK	0x0200

/* krb5_init_creds_step flags argument */
#define KRB5_INIT_CREDS_STEP_FLAG_CONTINUE	0x0001

typedef struct _krb5_verify_init_creds_opt {
    krb5_flags flags;
    int ap_req_nofail;
} krb5_verify_init_creds_opt;

#define KRB5_VERIFY_INIT_CREDS_OPT_AP_REQ_NOFAIL	0x0001

typedef struct krb5_verify_opt {
    unsigned int flags;
    krb5_ccache ccache;
    krb5_keytab keytab;
    krb5_boolean secure;
    const char *service;
} krb5_verify_opt;

#define KRB5_VERIFY_LREALMS		1
#define KRB5_VERIFY_NO_ADDRESSES	2

#define KRB5_KPASSWD_VERS_CHANGEPW      1
#define KRB5_KPASSWD_VERS_SETPW         0xff80

#define KRB5_KPASSWD_SUCCESS	0
#define KRB5_KPASSWD_MALFORMED	1
#define KRB5_KPASSWD_HARDERROR	2
#define KRB5_KPASSWD_AUTHERROR	3
#define KRB5_KPASSWD_SOFTERROR	4
#define KRB5_KPASSWD_ACCESSDENIED 5
#define KRB5_KPASSWD_BAD_VERSION 6
#define KRB5_KPASSWD_INITIAL_FLAG_NEEDED 7

#define KPASSWD_PORT 464

/* types for the new krbhst interface */
struct krb5_krbhst_data;
typedef struct krb5_krbhst_data *krb5_krbhst_handle;

#define KRB5_KRBHST_KDC		1
#define KRB5_KRBHST_ADMIN	2
#define KRB5_KRBHST_CHANGEPW	3
#define KRB5_KRBHST_KRB524	4
#define KRB5_KRBHST_KCA		5

typedef struct krb5_krbhst_info {
    enum { KRB5_KRBHST_UDP,
	   KRB5_KRBHST_TCP,
	   KRB5_KRBHST_HTTP } proto;
    unsigned short port;
    unsigned short def_port;
    struct addrinfo *ai;
    struct krb5_krbhst_info *next;
    char hostname[1]; /* has to come last */
} krb5_krbhst_info;

/* flags for krb5_krbhst_init_flags (and krb5_send_to_kdc_flags) */
enum {
    KRB5_KRBHST_FLAGS_MASTER      = 1,
    KRB5_KRBHST_FLAGS_LARGE_MSG	  = 2
};

typedef krb5_error_code
(KRB5_CALLCONV * krb5_send_to_kdc_func)(krb5_context, void *, krb5_krbhst_info *, time_t,
					const krb5_data *, krb5_data *);

/** flags for krb5_parse_name_flags */
enum {
    KRB5_PRINCIPAL_PARSE_NO_REALM = 1, /**< Require that there are no realm */
    KRB5_PRINCIPAL_PARSE_REQUIRE_REALM = 2, /**< Require a realm present */
    KRB5_PRINCIPAL_PARSE_ENTERPRISE = 4 /**< Parse as a NT-ENTERPRISE name */
};

/** flags for krb5_unparse_name_flags */
enum {
    KRB5_PRINCIPAL_UNPARSE_SHORT = 1, /**< No realm if it is the default realm */
    KRB5_PRINCIPAL_UNPARSE_NO_REALM = 2, /**< No realm */
    KRB5_PRINCIPAL_UNPARSE_DISPLAY = 4 /**< No quoting */
};

typedef struct krb5_sendto_ctx_data *krb5_sendto_ctx;

#define KRB5_SENDTO_DONE	0
#define KRB5_SENDTO_RESTART	1
#define KRB5_SENDTO_CONTINUE	2

typedef krb5_error_code
(KRB5_CALLCONV * krb5_sendto_ctx_func)(krb5_context, krb5_sendto_ctx, void *,
				       const krb5_data *, int *);

struct krb5_plugin;
enum krb5_plugin_type {
    PLUGIN_TYPE_DATA = 1,
    PLUGIN_TYPE_FUNC
};

struct credentials; /* this is to keep the compiler happy */
struct getargs;
struct sockaddr;

/**
 * Semi private, not stable yet
 */

typedef struct krb5_crypto_iov {
    unsigned int flags;
    /* ignored */
#define KRB5_CRYPTO_TYPE_EMPTY		0
    /* OUT krb5_crypto_length(KRB5_CRYPTO_TYPE_HEADER) */
#define KRB5_CRYPTO_TYPE_HEADER		1
    /* IN and OUT */
#define KRB5_CRYPTO_TYPE_DATA		2
    /* IN */
#define KRB5_CRYPTO_TYPE_SIGN_ONLY	3
   /* (only for encryption) OUT krb5_crypto_length(KRB5_CRYPTO_TYPE_TRAILER) */
#define KRB5_CRYPTO_TYPE_PADDING	4
   /* OUT krb5_crypto_length(KRB5_CRYPTO_TYPE_TRAILER) */
#define KRB5_CRYPTO_TYPE_TRAILER	5
   /* OUT krb5_crypto_length(KRB5_CRYPTO_TYPE_CHECKSUM) */
#define KRB5_CRYPTO_TYPE_CHECKSUM	6
    krb5_data data;
} krb5_crypto_iov;


/* Glue for MIT */

typedef struct {
    int32_t lr_type;
    krb5_timestamp value;
} krb5_last_req_entry;

typedef krb5_error_code
(KRB5_CALLCONV * krb5_gic_process_last_req)(krb5_context, krb5_last_req_entry **, void *);

/*
 *
 */

struct hx509_certs_data;

#include <krb5-protos.h>

/* variables */

extern KRB5_LIB_VARIABLE const char *krb5_config_file;
extern KRB5_LIB_VARIABLE const char *krb5_defkeyname;


extern KRB5_LIB_VARIABLE const krb5_cc_ops krb5_acc_ops;
extern KRB5_LIB_VARIABLE const krb5_cc_ops krb5_fcc_ops;
extern KRB5_LIB_VARIABLE const krb5_cc_ops krb5_mcc_ops;
extern KRB5_LIB_VARIABLE const krb5_cc_ops krb5_kcm_ops;
extern KRB5_LIB_VARIABLE const krb5_cc_ops krb5_akcm_ops;
extern KRB5_LIB_VARIABLE const krb5_cc_ops krb5_scc_ops;

extern KRB5_LIB_VARIABLE const krb5_kt_ops krb5_fkt_ops;
extern KRB5_LIB_VARIABLE const krb5_kt_ops krb5_wrfkt_ops;
extern KRB5_LIB_VARIABLE const krb5_kt_ops krb5_javakt_ops;
extern KRB5_LIB_VARIABLE const krb5_kt_ops krb5_mkt_ops;
extern KRB5_LIB_VARIABLE const krb5_kt_ops krb5_akf_ops;
extern KRB5_LIB_VARIABLE const krb5_kt_ops krb5_any_ops;

extern KRB5_LIB_VARIABLE const char *krb5_cc_type_api;
extern KRB5_LIB_VARIABLE const char *krb5_cc_type_file;
extern KRB5_LIB_VARIABLE const char *krb5_cc_type_memory;
extern KRB5_LIB_VARIABLE const char *krb5_cc_type_kcm;
extern KRB5_LIB_VARIABLE const char *krb5_cc_type_scc;

#endif /* __KRB5_H__ */

