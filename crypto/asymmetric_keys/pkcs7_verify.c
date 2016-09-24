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
#include <crypto/public_key.h>
#include "pkcs7_parser.h"

/*
 * Digest the relevant parts of the PKCS#7 data
 */
static int pkcs7_digest(struct pkcs7_message *pkcs7,
			struct pkcs7_signed_info *sinfo)
{
	struct public_key_signature *sig = sinfo->sig;
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t desc_size;
	int ret;

	kenter(",%u,%s", sinfo->index, sinfo->sig->hash_algo);

	if (!sinfo->sig->hash_algo)
		return -ENOPKG;

	/* Allocate the hashing algorithm we're going to need and find out how
	 * big the hash operational data will be.
	 */
	tfm = crypto_alloc_shash(sinfo->sig->hash_algo, 0, 0);
	if (IS_ERR(tfm))
		return (PTR_ERR(tfm) == -ENOENT) ? -ENOPKG : PTR_ERR(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	sig->digest_size = crypto_shash_digestsize(tfm);

	ret = -ENOMEM;
	sig->digest = kmalloc(sig->digest_size, GFP_KERNEL);
	if (!sig->digest)
		goto error_no_desc;

	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc)
		goto error_no_desc;

	desc->tfm   = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	/* Digest the message [RFC2315 9.3] */
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;
	ret = crypto_shash_finup(desc, pkcs7->data, pkcs7->data_len,
				 sig->digest);
	if (ret < 0)
		goto error;
	pr_devel("MsgDigest = [%*ph]\n", 8, sig->digest);

	/* However, if there are authenticated attributes, there must be a
	 * message digest attribute amongst them which corresponds to the
	 * digest we just calculated.
	 */
	if (sinfo->authattrs) {
		u8 tag;

		if (!sinfo->msgdigest) {
			pr_warn("Sig %u: No messageDigest\n", sinfo->index);
			ret = -EKEYREJECTED;
			goto error;
		}

		if (sinfo->msgdigest_len != sig->digest_size) {
			pr_debug("Sig %u: Invalid digest size (%u)\n",
				 sinfo->index, sinfo->msgdigest_len);
			ret = -EBADMSG;
			goto error;
		}

		if (memcmp(sig->digest, sinfo->msgdigest,
			   sinfo->msgdigest_len) != 0) {
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
		memset(sig->digest, 0, sig->digest_size);

		ret = crypto_shash_init(desc);
		if (ret < 0)
			goto error;
		tag = ASN1_CONS_BIT | ASN1_SET;
		ret = crypto_shash_update(desc, &tag, 1);
		if (ret < 0)
			goto error;
		ret = crypto_shash_finup(desc, sinfo->authattrs,
					 sinfo->authattrs_len, sig->digest);
		if (ret < 0)
			goto error;
		pr_devel("AADigest = [%*ph]\n", 8, sig->digest);
	}

error:
	kfree(desc);
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
		if (!asymmetric_key_id_same(x509->id, sinfo->sig->auth_ids[0]))
			continue;
		pr_devel("Sig %u: Found cert serial match X.509[%u]\n",
			 sinfo->index, certix);

		if (x509->pub->pkey_algo != sinfo->sig->pkey_algo) {
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
		 sinfo->sig->auth_ids[0]->len, sinfo->sig->auth_ids[0]->data);
	return 0;
}

/*
 * Verify the internal certificate chain as best we can.
 */
static int pkcs7_verify_sig_chain(struct pkcs7_message *pkcs7,
				  struct pkcs7_signed_info *sinfo)
{
	struct public_key_signature *sig;
	struct x509_certificate *x509 = sinfo->signer, *p;
	struct asymmetric_key_id *auth;
	int ret;

	kenter("");

	for (p = pkcs7->certs; p; p = p->next)
		p->seen = false;

