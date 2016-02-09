/* System trusted keyring for trusted public keys
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <keys/asymmetric-type.h>
#include <keys/system_keyring.h>
#include <crypto/pkcs7.h>

struct key *system_trusted_keyring;
EXPORT_SYMBOL_GPL(system_trusted_keyring);

extern __initconst const u8 system_certificate_list[];
extern __initconst const unsigned long system_certificate_list_size;

/*
 * Load the compiled-in keys
 */
static __init int system_trusted_keyring_init(void)
{
	pr_notice("Initialise system trusted keyring\n");

	system_trusted_keyring =
		keyring_alloc(".system_keyring",
			      KUIDT_INIT(0), KGIDT_INIT(0), current_cred(),
			      ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
			      KEY_USR_VIEW | KEY_USR_READ | KEY_USR_SEARCH),
			      KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (IS_ERR(system_trusted_keyring))
		panic("Can't allocate system trusted keyring\n");

	set_bit(KEY_FLAG_TRUSTED_ONLY, &system_trusted_keyring->flags);
	return 0;
}

/*
 * Must be initialised before we try and load the keys into the keyring.
 */
device_initcall(system_trusted_keyring_init);

/*
 * Load the compiled-in list of X.509 certificates.
 */
static __init int load_system_certificate_list(void)
{
	key_ref_t key;
	const u8 *p, *end;
	size_t plen;

	pr_notice("Loading compiled-in X.509 certificates\n");

	p = system_certificate_list;
	end = p + system_certificate_list_size;
	while (p < end) {
		/* Each cert begins with an ASN.1 SEQUENCE tag and must be more
		 * than 256 bytes in size.
		 */
		if (end - p < 4)
			goto dodgy_cert;
		if (p[0] != 0x30 &&
		    p[1] != 0x82)
			goto dodgy_cert;
		plen = (p[2] << 8) | p[3];
		plen += 4;
		if (plen > end - p)
			goto dodgy_cert;

		key = key_create_or_update(make_key_ref(system_trusted_keyring, 1),
					   "asymmetric",
					   NULL,
					   p,
					   plen,
					   ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
					   KEY_USR_VIEW | KEY_USR_READ),
					   KEY_ALLOC_NOT_IN_QUOTA |
					   KEY_ALLOC_TRUSTED |
					   KEY_ALLOC_BUILT_IN);
		if (IS_ERR(key)) {
			pr_err("Problem loading in-kernel X.509 certificate (%ld)\n",
			       PTR_ERR(key));
		} else {
			pr_notice("Loaded X.509 cert '%s'\n",
				  key_ref_to_ptr(key)->description);
			key_ref_put(key);
		}
		p += plen;
	}

	return 0;

dodgy_cert:
	pr_err("Problem parsing in-kernel X.509 certificate list\n");
	return 0;
}
late_initcall(load_system_certificate_list);

#ifdef CONFIG_SYSTEM_DATA_VERIFICATION

/**
 * Verify a PKCS#7-based signature on system data.
 * @data: The data to be verified.
 * @len: Size of @data.
 * @raw_pkcs7: The PKCS#7 message that is the signature.
 * @pkcs7_len: The size of @raw_pkcs7.
 * @usage: The use to which the key is being put.
 */
int system_verify_data(const void *data, unsigned long len,
		       const void *raw_pkcs7, size_t pkcs7_len,
		       enum key_being_used_for usage)
{
	struct pkcs7_message *pkcs7;
	bool trusted;
	int ret;

	pkcs7 = pkcs7_parse_message(raw_pkcs7, pkcs7_len);
	if (IS_ERR(pkcs7))
		return PTR_ERR(pkcs7);

	/* The data should be detached - so we need to supply it. */
	if (pkcs7_supply_detached_data(pkcs7, data, len) < 0) {
		pr_err("PKCS#7 signature with non-detached data\n");
		ret = -EBADMSG;
		goto error;
	}

	ret = pkcs7_verify(pkcs7, usage);
	if (ret < 0)
		goto error;

	ret = pkcs7_validate_trust(pkcs7, system_trusted_keyring, &trusted);
	if (ret < 0)
		goto error;

	if (!trusted) {
		pr_err("PKCS#7 signature not signed with a trusted key\n");
		ret = -ENOKEY;
	}

error:
	pkcs7_free_message(pkcs7);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(system_verify_data);

#endif /* CONFIG_SYSTEM_DATA_VERIFICATION */
