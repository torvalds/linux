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
#include "asymmetric_keys.h"
#include "public_key.h"
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

/**
 * x509_request_asymmetric_key - Request a key by X.509 certificate params.
 * @keyring: The keys to search.
 * @kid: The key ID.
 * @partial: Use partial match if true, exact if false.
 *
 * Find a key in the given keyring by subject name and key ID.  These might,
 * for instance, be the issuer name and the authority key ID of an X.509
 * certificate that needs to be verified.
 */
struct key *x509_request_asymmetric_key(struct key *keyring,
					const struct asymmetric_key_id *kid,
					bool partial)
{
	key_ref_t key;
	char *id, *p;

	/* Construct an identifier "id:<keyid>". */
	p = id = kmalloc(2 + 1 + kid->len * 2 + 1, GFP_KERNEL);
	if (!id)
		return ERR_PTR(-ENOMEM);

	if (partial) {
		*p++ = 'i';
		*p++ = 'd';
	} else {
		*p++ = 'e';
		*p++ = 'x';
	}
	*p++ = ':';
	p = bin2hex(p, kid->data, kid->len);
	*p = 0;

	pr_debug("Look up: \"%s\"\n", id);

	key = keyring_search(make_key_ref(keyring, 1),
			     &key_type_asymmetric, id);
	if (IS_ERR(key))
		pr_debug("Request for key '%s' err %ld\n", id, PTR_ERR(key));
	kfree(id);

	if (IS_ERR(key)) {
		switch (PTR_ERR(key)) {
			/* Hide some search errors */
		case -EACCES:
		case -ENOTDIR:
		case -EAGAIN:
			return ERR_PTR(-ENOKEY);
		default:
			return ERR_CAST(key);
		}
	}

	pr_devel("<==%s() = 0 [%x]\n", __func__,
		 key_serial(key_ref_to_ptr(key)));
	return key_ref_to_ptr(key);
}
EXPORT_SYMBOL_GPL(x509_request_asymmetric_key);

/*
 * Set up the signature parameters in an X.509 certificate.  This involves
 * digesting the signed data and extracting the signature.
 */
int x509_get_sig_params(struct x509_certificate *cert)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	void *digest;
	int ret;

	pr_devel("==>%s()\n", __func__);

	if (cert->unsupported_crypto)
		return -ENOPKG;
	if (cert->sig.rsa.s)
		return 0;

	cert->sig.rsa.s = mpi_read_raw_data(cert->raw_sig, cert->raw_sig_size);
	if (!cert->sig.rsa.s)
		return -ENOMEM;
	cert->sig.nr_mpi = 1;

	/* Allocate the hashing algorithm we're going to need and find out how
	 * big the hash operational data will be.
	 */
	tfm = crypto_alloc_shash(hash_algo_name[cert->sig.pkey_hash_algo], 0, 0);
	if (IS_ERR(tfm)) {
		if (PTR_ERR(tfm) == -ENOENT) {
			cert->unsupported_crypto = true;
			return -ENOPKG;
		}
		return PTR_ERR(tfm);
	}

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	digest_size = crypto_shash_digestsize(tfm);

	/* We allocate the hash operational data storage on the end of the
	 * digest storage space.
	 */
	ret = -ENOMEM;
	digest = kzalloc(digest_size + desc_size, GFP_KERNEL);
	if (!digest)
		goto error;

	cert->sig.digest = digest;
	cert->sig.digest_size = digest_size;

	desc = digest + digest_size;
	desc->tfm = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;
	might_sleep();
	ret = crypto_shash_finup(desc, cert->tbs, cert->tbs_size, digest);
