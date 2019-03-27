/*
 * Copyright 2012-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "ssl_locl.h"

#ifndef OPENSSL_NO_SSL_TRACE

/* Packet trace support for OpenSSL */

typedef struct {
    int num;
    const char *name;
} ssl_trace_tbl;

# define ssl_trace_str(val, tbl) \
    do_ssl_trace_str(val, tbl, OSSL_NELEM(tbl))

# define ssl_trace_list(bio, indent, msg, msglen, value, table) \
    do_ssl_trace_list(bio, indent, msg, msglen, value, \
                      table, OSSL_NELEM(table))

static const char *do_ssl_trace_str(int val, const ssl_trace_tbl *tbl,
                                    size_t ntbl)
{
    size_t i;

    for (i = 0; i < ntbl; i++, tbl++) {
        if (tbl->num == val)
            return tbl->name;
    }
    return "UNKNOWN";
}

static int do_ssl_trace_list(BIO *bio, int indent,
                             const unsigned char *msg, size_t msglen,
                             size_t vlen, const ssl_trace_tbl *tbl, size_t ntbl)
{
    int val;

    if (msglen % vlen)
        return 0;
    while (msglen) {
        val = msg[0];
        if (vlen == 2)
            val = (val << 8) | msg[1];
        BIO_indent(bio, indent, 80);
        BIO_printf(bio, "%s (%d)\n", do_ssl_trace_str(val, tbl, ntbl), val);
        msg += vlen;
        msglen -= vlen;
    }
    return 1;
}

/* Version number */

static const ssl_trace_tbl ssl_version_tbl[] = {
    {SSL3_VERSION, "SSL 3.0"},
    {TLS1_VERSION, "TLS 1.0"},
    {TLS1_1_VERSION, "TLS 1.1"},
    {TLS1_2_VERSION, "TLS 1.2"},
    {TLS1_3_VERSION, "TLS 1.3"},
    {DTLS1_VERSION, "DTLS 1.0"},
    {DTLS1_2_VERSION, "DTLS 1.2"},
    {DTLS1_BAD_VER, "DTLS 1.0 (bad)"}
};

static const ssl_trace_tbl ssl_content_tbl[] = {
    {SSL3_RT_CHANGE_CIPHER_SPEC, "ChangeCipherSpec"},
    {SSL3_RT_ALERT, "Alert"},
    {SSL3_RT_HANDSHAKE, "Handshake"},
    {SSL3_RT_APPLICATION_DATA, "ApplicationData"},
};

/* Handshake types, sorted by ascending id  */
static const ssl_trace_tbl ssl_handshake_tbl[] = {
    {SSL3_MT_HELLO_REQUEST, "HelloRequest"},
    {SSL3_MT_CLIENT_HELLO, "ClientHello"},
    {SSL3_MT_SERVER_HELLO, "ServerHello"},
    {DTLS1_MT_HELLO_VERIFY_REQUEST, "HelloVerifyRequest"},
    {SSL3_MT_NEWSESSION_TICKET, "NewSessionTicket"},
    {SSL3_MT_END_OF_EARLY_DATA, "EndOfEarlyData"},
    {SSL3_MT_ENCRYPTED_EXTENSIONS, "EncryptedExtensions"},
    {SSL3_MT_CERTIFICATE, "Certificate"},
    {SSL3_MT_SERVER_KEY_EXCHANGE, "ServerKeyExchange"},
    {SSL3_MT_CERTIFICATE_REQUEST, "CertificateRequest"},
    {SSL3_MT_SERVER_DONE, "ServerHelloDone"},
    {SSL3_MT_CERTIFICATE_VERIFY, "CertificateVerify"},
    {SSL3_MT_CLIENT_KEY_EXCHANGE, "ClientKeyExchange"},
    {SSL3_MT_FINISHED, "Finished"},
    {SSL3_MT_CERTIFICATE_URL, "CertificateUrl"},
    {SSL3_MT_CERTIFICATE_STATUS, "CertificateStatus"},
    {SSL3_MT_SUPPLEMENTAL_DATA, "SupplementalData"},
    {SSL3_MT_KEY_UPDATE, "KeyUpdate"},
# ifndef OPENSSL_NO_NEXTPROTONEG
    {SSL3_MT_NEXT_PROTO, "NextProto"},
# endif
    {SSL3_MT_MESSAGE_HASH, "MessageHash"}
};

