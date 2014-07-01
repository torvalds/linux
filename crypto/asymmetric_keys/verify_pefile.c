/* Parse a signed PE binary
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PEFILE: "fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pe.h>
#include <linux/asn1.h>
#include <crypto/pkcs7.h>
#include <crypto/hash.h>
#include "verify_pefile.h"

/*
 * Parse a PE binary.
 */
static int pefile_parse_binary(const void *pebuf, unsigned int pelen,
			       struct pefile_context *ctx)
{
	const struct mz_hdr *mz = pebuf;
	const struct pe_hdr *pe;
	const struct pe32_opt_hdr *pe32;
	const struct pe32plus_opt_hdr *pe64;
	const struct data_directory *ddir;
	const struct data_dirent *dde;
	const struct section_header *secs, *sec;
	size_t cursor, datalen = pelen;

	kenter("");

#define chkaddr(base, x, s)						\
	do {								\
		if ((x) < base || (s) >= datalen || (x) > datalen - (s)) \
			return -ELIBBAD;				\
	} while (0)

	chkaddr(0, 0, sizeof(*mz));
	if (mz->magic != MZ_MAGIC)
		return -ELIBBAD;
	cursor = sizeof(*mz);

	chkaddr(cursor, mz->peaddr, sizeof(*pe));
	pe = pebuf + mz->peaddr;
	if (pe->magic != PE_MAGIC)
		return -ELIBBAD;
	cursor = mz->peaddr + sizeof(*pe);

	chkaddr(0, cursor, sizeof(pe32->magic));
	pe32 = pebuf + cursor;
	pe64 = pebuf + cursor;

	switch (pe32->magic) {
	case PE_OPT_MAGIC_PE32:
		chkaddr(0, cursor, sizeof(*pe32));
		ctx->image_checksum_offset =
			(unsigned long)&pe32->csum - (unsigned long)pebuf;
		ctx->header_size = pe32->header_size;
		cursor += sizeof(*pe32);
		ctx->n_data_dirents = pe32->data_dirs;
		break;

	case PE_OPT_MAGIC_PE32PLUS:
		chkaddr(0, cursor, sizeof(*pe64));
		ctx->image_checksum_offset =
			(unsigned long)&pe64->csum - (unsigned long)pebuf;
		ctx->header_size = pe64->header_size;
		cursor += sizeof(*pe64);
		ctx->n_data_dirents = pe64->data_dirs;
		break;

	default:
		pr_debug("Unknown PEOPT magic = %04hx\n", pe32->magic);
		return -ELIBBAD;
	}

	pr_debug("checksum @ %x\n", ctx->image_checksum_offset);
	pr_debug("header size = %x\n", ctx->header_size);

	if (cursor >= ctx->header_size || ctx->header_size >= datalen)
		return -ELIBBAD;

	if (ctx->n_data_dirents > (ctx->header_size - cursor) / sizeof(*dde))
		return -ELIBBAD;

	ddir = pebuf + cursor;
	cursor += sizeof(*dde) * ctx->n_data_dirents;

	ctx->cert_dirent_offset =
		(unsigned long)&ddir->certs - (unsigned long)pebuf;
	ctx->certs_size = ddir->certs.size;

	if (!ddir->certs.virtual_address || !ddir->certs.size) {
		pr_debug("Unsigned PE binary\n");
		return -EKEYREJECTED;
	}

	chkaddr(ctx->header_size, ddir->certs.virtual_address,
		ddir->certs.size);
	ctx->sig_offset = ddir->certs.virtual_address;
	ctx->sig_len = ddir->certs.size;
	pr_debug("cert = %x @%x [%*ph]\n",
		 ctx->sig_len, ctx->sig_offset,
		 ctx->sig_len, pebuf + ctx->sig_offset);

	ctx->n_sections = pe->sections;
	if (ctx->n_sections > (ctx->header_size - cursor) / sizeof(*sec))
		return -ELIBBAD;
	ctx->secs = secs = pebuf + cursor;

	return 0;
}

/*
 * Check and strip the PE wrapper from around the signature and check that the
 * remnant looks something like PKCS#7.
 */
static int pefile_strip_sig_wrapper(const void *pebuf,
				    struct pefile_context *ctx)
{
	struct win_certificate wrapper;
	const u8 *pkcs7;

	if (ctx->sig_len < sizeof(wrapper)) {
		pr_debug("Signature wrapper too short\n");
		return -ELIBBAD;
	}

