/* Verify the signature on a PKCS#7 message.
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
#include <crypto/hash.h>
#include "public_key.h"
#include "pkcs7_parser.h"

/*
 * Digest the relevant parts of the PKCS#7 data
 */
static int pkcs7_digest(struct pkcs7_message *pkcs7,
			struct pkcs7_signed_info *sinfo)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	void *digest;
	int ret;

	kenter(",%u,%u", sinfo->index, sinfo->sig.pkey_hash_algo);

	if (sinfo->sig.pkey_hash_algo >= PKEY_HASH__LAST ||
	    !hash_algo_name[sinfo->sig.pkey_hash_algo])
		return -ENOPKG;

	/* Allocate the hashing algorithm we're going to need and find out how
	 * big the hash operational data will be.
	 */
	tfm = crypto_alloc_shash(hash_algo_name[sinfo->sig.pkey_hash_algo],
				 0, 0);
	if (IS_ERR(tfm))
		return (PTR_ERR(tfm) == -ENOENT) ? -ENOPKG : PTR_ERR(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	sinfo->sig.digest_size = digest_size = crypto_shash_digestsize(tfm);

	ret = -ENOMEM;
	digest = kzalloc(digest_size + desc_size, GFP_KERNEL);
	if (!digest)
		goto error_no_desc;

	desc = digest + digest_size;
	desc->tfm   = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	/* Digest the message [RFC2315 9.3] */
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;
	ret = crypto_shash_finup(desc, pkcs7->data, pkcs7->data_len, digest);
	if (ret < 0)
		goto error;
	pr_devel("MsgDigest = [%*ph]\n", 8, digest);

	/* However, if there are authenticated attributes, there must be a
	 * message digest attribute amongst them which corresponds to the
	 * digest we just calculated.
	 */
	if (sinfo->msgdigest) {
		u8 tag;

		if (sinfo->msgdigest_len != sinfo->sig.digest_size) {
			pr_debug("Sig %u: Invalid digest size (%u)\n",
				 sinfo->index, sinfo->msgdigest_len);
			ret = -EBADMSG;
			goto error;
		}

		if (memcmp(digest, sinfo->msgdigest, sinfo->msgdigest_len) != 0) {
			pr_debug("Sig %u: Message digest doesn't match\n",
				 sinfo->index);
			ret = -EKEYREJECTED;
			goto error;
		}

		/* We then calculate anew, using the authenticated attributes
		 * as the contents of the digest instead.  Note that we need to
		 * convert the attributes from a CONT.0 into a SET before we
		 * hash it.
		 */
		memset(digest, 0, sinfo->sig.digest_size);

		ret = crypto_shash_init(desc);
		if (ret < 0)
			goto error;
		tag = ASN1_CONS_BIT | ASN1_SET;
		ret = crypto_shash_update(desc, &tag, 1);
		if (ret < 0)
			goto error;
		ret = crypto_shash_finup(desc, sinfo->authattrs,
					 sinfo->authattrs_len, digest);
		if (ret < 0)
			goto error;
		pr_devel("AADigest = [%*ph]\n", 8, digest);
	}

	sinfo->sig.digest = digest;
	digest = NULL;

error:
	kfree(digest);
error_no_desc:
	crypto_free_shash(tfm);
	kleave(" = %d", ret);
	return ret;
}

/*
 * Find the key (X.509 certificate) to use to verify a PKCS#7 message.  PKCS#7
 * uses the issuer's name and the issuing certificate serial number for
 * matching purposes.  These must match the certificate issuer's name (not
 * subject's name) and the certificate serial number [RFC 2315 6.7].
 */
static int pkcs7_find_key(struct pkcs7_message *pkcs7,
			  struct pkcs7_signed_info *sinfo)
{
	struct x509_certificate *x509;
	unsigned certix = 1;

	kenter("%u", sinfo->index);

	for (x509 = pkcs7->certs; x509; x509 = x509->next, certix++) {
		/* I'm _assuming_ that the generator of the PKCS#7 message will
		 * encode the fields from the X.509 cert in the same way in the
		 * PKCS#7 message - but I can't be 100% sure of that.  It's
		 * possible this will need element-by-element comparison.
		 */
		if (!asymmetric_key_id_same(x509->id, sinfo->signing_cert_id))
			continue;
		pr_devel("Sig %u: Found cert serial match X.509[%u]\n",
			 sinfo->index, certix);

		if (x509->pub->pkey_algo != sinfo->sig.pkey_algo) {
			pr_warn("Sig %u: X.509 algo and PKCS#7 sig algo don't match\n",
				sinfo->index);
			continue;
		}

		sinfo->signer = x509;
		return 0;
	}

	/* The relevant X.509 cert isn't found here, but it might be found in
	 * the trust keyring.
	 */
	pr_debug("Sig %u: Issuing X.509 cert not found (#%*phN)\n",
		 sinfo->index,
		 sinfo->signing_cert_id->len, sinfo->signing_cert_id->data);
	return 0;
}

/*
 * Verify the internal certificate chain as best we can.
 */
static int pkcs7_verify_sig_chain(struct pkcs7_message *pkcs7,
				  struct pkcs7_signed_info *sinfo)
{
	struct x509_certificate *x509 = sinfo->signer, *p;
	int ret;

	kenter("");

	for (p = pkcs7->certs; p; p = p->next)
		p->seen = false;

