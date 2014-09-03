/* PKCS#7 crypto data parser internal definitions
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/oid_registry.h>
#include <crypto/pkcs7.h>
#include "x509_parser.h"

#define kenter(FMT, ...) \
	pr_devel("==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	pr_devel("<== %s()"FMT"\n", __func__, ##__VA_ARGS__)

struct pkcs7_signed_info {
	struct pkcs7_signed_info *next;
	struct x509_certificate *signer; /* Signing certificate (in msg->certs) */
	unsigned index;
	bool trusted;

	/* Message digest - the digest of the Content Data (or NULL) */
	const void	*msgdigest;
	unsigned	msgdigest_len;

	/* Authenticated Attribute data (or NULL) */
	unsigned	authattrs_len;
	const void	*authattrs;

	/* Issuing cert serial number and issuer's name */
	const void	*raw_serial;
	unsigned	raw_serial_size;
	unsigned	raw_issuer_size;
	const void	*raw_issuer;

	/* Message signature.
	 *
	 * This contains the generated digest of _either_ the Content Data or
	 * the Authenticated Attributes [RFC2315 9.3].  If the latter, one of
	 * the attributes contains the digest of the the Content Data within
	 * it.
	 */
	struct public_key_signature sig;
};

struct pkcs7_message {
	struct x509_certificate *certs;	/* Certificate list */
	struct x509_certificate *crl;	/* Revocation list */
	struct pkcs7_signed_info *signed_infos;

	/* Content Data (or NULL) */
	enum OID	data_type;	/* Type of Data */
	size_t		data_len;	/* Length of Data */
	size_t		data_hdrlen;	/* Length of Data ASN.1 header */
	const void	*data;		/* Content Data (or 0) */
};