/* Cipher suites */
static const ssl_trace_tbl ssl_ciphers_tbl[] = {
    {0x0000, "TLS_NULL_WITH_NULL_NULL"},
    {0x0001, "TLS_RSA_WITH_NULL_MD5"},
    {0x0002, "TLS_RSA_WITH_NULL_SHA"},
    {0x0003, "TLS_RSA_EXPORT_WITH_RC4_40_MD5"},
    {0x0004, "TLS_RSA_WITH_RC4_128_MD5"},
    {0x0005, "TLS_RSA_WITH_RC4_128_SHA"},
    {0x0006, "TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5"},
    {0x0007, "TLS_RSA_WITH_IDEA_CBC_SHA"},
    {0x0008, "TLS_RSA_EXPORT_WITH_DES40_CBC_SHA"},
    {0x0009, "TLS_RSA_WITH_DES_CBC_SHA"},
    {0x000A, "TLS_RSA_WITH_3DES_EDE_CBC_SHA"},
    {0x000B, "TLS_DH_DSS_EXPORT_WITH_DES40_CBC_SHA"},
    {0x000C, "TLS_DH_DSS_WITH_DES_CBC_SHA"},
    {0x000D, "TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA"},
    {0x000E, "TLS_DH_RSA_EXPORT_WITH_DES40_CBC_SHA"},
    {0x000F, "TLS_DH_RSA_WITH_DES_CBC_SHA"},
    {0x0010, "TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA"},
    {0x0011, "TLS_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA"},
    {0x0012, "TLS_DHE_DSS_WITH_DES_CBC_SHA"},
    {0x0013, "TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA"},
    {0x0014, "TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA"},
    {0x0015, "TLS_DHE_RSA_WITH_DES_CBC_SHA"},
    {0x0016, "TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA"},
    {0x0017, "TLS_DH_anon_EXPORT_WITH_RC4_40_MD5"},
    {0x0018, "TLS_DH_anon_WITH_RC4_128_MD5"},
    {0x0019, "TLS_DH_anon_EXPORT_WITH_DES40_CBC_SHA"},
    {0x001A, "TLS_DH_anon_WITH_DES_CBC_SHA"},
    {0x001B, "TLS_DH_anon_WITH_3DES_EDE_CBC_SHA"},
    {0x001D, "SSL_FORTEZZA_KEA_WITH_FORTEZZA_CBC_SHA"},
    {0x001E, "SSL_FORTEZZA_KEA_WITH_RC4_128_SHA"},
    {0x001F, "TLS_KRB5_WITH_3DES_EDE_CBC_SHA"},
    {0x0020, "TLS_KRB5_WITH_RC4_128_SHA"},
    {0x0021, "TLS_KRB5_WITH_IDEA_CBC_SHA"},
    {0x0022, "TLS_KRB5_WITH_DES_CBC_MD5"},
    {0x0023, "TLS_KRB5_WITH_3DES_EDE_CBC_MD5"},
    {0x0024, "TLS_KRB5_WITH_RC4_128_MD5"},
    {0x0025, "TLS_KRB5_WITH_IDEA_CBC_MD5"},
    {0x0026, "TLS_KRB5_EXPORT_WITH_DES_CBC_40_SHA"},
    {0x0027, "TLS_KRB5_EXPORT_WITH_RC2_CBC_40_SHA"},
    {0x0028, "TLS_KRB5_EXPORT_WITH_RC4_40_SHA"},
    {0x0029, "TLS_KRB5_EXPORT_WITH_DES_CBC_40_MD5"},
    {0x002A, "TLS_KRB5_EXPORT_WITH_RC2_CBC_40_MD5"},
    {0x002B, "TLS_KRB5_EXPORT_WITH_RC4_40_MD5"},
    {0x002C, "TLS_PSK_WITH_NULL_SHA"},
    {0x002D, "TLS_DHE_PSK_WITH_NULL_SHA"},
    {0x002E, "TLS_RSA_PSK_WITH_NULL_SHA"},
    {0x002F, "TLS_RSA_WITH_AES_128_CBC_SHA"},
    {0x0030, "TLS_DH_DSS_WITH_AES_128_CBC_SHA"},
    {0x0031, "TLS_DH_RSA_WITH_AES_128_CBC_SHA"},
    {0x0032, "TLS_DHE_DSS_WITH_AES_128_CBC_SHA"},
    {0x0033, "TLS_DHE_RSA_WITH_AES_128_CBC_SHA"},
    {0x0034, "TLS_DH_anon_WITH_AES_128_CBC_SHA"},
    {0x0035, "TLS_RSA_WITH_AES_256_CBC_SHA"},
    {0x0036, "TLS_DH_DSS_WITH_AES_256_CBC_SHA"},
    {0x0037, "TLS_DH_RSA_WITH_AES_256_CBC_SHA"},
    {0x0038, "TLS_DHE_DSS_WITH_AES_256_CBC_SHA"},
    {0x0039, "TLS_DHE_RSA_WITH_AES_256_CBC_SHA"},
    {0x003A, "TLS_DH_anon_WITH_AES_256_CBC_SHA"},
    {0x003B, "TLS_RSA_WITH_NULL_SHA256"},
    {0x003C, "TLS_RSA_WITH_AES_128_CBC_SHA256"},
    {0x003D, "TLS_RSA_WITH_AES_256_CBC_SHA256"},
    {0x003E, "TLS_DH_DSS_WITH_AES_128_CBC_SHA256"},
    {0x003F, "TLS_DH_RSA_WITH_AES_128_CBC_SHA256"},
    {0x0040, "TLS_DHE_DSS_WITH_AES_128_CBC_SHA256"},
    {0x0041, "TLS_RSA_WITH_CAMELLIA_128_CBC_SHA"},
    {0x0042, "TLS_DH_DSS_WITH_CAMELLIA_128_CBC_SHA"},
    {0x0043, "TLS_DH_RSA_WITH_CAMELLIA_128_CBC_SHA"},
    {0x0044, "TLS_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA"},
    {0x0045, "TLS_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA"},
    {0x0046, "TLS_DH_anon_WITH_CAMELLIA_128_CBC_SHA"},
    {0x0067, "TLS_DHE_RSA_WITH_AES_128_CBC_SHA256"},
    {0x0068, "TLS_DH_DSS_WITH_AES_256_CBC_SHA256"},
    {0x0069, "TLS_DH_RSA_WITH_AES_256_CBC_SHA256"},
    {0x006A, "TLS_DHE_DSS_WITH_AES_256_CBC_SHA256"},
    {0x006B, "TLS_DHE_RSA_WITH_AES_256_CBC_SHA256"},
    {0x006C, "TLS_DH_anon_WITH_AES_128_CBC_SHA256"},
    {0x006D, "TLS_DH_anon_WITH_AES_256_CBC_SHA256"},
    {0x0081, "TLS_GOSTR341001_WITH_28147_CNT_IMIT"},
    {0x0083, "TLS_GOSTR341001_WITH_NULL_GOSTR3411"},
    {0x0084, "TLS_RSA_WITH_CAMELLIA_256_CBC_SHA"},
    {0x0085, "TLS_DH_DSS_WITH_CAMELLIA_256_CBC_SHA"},
    {0x0086, "TLS_DH_RSA_WITH_CAMELLIA_256_CBC_SHA"},
    {0x0087, "TLS_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA"},
    {0x0088, "TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA"},
    {0x0089, "TLS_DH_anon_WITH_CAMELLIA_256_CBC_SHA"},
    {0x008A, "TLS_PSK_WITH_RC4_128_SHA"},
    {0x008B, "TLS_PSK_WITH_3DES_EDE_CBC_SHA"},
    {0x008C, "TLS_PSK_WITH_AES_128_CBC_SHA"},
    {0x008D, "TLS_PSK_WITH_AES_256_CBC_SHA"},
    {0x008E, "TLS_DHE_PSK_WITH_RC4_128_SHA"},
    {0x008F, "TLS_DHE_PSK_WITH_3DES_EDE_CBC_SHA"},
    {0x0090, "TLS_DHE_PSK_WITH_AES_128_CBC_SHA"},
    {0x0091, "TLS_DHE_PSK_WITH_AES_256_CBC_SHA"},
    {0x0092, "TLS_RSA_PSK_WITH_RC4_128_SHA"},
    {0x0093, "TLS_RSA_PSK_WITH_3DES_EDE_CBC_SHA"},
    {0x0094, "TLS_RSA_PSK_WITH_AES_128_CBC_SHA"},
    {0x0095, "TLS_RSA_PSK_WITH_AES_256_CBC_SHA"},
    {0x0096, "TLS_RSA_WITH_SEED_CBC_SHA"},
    {0x0097, "TLS_DH_DSS_WITH_SEED_CBC_SHA"},
    {0x0098, "TLS_DH_RSA_WITH_SEED_CBC_SHA"},
    {0x0099, "TLS_DHE_DSS_WITH_SEED_CBC_SHA"},
    {0x009A, "TLS_DHE_RSA_WITH_SEED_CBC_SHA"},
    {0x009B, "TLS_DH_anon_WITH_SEED_CBC_SHA"},
    {0x009C, "TLS_RSA_WITH_AES_128_GCM_SHA256"},
    {0x009D, "TLS_RSA_WITH_AES_256_GCM_SHA384"},
    {0x009E, "TLS_DHE_RSA_WITH_AES_128_GCM_SHA256"},
    {0x009F, "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"},
    {0x00A0, "TLS_DH_RSA_WITH_AES_128_GCM_SHA256"},
    {0x00A1, "TLS_DH_RSA_WITH_AES_256_GCM_SHA384"},
    {0x00A2, "TLS_DHE_DSS_WITH_AES_128_GCM_SHA256"},
    {0x00A3, "TLS_DHE_DSS_WITH_AES_256_GCM_SHA384"},
    {0x00A4, "TLS_DH_DSS_WITH_AES_128_GCM_SHA256"},
    {0x00A5, "TLS_DH_DSS_WITH_AES_256_GCM_SHA384"},
    {0x00A6, "TLS_DH_anon_WITH_AES_128_GCM_SHA256"},
    {0x00A7, "TLS_DH_anon_WITH_AES_256_GCM_SHA384"},
    {0x00A8, "TLS_PSK_WITH_AES_128_GCM_SHA256"},
    {0x00A9, "TLS_PSK_WITH_AES_256_GCM_SHA384"},
    {0x00AA, "TLS_DHE_PSK_WITH_AES_128_GCM_SHA256"},
    {0x00AB, "TLS_DHE_PSK_WITH_AES_256_GCM_SHA384"},
    {0x00AC, "TLS_RSA_PSK_WITH_AES_128_GCM_SHA256"},
    {0x00AD, "TLS_RSA_PSK_WITH_AES_256_GCM_SHA384"},
    {0x00AE, "TLS_PSK_WITH_AES_128_CBC_SHA256"},
    {0x00AF, "TLS_PSK_WITH_AES_256_CBC_SHA384"},
    {0x00B0, "TLS_PSK_WITH_NULL_SHA256"},
    {0x00B1, "TLS_PSK_WITH_NULL_SHA384"},
    {0x00B2, "TLS_DHE_PSK_WITH_AES_128_CBC_SHA256"},
    {0x00B3, "TLS_DHE_PSK_WITH_AES_256_CBC_SHA384"},
    {0x00B4, "TLS_DHE_PSK_WITH_NULL_SHA256"},
    {0x00B5, "TLS_DHE_PSK_WITH_NULL_SHA384"},
    {0x00B6, "TLS_RSA_PSK_WITH_AES_128_CBC_SHA256"},
    {0x00B7, "TLS_RSA_PSK_WITH_AES_256_CBC_SHA384"},
    {0x00B8, "TLS_RSA_PSK_WITH_NULL_SHA256"},
    {0x00B9, "TLS_RSA_PSK_WITH_NULL_SHA384"},
    {0x00BA, "TLS_RSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0x00BB, "TLS_DH_DSS_WITH_CAMELLIA_128_CBC_SHA256"},
    {0x00BC, "TLS_DH_RSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0x00BD, "TLS_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA256"},
    {0x00BE, "TLS_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0x00BF, "TLS_DH_anon_WITH_CAMELLIA_128_CBC_SHA256"},
    {0x00C0, "TLS_RSA_WITH_CAMELLIA_256_CBC_SHA256"},
    {0x00C1, "TLS_DH_DSS_WITH_CAMELLIA_256_CBC_SHA256"},
    {0x00C2, "TLS_DH_RSA_WITH_CAMELLIA_256_CBC_SHA256"},
    {0x00C3, "TLS_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA256"},
    {0x00C4, "TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA256"},
    {0x00C5, "TLS_DH_anon_WITH_CAMELLIA_256_CBC_SHA256"},
    {0x00FF, "TLS_EMPTY_RENEGOTIATION_INFO_SCSV"},
    {0x5600, "TLS_FALLBACK_SCSV"},
    {0xC001, "TLS_ECDH_ECDSA_WITH_NULL_SHA"},
    {0xC002, "TLS_ECDH_ECDSA_WITH_RC4_128_SHA"},
    {0xC003, "TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA"},
    {0xC004, "TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA"},
    {0xC005, "TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA"},
    {0xC006, "TLS_ECDHE_ECDSA_WITH_NULL_SHA"},
    {0xC007, "TLS_ECDHE_ECDSA_WITH_RC4_128_SHA"},
    {0xC008, "TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA"},
    {0xC009, "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA"},
    {0xC00A, "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA"},
    {0xC00B, "TLS_ECDH_RSA_WITH_NULL_SHA"},
    {0xC00C, "TLS_ECDH_RSA_WITH_RC4_128_SHA"},
    {0xC00D, "TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA"},
    {0xC00E, "TLS_ECDH_RSA_WITH_AES_128_CBC_SHA"},
    {0xC00F, "TLS_ECDH_RSA_WITH_AES_256_CBC_SHA"},
    {0xC010, "TLS_ECDHE_RSA_WITH_NULL_SHA"},
    {0xC011, "TLS_ECDHE_RSA_WITH_RC4_128_SHA"},
    {0xC012, "TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA"},
    {0xC013, "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA"},
    {0xC014, "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA"},
    {0xC015, "TLS_ECDH_anon_WITH_NULL_SHA"},
    {0xC016, "TLS_ECDH_anon_WITH_RC4_128_SHA"},
    {0xC017, "TLS_ECDH_anon_WITH_3DES_EDE_CBC_SHA"},
    {0xC018, "TLS_ECDH_anon_WITH_AES_128_CBC_SHA"},
    {0xC019, "TLS_ECDH_anon_WITH_AES_256_CBC_SHA"},
    {0xC01A, "TLS_SRP_SHA_WITH_3DES_EDE_CBC_SHA"},
    {0xC01B, "TLS_SRP_SHA_RSA_WITH_3DES_EDE_CBC_SHA"},
    {0xC01C, "TLS_SRP_SHA_DSS_WITH_3DES_EDE_CBC_SHA"},
    {0xC01D, "TLS_SRP_SHA_WITH_AES_128_CBC_SHA"},
    {0xC01E, "TLS_SRP_SHA_RSA_WITH_AES_128_CBC_SHA"},
    {0xC01F, "TLS_SRP_SHA_DSS_WITH_AES_128_CBC_SHA"},
    {0xC020, "TLS_SRP_SHA_WITH_AES_256_CBC_SHA"},
    {0xC021, "TLS_SRP_SHA_RSA_WITH_AES_256_CBC_SHA"},
    {0xC022, "TLS_SRP_SHA_DSS_WITH_AES_256_CBC_SHA"},
    {0xC023, "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256"},
    {0xC024, "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384"},
    {0xC025, "TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256"},
    {0xC026, "TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384"},
    {0xC027, "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256"},
    {0xC028, "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384"},
    {0xC029, "TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256"},
    {0xC02A, "TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384"},
    {0xC02B, "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"},
    {0xC02C, "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"},
    {0xC02D, "TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256"},
    {0xC02E, "TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384"},
    {0xC02F, "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"},
    {0xC030, "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"},
    {0xC031, "TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256"},
    {0xC032, "TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384"},
    {0xC033, "TLS_ECDHE_PSK_WITH_RC4_128_SHA"},
    {0xC034, "TLS_ECDHE_PSK_WITH_3DES_EDE_CBC_SHA"},
    {0xC035, "TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA"},
    {0xC036, "TLS_ECDHE_PSK_WITH_AES_256_CBC_SHA"},
    {0xC037, "TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256"},
    {0xC038, "TLS_ECDHE_PSK_WITH_AES_256_CBC_SHA384"},
    {0xC039, "TLS_ECDHE_PSK_WITH_NULL_SHA"},
    {0xC03A, "TLS_ECDHE_PSK_WITH_NULL_SHA256"},
    {0xC03B, "TLS_ECDHE_PSK_WITH_NULL_SHA384"},
    {0xC03C, "TLS_RSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC03D, "TLS_RSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC03E, "TLS_DH_DSS_WITH_ARIA_128_CBC_SHA256"},
    {0xC03F, "TLS_DH_DSS_WITH_ARIA_256_CBC_SHA384"},
    {0xC040, "TLS_DH_RSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC041, "TLS_DH_RSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC042, "TLS_DHE_DSS_WITH_ARIA_128_CBC_SHA256"},
    {0xC043, "TLS_DHE_DSS_WITH_ARIA_256_CBC_SHA384"},
    {0xC044, "TLS_DHE_RSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC045, "TLS_DHE_RSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC046, "TLS_DH_anon_WITH_ARIA_128_CBC_SHA256"},
    {0xC047, "TLS_DH_anon_WITH_ARIA_256_CBC_SHA384"},
    {0xC048, "TLS_ECDHE_ECDSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC049, "TLS_ECDHE_ECDSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC04A, "TLS_ECDH_ECDSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC04B, "TLS_ECDH_ECDSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC04C, "TLS_ECDHE_RSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC04D, "TLS_ECDHE_RSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC04E, "TLS_ECDH_RSA_WITH_ARIA_128_CBC_SHA256"},
    {0xC04F, "TLS_ECDH_RSA_WITH_ARIA_256_CBC_SHA384"},
    {0xC050, "TLS_RSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC051, "TLS_RSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC052, "TLS_DHE_RSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC053, "TLS_DHE_RSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC054, "TLS_DH_RSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC055, "TLS_DH_RSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC056, "TLS_DHE_DSS_WITH_ARIA_128_GCM_SHA256"},
    {0xC057, "TLS_DHE_DSS_WITH_ARIA_256_GCM_SHA384"},
    {0xC058, "TLS_DH_DSS_WITH_ARIA_128_GCM_SHA256"},
    {0xC059, "TLS_DH_DSS_WITH_ARIA_256_GCM_SHA384"},
    {0xC05A, "TLS_DH_anon_WITH_ARIA_128_GCM_SHA256"},
    {0xC05B, "TLS_DH_anon_WITH_ARIA_256_GCM_SHA384"},
    {0xC05C, "TLS_ECDHE_ECDSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC05D, "TLS_ECDHE_ECDSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC05E, "TLS_ECDH_ECDSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC05F, "TLS_ECDH_ECDSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC060, "TLS_ECDHE_RSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC061, "TLS_ECDHE_RSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC062, "TLS_ECDH_RSA_WITH_ARIA_128_GCM_SHA256"},
    {0xC063, "TLS_ECDH_RSA_WITH_ARIA_256_GCM_SHA384"},
    {0xC064, "TLS_PSK_WITH_ARIA_128_CBC_SHA256"},
    {0xC065, "TLS_PSK_WITH_ARIA_256_CBC_SHA384"},
    {0xC066, "TLS_DHE_PSK_WITH_ARIA_128_CBC_SHA256"},
    {0xC067, "TLS_DHE_PSK_WITH_ARIA_256_CBC_SHA384"},
    {0xC068, "TLS_RSA_PSK_WITH_ARIA_128_CBC_SHA256"},
    {0xC069, "TLS_RSA_PSK_WITH_ARIA_256_CBC_SHA384"},
    {0xC06A, "TLS_PSK_WITH_ARIA_128_GCM_SHA256"},
    {0xC06B, "TLS_PSK_WITH_ARIA_256_GCM_SHA384"},
    {0xC06C, "TLS_DHE_PSK_WITH_ARIA_128_GCM_SHA256"},
    {0xC06D, "TLS_DHE_PSK_WITH_ARIA_256_GCM_SHA384"},
    {0xC06E, "TLS_RSA_PSK_WITH_ARIA_128_GCM_SHA256"},
    {0xC06F, "TLS_RSA_PSK_WITH_ARIA_256_GCM_SHA384"},
    {0xC070, "TLS_ECDHE_PSK_WITH_ARIA_128_CBC_SHA256"},
    {0xC071, "TLS_ECDHE_PSK_WITH_ARIA_256_CBC_SHA384"},
    {0xC072, "TLS_ECDHE_ECDSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC073, "TLS_ECDHE_ECDSA_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC074, "TLS_ECDH_ECDSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC075, "TLS_ECDH_ECDSA_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC076, "TLS_ECDHE_RSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC077, "TLS_ECDHE_RSA_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC078, "TLS_ECDH_RSA_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC079, "TLS_ECDH_RSA_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC07A, "TLS_RSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC07B, "TLS_RSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC07C, "TLS_DHE_RSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC07D, "TLS_DHE_RSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC07E, "TLS_DH_RSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC07F, "TLS_DH_RSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC080, "TLS_DHE_DSS_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC081, "TLS_DHE_DSS_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC082, "TLS_DH_DSS_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC083, "TLS_DH_DSS_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC084, "TLS_DH_anon_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC085, "TLS_DH_anon_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC086, "TLS_ECDHE_ECDSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC087, "TLS_ECDHE_ECDSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC088, "TLS_ECDH_ECDSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC089, "TLS_ECDH_ECDSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC08A, "TLS_ECDHE_RSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC08B, "TLS_ECDHE_RSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC08C, "TLS_ECDH_RSA_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC08D, "TLS_ECDH_RSA_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC08E, "TLS_PSK_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC08F, "TLS_PSK_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC090, "TLS_DHE_PSK_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC091, "TLS_DHE_PSK_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC092, "TLS_RSA_PSK_WITH_CAMELLIA_128_GCM_SHA256"},
    {0xC093, "TLS_RSA_PSK_WITH_CAMELLIA_256_GCM_SHA384"},
    {0xC094, "TLS_PSK_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC095, "TLS_PSK_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC096, "TLS_DHE_PSK_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC097, "TLS_DHE_PSK_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC098, "TLS_RSA_PSK_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC099, "TLS_RSA_PSK_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC09A, "TLS_ECDHE_PSK_WITH_CAMELLIA_128_CBC_SHA256"},
    {0xC09B, "TLS_ECDHE_PSK_WITH_CAMELLIA_256_CBC_SHA384"},
    {0xC09C, "TLS_RSA_WITH_AES_128_CCM"},
    {0xC09D, "TLS_RSA_WITH_AES_256_CCM"},
    {0xC09E, "TLS_DHE_RSA_WITH_AES_128_CCM"},
    {0xC09F, "TLS_DHE_RSA_WITH_AES_256_CCM"},
    {0xC0A0, "TLS_RSA_WITH_AES_128_CCM_8"},
    {0xC0A1, "TLS_RSA_WITH_AES_256_CCM_8"},
    {0xC0A2, "TLS_DHE_RSA_WITH_AES_128_CCM_8"},
    {0xC0A3, "TLS_DHE_RSA_WITH_AES_256_CCM_8"},
    {0xC0A4, "TLS_PSK_WITH_AES_128_CCM"},
    {0xC0A5, "TLS_PSK_WITH_AES_256_CCM"},
    {0xC0A6, "TLS_DHE_PSK_WITH_AES_128_CCM"},
    {0xC0A7, "TLS_DHE_PSK_WITH_AES_256_CCM"},
    {0xC0A8, "TLS_PSK_WITH_AES_128_CCM_8"},
    {0xC0A9, "TLS_PSK_WITH_AES_256_CCM_8"},
    {0xC0AA, "TLS_PSK_DHE_WITH_AES_128_CCM_8"},
    {0xC0AB, "TLS_PSK_DHE_WITH_AES_256_CCM_8"},
    {0xC0AC, "TLS_ECDHE_ECDSA_WITH_AES_128_CCM"},
    {0xC0AD, "TLS_ECDHE_ECDSA_WITH_AES_256_CCM"},
    {0xC0AE, "TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8"},
    {0xC0AF, "TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8"},
    {0xCCA8, "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256"},
    {0xCCA9, "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256"},
    {0xCCAA, "TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256"},
    {0xCCAB, "TLS_PSK_WITH_CHACHA20_POLY1305_SHA256"},
    {0xCCAC, "TLS_ECDHE_PSK_WITH_CHACHA20_POLY1305_SHA256"},
    {0xCCAD, "TLS_DHE_PSK_WITH_CHACHA20_POLY1305_SHA256"},
    {0xCCAE, "TLS_RSA_PSK_WITH_CHACHA20_POLY1305_SHA256"},
    {0x1301, "TLS_AES_128_GCM_SHA256"},
    {0x1302, "TLS_AES_256_GCM_SHA384"},
    {0x1303, "TLS_CHACHA20_POLY1305_SHA256"},
    {0x1304, "TLS_AES_128_CCM_SHA256"},
    {0x1305, "TLS_AES_128_CCM_8_SHA256"},
    {0xFEFE, "SSL_RSA_FIPS_WITH_DES_CBC_SHA"},
    {0xFEFF, "SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA"},
    {0xFF85, "GOST2012-GOST8912-GOST8912"},
    {0xFF87, "GOST2012-NULL-GOST12"},
};

/* Compression methods */
static const ssl_trace_tbl ssl_comp_tbl[] = {
    {0x0000, "No Compression"},
    {0x0001, "Zlib Compression"}
};

/* Extensions sorted by ascending id */
static const ssl_trace_tbl ssl_exts_tbl[] = {
    {TLSEXT_TYPE_server_name, "server_name"},
    {TLSEXT_TYPE_max_fragment_length, "max_fragment_length"},
    {TLSEXT_TYPE_client_certificate_url, "client_certificate_url"},
    {TLSEXT_TYPE_trusted_ca_keys, "trusted_ca_keys"},
    {TLSEXT_TYPE_truncated_hmac, "truncated_hmac"},
    {TLSEXT_TYPE_status_request, "status_request"},
    {TLSEXT_TYPE_user_mapping, "user_mapping"},
    {TLSEXT_TYPE_client_authz, "client_authz"},
    {TLSEXT_TYPE_server_authz, "server_authz"},
    {TLSEXT_TYPE_cert_type, "cert_type"},
    {TLSEXT_TYPE_supported_groups, "supported_groups"},
    {TLSEXT_TYPE_ec_point_formats, "ec_point_formats"},
    {TLSEXT_TYPE_srp, "srp"},
    {TLSEXT_TYPE_signature_algorithms, "signature_algorithms"},
    {TLSEXT_TYPE_use_srtp, "use_srtp"},
    {TLSEXT_TYPE_heartbeat, "tls_heartbeat"},
    {TLSEXT_TYPE_application_layer_protocol_negotiation,
     "application_layer_protocol_negotiation"},
    {TLSEXT_TYPE_signed_certificate_timestamp, "signed_certificate_timestamps"},
    {TLSEXT_TYPE_padding, "padding"},
    {TLSEXT_TYPE_encrypt_then_mac, "encrypt_then_mac"},
    {TLSEXT_TYPE_extended_master_secret, "extended_master_secret"},
    {TLSEXT_TYPE_session_ticket, "session_ticket"},
    {TLSEXT_TYPE_psk, "psk"},
    {TLSEXT_TYPE_early_data, "early_data"},
    {TLSEXT_TYPE_supported_versions, "supported_versions"},
    {TLSEXT_TYPE_cookie, "cookie_ext"},
    {TLSEXT_TYPE_psk_kex_modes, "psk_key_exchange_modes"},
    {TLSEXT_TYPE_certificate_authorities, "certificate_authorities"},
    {TLSEXT_TYPE_post_handshake_auth, "post_handshake_auth"},
    {TLSEXT_TYPE_signature_algorithms_cert, "signature_algorithms_cert"},
    {TLSEXT_TYPE_key_share, "key_share"},
    {TLSEXT_TYPE_renegotiate, "renegotiate"},
# ifndef OPENSSL_NO_NEXTPROTONEG
    {TLSEXT_TYPE_next_proto_neg, "next_proto_neg"},
# endif
};

static const ssl_trace_tbl ssl_groups_tbl[] = {
    {1, "sect163k1 (K-163)"},
    {2, "sect163r1"},
    {3, "sect163r2 (B-163)"},
    {4, "sect193r1"},
    {5, "sect193r2"},
    {6, "sect233k1 (K-233)"},
    {7, "sect233r1 (B-233)"},
    {8, "sect239k1"},
    {9, "sect283k1 (K-283)"},
    {10, "sect283r1 (B-283)"},
    {11, "sect409k1 (K-409)"},
    {12, "sect409r1 (B-409)"},
    {13, "sect571k1 (K-571)"},
    {14, "sect571r1 (B-571)"},
    {15, "secp160k1"},
    {16, "secp160r1"},
    {17, "secp160r2"},
    {18, "secp192k1"},
    {19, "secp192r1 (P-192)"},
    {20, "secp224k1"},
    {21, "secp224r1 (P-224)"},
    {22, "secp256k1"},
    {23, "secp256r1 (P-256)"},
    {24, "secp384r1 (P-384)"},
    {25, "secp521r1 (P-521)"},
    {26, "brainpoolP256r1"},
    {27, "brainpoolP384r1"},
    {28, "brainpoolP512r1"},
    {29, "ecdh_x25519"},
    {30, "ecdh_x448"},
    {256, "ffdhe2048"},
    {257, "ffdhe3072"},
    {258, "ffdhe4096"},
    {259, "ffdhe6144"},
    {260, "ffdhe8192"},
    {0xFF01, "arbitrary_explicit_prime_curves"},
    {0xFF02, "arbitrary_explicit_char2_curves"}
};

static const ssl_trace_tbl ssl_point_tbl[] = {
    {0, "uncompressed"},
    {1, "ansiX962_compressed_prime"},
    {2, "ansiX962_compressed_char2"}
};

static const ssl_trace_tbl ssl_mfl_tbl[] = {
    {0, "disabled"},
    {1, "max_fragment_length := 2^9 (512 bytes)"},
    {2, "max_fragment_length := 2^10 (1024 bytes)"},
    {3, "max_fragment_length := 2^11 (2048 bytes)"},
    {4, "max_fragment_length := 2^12 (4096 bytes)"}
};

static const ssl_trace_tbl ssl_sigalg_tbl[] = {
    {TLSEXT_SIGALG_ecdsa_secp256r1_sha256, "ecdsa_secp256r1_sha256"},
    {TLSEXT_SIGALG_ecdsa_secp384r1_sha384, "ecdsa_secp384r1_sha384"},
    {TLSEXT_SIGALG_ecdsa_secp521r1_sha512, "ecdsa_secp521r1_sha512"},
    {TLSEXT_SIGALG_ecdsa_sha224, "ecdsa_sha224"},
    {TLSEXT_SIGALG_ed25519, "ed25519"},
    {TLSEXT_SIGALG_ed448, "ed448"},
    {TLSEXT_SIGALG_ecdsa_sha1, "ecdsa_sha1"},
    {TLSEXT_SIGALG_rsa_pss_rsae_sha256, "rsa_pss_rsae_sha256"},
    {TLSEXT_SIGALG_rsa_pss_rsae_sha384, "rsa_pss_rsae_sha384"},
    {TLSEXT_SIGALG_rsa_pss_rsae_sha512, "rsa_pss_rsae_sha512"},
    {TLSEXT_SIGALG_rsa_pss_pss_sha256, "rsa_pss_pss_sha256"},
    {TLSEXT_SIGALG_rsa_pss_pss_sha384, "rsa_pss_pss_sha384"},
    {TLSEXT_SIGALG_rsa_pss_pss_sha512, "rsa_pss_pss_sha512"},
    {TLSEXT_SIGALG_rsa_pkcs1_sha256, "rsa_pkcs1_sha256"},
    {TLSEXT_SIGALG_rsa_pkcs1_sha384, "rsa_pkcs1_sha384"},
    {TLSEXT_SIGALG_rsa_pkcs1_sha512, "rsa_pkcs1_sha512"},
    {TLSEXT_SIGALG_rsa_pkcs1_sha224, "rsa_pkcs1_sha224"},
    {TLSEXT_SIGALG_rsa_pkcs1_sha1, "rsa_pkcs1_sha1"},
    {TLSEXT_SIGALG_dsa_sha256, "dsa_sha256"},
    {TLSEXT_SIGALG_dsa_sha384, "dsa_sha384"},
    {TLSEXT_SIGALG_dsa_sha512, "dsa_sha512"},
    {TLSEXT_SIGALG_dsa_sha224, "dsa_sha224"},
    {TLSEXT_SIGALG_dsa_sha1, "dsa_sha1"},
    {TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256, "gost2012_256"},
    {TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512, "gost2012_512"},
    {TLSEXT_SIGALG_gostr34102001_gostr3411, "gost2001_gost94"},
};

static const ssl_trace_tbl ssl_ctype_tbl[] = {
    {1, "rsa_sign"},
    {2, "dss_sign"},
    {3, "rsa_fixed_dh"},
    {4, "dss_fixed_dh"},
    {5, "rsa_ephemeral_dh"},
    {6, "dss_ephemeral_dh"},
    {20, "fortezza_dms"},
    {64, "ecdsa_sign"},
    {65, "rsa_fixed_ecdh"},
    {66, "ecdsa_fixed_ecdh"}
};

static const ssl_trace_tbl ssl_psk_kex_modes_tbl[] = {
    {TLSEXT_KEX_MODE_KE, "psk_ke"},
    {TLSEXT_KEX_MODE_KE_DHE, "psk_dhe_ke"}
};

static const ssl_trace_tbl ssl_key_update_tbl[] = {
    {SSL_KEY_UPDATE_NOT_REQUESTED, "update_not_requested"},
    {SSL_KEY_UPDATE_REQUESTED, "update_requested"}
};

static void ssl_print_hex(BIO *bio, int indent, const char *name,
                          const unsigned char *msg, size_t msglen)
{
    size_t i;

    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "%s (len=%d): ", name, (int)msglen);
    for (i = 0; i < msglen; i++)
        BIO_printf(bio, "%02X", msg[i]);
    BIO_puts(bio, "\n");
}

static int ssl_print_hexbuf(BIO *bio, int indent, const char *name, size_t nlen,
                            const unsigned char **pmsg, size_t *pmsglen)
{
    size_t blen;
    const unsigned char *p = *pmsg;

    if (*pmsglen < nlen)
        return 0;
    blen = p[0];
    if (nlen > 1)
        blen = (blen << 8) | p[1];
    if (*pmsglen < nlen + blen)
        return 0;
    p += nlen;
    ssl_print_hex(bio, indent, name, p, blen);
    *pmsg += blen + nlen;
    *pmsglen -= blen + nlen;
    return 1;
}

static int ssl_print_version(BIO *bio, int indent, const char *name,
                             const unsigned char **pmsg, size_t *pmsglen,
                             unsigned int *version)
{
    int vers;

    if (*pmsglen < 2)
        return 0;
    vers = ((*pmsg)[0] << 8) | (*pmsg)[1];
    if (version != NULL)
        *version = vers;
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "%s=0x%x (%s)\n",
               name, vers, ssl_trace_str(vers, ssl_version_tbl));
    *pmsg += 2;
    *pmsglen -= 2;
    return 1;
}

static int ssl_print_random(BIO *bio, int indent,
                            const unsigned char **pmsg, size_t *pmsglen)
{
    unsigned int tm;
    const unsigned char *p = *pmsg;

    if (*pmsglen < 32)
        return 0;
    tm = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    p += 4;
    BIO_indent(bio, indent, 80);
    BIO_puts(bio, "Random:\n");
    BIO_indent(bio, indent + 2, 80);
    BIO_printf(bio, "gmt_unix_time=0x%08X\n", tm);
    ssl_print_hex(bio, indent + 2, "random_bytes", p, 28);
    *pmsg += 32;
    *pmsglen -= 32;
    return 1;
}

static int ssl_print_signature(BIO *bio, int indent, const SSL *ssl,
                               const unsigned char **pmsg, size_t *pmsglen)
{
    if (*pmsglen < 2)
        return 0;
    if (SSL_USE_SIGALGS(ssl)) {
        const unsigned char *p = *pmsg;
        unsigned int sigalg = (p[0] << 8) | p[1];

        BIO_indent(bio, indent, 80);
        BIO_printf(bio, "Signature Algorithm: %s (0x%04x)\n",
                   ssl_trace_str(sigalg, ssl_sigalg_tbl), sigalg);
        *pmsg += 2;
        *pmsglen -= 2;
    }
    return ssl_print_hexbuf(bio, indent, "Signature", 2, pmsg, pmsglen);
}

static int ssl_print_extension(BIO *bio, int indent, int server,
                               unsigned char mt, int extype,
                               const unsigned char *ext, size_t extlen)
{
    size_t xlen, share_len;
    unsigned int sigalg;
    uint32_t max_early_data;

    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "extension_type=%s(%d), length=%d\n",
               ssl_trace_str(extype, ssl_exts_tbl), extype, (int)extlen);
    switch (extype) {
    case TLSEXT_TYPE_max_fragment_length:
        if (extlen < 1)
            return 0;
        xlen = extlen;
        return ssl_trace_list(bio, indent + 2, ext, xlen, 1, ssl_mfl_tbl);

    case TLSEXT_TYPE_ec_point_formats:
        if (extlen < 1)
            return 0;
        xlen = ext[0];
        if (extlen != xlen + 1)
            return 0;
        return ssl_trace_list(bio, indent + 2, ext + 1, xlen, 1, ssl_point_tbl);

    case TLSEXT_TYPE_supported_groups:
        if (extlen < 2)
            return 0;
        xlen = (ext[0] << 8) | ext[1];
        if (extlen != xlen + 2)
            return 0;
        return ssl_trace_list(bio, indent + 2, ext + 2, xlen, 2, ssl_groups_tbl);
    case TLSEXT_TYPE_application_layer_protocol_negotiation:
        if (extlen < 2)
            return 0;
        xlen = (ext[0] << 8) | ext[1];
        if (extlen != xlen + 2)
            return 0;
        ext += 2;
        while (xlen > 0) {
            size_t plen = *ext++;

            if (plen + 1 > xlen)
                return 0;
            BIO_indent(bio, indent + 2, 80);
            BIO_write(bio, ext, plen);
            BIO_puts(bio, "\n");
            ext += plen;
            xlen -= plen + 1;
        }
        return 1;

    case TLSEXT_TYPE_signature_algorithms:

        if (extlen < 2)
            return 0;
        xlen = (ext[0] << 8) | ext[1];
        if (extlen != xlen + 2)
            return 0;
        if (xlen & 1)
            return 0;
        ext += 2;
        while (xlen > 0) {
            BIO_indent(bio, indent + 2, 80);
            sigalg = (ext[0] << 8) | ext[1];
            BIO_printf(bio, "%s (0x%04x)\n",
                       ssl_trace_str(sigalg, ssl_sigalg_tbl), sigalg);
            xlen -= 2;
            ext += 2;
        }
        break;

    case TLSEXT_TYPE_renegotiate:
        if (extlen < 1)
            return 0;
        xlen = ext[0];
        if (xlen + 1 != extlen)
            return 0;
        ext++;
        if (xlen) {
            if (server) {
                if (xlen & 1)
                    return 0;
                xlen >>= 1;
            }
            ssl_print_hex(bio, indent + 4, "client_verify_data", ext, xlen);
            if (server) {
                ext += xlen;
                ssl_print_hex(bio, indent + 4, "server_verify_data", ext, xlen);
            }
        } else {
            BIO_indent(bio, indent + 4, 80);
            BIO_puts(bio, "<EMPTY>\n");
        }
        break;

    case TLSEXT_TYPE_heartbeat:
        return 0;

    case TLSEXT_TYPE_session_ticket:
        if (extlen != 0)
            ssl_print_hex(bio, indent + 4, "ticket", ext, extlen);
        break;

    case TLSEXT_TYPE_key_share:
        if (server && extlen == 2) {
            int group_id;

            /* We assume this is an HRR, otherwise this is an invalid key_share */
            group_id = (ext[0] << 8) | ext[1];
            BIO_indent(bio, indent + 4, 80);
            BIO_printf(bio, "NamedGroup: %s (%d)\n",
                       ssl_trace_str(group_id, ssl_groups_tbl), group_id);
            break;
        }
        if (extlen < 2)
            return 0;
        if (server) {
            xlen = extlen;
        } else {
            xlen = (ext[0] << 8) | ext[1];
            if (extlen != xlen + 2)
                return 0;
            ext += 2;
        }
        for (; xlen > 0; ext += share_len, xlen -= share_len) {
            int group_id;

            if (xlen < 4)
                return 0;
            group_id = (ext[0] << 8) | ext[1];
            share_len = (ext[2] << 8) | ext[3];
            ext += 4;
            xlen -= 4;
            if (xlen < share_len)
                return 0;
            BIO_indent(bio, indent + 4, 80);
            BIO_printf(bio, "NamedGroup: %s (%d)\n",
                       ssl_trace_str(group_id, ssl_groups_tbl), group_id);
            ssl_print_hex(bio, indent + 4, "key_exchange: ", ext, share_len);
        }
        break;

    case TLSEXT_TYPE_supported_versions:
        if (server) {
            int version;

            if (extlen != 2)
                return 0;
            version = (ext[0] << 8) | ext[1];
            BIO_indent(bio, indent + 4, 80);
            BIO_printf(bio, "%s (%d)\n",
                       ssl_trace_str(version, ssl_version_tbl), version);
            break;
        }
        if (extlen < 1)
            return 0;
        xlen = ext[0];
        if (extlen != xlen + 1)
            return 0;
        return ssl_trace_list(bio, indent + 2, ext + 1, xlen, 2,
                              ssl_version_tbl);

    case TLSEXT_TYPE_psk_kex_modes:
        if (extlen < 1)
            return 0;
        xlen = ext[0];
        if (extlen != xlen + 1)
            return 0;
        return ssl_trace_list(bio, indent + 2, ext + 1, xlen, 1,
                              ssl_psk_kex_modes_tbl);

    case TLSEXT_TYPE_early_data:
        if (mt != SSL3_MT_NEWSESSION_TICKET)
            break;
        if (extlen != 4)
            return 0;
        max_early_data = (ext[0] << 24) | (ext[1] << 16) | (ext[2] << 8)
                         | ext[3];
        BIO_indent(bio, indent + 2, 80);
        BIO_printf(bio, "max_early_data=%u\n", max_early_data);
        break;

    default:
        BIO_dump_indent(bio, (const char *)ext, extlen, indent + 2);
    }
    return 1;
}

