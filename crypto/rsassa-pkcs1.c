// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RSA Signature Scheme with Appendix - PKCS #1 v1.5 (RFC 8017 sec 8.2)
 *
 * https://www.rfc-editor.org/rfc/rfc8017#section-8.2
 *
 * Copyright (c) 2015 - 2024 Intel Corporation
 */

#include <linux/module.h>
#include <linux/scatterlist.h>
#include <crypto/akcipher.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/sig.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/sig.h>

/*
 * Full Hash Prefix for EMSA-PKCS1-v1_5 encoding method (RFC 9580 table 24)
 *
 * RSA keys are usually much larger than the hash of the message to be signed.
 * The hash is therefore prepended by the Full Hash Prefix and a 0xff padding.
 * The Full Hash Prefix is an ASN.1 SEQUENCE containing the hash algorithm OID.
 *
 * https://www.rfc-editor.org/rfc/rfc9580#table-24
 */

static const u8 hash_prefix_none[] = { };

static const u8 hash_prefix_md5[] = {
	0x30, 0x20, 0x30, 0x0c, 0x06, 0x08,	  /* SEQUENCE (SEQUENCE (OID */
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05,	/*	<algorithm>, */
	0x05, 0x00, 0x04, 0x10		      /* NULL), OCTET STRING <hash>) */
};

static const u8 hash_prefix_sha1[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2b, 0x0e, 0x03, 0x02, 0x1a,
	0x05, 0x00, 0x04, 0x14
};

static const u8 hash_prefix_rmd160[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2b, 0x24, 0x03, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x14
};

static const u8 hash_prefix_sha224[] = {
	0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04,
	0x05, 0x00, 0x04, 0x1c
};

static const u8 hash_prefix_sha256[] = {
	0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x20
};

static const u8 hash_prefix_sha384[] = {
	0x30, 0x41, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02,
	0x05, 0x00, 0x04, 0x30
};

static const u8 hash_prefix_sha512[] = {
	0x30, 0x51, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03,
	0x05, 0x00, 0x04, 0x40
};

static const u8 hash_prefix_sha3_256[] = {
	0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x08,
	0x05, 0x00, 0x04, 0x20
};

static const u8 hash_prefix_sha3_384[] = {
	0x30, 0x41, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x09,
	0x05, 0x00, 0x04, 0x30
};

static const u8 hash_prefix_sha3_512[] = {
	0x30, 0x51, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x0a,
	0x05, 0x00, 0x04, 0x40
};

static const struct hash_prefix {
	const char	*name;
	const u8	*data;
	size_t		size;
} hash_prefixes[] = {
#define _(X) { #X, hash_prefix_##X, sizeof(hash_prefix_##X) }
	_(none),
	_(md5),
	_(sha1),
	_(rmd160),
	_(sha256),
	_(sha384),
	_(sha512),
	_(sha224),
#undef _
#define _(X) { "sha3-" #X, hash_prefix_sha3_##X, sizeof(hash_prefix_sha3_##X) }
	_(256),
	_(384),
	_(512),
#undef _
	{ NULL }
};

static const struct hash_prefix *rsassa_pkcs1_find_hash_prefix(const char *name)
{
	const struct hash_prefix *p;

	for (p = hash_prefixes; p->name; p++)
		if (strcmp(name, p->name) == 0)
			return p;
	return NULL;
}

static bool rsassa_pkcs1_invalid_hash_len(unsigned int len,
					  const struct hash_prefix *p)
{
	/*
	 * Legacy protocols such as TLS 1.1 or earlier and IKE version 1
	 * do not prepend a Full Hash Prefix to the hash.  In that case,
	 * the size of the Full Hash Prefix is zero.
	 */
	if (p->data == hash_prefix_none)
		return false;

	/*
	 * The final byte of the Full Hash Prefix encodes the hash length.
	 *
	 * This needs to be revisited should hash algorithms with more than
	 * 1016 bits (127 bytes * 8) ever be added.  The length would then
	 * be encoded into more than one byte by ASN.1.
	 */
	static_assert(HASH_MAX_DIGESTSIZE <= 127);

	return len != p->data[p->size - 1];
}

struct rsassa_pkcs1_ctx {
	struct crypto_akcipher *child;
	unsigned int key_size;
};

struct rsassa_pkcs1_inst_ctx {
	struct crypto_akcipher_spawn spawn;
	const struct hash_prefix *hash_prefix;
};