	memcpy(&wrapper, pebuf + ctx->sig_offset, sizeof(wrapper));
	pr_debug("sig wrapper = { %x, %x, %x }\n",
		 wrapper.length, wrapper.revision, wrapper.cert_type);

	/* Both pesign and sbsign round up the length of certificate table
	 * (in optional header data directories) to 8 byte alignment.
	 */
	if (round_up(wrapper.length, 8) != ctx->sig_len) {
		pr_debug("Signature wrapper len wrong\n");
		return -ELIBBAD;
	}
	if (wrapper.revision != WIN_CERT_REVISION_2_0) {
		pr_debug("Signature is not revision 2.0\n");
		return -ENOTSUPP;
	}
	if (wrapper.cert_type != WIN_CERT_TYPE_PKCS_SIGNED_DATA) {
		pr_debug("Signature certificate type is not PKCS\n");
		return -ENOTSUPP;
	}

	/* Looks like actual pkcs signature length is in wrapper->length.
	 * size obtained from data dir entries lists the total size of
	 * certificate table which is also aligned to octawrod boundary.
	 *
	 * So set signature length field appropriately.
	 */
	ctx->sig_len = wrapper.length;
	ctx->sig_offset += sizeof(wrapper);
	ctx->sig_len -= sizeof(wrapper);
	if (ctx->sig_len == 0) {
		pr_debug("Signature data missing\n");
		return -EKEYREJECTED;
	}

	/* What's left should a PKCS#7 cert */
	pkcs7 = pebuf + ctx->sig_offset;
	if (pkcs7[0] == (ASN1_CONS_BIT | ASN1_SEQ)) {
		if (pkcs7[1] == 0x82 &&
		    pkcs7[2] == (((ctx->sig_len - 4) >> 8) & 0xff) &&
		    pkcs7[3] ==  ((ctx->sig_len - 4)       & 0xff))
			return 0;
		if (pkcs7[1] == 0x80)
			return 0;
		if (pkcs7[1] > 0x82)
			return -EMSGSIZE;
	}

	pr_debug("Signature data not PKCS#7\n");
	return -ELIBBAD;
}

/**
 * verify_pefile_signature - Verify the signature on a PE binary image
 * @pebuf: Buffer containing the PE binary image
 * @pelen: Length of the binary image
 * @trust_keyring: Signing certificates to use as starting points
 * @_trusted: Set to true if trustworth, false otherwise
 *
 * Validate that the certificate chain inside the PKCS#7 message inside the PE
 * binary image intersects keys we already know and trust.
 *
 * Returns, in order of descending priority:
 *
 *  (*) -ELIBBAD if the image cannot be parsed, or:
 *
 *  (*) -EKEYREJECTED if a signature failed to match for which we have a valid
 *	key, or:
 *
 *  (*) 0 if at least one signature chain intersects with the keys in the trust
 *	keyring, or:
 *
 *  (*) -ENOPKG if a suitable crypto module couldn't be found for a check on a
 *	chain.
 *
 *  (*) -ENOKEY if we couldn't find a match for any of the signature chains in
 *	the message.
 *
 * May also return -ENOMEM.
 */
int verify_pefile_signature(const void *pebuf, unsigned pelen,
			    struct key *trusted_keyring, bool *_trusted)
{
	struct pkcs7_message *pkcs7;
	struct pefile_context ctx;
	const void *data;
	size_t datalen;
	int ret;

	kenter("");

	memset(&ctx, 0, sizeof(ctx));
	ret = pefile_parse_binary(pebuf, pelen, &ctx);
	if (ret < 0)
		return ret;

	ret = pefile_strip_sig_wrapper(pebuf, &ctx);
	if (ret < 0)
		return ret;

	pkcs7 = pkcs7_parse_message(pebuf + ctx.sig_offset, ctx.sig_len);
	if (IS_ERR(pkcs7))
		return PTR_ERR(pkcs7);
	ctx.pkcs7 = pkcs7;

	ret = pkcs7_get_content_data(ctx.pkcs7, &data, &datalen, false);
	if (ret < 0 || datalen == 0) {
		pr_devel("PKCS#7 message does not contain data\n");
		ret = -EBADMSG;
		goto error;
	}

	ret = -ENOANO; // Not yet complete

error:
	pkcs7_free_message(ctx.pkcs7);
	return ret;
}