	for (;;) {
		pr_debug("verify %s: %*phN\n",
			 x509->subject,
			 x509->raw_serial_size, x509->raw_serial);
		x509->seen = true;
		ret = x509_get_sig_params(x509);
		if (ret < 0)
			goto maybe_missing_crypto_in_x509;

		pr_debug("- issuer %s\n", x509->issuer);
		if (x509->authority)
			pr_debug("- authkeyid %*phN\n",
				 x509->authority->len, x509->authority->data);

		if (!x509->authority ||
		    strcmp(x509->subject, x509->issuer) == 0) {
			/* If there's no authority certificate specified, then
			 * the certificate must be self-signed and is the root
			 * of the chain.  Likewise if the cert is its own
			 * authority.
			 */
			pr_debug("- no auth?\n");
			if (x509->raw_subject_size != x509->raw_issuer_size ||
			    memcmp(x509->raw_subject, x509->raw_issuer,
				   x509->raw_issuer_size) != 0)
				return 0;

			ret = x509_check_signature(x509->pub, x509);
			if (ret < 0)
				goto maybe_missing_crypto_in_x509;
			x509->signer = x509;
			pr_debug("- self-signed\n");
			return 0;
		}

		/* Look through the X.509 certificates in the PKCS#7 message's
		 * list to see if the next one is there.
		 */
		pr_debug("- want %*phN\n",
			 x509->authority->len, x509->authority->data);
		for (p = pkcs7->certs; p; p = p->next) {
			if (!p->skid)
				continue;
			pr_debug("- cmp [%u] %*phN\n",
				 p->index, p->skid->len, p->skid->data);
			if (asymmetric_key_id_same(p->skid, x509->authority))
				goto found_issuer;
		}

		/* We didn't find the root of this chain */
		pr_debug("- top\n");
		return 0;

	found_issuer:
		pr_debug("- subject %s\n", p->subject);
		if (p->seen) {
			pr_warn("Sig %u: X.509 chain contains loop\n",
				sinfo->index);
			return 0;
		}
		ret = x509_check_signature(p->pub, x509);
		if (ret < 0)
			return ret;
		x509->signer = p;
		if (x509 == p) {
			pr_debug("- self-signed\n");
			return 0;
		}
		x509 = p;
		might_sleep();
	}

maybe_missing_crypto_in_x509:
	/* Just prune the certificate chain at this point if we lack some
	 * crypto module to go further.  Note, however, we don't want to set
	 * sinfo->missing_crypto as the signed info block may still be
	 * validatable against an X.509 cert lower in the chain that we have a
	 * trusted copy of.
	 */
	if (ret == -ENOPKG)
		return 0;
	return ret;
}

/*
 * Verify one signed information block from a PKCS#7 message.
 */
static int pkcs7_verify_one(struct pkcs7_message *pkcs7,
			    struct pkcs7_signed_info *sinfo)
{
	int ret;

	kenter(",%u", sinfo->index);

	/* First of all, digest the data in the PKCS#7 message and the
	 * signed information block
	 */
	ret = pkcs7_digest(pkcs7, sinfo);
	if (ret < 0)
		return ret;

	/* Find the key for the signature if there is one */
	ret = pkcs7_find_key(pkcs7, sinfo);
	if (ret < 0)
		return ret;

	if (!sinfo->signer)
		return 0;

	pr_devel("Using X.509[%u] for sig %u\n",
		 sinfo->signer->index, sinfo->index);

	/* Verify the PKCS#7 binary against the key */
	ret = public_key_verify_signature(sinfo->signer->pub, &sinfo->sig);
	if (ret < 0)
		return ret;

	pr_devel("Verified signature %u\n", sinfo->index);

	/* Verify the internal certificate chain */
	return pkcs7_verify_sig_chain(pkcs7, sinfo);
}

/**
 * pkcs7_verify - Verify a PKCS#7 message
 * @pkcs7: The PKCS#7 message to be verified
 *
 * Verify a PKCS#7 message is internally consistent - that is, the data digest
 * matches the digest in the AuthAttrs and any signature in the message or one
 * of the X.509 certificates it carries that matches another X.509 cert in the
 * message can be verified.
 *
 * This does not look to match the contents of the PKCS#7 message against any
 * external public keys.
 *
 * Returns, in order of descending priority:
 *
 *  (*) -EKEYREJECTED if a signature failed to match for which we found an
 *	appropriate X.509 certificate, or:
 *
 *  (*) -EBADMSG if some part of the message was invalid, or:
 *
 *  (*) -ENOPKG if none of the signature chains are verifiable because suitable
 *	crypto modules couldn't be found, or:
 *
 *  (*) 0 if all the signature chains that don't incur -ENOPKG can be verified
 *	(note that a signature chain may be of zero length), or:
 */
int pkcs7_verify(struct pkcs7_message *pkcs7)
{
	struct pkcs7_signed_info *sinfo;
	struct x509_certificate *x509;
	int enopkg = -ENOPKG;
	int ret, n;

	kenter("");

	for (n = 0, x509 = pkcs7->certs; x509; x509 = x509->next, n++) {
		ret = x509_get_sig_params(x509);
		if (ret < 0)
			return ret;
		pr_debug("X.509[%u] %*phN\n",
			 n, x509->authority->len, x509->authority->data);
	}

	for (sinfo = pkcs7->signed_infos; sinfo; sinfo = sinfo->next) {
		ret = pkcs7_verify_one(pkcs7, sinfo);
		if (ret < 0) {
			if (ret == -ENOPKG) {
				sinfo->unsupported_crypto = true;
				continue;
			}
			kleave(" = %d", ret);
			return ret;
		}
		enopkg = 0;
	}

	kleave(" = %d", enopkg);
	return enopkg;
}
EXPORT_SYMBOL_GPL(pkcs7_verify);
