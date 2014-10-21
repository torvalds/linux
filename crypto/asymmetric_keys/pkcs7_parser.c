/* PKCS#7 parser
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PKCS7: "fmt
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/oid_registry.h>
#include "public_key.h"
#include "pkcs7_parser.h"
#include "pkcs7-asn1.h"

struct pkcs7_parse_context {
	struct pkcs7_message	*msg;		/* Message being constructed */
	struct pkcs7_signed_info *sinfo;	/* SignedInfo being constructed */
	struct pkcs7_signed_info **ppsinfo;
	struct x509_certificate *certs;		/* Certificate cache */
	struct x509_certificate **ppcerts;
	unsigned long	data;			/* Start of data */
	enum OID	last_oid;		/* Last OID encountered */
	unsigned	x509_index;
	unsigned	sinfo_index;
	const void	*raw_serial;
	unsigned	raw_serial_size;
	unsigned	raw_issuer_size;
	const void	*raw_issuer;
};

/*
 * Free a signed information block.
 */
static void pkcs7_free_signed_info(struct pkcs7_signed_info *sinfo)
{
	if (sinfo) {
		mpi_free(sinfo->sig.mpi[0]);
		kfree(sinfo->sig.digest);
		kfree(sinfo->signing_cert_id);
		kfree(sinfo);
	}
}

/**
 * pkcs7_free_message - Free a PKCS#7 message
 * @pkcs7: The PKCS#7 message to free
 */
void pkcs7_free_message(struct pkcs7_message *pkcs7)
{
	struct x509_certificate *cert;
	struct pkcs7_signed_info *sinfo;

	if (pkcs7) {
		while (pkcs7->certs) {
			cert = pkcs7->certs;
			pkcs7->certs = cert->next;
			x509_free_certificate(cert);
		}
		while (pkcs7->crl) {
			cert = pkcs7->crl;
			pkcs7->crl = cert->next;
			x509_free_certificate(cert);
		}
		while (pkcs7->signed_infos) {
			sinfo = pkcs7->signed_infos;
			pkcs7->signed_infos = sinfo->next;
			pkcs7_free_signed_info(sinfo);
		}
		kfree(pkcs7);
	}
}
EXPORT_SYMBOL_GPL(pkcs7_free_message);

/**
 * pkcs7_parse_message - Parse a PKCS#7 message
 * @data: The raw binary ASN.1 encoded message to be parsed
 * @datalen: The size of the encoded message
 */
struct pkcs7_message *pkcs7_parse_message(const void *data, size_t datalen)
{
	struct pkcs7_parse_context *ctx;
	struct pkcs7_message *msg = ERR_PTR(-ENOMEM);
	int ret;

	ctx = kzalloc(sizeof(struct pkcs7_parse_context), GFP_KERNEL);
	if (!ctx)
		goto out_no_ctx;
	ctx->msg = kzalloc(sizeof(struct pkcs7_message), GFP_KERNEL);
	if (!ctx->msg)
		goto out_no_msg;
	ctx->sinfo = kzalloc(sizeof(struct pkcs7_signed_info), GFP_KERNEL);
	if (!ctx->sinfo)
		goto out_no_sinfo;

	ctx->data = (unsigned long)data;
	ctx->ppcerts = &ctx->certs;
	ctx->ppsinfo = &ctx->msg->signed_infos;

	/* Attempt to decode the signature */
	ret = asn1_ber_decoder(&pkcs7_decoder, ctx, data, datalen);
	if (ret < 0) {
		msg = ERR_PTR(ret);
		goto out;
	}

	msg = ctx->msg;
	ctx->msg = NULL;

out:
	while (ctx->certs) {
		struct x509_certificate *cert = ctx->certs;
		ctx->certs = cert->next;
		x509_free_certificate(cert);
	}
	pkcs7_free_signed_info(ctx->sinfo);
out_no_sinfo:
	pkcs7_free_message(ctx->msg);
out_no_msg:
	kfree(ctx);
out_no_ctx:
	return msg;
}
EXPORT_SYMBOL_GPL(pkcs7_parse_message);

