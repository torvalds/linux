/* X.509 certificate parser internal definitions
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/time.h>
#include <crypto/public_key.h>
#include <keys/asymmetric-type.h>

struct x509_certificate {
	struct x509_certificate *next;
	struct x509_certificate *signer;	/* Certificate that signed this one */
	struct public_key *pub;			/* Public key details */
	struct public_key_signature *sig;	/* Signature parameters */
	char		*issuer;		/* Name of certificate issuer */
	char		*subject;		/* Name of certificate subject */
	struct asymmetric_key_id *id;		/* Issuer + Serial number */
	struct asymmetric_key_id *skid;		/* Subject + subjectKeyId (optional) */
	time64_t	valid_from;
	time64_t	valid_to;
	const void	*tbs;			/* Signed data */
	unsigned	tbs_size;		/* Size of signed data */
	unsigned	raw_sig_size;		/* Size of sigature */
	const void	*raw_sig;		/* Signature data */
	const void	*raw_serial;		/* Raw serial number in ASN.1 */
	unsigned	raw_serial_size;
	unsigned	raw_issuer_size;
	const void	*raw_issuer;		/* Raw issuer name in ASN.1 */
	const void	*raw_subject;		/* Raw subject name in ASN.1 */
	unsigned	raw_subject_size;
	unsigned	raw_skid_size;
	const void	*raw_skid;		/* Raw subjectKeyId in ASN.1 */
	unsigned	index;
	bool		seen;			/* Infinite recursion prevention */
	bool		verified;
	bool		self_signed;		/* T if self-signed (check unsupported_sig too) */
	bool		unsupported_key;	/* T if key uses unsupported crypto */
	bool		unsupported_sig;	/* T if signature uses unsupported crypto */
	bool		blacklisted;
};

/*
 * x509_cert_parser.c
 */
extern void x509_free_certificate(struct x509_certificate *cert);
extern struct x509_certificate *x509_cert_parse(const void *data, size_t datalen);
extern int x509_decode_time(time64_t *_t,  size_t hdrlen,
			    unsigned char tag,
			    const unsigned char *value, size_t vlen);

/*
 * x509_public_key.c
 */
extern int x509_get_sig_params(struct x509_certificate *cert);
extern int x509_check_for_self_signed(struct x509_certificate *cert);
