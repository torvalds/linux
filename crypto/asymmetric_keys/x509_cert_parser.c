/* X.509 certificate parser
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "X.509: "fmt
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/oid_registry.h>
#include "public_key.h"
#include "x509_parser.h"
#include "x509-asn1.h"
#include "x509_akid-asn1.h"
#include "x509_rsakey-asn1.h"

struct x509_parse_context {
	struct x509_certificate	*cert;		/* Certificate being constructed */
	unsigned long	data;			/* Start of data */
	const void	*cert_start;		/* Start of cert content */
	const void	*key;			/* Key data */
	size_t		key_size;		/* Size of key data */
	enum OID	last_oid;		/* Last OID encountered */
	enum OID	algo_oid;		/* Algorithm OID */
	unsigned char	nr_mpi;			/* Number of MPIs stored */
	u8		o_size;			/* Size of organizationName (O) */
	u8		cn_size;		/* Size of commonName (CN) */
	u8		email_size;		/* Size of emailAddress */
	u16		o_offset;		/* Offset of organizationName (O) */
	u16		cn_offset;		/* Offset of commonName (CN) */
	u16		email_offset;		/* Offset of emailAddress */
	unsigned	raw_akid_size;
	const void	*raw_akid;		/* Raw authorityKeyId in ASN.1 */
	const void	*akid_raw_issuer;	/* Raw directoryName in authorityKeyId */
	unsigned	akid_raw_issuer_size;
};

/*
 * Free an X.509 certificate
 */
void x509_free_certificate(struct x509_certificate *cert)
{
	if (cert) {
		public_key_destroy(cert->pub);
		kfree(cert->issuer);
		kfree(cert->subject);
		kfree(cert->id);
		kfree(cert->skid);
		kfree(cert->akid_id);
		kfree(cert->akid_skid);
		kfree(cert->sig.digest);
		mpi_free(cert->sig.rsa.s);
		kfree(cert);
	}
}
EXPORT_SYMBOL_GPL(x509_free_certificate);

/*
 * Parse an X.509 certificate
 */
struct x509_certificate *x509_cert_parse(const void *data, size_t datalen)
{
	struct x509_certificate *cert;
	struct x509_parse_context *ctx;
	struct asymmetric_key_id *kid;
	long ret;

	ret = -ENOMEM;
	cert = kzalloc(sizeof(struct x509_certificate), GFP_KERNEL);
	if (!cert)
		goto error_no_cert;
	cert->pub = kzalloc(sizeof(struct public_key), GFP_KERNEL);
	if (!cert->pub)
		goto error_no_ctx;
	ctx = kzalloc(sizeof(struct x509_parse_context), GFP_KERNEL);
	if (!ctx)
		goto error_no_ctx;

	ctx->cert = cert;
	ctx->data = (unsigned long)data;

	/* Attempt to decode the certificate */
	ret = asn1_ber_decoder(&x509_decoder, ctx, data, datalen);
	if (ret < 0)
		goto error_decode;

	/* Decode the AuthorityKeyIdentifier */
	if (ctx->raw_akid) {
		pr_devel("AKID: %u %*phN\n",
			 ctx->raw_akid_size, ctx->raw_akid_size, ctx->raw_akid);
		ret = asn1_ber_decoder(&x509_akid_decoder, ctx,
				       ctx->raw_akid, ctx->raw_akid_size);
		if (ret < 0) {
			pr_warn("Couldn't decode AuthKeyIdentifier\n");
			goto error_decode;
		}
	}

	/* Decode the public key */
	ret = asn1_ber_decoder(&x509_rsakey_decoder, ctx,
			       ctx->key, ctx->key_size);
	if (ret < 0)
		goto error_decode;

	/* Generate cert issuer + serial number key ID */
	kid = asymmetric_key_generate_id(cert->raw_serial,
					 cert->raw_serial_size,
					 cert->raw_issuer,
					 cert->raw_issuer_size);
	if (IS_ERR(kid)) {
		ret = PTR_ERR(kid);
		goto error_decode;
	}
	cert->id = kid;