static int ssl_print_extensions(BIO *bio, int indent, int server,
                                unsigned char mt, const unsigned char **msgin,
                                size_t *msginlen)
{
    size_t extslen, msglen = *msginlen;
    const unsigned char *msg = *msgin;

    BIO_indent(bio, indent, 80);
    if (msglen == 0) {
        BIO_puts(bio, "No extensions\n");
        return 1;
    }
    if (msglen < 2)
        return 0;
    extslen = (msg[0] << 8) | msg[1];
    msglen -= 2;
    msg += 2;
    if (extslen == 0) {
        BIO_puts(bio, "No extensions\n");
        *msgin = msg;
        *msginlen = msglen;
        return 1;
    }
    if (extslen > msglen)
        return 0;
    BIO_printf(bio, "extensions, length = %d\n", (int)extslen);
    msglen -= extslen;
    while (extslen > 0) {
        int extype;
        size_t extlen;
        if (extslen < 4)
            return 0;
        extype = (msg[0] << 8) | msg[1];
        extlen = (msg[2] << 8) | msg[3];
        if (extslen < extlen + 4) {
            BIO_printf(bio, "extensions, extype = %d, extlen = %d\n", extype,
                       (int)extlen);
            BIO_dump_indent(bio, (const char *)msg, extslen, indent + 2);
            return 0;
        }
        msg += 4;
        if (!ssl_print_extension(bio, indent + 2, server, mt, extype, msg,
                                 extlen))
            return 0;
        msg += extlen;
        extslen -= extlen + 4;
    }

    *msgin = msg;
    *msginlen = msglen;
    return 1;
}

