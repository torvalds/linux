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
#include <keys/asymmetric-parser.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include "asymmetric_keys.h"

MODULE_LICENSE("GPL");

static LIST_HEAD(asymmetric_key_parsers);
static DECLARE_RWSEM(asymmetric_key_parsers_sem);

/**
 * asymmetric_key_generate_id: Construct an asymmetric key ID
 * @val_1: First binary blob
 * @len_1: Length of first binary blob
 * @val_2: Second binary blob
 * @len_2: Length of second binary blob
 *
 * Construct an asymmetric key ID from a pair of binary blobs.
 */
struct asymmetric_key_id *asymmetric_key_generate_id(const void *val_1,
						     size_t len_1,
						     const void *val_2,
						     size_t len_2)
{
	struct asymmetric_key_id *kid;

	kid = kmalloc(sizeof(struct asymmetric_key_id) + len_1 + len_2,
		      GFP_KERNEL);
	if (!kid)
		return ERR_PTR(-ENOMEM);
	kid->len = len_1 + len_2;
	memcpy(kid->data, val_1, len_1);
	memcpy(kid->data + len_1, val_2, len_2);
	return kid;
}
EXPORT_SYMBOL_GPL(asymmetric_key_generate_id);

/**
 * asymmetric_key_id_same - Return true if two asymmetric keys IDs are the same.
 * @kid_1, @kid_2: The key IDs to compare
 */
bool asymmetric_key_id_same(const struct asymmetric_key_id *kid1,
			    const struct asymmetric_key_id *kid2)
{
	if (!kid1 || !kid2)
		return false;
	if (kid1->len != kid2->len)
		return false;
	return memcmp(kid1->data, kid2->data, kid1->len) == 0;
}
EXPORT_SYMBOL_GPL(asymmetric_key_id_same);

/**
 * asymmetric_key_id_partial - Return true if two asymmetric keys IDs
 * partially match
 * @kid_1, @kid_2: The key IDs to compare
 */
bool asymmetric_key_id_partial(const struct asymmetric_key_id *kid1,
			       const struct asymmetric_key_id *kid2)
{
	if (!kid1 || !kid2)
		return false;
	if (kid1->len < kid2->len)
		return false;
	return memcmp(kid1->data + (kid1->len - kid2->len),
		      kid2->data, kid2->len) == 0;
}
EXPORT_SYMBOL_GPL(asymmetric_key_id_partial);

/**
 * asymmetric_match_key_ids - Search asymmetric key IDs
 * @kids: The list of key IDs to check
 * @match_id: The key ID we're looking for
 * @match: The match function to use
 */
static bool asymmetric_match_key_ids(
	const struct asymmetric_key_ids *kids,
	const struct asymmetric_key_id *match_id,
	bool (*match)(const struct asymmetric_key_id *kid1,
		      const struct asymmetric_key_id *kid2))
{
	int i;

	if (!kids || !match_id)
		return false;
	for (i = 0; i < ARRAY_SIZE(kids->id); i++)
		if (match(kids->id[i], match_id))
			return true;
	return false;
}

/**
 * asymmetric_key_hex_to_key_id - Convert a hex string into a key ID.
 * @id: The ID as a hex string.
 */
struct asymmetric_key_id *asymmetric_key_hex_to_key_id(const char *id)
{
	struct asymmetric_key_id *match_id;
	size_t hexlen;
	int ret;

	if (!*id)
		return ERR_PTR(-EINVAL);
	hexlen = strlen(id);
	if (hexlen & 1)
		return ERR_PTR(-EINVAL);

	match_id = kmalloc(sizeof(struct asymmetric_key_id) + hexlen / 2,
			   GFP_KERNEL);
	if (!match_id)
		return ERR_PTR(-ENOMEM);
	match_id->len = hexlen / 2;
	ret = hex2bin(match_id->data, id, hexlen / 2);
	if (ret < 0) {
		kfree(match_id);
		return ERR_PTR(-EINVAL);
	}
	return match_id;
}

/*
 * Match asymmetric keys by an exact match on an ID.
 */
static bool asymmetric_key_cmp(const struct key *key,
			       const struct key_match_data *match_data)
{
	const struct asymmetric_key_ids *kids = asymmetric_key_ids(key);
	const struct asymmetric_key_id *match_id = match_data->preparsed;

	return asymmetric_match_key_ids(kids, match_id,
					asymmetric_key_id_same);
}

/*
 * Match asymmetric keys by a partial match on an IDs.
 */
static bool asymmetric_key_cmp_partial(const struct key *key,
				       const struct key_match_data *match_data)
{
	const struct asymmetric_key_ids *kids = asymmetric_key_ids(key);
	const struct asymmetric_key_id *match_id = match_data->preparsed;

	return asymmetric_match_key_ids(kids, match_id,
					asymmetric_key_id_partial);
}

/*
 * Preparse the match criterion.  If we don't set lookup_type and cmp,
 * the default will be an exact match on the key description.
 *
 * There are some specifiers for matching key IDs rather than by the key
 * description:
 *
 *	"id:<id>" - find a key by partial match on any available ID
 *	"ex:<id>" - find a key by exact match on any available ID
 *
 * These have to be searched by iteration rather than by direct lookup because
 * the key is hashed according to its description.
 */
