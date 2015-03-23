/*
 * X.509v3 certificate parsing and processing
 * Copyright (c) 2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef X509V3_H
#define X509V3_H

#include "asn1.h"

struct x509_algorithm_identifier {
	struct asn1_oid oid;
};

struct x509_name_attr {
	enum x509_name_attr_type {
		X509_NAME_ATTR_NOT_USED,
		X509_NAME_ATTR_DC,
		X509_NAME_ATTR_CN,
		X509_NAME_ATTR_C,
		X509_NAME_ATTR_L,
		X509_NAME_ATTR_ST,
		X509_NAME_ATTR_O,
		X509_NAME_ATTR_OU
	} type;
	char *value;
};

#define X509_MAX_NAME_ATTRIBUTES 20

struct x509_name {
	struct x509_name_attr attr[X509_MAX_NAME_ATTRIBUTES];
	size_t num_attr;
	char *email; /* emailAddress */

	/* from alternative name extension */
	char *alt_email; /* rfc822Name */
	char *dns; /* dNSName */
	char *uri; /* uniformResourceIdentifier */
	u8 *ip; /* iPAddress */
	size_t ip_len; /* IPv4: 4, IPv6: 16 */
	struct asn1_oid rid; /* registeredID */
};

struct x509_certificate {
	struct x509_certificate *next;
	enum { X509_CERT_V1 = 0, X509_CERT_V2 = 1, X509_CERT_V3 = 2 } version;
	unsigned long serial_number;
	struct x509_algorithm_identifier signature;
	struct x509_name issuer;
	struct x509_name subject;
	os_time_t not_before;
	os_time_t not_after;
	struct x509_algorithm_identifier public_key_alg;
	u8 *public_key;
	size_t public_key_len;
	struct x509_algorithm_identifier signature_alg;
	u8 *sign_value;
	size_t sign_value_len;

	/* Extensions */
	unsigned int extensions_present;
#define X509_EXT_BASIC_CONSTRAINTS		(1 << 0)
#define X509_EXT_PATH_LEN_CONSTRAINT		(1 << 1)
#define X509_EXT_KEY_USAGE			(1 << 2)
#define X509_EXT_SUBJECT_ALT_NAME		(1 << 3)
#define X509_EXT_ISSUER_ALT_NAME		(1 << 4)

	/* BasicConstraints */
	int ca; /* cA */
	unsigned long path_len_constraint; /* pathLenConstraint */

	/* KeyUsage */
	unsigned long key_usage;
#define X509_KEY_USAGE_DIGITAL_SIGNATURE	(1 << 0)
#define X509_KEY_USAGE_NON_REPUDIATION		(1 << 1)
#define X509_KEY_USAGE_KEY_ENCIPHERMENT		(1 << 2)
#define X509_KEY_USAGE_DATA_ENCIPHERMENT	(1 << 3)
#define X509_KEY_USAGE_KEY_AGREEMENT		(1 << 4)
#define X509_KEY_USAGE_KEY_CERT_SIGN		(1 << 5)
#define X509_KEY_USAGE_CRL_SIGN			(1 << 6)
#define X509_KEY_USAGE_ENCIPHER_ONLY		(1 << 7)
#define X509_KEY_USAGE_DECIPHER_ONLY		(1 << 8)

	/*
	 * The DER format certificate follows struct x509_certificate. These
	 * pointers point to that buffer.
	 */
	const u8 *cert_start;
	size_t cert_len;
	const u8 *tbs_cert_start;
	size_t tbs_cert_len;
};

enum {
	X509_VALIDATE_OK,
	X509_VALIDATE_BAD_CERTIFICATE,
	X509_VALIDATE_UNSUPPORTED_CERTIFICATE,
	X509_VALIDATE_CERTIFICATE_REVOKED,
	X509_VALIDATE_CERTIFICATE_EXPIRED,
	X509_VALIDATE_CERTIFICATE_UNKNOWN,
	X509_VALIDATE_UNKNOWN_CA
};

void x509_certificate_free(struct x509_certificate *cert);
struct x509_certificate * x509_certificate_parse(const u8 *buf, size_t len);
void x509_name_string(struct x509_name *name, char *buf, size_t len);
int x509_name_compare(struct x509_name *a, struct x509_name *b);
void x509_certificate_chain_free(struct x509_certificate *cert);
int x509_certificate_check_signature(struct x509_certificate *issuer,
				     struct x509_certificate *cert);
int x509_certificate_chain_validate(struct x509_certificate *trusted,
				    struct x509_certificate *chain,
				    int *reason);
struct x509_certificate *
x509_certificate_get_subject(struct x509_certificate *chain,
			     struct x509_name *name);
int x509_certificate_self_signed(struct x509_certificate *cert);

#endif /* X509V3_H */
