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

#ifndef HEADER_SSL_H
# define HEADER_SSL_H

# include <openssl/e_os2.h>
# include <openssl/opensslconf.h>
# include <openssl/comp.h>
# include <openssl/bio.h>
# if OPENSSL_API_COMPAT < 0x10100000L
#  include <openssl/x509.h>
#  include <openssl/crypto.h>
#  include <openssl/buffer.h>
# endif
# include <openssl/lhash.h>
# include <openssl/pem.h>
# include <openssl/hmac.h>
# include <openssl/async.h>

# include <openssl/safestack.h>
# include <openssl/symhacks.h>
# include <openssl/ct.h>
# include <openssl/sslerr.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* OpenSSL version number for ASN.1 encoding of the session information */
/*-
 * Version 0 - initial version
 * Version 1 - added the optional peer certificate
 */
# define SSL_SESSION_ASN1_VERSION 0x0001

# define SSL_MAX_SSL_SESSION_ID_LENGTH           32
# define SSL_MAX_SID_CTX_LENGTH                  32

# define SSL_MIN_RSA_MODULUS_LENGTH_IN_BYTES     (512/8)
# define SSL_MAX_KEY_ARG_LENGTH                  8
# define SSL_MAX_MASTER_KEY_LENGTH               48

/* The maximum number of encrypt/decrypt pipelines we can support */
# define SSL_MAX_PIPELINES  32

/* text strings for the ciphers */

/* These are used to specify which ciphers to use and not to use */

# define SSL_TXT_LOW             "LOW"
# define SSL_TXT_MEDIUM          "MEDIUM"
# define SSL_TXT_HIGH            "HIGH"
# define SSL_TXT_FIPS            "FIPS"

# define SSL_TXT_aNULL           "aNULL"
# define SSL_TXT_eNULL           "eNULL"
# define SSL_TXT_NULL            "NULL"

# define SSL_TXT_kRSA            "kRSA"
# define SSL_TXT_kDHr            "kDHr"/* this cipher class has been removed */
# define SSL_TXT_kDHd            "kDHd"/* this cipher class has been removed */
# define SSL_TXT_kDH             "kDH"/* this cipher class has been removed */
# define SSL_TXT_kEDH            "kEDH"/* alias for kDHE */
# define SSL_TXT_kDHE            "kDHE"
# define SSL_TXT_kECDHr          "kECDHr"/* this cipher class has been removed */
# define SSL_TXT_kECDHe          "kECDHe"/* this cipher class has been removed */
# define SSL_TXT_kECDH           "kECDH"/* this cipher class has been removed */
# define SSL_TXT_kEECDH          "kEECDH"/* alias for kECDHE */
# define SSL_TXT_kECDHE          "kECDHE"
# define SSL_TXT_kPSK            "kPSK"
# define SSL_TXT_kRSAPSK         "kRSAPSK"
# define SSL_TXT_kECDHEPSK       "kECDHEPSK"
# define SSL_TXT_kDHEPSK         "kDHEPSK"
# define SSL_TXT_kGOST           "kGOST"
# define SSL_TXT_kSRP            "kSRP"

# define SSL_TXT_aRSA            "aRSA"
# define SSL_TXT_aDSS            "aDSS"
# define SSL_TXT_aDH             "aDH"/* this cipher class has been removed */
# define SSL_TXT_aECDH           "aECDH"/* this cipher class has been removed */
# define SSL_TXT_aECDSA          "aECDSA"
# define SSL_TXT_aPSK            "aPSK"
# define SSL_TXT_aGOST94         "aGOST94"
# define SSL_TXT_aGOST01         "aGOST01"
# define SSL_TXT_aGOST12         "aGOST12"
# define SSL_TXT_aGOST           "aGOST"
# define SSL_TXT_aSRP            "aSRP"

# define SSL_TXT_DSS             "DSS"
# define SSL_TXT_DH              "DH"
# define SSL_TXT_DHE             "DHE"/* same as "kDHE:-ADH" */
# define SSL_TXT_EDH             "EDH"/* alias for DHE */
# define SSL_TXT_ADH             "ADH"
# define SSL_TXT_RSA             "RSA"
# define SSL_TXT_ECDH            "ECDH"
# define SSL_TXT_EECDH           "EECDH"/* alias for ECDHE" */
# define SSL_TXT_ECDHE           "ECDHE"/* same as "kECDHE:-AECDH" */
# define SSL_TXT_AECDH           "AECDH"
# define SSL_TXT_ECDSA           "ECDSA"
# define SSL_TXT_PSK             "PSK"
# define SSL_TXT_SRP             "SRP"

# define SSL_TXT_DES             "DES"
# define SSL_TXT_3DES            "3DES"
# define SSL_TXT_RC4             "RC4"
# define SSL_TXT_RC2             "RC2"
# define SSL_TXT_IDEA            "IDEA"
# define SSL_TXT_SEED            "SEED"
# define SSL_TXT_AES128          "AES128"
# define SSL_TXT_AES256          "AES256"
# define SSL_TXT_AES             "AES"
# define SSL_TXT_AES_GCM         "AESGCM"
# define SSL_TXT_AES_CCM         "AESCCM"
# define SSL_TXT_AES_CCM_8       "AESCCM8"
# define SSL_TXT_CAMELLIA128     "CAMELLIA128"
# define SSL_TXT_CAMELLIA256     "CAMELLIA256"
# define SSL_TXT_CAMELLIA        "CAMELLIA"
# define SSL_TXT_CHACHA20        "CHACHA20"
# define SSL_TXT_GOST            "GOST89"
# define SSL_TXT_ARIA            "ARIA"
# define SSL_TXT_ARIA_GCM        "ARIAGCM"
# define SSL_TXT_ARIA128         "ARIA128"
# define SSL_TXT_ARIA256         "ARIA256"

# define SSL_TXT_MD5             "MD5"
# define SSL_TXT_SHA1            "SHA1"
# define SSL_TXT_SHA             "SHA"/* same as "SHA1" */
# define SSL_TXT_GOST94          "GOST94"
# define SSL_TXT_GOST89MAC       "GOST89MAC"
# define SSL_TXT_GOST12          "GOST12"
# define SSL_TXT_GOST89MAC12     "GOST89MAC12"
# define SSL_TXT_SHA256          "SHA256"
# define SSL_TXT_SHA384          "SHA384"

# define SSL_TXT_SSLV3           "SSLv3"
# define SSL_TXT_TLSV1           "TLSv1"
# define SSL_TXT_TLSV1_1         "TLSv1.1"
# define SSL_TXT_TLSV1_2         "TLSv1.2"

# define SSL_TXT_ALL             "ALL"

/*-
 * COMPLEMENTOF* definitions. These identifiers are used to (de-select)
 * ciphers normally not being used.
 * Example: "RC4" will activate all ciphers using RC4 including ciphers
 * without authentication, which would normally disabled by DEFAULT (due
 * the "!ADH" being part of default). Therefore "RC4:!COMPLEMENTOFDEFAULT"
 * will make sure that it is also disabled in the specific selection.
 * COMPLEMENTOF* identifiers are portable between version, as adjustments
 * to the default cipher setup will also be included here.
 *
 * COMPLEMENTOFDEFAULT does not experience the same special treatment that
 * DEFAULT gets, as only selection is being done and no sorting as needed
 * for DEFAULT.
 */
# define SSL_TXT_CMPALL          "COMPLEMENTOFALL"
# define SSL_TXT_CMPDEF          "COMPLEMENTOFDEFAULT"

/*
 * The following cipher list is used by default. It also is substituted when
 * an application-defined cipher list string starts with 'DEFAULT'.
 * This applies to ciphersuites for TLSv1.2 and below.
 */
# define SSL_DEFAULT_CIPHER_LIST "ALL:!COMPLEMENTOFDEFAULT:!eNULL"
/* This is the default set of TLSv1.3 ciphersuites */
# if !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)
#  define TLS_DEFAULT_CIPHERSUITES "TLS_AES_256_GCM_SHA384:" \
                                   "TLS_CHACHA20_POLY1305_SHA256:" \
                                   "TLS_AES_128_GCM_SHA256"
# else
#  define TLS_DEFAULT_CIPHERSUITES "TLS_AES_256_GCM_SHA384:" \
                                   "TLS_AES_128_GCM_SHA256"
#endif
/*
 * As of OpenSSL 1.0.0, ssl_create_cipher_list() in ssl/ssl_ciph.c always
 * starts with a reasonable order, and all we have to do for DEFAULT is
 * throwing out anonymous and unencrypted ciphersuites! (The latter are not
 * actually enabled by ALL, but "ALL:RSA" would enable some of them.)
 */

/* Used in SSL_set_shutdown()/SSL_get_shutdown(); */
# define SSL_SENT_SHUTDOWN       1
# define SSL_RECEIVED_SHUTDOWN   2

#ifdef __cplusplus
}
#endif

#ifdef  __cplusplus
extern "C" {
#endif

# define SSL_FILETYPE_ASN1       X509_FILETYPE_ASN1
# define SSL_FILETYPE_PEM        X509_FILETYPE_PEM

/*
 * This is needed to stop compilers complaining about the 'struct ssl_st *'
 * function parameters used to prototype callbacks in SSL_CTX.
 */
typedef struct ssl_st *ssl_crock_st;
typedef struct tls_session_ticket_ext_st TLS_SESSION_TICKET_EXT;
typedef struct ssl_method_st SSL_METHOD;
typedef struct ssl_cipher_st SSL_CIPHER;
typedef struct ssl_session_st SSL_SESSION;
typedef struct tls_sigalgs_st TLS_SIGALGS;
typedef struct ssl_conf_ctx_st SSL_CONF_CTX;
typedef struct ssl_comp_st SSL_COMP;

STACK_OF(SSL_CIPHER);
STACK_OF(SSL_COMP);

/* SRTP protection profiles for use with the use_srtp extension (RFC 5764)*/
typedef struct srtp_protection_profile_st {
    const char *name;
    unsigned long id;
} SRTP_PROTECTION_PROFILE;

DEFINE_STACK_OF(SRTP_PROTECTION_PROFILE)

typedef int (*tls_session_ticket_ext_cb_fn)(SSL *s, const unsigned char *data,
                                            int len, void *arg);
typedef int (*tls_session_secret_cb_fn)(SSL *s, void *secret, int *secret_len,
                                        STACK_OF(SSL_CIPHER) *peer_ciphers,
                                        const SSL_CIPHER **cipher, void *arg);

/* Extension context codes */
/* This extension is only allowed in TLS */
#define SSL_EXT_TLS_ONLY                        0x0001
/* This extension is only allowed in DTLS */
#define SSL_EXT_DTLS_ONLY                       0x0002
/* Some extensions may be allowed in DTLS but we don't implement them for it */
#define SSL_EXT_TLS_IMPLEMENTATION_ONLY         0x0004
/* Most extensions are not defined for SSLv3 but EXT_TYPE_renegotiate is */
#define SSL_EXT_SSL3_ALLOWED                    0x0008
/* Extension is only defined for TLS1.2 and below */
#define SSL_EXT_TLS1_2_AND_BELOW_ONLY           0x0010
/* Extension is only defined for TLS1.3 and above */
#define SSL_EXT_TLS1_3_ONLY                     0x0020
/* Ignore this extension during parsing if we are resuming */
#define SSL_EXT_IGNORE_ON_RESUMPTION            0x0040
#define SSL_EXT_CLIENT_HELLO                    0x0080
/* Really means TLS1.2 or below */
#define SSL_EXT_TLS1_2_SERVER_HELLO             0x0100
#define SSL_EXT_TLS1_3_SERVER_HELLO             0x0200
#define SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS     0x0400
#define SSL_EXT_TLS1_3_HELLO_RETRY_REQUEST      0x0800
#define SSL_EXT_TLS1_3_CERTIFICATE              0x1000
#define SSL_EXT_TLS1_3_NEW_SESSION_TICKET       0x2000
#define SSL_EXT_TLS1_3_CERTIFICATE_REQUEST      0x4000

/* Typedefs for handling custom extensions */

typedef int (*custom_ext_add_cb)(SSL *s, unsigned int ext_type,
                                 const unsigned char **out, size_t *outlen,
                                 int *al, void *add_arg);

typedef void (*custom_ext_free_cb)(SSL *s, unsigned int ext_type,
                                   const unsigned char *out, void *add_arg);

typedef int (*custom_ext_parse_cb)(SSL *s, unsigned int ext_type,
                                   const unsigned char *in, size_t inlen,
                                   int *al, void *parse_arg);


typedef int (*SSL_custom_ext_add_cb_ex)(SSL *s, unsigned int ext_type,
                                        unsigned int context,
                                        const unsigned char **out,
                                        size_t *outlen, X509 *x,
                                        size_t chainidx,
                                        int *al, void *add_arg);

typedef void (*SSL_custom_ext_free_cb_ex)(SSL *s, unsigned int ext_type,
                                          unsigned int context,
                                          const unsigned char *out,
                                          void *add_arg);

typedef int (*SSL_custom_ext_parse_cb_ex)(SSL *s, unsigned int ext_type,
                                          unsigned int context,
                                          const unsigned char *in,
                                          size_t inlen, X509 *x,
                                          size_t chainidx,
                                          int *al, void *parse_arg);

/* Typedef for verification callback */
typedef int (*SSL_verify_cb)(int preverify_ok, X509_STORE_CTX *x509_ctx);

/*
 * Some values are reserved until OpenSSL 1.2.0 because they were previously
 * included in SSL_OP_ALL in a 1.1.x release.
 *
 * Reserved value (until OpenSSL 1.2.0)                  0x00000001U
 * Reserved value (until OpenSSL 1.2.0)                  0x00000002U
 */
/* Allow initial connection to servers that don't support RI */
# define SSL_OP_LEGACY_SERVER_CONNECT                    0x00000004U

/* Reserved value (until OpenSSL 1.2.0)                  0x00000008U */
# define SSL_OP_TLSEXT_PADDING                           0x00000010U
/* Reserved value (until OpenSSL 1.2.0)                  0x00000020U */
# define SSL_OP_SAFARI_ECDHE_ECDSA_BUG                   0x00000040U
/*
 * Reserved value (until OpenSSL 1.2.0)                  0x00000080U
 * Reserved value (until OpenSSL 1.2.0)                  0x00000100U
 * Reserved value (until OpenSSL 1.2.0)                  0x00000200U
 */

/* In TLSv1.3 allow a non-(ec)dhe based kex_mode */
# define SSL_OP_ALLOW_NO_DHE_KEX                         0x00000400U

/*
 * Disable SSL 3.0/TLS 1.0 CBC vulnerability workaround that was added in
 * OpenSSL 0.9.6d.  Usually (depending on the application protocol) the
 * workaround is not needed.  Unfortunately some broken SSL/TLS
 * implementations cannot handle it at all, which is why we include it in
 * SSL_OP_ALL. Added in 0.9.6e
 */
# define SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS              0x00000800U

/* DTLS options */
# define SSL_OP_NO_QUERY_MTU                             0x00001000U
/* Turn on Cookie Exchange (on relevant for servers) */
# define SSL_OP_COOKIE_EXCHANGE                          0x00002000U
/* Don't use RFC4507 ticket extension */
# define SSL_OP_NO_TICKET                                0x00004000U
# ifndef OPENSSL_NO_DTLS1_METHOD
/* Use Cisco's "speshul" version of DTLS_BAD_VER
 * (only with deprecated DTLSv1_client_method())  */
#  define SSL_OP_CISCO_ANYCONNECT                        0x00008000U
# endif

/* As server, disallow session resumption on renegotiation */
# define SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION   0x00010000U
/* Don't use compression even if supported */
# define SSL_OP_NO_COMPRESSION                           0x00020000U
/* Permit unsafe legacy renegotiation */
# define SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION        0x00040000U
/* Disable encrypt-then-mac */
# define SSL_OP_NO_ENCRYPT_THEN_MAC                      0x00080000U

/*
 * Enable TLSv1.3 Compatibility mode. This is on by default. A future version
 * of OpenSSL may have this disabled by default.
 */
# define SSL_OP_ENABLE_MIDDLEBOX_COMPAT                  0x00100000U

/* Prioritize Chacha20Poly1305 when client does.
 * Modifies SSL_OP_CIPHER_SERVER_PREFERENCE */
# define SSL_OP_PRIORITIZE_CHACHA                        0x00200000U

/*
 * Set on servers to choose the cipher according to the server's preferences
 */
# define SSL_OP_CIPHER_SERVER_PREFERENCE                 0x00400000U
/*
 * If set, a server will allow a client to issue a SSLv3.0 version number as
 * latest version supported in the premaster secret, even when TLSv1.0
 * (version 3.1) was announced in the client hello. Normally this is
 * forbidden to prevent version rollback attacks.
 */
# define SSL_OP_TLS_ROLLBACK_BUG                         0x00800000U

/*
 * Switches off automatic TLSv1.3 anti-replay protection for early data. This
 * is a server-side option only (no effect on the client).
 */
# define SSL_OP_NO_ANTI_REPLAY                           0x01000000U

# define SSL_OP_NO_SSLv3                                 0x02000000U
# define SSL_OP_NO_TLSv1                                 0x04000000U
# define SSL_OP_NO_TLSv1_2                               0x08000000U
# define SSL_OP_NO_TLSv1_1                               0x10000000U
# define SSL_OP_NO_TLSv1_3                               0x20000000U

# define SSL_OP_NO_DTLSv1                                0x04000000U
# define SSL_OP_NO_DTLSv1_2                              0x08000000U

# define SSL_OP_NO_SSL_MASK (SSL_OP_NO_SSLv3|\
        SSL_OP_NO_TLSv1|SSL_OP_NO_TLSv1_1|SSL_OP_NO_TLSv1_2|SSL_OP_NO_TLSv1_3)
