// SPDX-License-Identifier: GPL-2.0-or-later
/* Asymmetric public-key cryptography key type
 *
 * See Documentation/crypto/asymmetric-keys.rst
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <keys/asymmetric-subtype.h>
#include <keys/asymmetric-parser.h>
#include <crypto/public_key.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <keys/system_keyring.h>
#include <keys/user-type.h>
#include "asymmetric_keys.h"

MODULE_LICENSE("GPL");

const char *const key_being_used_for[NR__KEY_BEING_USED_FOR] = {
	[VERIFYING_MODULE_SIGNATURE]		= "mod sig",
	[VERIFYING_FIRMWARE_SIGNATURE]		= "firmware sig",
	[VERIFYING_KEXEC_PE_SIGNATURE]		= "kexec PE sig",
	[VERIFYING_KEY_SIGNATURE]		= "key sig",
	[VERIFYING_KEY_SELF_SIGNATURE]		= "key self sig",
	[VERIFYING_UNSPECIFIED_SIGNATURE]	= "unspec sig",
};
EXPORT_SYMBOL_GPL(key_being_used_for);

static LIST_HEAD(asymmetric_key_parsers);
static DECLARE_RWSEM(asymmetric_key_parsers_sem);

/**
 * find_asymmetric_key - Find a key by ID.
 * @keyring: The keys to search.
 * @id_0: The first ID to look for or NULL.
 * @id_1: The second ID to look for or NULL, matched together with @id_0
 * against @keyring keys' id[0] and id[1].
 * @id_2: The fallback ID to match against @keyring keys' id[2] if both of the
 * other IDs are NULL.
 * @partial: Use partial match for @id_0 and @id_1 if true, exact if false.
 *
 * Find a key in the given keyring by identifier.  The preferred identifier is
 * the id_0 and the fallback identifier is the id_1.  If both are given, the
 * former is matched (exactly or partially) against either of the sought key's
 * identifiers and the latter must match the found key's second identifier
 * exactly.  If both are missing, id_2 must match the sought key's third
 * identifier exactly.
 */
struct key *find_asymmetric_key(struct key *keyring,
				const struct asymmetric_key_id *id_0,
				const struct asymmetric_key_id *id_1,
				const struct asymmetric_key_id *id_2,
				bool partial)
{
	struct key *key;
	key_ref_t ref;
	const char *lookup;
	char *req, *p;
	int len;

	WARN_ON(!id_0 && !id_1 && !id_2);

	if (id_0) {
		lookup = id_0->data;
		len = id_0->len;
	} else if (id_1) {
		lookup = id_1->data;
		len = id_1->len;
	} else {
		lookup = id_2->data;
		len = id_2->len;
	}

	/* Construct an identifier "id:<keyid>". */
	p = req = kmalloc(2 + 1 + len * 2 + 1, GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	if (!id_0 && !id_1) {
		*p++ = 'd';
		*p++ = 'n';
	} else if (partial) {
		*p++ = 'i';
		*p++ = 'd';
	} else {
		*p++ = 'e';
		*p++ = 'x';
	}
	*p++ = ':';
	p = bin2hex(p, lookup, len);
	*p = 0;

	pr_debug("Look up: \"%s\"\n", req);

	ref = keyring_search(make_key_ref(keyring, 1),
			     &key_type_asymmetric, req, true);
	if (IS_ERR(ref))
		pr_debug("Request for key '%s' err %ld\n", req, PTR_ERR(ref));
	kfree(req);

	if (IS_ERR(ref)) {
		switch (PTR_ERR(ref)) {
			/* Hide some search errors */
		case -EACCES:
		case -ENOTDIR:
		case -EAGAIN:
			return ERR_PTR(-ENOKEY);
		default:
			return ERR_CAST(ref);
		}
	}

	key = key_ref_to_ptr(ref);
	if (id_0 && id_1) {
		const struct asymmetric_key_ids *kids = asymmetric_key_ids(key);

		if (!kids->id[1]) {
			pr_debug("First ID matches, but second is missing\n");
			goto reject;
		}
		if (!asymmetric_key_id_same(id_1, kids->id[1])) {
			pr_debug("First ID matches, but second does not\n");
			goto reject;
		}
	}

	pr_devel("<==%s() = 0 [%x]\n", __func__, key_serial(key));
	return key;

reject:
	key_put(key);
	return ERR_PTR(-EKEYREJECTED);
}
EXPORT_SYMBOL_GPL(find_asymmetric_key);

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
 * @kid1: The key ID to compare
 * @kid2: The key ID to compare
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
 * @kid1: The key ID to compare
 * @kid2: The key ID to compare
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
 * asymmetric_match_key_ids - Search asymmetric key IDs 1 & 2
 * @kids: The pair of key IDs to check
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
	for (i = 0; i < 2; i++)
		if (match(kids->id[i], match_id))
			return true;
	return false;
}

