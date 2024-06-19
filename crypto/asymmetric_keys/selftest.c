// SPDX-License-Identifier: GPL-2.0-or-later
/* Self-testing for signature checking.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <crypto/pkcs7.h>
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/key.h>
#include <linux/module.h>
#include "selftest.h"
#include "x509_parser.h"

void fips_signature_selftest(const char *name,
			     const u8 *keys, size_t keys_len,
			     const u8 *data, size_t data_len,
			     const u8 *sig, size_t sig_len)
{
	struct key *keyring;
	int ret;

	pr_notice("Running certificate verification %s selftest\n", name);

	keyring = keyring_alloc(".certs_selftest",
				GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, current_cred(),
				(KEY_POS_ALL & ~KEY_POS_SETATTR) |
				KEY_USR_VIEW | KEY_USR_READ |
				KEY_USR_SEARCH,
				KEY_ALLOC_NOT_IN_QUOTA,
				NULL, NULL);
	if (IS_ERR(keyring))
		panic("Can't allocate certs %s selftest keyring: %ld\n", name, PTR_ERR(keyring));

	ret = x509_load_certificate_list(keys, keys_len, keyring);
	if (ret < 0)
		panic("Can't allocate certs %s selftest keyring: %d\n", name, ret);

	struct pkcs7_message *pkcs7;

	pkcs7 = pkcs7_parse_message(sig, sig_len);
	if (IS_ERR(pkcs7))
		panic("Certs %s selftest: pkcs7_parse_message() = %d\n", name, ret);

	pkcs7_supply_detached_data(pkcs7, data, data_len);

	ret = pkcs7_verify(pkcs7, VERIFYING_MODULE_SIGNATURE);
	if (ret < 0)
		panic("Certs %s selftest: pkcs7_verify() = %d\n", name, ret);

	ret = pkcs7_validate_trust(pkcs7, keyring);
	if (ret < 0)
		panic("Certs %s selftest: pkcs7_validate_trust() = %d\n", name, ret);

	pkcs7_free_message(pkcs7);

	key_put(keyring);
}

static int __init fips_signature_selftest_init(void)
{
	fips_signature_selftest_rsa();
	fips_signature_selftest_ecdsa();
	return 0;
}

late_initcall(fips_signature_selftest_init);

MODULE_DESCRIPTION("X.509 self tests");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");