# define SSL_OP_NO_DTLS_MASK (SSL_OP_NO_DTLSv1|SSL_OP_NO_DTLSv1_2)

/* Disallow all renegotiation */
# define SSL_OP_NO_RENEGOTIATION                         0x40000000U

/*
 * Make server add server-hello extension from early version of cryptopro
 * draft, when GOST ciphersuite is negotiated. Required for interoperability
 * with CryptoPro CSP 3.x
 */
# define SSL_OP_CRYPTOPRO_TLSEXT_BUG                     0x80000000U

/*
 * SSL_OP_ALL: various bug workarounds that should be rather harmless.
 * This used to be 0x000FFFFFL before 0.9.7.
 * This used to be 0x80000BFFU before 1.1.1.
 */
# define SSL_OP_ALL        (SSL_OP_CRYPTOPRO_TLSEXT_BUG|\
                            SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS|\
                            SSL_OP_LEGACY_SERVER_CONNECT|\
                            SSL_OP_TLSEXT_PADDING|\
                            SSL_OP_SAFARI_ECDHE_ECDSA_BUG)

/* OBSOLETE OPTIONS: retained for compatibility */

/* Removed from OpenSSL 1.1.0. Was 0x00000001L */
/* Related to removed SSLv2. */
# define SSL_OP_MICROSOFT_SESS_ID_BUG                    0x0
/* Removed from OpenSSL 1.1.0. Was 0x00000002L */
/* Related to removed SSLv2. */
# define SSL_OP_NETSCAPE_CHALLENGE_BUG                   0x0
/* Removed from OpenSSL 0.9.8q and 1.0.0c. Was 0x00000008L */
/* Dead forever, see CVE-2010-4180 */
# define SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG         0x0
/* Removed from OpenSSL 1.0.1h and 1.0.2. Was 0x00000010L */
/* Refers to ancient SSLREF and SSLv2. */
# define SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG              0x0
/* Removed from OpenSSL 1.1.0. Was 0x00000020 */
# define SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER               0x0
/* Removed from OpenSSL 0.9.7h and 0.9.8b. Was 0x00000040L */
# define SSL_OP_MSIE_SSLV2_RSA_PADDING                   0x0
/* Removed from OpenSSL 1.1.0. Was 0x00000080 */
/* Ancient SSLeay version. */
# define SSL_OP_SSLEAY_080_CLIENT_DH_BUG                 0x0
/* Removed from OpenSSL 1.1.0. Was 0x00000100L */
# define SSL_OP_TLS_D5_BUG                               0x0
/* Removed from OpenSSL 1.1.0. Was 0x00000200L */
# define SSL_OP_TLS_BLOCK_PADDING_BUG                    0x0
/* Removed from OpenSSL 1.1.0. Was 0x00080000L */
# define SSL_OP_SINGLE_ECDH_USE                          0x0
/* Removed from OpenSSL 1.1.0. Was 0x00100000L */
# define SSL_OP_SINGLE_DH_USE                            0x0
/* Removed from OpenSSL 1.0.1k and 1.0.2. Was 0x00200000L */
# define SSL_OP_EPHEMERAL_RSA                            0x0
/* Removed from OpenSSL 1.1.0. Was 0x01000000L */
# define SSL_OP_NO_SSLv2                                 0x0
/* Removed from OpenSSL 1.0.1. Was 0x08000000L */
# define SSL_OP_PKCS1_CHECK_1                            0x0
/* Removed from OpenSSL 1.0.1. Was 0x10000000L */
# define SSL_OP_PKCS1_CHECK_2                            0x0
/* Removed from OpenSSL 1.1.0. Was 0x20000000L */
# define SSL_OP_NETSCAPE_CA_DN_BUG                       0x0
/* Removed from OpenSSL 1.1.0. Was 0x40000000L */
# define SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG          0x0

/*
 * Allow SSL_write(..., n) to return r with 0 < r < n (i.e. report success
 * when just a single record has been written):
 */
# define SSL_MODE_ENABLE_PARTIAL_WRITE       0x00000001U
/*
 * Make it possible to retry SSL_write() with changed buffer location (buffer
 * contents must stay the same!); this is not the default to avoid the
 * misconception that non-blocking SSL_write() behaves like non-blocking
 * write():
 */
# define SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER 0x00000002U
/*
 * Never bother the application with retries if the transport is blocking:
 */
# define SSL_MODE_AUTO_RETRY 0x00000004U
/* Don't attempt to automatically build certificate chain */
# define SSL_MODE_NO_AUTO_CHAIN 0x00000008U
/*
 * Save RAM by releasing read and write buffers when they're empty. (SSL3 and
 * TLS only.) Released buffers are freed.
 */
# define SSL_MODE_RELEASE_BUFFERS 0x00000010U
/*
 * Send the current time in the Random fields of the ClientHello and
 * ServerHello records for compatibility with hypothetical implementations
 * that require it.
 */
# define SSL_MODE_SEND_CLIENTHELLO_TIME 0x00000020U
# define SSL_MODE_SEND_SERVERHELLO_TIME 0x00000040U
/*
 * Send TLS_FALLBACK_SCSV in the ClientHello. To be set only by applications
 * that reconnect with a downgraded protocol version; see
 * draft-ietf-tls-downgrade-scsv-00 for details. DO NOT ENABLE THIS if your
 * application attempts a normal handshake. Only use this in explicit
 * fallback retries, following the guidance in
 * draft-ietf-tls-downgrade-scsv-00.
 */
# define SSL_MODE_SEND_FALLBACK_SCSV 0x00000080U
/*
 * Support Asynchronous operation
 */
# define SSL_MODE_ASYNC 0x00000100U

/*
 * When using DTLS/SCTP, include the terminating zero in the label
 * used for computing the endpoint-pair shared secret. Required for
 * interoperability with implementations having this bug like these
 * older version of OpenSSL:
 * - OpenSSL 1.0.0 series
 * - OpenSSL 1.0.1 series
 * - OpenSSL 1.0.2 series
 * - OpenSSL 1.1.0 series
 * - OpenSSL 1.1.1 and 1.1.1a
 */
# define SSL_MODE_DTLS_SCTP_LABEL_LENGTH_BUG 0x00000400U

/* Cert related flags */
/*
 * Many implementations ignore some aspects of the TLS standards such as
 * enforcing certificate chain algorithms. When this is set we enforce them.
 */
# define SSL_CERT_FLAG_TLS_STRICT                0x00000001U

/* Suite B modes, takes same values as certificate verify flags */
# define SSL_CERT_FLAG_SUITEB_128_LOS_ONLY       0x10000
/* Suite B 192 bit only mode */
# define SSL_CERT_FLAG_SUITEB_192_LOS            0x20000
/* Suite B 128 bit mode allowing 192 bit algorithms */
# define SSL_CERT_FLAG_SUITEB_128_LOS            0x30000

/* Perform all sorts of protocol violations for testing purposes */
# define SSL_CERT_FLAG_BROKEN_PROTOCOL           0x10000000

/* Flags for building certificate chains */
/* Treat any existing certificates as untrusted CAs */
# define SSL_BUILD_CHAIN_FLAG_UNTRUSTED          0x1
/* Don't include root CA in chain */
# define SSL_BUILD_CHAIN_FLAG_NO_ROOT            0x2
/* Just check certificates already there */
# define SSL_BUILD_CHAIN_FLAG_CHECK              0x4
/* Ignore verification errors */
# define SSL_BUILD_CHAIN_FLAG_IGNORE_ERROR       0x8
/* Clear verification errors from queue */
# define SSL_BUILD_CHAIN_FLAG_CLEAR_ERROR        0x10

/* Flags returned by SSL_check_chain */
/* Certificate can be used with this session */
# define CERT_PKEY_VALID         0x1
/* Certificate can also be used for signing */
# define CERT_PKEY_SIGN          0x2
/* EE certificate signing algorithm OK */
# define CERT_PKEY_EE_SIGNATURE  0x10
/* CA signature algorithms OK */
# define CERT_PKEY_CA_SIGNATURE  0x20
/* EE certificate parameters OK */
# define CERT_PKEY_EE_PARAM      0x40
/* CA certificate parameters OK */
# define CERT_PKEY_CA_PARAM      0x80
/* Signing explicitly allowed as opposed to SHA1 fallback */
# define CERT_PKEY_EXPLICIT_SIGN 0x100
/* Client CA issuer names match (always set for server cert) */
# define CERT_PKEY_ISSUER_NAME   0x200
/* Cert type matches client types (always set for server cert) */
# define CERT_PKEY_CERT_TYPE     0x400
/* Cert chain suitable to Suite B */
# define CERT_PKEY_SUITEB        0x800

# define SSL_CONF_FLAG_CMDLINE           0x1
# define SSL_CONF_FLAG_FILE              0x2
# define SSL_CONF_FLAG_CLIENT            0x4
# define SSL_CONF_FLAG_SERVER            0x8
# define SSL_CONF_FLAG_SHOW_ERRORS       0x10
# define SSL_CONF_FLAG_CERTIFICATE       0x20
# define SSL_CONF_FLAG_REQUIRE_PRIVATE   0x40
/* Configuration value types */
# define SSL_CONF_TYPE_UNKNOWN           0x0
# define SSL_CONF_TYPE_STRING            0x1
# define SSL_CONF_TYPE_FILE              0x2
# define SSL_CONF_TYPE_DIR               0x3
# define SSL_CONF_TYPE_NONE              0x4

/* Maximum length of the application-controlled segment of a a TLSv1.3 cookie */
# define SSL_COOKIE_LENGTH                       4096

/*
 * Note: SSL[_CTX]_set_{options,mode} use |= op on the previous value, they
 * cannot be used to clear bits.
 */

unsigned long SSL_CTX_get_options(const SSL_CTX *ctx);
unsigned long SSL_get_options(const SSL *s);
unsigned long SSL_CTX_clear_options(SSL_CTX *ctx, unsigned long op);
unsigned long SSL_clear_options(SSL *s, unsigned long op);
unsigned long SSL_CTX_set_options(SSL_CTX *ctx, unsigned long op);
unsigned long SSL_set_options(SSL *s, unsigned long op);

# define SSL_CTX_set_mode(ctx,op) \
        SSL_CTX_ctrl((ctx),SSL_CTRL_MODE,(op),NULL)
# define SSL_CTX_clear_mode(ctx,op) \
        SSL_CTX_ctrl((ctx),SSL_CTRL_CLEAR_MODE,(op),NULL)
# define SSL_CTX_get_mode(ctx) \
        SSL_CTX_ctrl((ctx),SSL_CTRL_MODE,0,NULL)
# define SSL_clear_mode(ssl,op) \
        SSL_ctrl((ssl),SSL_CTRL_CLEAR_MODE,(op),NULL)
# define SSL_set_mode(ssl,op) \
        SSL_ctrl((ssl),SSL_CTRL_MODE,(op),NULL)
# define SSL_get_mode(ssl) \
        SSL_ctrl((ssl),SSL_CTRL_MODE,0,NULL)
# define SSL_set_mtu(ssl, mtu) \
        SSL_ctrl((ssl),SSL_CTRL_SET_MTU,(mtu),NULL)
# define DTLS_set_link_mtu(ssl, mtu) \
        SSL_ctrl((ssl),DTLS_CTRL_SET_LINK_MTU,(mtu),NULL)
# define DTLS_get_link_min_mtu(ssl) \
        SSL_ctrl((ssl),DTLS_CTRL_GET_LINK_MIN_MTU,0,NULL)

# define SSL_get_secure_renegotiation_support(ssl) \
        SSL_ctrl((ssl), SSL_CTRL_GET_RI_SUPPORT, 0, NULL)

# ifndef OPENSSL_NO_HEARTBEATS
#  define SSL_heartbeat(ssl) \
        SSL_ctrl((ssl),SSL_CTRL_DTLS_EXT_SEND_HEARTBEAT,0,NULL)
# endif

# define SSL_CTX_set_cert_flags(ctx,op) \
        SSL_CTX_ctrl((ctx),SSL_CTRL_CERT_FLAGS,(op),NULL)
# define SSL_set_cert_flags(s,op) \
        SSL_ctrl((s),SSL_CTRL_CERT_FLAGS,(op),NULL)
# define SSL_CTX_clear_cert_flags(ctx,op) \
        SSL_CTX_ctrl((ctx),SSL_CTRL_CLEAR_CERT_FLAGS,(op),NULL)
# define SSL_clear_cert_flags(s,op) \
        SSL_ctrl((s),SSL_CTRL_CLEAR_CERT_FLAGS,(op),NULL)

void SSL_CTX_set_msg_callback(SSL_CTX *ctx,
                              void (*cb) (int write_p, int version,
                                          int content_type, const void *buf,
                                          size_t len, SSL *ssl, void *arg));
void SSL_set_msg_callback(SSL *ssl,
                          void (*cb) (int write_p, int version,
                                      int content_type, const void *buf,
                                      size_t len, SSL *ssl, void *arg));
# define SSL_CTX_set_msg_callback_arg(ctx, arg) SSL_CTX_ctrl((ctx), SSL_CTRL_SET_MSG_CALLBACK_ARG, 0, (arg))
# define SSL_set_msg_callback_arg(ssl, arg) SSL_ctrl((ssl), SSL_CTRL_SET_MSG_CALLBACK_ARG, 0, (arg))

# define SSL_get_extms_support(s) \
        SSL_ctrl((s),SSL_CTRL_GET_EXTMS_SUPPORT,0,NULL)

# ifndef OPENSSL_NO_SRP

/* see tls_srp.c */
__owur int SSL_SRP_CTX_init(SSL *s);
__owur int SSL_CTX_SRP_CTX_init(SSL_CTX *ctx);
int SSL_SRP_CTX_free(SSL *ctx);
int SSL_CTX_SRP_CTX_free(SSL_CTX *ctx);
__owur int SSL_srp_server_param_with_username(SSL *s, int *ad);
__owur int SRP_Calc_A_param(SSL *s);

# endif

/* 100k max cert list */
# define SSL_MAX_CERT_LIST_DEFAULT 1024*100

# define SSL_SESSION_CACHE_MAX_SIZE_DEFAULT      (1024*20)

/*
 * This callback type is used inside SSL_CTX, SSL, and in the functions that
 * set them. It is used to override the generation of SSL/TLS session IDs in
 * a server. Return value should be zero on an error, non-zero to proceed.
 * Also, callbacks should themselves check if the id they generate is unique
 * otherwise the SSL handshake will fail with an error - callbacks can do
 * this using the 'ssl' value they're passed by;
 * SSL_has_matching_session_id(ssl, id, *id_len) The length value passed in
 * is set at the maximum size the session ID can be. In SSLv3/TLSv1 it is 32
 * bytes. The callback can alter this length to be less if desired. It is
 * also an error for the callback to set the size to zero.
 */
typedef int (*GEN_SESSION_CB) (SSL *ssl, unsigned char *id,
                               unsigned int *id_len);

# define SSL_SESS_CACHE_OFF                      0x0000
# define SSL_SESS_CACHE_CLIENT                   0x0001
# define SSL_SESS_CACHE_SERVER                   0x0002
# define SSL_SESS_CACHE_BOTH     (SSL_SESS_CACHE_CLIENT|SSL_SESS_CACHE_SERVER)
# define SSL_SESS_CACHE_NO_AUTO_CLEAR            0x0080
/* enough comments already ... see SSL_CTX_set_session_cache_mode(3) */
# define SSL_SESS_CACHE_NO_INTERNAL_LOOKUP       0x0100
# define SSL_SESS_CACHE_NO_INTERNAL_STORE        0x0200
# define SSL_SESS_CACHE_NO_INTERNAL \
        (SSL_SESS_CACHE_NO_INTERNAL_LOOKUP|SSL_SESS_CACHE_NO_INTERNAL_STORE)

