// SPDX-License-Identifier: GPL-2.0-or-later
/* In-software asymmetric public-key crypto subtype
 *
 * See Documentation/crypto/asymmetric-keys.rst
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "PKEY: "fmt
#include <crypto/akcipher.h>
#include <crypto/public_key.h>
#include <crypto/sig.h>
#include <keys/asymmetric-subtype.h>
#include <linux/asn1.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

MODULE_DESCRIPTION("In-software asymmetric public-key subtype");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

/*
 * Provide a part of a description of the key for /proc/keys.
 */
static void public_key_describe(const struct key *asymmetric_key,
				struct seq_file *m)
{
	struct public_key *key = asymmetric_key->payload.data[asym_crypto];

	if (key)
		seq_printf(m, "%s.%s", key->id_type, key->pkey_algo);
}

/*
 * Destroy a public key algorithm key.
 */
void public_key_free(struct public_key *key)
{
	if (key) {
		kfree_sensitive(key->key);
		kfree(key->params);
		kfree(key);
	}
}
EXPORT_SYMBOL_GPL(public_key_free);

/*
 * Destroy a public key algorithm key.
 */
static void public_key_destroy(void *payload0, void *payload3)
{
	public_key_free(payload0);
	public_key_signature_free(payload3);
}

/*
 * Given a public_key, and an encoding and hash_algo to be used for signing
 * and/or verification with that key, determine the name of the corresponding
 * akcipher algorithm.  Also check that encoding and hash_algo are allowed.
 */
static int
software_key_determine_akcipher(const struct public_key *pkey,
				const char *encoding, const char *hash_algo,
				char alg_name[CRYPTO_MAX_ALG_NAME], bool *sig,
				enum kernel_pkey_operation op)
{
	int n;

	*sig = true;

	if (!encoding)
		return -EINVAL;

	if (strcmp(pkey->pkey_algo, "rsa") == 0) {
		/*
		 * RSA signatures usually use EMSA-PKCS1-1_5 [RFC3447 sec 8.2].
		 */
		if (strcmp(encoding, "pkcs1") == 0) {
			*sig = op == kernel_pkey_sign ||
			       op == kernel_pkey_verify;
			if (!hash_algo) {
				n = snprintf(alg_name, CRYPTO_MAX_ALG_NAME,
					     "pkcs1pad(%s)",
					     pkey->pkey_algo);
			} else {
				n = snprintf(alg_name, CRYPTO_MAX_ALG_NAME,
					     "pkcs1pad(%s,%s)",
					     pkey->pkey_algo, hash_algo);
			}
			return n >= CRYPTO_MAX_ALG_NAME ? -EINVAL : 0;
		}
		if (strcmp(encoding, "raw") != 0)
			return -EINVAL;
		/*
		 * Raw RSA cannot differentiate between different hash
		 * algorithms.
		 */
		if (hash_algo)
			return -EINVAL;
		*sig = false;
	} else if (strncmp(pkey->pkey_algo, "ecdsa", 5) == 0) {
		if (strcmp(encoding, "x962") != 0)
			return -EINVAL;
		/*
		 * ECDSA signatures are taken over a raw hash, so they don't
		 * differentiate between different hash algorithms.  That means
		 * that the verifier should hard-code a specific hash algorithm.
		 * Unfortunately, in practice ECDSA is used with multiple SHAs,
		 * so we have to allow all of them and not just one.
		 */
		if (!hash_algo)
			return -EINVAL;
		if (strcmp(hash_algo, "sha1") != 0 &&
		    strcmp(hash_algo, "sha224") != 0 &&
		    strcmp(hash_algo, "sha256") != 0 &&
		    strcmp(hash_algo, "sha384") != 0 &&
		    strcmp(hash_algo, "sha512") != 0 &&
		    strcmp(hash_algo, "sha3-256") != 0 &&
		    strcmp(hash_algo, "sha3-384") != 0 &&
		    strcmp(hash_algo, "sha3-512") != 0)
			return -EINVAL;
	} else if (strcmp(pkey->pkey_algo, "sm2") == 0) {
		if (strcmp(encoding, "raw") != 0)
			return -EINVAL;
		if (!hash_algo)
			return -EINVAL;
		if (strcmp(hash_algo, "sm3") != 0)
			return -EINVAL;
	} else if (strcmp(pkey->pkey_algo, "ecrdsa") == 0) {
		if (strcmp(encoding, "raw") != 0)
			return -EINVAL;
		if (!hash_algo)
			return -EINVAL;
		if (strcmp(hash_algo, "streebog256") != 0 &&
		    strcmp(hash_algo, "streebog512") != 0)
			return -EINVAL;
	} else {
		/* Unknown public key algorithm */
		return -ENOPKG;
	}
	if (strscpy(alg_name, pkey->pkey_algo, CRYPTO_MAX_ALG_NAME) < 0)
		return -EINVAL;
	return 0;
}