static int ssl_print_client_hello(BIO *bio, const SSL *ssl, int indent,
                                  const unsigned char *msg, size_t msglen)
{
    size_t len;
    unsigned int cs;

    if (!ssl_print_version(bio, indent, "client_version", &msg, &msglen, NULL))
        return 0;
    if (!ssl_print_random(bio, indent, &msg, &msglen))
        return 0;
    if (!ssl_print_hexbuf(bio, indent, "session_id", 1, &msg, &msglen))
        return 0;
    if (SSL_IS_DTLS(ssl)) {
        if (!ssl_print_hexbuf(bio, indent, "cookie", 1, &msg, &msglen))
            return 0;
    }
    if (msglen < 2)
        return 0;
    len = (msg[0] << 8) | msg[1];
    msg += 2;
    msglen -= 2;
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "cipher_suites (len=%d)\n", (int)len);
    if (msglen < len || len & 1)
        return 0;
    while (len > 0) {
        cs = (msg[0] << 8) | msg[1];
        BIO_indent(bio, indent + 2, 80);
        BIO_printf(bio, "{0x%02X, 0x%02X} %s\n",
                   msg[0], msg[1], ssl_trace_str(cs, ssl_ciphers_tbl));
        msg += 2;
        msglen -= 2;
        len -= 2;
    }
    if (msglen < 1)
        return 0;
    len = msg[0];
    msg++;
    msglen--;
    if (msglen < len)
        return 0;
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "compression_methods (len=%d)\n", (int)len);
    while (len > 0) {
        BIO_indent(bio, indent + 2, 80);
        BIO_printf(bio, "%s (0x%02X)\n",
                   ssl_trace_str(msg[0], ssl_comp_tbl), msg[0]);
        msg++;
        msglen--;
        len--;
    }
    if (!ssl_print_extensions(bio, indent, 0, SSL3_MT_CLIENT_HELLO, &msg,
                              &msglen))
        return 0;
    return 1;
}