LHASH_OF(SSL_SESSION) *SSL_CTX_sessions(SSL_CTX *ctx);
# define SSL_CTX_sess_number(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_NUMBER,0,NULL)
# define SSL_CTX_sess_connect(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_CONNECT,0,NULL)
# define SSL_CTX_sess_connect_good(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_CONNECT_GOOD,0,NULL)
# define SSL_CTX_sess_connect_renegotiate(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_CONNECT_RENEGOTIATE,0,NULL)
# define SSL_CTX_sess_accept(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_ACCEPT,0,NULL)
# define SSL_CTX_sess_accept_renegotiate(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_ACCEPT_RENEGOTIATE,0,NULL)
# define SSL_CTX_sess_accept_good(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_ACCEPT_GOOD,0,NULL)
# define SSL_CTX_sess_hits(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_HIT,0,NULL)
# define SSL_CTX_sess_cb_hits(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_CB_HIT,0,NULL)
# define SSL_CTX_sess_misses(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_MISSES,0,NULL)
# define SSL_CTX_sess_timeouts(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_TIMEOUTS,0,NULL)
# define SSL_CTX_sess_cache_full(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SESS_CACHE_FULL,0,NULL)

void SSL_CTX_sess_set_new_cb(SSL_CTX *ctx,
                             int (*new_session_cb) (struct ssl_st *ssl,
                                                    SSL_SESSION *sess));
int (*SSL_CTX_sess_get_new_cb(SSL_CTX *ctx)) (struct ssl_st *ssl,
                                              SSL_SESSION *sess);
void SSL_CTX_sess_set_remove_cb(SSL_CTX *ctx,
                                void (*remove_session_cb) (struct ssl_ctx_st
                                                           *ctx,
                                                           SSL_SESSION *sess));
void (*SSL_CTX_sess_get_remove_cb(SSL_CTX *ctx)) (struct ssl_ctx_st *ctx,
                                                  SSL_SESSION *sess);
void SSL_CTX_sess_set_get_cb(SSL_CTX *ctx,
                             SSL_SESSION *(*get_session_cb) (struct ssl_st
                                                             *ssl,
                                                             const unsigned char
                                                             *data, int len,
                                                             int *copy));
SSL_SESSION *(*SSL_CTX_sess_get_get_cb(SSL_CTX *ctx)) (struct ssl_st *ssl,
                                                       const unsigned char *data,
                                                       int len, int *copy);
void SSL_CTX_set_info_callback(SSL_CTX *ctx,
                               void (*cb) (const SSL *ssl, int type, int val));
void (*SSL_CTX_get_info_callback(SSL_CTX *ctx)) (const SSL *ssl, int type,
                                                 int val);
void SSL_CTX_set_client_cert_cb(SSL_CTX *ctx,
                                int (*client_cert_cb) (SSL *ssl, X509 **x509,
                                                       EVP_PKEY **pkey));
int (*SSL_CTX_get_client_cert_cb(SSL_CTX *ctx)) (SSL *ssl, X509 **x509,
                                                 EVP_PKEY **pkey);
# ifndef OPENSSL_NO_ENGINE
__owur int SSL_CTX_set_client_cert_engine(SSL_CTX *ctx, ENGINE *e);
# endif
void SSL_CTX_set_cookie_generate_cb(SSL_CTX *ctx,
                                    int (*app_gen_cookie_cb) (SSL *ssl,
                                                              unsigned char
                                                              *cookie,
                                                              unsigned int
                                                              *cookie_len));
void SSL_CTX_set_cookie_verify_cb(SSL_CTX *ctx,
                                  int (*app_verify_cookie_cb) (SSL *ssl,
                                                               const unsigned
                                                               char *cookie,
                                                               unsigned int
                                                               cookie_len));

void SSL_CTX_set_stateless_cookie_generate_cb(
    SSL_CTX *ctx,
    int (*gen_stateless_cookie_cb) (SSL *ssl,
                                    unsigned char *cookie,
                                    size_t *cookie_len));
void SSL_CTX_set_stateless_cookie_verify_cb(
    SSL_CTX *ctx,
    int (*verify_stateless_cookie_cb) (SSL *ssl,
                                       const unsigned char *cookie,
                                       size_t cookie_len));
# ifndef OPENSSL_NO_NEXTPROTONEG

typedef int (*SSL_CTX_npn_advertised_cb_func)(SSL *ssl,
                                              const unsigned char **out,
                                              unsigned int *outlen,
                                              void *arg);
void SSL_CTX_set_next_protos_advertised_cb(SSL_CTX *s,
                                           SSL_CTX_npn_advertised_cb_func cb,
                                           void *arg);
#  define SSL_CTX_set_npn_advertised_cb SSL_CTX_set_next_protos_advertised_cb

typedef int (*SSL_CTX_npn_select_cb_func)(SSL *s,
                                          unsigned char **out,
                                          unsigned char *outlen,
                                          const unsigned char *in,
                                          unsigned int inlen,
                                          void *arg);
void SSL_CTX_set_next_proto_select_cb(SSL_CTX *s,
                                      SSL_CTX_npn_select_cb_func cb,
                                      void *arg);
#  define SSL_CTX_set_npn_select_cb SSL_CTX_set_next_proto_select_cb

void SSL_get0_next_proto_negotiated(const SSL *s, const unsigned char **data,
                                    unsigned *len);
#  define SSL_get0_npn_negotiated SSL_get0_next_proto_negotiated
# endif

__owur int SSL_select_next_proto(unsigned char **out, unsigned char *outlen,
                                 const unsigned char *in, unsigned int inlen,
                                 const unsigned char *client,
                                 unsigned int client_len);

# define OPENSSL_NPN_UNSUPPORTED 0
# define OPENSSL_NPN_NEGOTIATED  1
# define OPENSSL_NPN_NO_OVERLAP  2

__owur int SSL_CTX_set_alpn_protos(SSL_CTX *ctx, const unsigned char *protos,
                                   unsigned int protos_len);
__owur int SSL_set_alpn_protos(SSL *ssl, const unsigned char *protos,
                               unsigned int protos_len);
typedef int (*SSL_CTX_alpn_select_cb_func)(SSL *ssl,
                                           const unsigned char **out,
                                           unsigned char *outlen,
                                           const unsigned char *in,
                                           unsigned int inlen,
                                           void *arg);
void SSL_CTX_set_alpn_select_cb(SSL_CTX *ctx,
                                SSL_CTX_alpn_select_cb_func cb,
                                void *arg);
void SSL_get0_alpn_selected(const SSL *ssl, const unsigned char **data,
                            unsigned int *len);

# ifndef OPENSSL_NO_PSK
/*
 * the maximum length of the buffer given to callbacks containing the
 * resulting identity/psk
 */
#  define PSK_MAX_IDENTITY_LEN 128
#  define PSK_MAX_PSK_LEN 256
typedef unsigned int (*SSL_psk_client_cb_func)(SSL *ssl,
                                               const char *hint,
                                               char *identity,
                                               unsigned int max_identity_len,
                                               unsigned char *psk,
                                               unsigned int max_psk_len);
void SSL_CTX_set_psk_client_callback(SSL_CTX *ctx, SSL_psk_client_cb_func cb);
void SSL_set_psk_client_callback(SSL *ssl, SSL_psk_client_cb_func cb);

typedef unsigned int (*SSL_psk_server_cb_func)(SSL *ssl,
                                               const char *identity,
                                               unsigned char *psk,
                                               unsigned int max_psk_len);
void SSL_CTX_set_psk_server_callback(SSL_CTX *ctx, SSL_psk_server_cb_func cb);
void SSL_set_psk_server_callback(SSL *ssl, SSL_psk_server_cb_func cb);

__owur int SSL_CTX_use_psk_identity_hint(SSL_CTX *ctx, const char *identity_hint);
__owur int SSL_use_psk_identity_hint(SSL *s, const char *identity_hint);
const char *SSL_get_psk_identity_hint(const SSL *s);
const char *SSL_get_psk_identity(const SSL *s);
# endif

typedef int (*SSL_psk_find_session_cb_func)(SSL *ssl,
                                            const unsigned char *identity,
                                            size_t identity_len,
                                            SSL_SESSION **sess);
typedef int (*SSL_psk_use_session_cb_func)(SSL *ssl, const EVP_MD *md,
                                           const unsigned char **id,
                                           size_t *idlen,
                                           SSL_SESSION **sess);

void SSL_set_psk_find_session_callback(SSL *s, SSL_psk_find_session_cb_func cb);
void SSL_CTX_set_psk_find_session_callback(SSL_CTX *ctx,
                                           SSL_psk_find_session_cb_func cb);
void SSL_set_psk_use_session_callback(SSL *s, SSL_psk_use_session_cb_func cb);
void SSL_CTX_set_psk_use_session_callback(SSL_CTX *ctx,
                                          SSL_psk_use_session_cb_func cb);

/* Register callbacks to handle custom TLS Extensions for client or server. */

__owur int SSL_CTX_has_client_custom_ext(const SSL_CTX *ctx,
                                         unsigned int ext_type);

__owur int SSL_CTX_add_client_custom_ext(SSL_CTX *ctx,
                                         unsigned int ext_type,
                                         custom_ext_add_cb add_cb,
                                         custom_ext_free_cb free_cb,
                                         void *add_arg,
                                         custom_ext_parse_cb parse_cb,
                                         void *parse_arg);

__owur int SSL_CTX_add_server_custom_ext(SSL_CTX *ctx,
                                         unsigned int ext_type,
                                         custom_ext_add_cb add_cb,
                                         custom_ext_free_cb free_cb,
                                         void *add_arg,
                                         custom_ext_parse_cb parse_cb,
                                         void *parse_arg);

__owur int SSL_CTX_add_custom_ext(SSL_CTX *ctx, unsigned int ext_type,
                                  unsigned int context,
                                  SSL_custom_ext_add_cb_ex add_cb,
                                  SSL_custom_ext_free_cb_ex free_cb,
                                  void *add_arg,
                                  SSL_custom_ext_parse_cb_ex parse_cb,
                                  void *parse_arg);

__owur int SSL_extension_supported(unsigned int ext_type);

# define SSL_NOTHING            1
# define SSL_WRITING            2
# define SSL_READING            3
# define SSL_X509_LOOKUP        4
# define SSL_ASYNC_PAUSED       5
# define SSL_ASYNC_NO_JOBS      6
# define SSL_CLIENT_HELLO_CB    7

/* These will only be used when doing non-blocking IO */
# define SSL_want_nothing(s)         (SSL_want(s) == SSL_NOTHING)
# define SSL_want_read(s)            (SSL_want(s) == SSL_READING)
# define SSL_want_write(s)           (SSL_want(s) == SSL_WRITING)
# define SSL_want_x509_lookup(s)     (SSL_want(s) == SSL_X509_LOOKUP)
# define SSL_want_async(s)           (SSL_want(s) == SSL_ASYNC_PAUSED)
# define SSL_want_async_job(s)       (SSL_want(s) == SSL_ASYNC_NO_JOBS)
# define SSL_want_client_hello_cb(s) (SSL_want(s) == SSL_CLIENT_HELLO_CB)

# define SSL_MAC_FLAG_READ_MAC_STREAM 1
# define SSL_MAC_FLAG_WRITE_MAC_STREAM 2

/*
 * A callback for logging out TLS key material. This callback should log out
 * |line| followed by a newline.
 */
typedef void (*SSL_CTX_keylog_cb_func)(const SSL *ssl, const char *line);

/*
 * SSL_CTX_set_keylog_callback configures a callback to log key material. This
 * is intended for debugging use with tools like Wireshark. The cb function
 * should log line followed by a newline.
 */
void SSL_CTX_set_keylog_callback(SSL_CTX *ctx, SSL_CTX_keylog_cb_func cb);

/*
 * SSL_CTX_get_keylog_callback returns the callback configured by
 * SSL_CTX_set_keylog_callback.
 */
SSL_CTX_keylog_cb_func SSL_CTX_get_keylog_callback(const SSL_CTX *ctx);

int SSL_CTX_set_max_early_data(SSL_CTX *ctx, uint32_t max_early_data);
uint32_t SSL_CTX_get_max_early_data(const SSL_CTX *ctx);
int SSL_set_max_early_data(SSL *s, uint32_t max_early_data);
uint32_t SSL_get_max_early_data(const SSL *s);
int SSL_CTX_set_recv_max_early_data(SSL_CTX *ctx, uint32_t recv_max_early_data);
uint32_t SSL_CTX_get_recv_max_early_data(const SSL_CTX *ctx);
int SSL_set_recv_max_early_data(SSL *s, uint32_t recv_max_early_data);
uint32_t SSL_get_recv_max_early_data(const SSL *s);

#ifdef __cplusplus
}
#endif

# include <openssl/ssl2.h>
# include <openssl/ssl3.h>
# include <openssl/tls1.h>      /* This is mostly sslv3 with a few tweaks */
# include <openssl/dtls1.h>     /* Datagram TLS */
# include <openssl/srtp.h>      /* Support for the use_srtp extension */

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * These need to be after the above set of includes due to a compiler bug
 * in VisualStudio 2015
 */
DEFINE_STACK_OF_CONST(SSL_CIPHER)
DEFINE_STACK_OF(SSL_COMP)

/* compatibility */
# define SSL_set_app_data(s,arg)         (SSL_set_ex_data(s,0,(char *)(arg)))
# define SSL_get_app_data(s)             (SSL_get_ex_data(s,0))
# define SSL_SESSION_set_app_data(s,a)   (SSL_SESSION_set_ex_data(s,0, \
                                                                  (char *)(a)))
# define SSL_SESSION_get_app_data(s)     (SSL_SESSION_get_ex_data(s,0))
# define SSL_CTX_get_app_data(ctx)       (SSL_CTX_get_ex_data(ctx,0))
# define SSL_CTX_set_app_data(ctx,arg)   (SSL_CTX_set_ex_data(ctx,0, \
                                                              (char *)(arg)))
DEPRECATEDIN_1_1_0(void SSL_set_debug(SSL *s, int debug))

/* TLSv1.3 KeyUpdate message types */
/* -1 used so that this is an invalid value for the on-the-wire protocol */
#define SSL_KEY_UPDATE_NONE             -1
/* Values as defined for the on-the-wire protocol */
#define SSL_KEY_UPDATE_NOT_REQUESTED     0
#define SSL_KEY_UPDATE_REQUESTED         1

/*
 * The valid handshake states (one for each type message sent and one for each
 * type of message received). There are also two "special" states:
 * TLS = TLS or DTLS state
 * DTLS = DTLS specific state
 * CR/SR = Client Read/Server Read
 * CW/SW = Client Write/Server Write
 *
 * The "special" states are:
 * TLS_ST_BEFORE = No handshake has been initiated yet
 * TLS_ST_OK = A handshake has been successfully completed
 */
typedef enum {
    TLS_ST_BEFORE,
    TLS_ST_OK,
    DTLS_ST_CR_HELLO_VERIFY_REQUEST,
    TLS_ST_CR_SRVR_HELLO,
    TLS_ST_CR_CERT,
    TLS_ST_CR_CERT_STATUS,
    TLS_ST_CR_KEY_EXCH,
    TLS_ST_CR_CERT_REQ,
    TLS_ST_CR_SRVR_DONE,
    TLS_ST_CR_SESSION_TICKET,
    TLS_ST_CR_CHANGE,
    TLS_ST_CR_FINISHED,
    TLS_ST_CW_CLNT_HELLO,
    TLS_ST_CW_CERT,
    TLS_ST_CW_KEY_EXCH,
    TLS_ST_CW_CERT_VRFY,
    TLS_ST_CW_CHANGE,
    TLS_ST_CW_NEXT_PROTO,
    TLS_ST_CW_FINISHED,
    TLS_ST_SW_HELLO_REQ,
    TLS_ST_SR_CLNT_HELLO,
    DTLS_ST_SW_HELLO_VERIFY_REQUEST,
    TLS_ST_SW_SRVR_HELLO,
    TLS_ST_SW_CERT,
    TLS_ST_SW_KEY_EXCH,
    TLS_ST_SW_CERT_REQ,
    TLS_ST_SW_SRVR_DONE,
    TLS_ST_SR_CERT,
    TLS_ST_SR_KEY_EXCH,
    TLS_ST_SR_CERT_VRFY,
    TLS_ST_SR_NEXT_PROTO,
    TLS_ST_SR_CHANGE,
    TLS_ST_SR_FINISHED,
    TLS_ST_SW_SESSION_TICKET,
    TLS_ST_SW_CERT_STATUS,
    TLS_ST_SW_CHANGE,
    TLS_ST_SW_FINISHED,
    TLS_ST_SW_ENCRYPTED_EXTENSIONS,
    TLS_ST_CR_ENCRYPTED_EXTENSIONS,
    TLS_ST_CR_CERT_VRFY,
    TLS_ST_SW_CERT_VRFY,
    TLS_ST_CR_HELLO_REQ,
    TLS_ST_SW_KEY_UPDATE,
    TLS_ST_CW_KEY_UPDATE,
    TLS_ST_SR_KEY_UPDATE,
    TLS_ST_CR_KEY_UPDATE,
    TLS_ST_EARLY_DATA,
    TLS_ST_PENDING_EARLY_DATA_END,
    TLS_ST_CW_END_OF_EARLY_DATA,
    TLS_ST_SR_END_OF_EARLY_DATA
} OSSL_HANDSHAKE_STATE;

