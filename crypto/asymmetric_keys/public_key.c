/* In-software asymmetric public-key crypto subtype
 *
 * See Documentation/crypto/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PKEY: "fmt
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <keys/asymmetric-subtype.h>
#include <crypto/public_key.h>

MODULE_LICENSE("GPL");

const char *const pkey_algo_name[PKEY_ALGO__LAST] = {
	[PKEY_ALGO_DSA]		= "dsa",
	[PKEY_ALGO_RSA]		= "rsa",
};
EXPORT_SYMBOL_GPL(pkey_algo_name);

const char *const pkey_id_type_name[PKEY_ID_TYPE__LAST] = {
	[PKEY_ID_PGP]		= "PGP",
	[PKEY_ID_X509]		= "X509",
	[PKEY_ID_PKCS7]		= "PKCS#7",
};
EXPORT_SYMBOL_GPL(pkey_id_type_name);

static int (*alg_verify[PKEY_ALGO__LAST])(const struct public_key *pkey,
	const struct public_key_signature *sig) = {
	NULL,
	rsa_verify_signature
};

/*
 * Provide a part of a description of the key for /proc/keys.
 */
static void public_key_describe(const struct key *asymmetric_key,
				struct seq_file *m)
{
	struct public_key *key = asymmetric_key->payload.data[asym_crypto];

	if (key)
		seq_printf(m, "%s.%s",
			   pkey_id_type_name[key->id_type],
			   pkey_algo_name[key->pkey_algo]);
}

/*
 * Destroy a public key algorithm key.
 */
void public_key_destroy(void *payload)
{
	struct public_key *key = payload;

	if (key)
		kfree(key->key);
	kfree(key);
}
EXPORT_SYMBOL_GPL(public_key_destroy);

/*
 * Verify a signature using a public key.
 */
int public_key_verify_signature(const struct public_key *pkey,
				const struct public_key_signature *sig)
{
	BUG_ON(!pkey);
	BUG_ON(!sig);
	BUG_ON(!sig->digest);
	BUG_ON(!sig->s);

	if (pkey->pkey_algo >= PKEY_ALGO__LAST)
		return -ENOPKG;

	if (!alg_verify[pkey->pkey_algo])
		return -ENOPKG;

	return alg_verify[pkey->pkey_algo](pkey, sig);
}
EXPORT_SYMBOL_GPL(public_key_verify_signature);

static int public_key_verify_signature_2(const struct key *key,
					 const struct public_key_signature *sig)
{
	const struct public_key *pk = key->payload.data[asym_crypto];
	return public_key_verify_signature(pk, sig);
}

/*
 * Public key algorithm asymmetric key subtype
 */
struct asymmetric_key_subtype public_key_subtype = {
	.owner			= THIS_MODULE,
	.name			= "public_key",
	.name_len		= sizeof("public_key") - 1,
	.describe		= public_key_describe,
	.destroy		= public_key_destroy,
	.verify_signature	= public_key_verify_signature_2,
};
EXPORT_SYMBOL_GPL(public_key_subtype);
