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
#include "public_key.h"

MODULE_LICENSE("GPL");

const char *const pkey_algo_name[PKEY_ALGO__LAST] = {
	[PKEY_ALGO_DSA]		= "DSA",
	[PKEY_ALGO_RSA]		= "RSA",
};
EXPORT_SYMBOL_GPL(pkey_algo_name);

const struct public_key_algorithm *pkey_algo[PKEY_ALGO__LAST] = {
#if defined(CONFIG_PUBLIC_KEY_ALGO_RSA) || \
	defined(CONFIG_PUBLIC_KEY_ALGO_RSA_MODULE)
	[PKEY_ALGO_RSA]		= &RSA_public_key_algorithm,
#endif
};
EXPORT_SYMBOL_GPL(pkey_algo);

const char *const pkey_id_type_name[PKEY_ID_TYPE__LAST] = {
	[PKEY_ID_PGP]		= "PGP",
	[PKEY_ID_X509]		= "X509",
	[PKEY_ID_PKCS7]		= "PKCS#7",
};
EXPORT_SYMBOL_GPL(pkey_id_type_name);

/*
 * Provide a part of a description of the key for /proc/keys.
 */
static void public_key_describe(const struct key *asymmetric_key,
				struct seq_file *m)
{
	struct public_key *key = asymmetric_key->payload.data;

	if (key)
		seq_printf(m, "%s.%s",
			   pkey_id_type_name[key->id_type], key->algo->name);
}

/*
 * Destroy a public key algorithm key.
 */
void public_key_destroy(void *payload)
{
	struct public_key *key = payload;
	int i;

	if (key) {
		for (i = 0; i < ARRAY_SIZE(key->mpi); i++)
			mpi_free(key->mpi[i]);
		kfree(key);
	}
}
EXPORT_SYMBOL_GPL(public_key_destroy);

/*
 * Verify a signature using a public key.
 */
int public_key_verify_signature(const struct public_key *pk,
				const struct public_key_signature *sig)
{
	const struct public_key_algorithm *algo;

	BUG_ON(!pk);
	BUG_ON(!pk->mpi[0]);
	BUG_ON(!pk->mpi[1]);
	BUG_ON(!sig);
	BUG_ON(!sig->digest);
	BUG_ON(!sig->mpi[0]);

	algo = pk->algo;
	if (!algo) {
		if (pk->pkey_algo >= PKEY_ALGO__LAST)
			return -ENOPKG;
		algo = pkey_algo[pk->pkey_algo];
		if (!algo)
			return -ENOPKG;
	}

	if (!algo->verify_signature)
		return -ENOTSUPP;

	if (sig->nr_mpi != algo->n_sig_mpi) {
		pr_debug("Signature has %u MPI not %u\n",
			 sig->nr_mpi, algo->n_sig_mpi);
		return -EINVAL;
	}

	return algo->verify_signature(pk, sig);
}
EXPORT_SYMBOL_GPL(public_key_verify_signature);

static int public_key_verify_signature_2(const struct key *key,
					 const struct public_key_signature *sig)
{
	const struct public_key *pk = key->payload.data;
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