/*
 * Most of the following state values are no longer used and are defined to be
 * the closest equivalent value in the current state machine code. Not all
 * defines have an equivalent and are set to a dummy value (-1). SSL_ST_CONNECT
 * and SSL_ST_ACCEPT are still in use in the definition of SSL_CB_ACCEPT_LOOP,
 * SSL_CB_ACCEPT_EXIT, SSL_CB_CONNECT_LOOP and SSL_CB_CONNECT_EXIT.
 */

# define SSL_ST_CONNECT                  0x1000
# define SSL_ST_ACCEPT                   0x2000

# define SSL_ST_MASK                     0x0FFF

# define SSL_CB_LOOP                     0x01
# define SSL_CB_EXIT                     0x02
# define SSL_CB_READ                     0x04
# define SSL_CB_WRITE                    0x08
# define SSL_CB_ALERT                    0x4000/* used in callback */
# define SSL_CB_READ_ALERT               (SSL_CB_ALERT|SSL_CB_READ)
# define SSL_CB_WRITE_ALERT              (SSL_CB_ALERT|SSL_CB_WRITE)
# define SSL_CB_ACCEPT_LOOP              (SSL_ST_ACCEPT|SSL_CB_LOOP)
# define SSL_CB_ACCEPT_EXIT              (SSL_ST_ACCEPT|SSL_CB_EXIT)
# define SSL_CB_CONNECT_LOOP             (SSL_ST_CONNECT|SSL_CB_LOOP)
# define SSL_CB_CONNECT_EXIT             (SSL_ST_CONNECT|SSL_CB_EXIT)
# define SSL_CB_HANDSHAKE_START          0x10
# define SSL_CB_HANDSHAKE_DONE           0x20

/* Is the SSL_connection established? */
# define SSL_in_connect_init(a)          (SSL_in_init(a) && !SSL_is_server(a))
# define SSL_in_accept_init(a)           (SSL_in_init(a) && SSL_is_server(a))
int SSL_in_init(const SSL *s);
int SSL_in_before(const SSL *s);
int SSL_is_init_finished(const SSL *s);

/*
 * The following 3 states are kept in ssl->rlayer.rstate when reads fail, you
 * should not need these
 */
# define SSL_ST_READ_HEADER                      0xF0
# define SSL_ST_READ_BODY                        0xF1
# define SSL_ST_READ_DONE                        0xF2

/*-
 * Obtain latest Finished message
 *   -- that we sent (SSL_get_finished)
 *   -- that we expected from peer (SSL_get_peer_finished).
 * Returns length (0 == no Finished so far), copies up to 'count' bytes.
 */
size_t SSL_get_finished(const SSL *s, void *buf, size_t count);
size_t SSL_get_peer_finished(const SSL *s, void *buf, size_t count);

/*
 * use either SSL_VERIFY_NONE or SSL_VERIFY_PEER, the last 3 options are
 * 'ored' with SSL_VERIFY_PEER if they are desired
 */
# define SSL_VERIFY_NONE                 0x00
# define SSL_VERIFY_PEER                 0x01
# define SSL_VERIFY_FAIL_IF_NO_PEER_CERT 0x02
# define SSL_VERIFY_CLIENT_ONCE          0x04
# define SSL_VERIFY_POST_HANDSHAKE       0x08

# if OPENSSL_API_COMPAT < 0x10100000L
#  define OpenSSL_add_ssl_algorithms()   SSL_library_init()
#  define SSLeay_add_ssl_algorithms()    SSL_library_init()
# endif

/* More backward compatibility */
# define SSL_get_cipher(s) \
                SSL_CIPHER_get_name(SSL_get_current_cipher(s))
# define SSL_get_cipher_bits(s,np) \
                SSL_CIPHER_get_bits(SSL_get_current_cipher(s),np)
# define SSL_get_cipher_version(s) \
                SSL_CIPHER_get_version(SSL_get_current_cipher(s))
# define SSL_get_cipher_name(s) \
                SSL_CIPHER_get_name(SSL_get_current_cipher(s))
# define SSL_get_time(a)         SSL_SESSION_get_time(a)
# define SSL_set_time(a,b)       SSL_SESSION_set_time((a),(b))
# define SSL_get_timeout(a)      SSL_SESSION_get_timeout(a)
# define SSL_set_timeout(a,b)    SSL_SESSION_set_timeout((a),(b))

# define d2i_SSL_SESSION_bio(bp,s_id) ASN1_d2i_bio_of(SSL_SESSION,SSL_SESSION_new,d2i_SSL_SESSION,bp,s_id)
# define i2d_SSL_SESSION_bio(bp,s_id) ASN1_i2d_bio_of(SSL_SESSION,i2d_SSL_SESSION,bp,s_id)

DECLARE_PEM_rw(SSL_SESSION, SSL_SESSION)
# define SSL_AD_REASON_OFFSET            1000/* offset to get SSL_R_... value
                                              * from SSL_AD_... */
/* These alert types are for SSLv3 and TLSv1 */
# define SSL_AD_CLOSE_NOTIFY             SSL3_AD_CLOSE_NOTIFY
/* fatal */
# define SSL_AD_UNEXPECTED_MESSAGE       SSL3_AD_UNEXPECTED_MESSAGE
/* fatal */
# define SSL_AD_BAD_RECORD_MAC           SSL3_AD_BAD_RECORD_MAC
# define SSL_AD_DECRYPTION_FAILED        TLS1_AD_DECRYPTION_FAILED
# define SSL_AD_RECORD_OVERFLOW          TLS1_AD_RECORD_OVERFLOW
/* fatal */
# define SSL_AD_DECOMPRESSION_FAILURE    SSL3_AD_DECOMPRESSION_FAILURE
/* fatal */
# define SSL_AD_HANDSHAKE_FAILURE        SSL3_AD_HANDSHAKE_FAILURE
/* Not for TLS */
# define SSL_AD_NO_CERTIFICATE           SSL3_AD_NO_CERTIFICATE
# define SSL_AD_BAD_CERTIFICATE          SSL3_AD_BAD_CERTIFICATE
# define SSL_AD_UNSUPPORTED_CERTIFICATE  SSL3_AD_UNSUPPORTED_CERTIFICATE
# define SSL_AD_CERTIFICATE_REVOKED      SSL3_AD_CERTIFICATE_REVOKED
# define SSL_AD_CERTIFICATE_EXPIRED      SSL3_AD_CERTIFICATE_EXPIRED
# define SSL_AD_CERTIFICATE_UNKNOWN      SSL3_AD_CERTIFICATE_UNKNOWN
/* fatal */
# define SSL_AD_ILLEGAL_PARAMETER        SSL3_AD_ILLEGAL_PARAMETER
/* fatal */
# define SSL_AD_UNKNOWN_CA               TLS1_AD_UNKNOWN_CA
/* fatal */
# define SSL_AD_ACCESS_DENIED            TLS1_AD_ACCESS_DENIED
/* fatal */
# define SSL_AD_DECODE_ERROR             TLS1_AD_DECODE_ERROR
# define SSL_AD_DECRYPT_ERROR            TLS1_AD_DECRYPT_ERROR
/* fatal */
# define SSL_AD_EXPORT_RESTRICTION       TLS1_AD_EXPORT_RESTRICTION
/* fatal */
# define SSL_AD_PROTOCOL_VERSION         TLS1_AD_PROTOCOL_VERSION
/* fatal */
# define SSL_AD_INSUFFICIENT_SECURITY    TLS1_AD_INSUFFICIENT_SECURITY
/* fatal */
# define SSL_AD_INTERNAL_ERROR           TLS1_AD_INTERNAL_ERROR
# define SSL_AD_USER_CANCELLED           TLS1_AD_USER_CANCELLED
# define SSL_AD_NO_RENEGOTIATION         TLS1_AD_NO_RENEGOTIATION
# define SSL_AD_MISSING_EXTENSION        TLS13_AD_MISSING_EXTENSION
# define SSL_AD_CERTIFICATE_REQUIRED     TLS13_AD_CERTIFICATE_REQUIRED
# define SSL_AD_UNSUPPORTED_EXTENSION    TLS1_AD_UNSUPPORTED_EXTENSION
# define SSL_AD_CERTIFICATE_UNOBTAINABLE TLS1_AD_CERTIFICATE_UNOBTAINABLE
# define SSL_AD_UNRECOGNIZED_NAME        TLS1_AD_UNRECOGNIZED_NAME
# define SSL_AD_BAD_CERTIFICATE_STATUS_RESPONSE TLS1_AD_BAD_CERTIFICATE_STATUS_RESPONSE
# define SSL_AD_BAD_CERTIFICATE_HASH_VALUE TLS1_AD_BAD_CERTIFICATE_HASH_VALUE
/* fatal */
# define SSL_AD_UNKNOWN_PSK_IDENTITY     TLS1_AD_UNKNOWN_PSK_IDENTITY
/* fatal */
# define SSL_AD_INAPPROPRIATE_FALLBACK   TLS1_AD_INAPPROPRIATE_FALLBACK
# define SSL_AD_NO_APPLICATION_PROTOCOL  TLS1_AD_NO_APPLICATION_PROTOCOL
# define SSL_ERROR_NONE                  0
# define SSL_ERROR_SSL                   1
# define SSL_ERROR_WANT_READ             2
# define SSL_ERROR_WANT_WRITE            3
# define SSL_ERROR_WANT_X509_LOOKUP      4
# define SSL_ERROR_SYSCALL               5/* look at error stack/return
                                           * value/errno */
# define SSL_ERROR_ZERO_RETURN           6
# define SSL_ERROR_WANT_CONNECT          7
# define SSL_ERROR_WANT_ACCEPT           8
# define SSL_ERROR_WANT_ASYNC            9
# define SSL_ERROR_WANT_ASYNC_JOB       10
# define SSL_ERROR_WANT_CLIENT_HELLO_CB 11
# define SSL_CTRL_SET_TMP_DH                     3
# define SSL_CTRL_SET_TMP_ECDH                   4
# define SSL_CTRL_SET_TMP_DH_CB                  6
# define SSL_CTRL_GET_CLIENT_CERT_REQUEST        9
# define SSL_CTRL_GET_NUM_RENEGOTIATIONS         10
# define SSL_CTRL_CLEAR_NUM_RENEGOTIATIONS       11
# define SSL_CTRL_GET_TOTAL_RENEGOTIATIONS       12
# define SSL_CTRL_GET_FLAGS                      13
# define SSL_CTRL_EXTRA_CHAIN_CERT               14
# define SSL_CTRL_SET_MSG_CALLBACK               15
# define SSL_CTRL_SET_MSG_CALLBACK_ARG           16
/* only applies to datagram connections */
# define SSL_CTRL_SET_MTU                17
/* Stats */
# define SSL_CTRL_SESS_NUMBER                    20
# define SSL_CTRL_SESS_CONNECT                   21
# define SSL_CTRL_SESS_CONNECT_GOOD              22
# define SSL_CTRL_SESS_CONNECT_RENEGOTIATE       23
# define SSL_CTRL_SESS_ACCEPT                    24
# define SSL_CTRL_SESS_ACCEPT_GOOD               25
# define SSL_CTRL_SESS_ACCEPT_RENEGOTIATE        26
# define SSL_CTRL_SESS_HIT                       27
# define SSL_CTRL_SESS_CB_HIT                    28
# define SSL_CTRL_SESS_MISSES                    29
# define SSL_CTRL_SESS_TIMEOUTS                  30
# define SSL_CTRL_SESS_CACHE_FULL                31
# define SSL_CTRL_MODE                           33
# define SSL_CTRL_GET_READ_AHEAD                 40
# define SSL_CTRL_SET_READ_AHEAD                 41
# define SSL_CTRL_SET_SESS_CACHE_SIZE            42
# define SSL_CTRL_GET_SESS_CACHE_SIZE            43
# define SSL_CTRL_SET_SESS_CACHE_MODE            44
# define SSL_CTRL_GET_SESS_CACHE_MODE            45
# define SSL_CTRL_GET_MAX_CERT_LIST              50
# define SSL_CTRL_SET_MAX_CERT_LIST              51
# define SSL_CTRL_SET_MAX_SEND_FRAGMENT          52
/* see tls1.h for macros based on these */
# define SSL_CTRL_SET_TLSEXT_SERVERNAME_CB       53
# define SSL_CTRL_SET_TLSEXT_SERVERNAME_ARG      54
# define SSL_CTRL_SET_TLSEXT_HOSTNAME            55
# define SSL_CTRL_SET_TLSEXT_DEBUG_CB            56
# define SSL_CTRL_SET_TLSEXT_DEBUG_ARG           57
# define SSL_CTRL_GET_TLSEXT_TICKET_KEYS         58
# define SSL_CTRL_SET_TLSEXT_TICKET_KEYS         59
/*# define SSL_CTRL_SET_TLSEXT_OPAQUE_PRF_INPUT    60 */
/*# define SSL_CTRL_SET_TLSEXT_OPAQUE_PRF_INPUT_CB 61 */
/*# define SSL_CTRL_SET_TLSEXT_OPAQUE_PRF_INPUT_CB_ARG 62 */
# define SSL_CTRL_SET_TLSEXT_STATUS_REQ_CB       63
# define SSL_CTRL_SET_TLSEXT_STATUS_REQ_CB_ARG   64
# define SSL_CTRL_SET_TLSEXT_STATUS_REQ_TYPE     65
# define SSL_CTRL_GET_TLSEXT_STATUS_REQ_EXTS     66
# define SSL_CTRL_SET_TLSEXT_STATUS_REQ_EXTS     67
# define SSL_CTRL_GET_TLSEXT_STATUS_REQ_IDS      68
# define SSL_CTRL_SET_TLSEXT_STATUS_REQ_IDS      69
# define SSL_CTRL_GET_TLSEXT_STATUS_REQ_OCSP_RESP        70
# define SSL_CTRL_SET_TLSEXT_STATUS_REQ_OCSP_RESP        71
# define SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB       72
# define SSL_CTRL_SET_TLS_EXT_SRP_USERNAME_CB    75
# define SSL_CTRL_SET_SRP_VERIFY_PARAM_CB                76
# define SSL_CTRL_SET_SRP_GIVE_CLIENT_PWD_CB             77
# define SSL_CTRL_SET_SRP_ARG            78
# define SSL_CTRL_SET_TLS_EXT_SRP_USERNAME               79
# define SSL_CTRL_SET_TLS_EXT_SRP_STRENGTH               80
# define SSL_CTRL_SET_TLS_EXT_SRP_PASSWORD               81
# ifndef OPENSSL_NO_HEARTBEATS
#  define SSL_CTRL_DTLS_EXT_SEND_HEARTBEAT               85
#  define SSL_CTRL_GET_DTLS_EXT_HEARTBEAT_PENDING        86
#  define SSL_CTRL_SET_DTLS_EXT_HEARTBEAT_NO_REQUESTS    87
# endif
# define DTLS_CTRL_GET_TIMEOUT           73
# define DTLS_CTRL_HANDLE_TIMEOUT        74
# define SSL_CTRL_GET_RI_SUPPORT                 76
# define SSL_CTRL_CLEAR_MODE                     78
# define SSL_CTRL_SET_NOT_RESUMABLE_SESS_CB      79
# define SSL_CTRL_GET_EXTRA_CHAIN_CERTS          82
# define SSL_CTRL_CLEAR_EXTRA_CHAIN_CERTS        83
# define SSL_CTRL_CHAIN                          88
# define SSL_CTRL_CHAIN_CERT                     89
# define SSL_CTRL_GET_GROUPS                     90
# define SSL_CTRL_SET_GROUPS                     91
# define SSL_CTRL_SET_GROUPS_LIST                92
# define SSL_CTRL_GET_SHARED_GROUP               93
# define SSL_CTRL_SET_SIGALGS                    97
# define SSL_CTRL_SET_SIGALGS_LIST               98
# define SSL_CTRL_CERT_FLAGS                     99
# define SSL_CTRL_CLEAR_CERT_FLAGS               100
# define SSL_CTRL_SET_CLIENT_SIGALGS             101
# define SSL_CTRL_SET_CLIENT_SIGALGS_LIST        102
# define SSL_CTRL_GET_CLIENT_CERT_TYPES          103
# define SSL_CTRL_SET_CLIENT_CERT_TYPES          104
# define SSL_CTRL_BUILD_CERT_CHAIN               105
# define SSL_CTRL_SET_VERIFY_CERT_STORE          106
# define SSL_CTRL_SET_CHAIN_CERT_STORE           107
# define SSL_CTRL_GET_PEER_SIGNATURE_NID         108
# define SSL_CTRL_GET_PEER_TMP_KEY               109
# define SSL_CTRL_GET_RAW_CIPHERLIST             110
# define SSL_CTRL_GET_EC_POINT_FORMATS           111
# define SSL_CTRL_GET_CHAIN_CERTS                115
# define SSL_CTRL_SELECT_CURRENT_CERT            116
# define SSL_CTRL_SET_CURRENT_CERT               117
# define SSL_CTRL_SET_DH_AUTO                    118
# define DTLS_CTRL_SET_LINK_MTU                  120
# define DTLS_CTRL_GET_LINK_MIN_MTU              121
# define SSL_CTRL_GET_EXTMS_SUPPORT              122
# define SSL_CTRL_SET_MIN_PROTO_VERSION          123
# define SSL_CTRL_SET_MAX_PROTO_VERSION          124
# define SSL_CTRL_SET_SPLIT_SEND_FRAGMENT        125
# define SSL_CTRL_SET_MAX_PIPELINES              126
# define SSL_CTRL_GET_TLSEXT_STATUS_REQ_TYPE     127
# define SSL_CTRL_GET_TLSEXT_STATUS_REQ_CB       128
# define SSL_CTRL_GET_TLSEXT_STATUS_REQ_CB_ARG   129
# define SSL_CTRL_GET_MIN_PROTO_VERSION          130
# define SSL_CTRL_GET_MAX_PROTO_VERSION          131
# define SSL_CTRL_GET_SIGNATURE_NID              132
# define SSL_CTRL_GET_TMP_KEY                    133
# define SSL_CERT_SET_FIRST                      1
# define SSL_CERT_SET_NEXT                       2
# define SSL_CERT_SET_SERVER                     3
# define DTLSv1_get_timeout(ssl, arg) \
        SSL_ctrl(ssl,DTLS_CTRL_GET_TIMEOUT,0, (void *)(arg))