static int dtls_print_hello_vfyrequest(BIO *bio, int indent,
                                       const unsigned char *msg, size_t msglen)
{
    if (!ssl_print_version(bio, indent, "server_version", &msg, &msglen, NULL))
        return 0;
    if (!ssl_print_hexbuf(bio, indent, "cookie", 1, &msg, &msglen))
        return 0;
    return 1;
}

static int ssl_print_server_hello(BIO *bio, int indent,
                                  const unsigned char *msg, size_t msglen)
{
    unsigned int cs;
    unsigned int vers;

    if (!ssl_print_version(bio, indent, "server_version", &msg, &msglen, &vers))
        return 0;
    if (!ssl_print_random(bio, indent, &msg, &msglen))
        return 0;
    if (vers != TLS1_3_VERSION
            && !ssl_print_hexbuf(bio, indent, "session_id", 1, &msg, &msglen))
        return 0;
    if (msglen < 2)
        return 0;
    cs = (msg[0] << 8) | msg[1];
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "cipher_suite {0x%02X, 0x%02X} %s\n",
               msg[0], msg[1], ssl_trace_str(cs, ssl_ciphers_tbl));
    msg += 2;
    msglen -= 2;
    if (vers != TLS1_3_VERSION) {
        if (msglen < 1)
            return 0;
        BIO_indent(bio, indent, 80);
        BIO_printf(bio, "compression_method: %s (0x%02X)\n",
                   ssl_trace_str(msg[0], ssl_comp_tbl), msg[0]);
        msg++;
        msglen--;
    }
    if (!ssl_print_extensions(bio, indent, 1, SSL3_MT_SERVER_HELLO, &msg,
                              &msglen))
        return 0;
    return 1;
}

