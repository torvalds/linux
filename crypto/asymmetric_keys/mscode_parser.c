// SPDX-License-Identifier: GPL-2.0-or-later
/* Parse a Microsoft Individual Code Signing blob
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "MSCODE: "fmt
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/oid_registry.h>
#include <crypto/pkcs7.h>
#include "verify_pefile.h"
#include "mscode.asn1.h"

/*
 * Parse a Microsoft Individual Code Signing blob
 */
int mscode_parse(void *_ctx, const void *content_data, size_t data_len,
		 size_t asn1hdrlen)
{
	struct pefile_context *ctx = _ctx;

	content_data -= asn1hdrlen;
	data_len += asn1hdrlen;
	pr_devel("Data: %zu [%*ph]\n", data_len, (unsigned)(data_len),
		 content_data);

	return asn1_ber_decoder(&mscode_decoder, ctx, content_data, data_len);
}

/*
 * Check the content type OID
 */
int mscode_note_content_type(void *context, size_t hdrlen,
			     unsigned char tag,
			     const void *value, size_t vlen)
{
	enum OID oid;

	oid = look_up_OID(value, vlen);
	if (oid == OID__NR) {
		char buffer[50];

		sprint_oid(value, vlen, buffer, sizeof(buffer));
		pr_err("Unknown OID: %s\n", buffer);
		return -EBADMSG;
	}

	/*
	 * pesign utility had a bug where it was putting
	 * OID_msIndividualSPKeyPurpose instead of OID_msPeImageDataObjId
	 * So allow both OIDs.
	 */
	if (oid != OID_msPeImageDataObjId &&
	    oid != OID_msIndividualSPKeyPurpose) {
		pr_err("Unexpected content type OID %u\n", oid);
		return -EBADMSG;
	}

	return 0;
}

/*
 * Note the digest algorithm OID
 */
int mscode_note_digest_algo(void *context, size_t hdrlen,
			    unsigned char tag,
			    const void *value, size_t vlen)
{
	struct pefile_context *ctx = context;
	char buffer[50];
	enum OID oid;

	oid = look_up_OID(value, vlen);
	switch (oid) {
	case OID_sha1:
		ctx->digest_algo = "sha1";
		break;
	case OID_sha256:
		ctx->digest_algo = "sha256";
		break;
	case OID_sha384:
		ctx->digest_algo = "sha384";
		break;
	case OID_sha512:
		ctx->digest_algo = "sha512";
		break;
	case OID_sha224:
		ctx->digest_algo = "sha224";
		break;

	case OID__NR:
		sprint_oid(value, vlen, buffer, sizeof(buffer));
		pr_err("Unknown OID: %s\n", buffer);
		return -EBADMSG;

	default:
		pr_err("Unsupported content type: %u\n", oid);
		return -ENOPKG;
	}

	return 0;
}

/*
 * Note the digest we're guaranteeing with this certificate
 */
int mscode_note_digest(void *context, size_t hdrlen,
		       unsigned char tag,
		       const void *value, size_t vlen)
{
	struct pefile_context *ctx = context;

	ctx->digest = kmemdup(value, vlen, GFP_KERNEL);
	if (!ctx->digest)
		return -ENOMEM;

	ctx->digest_len = vlen;

	return 0;
}