# define DTLSv1_handle_timeout(ssl) \
        SSL_ctrl(ssl,DTLS_CTRL_HANDLE_TIMEOUT,0, NULL)
# define SSL_num_renegotiations(ssl) \
        SSL_ctrl((ssl),SSL_CTRL_GET_NUM_RENEGOTIATIONS,0,NULL)
# define SSL_clear_num_renegotiations(ssl) \
        SSL_ctrl((ssl),SSL_CTRL_CLEAR_NUM_RENEGOTIATIONS,0,NULL)
# define SSL_total_renegotiations(ssl) \
        SSL_ctrl((ssl),SSL_CTRL_GET_TOTAL_RENEGOTIATIONS,0,NULL)
# define SSL_CTX_set_tmp_dh(ctx,dh) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_TMP_DH,0,(char *)(dh))
# define SSL_CTX_set_tmp_ecdh(ctx,ecdh) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_TMP_ECDH,0,(char *)(ecdh))
# define SSL_CTX_set_dh_auto(ctx, onoff) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_DH_AUTO,onoff,NULL)
# define SSL_set_dh_auto(s, onoff) \
        SSL_ctrl(s,SSL_CTRL_SET_DH_AUTO,onoff,NULL)
# define SSL_set_tmp_dh(ssl,dh) \
        SSL_ctrl(ssl,SSL_CTRL_SET_TMP_DH,0,(char *)(dh))
# define SSL_set_tmp_ecdh(ssl,ecdh) \
        SSL_ctrl(ssl,SSL_CTRL_SET_TMP_ECDH,0,(char *)(ecdh))
# define SSL_CTX_add_extra_chain_cert(ctx,x509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_EXTRA_CHAIN_CERT,0,(char *)(x509))
# define SSL_CTX_get_extra_chain_certs(ctx,px509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_EXTRA_CHAIN_CERTS,0,px509)
# define SSL_CTX_get_extra_chain_certs_only(ctx,px509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_EXTRA_CHAIN_CERTS,1,px509)
# define SSL_CTX_clear_extra_chain_certs(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_CLEAR_EXTRA_CHAIN_CERTS,0,NULL)
# define SSL_CTX_set0_chain(ctx,sk) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_CHAIN,0,(char *)(sk))
# define SSL_CTX_set1_chain(ctx,sk) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_CHAIN,1,(char *)(sk))
# define SSL_CTX_add0_chain_cert(ctx,x509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_CHAIN_CERT,0,(char *)(x509))
# define SSL_CTX_add1_chain_cert(ctx,x509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_CHAIN_CERT,1,(char *)(x509))
# define SSL_CTX_get0_chain_certs(ctx,px509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_CHAIN_CERTS,0,px509)
# define SSL_CTX_clear_chain_certs(ctx) \
        SSL_CTX_set0_chain(ctx,NULL)
# define SSL_CTX_build_cert_chain(ctx, flags) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_BUILD_CERT_CHAIN, flags, NULL)
# define SSL_CTX_select_current_cert(ctx,x509) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SELECT_CURRENT_CERT,0,(char *)(x509))
# define SSL_CTX_set_current_cert(ctx, op) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_CURRENT_CERT, op, NULL)
# define SSL_CTX_set0_verify_cert_store(ctx,st) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_VERIFY_CERT_STORE,0,(char *)(st))
# define SSL_CTX_set1_verify_cert_store(ctx,st) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_VERIFY_CERT_STORE,1,(char *)(st))
# define SSL_CTX_set0_chain_cert_store(ctx,st) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_CHAIN_CERT_STORE,0,(char *)(st))
# define SSL_CTX_set1_chain_cert_store(ctx,st) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_CHAIN_CERT_STORE,1,(char *)(st))
# define SSL_set0_chain(ctx,sk) \
        SSL_ctrl(ctx,SSL_CTRL_CHAIN,0,(char *)(sk))
# define SSL_set1_chain(ctx,sk) \
        SSL_ctrl(ctx,SSL_CTRL_CHAIN,1,(char *)(sk))
# define SSL_add0_chain_cert(ctx,x509) \
        SSL_ctrl(ctx,SSL_CTRL_CHAIN_CERT,0,(char *)(x509))
# define SSL_add1_chain_cert(ctx,x509) \
        SSL_ctrl(ctx,SSL_CTRL_CHAIN_CERT,1,(char *)(x509))
# define SSL_get0_chain_certs(ctx,px509) \
        SSL_ctrl(ctx,SSL_CTRL_GET_CHAIN_CERTS,0,px509)
# define SSL_clear_chain_certs(ctx) \
        SSL_set0_chain(ctx,NULL)
# define SSL_build_cert_chain(s, flags) \
        SSL_ctrl(s,SSL_CTRL_BUILD_CERT_CHAIN, flags, NULL)
# define SSL_select_current_cert(ctx,x509) \
        SSL_ctrl(ctx,SSL_CTRL_SELECT_CURRENT_CERT,0,(char *)(x509))
# define SSL_set_current_cert(ctx,op) \
        SSL_ctrl(ctx,SSL_CTRL_SET_CURRENT_CERT, op, NULL)
# define SSL_set0_verify_cert_store(s,st) \
        SSL_ctrl(s,SSL_CTRL_SET_VERIFY_CERT_STORE,0,(char *)(st))
# define SSL_set1_verify_cert_store(s,st) \
        SSL_ctrl(s,SSL_CTRL_SET_VERIFY_CERT_STORE,1,(char *)(st))
# define SSL_set0_chain_cert_store(s,st) \
        SSL_ctrl(s,SSL_CTRL_SET_CHAIN_CERT_STORE,0,(char *)(st))
# define SSL_set1_chain_cert_store(s,st) \
        SSL_ctrl(s,SSL_CTRL_SET_CHAIN_CERT_STORE,1,(char *)(st))
# define SSL_get1_groups(ctx, s) \
        SSL_ctrl(ctx,SSL_CTRL_GET_GROUPS,0,(char *)(s))
# define SSL_CTX_set1_groups(ctx, glist, glistlen) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_GROUPS,glistlen,(char *)(glist))
# define SSL_CTX_set1_groups_list(ctx, s) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_GROUPS_LIST,0,(char *)(s))
# define SSL_set1_groups(ctx, glist, glistlen) \
        SSL_ctrl(ctx,SSL_CTRL_SET_GROUPS,glistlen,(char *)(glist))
# define SSL_set1_groups_list(ctx, s) \
        SSL_ctrl(ctx,SSL_CTRL_SET_GROUPS_LIST,0,(char *)(s))
# define SSL_get_shared_group(s, n) \
        SSL_ctrl(s,SSL_CTRL_GET_SHARED_GROUP,n,NULL)
# define SSL_CTX_set1_sigalgs(ctx, slist, slistlen) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_SIGALGS,slistlen,(int *)(slist))
# define SSL_CTX_set1_sigalgs_list(ctx, s) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_SIGALGS_LIST,0,(char *)(s))
# define SSL_set1_sigalgs(ctx, slist, slistlen) \
        SSL_ctrl(ctx,SSL_CTRL_SET_SIGALGS,slistlen,(int *)(slist))
# define SSL_set1_sigalgs_list(ctx, s) \
        SSL_ctrl(ctx,SSL_CTRL_SET_SIGALGS_LIST,0,(char *)(s))
# define SSL_CTX_set1_client_sigalgs(ctx, slist, slistlen) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_CLIENT_SIGALGS,slistlen,(int *)(slist))
# define SSL_CTX_set1_client_sigalgs_list(ctx, s) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_CLIENT_SIGALGS_LIST,0,(char *)(s))
# define SSL_set1_client_sigalgs(ctx, slist, slistlen) \
        SSL_ctrl(ctx,SSL_CTRL_SET_CLIENT_SIGALGS,clistlen,(int *)(slist))
# define SSL_set1_client_sigalgs_list(ctx, s) \
        SSL_ctrl(ctx,SSL_CTRL_SET_CLIENT_SIGALGS_LIST,0,(char *)(s))
# define SSL_get0_certificate_types(s, clist) \
        SSL_ctrl(s, SSL_CTRL_GET_CLIENT_CERT_TYPES, 0, (char *)(clist))
# define SSL_CTX_set1_client_certificate_types(ctx, clist, clistlen) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_CLIENT_CERT_TYPES,clistlen, \
                     (char *)(clist))
# define SSL_set1_client_certificate_types(s, clist, clistlen) \
        SSL_ctrl(s,SSL_CTRL_SET_CLIENT_CERT_TYPES,clistlen,(char *)(clist))
# define SSL_get_signature_nid(s, pn) \
        SSL_ctrl(s,SSL_CTRL_GET_SIGNATURE_NID,0,pn)
# define SSL_get_peer_signature_nid(s, pn) \
        SSL_ctrl(s,SSL_CTRL_GET_PEER_SIGNATURE_NID,0,pn)
# define SSL_get_peer_tmp_key(s, pk) \
        SSL_ctrl(s,SSL_CTRL_GET_PEER_TMP_KEY,0,pk)
# define SSL_get_tmp_key(s, pk) \
        SSL_ctrl(s,SSL_CTRL_GET_TMP_KEY,0,pk)
# define SSL_get0_raw_cipherlist(s, plst) \
        SSL_ctrl(s,SSL_CTRL_GET_RAW_CIPHERLIST,0,plst)
# define SSL_get0_ec_point_formats(s, plst) \
        SSL_ctrl(s,SSL_CTRL_GET_EC_POINT_FORMATS,0,plst)
# define SSL_CTX_set_min_proto_version(ctx, version) \
        SSL_CTX_ctrl(ctx, SSL_CTRL_SET_MIN_PROTO_VERSION, version, NULL)
# define SSL_CTX_set_max_proto_version(ctx, version) \
        SSL_CTX_ctrl(ctx, SSL_CTRL_SET_MAX_PROTO_VERSION, version, NULL)
# define SSL_CTX_get_min_proto_version(ctx) \
        SSL_CTX_ctrl(ctx, SSL_CTRL_GET_MIN_PROTO_VERSION, 0, NULL)
# define SSL_CTX_get_max_proto_version(ctx) \
        SSL_CTX_ctrl(ctx, SSL_CTRL_GET_MAX_PROTO_VERSION, 0, NULL)
# define SSL_set_min_proto_version(s, version) \
        SSL_ctrl(s, SSL_CTRL_SET_MIN_PROTO_VERSION, version, NULL)
# define SSL_set_max_proto_version(s, version) \
        SSL_ctrl(s, SSL_CTRL_SET_MAX_PROTO_VERSION, version, NULL)
# define SSL_get_min_proto_version(s) \
        SSL_ctrl(s, SSL_CTRL_GET_MIN_PROTO_VERSION, 0, NULL)
# define SSL_get_max_proto_version(s) \
        SSL_ctrl(s, SSL_CTRL_GET_MAX_PROTO_VERSION, 0, NULL)

/* Backwards compatibility, original 1.1.0 names */
# define SSL_CTRL_GET_SERVER_TMP_KEY \
         SSL_CTRL_GET_PEER_TMP_KEY
# define SSL_get_server_tmp_key(s, pk) \
         SSL_get_peer_tmp_key(s, pk)

/*
 * The following symbol names are old and obsolete. They are kept
 * for compatibility reasons only and should not be used anymore.
 */
# define SSL_CTRL_GET_CURVES           SSL_CTRL_GET_GROUPS
# define SSL_CTRL_SET_CURVES           SSL_CTRL_SET_GROUPS
# define SSL_CTRL_SET_CURVES_LIST      SSL_CTRL_SET_GROUPS_LIST
# define SSL_CTRL_GET_SHARED_CURVE     SSL_CTRL_GET_SHARED_GROUP

# define SSL_get1_curves               SSL_get1_groups
# define SSL_CTX_set1_curves           SSL_CTX_set1_groups
# define SSL_CTX_set1_curves_list      SSL_CTX_set1_groups_list
# define SSL_set1_curves               SSL_set1_groups
# define SSL_set1_curves_list          SSL_set1_groups_list
# define SSL_get_shared_curve          SSL_get_shared_group


# if OPENSSL_API_COMPAT < 0x10100000L
/* Provide some compatibility macros for removed functionality. */
#  define SSL_CTX_need_tmp_RSA(ctx)                0
#  define SSL_CTX_set_tmp_rsa(ctx,rsa)             1
#  define SSL_need_tmp_RSA(ssl)                    0
#  define SSL_set_tmp_rsa(ssl,rsa)                 1
#  define SSL_CTX_set_ecdh_auto(dummy, onoff)      ((onoff) != 0)
#  define SSL_set_ecdh_auto(dummy, onoff)          ((onoff) != 0)
/*
 * We "pretend" to call the callback to avoid warnings about unused static
 * functions.
 */
#  define SSL_CTX_set_tmp_rsa_callback(ctx, cb)    while(0) (cb)(NULL, 0, 0)
#  define SSL_set_tmp_rsa_callback(ssl, cb)        while(0) (cb)(NULL, 0, 0)
# endif
__owur const BIO_METHOD *BIO_f_ssl(void);
__owur BIO *BIO_new_ssl(SSL_CTX *ctx, int client);
__owur BIO *BIO_new_ssl_connect(SSL_CTX *ctx);
__owur BIO *BIO_new_buffer_ssl_connect(SSL_CTX *ctx);
__owur int BIO_ssl_copy_session_id(BIO *to, BIO *from);
void BIO_ssl_shutdown(BIO *ssl_bio);

__owur int SSL_CTX_set_cipher_list(SSL_CTX *, const char *str);
__owur SSL_CTX *SSL_CTX_new(const SSL_METHOD *meth);
int SSL_CTX_up_ref(SSL_CTX *ctx);
void SSL_CTX_free(SSL_CTX *);
__owur long SSL_CTX_set_timeout(SSL_CTX *ctx, long t);
__owur long SSL_CTX_get_timeout(const SSL_CTX *ctx);
__owur X509_STORE *SSL_CTX_get_cert_store(const SSL_CTX *);
void SSL_CTX_set_cert_store(SSL_CTX *, X509_STORE *);
void SSL_CTX_set1_cert_store(SSL_CTX *, X509_STORE *);
__owur int SSL_want(const SSL *s);
__owur int SSL_clear(SSL *s);

void SSL_CTX_flush_sessions(SSL_CTX *ctx, long tm);

