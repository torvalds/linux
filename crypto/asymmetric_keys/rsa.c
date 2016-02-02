/* RSA asymmetric public-key algorithm [RFC3447]
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "RSA: "fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <crypto/akcipher.h>
#include <crypto/public_key.h>
#include <crypto/algapi.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RSA Public Key Algorithm");

#define kenter(FMT, ...) \
	pr_devel("==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	pr_devel("<== %s()"FMT"\n", __func__, ##__VA_ARGS__)

/*
 * Hash algorithm OIDs plus ASN.1 DER wrappings [RFC4880 sec 5.2.2].
 */
static const u8 RSA_digest_info_MD5[] = {
	0x30, 0x20, 0x30, 0x0C, 0x06, 0x08,
	0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x02, 0x05, /* OID */
	0x05, 0x00, 0x04, 0x10
};

static const u8 RSA_digest_info_SHA1[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2B, 0x0E, 0x03, 0x02, 0x1A,
	0x05, 0x00, 0x04, 0x14
};

static const u8 RSA_digest_info_RIPE_MD_160[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2B, 0x24, 0x03, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x14
};

static const u8 RSA_digest_info_SHA224[] = {
	0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04,
	0x05, 0x00, 0x04, 0x1C
};

static const u8 RSA_digest_info_SHA256[] = {
	0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x20
};

static const u8 RSA_digest_info_SHA384[] = {
	0x30, 0x41, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02,
	0x05, 0x00, 0x04, 0x30
};

static const u8 RSA_digest_info_SHA512[] = {
	0x30, 0x51, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03,
	0x05, 0x00, 0x04, 0x40
};

static const struct {
	const u8 *data;
	size_t size;
} RSA_ASN1_templates[PKEY_HASH__LAST] = {
#define _(X) { RSA_digest_info_##X, sizeof(RSA_digest_info_##X) }
	[HASH_ALGO_MD5]		= _(MD5),
	[HASH_ALGO_SHA1]	= _(SHA1),
	[HASH_ALGO_RIPE_MD_160]	= _(RIPE_MD_160),
	[HASH_ALGO_SHA256]	= _(SHA256),
	[HASH_ALGO_SHA384]	= _(SHA384),
	[HASH_ALGO_SHA512]	= _(SHA512),
	[HASH_ALGO_SHA224]	= _(SHA224),
#undef _
};

struct rsa_completion {
	struct completion completion;
	int err;
};

/*
 * Perform the RSA signature verification.
 * @H: Value of hash of data and metadata
 * @EM: The computed signature value
 * @k: The size of EM (EM[0] is an invalid location but should hold 0x00)
 * @hash_size: The size of H
 * @asn1_template: The DigestInfo ASN.1 template
 * @asn1_size: Size of asm1_template[]
 */
static int rsa_verify(const u8 *H, const u8 *EM, size_t k, size_t hash_size,
		      const u8 *asn1_template, size_t asn1_size)
{
	unsigned PS_end, T_offset, i;

	kenter(",,%zu,%zu,%zu", k, hash_size, asn1_size);

	if (k < 2 + 1 + asn1_size + hash_size)
		return -EBADMSG;

	/* Decode the EMSA-PKCS1-v1_5
	 * note: leading zeros are stripped by the RSA implementation
	 */
	if (EM[0] != 0x01) {
		kleave(" = -EBADMSG [EM[0] == %02u]", EM[0]);
		return -EBADMSG;
	}

	T_offset = k - (asn1_size + hash_size);
	PS_end = T_offset - 1;
	if (EM[PS_end] != 0x00) {
		kleave(" = -EBADMSG [EM[T-1] == %02u]", EM[PS_end]);
		return -EBADMSG;
	}

	for (i = 1; i < PS_end; i++) {
		if (EM[i] != 0xff) {
			kleave(" = -EBADMSG [EM[PS%x] == %02u]", i - 2, EM[i]);
			return -EBADMSG;
		}
	}

	if (crypto_memneq(asn1_template, EM + T_offset, asn1_size) != 0) {
		kleave(" = -EBADMSG [EM[T] ASN.1 mismatch]");
		return -EBADMSG;
	}

	if (crypto_memneq(H, EM + T_offset + asn1_size, hash_size) != 0) {
		kleave(" = -EKEYREJECTED [EM[T] hash mismatch]");
		return -EKEYREJECTED;
	}

	kleave(" = 0");
	return 0;
}

static void public_key_verify_done(struct crypto_async_request *req, int err)
{
	struct rsa_completion *compl = req->data;

	if (err == -EINPROGRESS)
		return;

	compl->err = err;
	complete(&compl->completion);
}

int rsa_verify_signature(const struct public_key *pkey,
			 const struct public_key_signature *sig)
{
	struct crypto_akcipher *tfm;
	struct akcipher_request *req;
	struct rsa_completion compl;
	struct scatterlist sig_sg, sg_out;
	void *outbuf = NULL;
	unsigned int outlen = 0;
	int ret = -ENOMEM;

	tfm = crypto_alloc_akcipher("rsa", 0, 0);
	if (IS_ERR(tfm))
		goto error_out;

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	ret = crypto_akcipher_set_pub_key(tfm, pkey->key, pkey->keylen);
	if (ret)
		goto error_free_req;

	ret = -EINVAL;
	outlen = crypto_akcipher_maxsize(tfm);
	if (!outlen)
		goto error_free_req;

	/* Initialize the output buffer */
	ret = -ENOMEM;
	outbuf = kmalloc(outlen, GFP_KERNEL);
	if (!outbuf)
		goto error_free_req;

	sg_init_one(&sig_sg, sig->s, sig->s_size);
	sg_init_one(&sg_out, outbuf, outlen);
	akcipher_request_set_crypt(req, &sig_sg, &sg_out, sig->s_size, outlen);
	init_completion(&compl.completion);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      public_key_verify_done, &compl);

	ret = crypto_akcipher_verify(req);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&compl.completion);
		ret = compl.err;
	}

	if (ret)
		goto error_free_req;

	/* Output from the operation is an encoded message (EM) of
	 * length k octets.
	 */
	outlen = req->dst_len;
	ret = rsa_verify(sig->digest, outbuf, outlen, sig->digest_size,
			 RSA_ASN1_templates[sig->pkey_hash_algo].data,
			 RSA_ASN1_templates[sig->pkey_hash_algo].size);
error_free_req:
	akcipher_request_free(req);
error_free_tfm:
	crypto_free_akcipher(tfm);
error_out:
	kfree(outbuf);
	return ret;
}
EXPORT_SYMBOL_GPL(rsa_verify_signature);