static int ssl_get_keyex(const char **pname, const SSL *ssl)
{
    unsigned long alg_k = ssl->s3->tmp.new_cipher->algorithm_mkey;

    if (alg_k & SSL_kRSA) {
        *pname = "rsa";
        return SSL_kRSA;
    }
    if (alg_k & SSL_kDHE) {
        *pname = "DHE";
        return SSL_kDHE;
    }
    if (alg_k & SSL_kECDHE) {
        *pname = "ECDHE";
        return SSL_kECDHE;
    }
    if (alg_k & SSL_kPSK) {
        *pname = "PSK";
        return SSL_kPSK;
    }
    if (alg_k & SSL_kRSAPSK) {
        *pname = "RSAPSK";
        return SSL_kRSAPSK;
    }
    if (alg_k & SSL_kDHEPSK) {
        *pname = "DHEPSK";
        return SSL_kDHEPSK;
    }
    if (alg_k & SSL_kECDHEPSK) {
        *pname = "ECDHEPSK";
        return SSL_kECDHEPSK;
    }
    if (alg_k & SSL_kSRP) {
        *pname = "SRP";
        return SSL_kSRP;
    }
    if (alg_k & SSL_kGOST) {
        *pname = "GOST";
        return SSL_kGOST;
    }
    *pname = "UNKNOWN";
    return 0;
}