__owur const SSL_CIPHER *SSL_get_current_cipher(const SSL *s);
__owur const SSL_CIPHER *SSL_get_pending_cipher(const SSL *s);
__owur int SSL_CIPHER_get_bits(const SSL_CIPHER *c, int *alg_bits);
__owur const char *SSL_CIPHER_get_version(const SSL_CIPHER *c);
__owur const char *SSL_CIPHER_get_name(const SSL_CIPHER *c);
__owur const char *SSL_CIPHER_standard_name(const SSL_CIPHER *c);
__owur const char *OPENSSL_cipher_name(const char *rfc_name);
__owur uint32_t SSL_CIPHER_get_id(const SSL_CIPHER *c);
__owur uint16_t SSL_CIPHER_get_protocol_id(const SSL_CIPHER *c);
__owur int SSL_CIPHER_get_kx_nid(const SSL_CIPHER *c);
__owur int SSL_CIPHER_get_auth_nid(const SSL_CIPHER *c);
__owur const EVP_MD *SSL_CIPHER_get_handshake_digest(const SSL_CIPHER *c);
__owur int SSL_CIPHER_is_aead(const SSL_CIPHER *c);

__owur int SSL_get_fd(const SSL *s);
__owur int SSL_get_rfd(const SSL *s);
__owur int SSL_get_wfd(const SSL *s);
__owur const char *SSL_get_cipher_list(const SSL *s, int n);
__owur char *SSL_get_shared_ciphers(const SSL *s, char *buf, int size);
__owur int SSL_get_read_ahead(const SSL *s);
__owur int SSL_pending(const SSL *s);
__owur int SSL_has_pending(const SSL *s);
# ifndef OPENSSL_NO_SOCK
__owur int SSL_set_fd(SSL *s, int fd);
__owur int SSL_set_rfd(SSL *s, int fd);
__owur int SSL_set_wfd(SSL *s, int fd);
# endif
void SSL_set0_rbio(SSL *s, BIO *rbio);
void SSL_set0_wbio(SSL *s, BIO *wbio);
void SSL_set_bio(SSL *s, BIO *rbio, BIO *wbio);
__owur BIO *SSL_get_rbio(const SSL *s);
__owur BIO *SSL_get_wbio(const SSL *s);
__owur int SSL_set_cipher_list(SSL *s, const char *str);
__owur int SSL_CTX_set_ciphersuites(SSL_CTX *ctx, const char *str);
__owur int SSL_set_ciphersuites(SSL *s, const char *str);
void SSL_set_read_ahead(SSL *s, int yes);
__owur int SSL_get_verify_mode(const SSL *s);
__owur int SSL_get_verify_depth(const SSL *s);
__owur SSL_verify_cb SSL_get_verify_callback(const SSL *s);
void SSL_set_verify(SSL *s, int mode, SSL_verify_cb callback);
void SSL_set_verify_depth(SSL *s, int depth);
void SSL_set_cert_cb(SSL *s, int (*cb) (SSL *ssl, void *arg), void *arg);
# ifndef OPENSSL_NO_RSA
__owur int SSL_use_RSAPrivateKey(SSL *ssl, RSA *rsa);
__owur int SSL_use_RSAPrivateKey_ASN1(SSL *ssl, const unsigned char *d,
                                      long len);
# endif
__owur int SSL_use_PrivateKey(SSL *ssl, EVP_PKEY *pkey);
__owur int SSL_use_PrivateKey_ASN1(int pk, SSL *ssl, const unsigned char *d,
                                   long len);
__owur int SSL_use_certificate(SSL *ssl, X509 *x);
__owur int SSL_use_certificate_ASN1(SSL *ssl, const unsigned char *d, int len);
__owur int SSL_use_cert_and_key(SSL *ssl, X509 *x509, EVP_PKEY *privatekey,
                                STACK_OF(X509) *chain, int override);


/* serverinfo file format versions */
# define SSL_SERVERINFOV1   1
# define SSL_SERVERINFOV2   2

/* Set serverinfo data for the current active cert. */
__owur int SSL_CTX_use_serverinfo(SSL_CTX *ctx, const unsigned char *serverinfo,
                                  size_t serverinfo_length);
__owur int SSL_CTX_use_serverinfo_ex(SSL_CTX *ctx, unsigned int version,
                                     const unsigned char *serverinfo,
                                     size_t serverinfo_length);
__owur int SSL_CTX_use_serverinfo_file(SSL_CTX *ctx, const char *file);

#ifndef OPENSSL_NO_RSA
__owur int SSL_use_RSAPrivateKey_file(SSL *ssl, const char *file, int type);
#endif

__owur int SSL_use_PrivateKey_file(SSL *ssl, const char *file, int type);
__owur int SSL_use_certificate_file(SSL *ssl, const char *file, int type);

#ifndef OPENSSL_NO_RSA
__owur int SSL_CTX_use_RSAPrivateKey_file(SSL_CTX *ctx, const char *file,
                                          int type);
#endif
__owur int SSL_CTX_use_PrivateKey_file(SSL_CTX *ctx, const char *file,
                                       int type);
__owur int SSL_CTX_use_certificate_file(SSL_CTX *ctx, const char *file,
                                        int type);
/* PEM type */
__owur int SSL_CTX_use_certificate_chain_file(SSL_CTX *ctx, const char *file);
__owur int SSL_use_certificate_chain_file(SSL *ssl, const char *file);
__owur STACK_OF(X509_NAME) *SSL_load_client_CA_file(const char *file);
__owur int SSL_add_file_cert_subjects_to_stack(STACK_OF(X509_NAME) *stackCAs,
                                               const char *file);
int SSL_add_dir_cert_subjects_to_stack(STACK_OF(X509_NAME) *stackCAs,
                                       const char *dir);

# if OPENSSL_API_COMPAT < 0x10100000L
#  define SSL_load_error_strings() \
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS \
                     | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL)
# endif

__owur const char *SSL_state_string(const SSL *s);
__owur const char *SSL_rstate_string(const SSL *s);
__owur const char *SSL_state_string_long(const SSL *s);
__owur const char *SSL_rstate_string_long(const SSL *s);
__owur long SSL_SESSION_get_time(const SSL_SESSION *s);
__owur long SSL_SESSION_set_time(SSL_SESSION *s, long t);
__owur long SSL_SESSION_get_timeout(const SSL_SESSION *s);
__owur long SSL_SESSION_set_timeout(SSL_SESSION *s, long t);
__owur int SSL_SESSION_get_protocol_version(const SSL_SESSION *s);
__owur int SSL_SESSION_set_protocol_version(SSL_SESSION *s, int version);

__owur const char *SSL_SESSION_get0_hostname(const SSL_SESSION *s);
__owur int SSL_SESSION_set1_hostname(SSL_SESSION *s, const char *hostname);
void SSL_SESSION_get0_alpn_selected(const SSL_SESSION *s,
                                    const unsigned char **alpn,
                                    size_t *len);
__owur int SSL_SESSION_set1_alpn_selected(SSL_SESSION *s,
                                          const unsigned char *alpn,
                                          size_t len);
__owur const SSL_CIPHER *SSL_SESSION_get0_cipher(const SSL_SESSION *s);
__owur int SSL_SESSION_set_cipher(SSL_SESSION *s, const SSL_CIPHER *cipher);
__owur int SSL_SESSION_has_ticket(const SSL_SESSION *s);
__owur unsigned long SSL_SESSION_get_ticket_lifetime_hint(const SSL_SESSION *s);
void SSL_SESSION_get0_ticket(const SSL_SESSION *s, const unsigned char **tick,
                             size_t *len);
__owur uint32_t SSL_SESSION_get_max_early_data(const SSL_SESSION *s);
__owur int SSL_SESSION_set_max_early_data(SSL_SESSION *s,
                                          uint32_t max_early_data);
__owur int SSL_copy_session_id(SSL *to, const SSL *from);
__owur X509 *SSL_SESSION_get0_peer(SSL_SESSION *s);
__owur int SSL_SESSION_set1_id_context(SSL_SESSION *s,
                                       const unsigned char *sid_ctx,
                                       unsigned int sid_ctx_len);
__owur int SSL_SESSION_set1_id(SSL_SESSION *s, const unsigned char *sid,
                               unsigned int sid_len);
__owur int SSL_SESSION_is_resumable(const SSL_SESSION *s);

__owur SSL_SESSION *SSL_SESSION_new(void);
__owur SSL_SESSION *SSL_SESSION_dup(SSL_SESSION *src);
const unsigned char *SSL_SESSION_get_id(const SSL_SESSION *s,
                                        unsigned int *len);
const unsigned char *SSL_SESSION_get0_id_context(const SSL_SESSION *s,
                                                 unsigned int *len);
__owur unsigned int SSL_SESSION_get_compress_id(const SSL_SESSION *s);
# ifndef OPENSSL_NO_STDIO
int SSL_SESSION_print_fp(FILE *fp, const SSL_SESSION *ses);
# endif
int SSL_SESSION_print(BIO *fp, const SSL_SESSION *ses);
int SSL_SESSION_print_keylog(BIO *bp, const SSL_SESSION *x);
int SSL_SESSION_up_ref(SSL_SESSION *ses);
void SSL_SESSION_free(SSL_SESSION *ses);
__owur int i2d_SSL_SESSION(SSL_SESSION *in, unsigned char **pp);
__owur int SSL_set_session(SSL *to, SSL_SESSION *session);
int SSL_CTX_add_session(SSL_CTX *ctx, SSL_SESSION *session);
int SSL_CTX_remove_session(SSL_CTX *ctx, SSL_SESSION *session);
__owur int SSL_CTX_set_generate_session_id(SSL_CTX *ctx, GEN_SESSION_CB cb);
__owur int SSL_set_generate_session_id(SSL *s, GEN_SESSION_CB cb);
__owur int SSL_has_matching_session_id(const SSL *s,
                                       const unsigned char *id,
                                       unsigned int id_len);
SSL_SESSION *d2i_SSL_SESSION(SSL_SESSION **a, const unsigned char **pp,
                             long length);

# ifdef HEADER_X509_H
__owur X509 *SSL_get_peer_certificate(const SSL *s);
# endif

__owur STACK_OF(X509) *SSL_get_peer_cert_chain(const SSL *s);

__owur int SSL_CTX_get_verify_mode(const SSL_CTX *ctx);
__owur int SSL_CTX_get_verify_depth(const SSL_CTX *ctx);
__owur SSL_verify_cb SSL_CTX_get_verify_callback(const SSL_CTX *ctx);
void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, SSL_verify_cb callback);
void SSL_CTX_set_verify_depth(SSL_CTX *ctx, int depth);
void SSL_CTX_set_cert_verify_callback(SSL_CTX *ctx,
                                      int (*cb) (X509_STORE_CTX *, void *),
                                      void *arg);
void SSL_CTX_set_cert_cb(SSL_CTX *c, int (*cb) (SSL *ssl, void *arg),
                         void *arg);
# ifndef OPENSSL_NO_RSA
__owur int SSL_CTX_use_RSAPrivateKey(SSL_CTX *ctx, RSA *rsa);
__owur int SSL_CTX_use_RSAPrivateKey_ASN1(SSL_CTX *ctx, const unsigned char *d,
                                          long len);
# endif
__owur int SSL_CTX_use_PrivateKey(SSL_CTX *ctx, EVP_PKEY *pkey);
__owur int SSL_CTX_use_PrivateKey_ASN1(int pk, SSL_CTX *ctx,
                                       const unsigned char *d, long len);
__owur int SSL_CTX_use_certificate(SSL_CTX *ctx, X509 *x);
__owur int SSL_CTX_use_certificate_ASN1(SSL_CTX *ctx, int len,
                                        const unsigned char *d);
__owur int SSL_CTX_use_cert_and_key(SSL_CTX *ctx, X509 *x509, EVP_PKEY *privatekey,
                                    STACK_OF(X509) *chain, int override);

void SSL_CTX_set_default_passwd_cb(SSL_CTX *ctx, pem_password_cb *cb);
void SSL_CTX_set_default_passwd_cb_userdata(SSL_CTX *ctx, void *u);
pem_password_cb *SSL_CTX_get_default_passwd_cb(SSL_CTX *ctx);
void *SSL_CTX_get_default_passwd_cb_userdata(SSL_CTX *ctx);
void SSL_set_default_passwd_cb(SSL *s, pem_password_cb *cb);
void SSL_set_default_passwd_cb_userdata(SSL *s, void *u);
pem_password_cb *SSL_get_default_passwd_cb(SSL *s);
void *SSL_get_default_passwd_cb_userdata(SSL *s);

__owur int SSL_CTX_check_private_key(const SSL_CTX *ctx);
__owur int SSL_check_private_key(const SSL *ctx);

__owur int SSL_CTX_set_session_id_context(SSL_CTX *ctx,
                                          const unsigned char *sid_ctx,
                                          unsigned int sid_ctx_len);

SSL *SSL_new(SSL_CTX *ctx);
int SSL_up_ref(SSL *s);
int SSL_is_dtls(const SSL *s);
__owur int SSL_set_session_id_context(SSL *ssl, const unsigned char *sid_ctx,
                                      unsigned int sid_ctx_len);

__owur int SSL_CTX_set_purpose(SSL_CTX *ctx, int purpose);
__owur int SSL_set_purpose(SSL *ssl, int purpose);
__owur int SSL_CTX_set_trust(SSL_CTX *ctx, int trust);
__owur int SSL_set_trust(SSL *ssl, int trust);

__owur int SSL_set1_host(SSL *s, const char *hostname);
__owur int SSL_add1_host(SSL *s, const char *hostname);
__owur const char *SSL_get0_peername(SSL *s);
void SSL_set_hostflags(SSL *s, unsigned int flags);

__owur int SSL_CTX_dane_enable(SSL_CTX *ctx);
__owur int SSL_CTX_dane_mtype_set(SSL_CTX *ctx, const EVP_MD *md,
                                  uint8_t mtype, uint8_t ord);
__owur int SSL_dane_enable(SSL *s, const char *basedomain);
__owur int SSL_dane_tlsa_add(SSL *s, uint8_t usage, uint8_t selector,
                             uint8_t mtype, unsigned const char *data, size_t dlen);
__owur int SSL_get0_dane_authority(SSL *s, X509 **mcert, EVP_PKEY **mspki);
__owur int SSL_get0_dane_tlsa(SSL *s, uint8_t *usage, uint8_t *selector,
                              uint8_t *mtype, unsigned const char **data,
                              size_t *dlen);
/*
 * Bridge opacity barrier between libcrypt and libssl, also needed to support
 * offline testing in test/danetest.c
 */
SSL_DANE *SSL_get0_dane(SSL *ssl);
/*
 * DANE flags
 */
unsigned long SSL_CTX_dane_set_flags(SSL_CTX *ctx, unsigned long flags);
unsigned long SSL_CTX_dane_clear_flags(SSL_CTX *ctx, unsigned long flags);
unsigned long SSL_dane_set_flags(SSL *ssl, unsigned long flags);
unsigned long SSL_dane_clear_flags(SSL *ssl, unsigned long flags);

__owur int SSL_CTX_set1_param(SSL_CTX *ctx, X509_VERIFY_PARAM *vpm);
__owur int SSL_set1_param(SSL *ssl, X509_VERIFY_PARAM *vpm);

__owur X509_VERIFY_PARAM *SSL_CTX_get0_param(SSL_CTX *ctx);
__owur X509_VERIFY_PARAM *SSL_get0_param(SSL *ssl);

# ifndef OPENSSL_NO_SRP
int SSL_CTX_set_srp_username(SSL_CTX *ctx, char *name);
int SSL_CTX_set_srp_password(SSL_CTX *ctx, char *password);
int SSL_CTX_set_srp_strength(SSL_CTX *ctx, int strength);
int SSL_CTX_set_srp_client_pwd_callback(SSL_CTX *ctx,
                                        char *(*cb) (SSL *, void *));
int SSL_CTX_set_srp_verify_param_callback(SSL_CTX *ctx,
                                          int (*cb) (SSL *, void *));
int SSL_CTX_set_srp_username_callback(SSL_CTX *ctx,
                                      int (*cb) (SSL *, int *, void *));
int SSL_CTX_set_srp_cb_arg(SSL_CTX *ctx, void *arg);

int SSL_set_srp_server_param(SSL *s, const BIGNUM *N, const BIGNUM *g,
                             BIGNUM *sa, BIGNUM *v, char *info);
int SSL_set_srp_server_param_pw(SSL *s, const char *user, const char *pass,
                                const char *grp);

__owur BIGNUM *SSL_get_srp_g(SSL *s);
__owur BIGNUM *SSL_get_srp_N(SSL *s);

__owur char *SSL_get_srp_username(SSL *s);
__owur char *SSL_get_srp_userinfo(SSL *s);
# endif

/*
 * ClientHello callback and helpers.
 */