	for (;;) {
		pr_debug("verify %s: %*phN\n",
			 x509->subject,
			 x509->raw_serial_size, x509->raw_serial);
		x509->seen = true;
		if (x509->unsupported_key)
			goto unsupported_crypto_in_x509;

		pr_debug("- issuer %s\n", x509->issuer);
		sig = x509->sig;
		if (sig->auth_ids[0])
			pr_debug("- authkeyid.id %*phN\n",
				 sig->auth_ids[0]->len, sig->auth_ids[0]->data);
		if (sig->auth_ids[1])
			pr_debug("- authkeyid.skid %*phN\n",
				 sig->auth_ids[1]->len, sig->auth_ids[1]->data);

		if (x509->self_signed) {
			/* If there's no authority certificate specified, then
			 * the certificate must be self-signed and is the root
			 * of the chain.  Likewise if the cert is its own
			 * authority.
			 */
			if (x509->unsupported_sig)
				goto unsupported_crypto_in_x509;
			x509->signer = x509;
			pr_debug("- self-signed\n");
			return 0;
		}

		/* Look through the X.509 certificates in the PKCS#7 message's
		 * list to see if the next one is there.
		 */
		auth = sig->auth_ids[0];
		if (auth) {
			pr_debug("- want %*phN\n", auth->len, auth->data);
			for (p = pkcs7->certs; p; p = p->next) {
				pr_debug("- cmp [%u] %*phN\n",
					 p->index, p->id->len, p->id->data);
				if (asymmetric_key_id_same(p->id, auth))
					goto found_issuer_check_skid;
			}
		} else if (sig->auth_ids[1]) {
			auth = sig->auth_ids[1];
			pr_debug("- want %*phN\n", auth->len, auth->data);
			for (p = pkcs7->certs; p; p = p->next) {
				if (!p->skid)
					continue;
				pr_debug("- cmp [%u] %*phN\n",
					 p->index, p->skid->len, p->skid->data);
				if (asymmetric_key_id_same(p->skid, auth))
					goto found_issuer;
			}
		}

		/* We didn't find the root of this chain */
		pr_debug("- top\n");
		return 0;

	found_issuer_check_skid:
		/* We matched issuer + serialNumber, but if there's an
		 * authKeyId.keyId, that must match the CA subjKeyId also.
		 */
		if (sig->auth_ids[1] &&
		    !asymmetric_key_id_same(p->skid, sig->auth_ids[1])) {
			pr_warn("Sig %u: X.509 chain contains auth-skid nonmatch (%u->%u)\n",
				sinfo->index, x509->index, p->index);
			return -EKEYREJECTED;
		}
	found_issuer:
		pr_debug("- subject %s\n", p->subject);
		if (p->seen) {
			pr_warn("Sig %u: X.509 chain contains loop\n",
				sinfo->index);
			return 0;
		}
		ret = public_key_verify_signature(p->pub, p->sig);
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

unsupported_crypto_in_x509:
	/* Just prune the certificate chain at this point if we lack some
	 * crypto module to go further.  Note, however, we don't want to set
	 * sinfo->unsupported_crypto as the signed info block may still be
	 * validatable against an X.509 cert lower in the chain that we have a
	 * trusted copy of.
	 */
	return 0;
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

	/* Check that the PKCS#7 signing time is valid according to the X.509
	 * certificate.  We can't, however, check against the system clock
	 * since that may not have been set yet and may be wrong.
	 */
	if (test_bit(sinfo_has_signing_time, &sinfo->aa_set)) {
		if (sinfo->signing_time < sinfo->signer->valid_from ||
		    sinfo->signing_time > sinfo->signer->valid_to) {
			pr_warn("Message signed outside of X.509 validity window\n");
			return -EKEYREJECTED;
		}
	}

	/* Verify the PKCS#7 binary against the key */
	ret = public_key_verify_signature(sinfo->signer->pub, sinfo->sig);
	if (ret < 0)
		return ret;

	pr_devel("Verified signature %u\n", sinfo->index);

	/* Verify the internal certificate chain */
	return pkcs7_verify_sig_chain(pkcs7, sinfo);
}

/**
 * pkcs7_verify - Verify a PKCS#7 message
 * @pkcs7: The PKCS#7 message to be verified
 * @usage: The use to which the key is being put
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
 *  (*) -EKEYREJECTED if a key was selected that had a usage restriction at
 *      odds with the specified usage, or:
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
int pkcs7_verify(struct pkcs7_message *pkcs7,
		 enum key_being_used_for usage)
{
	struct pkcs7_signed_info *sinfo;
	int enopkg = -ENOPKG;
	int ret;

	kenter("");

	switch (usage) {
	case VERIFYING_MODULE_SIGNATURE:
		if (pkcs7->data_type != OID_data) {
			pr_warn("Invalid module sig (not pkcs7-data)\n");
			return -EKEYREJECTED;
		}
		if (pkcs7->have_authattrs) {
			pr_warn("Invalid module sig (has authattrs)\n");
			return -EKEYREJECTED;
		}
		break;
	case VERIFYING_FIRMWARE_SIGNATURE:
		if (pkcs7->data_type != OID_data) {
			pr_warn("Invalid firmware sig (not pkcs7-data)\n");
			return -EKEYREJECTED;
		}
		if (!pkcs7->have_authattrs) {
			pr_warn("Invalid firmware sig (missing authattrs)\n");
			return -EKEYREJECTED;
		}
		break;
	case VERIFYING_KEXEC_PE_SIGNATURE:
		if (pkcs7->data_type != OID_msIndirectData) {
			pr_warn("Invalid kexec sig (not Authenticode)\n");
			return -EKEYREJECTED;
		}
		/* Authattr presence checked in parser */
		break;
	case VERIFYING_UNSPECIFIED_SIGNATURE:
		if (pkcs7->data_type != OID_data) {
			pr_warn("Invalid unspecified sig (not pkcs7-data)\n");
			return -EKEYREJECTED;
		}
		break;
	default:
		return -EINVAL;
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

/**
 * pkcs7_supply_detached_data - Supply the data needed to verify a PKCS#7 message
 * @pkcs7: The PKCS#7 message
 * @data: The data to be verified
 * @datalen: The amount of data
 *
 * Supply the detached data needed to verify a PKCS#7 message.  Note that no
 * attempt to retain/pin the data is made.  That is left to the caller.  The
 * data will not be modified by pkcs7_verify() and will not be freed when the
 * PKCS#7 message is freed.
 *
 * Returns -EINVAL if data is already supplied in the message, 0 otherwise.
 */
int pkcs7_supply_detached_data(struct pkcs7_message *pkcs7,
			       const void *data, size_t datalen)
{
	if (pkcs7->data) {
		pr_debug("Data already supplied\n");
		return -EINVAL;
	}
	pkcs7->data = data;
	pkcs7->data_len = datalen;
	return 0;
}
