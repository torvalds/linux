/* PKCS#8 Private Key parser [RFC 5208].
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PKCS8: "fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/oid_registry.h>
#include <keys/asymmetric-subtype.h>
#include <keys/asymmetric-parser.h>
#include <crypto/public_key.h>
#include "pkcs8.asn1.h"

struct pkcs8_parse_context {
	struct public_key *pub;
	unsigned long	data;			/* Start of data */
	enum OID	last_oid;		/* Last OID encountered */
	enum OID	algo_oid;		/* Algorithm OID */
	u32		key_size;
	const void	*key;
};

/*
 * Note an OID when we find one for later processing when we know how to
 * interpret it.
 */
int pkcs8_note_OID(void *context, size_t hdrlen,
		   unsigned char tag,
		   const void *value, size_t vlen)
{
	struct pkcs8_parse_context *ctx = context;

	ctx->last_oid = look_up_OID(value, vlen);
	if (ctx->last_oid == OID__NR) {
		char buffer[50];

		sprint_oid(value, vlen, buffer, sizeof(buffer));
		pr_info("Unknown OID: [%lu] %s\n",
			(unsigned long)value - ctx->data, buffer);
	}
	return 0;
}

/*
 * Note the version number of the ASN.1 blob.
 */
int pkcs8_note_version(void *context, size_t hdrlen,
		       unsigned char tag,
		       const void *value, size_t vlen)
{
	if (vlen != 1 || ((const u8 *)value)[0] != 0) {
		pr_warn("Unsupported PKCS#8 version\n");
		return -EBADMSG;
	}
	return 0;
}

/*
 * Note the public algorithm.
 */
int pkcs8_note_algo(void *context, size_t hdrlen,
		    unsigned char tag,
		    const void *value, size_t vlen)
{
	struct pkcs8_parse_context *ctx = context;

	if (ctx->last_oid != OID_rsaEncryption)
		return -ENOPKG;

	ctx->pub->pkey_algo = "rsa";
	return 0;
}

/*
 * Note the key data of the ASN.1 blob.
 */
int pkcs8_note_key(void *context, size_t hdrlen,
		   unsigned char tag,
		   const void *value, size_t vlen)
{
	struct pkcs8_parse_context *ctx = context;

	ctx->key = value;
	ctx->key_size = vlen;
	return 0;
}

/*
 * Parse a PKCS#8 private key blob.
 */
static struct public_key *pkcs8_parse(const void *data, size_t datalen)
{
	struct pkcs8_parse_context ctx;
	struct public_key *pub;
	long ret;

	memset(&ctx, 0, sizeof(ctx));

	ret = -ENOMEM;
	ctx.pub = kzalloc(sizeof(struct public_key), GFP_KERNEL);
	if (!ctx.pub)
		goto error;

	ctx.data = (unsigned long)data;

	/* Attempt to decode the private key */
	ret = asn1_ber_decoder(&pkcs8_decoder, &ctx, data, datalen);
	if (ret < 0)
		goto error_decode;

	ret = -ENOMEM;
	pub = ctx.pub;
	pub->key = kmemdup(ctx.key, ctx.key_size, GFP_KERNEL);
	if (!pub->key)
		goto error_decode;

	pub->keylen = ctx.key_size;
	pub->key_is_private = true;
	return pub;

error_decode:
	kfree(ctx.pub);
error:
	return ERR_PTR(ret);
}

/*
 * Attempt to parse a data blob for a key as a PKCS#8 private key.
 */
static int pkcs8_key_preparse(struct key_preparsed_payload *prep)
{
	struct public_key *pub;

	pub = pkcs8_parse(prep->data, prep->datalen);
	if (IS_ERR(pub))
		return PTR_ERR(pub);

	pr_devel("Cert Key Algo: %s\n", pub->pkey_algo);
	pub->id_type = "PKCS8";

	/* We're pinning the module by being linked against it */
	__module_get(public_key_subtype.owner);
	prep->payload.data[asym_subtype] = &public_key_subtype;
	prep->payload.data[asym_key_ids] = NULL;
	prep->payload.data[asym_crypto] = pub;
	prep->payload.data[asym_auth] = NULL;
	prep->quotalen = 100;
	return 0;
}

static struct asymmetric_key_parser pkcs8_key_parser = {
	.owner	= THIS_MODULE,
	.name	= "pkcs8",
	.parse	= pkcs8_key_preparse,
};

/*
 * Module stuff
 */
static int __init pkcs8_key_init(void)
{
	return register_asymmetric_key_parser(&pkcs8_key_parser);
}

static void __exit pkcs8_key_exit(void)
{
	unregister_asymmetric_key_parser(&pkcs8_key_parser);
}

module_init(pkcs8_key_init);
module_exit(pkcs8_key_exit);

MODULE_DESCRIPTION("PKCS#8 certificate parser");
MODULE_LICENSE("GPL");