# define SSL_CLIENT_HELLO_SUCCESS 1
# define SSL_CLIENT_HELLO_ERROR   0
# define SSL_CLIENT_HELLO_RETRY   (-1)

typedef int (*SSL_client_hello_cb_fn) (SSL *s, int *al, void *arg);
void SSL_CTX_set_client_hello_cb(SSL_CTX *c, SSL_client_hello_cb_fn cb,
                                 void *arg);
int SSL_client_hello_isv2(SSL *s);
unsigned int SSL_client_hello_get0_legacy_version(SSL *s);
size_t SSL_client_hello_get0_random(SSL *s, const unsigned char **out);
size_t SSL_client_hello_get0_session_id(SSL *s, const unsigned char **out);
size_t SSL_client_hello_get0_ciphers(SSL *s, const unsigned char **out);
size_t SSL_client_hello_get0_compression_methods(SSL *s,
                                                 const unsigned char **out);
int SSL_client_hello_get1_extensions_present(SSL *s, int **out, size_t *outlen);
int SSL_client_hello_get0_ext(SSL *s, unsigned int type,
                              const unsigned char **out, size_t *outlen);

void SSL_certs_clear(SSL *s);
void SSL_free(SSL *ssl);
# ifdef OSSL_ASYNC_FD
/*
 * Windows application developer has to include windows.h to use these.
 */
__owur int SSL_waiting_for_async(SSL *s);
__owur int SSL_get_all_async_fds(SSL *s, OSSL_ASYNC_FD *fds, size_t *numfds);
__owur int SSL_get_changed_async_fds(SSL *s, OSSL_ASYNC_FD *addfd,
                                     size_t *numaddfds, OSSL_ASYNC_FD *delfd,
                                     size_t *numdelfds);
# endif
__owur int SSL_accept(SSL *ssl);
__owur int SSL_stateless(SSL *s);
__owur int SSL_connect(SSL *ssl);
__owur int SSL_read(SSL *ssl, void *buf, int num);
__owur int SSL_read_ex(SSL *ssl, void *buf, size_t num, size_t *readbytes);

# define SSL_READ_EARLY_DATA_ERROR   0
# define SSL_READ_EARLY_DATA_SUCCESS 1
# define SSL_READ_EARLY_DATA_FINISH  2

__owur int SSL_read_early_data(SSL *s, void *buf, size_t num,
                               size_t *readbytes);
__owur int SSL_peek(SSL *ssl, void *buf, int num);
__owur int SSL_peek_ex(SSL *ssl, void *buf, size_t num, size_t *readbytes);
__owur int SSL_write(SSL *ssl, const void *buf, int num);
__owur int SSL_write_ex(SSL *s, const void *buf, size_t num, size_t *written);
__owur int SSL_write_early_data(SSL *s, const void *buf, size_t num,
                                size_t *written);
long SSL_ctrl(SSL *ssl, int cmd, long larg, void *parg);
long SSL_callback_ctrl(SSL *, int, void (*)(void));
long SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg);
long SSL_CTX_callback_ctrl(SSL_CTX *, int, void (*)(void));

# define SSL_EARLY_DATA_NOT_SENT    0
# define SSL_EARLY_DATA_REJECTED    1
# define SSL_EARLY_DATA_ACCEPTED    2

__owur int SSL_get_early_data_status(const SSL *s);

__owur int SSL_get_error(const SSL *s, int ret_code);
__owur const char *SSL_get_version(const SSL *s);

/* This sets the 'default' SSL version that SSL_new() will create */
__owur int SSL_CTX_set_ssl_version(SSL_CTX *ctx, const SSL_METHOD *meth);

# ifndef OPENSSL_NO_SSL3_METHOD
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *SSLv3_method(void)) /* SSLv3 */
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *SSLv3_server_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *SSLv3_client_method(void))
# endif

#define SSLv23_method           TLS_method
#define SSLv23_server_method    TLS_server_method
#define SSLv23_client_method    TLS_client_method

/* Negotiate highest available SSL/TLS version */
__owur const SSL_METHOD *TLS_method(void);
__owur const SSL_METHOD *TLS_server_method(void);
__owur const SSL_METHOD *TLS_client_method(void);

# ifndef OPENSSL_NO_TLS1_METHOD
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_method(void)) /* TLSv1.0 */
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_server_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_client_method(void))
# endif

# ifndef OPENSSL_NO_TLS1_1_METHOD
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_1_method(void)) /* TLSv1.1 */
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_1_server_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_1_client_method(void))
# endif

# ifndef OPENSSL_NO_TLS1_2_METHOD
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_2_method(void)) /* TLSv1.2 */
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_2_server_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *TLSv1_2_client_method(void))
# endif

# ifndef OPENSSL_NO_DTLS1_METHOD
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *DTLSv1_method(void)) /* DTLSv1.0 */
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *DTLSv1_server_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *DTLSv1_client_method(void))
# endif

# ifndef OPENSSL_NO_DTLS1_2_METHOD
/* DTLSv1.2 */
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *DTLSv1_2_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *DTLSv1_2_server_method(void))
DEPRECATEDIN_1_1_0(__owur const SSL_METHOD *DTLSv1_2_client_method(void))
# endif

__owur const SSL_METHOD *DTLS_method(void); /* DTLS 1.0 and 1.2 */
__owur const SSL_METHOD *DTLS_server_method(void); /* DTLS 1.0 and 1.2 */
__owur const SSL_METHOD *DTLS_client_method(void); /* DTLS 1.0 and 1.2 */

__owur size_t DTLS_get_data_mtu(const SSL *s);

__owur STACK_OF(SSL_CIPHER) *SSL_get_ciphers(const SSL *s);
__owur STACK_OF(SSL_CIPHER) *SSL_CTX_get_ciphers(const SSL_CTX *ctx);
__owur STACK_OF(SSL_CIPHER) *SSL_get_client_ciphers(const SSL *s);
__owur STACK_OF(SSL_CIPHER) *SSL_get1_supported_ciphers(SSL *s);

__owur int SSL_do_handshake(SSL *s);
int SSL_key_update(SSL *s, int updatetype);
int SSL_get_key_update_type(const SSL *s);
int SSL_renegotiate(SSL *s);
int SSL_renegotiate_abbreviated(SSL *s);
__owur int SSL_renegotiate_pending(const SSL *s);
int SSL_shutdown(SSL *s);
__owur int SSL_verify_client_post_handshake(SSL *s);
void SSL_CTX_set_post_handshake_auth(SSL_CTX *ctx, int val);
void SSL_set_post_handshake_auth(SSL *s, int val);

__owur const SSL_METHOD *SSL_CTX_get_ssl_method(const SSL_CTX *ctx);
__owur const SSL_METHOD *SSL_get_ssl_method(const SSL *s);
__owur int SSL_set_ssl_method(SSL *s, const SSL_METHOD *method);
__owur const char *SSL_alert_type_string_long(int value);
__owur const char *SSL_alert_type_string(int value);
__owur const char *SSL_alert_desc_string_long(int value);
__owur const char *SSL_alert_desc_string(int value);

void SSL_set0_CA_list(SSL *s, STACK_OF(X509_NAME) *name_list);
void SSL_CTX_set0_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list);
__owur const STACK_OF(X509_NAME) *SSL_get0_CA_list(const SSL *s);
__owur const STACK_OF(X509_NAME) *SSL_CTX_get0_CA_list(const SSL_CTX *ctx);
__owur int SSL_add1_to_CA_list(SSL *ssl, const X509 *x);
__owur int SSL_CTX_add1_to_CA_list(SSL_CTX *ctx, const X509 *x);
__owur const STACK_OF(X509_NAME) *SSL_get0_peer_CA_list(const SSL *s);

void SSL_set_client_CA_list(SSL *s, STACK_OF(X509_NAME) *name_list);
void SSL_CTX_set_client_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list);
__owur STACK_OF(X509_NAME) *SSL_get_client_CA_list(const SSL *s);
__owur STACK_OF(X509_NAME) *SSL_CTX_get_client_CA_list(const SSL_CTX *s);
__owur int SSL_add_client_CA(SSL *ssl, X509 *x);
__owur int SSL_CTX_add_client_CA(SSL_CTX *ctx, X509 *x);

void SSL_set_connect_state(SSL *s);
void SSL_set_accept_state(SSL *s);

__owur long SSL_get_default_timeout(const SSL *s);

# if OPENSSL_API_COMPAT < 0x10100000L
#  define SSL_library_init() OPENSSL_init_ssl(0, NULL)
# endif

__owur char *SSL_CIPHER_description(const SSL_CIPHER *, char *buf, int size);
__owur STACK_OF(X509_NAME) *SSL_dup_CA_list(const STACK_OF(X509_NAME) *sk);

__owur SSL *SSL_dup(SSL *ssl);

__owur X509 *SSL_get_certificate(const SSL *ssl);
/*
 * EVP_PKEY
 */
struct evp_pkey_st *SSL_get_privatekey(const SSL *ssl);

__owur X509 *SSL_CTX_get0_certificate(const SSL_CTX *ctx);
__owur EVP_PKEY *SSL_CTX_get0_privatekey(const SSL_CTX *ctx);

void SSL_CTX_set_quiet_shutdown(SSL_CTX *ctx, int mode);
__owur int SSL_CTX_get_quiet_shutdown(const SSL_CTX *ctx);
void SSL_set_quiet_shutdown(SSL *ssl, int mode);
__owur int SSL_get_quiet_shutdown(const SSL *ssl);
void SSL_set_shutdown(SSL *ssl, int mode);
__owur int SSL_get_shutdown(const SSL *ssl);
__owur int SSL_version(const SSL *ssl);
__owur int SSL_client_version(const SSL *s);
__owur int SSL_CTX_set_default_verify_paths(SSL_CTX *ctx);
__owur int SSL_CTX_set_default_verify_dir(SSL_CTX *ctx);
__owur int SSL_CTX_set_default_verify_file(SSL_CTX *ctx);
__owur int SSL_CTX_load_verify_locations(SSL_CTX *ctx, const char *CAfile,
                                         const char *CApath);
# define SSL_get0_session SSL_get_session/* just peek at pointer */
__owur SSL_SESSION *SSL_get_session(const SSL *ssl);
__owur SSL_SESSION *SSL_get1_session(SSL *ssl); /* obtain a reference count */
__owur SSL_CTX *SSL_get_SSL_CTX(const SSL *ssl);
SSL_CTX *SSL_set_SSL_CTX(SSL *ssl, SSL_CTX *ctx);
void SSL_set_info_callback(SSL *ssl,
                           void (*cb) (const SSL *ssl, int type, int val));
void (*SSL_get_info_callback(const SSL *ssl)) (const SSL *ssl, int type,
                                               int val);
__owur OSSL_HANDSHAKE_STATE SSL_get_state(const SSL *ssl);

void SSL_set_verify_result(SSL *ssl, long v);
__owur long SSL_get_verify_result(const SSL *ssl);
__owur STACK_OF(X509) *SSL_get0_verified_chain(const SSL *s);

__owur size_t SSL_get_client_random(const SSL *ssl, unsigned char *out,
                                    size_t outlen);
__owur size_t SSL_get_server_random(const SSL *ssl, unsigned char *out,
                                    size_t outlen);
__owur size_t SSL_SESSION_get_master_key(const SSL_SESSION *sess,
                                         unsigned char *out, size_t outlen);
__owur int SSL_SESSION_set1_master_key(SSL_SESSION *sess,
                                       const unsigned char *in, size_t len);
uint8_t SSL_SESSION_get_max_fragment_length(const SSL_SESSION *sess);

#define SSL_get_ex_new_index(l, p, newf, dupf, freef) \
    CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL, l, p, newf, dupf, freef)
__owur int SSL_set_ex_data(SSL *ssl, int idx, void *data);
void *SSL_get_ex_data(const SSL *ssl, int idx);
#define SSL_SESSION_get_ex_new_index(l, p, newf, dupf, freef) \
    CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL_SESSION, l, p, newf, dupf, freef)
__owur int SSL_SESSION_set_ex_data(SSL_SESSION *ss, int idx, void *data);
void *SSL_SESSION_get_ex_data(const SSL_SESSION *ss, int idx);
#define SSL_CTX_get_ex_new_index(l, p, newf, dupf, freef) \
    CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL_CTX, l, p, newf, dupf, freef)
__owur int SSL_CTX_set_ex_data(SSL_CTX *ssl, int idx, void *data);
void *SSL_CTX_get_ex_data(const SSL_CTX *ssl, int idx);

__owur int SSL_get_ex_data_X509_STORE_CTX_idx(void);

# define SSL_CTX_sess_set_cache_size(ctx,t) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_SESS_CACHE_SIZE,t,NULL)
# define SSL_CTX_sess_get_cache_size(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_SESS_CACHE_SIZE,0,NULL)
# define SSL_CTX_set_session_cache_mode(ctx,m) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_SESS_CACHE_MODE,m,NULL)
# define SSL_CTX_get_session_cache_mode(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_SESS_CACHE_MODE,0,NULL)

# define SSL_CTX_get_default_read_ahead(ctx) SSL_CTX_get_read_ahead(ctx)
# define SSL_CTX_set_default_read_ahead(ctx,m) SSL_CTX_set_read_ahead(ctx,m)
# define SSL_CTX_get_read_ahead(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_READ_AHEAD,0,NULL)
# define SSL_CTX_set_read_ahead(ctx,m) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_READ_AHEAD,m,NULL)
# define SSL_CTX_get_max_cert_list(ctx) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_GET_MAX_CERT_LIST,0,NULL)
# define SSL_CTX_set_max_cert_list(ctx,m) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_MAX_CERT_LIST,m,NULL)
# define SSL_get_max_cert_list(ssl) \
        SSL_ctrl(ssl,SSL_CTRL_GET_MAX_CERT_LIST,0,NULL)
# define SSL_set_max_cert_list(ssl,m) \
        SSL_ctrl(ssl,SSL_CTRL_SET_MAX_CERT_LIST,m,NULL)

# define SSL_CTX_set_max_send_fragment(ctx,m) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_MAX_SEND_FRAGMENT,m,NULL)
# define SSL_set_max_send_fragment(ssl,m) \
        SSL_ctrl(ssl,SSL_CTRL_SET_MAX_SEND_FRAGMENT,m,NULL)
# define SSL_CTX_set_split_send_fragment(ctx,m) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_SPLIT_SEND_FRAGMENT,m,NULL)
# define SSL_set_split_send_fragment(ssl,m) \
        SSL_ctrl(ssl,SSL_CTRL_SET_SPLIT_SEND_FRAGMENT,m,NULL)
# define SSL_CTX_set_max_pipelines(ctx,m) \
        SSL_CTX_ctrl(ctx,SSL_CTRL_SET_MAX_PIPELINES,m,NULL)
# define SSL_set_max_pipelines(ssl,m) \
        SSL_ctrl(ssl,SSL_CTRL_SET_MAX_PIPELINES,m,NULL)

void SSL_CTX_set_default_read_buffer_len(SSL_CTX *ctx, size_t len);
void SSL_set_default_read_buffer_len(SSL *s, size_t len);

# ifndef OPENSSL_NO_DH
/* NB: the |keylength| is only applicable when is_export is true */
void SSL_CTX_set_tmp_dh_callback(SSL_CTX *ctx,
                                 DH *(*dh) (SSL *ssl, int is_export,
                                            int keylength));
void SSL_set_tmp_dh_callback(SSL *ssl,
                             DH *(*dh) (SSL *ssl, int is_export,
                                        int keylength));
# endif

__owur const COMP_METHOD *SSL_get_current_compression(const SSL *s);
__owur const COMP_METHOD *SSL_get_current_expansion(const SSL *s);
__owur const char *SSL_COMP_get_name(const COMP_METHOD *comp);
__owur const char *SSL_COMP_get0_name(const SSL_COMP *comp);
__owur int SSL_COMP_get_id(const SSL_COMP *comp);
STACK_OF(SSL_COMP) *SSL_COMP_get_compression_methods(void);
__owur STACK_OF(SSL_COMP) *SSL_COMP_set0_compression_methods(STACK_OF(SSL_COMP)
                                                             *meths);
# if OPENSSL_API_COMPAT < 0x10100000L
#  define SSL_COMP_free_compression_methods() while(0) continue
# endif
__owur int SSL_COMP_add_compression_method(int id, COMP_METHOD *cm);

const SSL_CIPHER *SSL_CIPHER_find(SSL *ssl, const unsigned char *ptr);
int SSL_CIPHER_get_cipher_nid(const SSL_CIPHER *c);
int SSL_CIPHER_get_digest_nid(const SSL_CIPHER *c);
int SSL_bytes_to_cipher_list(SSL *s, const unsigned char *bytes, size_t len,
                             int isv2format, STACK_OF(SSL_CIPHER) **sk,
                             STACK_OF(SSL_CIPHER) **scsvs);

