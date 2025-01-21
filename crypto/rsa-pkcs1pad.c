// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RSA padding templates.
 *
 * Copyright (c) 2015  Intel Corporation
 */

#include <crypto/algapi.h>
#include <crypto/akcipher.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/scatterlist.h>

struct pkcs1pad_ctx {
	struct crypto_akcipher *child;
	unsigned int key_size;
};

struct pkcs1pad_inst_ctx {
	struct crypto_akcipher_spawn spawn;
};

struct pkcs1pad_request {
	struct scatterlist in_sg[2], out_sg[1];
	uint8_t *in_buf, *out_buf;
	struct akcipher_request child_req;
};

static int pkcs1pad_set_pub_key(struct crypto_akcipher *tfm, const void *key,
		unsigned int keylen)
{
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);

	return rsa_set_key(ctx->child, &ctx->key_size, RSA_PUB, key, keylen);
}

static int pkcs1pad_set_priv_key(struct crypto_akcipher *tfm, const void *key,
		unsigned int keylen)
{
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);

	return rsa_set_key(ctx->child, &ctx->key_size, RSA_PRIV, key, keylen);
}

static unsigned int pkcs1pad_get_max_size(struct crypto_akcipher *tfm)
{
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);

	/*
	 * The maximum destination buffer size for the encrypt operation
	 * will be the same as for RSA, even though it's smaller for
	 * decrypt.
	 */

	return ctx->key_size;
}

static void pkcs1pad_sg_set_buf(struct scatterlist *sg, void *buf, size_t len,
		struct scatterlist *next)
{
	int nsegs = next ? 2 : 1;

	sg_init_table(sg, nsegs);
	sg_set_buf(sg, buf, len);

	if (next)
		sg_chain(sg, nsegs, next);
}

static int pkcs1pad_encrypt_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct pkcs1pad_request *req_ctx = akcipher_request_ctx(req);
	unsigned int pad_len;
	unsigned int len;
	u8 *out_buf;

	if (err)
		goto out;

	len = req_ctx->child_req.dst_len;
	pad_len = ctx->key_size - len;

	/* Four billion to one */
	if (likely(!pad_len))
		goto out;

	out_buf = kzalloc(ctx->key_size, GFP_ATOMIC);
	err = -ENOMEM;
	if (!out_buf)
		goto out;

	sg_copy_to_buffer(req->dst, sg_nents_for_len(req->dst, len),
			  out_buf + pad_len, len);
	sg_copy_from_buffer(req->dst,
			    sg_nents_for_len(req->dst, ctx->key_size),
			    out_buf, ctx->key_size);
	kfree_sensitive(out_buf);

out:
	req->dst_len = ctx->key_size;

	kfree(req_ctx->in_buf);

	return err;
}

static void pkcs1pad_encrypt_complete_cb(void *data, int err)
{
	struct akcipher_request *req = data;

	if (err == -EINPROGRESS)
		goto out;

	err = pkcs1pad_encrypt_complete(req, err);

out:
	akcipher_request_complete(req, err);
}

static int pkcs1pad_encrypt(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct pkcs1pad_request *req_ctx = akcipher_request_ctx(req);
	int err;
	unsigned int i, ps_end;

	if (!ctx->key_size)
		return -EINVAL;

	if (req->src_len > ctx->key_size - 11)
		return -EOVERFLOW;

	if (req->dst_len < ctx->key_size) {
		req->dst_len = ctx->key_size;
		return -EOVERFLOW;
	}

	req_ctx->in_buf = kmalloc(ctx->key_size - 1 - req->src_len,
				  GFP_KERNEL);
	if (!req_ctx->in_buf)
		return -ENOMEM;

	ps_end = ctx->key_size - req->src_len - 2;
	req_ctx->in_buf[0] = 0x02;
	for (i = 1; i < ps_end; i++)
		req_ctx->in_buf[i] = get_random_u32_inclusive(1, 255);
	req_ctx->in_buf[ps_end] = 0x00;

	pkcs1pad_sg_set_buf(req_ctx->in_sg, req_ctx->in_buf,
			ctx->key_size - 1 - req->src_len, req->src);

	akcipher_request_set_tfm(&req_ctx->child_req, ctx->child);
	akcipher_request_set_callback(&req_ctx->child_req, req->base.flags,
			pkcs1pad_encrypt_complete_cb, req);

	/* Reuse output buffer */
	akcipher_request_set_crypt(&req_ctx->child_req, req_ctx->in_sg,
				   req->dst, ctx->key_size - 1, req->dst_len);

	err = crypto_akcipher_encrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return pkcs1pad_encrypt_complete(req, err);

	return err;
}