/* helper function can be called directly with pre-allocated memory */
inline int __asymmetric_key_hex_to_key_id(const char *id,
				   struct asymmetric_key_id *match_id,
				   size_t hexlen)
{
	match_id->len = hexlen;
	return hex2bin(match_id->data, id, hexlen);
}

/**
 * asymmetric_key_hex_to_key_id - Convert a hex string into a key ID.
 * @id: The ID as a hex string.
 */
struct asymmetric_key_id *asymmetric_key_hex_to_key_id(const char *id)
{
	struct asymmetric_key_id *match_id;
	size_t asciihexlen;
	int ret;

	if (!*id)
		return ERR_PTR(-EINVAL);
	asciihexlen = strlen(id);
	if (asciihexlen & 1)
		return ERR_PTR(-EINVAL);

	match_id = kmalloc(sizeof(struct asymmetric_key_id) + asciihexlen / 2,
			   GFP_KERNEL);
	if (!match_id)
		return ERR_PTR(-ENOMEM);
	ret = __asymmetric_key_hex_to_key_id(id, match_id, asciihexlen / 2);
	if (ret < 0) {
		kfree(match_id);
		return ERR_PTR(-EINVAL);
	}
	return match_id;
}

/*
 * Match asymmetric keys by an exact match on one of the first two IDs.
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
 * Match asymmetric keys by a partial match on one of the first two IDs.
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
 * Match asymmetric keys by an exact match on the third IDs.
 */
static bool asymmetric_key_cmp_name(const struct key *key,
				    const struct key_match_data *match_data)
{
	const struct asymmetric_key_ids *kids = asymmetric_key_ids(key);
	const struct asymmetric_key_id *match_id = match_data->preparsed;

	return kids && asymmetric_key_id_same(kids->id[2], match_id);
}

/*
 * Preparse the match criterion.  If we don't set lookup_type and cmp,
 * the default will be an exact match on the key description.
 *
 * There are some specifiers for matching key IDs rather than by the key
 * description:
 *
 *	"id:<id>" - find a key by partial match on one of the first two IDs
 *	"ex:<id>" - find a key by exact match on one of the first two IDs
 *	"dn:<id>" - find a key by exact match on the third ID
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
	} else if (spec[0] == 'd' &&
		   spec[1] == 'n' &&
		   spec[2] == ':') {
		id = spec + 3;
		cmp = asymmetric_key_cmp_name;
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
 * Clean up the key ID list
 */
static void asymmetric_key_free_kids(struct asymmetric_key_ids *kids)
{
	int i;

	if (kids) {
		for (i = 0; i < ARRAY_SIZE(kids->id); i++)
			kfree(kids->id[i]);
		kfree(kids);
	}
}

/*
 * Clean up the preparse data
 */
static void asymmetric_key_free_preparse(struct key_preparsed_payload *prep)
{
	struct asymmetric_key_subtype *subtype = prep->payload.data[asym_subtype];
	struct asymmetric_key_ids *kids = prep->payload.data[asym_key_ids];

	pr_devel("==>%s()\n", __func__);

	if (subtype) {
		subtype->destroy(prep->payload.data[asym_crypto],
				 prep->payload.data[asym_auth]);
		module_put(subtype->owner);
	}
	asymmetric_key_free_kids(kids);
	kfree(prep->description);
}

/*
 * dispose of the data dangling from the corpse of a asymmetric key
 */