error:
	crypto_free_shash(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(x509_get_sig_params);

/*
 * Check the signature on a certificate using the provided public key
 */
int x509_check_signature(const struct public_key *pub,
			 struct x509_certificate *cert)
{
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = x509_get_sig_params(cert);
	if (ret < 0)
		return ret;

	ret = public_key_verify_signature(pub, &cert->sig);
	if (ret == -ENOPKG)
		cert->unsupported_crypto = true;
	pr_debug("Cert Verification: %d\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(x509_check_signature);

/*
 * Check the new certificate against the ones in the trust keyring.  If one of
 * those is the signing key and validates the new certificate, then mark the
 * new certificate as being trusted.
 *
 * Return 0 if the new certificate was successfully validated, 1 if we couldn't
 * find a matching parent certificate in the trusted list and an error if there
 * is a matching certificate but the signature check fails.
 */
static int x509_validate_trust(struct x509_certificate *cert,
			       struct key *trust_keyring)
{
	struct key *key;
	int ret = 1;

	if (!trust_keyring)
		return -EOPNOTSUPP;

	if (ca_keyid && !asymmetric_key_id_partial(cert->authority, ca_keyid))
		return -EPERM;

	key = x509_request_asymmetric_key(trust_keyring, cert->authority,
					  false);
	if (!IS_ERR(key))  {
		if (!use_builtin_keys
		    || test_bit(KEY_FLAG_BUILTIN, &key->flags))
			ret = x509_check_signature(key->payload.data, cert);
		key_put(key);
	}
	return ret;
}

/*
 * Attempt to parse a data blob for a key as an X509 certificate.
 */
static int x509_key_preparse(struct key_preparsed_payload *prep)
{
	struct asymmetric_key_ids *kids;
	struct x509_certificate *cert;
	const char *q;
	size_t srlen, sulen;
	char *desc = NULL, *p;
	int ret;

	cert = x509_cert_parse(prep->data, prep->datalen);
	if (IS_ERR(cert))
		return PTR_ERR(cert);

	pr_devel("Cert Issuer: %s\n", cert->issuer);
	pr_devel("Cert Subject: %s\n", cert->subject);

	if (cert->pub->pkey_algo >= PKEY_ALGO__LAST ||
	    cert->sig.pkey_algo >= PKEY_ALGO__LAST ||
	    cert->sig.pkey_hash_algo >= PKEY_HASH__LAST ||
	    !pkey_algo[cert->pub->pkey_algo] ||
	    !pkey_algo[cert->sig.pkey_algo] ||
	    !hash_algo_name[cert->sig.pkey_hash_algo]) {
		ret = -ENOPKG;
		goto error_free_cert;
	}

	pr_devel("Cert Key Algo: %s\n", pkey_algo_name[cert->pub->pkey_algo]);
	pr_devel("Cert Valid From: %04ld-%02d-%02d %02d:%02d:%02d\n",
		 cert->valid_from.tm_year + 1900, cert->valid_from.tm_mon + 1,
		 cert->valid_from.tm_mday, cert->valid_from.tm_hour,
		 cert->valid_from.tm_min,  cert->valid_from.tm_sec);
	pr_devel("Cert Valid To: %04ld-%02d-%02d %02d:%02d:%02d\n",
		 cert->valid_to.tm_year + 1900, cert->valid_to.tm_mon + 1,
		 cert->valid_to.tm_mday, cert->valid_to.tm_hour,
		 cert->valid_to.tm_min,  cert->valid_to.tm_sec);
	pr_devel("Cert Signature: %s + %s\n",
		 pkey_algo_name[cert->sig.pkey_algo],
		 hash_algo_name[cert->sig.pkey_hash_algo]);

	cert->pub->algo = pkey_algo[cert->pub->pkey_algo];
	cert->pub->id_type = PKEY_ID_X509;

	/* Check the signature on the key if it appears to be self-signed */
	if (!cert->authority ||
	    asymmetric_key_id_same(cert->skid, cert->authority)) {
		ret = x509_check_signature(cert->pub, cert); /* self-signed */
		if (ret < 0)
			goto error_free_cert;
	} else if (!prep->trusted) {
		ret = x509_validate_trust(cert, get_system_trusted_keyring());
		if (!ret)
			prep->trusted = 1;
	}

	/* Propose a description */
	sulen = strlen(cert->subject);
	if (cert->raw_skid) {
		srlen = cert->raw_skid_size;
		q = cert->raw_skid;
	} else {
		srlen = cert->raw_serial_size;
		q = cert->raw_serial;
	}
	if (srlen > 1 && *q == 0) {
		srlen--;
		q++;
	}

	ret = -ENOMEM;
	desc = kmalloc(sulen + 2 + srlen * 2 + 1, GFP_KERNEL);
	if (!desc)
		goto error_free_cert;
	p = memcpy(desc, cert->subject, sulen);
	p += sulen;
	*p++ = ':';
	*p++ = ' ';
	p = bin2hex(p, q, srlen);
	*p = 0;

	kids = kmalloc(sizeof(struct asymmetric_key_ids), GFP_KERNEL);
	if (!kids)
		goto error_free_desc;
	kids->id[0] = cert->id;
	kids->id[1] = cert->skid;

	/* We're pinning the module by being linked against it */
	__module_get(public_key_subtype.owner);
	prep->type_data[0] = &public_key_subtype;
	prep->type_data[1] = kids;
	prep->payload[0] = cert->pub;
	prep->description = desc;
	prep->quotalen = 100;

	/* We've finished with the certificate */
	cert->pub = NULL;
	cert->id = NULL;
	cert->skid = NULL;
	desc = NULL;
	ret = 0;

error_free_desc:
	kfree(desc);
error_free_cert:
	x509_free_certificate(cert);
	return ret;
}

static struct asymmetric_key_parser x509_key_parser = {
	.owner	= THIS_MODULE,
	.name	= "x509",
	.parse	= x509_key_preparse,
};

/*
 * Module stuff
 */
static int __init x509_key_init(void)
{
	return register_asymmetric_key_parser(&x509_key_parser);
}

static void __exit x509_key_exit(void)
{
	unregister_asymmetric_key_parser(&x509_key_parser);
}

module_init(x509_key_init);
module_exit(x509_key_exit);

MODULE_DESCRIPTION("X.509 certificate parser");
MODULE_LICENSE("GPL");