static int rsassa_pkcs1_sign(struct crypto_sig *tfm,
			     const void *src, unsigned int slen,
			     void *dst, unsigned int dlen)
{
	struct sig_instance *inst = sig_alg_instance(tfm);
	struct rsassa_pkcs1_inst_ctx *ictx = sig_instance_ctx(inst);
	const struct hash_prefix *hash_prefix = ictx->hash_prefix;
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);
	unsigned int pad_len;
	unsigned int ps_end;
	unsigned int len;
	u8 *in_buf;
	int err;

	if (!ctx->key_size)
		return -EINVAL;

	if (dlen < ctx->key_size)
		return -EOVERFLOW;

	if (rsassa_pkcs1_invalid_hash_len(slen, hash_prefix))
		return -EINVAL;

	if (slen + hash_prefix->size > ctx->key_size - 11)
		return -EOVERFLOW;

	pad_len = ctx->key_size - slen - hash_prefix->size - 1;

	/* RFC 8017 sec 8.2.1 step 1 - EMSA-PKCS1-v1_5 encoding generation */
	in_buf = dst;
	memmove(in_buf + pad_len + hash_prefix->size, src, slen);
	memcpy(in_buf + pad_len, hash_prefix->data, hash_prefix->size);

	ps_end = pad_len - 1;
	in_buf[0] = 0x01;
	memset(in_buf + 1, 0xff, ps_end - 1);
	in_buf[ps_end] = 0x00;


	/* RFC 8017 sec 8.2.1 step 2 - RSA signature */
	err = crypto_akcipher_sync_decrypt(ctx->child, in_buf,
					   ctx->key_size - 1, in_buf,
					   ctx->key_size);
	if (err < 0)
		return err;

	len = err;
	pad_len = ctx->key_size - len;

	/* Four billion to one */
	if (unlikely(pad_len)) {
		memmove(dst + pad_len, dst, len);
		memset(dst, 0, pad_len);
	}

	return 0;
}

static int rsassa_pkcs1_verify(struct crypto_sig *tfm,
			       const void *src, unsigned int slen,
			       const void *digest, unsigned int dlen)
{
	struct sig_instance *inst = sig_alg_instance(tfm);
	struct rsassa_pkcs1_inst_ctx *ictx = sig_instance_ctx(inst);
	const struct hash_prefix *hash_prefix = ictx->hash_prefix;
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);
	unsigned int child_reqsize = crypto_akcipher_reqsize(ctx->child);
	struct akcipher_request *child_req __free(kfree_sensitive) = NULL;
	struct crypto_wait cwait;
	struct scatterlist sg;
	unsigned int dst_len;
	unsigned int pos;
	u8 *out_buf;
	int err;

	/* RFC 8017 sec 8.2.2 step 1 - length checking */
	if (!ctx->key_size ||
	    slen != ctx->key_size ||
	    rsassa_pkcs1_invalid_hash_len(dlen, hash_prefix))
		return -EINVAL;

	/* RFC 8017 sec 8.2.2 step 2 - RSA verification */
	child_req = kmalloc(sizeof(*child_req) + child_reqsize + ctx->key_size,
			    GFP_KERNEL);
	if (!child_req)
		return -ENOMEM;

	out_buf = (u8 *)(child_req + 1) + child_reqsize;
	memcpy(out_buf, src, slen);

	crypto_init_wait(&cwait);
	sg_init_one(&sg, out_buf, slen);
	akcipher_request_set_tfm(child_req, ctx->child);
	akcipher_request_set_crypt(child_req, &sg, &sg, slen, slen);
	akcipher_request_set_callback(child_req, CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &cwait);

	err = crypto_akcipher_encrypt(child_req);
	err = crypto_wait_req(err, &cwait);
	if (err)
		return err;

	/* RFC 8017 sec 8.2.2 step 3 - EMSA-PKCS1-v1_5 encoding verification */
	dst_len = child_req->dst_len;
	if (dst_len < ctx->key_size - 1)
		return -EINVAL;

	if (dst_len == ctx->key_size) {
		if (out_buf[0] != 0x00)
			/* Encrypted value had no leading 0 byte */
			return -EINVAL;

		dst_len--;
		out_buf++;
	}

	if (out_buf[0] != 0x01)
		return -EBADMSG;

	for (pos = 1; pos < dst_len; pos++)
		if (out_buf[pos] != 0xff)
			break;

	if (pos < 9 || pos == dst_len || out_buf[pos] != 0x00)
		return -EBADMSG;
	pos++;

	if (hash_prefix->size > dst_len - pos)
		return -EBADMSG;
	if (crypto_memneq(out_buf + pos, hash_prefix->data, hash_prefix->size))
		return -EBADMSG;
	pos += hash_prefix->size;

	/* RFC 8017 sec 8.2.2 step 4 - comparison of digest with out_buf */
	if (dlen != dst_len - pos)
		return -EKEYREJECTED;
	if (memcmp(digest, out_buf + pos, dlen) != 0)
		return -EKEYREJECTED;

	return 0;
}

