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
#include "public_key.h"
#include "pkcs7_parser.h"

/**
 * Check the trust on one PKCS#7 SignedInfo block.
 */
int pkcs7_validate_trust_one(struct pkcs7_message *pkcs7,
			     struct pkcs7_signed_info *sinfo,
			     struct key *trust_keyring)
{
	struct public_key_signature *sig = &sinfo->sig;
	struct x509_certificate *x509, *last = NULL, *p;
	struct key *key;
	bool trusted;
	int ret;

	kenter(",%u,", sinfo->index);

	for (x509 = sinfo->signer; x509; x509 = x509->signer) {
		if (x509->seen) {
			if (x509->verified) {
				trusted = x509->trusted;
				goto verified;
			}
			kleave(" = -ENOKEY [cached]");
			return -ENOKEY;
		}
		x509->seen = true;

		/* Look to see if this certificate is present in the trusted
		 * keys.
		 */
		key = x509_request_asymmetric_key(trust_keyring, x509->subject,
						  x509->fingerprint);
		if (!IS_ERR(key))
			/* One of the X.509 certificates in the PKCS#7 message
			 * is apparently the same as one we already trust.
			 * Verify that the trusted variant can also validate
			 * the signature on the descendant.
			 */
			goto matched;
		if (key == ERR_PTR(-ENOMEM))
			return -ENOMEM;

		 /* Self-signed certificates form roots of their own, and if we
		  * don't know them, then we can't accept them.
		  */
		if (x509->next == x509) {
			kleave(" = -ENOKEY [unknown self-signed]");
			return -ENOKEY;
		}

		might_sleep();
		last = x509;
		sig = &last->sig;
	}

	/* No match - see if the root certificate has a signer amongst the
	 * trusted keys.
	 */
	if (!last || !last->issuer || !last->authority) {
		kleave(" = -ENOKEY [no backref]");
		return -ENOKEY;
	}

	key = x509_request_asymmetric_key(trust_keyring, last->issuer,
					  last->authority);
	if (IS_ERR(key))
		return PTR_ERR(key) == -ENOMEM ? -ENOMEM : -ENOKEY;
	x509 = last;

matched:
	ret = verify_signature(key, sig);
	trusted = test_bit(KEY_FLAG_TRUSTED, &key->flags);
	key_put(key);
	if (ret < 0) {
		if (ret == -ENOMEM)
			return ret;
		kleave(" = -EKEYREJECTED [verify %d]", ret);
		return -EKEYREJECTED;
	}

verified:
	x509->verified = true;
	for (p = sinfo->signer; p != x509; p = p->signer) {
		p->verified = true;
		p->trusted = trusted;
	}
	sinfo->trusted = trusted;
	kleave(" = 0");
	return 0;
}

/**
 * pkcs7_validate_trust - Validate PKCS#7 trust chain
 * @pkcs7: The PKCS#7 certificate to validate
 * @trust_keyring: Signing certificates to use as starting points
 * @_trusted: Set to true if trustworth, false otherwise
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
			 struct key *trust_keyring,
			 bool *_trusted)
{
	struct pkcs7_signed_info *sinfo;
	struct x509_certificate *p;
	int cached_ret = 0, ret;

	for (p = pkcs7->certs; p; p = p->next)
		p->seen = false;

	for (sinfo = pkcs7->signed_infos; sinfo; sinfo = sinfo->next) {
		ret = pkcs7_validate_trust_one(pkcs7, sinfo, trust_keyring);
		if (ret < 0) {
			if (ret == -ENOPKG) {
				cached_ret = -ENOPKG;
			} else if (ret == -ENOKEY) {
				if (cached_ret == 0)
					cached_ret = -ENOKEY;
			} else {
				return ret;
			}
		}
		*_trusted |= sinfo->trusted;
	}

	return cached_ret;
}
EXPORT_SYMBOL_GPL(pkcs7_validate_trust);
