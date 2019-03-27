/*
 * SSL/TLS interface functions for OpenSSL
 * Copyright (c) 2004-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TLS_OPENSSL_H
#define TLS_OPENSSL_H

enum ocsp_result {
	OCSP_GOOD, OCSP_REVOKED, OCSP_NO_RESPONSE, OCSP_INVALID
};

enum ocsp_result check_ocsp_resp(SSL_CTX *ssl_ctx, SSL *ssl, X509 *cert,
				 X509 *issuer, X509 *issuer_issuer);

#endif /* TLS_OPENSSL_H */