static int ssl_print_client_keyex(BIO *bio, int indent, const SSL *ssl,
                                  const unsigned char *msg, size_t msglen)
{
    const char *algname;
    int id = ssl_get_keyex(&algname, ssl);

    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "KeyExchangeAlgorithm=%s\n", algname);
    if (id & SSL_PSK) {
        if (!ssl_print_hexbuf(bio, indent + 2,
                              "psk_identity", 2, &msg, &msglen))
            return 0;
    }
    switch (id) {

    case SSL_kRSA:
    case SSL_kRSAPSK:
        if (TLS1_get_version(ssl) == SSL3_VERSION) {
            ssl_print_hex(bio, indent + 2,
                          "EncryptedPreMasterSecret", msg, msglen);
        } else {
            if (!ssl_print_hexbuf(bio, indent + 2,
                                  "EncryptedPreMasterSecret", 2, &msg, &msglen))
                return 0;
        }
        break;

    case SSL_kDHE:
    case SSL_kDHEPSK:
        if (!ssl_print_hexbuf(bio, indent + 2, "dh_Yc", 2, &msg, &msglen))
            return 0;
        break;

    case SSL_kECDHE:
    case SSL_kECDHEPSK:
        if (!ssl_print_hexbuf(bio, indent + 2, "ecdh_Yc", 1, &msg, &msglen))
            return 0;
        break;

    }

    return !msglen;
}

static int ssl_print_server_keyex(BIO *bio, int indent, const SSL *ssl,
                                  const unsigned char *msg, size_t msglen)
{
    const char *algname;
    int id = ssl_get_keyex(&algname, ssl);

    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "KeyExchangeAlgorithm=%s\n", algname);
    if (id & SSL_PSK) {
        if (!ssl_print_hexbuf(bio, indent + 2,
                              "psk_identity_hint", 2, &msg, &msglen))
            return 0;
    }
    switch (id) {
    case SSL_kRSA:

        if (!ssl_print_hexbuf(bio, indent + 2, "rsa_modulus", 2, &msg, &msglen))
            return 0;
        if (!ssl_print_hexbuf(bio, indent + 2, "rsa_exponent", 2,
                              &msg, &msglen))
            return 0;
        break;

    case SSL_kDHE:
    case SSL_kDHEPSK:
        if (!ssl_print_hexbuf(bio, indent + 2, "dh_p", 2, &msg, &msglen))
            return 0;
        if (!ssl_print_hexbuf(bio, indent + 2, "dh_g", 2, &msg, &msglen))
            return 0;
        if (!ssl_print_hexbuf(bio, indent + 2, "dh_Ys", 2, &msg, &msglen))
            return 0;
        break;

# ifndef OPENSSL_NO_EC
    case SSL_kECDHE:
    case SSL_kECDHEPSK:
        if (msglen < 1)
            return 0;
        BIO_indent(bio, indent + 2, 80);
        if (msg[0] == EXPLICIT_PRIME_CURVE_TYPE)
            BIO_puts(bio, "explicit_prime\n");
        else if (msg[0] == EXPLICIT_CHAR2_CURVE_TYPE)
            BIO_puts(bio, "explicit_char2\n");
        else if (msg[0] == NAMED_CURVE_TYPE) {
            int curve;
            if (msglen < 3)
                return 0;
            curve = (msg[1] << 8) | msg[2];
            BIO_printf(bio, "named_curve: %s (%d)\n",
                       ssl_trace_str(curve, ssl_groups_tbl), curve);
            msg += 3;
            msglen -= 3;
            if (!ssl_print_hexbuf(bio, indent + 2, "point", 1, &msg, &msglen))
                return 0;
        } else {
            BIO_printf(bio, "UNKNOWN CURVE PARAMETER TYPE %d\n", msg[0]);
            return 0;
        }
        break;
# endif

    case SSL_kPSK:
    case SSL_kRSAPSK:
        break;
    }
    if (!(id & SSL_PSK))
        ssl_print_signature(bio, indent, ssl, &msg, &msglen);
    return !msglen;
}

static int ssl_print_certificate(BIO *bio, int indent,
                                 const unsigned char **pmsg, size_t *pmsglen)
{
    size_t msglen = *pmsglen;
    size_t clen;
    X509 *x;
    const unsigned char *p = *pmsg, *q;

    if (msglen < 3)
        return 0;
    clen = (p[0] << 16) | (p[1] << 8) | p[2];
    if (msglen < clen + 3)
        return 0;
    q = p + 3;
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "ASN.1Cert, length=%d", (int)clen);
    x = d2i_X509(NULL, &q, clen);
    if (!x)
        BIO_puts(bio, "<UNPARSEABLE CERTIFICATE>\n");
    else {
        BIO_puts(bio, "\n------details-----\n");
        X509_print_ex(bio, x, XN_FLAG_ONELINE, 0);
        PEM_write_bio_X509(bio, x);
        /* Print certificate stuff */
        BIO_puts(bio, "------------------\n");
        X509_free(x);
    }
    if (q != p + 3 + clen) {
        BIO_puts(bio, "<TRAILING GARBAGE AFTER CERTIFICATE>\n");
    }
    *pmsg += clen + 3;
    *pmsglen -= clen + 3;
    return 1;
}

static int ssl_print_certificates(BIO *bio, const SSL *ssl, int server,
                                  int indent, const unsigned char *msg,
                                  size_t msglen)
{
    size_t clen;

    if (SSL_IS_TLS13(ssl)
            && !ssl_print_hexbuf(bio, indent, "context", 1, &msg, &msglen))
        return 0;

    if (msglen < 3)
        return 0;
    clen = (msg[0] << 16) | (msg[1] << 8) | msg[2];
    if (msglen != clen + 3)
        return 0;
    msg += 3;
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "certificate_list, length=%d\n", (int)clen);
    while (clen > 0) {
        if (!ssl_print_certificate(bio, indent + 2, &msg, &clen))
            return 0;
        if (!ssl_print_extensions(bio, indent + 2, server, SSL3_MT_CERTIFICATE,
                                  &msg, &clen))
            return 0;

    }
    return 1;
}

static int ssl_print_cert_request(BIO *bio, int indent, const SSL *ssl,
                                  const unsigned char *msg, size_t msglen)
{
    size_t xlen;
    unsigned int sigalg;

    if (SSL_IS_TLS13(ssl)) {
        if (!ssl_print_hexbuf(bio, indent, "request_context", 1, &msg, &msglen))
            return 0;
        if (!ssl_print_extensions(bio, indent, 1,
                                  SSL3_MT_CERTIFICATE_REQUEST, &msg, &msglen))
            return 0;
        return 1;
    } else {
        if (msglen < 1)
            return 0;
        xlen = msg[0];
        if (msglen < xlen + 1)
            return 0;
        msg++;
        BIO_indent(bio, indent, 80);
        BIO_printf(bio, "certificate_types (len=%d)\n", (int)xlen);
        if (!ssl_trace_list(bio, indent + 2, msg, xlen, 1, ssl_ctype_tbl))
            return 0;
        msg += xlen;
        msglen -= xlen + 1;
    }
    if (SSL_USE_SIGALGS(ssl)) {
        if (msglen < 2)
            return 0;
        xlen = (msg[0] << 8) | msg[1];
        if (msglen < xlen + 2 || (xlen & 1))
            return 0;
        msg += 2;
        msglen -= xlen + 2;
        BIO_indent(bio, indent, 80);
        BIO_printf(bio, "signature_algorithms (len=%d)\n", (int)xlen);
        while (xlen > 0) {
            BIO_indent(bio, indent + 2, 80);
            sigalg = (msg[0] << 8) | msg[1];
            BIO_printf(bio, "%s (0x%04x)\n",
                       ssl_trace_str(sigalg, ssl_sigalg_tbl), sigalg);
            xlen -= 2;
            msg += 2;
        }
        msg += xlen;
    }

    if (msglen < 2)
        return 0;
    xlen = (msg[0] << 8) | msg[1];
    BIO_indent(bio, indent, 80);
    if (msglen < xlen + 2)
        return 0;
    msg += 2;
    msglen -= 2 + xlen;
    BIO_printf(bio, "certificate_authorities (len=%d)\n", (int)xlen);
    while (xlen > 0) {
        size_t dlen;
        X509_NAME *nm;
        const unsigned char *p;
        if (xlen < 2)
            return 0;
        dlen = (msg[0] << 8) | msg[1];
        if (xlen < dlen + 2)
            return 0;
        msg += 2;
        BIO_indent(bio, indent + 2, 80);
        BIO_printf(bio, "DistinguishedName (len=%d): ", (int)dlen);
        p = msg;
        nm = d2i_X509_NAME(NULL, &p, dlen);
        if (!nm) {
            BIO_puts(bio, "<UNPARSEABLE DN>\n");
        } else {
            X509_NAME_print_ex(bio, nm, 0, XN_FLAG_ONELINE);
            BIO_puts(bio, "\n");
            X509_NAME_free(nm);
        }
        xlen -= dlen + 2;
        msg += dlen;
    }
    if (SSL_IS_TLS13(ssl)) {
        if (!ssl_print_hexbuf(bio, indent, "request_extensions", 2,
                              &msg, &msglen))
            return 0;
    }
    return msglen == 0;
}