	kfree(ctx);
	return cert;

error_decode:
	kfree(ctx);
error_no_ctx:
	x509_free_certificate(cert);
error_no_cert:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(x509_cert_parse);

/*
 * Note an OID when we find one for later processing when we know how
 * to interpret it.
 */
int x509_note_OID(void *context, size_t hdrlen,
	     unsigned char tag,
	     const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	ctx->last_oid = look_up_OID(value, vlen);
	if (ctx->last_oid == OID__NR) {
		char buffer[50];
		sprint_oid(value, vlen, buffer, sizeof(buffer));
		pr_debug("Unknown OID: [%lu] %s\n",
			 (unsigned long)value - ctx->data, buffer);
	}
	return 0;
}

/*
 * Save the position of the TBS data so that we can check the signature over it
 * later.
 */
int x509_note_tbs_certificate(void *context, size_t hdrlen,
			      unsigned char tag,
			      const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	pr_debug("x509_note_tbs_certificate(,%zu,%02x,%ld,%zu)!\n",
		 hdrlen, tag, (unsigned long)value - ctx->data, vlen);

	ctx->cert->tbs = value - hdrlen;
	ctx->cert->tbs_size = vlen + hdrlen;
	return 0;
}

/*
 * Record the public key algorithm
 */
int x509_note_pkey_algo(void *context, size_t hdrlen,
			unsigned char tag,
			const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	pr_debug("PubKey Algo: %u\n", ctx->last_oid);

	switch (ctx->last_oid) {
	case OID_md2WithRSAEncryption:
	case OID_md3WithRSAEncryption:
	default:
		return -ENOPKG; /* Unsupported combination */

	case OID_md4WithRSAEncryption:
		ctx->cert->sig.pkey_hash_algo = HASH_ALGO_MD5;
		ctx->cert->sig.pkey_algo = PKEY_ALGO_RSA;
		break;

	case OID_sha1WithRSAEncryption:
		ctx->cert->sig.pkey_hash_algo = HASH_ALGO_SHA1;
		ctx->cert->sig.pkey_algo = PKEY_ALGO_RSA;
		break;

	case OID_sha256WithRSAEncryption:
		ctx->cert->sig.pkey_hash_algo = HASH_ALGO_SHA256;
		ctx->cert->sig.pkey_algo = PKEY_ALGO_RSA;
		break;

	case OID_sha384WithRSAEncryption:
		ctx->cert->sig.pkey_hash_algo = HASH_ALGO_SHA384;
		ctx->cert->sig.pkey_algo = PKEY_ALGO_RSA;
		break;

	case OID_sha512WithRSAEncryption:
		ctx->cert->sig.pkey_hash_algo = HASH_ALGO_SHA512;
		ctx->cert->sig.pkey_algo = PKEY_ALGO_RSA;
		break;

	case OID_sha224WithRSAEncryption:
		ctx->cert->sig.pkey_hash_algo = HASH_ALGO_SHA224;
		ctx->cert->sig.pkey_algo = PKEY_ALGO_RSA;
		break;
	}

	ctx->algo_oid = ctx->last_oid;
	return 0;
}

/*
 * Note the whereabouts and type of the signature.
 */
int x509_note_signature(void *context, size_t hdrlen,
			unsigned char tag,
			const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	pr_debug("Signature type: %u size %zu\n", ctx->last_oid, vlen);

	if (ctx->last_oid != ctx->algo_oid) {
		pr_warn("Got cert with pkey (%u) and sig (%u) algorithm OIDs\n",
			ctx->algo_oid, ctx->last_oid);
		return -EINVAL;
	}

	ctx->cert->raw_sig = value;
	ctx->cert->raw_sig_size = vlen;
	return 0;
}

/*
 * Note the certificate serial number
 */
int x509_note_serial(void *context, size_t hdrlen,
		     unsigned char tag,
		     const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	ctx->cert->raw_serial = value;
	ctx->cert->raw_serial_size = vlen;
	return 0;
}

/*
 * Note some of the name segments from which we'll fabricate a name.
 */
int x509_extract_name_segment(void *context, size_t hdrlen,
			      unsigned char tag,
			      const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	switch (ctx->last_oid) {
	case OID_commonName:
		ctx->cn_size = vlen;
		ctx->cn_offset = (unsigned long)value - ctx->data;
		break;
	case OID_organizationName:
		ctx->o_size = vlen;
		ctx->o_offset = (unsigned long)value - ctx->data;
		break;
	case OID_email_address:
		ctx->email_size = vlen;
		ctx->email_offset = (unsigned long)value - ctx->data;
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Fabricate and save the issuer and subject names
 */
static int x509_fabricate_name(struct x509_parse_context *ctx, size_t hdrlen,
			       unsigned char tag,
			       char **_name, size_t vlen)
{
	const void *name, *data = (const void *)ctx->data;
	size_t namesize;
	char *buffer;

	if (*_name)
		return -EINVAL;

	/* Empty name string if no material */
	if (!ctx->cn_size && !ctx->o_size && !ctx->email_size) {
		buffer = kmalloc(1, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;
		buffer[0] = 0;
		goto done;
	}

	if (ctx->cn_size && ctx->o_size) {
		/* Consider combining O and CN, but use only the CN if it is
		 * prefixed by the O, or a significant portion thereof.
		 */
		namesize = ctx->cn_size;
		name = data + ctx->cn_offset;
		if (ctx->cn_size >= ctx->o_size &&
		    memcmp(data + ctx->cn_offset, data + ctx->o_offset,
			   ctx->o_size) == 0)
			goto single_component;
		if (ctx->cn_size >= 7 &&
		    ctx->o_size >= 7 &&
		    memcmp(data + ctx->cn_offset, data + ctx->o_offset, 7) == 0)
			goto single_component;

		buffer = kmalloc(ctx->o_size + 2 + ctx->cn_size + 1,
				 GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		memcpy(buffer,
		       data + ctx->o_offset, ctx->o_size);
		buffer[ctx->o_size + 0] = ':';
		buffer[ctx->o_size + 1] = ' ';
		memcpy(buffer + ctx->o_size + 2,
		       data + ctx->cn_offset, ctx->cn_size);
		buffer[ctx->o_size + 2 + ctx->cn_size] = 0;
		goto done;

	} else if (ctx->cn_size) {
		namesize = ctx->cn_size;
		name = data + ctx->cn_offset;
	} else if (ctx->o_size) {
		namesize = ctx->o_size;
		name = data + ctx->o_offset;
	} else {
		namesize = ctx->email_size;
		name = data + ctx->email_offset;
	}

single_component:
	buffer = kmalloc(namesize + 1, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	memcpy(buffer, name, namesize);
	buffer[namesize] = 0;

done:
	*_name = buffer;
	ctx->cn_size = 0;
	ctx->o_size = 0;
	ctx->email_size = 0;
	return 0;
}

int x509_note_issuer(void *context, size_t hdrlen,
		     unsigned char tag,
		     const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	ctx->cert->raw_issuer = value;
	ctx->cert->raw_issuer_size = vlen;
	return x509_fabricate_name(ctx, hdrlen, tag, &ctx->cert->issuer, vlen);
}

int x509_note_subject(void *context, size_t hdrlen,
		      unsigned char tag,
		      const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	ctx->cert->raw_subject = value;
	ctx->cert->raw_subject_size = vlen;
	return x509_fabricate_name(ctx, hdrlen, tag, &ctx->cert->subject, vlen);
}

/*
 * Extract the data for the public key algorithm
 */
int x509_extract_key_data(void *context, size_t hdrlen,
			  unsigned char tag,
			  const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	if (ctx->last_oid != OID_rsaEncryption)
		return -ENOPKG;

	ctx->cert->pub->pkey_algo = PKEY_ALGO_RSA;

	/* Discard the BIT STRING metadata */
	ctx->key = value + 1;
	ctx->key_size = vlen - 1;
	return 0;
}

/*
 * Extract a RSA public key value
 */
int rsa_extract_mpi(void *context, size_t hdrlen,
		    unsigned char tag,
		    const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	MPI mpi;

	if (ctx->nr_mpi >= ARRAY_SIZE(ctx->cert->pub->mpi)) {
		pr_err("Too many public key MPIs in certificate\n");
		return -EBADMSG;
	}

	mpi = mpi_read_raw_data(value, vlen);
	if (!mpi)
		return -ENOMEM;

	ctx->cert->pub->mpi[ctx->nr_mpi++] = mpi;
	return 0;
}

/* The keyIdentifier in AuthorityKeyIdentifier SEQUENCE is tag(CONT,PRIM,0) */
#define SEQ_TAG_KEYID (ASN1_CONT << 6)

/*
 * Process certificate extensions that are used to qualify the certificate.
 */
int x509_process_extension(void *context, size_t hdrlen,
			   unsigned char tag,
			   const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	struct asymmetric_key_id *kid;
	const unsigned char *v = value;

	pr_debug("Extension: %u\n", ctx->last_oid);

	if (ctx->last_oid == OID_subjectKeyIdentifier) {
		/* Get hold of the key fingerprint */
		if (ctx->cert->skid || vlen < 3)
			return -EBADMSG;
		if (v[0] != ASN1_OTS || v[1] != vlen - 2)
			return -EBADMSG;
		v += 2;
		vlen -= 2;

		ctx->cert->raw_skid_size = vlen;
		ctx->cert->raw_skid = v;
		kid = asymmetric_key_generate_id(v, vlen, "", 0);
		if (IS_ERR(kid))
			return PTR_ERR(kid);
		ctx->cert->skid = kid;
		pr_debug("subjkeyid %*phN\n", kid->len, kid->data);
		return 0;
	}

	if (ctx->last_oid == OID_authorityKeyIdentifier) {
		/* Get hold of the CA key fingerprint */
		ctx->raw_akid = v;
		ctx->raw_akid_size = vlen;
		return 0;
	}

	return 0;
}

/**
 * x509_decode_time - Decode an X.509 time ASN.1 object
 * @_t: The time to fill in
 * @hdrlen: The length of the object header
 * @tag: The object tag
 * @value: The object value
 * @vlen: The size of the object value
 *
 * Decode an ASN.1 universal time or generalised time field into a struct the
 * kernel can handle and check it for validity.  The time is decoded thus:
 *
 *	[RFC5280 §4.1.2.5]
 *	CAs conforming to this profile MUST always encode certificate validity
 *	dates through the year 2049 as UTCTime; certificate validity dates in
 *	2050 or later MUST be encoded as GeneralizedTime.  Conforming
 *	applications MUST be able to process validity dates that are encoded in
 *	either UTCTime or GeneralizedTime.
 */
int x509_decode_time(time64_t *_t,  size_t hdrlen,
		     unsigned char tag,
		     const unsigned char *value, size_t vlen)
{
	static const unsigned char month_lengths[] = { 31, 28, 31, 30, 31, 30,
						       31, 31, 30, 31, 30, 31 };
	const unsigned char *p = value;
	unsigned year, mon, day, hour, min, sec, mon_len;

#define dec2bin(X) ({ unsigned char x = (X) - '0'; if (x > 9) goto invalid_time; x; })
#define DD2bin(P) ({ unsigned x = dec2bin(P[0]) * 10 + dec2bin(P[1]); P += 2; x; })

	if (tag == ASN1_UNITIM) {
		/* UTCTime: YYMMDDHHMMSSZ */
		if (vlen != 13)
			goto unsupported_time;
		year = DD2bin(p);
		if (year >= 50)
			year += 1900;
		else
			year += 2000;
	} else if (tag == ASN1_GENTIM) {
		/* GenTime: YYYYMMDDHHMMSSZ */
		if (vlen != 15)
			goto unsupported_time;
		year = DD2bin(p) * 100 + DD2bin(p);
		if (year >= 1950 && year <= 2049)
			goto invalid_time;
	} else {
		goto unsupported_time;
	}

	mon  = DD2bin(p);
	day = DD2bin(p);
	hour = DD2bin(p);
	min  = DD2bin(p);
	sec  = DD2bin(p);

	if (*p != 'Z')
		goto unsupported_time;

	if (year < 1970 ||
	    mon < 1 || mon > 12)
		goto invalid_time;

	mon_len = month_lengths[mon - 1];
	if (mon == 2) {
		if (year % 4 == 0) {
			mon_len = 29;
			if (year % 100 == 0) {
				mon_len = 28;
				if (year % 400 == 0)
					mon_len = 29;
			}
		}
	}

	if (day < 1 || day > mon_len ||
	    hour > 23 ||
	    min > 59 ||
	    sec > 59)
		goto invalid_time;

	*_t = mktime64(year, mon, day, hour, min, sec);
	return 0;

unsupported_time:
	pr_debug("Got unsupported time [tag %02x]: '%*phN'\n",
		 tag, (int)vlen, value);
	return -EBADMSG;
invalid_time:
	pr_debug("Got invalid time [tag %02x]: '%*phN'\n",
		 tag, (int)vlen, value);
	return -EBADMSG;
}
EXPORT_SYMBOL_GPL(x509_decode_time);

int x509_note_not_before(void *context, size_t hdrlen,
			 unsigned char tag,
			 const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	return x509_decode_time(&ctx->cert->valid_from, hdrlen, tag, value, vlen);
}

int x509_note_not_after(void *context, size_t hdrlen,
			unsigned char tag,
			const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	return x509_decode_time(&ctx->cert->valid_to, hdrlen, tag, value, vlen);
}

/*
 * Note a key identifier-based AuthorityKeyIdentifier
 */
int x509_akid_note_kid(void *context, size_t hdrlen,
		       unsigned char tag,
		       const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	struct asymmetric_key_id *kid;

	pr_debug("AKID: keyid: %*phN\n", (int)vlen, value);

	if (ctx->cert->akid_skid)
		return 0;

	kid = asymmetric_key_generate_id(value, vlen, "", 0);
	if (IS_ERR(kid))
		return PTR_ERR(kid);
	pr_debug("authkeyid %*phN\n", kid->len, kid->data);
	ctx->cert->akid_skid = kid;
	return 0;
}

/*
 * Note a directoryName in an AuthorityKeyIdentifier
 */
int x509_akid_note_name(void *context, size_t hdrlen,
			unsigned char tag,
			const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;

	pr_debug("AKID: name: %*phN\n", (int)vlen, value);

	ctx->akid_raw_issuer = value;
	ctx->akid_raw_issuer_size = vlen;
	return 0;
}

/*
 * Note a serial number in an AuthorityKeyIdentifier
 */
int x509_akid_note_serial(void *context, size_t hdrlen,
			  unsigned char tag,
			  const void *value, size_t vlen)
{
	struct x509_parse_context *ctx = context;
	struct asymmetric_key_id *kid;

	pr_debug("AKID: serial: %*phN\n", (int)vlen, value);

	if (!ctx->akid_raw_issuer || ctx->cert->akid_id)
		return 0;

	kid = asymmetric_key_generate_id(value,
					 vlen,
					 ctx->akid_raw_issuer,
					 ctx->akid_raw_issuer_size);
	if (IS_ERR(kid))
		return PTR_ERR(kid);

	pr_debug("authkeyid %*phN\n", kid->len, kid->data);
	ctx->cert->akid_id = kid;
	return 0;
}
