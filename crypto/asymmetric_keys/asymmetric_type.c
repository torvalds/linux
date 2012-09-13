/* Asymmetric public-key cryptography key type
 *
 * See Documentation/security/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <keys/asymmetric-subtype.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "asymmetric_keys.h"

MODULE_LICENSE("GPL");

/*
 * Match asymmetric keys on (part of) their name
 * We have some shorthand methods for matching keys.  We allow:
 *
 *	"<desc>"	- request a key by description
 *	"id:<id>"	- request a key matching the ID
 *	"<subtype>:<id>" - request a key of a subtype
 */
static int asymmetric_key_match(const struct key *key, const void *description)
{
	const struct asymmetric_key_subtype *subtype = asymmetric_key_subtype(key);
	const char *spec = description;
	const char *id, *kid;
	ptrdiff_t speclen;
	size_t idlen, kidlen;

	if (!subtype || !spec || !*spec)
		return 0;

	/* See if the full key description matches as is */
	if (key->description && strcmp(key->description, description) == 0)
		return 1;

	/* All tests from here on break the criterion description into a
	 * specifier, a colon and then an identifier.
	 */
	id = strchr(spec, ':');
	if (!id)
		return 0;

	speclen = id - spec;
	id++;

	/* Anything after here requires a partial match on the ID string */
	kid = asymmetric_key_id(key);
	if (!kid)
		return 0;

	idlen = strlen(id);
	kidlen = strlen(kid);
	if (idlen > kidlen)
		return 0;

	kid += kidlen - idlen;
	if (strcasecmp(id, kid) != 0)
		return 0;

	if (speclen == 2 &&
	    memcmp(spec, "id", 2) == 0)
		return 1;

	if (speclen == subtype->name_len &&
	    memcmp(spec, subtype->name, speclen) == 0)
		return 1;

	return 0;
}

/*
 * Describe the asymmetric key
 */
static void asymmetric_key_describe(const struct key *key, struct seq_file *m)
{
	const struct asymmetric_key_subtype *subtype = asymmetric_key_subtype(key);
	const char *kid = asymmetric_key_id(key);
	size_t n;

	seq_puts(m, key->description);

	if (subtype) {
		seq_puts(m, ": ");
		subtype->describe(key, m);

		if (kid) {
			seq_putc(m, ' ');
			n = strlen(kid);
			if (n <= 8)
				seq_puts(m, kid);
			else
				seq_puts(m, kid + n - 8);
		}

		seq_puts(m, " [");
		/* put something here to indicate the key's capabilities */
		seq_putc(m, ']');
	}
}

/*
 * Instantiate a asymmetric_key defined key.  The key was preparsed, so we just
 * have to transfer the data here.
 */
static int asymmetric_key_instantiate(struct key *key, struct key_preparsed_payload *prep)
{
	return -EOPNOTSUPP;
}

/*
 * dispose of the data dangling from the corpse of a asymmetric key
 */
static void asymmetric_key_destroy(struct key *key)
{
	struct asymmetric_key_subtype *subtype = asymmetric_key_subtype(key);
	if (subtype) {
		subtype->destroy(key->payload.data);
		module_put(subtype->owner);
		key->type_data.p[0] = NULL;
	}
	kfree(key->type_data.p[1]);
	key->type_data.p[1] = NULL;
}

struct key_type key_type_asymmetric = {
	.name		= "asymmetric",
	.instantiate	= asymmetric_key_instantiate,
	.match		= asymmetric_key_match,
	.destroy	= asymmetric_key_destroy,
	.describe	= asymmetric_key_describe,
};
EXPORT_SYMBOL_GPL(key_type_asymmetric);

/*
 * Module stuff
 */
static int __init asymmetric_key_init(void)
{
	return register_key_type(&key_type_asymmetric);
}

static void __exit asymmetric_key_cleanup(void)
{
	unregister_key_type(&key_type_asymmetric);
}

module_init(asymmetric_key_init);
module_exit(asymmetric_key_cleanup);