/**
 * pkcs7_get_content_data - Get access to the PKCS#7 content
 * @pkcs7: The preparsed PKCS#7 message to access
 * @_data: Place to return a pointer to the data
 * @_data_len: Place to return the data length
 * @want_wrapper: True if the ASN.1 object header should be included in the data
 *
 * Get access to the data content of the PKCS#7 message, including, optionally,
 * the header of the ASN.1 object that contains it.  Returns -ENODATA if the
 * data object was missing from the message.
 */
int pkcs7_get_content_data(const struct pkcs7_message *pkcs7,
			   const void **_data, size_t *_data_len,
			   bool want_wrapper)
{
	size_t wrapper;

	if (!pkcs7->data)
		return -ENODATA;

	wrapper = want_wrapper ? pkcs7->data_hdrlen : 0;
	*_data = pkcs7->data - wrapper;
	*_data_len = pkcs7->data_len + wrapper;
	return 0;
}
EXPORT_SYMBOL_GPL(pkcs7_get_content_data);

/*
 * Note an OID when we find one for later processing when we know how
 * to interpret it.
 */
int pkcs7_note_OID(void *context, size_t hdrlen,
		   unsigned char tag,
		   const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	ctx->last_oid = look_up_OID(value, vlen);
	if (ctx->last_oid == OID__NR) {
		char buffer[50];
		sprint_oid(value, vlen, buffer, sizeof(buffer));
		printk("PKCS7: Unknown OID: [%lu] %s\n",
		       (unsigned long)value - ctx->data, buffer);
	}
	return 0;
}

/*
 * Note the digest algorithm for the signature.
 */
int pkcs7_sig_note_digest_algo(void *context, size_t hdrlen,
			       unsigned char tag,
			       const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	switch (ctx->last_oid) {
	case OID_md4:
		ctx->sinfo->sig.pkey_hash_algo = HASH_ALGO_MD4;
		break;
	case OID_md5:
		ctx->sinfo->sig.pkey_hash_algo = HASH_ALGO_MD5;
		break;
	case OID_sha1:
		ctx->sinfo->sig.pkey_hash_algo = HASH_ALGO_SHA1;
		break;
	case OID_sha256:
		ctx->sinfo->sig.pkey_hash_algo = HASH_ALGO_SHA256;
		break;
	default:
		printk("Unsupported digest algo: %u\n", ctx->last_oid);
		return -ENOPKG;
	}
	return 0;
}

/*
 * Note the public key algorithm for the signature.
 */
int pkcs7_sig_note_pkey_algo(void *context, size_t hdrlen,
			     unsigned char tag,
			     const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	switch (ctx->last_oid) {
	case OID_rsaEncryption:
		ctx->sinfo->sig.pkey_algo = PKEY_ALGO_RSA;
		break;
	default:
		printk("Unsupported pkey algo: %u\n", ctx->last_oid);
		return -ENOPKG;
	}
	return 0;
}

/*
 * Extract a certificate and store it in the context.
 */
int pkcs7_extract_cert(void *context, size_t hdrlen,
		       unsigned char tag,
		       const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;
	struct x509_certificate *x509;

	if (tag != ((ASN1_UNIV << 6) | ASN1_CONS_BIT | ASN1_SEQ)) {
		pr_debug("Cert began with tag %02x at %lu\n",
			 tag, (unsigned long)ctx - ctx->data);
		return -EBADMSG;
	}

	/* We have to correct for the header so that the X.509 parser can start
	 * from the beginning.  Note that since X.509 stipulates DER, there
	 * probably shouldn't be an EOC trailer - but it is in PKCS#7 (which
	 * stipulates BER).
	 */
	value -= hdrlen;
	vlen += hdrlen;

	if (((u8*)value)[1] == 0x80)
		vlen += 2; /* Indefinite length - there should be an EOC */

	x509 = x509_cert_parse(value, vlen);
	if (IS_ERR(x509))
		return PTR_ERR(x509);

	x509->index = ++ctx->x509_index;
	pr_debug("Got cert %u for %s\n", x509->index, x509->subject);
	pr_debug("- fingerprint %*phN\n", x509->id->len, x509->id->data);

	*ctx->ppcerts = x509;
	ctx->ppcerts = &x509->next;
	return 0;
}

