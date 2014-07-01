/* Testing module to load key from trusted PKCS#7 message
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PKCS7key: "fmt
#include <linux/key.h>
#include <linux/key-type.h>
#include <crypto/pkcs7.h>
#include <keys/user-type.h>
#include <keys/system_keyring.h>
#include "pkcs7_parser.h"

/*
 * Instantiate a PKCS#7 wrapped and validated key.
 */
int pkcs7_instantiate(struct key *key, struct key_preparsed_payload *prep)
{
	struct pkcs7_message *pkcs7;
	const void *data, *saved_prep_data;
	size_t datalen, saved_prep_datalen;
	bool trusted;
	int ret;

	kenter("");

	saved_prep_data = prep->data;
	saved_prep_datalen = prep->datalen;
	pkcs7 = pkcs7_parse_message(saved_prep_data, saved_prep_datalen);
	if (IS_ERR(pkcs7)) {
		ret = PTR_ERR(pkcs7);
		goto error;
	}

	ret = pkcs7_verify(pkcs7);
	if (ret < 0)
		goto error_free;

	ret = pkcs7_validate_trust(pkcs7, system_trusted_keyring, &trusted);
	if (ret < 0)
		goto error_free;
	if (!trusted)
		pr_warn("PKCS#7 message doesn't chain back to a trusted key\n");

	ret = pkcs7_get_content_data(pkcs7, &data, &datalen, false);
	if (ret < 0)
		goto error_free;

	prep->data = data;
	prep->datalen = datalen;
	ret = user_instantiate(key, prep);
	prep->data = saved_prep_data;
	prep->datalen = saved_prep_datalen;

error_free:
	pkcs7_free_message(pkcs7);
error:
	kleave(" = %d", ret);
	return ret;
}

/*
 * user defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
struct key_type key_type_pkcs7 = {
	.name			= "pkcs7_test",
	.def_lookup_type	= KEYRING_SEARCH_LOOKUP_DIRECT,
	.instantiate		= pkcs7_instantiate,
	.match			= user_match,
	.revoke			= user_revoke,
	.destroy		= user_destroy,
	.describe		= user_describe,
	.read			= user_read,
};

/*
 * Module stuff
 */
static int __init pkcs7_key_init(void)
{
	return register_key_type(&key_type_pkcs7);
}

static void __exit pkcs7_key_cleanup(void)
{
	unregister_key_type(&key_type_pkcs7);
}

module_init(pkcs7_key_init);
module_exit(pkcs7_key_cleanup);
