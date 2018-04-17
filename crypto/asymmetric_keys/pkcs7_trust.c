/* Validate the trust chain of a PKCS#7 message.
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PKCS7: "fmt
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/asn1.h>
#include <linux/key.h>
#include <keys/asymmetric-type.h>
#include <crypto/public_key.h>
#include "pkcs7_parser.h"

/**
 * Check the trust on one PKCS#7 SignedInfo block.
 */
static int pkcs7_validate_trust_one(struct pkcs7_message *pkcs7,
				    struct pkcs7_signed_info *sinfo,
				    struct key *trust_keyring)
{
	struct public_key_signature *sig = sinfo->sig;
	struct x509_certificate *x509, *last = NULL, *p;
	struct key *key;
	int ret;

	kenter(",%u,", sinfo->index);

	if (sinfo->unsupported_crypto) {
		kleave(" = -ENOPKG [cached]");
		return -ENOPKG;
	}

	for (x509 = sinfo->signer; x509; x509 = x509->signer) {
		if (x509->seen) {
			if (x509->verified)
				goto verified;
			kleave(" = -ENOKEY [cached]");
			return -ENOKEY;
		}
		x509->seen = true;

		/* Look to see if this certificate is present in the trusted
		 * keys.
		 */
		key = find_asymmetric_key(trust_keyring,
					  x509->id, x509->skid, false);
		if (!IS_ERR(key)) {
			/* One of the X.509 certificates in the PKCS#7 message
			 * is apparently the same as one we already trust.
			 * Verify that the trusted variant can also validate
			 * the signature on the descendant.
			 */
			pr_devel("sinfo %u: Cert %u as key %x\n",
				 sinfo->index, x509->index, key_serial(key));
			goto matched;
		}
		if (key == ERR_PTR(-ENOMEM))
			return -ENOMEM;

		 /* Self-signed certificates form roots of their own, and if we
		  * don't know them, then we can't accept them.
		  */
		if (x509->signer == x509) {
			kleave(" = -ENOKEY [unknown self-signed]");
			return -ENOKEY;
		}

		might_sleep();
		last = x509;
		sig = last->sig;
	}

	/* No match - see if the root certificate has a signer amongst the
	 * trusted keys.
	 */
	if (last && (last->sig->auth_ids[0] || last->sig->auth_ids[1])) {
		key = find_asymmetric_key(trust_keyring,
					  last->sig->auth_ids[0],
					  last->sig->auth_ids[1],
					  false);
		if (!IS_ERR(key)) {
			x509 = last;
			pr_devel("sinfo %u: Root cert %u signer is key %x\n",
				 sinfo->index, x509->index, key_serial(key));
			goto matched;
		}
		if (PTR_ERR(key) != -ENOKEY)
			return PTR_ERR(key);
	}

	/* As a last resort, see if we have a trusted public key that matches
	 * the signed info directly.
	 */
	key = find_asymmetric_key(trust_keyring,
				  sinfo->sig->auth_ids[0], NULL, false);
	if (!IS_ERR(key)) {
		pr_devel("sinfo %u: Direct signer is key %x\n",
			 sinfo->index, key_serial(key));
		x509 = NULL;
		goto matched;
	}
	if (PTR_ERR(key) != -ENOKEY)
		return PTR_ERR(key);

	kleave(" = -ENOKEY [no backref]");
	return -ENOKEY;

matched:
	ret = verify_signature(key, sig);
	key_put(key);
	if (ret < 0) {
		if (ret == -ENOMEM)
			return ret;
		kleave(" = -EKEYREJECTED [verify %d]", ret);
		return -EKEYREJECTED;
	}

verified:
	if (x509) {
		x509->verified = true;
		for (p = sinfo->signer; p != x509; p = p->signer)
			p->verified = true;
	}
	kleave(" = 0");
	return 0;
}

/**
 * pkcs7_validate_trust - Validate PKCS#7 trust chain
 * @pkcs7: The PKCS#7 certificate to validate
 * @trust_keyring: Signing certificates to use as starting points
 *
 * Validate that the certificate chain inside the PKCS#7 message intersects
 * keys we already know and trust.
 *
 * Returns, in order of descending priority:
 *
 *  (*) -EKEYREJECTED if a signature failed to match for which we have a valid
 *	key, or:
 *
 *  (*) 0 if at least one signature chain intersects with the keys in the trust
 *	keyring, or:
 *
 *  (*) -ENOPKG if a suitable crypto module couldn't be found for a check on a
 *	chain.
 *
 *  (*) -ENOKEY if we couldn't find a match for any of the signature chains in
 *	the message.
 *
 * May also return -ENOMEM.
 */
int pkcs7_validate_trust(struct pkcs7_message *pkcs7,
			 struct key *trust_keyring)
{
	struct pkcs7_signed_info *sinfo;
	struct x509_certificate *p;
	int cached_ret = -ENOKEY;
	int ret;

	for (p = pkcs7->certs; p; p = p->next)
		p->seen = false;

	for (sinfo = pkcs7->signed_infos; sinfo; sinfo = sinfo->next) {
		ret = pkcs7_validate_trust_one(pkcs7, sinfo, trust_keyring);
		switch (ret) {
		case -ENOKEY:
			continue;
		case -ENOPKG:
			if (cached_ret == -ENOKEY)
				cached_ret = -ENOPKG;
			continue;
		case 0:
			cached_ret = 0;
			continue;
		default:
			return ret;
		}
	}

	return cached_ret;
}
EXPORT_SYMBOL_GPL(pkcs7_validate_trust);