static int pkcs1pad_decrypt_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct pkcs1pad_request *req_ctx = akcipher_request_ctx(req);
	unsigned int dst_len;
	unsigned int pos;
	u8 *out_buf;

	if (err)
		goto done;

	err = -EINVAL;
	dst_len = req_ctx->child_req.dst_len;
	if (dst_len < ctx->key_size - 1)
		goto done;

	out_buf = req_ctx->out_buf;
	if (dst_len == ctx->key_size) {
		if (out_buf[0] != 0x00)
			/* Decrypted value had no leading 0 byte */
			goto done;

		dst_len--;
		out_buf++;
	}

	if (out_buf[0] != 0x02)
		goto done;

	for (pos = 1; pos < dst_len; pos++)
		if (out_buf[pos] == 0x00)
			break;
	if (pos < 9 || pos == dst_len)
		goto done;
	pos++;

	err = 0;

	if (req->dst_len < dst_len - pos)
		err = -EOVERFLOW;
	req->dst_len = dst_len - pos;

	if (!err)
		sg_copy_from_buffer(req->dst,
				sg_nents_for_len(req->dst, req->dst_len),
				out_buf + pos, req->dst_len);

done:
	kfree_sensitive(req_ctx->out_buf);

	return err;
}

static void pkcs1pad_decrypt_complete_cb(void *data, int err)
{
	struct akcipher_request *req = data;

	if (err == -EINPROGRESS)
		goto out;

	err = pkcs1pad_decrypt_complete(req, err);

out:
	akcipher_request_complete(req, err);
}

static int pkcs1pad_decrypt(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct pkcs1pad_request *req_ctx = akcipher_request_ctx(req);
	int err;

	if (!ctx->key_size || req->src_len != ctx->key_size)
		return -EINVAL;

	req_ctx->out_buf = kmalloc(ctx->key_size, GFP_KERNEL);
	if (!req_ctx->out_buf)
		return -ENOMEM;

	pkcs1pad_sg_set_buf(req_ctx->out_sg, req_ctx->out_buf,
			    ctx->key_size, NULL);

	akcipher_request_set_tfm(&req_ctx->child_req, ctx->child);
	akcipher_request_set_callback(&req_ctx->child_req, req->base.flags,
			pkcs1pad_decrypt_complete_cb, req);

	/* Reuse input buffer, output to a new buffer */
	akcipher_request_set_crypt(&req_ctx->child_req, req->src,
				   req_ctx->out_sg, req->src_len,
				   ctx->key_size);

	err = crypto_akcipher_decrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return pkcs1pad_decrypt_complete(req, err);

	return err;
}

static int pkcs1pad_init_tfm(struct crypto_akcipher *tfm)
{
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct pkcs1pad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct crypto_akcipher *child_tfm;

	child_tfm = crypto_spawn_akcipher(&ictx->spawn);
	if (IS_ERR(child_tfm))
		return PTR_ERR(child_tfm);

	ctx->child = child_tfm;

	akcipher_set_reqsize(tfm, sizeof(struct pkcs1pad_request) +
				  crypto_akcipher_reqsize(child_tfm));

	return 0;
}

static void pkcs1pad_exit_tfm(struct crypto_akcipher *tfm)
{
	struct pkcs1pad_ctx *ctx = akcipher_tfm_ctx(tfm);

	crypto_free_akcipher(ctx->child);
}

static void pkcs1pad_free(struct akcipher_instance *inst)
{
	struct pkcs1pad_inst_ctx *ctx = akcipher_instance_ctx(inst);
	struct crypto_akcipher_spawn *spawn = &ctx->spawn;

	crypto_drop_akcipher(spawn);
	kfree(inst);
}

static int pkcs1pad_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	u32 mask;
	struct akcipher_instance *inst;
	struct pkcs1pad_inst_ctx *ctx;
	struct akcipher_alg *rsa_alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AKCIPHER, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = akcipher_instance_ctx(inst);

	err = crypto_grab_akcipher(&ctx->spawn, akcipher_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	rsa_alg = crypto_spawn_akcipher_alg(&ctx->spawn);

	if (strcmp(rsa_alg->base.cra_name, "rsa") != 0) {
		err = -EINVAL;
		goto err_free_inst;
	}

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name,
		     CRYPTO_MAX_ALG_NAME, "pkcs1pad(%s)",
		     rsa_alg->base.cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	if (snprintf(inst->alg.base.cra_driver_name,
		     CRYPTO_MAX_ALG_NAME, "pkcs1pad(%s)",
		     rsa_alg->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = rsa_alg->base.cra_priority;
	inst->alg.base.cra_ctxsize = sizeof(struct pkcs1pad_ctx);

	inst->alg.init = pkcs1pad_init_tfm;
	inst->alg.exit = pkcs1pad_exit_tfm;

	inst->alg.encrypt = pkcs1pad_encrypt;
	inst->alg.decrypt = pkcs1pad_decrypt;
	inst->alg.set_pub_key = pkcs1pad_set_pub_key;
	inst->alg.set_priv_key = pkcs1pad_set_priv_key;
	inst->alg.max_size = pkcs1pad_get_max_size;

	inst->free = pkcs1pad_free;

	err = akcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		pkcs1pad_free(inst);
	}
	return err;
}

struct crypto_template rsa_pkcs1pad_tmpl = {
	.name = "pkcs1pad",
	.create = pkcs1pad_create,
	.module = THIS_MODULE,
};

MODULE_ALIAS_CRYPTO("pkcs1pad");