/*
 * Save the certificate list
 */
int pkcs7_note_certificate_list(void *context, size_t hdrlen,
				unsigned char tag,
				const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	pr_devel("Got cert list (%02x)\n", tag);

	*ctx->ppcerts = ctx->msg->certs;
	ctx->msg->certs = ctx->certs;
	ctx->certs = NULL;
	ctx->ppcerts = &ctx->certs;
	return 0;
}

/*
 * Extract the data from the message and store that and its content type OID in
 * the context.
 */
int pkcs7_note_data(void *context, size_t hdrlen,
		    unsigned char tag,
		    const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	pr_debug("Got data\n");

	ctx->msg->data = value;
	ctx->msg->data_len = vlen;
	ctx->msg->data_hdrlen = hdrlen;
	ctx->msg->data_type = ctx->last_oid;
	return 0;
}

/*
 * Parse authenticated attributes
 */
int pkcs7_sig_note_authenticated_attr(void *context, size_t hdrlen,
				      unsigned char tag,
				      const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	pr_devel("AuthAttr: %02x %zu [%*ph]\n", tag, vlen, (unsigned)vlen, value);

	switch (ctx->last_oid) {
	case OID_messageDigest:
		if (tag != ASN1_OTS)
			return -EBADMSG;
		ctx->sinfo->msgdigest = value;
		ctx->sinfo->msgdigest_len = vlen;
		return 0;
	default:
		return 0;
	}
}

/*
 * Note the set of auth attributes for digestion purposes [RFC2315 9.3]
 */
int pkcs7_sig_note_set_of_authattrs(void *context, size_t hdrlen,
				    unsigned char tag,
				    const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;

	/* We need to switch the 'CONT 0' to a 'SET OF' when we digest */
	ctx->sinfo->authattrs = value - (hdrlen - 1);
	ctx->sinfo->authattrs_len = vlen + (hdrlen - 1);
	return 0;
}

/*
 * Note the issuing certificate serial number
 */
int pkcs7_sig_note_serial(void *context, size_t hdrlen,
			  unsigned char tag,
			  const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;
	ctx->raw_serial = value;
	ctx->raw_serial_size = vlen;
	return 0;
}

/*
 * Note the issuer's name
 */
int pkcs7_sig_note_issuer(void *context, size_t hdrlen,
			  unsigned char tag,
			  const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;
	ctx->raw_issuer = value;
	ctx->raw_issuer_size = vlen;
	return 0;
}

/*
 * Note the signature data
 */
int pkcs7_sig_note_signature(void *context, size_t hdrlen,
			     unsigned char tag,
			     const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;
	MPI mpi;

	BUG_ON(ctx->sinfo->sig.pkey_algo != PKEY_ALGO_RSA);

	mpi = mpi_read_raw_data(value, vlen);
	if (!mpi)
		return -ENOMEM;

	ctx->sinfo->sig.mpi[0] = mpi;
	ctx->sinfo->sig.nr_mpi = 1;
	return 0;
}

/*
 * Note a signature information block
 */
int pkcs7_note_signed_info(void *context, size_t hdrlen,
			   unsigned char tag,
			   const void *value, size_t vlen)
{
	struct pkcs7_parse_context *ctx = context;
	struct pkcs7_signed_info *sinfo = ctx->sinfo;
	struct asymmetric_key_id *kid;

	/* Generate cert issuer + serial number key ID */
	kid = asymmetric_key_generate_id(ctx->raw_serial,
					 ctx->raw_serial_size,
					 ctx->raw_issuer,
					 ctx->raw_issuer_size);
	if (IS_ERR(kid))
		return PTR_ERR(kid);

	sinfo->signing_cert_id = kid;
	sinfo->index = ++ctx->sinfo_index;
	*ctx->ppsinfo = sinfo;
	ctx->ppsinfo = &sinfo->next;
	ctx->sinfo = kzalloc(sizeof(struct pkcs7_signed_info), GFP_KERNEL);
	if (!ctx->sinfo)
		return -ENOMEM;
	return 0;
}