static u8 *pkey_pack_u32(u8 *dst, u32 val)
{
	memcpy(dst, &val, sizeof(val));
	return dst + sizeof(val);
}

/*
 * Query information about a key.
 */
static int software_key_query(const struct kernel_pkey_params *params,
			      struct kernel_pkey_query *info)
{
	struct crypto_akcipher *tfm;
	struct public_key *pkey = params->key->payload.data[asym_crypto];
	char alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_sig *sig;
	u8 *key, *ptr;
	int ret, len;
	bool issig;

	ret = software_key_determine_akcipher(pkey, params->encoding,
					      params->hash_algo, alg_name,
					      &issig, kernel_pkey_sign);
	if (ret < 0)
		return ret;

	key = kmalloc(pkey->keylen + sizeof(u32) * 2 + pkey->paramlen,
		      GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	memcpy(key, pkey->key, pkey->keylen);
	ptr = key + pkey->keylen;
	ptr = pkey_pack_u32(ptr, pkey->algo);
	ptr = pkey_pack_u32(ptr, pkey->paramlen);
	memcpy(ptr, pkey->params, pkey->paramlen);

	if (issig) {
		sig = crypto_alloc_sig(alg_name, 0, 0);
		if (IS_ERR(sig)) {
			ret = PTR_ERR(sig);
			goto error_free_key;
		}

		if (pkey->key_is_private)
			ret = crypto_sig_set_privkey(sig, key, pkey->keylen);
		else
			ret = crypto_sig_set_pubkey(sig, key, pkey->keylen);
		if (ret < 0)
			goto error_free_tfm;

		len = crypto_sig_maxsize(sig);

		info->supported_ops = KEYCTL_SUPPORTS_VERIFY;
		if (pkey->key_is_private)
			info->supported_ops |= KEYCTL_SUPPORTS_SIGN;

		if (strcmp(params->encoding, "pkcs1") == 0) {
			info->supported_ops |= KEYCTL_SUPPORTS_ENCRYPT;
			if (pkey->key_is_private)
				info->supported_ops |= KEYCTL_SUPPORTS_DECRYPT;
		}
	} else {
		tfm = crypto_alloc_akcipher(alg_name, 0, 0);
		if (IS_ERR(tfm)) {
			ret = PTR_ERR(tfm);
			goto error_free_key;
		}

		if (pkey->key_is_private)
			ret = crypto_akcipher_set_priv_key(tfm, key, pkey->keylen);
		else
			ret = crypto_akcipher_set_pub_key(tfm, key, pkey->keylen);
		if (ret < 0)
			goto error_free_tfm;

		len = crypto_akcipher_maxsize(tfm);

		info->supported_ops = KEYCTL_SUPPORTS_ENCRYPT;
		if (pkey->key_is_private)
			info->supported_ops |= KEYCTL_SUPPORTS_DECRYPT;
	}

	info->key_size = len * 8;

	if (strncmp(pkey->pkey_algo, "ecdsa", 5) == 0) {
		/*
		 * ECDSA key sizes are much smaller than RSA, and thus could
		 * operate on (hashed) inputs that are larger than key size.
		 * For example SHA384-hashed input used with secp256r1
		 * based keys.  Set max_data_size to be at least as large as
		 * the largest supported hash size (SHA512)
		 */
		info->max_data_size = 64;

		/*
		 * Verify takes ECDSA-Sig (described in RFC 5480) as input,
		 * which is actually 2 'key_size'-bit integers encoded in
		 * ASN.1.  Account for the ASN.1 encoding overhead here.
		 */
		info->max_sig_size = 2 * (len + 3) + 2;
	} else {
		info->max_data_size = len;
		info->max_sig_size = len;
	}

	info->max_enc_size = len;
	info->max_dec_size = len;

	ret = 0;

error_free_tfm:
	if (issig)
		crypto_free_sig(sig);
	else
		crypto_free_akcipher(tfm);
error_free_key:
	kfree_sensitive(key);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Do encryption, decryption and signing ops.
 */
static int software_key_eds_op(struct kernel_pkey_params *params,
			       const void *in, void *out)
{
	const struct public_key *pkey = params->key->payload.data[asym_crypto];
	char alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_akcipher *tfm;
	struct crypto_sig *sig;
	char *key, *ptr;
	bool issig;
	int ksz;
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = software_key_determine_akcipher(pkey, params->encoding,
					      params->hash_algo, alg_name,
					      &issig, params->op);
	if (ret < 0)
		return ret;

	key = kmalloc(pkey->keylen + sizeof(u32) * 2 + pkey->paramlen,
		      GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	memcpy(key, pkey->key, pkey->keylen);
	ptr = key + pkey->keylen;
	ptr = pkey_pack_u32(ptr, pkey->algo);
	ptr = pkey_pack_u32(ptr, pkey->paramlen);
	memcpy(ptr, pkey->params, pkey->paramlen);

	if (issig) {
		sig = crypto_alloc_sig(alg_name, 0, 0);
		if (IS_ERR(sig)) {
			ret = PTR_ERR(sig);
			goto error_free_key;
		}

		if (pkey->key_is_private)
			ret = crypto_sig_set_privkey(sig, key, pkey->keylen);
		else
			ret = crypto_sig_set_pubkey(sig, key, pkey->keylen);
		if (ret)
			goto error_free_tfm;

		ksz = crypto_sig_maxsize(sig);
	} else {
		tfm = crypto_alloc_akcipher(alg_name, 0, 0);
		if (IS_ERR(tfm)) {
			ret = PTR_ERR(tfm);
			goto error_free_key;
		}

		if (pkey->key_is_private)
			ret = crypto_akcipher_set_priv_key(tfm, key, pkey->keylen);
		else
			ret = crypto_akcipher_set_pub_key(tfm, key, pkey->keylen);
		if (ret)
			goto error_free_tfm;

		ksz = crypto_akcipher_maxsize(tfm);
	}

	ret = -EINVAL;

	/* Perform the encryption calculation. */
	switch (params->op) {
	case kernel_pkey_encrypt:
		if (issig)
			break;
		ret = crypto_akcipher_sync_encrypt(tfm, in, params->in_len,
						   out, params->out_len);
		break;
	case kernel_pkey_decrypt:
		if (issig)
			break;
		ret = crypto_akcipher_sync_decrypt(tfm, in, params->in_len,
						   out, params->out_len);
		break;
	case kernel_pkey_sign:
		if (!issig)
			break;
		ret = crypto_sig_sign(sig, in, params->in_len,
				      out, params->out_len);
		break;
	default:
		BUG();
	}

	if (ret == 0)
		ret = ksz;

error_free_tfm:
	if (issig)
		crypto_free_sig(sig);
	else
		crypto_free_akcipher(tfm);
error_free_key:
	kfree_sensitive(key);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Verify a signature using a public key.
 */
int public_key_verify_signature(const struct public_key *pkey,
				const struct public_key_signature *sig)
{
	char alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_sig *tfm;
	char *key, *ptr;
	bool issig;
	int ret;

	pr_devel("==>%s()\n", __func__);

	BUG_ON(!pkey);
	BUG_ON(!sig);
	BUG_ON(!sig->s);

	/*
	 * If the signature specifies a public key algorithm, it *must* match
	 * the key's actual public key algorithm.
	 *
	 * Small exception: ECDSA signatures don't specify the curve, but ECDSA
	 * keys do.  So the strings can mismatch slightly in that case:
	 * "ecdsa-nist-*" for the key, but "ecdsa" for the signature.
	 */
	if (sig->pkey_algo) {
		if (strcmp(pkey->pkey_algo, sig->pkey_algo) != 0 &&
		    (strncmp(pkey->pkey_algo, "ecdsa-", 6) != 0 ||
		     strcmp(sig->pkey_algo, "ecdsa") != 0))
			return -EKEYREJECTED;
	}

	ret = software_key_determine_akcipher(pkey, sig->encoding,
					      sig->hash_algo, alg_name,
					      &issig, kernel_pkey_verify);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_sig(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	key = kmalloc(pkey->keylen + sizeof(u32) * 2 + pkey->paramlen,
		      GFP_KERNEL);
	if (!key) {
		ret = -ENOMEM;
		goto error_free_tfm;
	}

	memcpy(key, pkey->key, pkey->keylen);
	ptr = key + pkey->keylen;
	ptr = pkey_pack_u32(ptr, pkey->algo);
	ptr = pkey_pack_u32(ptr, pkey->paramlen);
	memcpy(ptr, pkey->params, pkey->paramlen);

	if (pkey->key_is_private)
		ret = crypto_sig_set_privkey(tfm, key, pkey->keylen);
	else
		ret = crypto_sig_set_pubkey(tfm, key, pkey->keylen);
	if (ret)
		goto error_free_key;

	ret = crypto_sig_verify(tfm, sig->s, sig->s_size,
				sig->digest, sig->digest_size);

error_free_key:
	kfree_sensitive(key);
error_free_tfm:
	crypto_free_sig(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	if (WARN_ON_ONCE(ret > 0))
		ret = -EINVAL;
	return ret;
}
EXPORT_SYMBOL_GPL(public_key_verify_signature);

static int public_key_verify_signature_2(const struct key *key,
					 const struct public_key_signature *sig)
{
	const struct public_key *pk = key->payload.data[asym_crypto];
	return public_key_verify_signature(pk, sig);
}

/*
 * Public key algorithm asymmetric key subtype
 */
struct asymmetric_key_subtype public_key_subtype = {
	.owner			= THIS_MODULE,
	.name			= "public_key",
	.name_len		= sizeof("public_key") - 1,
	.describe		= public_key_describe,
	.destroy		= public_key_destroy,
	.query			= software_key_query,
	.eds_op			= software_key_eds_op,
	.verify_signature	= public_key_verify_signature_2,
};
EXPORT_SYMBOL_GPL(public_key_subtype);