static int asymmetric_key_match_preparse(struct key_match_data *match_data)
{
	struct asymmetric_key_id *match_id;
	const char *spec = match_data->raw_data;
	const char *id;
	bool (*cmp)(const struct key *, const struct key_match_data *) =
		asymmetric_key_cmp;

	if (!spec || !*spec)
		return -EINVAL;
	if (spec[0] == 'i' &&
	    spec[1] == 'd' &&
	    spec[2] == ':') {
		id = spec + 3;
		cmp = asymmetric_key_cmp_partial;
	} else if (spec[0] == 'e' &&
		   spec[1] == 'x' &&
		   spec[2] == ':') {
		id = spec + 3;
	} else {
		goto default_match;
	}

	match_id = asymmetric_key_hex_to_key_id(id);
	if (IS_ERR(match_id))
		return PTR_ERR(match_id);

	match_data->preparsed = match_id;
	match_data->cmp = cmp;
	match_data->lookup_type = KEYRING_SEARCH_LOOKUP_ITERATE;
	return 0;

default_match:
	return 0;
}

/*
 * Free the preparsed the match criterion.
 */
static void asymmetric_key_match_free(struct key_match_data *match_data)
{
	kfree(match_data->preparsed);
}

/*
 * Describe the asymmetric key
 */
static void asymmetric_key_describe(const struct key *key, struct seq_file *m)
{
	const struct asymmetric_key_subtype *subtype = asymmetric_key_subtype(key);
	const struct asymmetric_key_ids *kids = asymmetric_key_ids(key);
	const struct asymmetric_key_id *kid;
	const unsigned char *p;
	int n;

	seq_puts(m, key->description);

	if (subtype) {
		seq_puts(m, ": ");
		subtype->describe(key, m);

		if (kids && kids->id[1]) {
			kid = kids->id[1];
			seq_putc(m, ' ');
			n = kid->len;
			p = kid->data;
			if (n > 4) {
				p += n - 4;
				n = 4;
			}
			seq_printf(m, "%*phN", n, p);
		}

		seq_puts(m, " [");
		/* put something here to indicate the key's capabilities */
		seq_putc(m, ']');
	}
}

/*
 * Preparse a asymmetric payload to get format the contents appropriately for the
 * internal payload to cut down on the number of scans of the data performed.
 *
 * We also generate a proposed description from the contents of the key that
 * can be used to name the key if the user doesn't want to provide one.
 */
static int asymmetric_key_preparse(struct key_preparsed_payload *prep)
{
	struct asymmetric_key_parser *parser;
	int ret;

	pr_devel("==>%s()\n", __func__);

	if (prep->datalen == 0)
		return -EINVAL;

	down_read(&asymmetric_key_parsers_sem);

	ret = -EBADMSG;
	list_for_each_entry(parser, &asymmetric_key_parsers, link) {
		pr_debug("Trying parser '%s'\n", parser->name);

		ret = parser->parse(prep);
		if (ret != -EBADMSG) {
			pr_debug("Parser recognised the format (ret %d)\n",
				 ret);
			break;
		}
	}

	up_read(&asymmetric_key_parsers_sem);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Clean up the preparse data
 */
static void asymmetric_key_free_preparse(struct key_preparsed_payload *prep)
{
	struct asymmetric_key_subtype *subtype = prep->type_data[0];
	struct asymmetric_key_ids *kids = prep->type_data[1];
	int i;

	pr_devel("==>%s()\n", __func__);

	if (subtype) {
		subtype->destroy(prep->payload[0]);
		module_put(subtype->owner);
	}
	if (kids) {
		for (i = 0; i < ARRAY_SIZE(kids->id); i++)
			kfree(kids->id[i]);
		kfree(kids);
	}
	kfree(prep->description);
}

/*
 * dispose of the data dangling from the corpse of a asymmetric key
 */
static void asymmetric_key_destroy(struct key *key)
{
	struct asymmetric_key_subtype *subtype = asymmetric_key_subtype(key);
	struct asymmetric_key_ids *kids = key->type_data.p[1];

	if (subtype) {
		subtype->destroy(key->payload.data);
		module_put(subtype->owner);
		key->type_data.p[0] = NULL;
	}

	if (kids) {
		kfree(kids->id[0]);
		kfree(kids->id[1]);
		kfree(kids);
		key->type_data.p[1] = NULL;
	}
}

struct key_type key_type_asymmetric = {
	.name		= "asymmetric",
	.preparse	= asymmetric_key_preparse,
	.free_preparse	= asymmetric_key_free_preparse,
	.instantiate	= generic_key_instantiate,
	.match_preparse	= asymmetric_key_match_preparse,
	.match_free	= asymmetric_key_match_free,
	.destroy	= asymmetric_key_destroy,
	.describe	= asymmetric_key_describe,
};
EXPORT_SYMBOL_GPL(key_type_asymmetric);

/**
 * register_asymmetric_key_parser - Register a asymmetric key blob parser
 * @parser: The parser to register
 */
int register_asymmetric_key_parser(struct asymmetric_key_parser *parser)
{
	struct asymmetric_key_parser *cursor;
	int ret;

	down_write(&asymmetric_key_parsers_sem);

	list_for_each_entry(cursor, &asymmetric_key_parsers, link) {
		if (strcmp(cursor->name, parser->name) == 0) {
			pr_err("Asymmetric key parser '%s' already registered\n",
			       parser->name);
			ret = -EEXIST;
			goto out;
		}
	}

	list_add_tail(&parser->link, &asymmetric_key_parsers);

	pr_notice("Asymmetric key parser '%s' registered\n", parser->name);
	ret = 0;

out:
	up_write(&asymmetric_key_parsers_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(register_asymmetric_key_parser);

/**
 * unregister_asymmetric_key_parser - Unregister a asymmetric key blob parser
 * @parser: The parser to unregister
 */
void unregister_asymmetric_key_parser(struct asymmetric_key_parser *parser)
{
	down_write(&asymmetric_key_parsers_sem);
	list_del(&parser->link);
	up_write(&asymmetric_key_parsers_sem);

	pr_notice("Asymmetric key parser '%s' unregistered\n", parser->name);
}
EXPORT_SYMBOL_GPL(unregister_asymmetric_key_parser);

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
