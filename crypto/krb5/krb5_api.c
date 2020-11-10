// SPDX-License-Identifier: GPL-2.0-or-later
/* Kerberos 5 crypto library.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include "internal.h"

MODULE_DESCRIPTION("Kerberos 5 crypto");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

static const struct krb5_enctype *const krb5_supported_enctypes[] = {
};

/**
 * crypto_krb5_find_enctype - Find the handler for a Kerberos5 encryption type
 * @enctype: The standard Kerberos encryption type number
 *
 * Look up a Kerberos encryption type by number.  If successful, returns a
 * pointer to the type tables; returns NULL otherwise.
 */
const struct krb5_enctype *crypto_krb5_find_enctype(u32 enctype)
{
	const struct krb5_enctype *krb5;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(krb5_supported_enctypes); i++) {
		krb5 = krb5_supported_enctypes[i];
		if (krb5->etype == enctype)
			return krb5;
	}

	return NULL;
}
EXPORT_SYMBOL(crypto_krb5_find_enctype);