/* TLS extensions functions */
__owur int SSL_set_session_ticket_ext(SSL *s, void *ext_data, int ext_len);

__owur int SSL_set_session_ticket_ext_cb(SSL *s,
                                         tls_session_ticket_ext_cb_fn cb,
                                         void *arg);

/* Pre-shared secret session resumption functions */
__owur int SSL_set_session_secret_cb(SSL *s,
                                     tls_session_secret_cb_fn session_secret_cb,
                                     void *arg);

void SSL_CTX_set_not_resumable_session_callback(SSL_CTX *ctx,
                                                int (*cb) (SSL *ssl,
                                                           int
                                                           is_forward_secure));

void SSL_set_not_resumable_session_callback(SSL *ssl,
                                            int (*cb) (SSL *ssl,
                                                       int is_forward_secure));

void SSL_CTX_set_record_padding_callback(SSL_CTX *ctx,
                                         size_t (*cb) (SSL *ssl, int type,
                                                       size_t len, void *arg));
void SSL_CTX_set_record_padding_callback_arg(SSL_CTX *ctx, void *arg);
void *SSL_CTX_get_record_padding_callback_arg(const SSL_CTX *ctx);
int SSL_CTX_set_block_padding(SSL_CTX *ctx, size_t block_size);

void SSL_set_record_padding_callback(SSL *ssl,
                                    size_t (*cb) (SSL *ssl, int type,
                                                  size_t len, void *arg));
void SSL_set_record_padding_callback_arg(SSL *ssl, void *arg);
void *SSL_get_record_padding_callback_arg(const SSL *ssl);
int SSL_set_block_padding(SSL *ssl, size_t block_size);

int SSL_set_num_tickets(SSL *s, size_t num_tickets);
size_t SSL_get_num_tickets(const SSL *s);
int SSL_CTX_set_num_tickets(SSL_CTX *ctx, size_t num_tickets);
size_t SSL_CTX_get_num_tickets(const SSL_CTX *ctx);

# if OPENSSL_API_COMPAT < 0x10100000L
#  define SSL_cache_hit(s) SSL_session_reused(s)
# endif

__owur int SSL_session_reused(SSL *s);
__owur int SSL_is_server(const SSL *s);

__owur __owur SSL_CONF_CTX *SSL_CONF_CTX_new(void);
int SSL_CONF_CTX_finish(SSL_CONF_CTX *cctx);
void SSL_CONF_CTX_free(SSL_CONF_CTX *cctx);
unsigned int SSL_CONF_CTX_set_flags(SSL_CONF_CTX *cctx, unsigned int flags);
__owur unsigned int SSL_CONF_CTX_clear_flags(SSL_CONF_CTX *cctx,
                                             unsigned int flags);
__owur int SSL_CONF_CTX_set1_prefix(SSL_CONF_CTX *cctx, const char *pre);

void SSL_CONF_CTX_set_ssl(SSL_CONF_CTX *cctx, SSL *ssl);
void SSL_CONF_CTX_set_ssl_ctx(SSL_CONF_CTX *cctx, SSL_CTX *ctx);

__owur int SSL_CONF_cmd(SSL_CONF_CTX *cctx, const char *cmd, const char *value);
__owur int SSL_CONF_cmd_argv(SSL_CONF_CTX *cctx, int *pargc, char ***pargv);
__owur int SSL_CONF_cmd_value_type(SSL_CONF_CTX *cctx, const char *cmd);

void SSL_add_ssl_module(void);
int SSL_config(SSL *s, const char *name);
int SSL_CTX_config(SSL_CTX *ctx, const char *name);

# ifndef OPENSSL_NO_SSL_TRACE
void SSL_trace(int write_p, int version, int content_type,
               const void *buf, size_t len, SSL *ssl, void *arg);
# endif

# ifndef OPENSSL_NO_SOCK
int DTLSv1_listen(SSL *s, BIO_ADDR *client);
# endif

# ifndef OPENSSL_NO_CT

/*
 * A callback for verifying that the received SCTs are sufficient.
 * Expected to return 1 if they are sufficient, otherwise 0.
 * May return a negative integer if an error occurs.
 * A connection should be aborted if the SCTs are deemed insufficient.
 */
typedef int (*ssl_ct_validation_cb)(const CT_POLICY_EVAL_CTX *ctx,
                                    const STACK_OF(SCT) *scts, void *arg);

/*
 * Sets a |callback| that is invoked upon receipt of ServerHelloDone to validate
 * the received SCTs.
 * If the callback returns a non-positive result, the connection is terminated.
 * Call this function before beginning a handshake.
 * If a NULL |callback| is provided, SCT validation is disabled.
 * |arg| is arbitrary userdata that will be passed to the callback whenever it
 * is invoked. Ownership of |arg| remains with the caller.
 *
 * NOTE: A side-effect of setting a CT callback is that an OCSP stapled response
 *       will be requested.
 */
int SSL_set_ct_validation_callback(SSL *s, ssl_ct_validation_cb callback,
                                   void *arg);
int SSL_CTX_set_ct_validation_callback(SSL_CTX *ctx,
                                       ssl_ct_validation_cb callback,
                                       void *arg);
#define SSL_disable_ct(s) \
        ((void) SSL_set_validation_callback((s), NULL, NULL))
#define SSL_CTX_disable_ct(ctx) \
        ((void) SSL_CTX_set_validation_callback((ctx), NULL, NULL))

/*
 * The validation type enumerates the available behaviours of the built-in SSL
 * CT validation callback selected via SSL_enable_ct() and SSL_CTX_enable_ct().
 * The underlying callback is a static function in libssl.
 */
enum {
    SSL_CT_VALIDATION_PERMISSIVE = 0,
    SSL_CT_VALIDATION_STRICT
};

/*
 * Enable CT by setting up a callback that implements one of the built-in
 * validation variants.  The SSL_CT_VALIDATION_PERMISSIVE variant always
 * continues the handshake, the application can make appropriate decisions at
 * handshake completion.  The SSL_CT_VALIDATION_STRICT variant requires at
 * least one valid SCT, or else handshake termination will be requested.  The
 * handshake may continue anyway if SSL_VERIFY_NONE is in effect.
 */
int SSL_enable_ct(SSL *s, int validation_mode);
int SSL_CTX_enable_ct(SSL_CTX *ctx, int validation_mode);

/*
 * Report whether a non-NULL callback is enabled.
 */
int SSL_ct_is_enabled(const SSL *s);
int SSL_CTX_ct_is_enabled(const SSL_CTX *ctx);

/* Gets the SCTs received from a connection */
const STACK_OF(SCT) *SSL_get0_peer_scts(SSL *s);

/*
 * Loads the CT log list from the default location.
 * If a CTLOG_STORE has previously been set using SSL_CTX_set_ctlog_store,
 * the log information loaded from this file will be appended to the
 * CTLOG_STORE.
 * Returns 1 on success, 0 otherwise.
 */
int SSL_CTX_set_default_ctlog_list_file(SSL_CTX *ctx);

/*
 * Loads the CT log list from the specified file path.
 * If a CTLOG_STORE has previously been set using SSL_CTX_set_ctlog_store,
 * the log information loaded from this file will be appended to the
 * CTLOG_STORE.
 * Returns 1 on success, 0 otherwise.
 */
int SSL_CTX_set_ctlog_list_file(SSL_CTX *ctx, const char *path);

/*
 * Sets the CT log list used by all SSL connections created from this SSL_CTX.
 * Ownership of the CTLOG_STORE is transferred to the SSL_CTX.
 */
void SSL_CTX_set0_ctlog_store(SSL_CTX *ctx, CTLOG_STORE *logs);

/*
 * Gets the CT log list used by all SSL connections created from this SSL_CTX.
 * This will be NULL unless one of the following functions has been called:
 * - SSL_CTX_set_default_ctlog_list_file
 * - SSL_CTX_set_ctlog_list_file
 * - SSL_CTX_set_ctlog_store
 */
const CTLOG_STORE *SSL_CTX_get0_ctlog_store(const SSL_CTX *ctx);

# endif /* OPENSSL_NO_CT */

/* What the "other" parameter contains in security callback */
/* Mask for type */
# define SSL_SECOP_OTHER_TYPE    0xffff0000
# define SSL_SECOP_OTHER_NONE    0
# define SSL_SECOP_OTHER_CIPHER  (1 << 16)
# define SSL_SECOP_OTHER_CURVE   (2 << 16)
# define SSL_SECOP_OTHER_DH      (3 << 16)
# define SSL_SECOP_OTHER_PKEY    (4 << 16)
# define SSL_SECOP_OTHER_SIGALG  (5 << 16)
# define SSL_SECOP_OTHER_CERT    (6 << 16)

/* Indicated operation refers to peer key or certificate */
# define SSL_SECOP_PEER          0x1000

/* Values for "op" parameter in security callback */

/* Called to filter ciphers */
/* Ciphers client supports */
# define SSL_SECOP_CIPHER_SUPPORTED      (1 | SSL_SECOP_OTHER_CIPHER)
/* Cipher shared by client/server */
# define SSL_SECOP_CIPHER_SHARED         (2 | SSL_SECOP_OTHER_CIPHER)
/* Sanity check of cipher server selects */
# define SSL_SECOP_CIPHER_CHECK          (3 | SSL_SECOP_OTHER_CIPHER)
/* Curves supported by client */
# define SSL_SECOP_CURVE_SUPPORTED       (4 | SSL_SECOP_OTHER_CURVE)
/* Curves shared by client/server */
# define SSL_SECOP_CURVE_SHARED          (5 | SSL_SECOP_OTHER_CURVE)
/* Sanity check of curve server selects */
# define SSL_SECOP_CURVE_CHECK           (6 | SSL_SECOP_OTHER_CURVE)
/* Temporary DH key */
# define SSL_SECOP_TMP_DH                (7 | SSL_SECOP_OTHER_PKEY)
/* SSL/TLS version */
# define SSL_SECOP_VERSION               (9 | SSL_SECOP_OTHER_NONE)
/* Session tickets */
# define SSL_SECOP_TICKET                (10 | SSL_SECOP_OTHER_NONE)
/* Supported signature algorithms sent to peer */
# define SSL_SECOP_SIGALG_SUPPORTED      (11 | SSL_SECOP_OTHER_SIGALG)
/* Shared signature algorithm */
# define SSL_SECOP_SIGALG_SHARED         (12 | SSL_SECOP_OTHER_SIGALG)
/* Sanity check signature algorithm allowed */
# define SSL_SECOP_SIGALG_CHECK          (13 | SSL_SECOP_OTHER_SIGALG)
/* Used to get mask of supported public key signature algorithms */
# define SSL_SECOP_SIGALG_MASK           (14 | SSL_SECOP_OTHER_SIGALG)
/* Use to see if compression is allowed */
# define SSL_SECOP_COMPRESSION           (15 | SSL_SECOP_OTHER_NONE)
/* EE key in certificate */
# define SSL_SECOP_EE_KEY                (16 | SSL_SECOP_OTHER_CERT)
/* CA key in certificate */
# define SSL_SECOP_CA_KEY                (17 | SSL_SECOP_OTHER_CERT)
/* CA digest algorithm in certificate */
# define SSL_SECOP_CA_MD                 (18 | SSL_SECOP_OTHER_CERT)
/* Peer EE key in certificate */
# define SSL_SECOP_PEER_EE_KEY           (SSL_SECOP_EE_KEY | SSL_SECOP_PEER)
/* Peer CA key in certificate */
# define SSL_SECOP_PEER_CA_KEY           (SSL_SECOP_CA_KEY | SSL_SECOP_PEER)
/* Peer CA digest algorithm in certificate */
# define SSL_SECOP_PEER_CA_MD            (SSL_SECOP_CA_MD | SSL_SECOP_PEER)

void SSL_set_security_level(SSL *s, int level);
__owur int SSL_get_security_level(const SSL *s);
void SSL_set_security_callback(SSL *s,
                               int (*cb) (const SSL *s, const SSL_CTX *ctx,
                                          int op, int bits, int nid,
                                          void *other, void *ex));
int (*SSL_get_security_callback(const SSL *s)) (const SSL *s,
                                                const SSL_CTX *ctx, int op,
                                                int bits, int nid, void *other,
                                                void *ex);
void SSL_set0_security_ex_data(SSL *s, void *ex);
__owur void *SSL_get0_security_ex_data(const SSL *s);

void SSL_CTX_set_security_level(SSL_CTX *ctx, int level);
__owur int SSL_CTX_get_security_level(const SSL_CTX *ctx);
void SSL_CTX_set_security_callback(SSL_CTX *ctx,
                                   int (*cb) (const SSL *s, const SSL_CTX *ctx,
                                              int op, int bits, int nid,
                                              void *other, void *ex));
int (*SSL_CTX_get_security_callback(const SSL_CTX *ctx)) (const SSL *s,
                                                          const SSL_CTX *ctx,
                                                          int op, int bits,
                                                          int nid,
                                                          void *other,
                                                          void *ex);
void SSL_CTX_set0_security_ex_data(SSL_CTX *ctx, void *ex);
__owur void *SSL_CTX_get0_security_ex_data(const SSL_CTX *ctx);

/* OPENSSL_INIT flag 0x010000 reserved for internal use */
# define OPENSSL_INIT_NO_LOAD_SSL_STRINGS    0x00100000L
# define OPENSSL_INIT_LOAD_SSL_STRINGS       0x00200000L

# define OPENSSL_INIT_SSL_DEFAULT \
        (OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS)

int OPENSSL_init_ssl(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings);

# ifndef OPENSSL_NO_UNIT_TEST
__owur const struct openssl_ssl_test_functions *SSL_test_functions(void);
# endif

__owur int SSL_free_buffers(SSL *ssl);
__owur int SSL_alloc_buffers(SSL *ssl);

/* Status codes passed to the decrypt session ticket callback. Some of these
 * are for internal use only and are never passed to the callback. */
typedef int SSL_TICKET_STATUS;

/* Support for ticket appdata */
/* fatal error, malloc failure */
# define SSL_TICKET_FATAL_ERR_MALLOC 0
/* fatal error, either from parsing or decrypting the ticket */
# define SSL_TICKET_FATAL_ERR_OTHER  1
/* No ticket present */
# define SSL_TICKET_NONE             2
/* Empty ticket present */
# define SSL_TICKET_EMPTY            3
/* the ticket couldn't be decrypted */
# define SSL_TICKET_NO_DECRYPT       4
/* a ticket was successfully decrypted */
# define SSL_TICKET_SUCCESS          5
/* same as above but the ticket needs to be renewed */
# define SSL_TICKET_SUCCESS_RENEW    6

/* Return codes for the decrypt session ticket callback */
typedef int SSL_TICKET_RETURN;

/* An error occurred */
#define SSL_TICKET_RETURN_ABORT             0
/* Do not use the ticket, do not send a renewed ticket to the client */
#define SSL_TICKET_RETURN_IGNORE            1
/* Do not use the ticket, send a renewed ticket to the client */
#define SSL_TICKET_RETURN_IGNORE_RENEW      2
/* Use the ticket, do not send a renewed ticket to the client */
#define SSL_TICKET_RETURN_USE               3
/* Use the ticket, send a renewed ticket to the client */
#define SSL_TICKET_RETURN_USE_RENEW         4

typedef int (*SSL_CTX_generate_session_ticket_fn)(SSL *s, void *arg);
typedef SSL_TICKET_RETURN (*SSL_CTX_decrypt_session_ticket_fn)(SSL *s, SSL_SESSION *ss,
                                                               const unsigned char *keyname,
                                                               size_t keyname_length,
                                                               SSL_TICKET_STATUS status,
                                                               void *arg);
int SSL_CTX_set_session_ticket_cb(SSL_CTX *ctx,
                                  SSL_CTX_generate_session_ticket_fn gen_cb,
                                  SSL_CTX_decrypt_session_ticket_fn dec_cb,
                                  void *arg);
int SSL_SESSION_set1_ticket_appdata(SSL_SESSION *ss, const void *data, size_t len);
int SSL_SESSION_get0_ticket_appdata(SSL_SESSION *ss, void **data, size_t *len);

extern const char SSL_version_str[];

typedef unsigned int (*DTLS_timer_cb)(SSL *s, unsigned int timer_us);

void DTLS_set_timer_cb(SSL *s, DTLS_timer_cb cb);


typedef int (*SSL_allow_early_data_cb_fn)(SSL *s, void *arg);
void SSL_CTX_set_allow_early_data_cb(SSL_CTX *ctx,
                                     SSL_allow_early_data_cb_fn cb,
                                     void *arg);
void SSL_set_allow_early_data_cb(SSL *s,
                                 SSL_allow_early_data_cb_fn cb,
                                 void *arg);

# ifdef  __cplusplus
}
# endif
#endif