static unsigned int rsassa_pkcs1_key_size(struct crypto_sig *tfm)
{
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);

	return ctx->key_size;
}

static int rsassa_pkcs1_set_pub_key(struct crypto_sig *tfm,
				    const void *key, unsigned int keylen)
{
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);

	return rsa_set_key(ctx->child, &ctx->key_size, RSA_PUB, key, keylen);
}

static int rsassa_pkcs1_set_priv_key(struct crypto_sig *tfm,
				     const void *key, unsigned int keylen)
{
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);

	return rsa_set_key(ctx->child, &ctx->key_size, RSA_PRIV, key, keylen);
}

static int rsassa_pkcs1_init_tfm(struct crypto_sig *tfm)
{
	struct sig_instance *inst = sig_alg_instance(tfm);
	struct rsassa_pkcs1_inst_ctx *ictx = sig_instance_ctx(inst);
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);
	struct crypto_akcipher *child_tfm;

	child_tfm = crypto_spawn_akcipher(&ictx->spawn);
	if (IS_ERR(child_tfm))
		return PTR_ERR(child_tfm);

	ctx->child = child_tfm;

	return 0;
}

static void rsassa_pkcs1_exit_tfm(struct crypto_sig *tfm)
{
	struct rsassa_pkcs1_ctx *ctx = crypto_sig_ctx(tfm);

	crypto_free_akcipher(ctx->child);
}

static void rsassa_pkcs1_free(struct sig_instance *inst)
{
	struct rsassa_pkcs1_inst_ctx *ctx = sig_instance_ctx(inst);
	struct crypto_akcipher_spawn *spawn = &ctx->spawn;

	crypto_drop_akcipher(spawn);
	kfree(inst);
}

static int rsassa_pkcs1_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct rsassa_pkcs1_inst_ctx *ctx;
	struct akcipher_alg *rsa_alg;
	struct sig_instance *inst;
	const char *hash_name;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SIG, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = sig_instance_ctx(inst);

	err = crypto_grab_akcipher(&ctx->spawn, sig_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	rsa_alg = crypto_spawn_akcipher_alg(&ctx->spawn);

	if (strcmp(rsa_alg->base.cra_name, "rsa") != 0) {
		err = -EINVAL;
		goto err_free_inst;
	}

	hash_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(hash_name)) {
		err = PTR_ERR(hash_name);
		goto err_free_inst;
	}

	ctx->hash_prefix = rsassa_pkcs1_find_hash_prefix(hash_name);
	if (!ctx->hash_prefix) {
		err = -EINVAL;
		goto err_free_inst;
	}

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "pkcs1(%s,%s)", rsa_alg->base.cra_name,
		     hash_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "pkcs1(%s,%s)", rsa_alg->base.cra_driver_name,
		     hash_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = rsa_alg->base.cra_priority;
	inst->alg.base.cra_ctxsize = sizeof(struct rsassa_pkcs1_ctx);

	inst->alg.init = rsassa_pkcs1_init_tfm;
	inst->alg.exit = rsassa_pkcs1_exit_tfm;

	inst->alg.sign = rsassa_pkcs1_sign;
	inst->alg.verify = rsassa_pkcs1_verify;
	inst->alg.key_size = rsassa_pkcs1_key_size;
	inst->alg.set_pub_key = rsassa_pkcs1_set_pub_key;
	inst->alg.set_priv_key = rsassa_pkcs1_set_priv_key;

	inst->free = rsassa_pkcs1_free;

	err = sig_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		rsassa_pkcs1_free(inst);
	}
	return err;
}

struct crypto_template rsassa_pkcs1_tmpl = {
	.name = "pkcs1",
	.create = rsassa_pkcs1_create,
	.module = THIS_MODULE,
};

MODULE_ALIAS_CRYPTO("pkcs1");
