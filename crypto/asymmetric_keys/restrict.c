/* Instantiate a public key crypto key from an X.509 Certificate
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/mpi.h>
#include <linux/asn1_decoder.h>
#include <keys/asymmetric-subtype.h>
#include <keys/asymmetric-parser.h>
#include <keys/system_keyring.h>
#include <crypto/hash.h>
#include <crypto/public_key.h>
#include "asymmetric_keys.h"
#include "x509_parser.h"

static bool use_builtin_keys;
static struct asymmetric_key_id *ca_keyid;

#ifndef MODULE
static struct {
	struct asymmetric_key_id id;
	unsigned char data[10];
} cakey;

static int __init ca_keys_setup(char *str)
{
	if (!str)		/* default system keyring */
		return 1;

	if (strncmp(str, "id:", 3) == 0) {
		struct asymmetric_key_id *p = &cakey.id;
		size_t hexlen = (strlen(str) - 3) / 2;
		int ret;

		if (hexlen == 0 || hexlen > sizeof(cakey.data)) {
			pr_err("Missing or invalid ca_keys id\n");
			return 1;
		}

		ret = __asymmetric_key_hex_to_key_id(str + 3, p, hexlen);
		if (ret < 0)
			pr_err("Unparsable ca_keys id hex string\n");
		else
			ca_keyid = p;	/* owner key 'id:xxxxxx' */
	} else if (strcmp(str, "builtin") == 0) {
		use_builtin_keys = true;
	}

	return 1;
}
__setup("ca_keys=", ca_keys_setup);
#endif

/*
 * Check the new certificate against the ones in the trust keyring.  If one of
 * those is the signing key and validates the new certificate, then mark the
 * new certificate as being trusted.
 *
 * Return 0 if the new certificate was successfully validated, 1 if we couldn't
 * find a matching parent certificate in the trusted list and an error if there
 * is a matching certificate but the signature check fails.
 */
int x509_validate_trust(struct x509_certificate *cert,
			struct key *trust_keyring)
{
	struct public_key_signature *sig = cert->sig;
	struct key *key;
	int ret = 1;

	if (!sig->auth_ids[0] && !sig->auth_ids[1])
		return 1;

	if (!trust_keyring)
		return -EOPNOTSUPP;
	if (ca_keyid && !asymmetric_key_id_partial(sig->auth_ids[1], ca_keyid))
		return -EPERM;
	if (cert->unsupported_sig)
		return -ENOPKG;

	key = find_asymmetric_key(trust_keyring,
				  sig->auth_ids[0], sig->auth_ids[1],
				  false);
	if (IS_ERR(key))
		return PTR_ERR(key);

	if (!use_builtin_keys ||
	    test_bit(KEY_FLAG_BUILTIN, &key->flags)) {
		ret = verify_signature(key, cert->sig);
		if (ret == -ENOPKG)
			cert->unsupported_sig = true;
	}
	key_put(key);
	return ret;
}
EXPORT_SYMBOL_GPL(x509_validate_trust);