static void asymmetric_key_destroy(struct key *key)
{
	struct asymmetric_key_subtype *subtype = asymmetric_key_subtype(key);
	struct asymmetric_key_ids *kids = key->payload.data[asym_key_ids];
	void *data = key->payload.data[asym_crypto];
	void *auth = key->payload.data[asym_auth];

	key->payload.data[asym_crypto] = NULL;
	key->payload.data[asym_subtype] = NULL;
	key->payload.data[asym_key_ids] = NULL;
	key->payload.data[asym_auth] = NULL;

	if (subtype) {
		subtype->destroy(data, auth);
		module_put(subtype->owner);
	}

	asymmetric_key_free_kids(kids);
}

static struct key_restriction *asymmetric_restriction_alloc(
	key_restrict_link_func_t check,
	struct key *key)
{
	struct key_restriction *keyres =
		kzalloc(sizeof(struct key_restriction), GFP_KERNEL);

	if (!keyres)
		return ERR_PTR(-ENOMEM);

	keyres->check = check;
	keyres->key = key;
	keyres->keytype = &key_type_asymmetric;

	return keyres;
}

/*
 * look up keyring restrict functions for asymmetric keys
 */
static struct key_restriction *asymmetric_lookup_restriction(
	const char *restriction)
{
	char *restrict_method;
	char *parse_buf;
	char *next;
	struct key_restriction *ret = ERR_PTR(-EINVAL);

	if (strcmp("builtin_trusted", restriction) == 0)
		return asymmetric_restriction_alloc(
			restrict_link_by_builtin_trusted, NULL);

	if (strcmp("builtin_and_secondary_trusted", restriction) == 0)
		return asymmetric_restriction_alloc(
			restrict_link_by_builtin_and_secondary_trusted, NULL);

	parse_buf = kstrndup(restriction, PAGE_SIZE, GFP_KERNEL);
	if (!parse_buf)
		return ERR_PTR(-ENOMEM);

	next = parse_buf;
	restrict_method = strsep(&next, ":");

	if ((strcmp(restrict_method, "key_or_keyring") == 0) && next) {
		char *key_text;
		key_serial_t serial;
		struct key *key;
		key_restrict_link_func_t link_fn =
			restrict_link_by_key_or_keyring;
		bool allow_null_key = false;

		key_text = strsep(&next, ":");

		if (next) {
			if (strcmp(next, "chain") != 0)
				goto out;

			link_fn = restrict_link_by_key_or_keyring_chain;
			allow_null_key = true;
		}

		if (kstrtos32(key_text, 0, &serial) < 0)
			goto out;

		if ((serial == 0) && allow_null_key) {
			key = NULL;
		} else {
			key = key_lookup(serial);
			if (IS_ERR(key)) {
				ret = ERR_CAST(key);
				goto out;
			}
		}

		ret = asymmetric_restriction_alloc(link_fn, key);
		if (IS_ERR(ret))
			key_put(key);
	}

out:
	kfree(parse_buf);
	return ret;
}

int asymmetric_key_eds_op(struct kernel_pkey_params *params,
			  const void *in, void *out)
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
	if (!subtype->eds_op)
		return -ENOTSUPP;

	ret = subtype->eds_op(params, in, out);

	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

static int asymmetric_key_verify_signature(struct kernel_pkey_params *params,
					   const void *in, const void *in2)
{
	struct public_key_signature sig = {
		.s_size		= params->in2_len,
		.digest_size	= params->in_len,
		.encoding	= params->encoding,
		.hash_algo	= params->hash_algo,
		.digest		= (void *)in,
		.s		= (void *)in2,
	};

	return verify_signature(params->key, &sig);
}

struct key_type key_type_asymmetric = {
	.name			= "asymmetric",
	.preparse		= asymmetric_key_preparse,
	.free_preparse		= asymmetric_key_free_preparse,
	.instantiate		= generic_key_instantiate,
	.match_preparse		= asymmetric_key_match_preparse,
	.match_free		= asymmetric_key_match_free,
	.destroy		= asymmetric_key_destroy,
	.describe		= asymmetric_key_describe,
	.lookup_restriction	= asymmetric_lookup_restriction,
	.asym_query		= query_asymmetric_key,
	.asym_eds_op		= asymmetric_key_eds_op,
	.asym_verify_signature	= asymmetric_key_verify_signature,
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
