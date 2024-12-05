// SPDX-License-Identifier: GPL-2.0-or-later
/* Signature verification with an asymmetric key
 *
 * See Documentation/crypto/asymmetric-keys.rst
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "SIG: "fmt
#include <keys/asymmetric-subtype.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/keyctl.h>
#include <crypto/public_key.h>
#include <keys/user-type.h>
#include "asymmetric_keys.h"

/*
 * Destroy a public key signature.
 */
void public_key_signature_free(struct public_key_signature *sig)
{
	int i;

	if (sig) {
		for (i = 0; i < ARRAY_SIZE(sig->auth_ids); i++)
			kfree(sig->auth_ids[i]);
		kfree(sig->s);
		kfree(sig->digest);
		kfree(sig);
	}
}
EXPORT_SYMBOL_GPL(public_key_signature_free);

/**
 * query_asymmetric_key - Get information about an asymmetric key.
 * @params: Various parameters.
 * @info: Where to put the information.
 */
int query_asymmetric_key(const struct kernel_pkey_params *params,
			 struct kernel_pkey_query *info)
{
	const struct asymmetric_key_subtype *subtype;
	struct key *key = params->key;
	int ret;

	pr_devel("==>%s()\n", __func__);

	if (key->type != &key_type_asymmetric)
		return -EINVAL;
	subtype = asymmetric_key_subtype(key);
	if (!subtype ||
	    !key->payload.data[0])
		return -EINVAL;
	if (!subtype->query)
		return -ENOTSUPP;

	ret = subtype->query(params, info);

	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(query_asymmetric_key);

/**
 * verify_signature - Initiate the use of an asymmetric key to verify a signature
 * @key: The asymmetric key to verify against
 * @sig: The signature to check
 *
 * Returns 0 if successful or else an error.
 */
int verify_signature(const struct key *key,
		     const struct public_key_signature *sig)
{
	const struct asymmetric_key_subtype *subtype;
	int ret;

	pr_devel("==>%s()\n", __func__);

	if (key->type != &key_type_asymmetric)
		return -EINVAL;
	subtype = asymmetric_key_subtype(key);
	if (!subtype ||
	    !key->payload.data[0])
		return -EINVAL;
	if (!subtype->verify_signature)
		return -ENOTSUPP;

	ret = subtype->verify_signature(key, sig);

	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(verify_signature);