static int ssl_print_ticket(BIO *bio, int indent, const SSL *ssl,
                            const unsigned char *msg, size_t msglen)
{
    unsigned int tick_life;

    if (msglen == 0) {
        BIO_indent(bio, indent + 2, 80);
        BIO_puts(bio, "No Ticket\n");
        return 1;
    }
    if (msglen < 4)
        return 0;
    tick_life = (msg[0] << 24) | (msg[1] << 16) | (msg[2] << 8) | msg[3];
    msglen -= 4;
    msg += 4;
    BIO_indent(bio, indent + 2, 80);
    BIO_printf(bio, "ticket_lifetime_hint=%u\n", tick_life);
    if (SSL_IS_TLS13(ssl)) {
        unsigned int ticket_age_add;

        if (msglen < 4)
            return 0;
        ticket_age_add =
            (msg[0] << 24) | (msg[1] << 16) | (msg[2] << 8) | msg[3];
        msglen -= 4;
        msg += 4;
        BIO_indent(bio, indent + 2, 80);
        BIO_printf(bio, "ticket_age_add=%u\n", ticket_age_add);
        if (!ssl_print_hexbuf(bio, indent + 2, "ticket_nonce", 1, &msg,
                              &msglen))
            return 0;
    }
    if (!ssl_print_hexbuf(bio, indent + 2, "ticket", 2, &msg, &msglen))
        return 0;
    if (SSL_IS_TLS13(ssl)
            && !ssl_print_extensions(bio, indent + 2, 0,
                                     SSL3_MT_NEWSESSION_TICKET, &msg, &msglen))
        return 0;
    if (msglen)
        return 0;
    return 1;
}

static int ssl_print_handshake(BIO *bio, const SSL *ssl, int server,
                               const unsigned char *msg, size_t msglen,
                               int indent)
{
    size_t hlen;
    unsigned char htype;

    if (msglen < 4)
        return 0;
    htype = msg[0];
    hlen = (msg[1] << 16) | (msg[2] << 8) | msg[3];
    BIO_indent(bio, indent, 80);
    BIO_printf(bio, "%s, Length=%d\n",
               ssl_trace_str(htype, ssl_handshake_tbl), (int)hlen);
    msg += 4;
    msglen -= 4;
    if (SSL_IS_DTLS(ssl)) {
        if (msglen < 8)
            return 0;
        BIO_indent(bio, indent, 80);
        BIO_printf(bio, "message_seq=%d, fragment_offset=%d, "
                   "fragment_length=%d\n",
                   (msg[0] << 8) | msg[1],
                   (msg[2] << 16) | (msg[3] << 8) | msg[4],
                   (msg[5] << 16) | (msg[6] << 8) | msg[7]);
        msg += 8;
        msglen -= 8;
    }
    if (msglen < hlen)
        return 0;
    switch (htype) {
    case SSL3_MT_CLIENT_HELLO:
        if (!ssl_print_client_hello(bio, ssl, indent + 2, msg, msglen))
            return 0;
        break;

    case DTLS1_MT_HELLO_VERIFY_REQUEST:
        if (!dtls_print_hello_vfyrequest(bio, indent + 2, msg, msglen))
            return 0;
        break;

    case SSL3_MT_SERVER_HELLO:
        if (!ssl_print_server_hello(bio, indent + 2, msg, msglen))
            return 0;
        break;

    case SSL3_MT_SERVER_KEY_EXCHANGE:
        if (!ssl_print_server_keyex(bio, indent + 2, ssl, msg, msglen))
            return 0;
        break;

    case SSL3_MT_CLIENT_KEY_EXCHANGE:
        if (!ssl_print_client_keyex(bio, indent + 2, ssl, msg, msglen))
            return 0;
        break;

    case SSL3_MT_CERTIFICATE:
        if (!ssl_print_certificates(bio, ssl, server, indent + 2, msg, msglen))
            return 0;
        break;

    case SSL3_MT_CERTIFICATE_VERIFY:
        if (!ssl_print_signature(bio, indent + 2, ssl, &msg, &msglen))
            return 0;
        break;

    case SSL3_MT_CERTIFICATE_REQUEST:
        if (!ssl_print_cert_request(bio, indent + 2, ssl, msg, msglen))
            return 0;
        break;

    case SSL3_MT_FINISHED:
        ssl_print_hex(bio, indent + 2, "verify_data", msg, msglen);
        break;

    case SSL3_MT_SERVER_DONE:
        if (msglen != 0)
            ssl_print_hex(bio, indent + 2, "unexpected value", msg, msglen);
        break;

    case SSL3_MT_NEWSESSION_TICKET:
        if (!ssl_print_ticket(bio, indent + 2, ssl, msg, msglen))
            return 0;
        break;

    case SSL3_MT_ENCRYPTED_EXTENSIONS:
        if (!ssl_print_extensions(bio, indent + 2, 1,
                                  SSL3_MT_ENCRYPTED_EXTENSIONS, &msg, &msglen))
            return 0;
        break;

    case SSL3_MT_KEY_UPDATE:
        if (msglen != 1) {
            ssl_print_hex(bio, indent + 2, "unexpected value", msg, msglen);
            return 0;
        }
        if (!ssl_trace_list(bio, indent + 2, msg, msglen, 1,
                            ssl_key_update_tbl))
            return 0;
        break;

    default:
        BIO_indent(bio, indent + 2, 80);
        BIO_puts(bio, "Unsupported, hex dump follows:\n");
        BIO_dump_indent(bio, (const char *)msg, msglen, indent + 4);
    }
    return 1;
}

void SSL_trace(int write_p, int version, int content_type,
               const void *buf, size_t msglen, SSL *ssl, void *arg)
{
    const unsigned char *msg = buf;
    BIO *bio = arg;

    switch (content_type) {
    case SSL3_RT_HEADER:
        {
            int hvers;

            /* avoid overlapping with length at the end of buffer */
            if (msglen < (size_t)(SSL_IS_DTLS(ssl) ?
                     DTLS1_RT_HEADER_LENGTH : SSL3_RT_HEADER_LENGTH)) {
                BIO_puts(bio, write_p ? "Sent" : "Received");
                ssl_print_hex(bio, 0, " too short message", msg, msglen);
                break;
            }
            hvers = msg[1] << 8 | msg[2];
            BIO_puts(bio, write_p ? "Sent" : "Received");
            BIO_printf(bio, " Record\nHeader:\n  Version = %s (0x%x)\n",
                       ssl_trace_str(hvers, ssl_version_tbl), hvers);
            if (SSL_IS_DTLS(ssl)) {
                BIO_printf(bio,
                           "  epoch=%d, sequence_number=%04x%04x%04x\n",
                           (msg[3] << 8 | msg[4]),
                           (msg[5] << 8 | msg[6]),
                           (msg[7] << 8 | msg[8]), (msg[9] << 8 | msg[10]));
            }

            BIO_printf(bio, "  Content Type = %s (%d)\n  Length = %d",
                       ssl_trace_str(msg[0], ssl_content_tbl), msg[0],
                       msg[msglen - 2] << 8 | msg[msglen - 1]);
        }
        break;

    case SSL3_RT_INNER_CONTENT_TYPE:
        BIO_printf(bio, "  Inner Content Type = %s (%d)",
                   ssl_trace_str(msg[0], ssl_content_tbl), msg[0]);
        break;

    case SSL3_RT_HANDSHAKE:
        if (!ssl_print_handshake(bio, ssl, ssl->server ? write_p : !write_p,
                                 msg, msglen, 4))
            BIO_printf(bio, "Message length parse error!\n");
        break;

    case SSL3_RT_CHANGE_CIPHER_SPEC:
        if (msglen == 1 && msg[0] == 1)
            BIO_puts(bio, "    change_cipher_spec (1)\n");
        else
            ssl_print_hex(bio, 4, "unknown value", msg, msglen);
        break;

    case SSL3_RT_ALERT:
        if (msglen != 2)
            BIO_puts(bio, "    Illegal Alert Length\n");
        else {
            BIO_printf(bio, "    Level=%s(%d), description=%s(%d)\n",
                       SSL_alert_type_string_long(msg[0] << 8),
                       msg[0], SSL_alert_desc_string_long(msg[1]), msg[1]);
        }

    }

    BIO_puts(bio, "\n");
}

#endif
