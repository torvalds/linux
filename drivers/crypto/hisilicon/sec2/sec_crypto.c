// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/sha.h>
#include <crypto/skcipher.h>
#include <crypto/xts.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>

#include "sec.h"
#include "sec_crypto.h"

#define SEC_PRIORITY		4001
#define SEC_XTS_MIN_KEY_SIZE	(2 * AES_MIN_KEY_SIZE)
#define SEC_XTS_MAX_KEY_SIZE	(2 * AES_MAX_KEY_SIZE)
#define SEC_DES3_2KEY_SIZE	(2 * DES_KEY_SIZE)
#define SEC_DES3_3KEY_SIZE	(3 * DES_KEY_SIZE)

/* SEC sqe(bd) bit operational relative MACRO */
#define SEC_DE_OFFSET		1
#define SEC_CIPHER_OFFSET	4
#define SEC_SCENE_OFFSET	3
#define SEC_DST_SGL_OFFSET	2
#define SEC_SRC_SGL_OFFSET	7
#define SEC_CKEY_OFFSET		9
#define SEC_CMODE_OFFSET	12
#define SEC_AKEY_OFFSET         5
#define SEC_AEAD_ALG_OFFSET     11
#define SEC_AUTH_OFFSET		6

#define SEC_FLAG_OFFSET		7
#define SEC_FLAG_MASK		0x0780
#define SEC_TYPE_MASK		0x0F
#define SEC_DONE_MASK		0x0001

#define SEC_TOTAL_IV_SZ		(SEC_IV_SIZE * QM_Q_DEPTH)
#define SEC_SGL_SGE_NR		128
#define SEC_CTX_DEV(ctx)	(&(ctx)->sec->qm.pdev->dev)
#define SEC_CIPHER_AUTH		0xfe
#define SEC_AUTH_CIPHER		0x1
#define SEC_MAX_MAC_LEN		64
#define SEC_TOTAL_MAC_SZ	(SEC_MAX_MAC_LEN * QM_Q_DEPTH)
#define SEC_SQE_LEN_RATE	4
#define SEC_SQE_CFLAG		2
#define SEC_SQE_AEAD_FLAG	3
#define SEC_SQE_DONE		0x1

static atomic_t sec_active_devs;

/* Get an en/de-cipher queue cyclically to balance load over queues of TFM */
static inline int sec_alloc_queue_id(struct sec_ctx *ctx, struct sec_req *req)
{
	if (req->c_req.encrypt)
		return (u32)atomic_inc_return(&ctx->enc_qcyclic) %
				 ctx->hlf_q_num;

	return (u32)atomic_inc_return(&ctx->dec_qcyclic) % ctx->hlf_q_num +
				 ctx->hlf_q_num;
}

static inline void sec_free_queue_id(struct sec_ctx *ctx, struct sec_req *req)
{
	if (req->c_req.encrypt)
		atomic_dec(&ctx->enc_qcyclic);
	else
		atomic_dec(&ctx->dec_qcyclic);
}

static int sec_alloc_req_id(struct sec_req *req, struct sec_qp_ctx *qp_ctx)
{
	int req_id;

	mutex_lock(&qp_ctx->req_lock);

	req_id = idr_alloc_cyclic(&qp_ctx->req_idr, NULL,
				  0, QM_Q_DEPTH, GFP_ATOMIC);
	mutex_unlock(&qp_ctx->req_lock);
	if (unlikely(req_id < 0)) {
		dev_err(SEC_CTX_DEV(req->ctx), "alloc req id fail!\n");
		return req_id;
	}

	req->qp_ctx = qp_ctx;
	qp_ctx->req_list[req_id] = req;
	return req_id;
}

static void sec_free_req_id(struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	int req_id = req->req_id;

	if (unlikely(req_id < 0 || req_id >= QM_Q_DEPTH)) {
		dev_err(SEC_CTX_DEV(req->ctx), "free request id invalid!\n");
		return;
	}

	qp_ctx->req_list[req_id] = NULL;
	req->qp_ctx = NULL;

	mutex_lock(&qp_ctx->req_lock);
	idr_remove(&qp_ctx->req_idr, req_id);
	mutex_unlock(&qp_ctx->req_lock);
}

static int sec_aead_verify(struct sec_req *req, struct sec_qp_ctx *qp_ctx)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(aead_req);
	u8 *mac_out = qp_ctx->res[req->req_id].out_mac;
	size_t authsize = crypto_aead_authsize(tfm);
	u8 *mac = mac_out + SEC_MAX_MAC_LEN;
	struct scatterlist *sgl = aead_req->src;
	size_t sz;

	sz = sg_pcopy_to_buffer(sgl, sg_nents(sgl), mac, authsize,
				aead_req->cryptlen + aead_req->assoclen -
				authsize);
	if (unlikely(sz != authsize || memcmp(mac_out, mac, sz))) {
		dev_err(SEC_CTX_DEV(req->ctx), "aead verify failure!\n");
		return -EBADMSG;
	}

	return 0;
}

static void sec_req_cb(struct hisi_qp *qp, void *resp)
{
	struct sec_qp_ctx *qp_ctx = qp->qp_ctx;
	struct sec_sqe *bd = resp;
	struct sec_ctx *ctx;
	struct sec_req *req;
	u16 done, flag;
	int err = 0;
	u8 type;

	type = bd->type_cipher_auth & SEC_TYPE_MASK;
	if (unlikely(type != SEC_BD_TYPE2)) {
		pr_err("err bd type [%d]\n", type);
		return;
	}

	req = qp_ctx->req_list[le16_to_cpu(bd->type2.tag)];
	req->err_type = bd->type2.error_type;
	ctx = req->ctx;
	done = le16_to_cpu(bd->type2.done_flag) & SEC_DONE_MASK;
	flag = (le16_to_cpu(bd->type2.done_flag) &
		SEC_FLAG_MASK) >> SEC_FLAG_OFFSET;
	if (unlikely(req->err_type || done != SEC_SQE_DONE ||
	    (ctx->alg_type == SEC_SKCIPHER && flag != SEC_SQE_CFLAG) ||
	    (ctx->alg_type == SEC_AEAD && flag != SEC_SQE_AEAD_FLAG))) {
		dev_err(SEC_CTX_DEV(ctx),
			"err_type[%d],done[%d],flag[%d]\n",
			req->err_type, done, flag);
		err = -EIO;
	}

	if (ctx->alg_type == SEC_AEAD && !req->c_req.encrypt)
		err = sec_aead_verify(req, qp_ctx);

	atomic64_inc(&ctx->sec->debug.dfx.recv_cnt);

	ctx->req_op->buf_unmap(ctx, req);

	ctx->req_op->callback(ctx, req, err);
}

static int sec_bd_send(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	int ret;

	mutex_lock(&qp_ctx->req_lock);
	ret = hisi_qp_send(qp_ctx->qp, &req->sec_sqe);
	mutex_unlock(&qp_ctx->req_lock);
	atomic64_inc(&ctx->sec->debug.dfx.send_cnt);

	if (unlikely(ret == -EBUSY))
		return -ENOBUFS;

	if (!ret) {
		if (req->fake_busy)
			ret = -EBUSY;
		else
			ret = -EINPROGRESS;
	}

	return ret;
}

/* Get DMA memory resources */
static int sec_alloc_civ_resource(struct device *dev, struct sec_alg_res *res)
{
	int i;

	res->c_ivin = dma_alloc_coherent(dev, SEC_TOTAL_IV_SZ,
					 &res->c_ivin_dma, GFP_KERNEL);
	if (!res->c_ivin)
		return -ENOMEM;

	for (i = 1; i < QM_Q_DEPTH; i++) {
		res[i].c_ivin_dma = res->c_ivin_dma + i * SEC_IV_SIZE;
		res[i].c_ivin = res->c_ivin + i * SEC_IV_SIZE;
	}

	return 0;
}

static void sec_free_civ_resource(struct device *dev, struct sec_alg_res *res)
{
	if (res->c_ivin)
		dma_free_coherent(dev, SEC_TOTAL_IV_SZ,
				  res->c_ivin, res->c_ivin_dma);
}

static int sec_alloc_mac_resource(struct device *dev, struct sec_alg_res *res)
{
	int i;

	res->out_mac = dma_alloc_coherent(dev, SEC_TOTAL_MAC_SZ << 1,
					  &res->out_mac_dma, GFP_KERNEL);
	if (!res->out_mac)
		return -ENOMEM;

	for (i = 1; i < QM_Q_DEPTH; i++) {
		res[i].out_mac_dma = res->out_mac_dma +
				     i * (SEC_MAX_MAC_LEN << 1);
		res[i].out_mac = res->out_mac + i * (SEC_MAX_MAC_LEN << 1);
	}

	return 0;
}

static void sec_free_mac_resource(struct device *dev, struct sec_alg_res *res)
{
	if (res->out_mac)
		dma_free_coherent(dev, SEC_TOTAL_MAC_SZ << 1,
				  res->out_mac, res->out_mac_dma);
}

static int sec_alg_resource_alloc(struct sec_ctx *ctx,
				  struct sec_qp_ctx *qp_ctx)
{
	struct device *dev = SEC_CTX_DEV(ctx);
	struct sec_alg_res *res = qp_ctx->res;
	int ret;

	ret = sec_alloc_civ_resource(dev, res);
	if (ret)
		return ret;

	if (ctx->alg_type == SEC_AEAD) {
		ret = sec_alloc_mac_resource(dev, res);
		if (ret)
			goto get_fail;
	}

	return 0;
get_fail:
	sec_free_civ_resource(dev, res);

	return ret;
}

static void sec_alg_resource_free(struct sec_ctx *ctx,
				  struct sec_qp_ctx *qp_ctx)
{
	struct device *dev = SEC_CTX_DEV(ctx);

	sec_free_civ_resource(dev, qp_ctx->res);

	if (ctx->alg_type == SEC_AEAD)
		sec_free_mac_resource(dev, qp_ctx->res);
}

static int sec_create_qp_ctx(struct hisi_qm *qm, struct sec_ctx *ctx,
			     int qp_ctx_id, int alg_type)
{
	struct device *dev = SEC_CTX_DEV(ctx);
	struct sec_qp_ctx *qp_ctx;
	struct hisi_qp *qp;
	int ret = -ENOMEM;

	qp = hisi_qm_create_qp(qm, alg_type);
	if (IS_ERR(qp))
		return PTR_ERR(qp);

	qp_ctx = &ctx->qp_ctx[qp_ctx_id];
	qp->req_type = 0;
	qp->qp_ctx = qp_ctx;
	qp->req_cb = sec_req_cb;
	qp_ctx->qp = qp;
	qp_ctx->ctx = ctx;

	mutex_init(&qp_ctx->req_lock);
	atomic_set(&qp_ctx->pending_reqs, 0);
	idr_init(&qp_ctx->req_idr);

	qp_ctx->c_in_pool = hisi_acc_create_sgl_pool(dev, QM_Q_DEPTH,
						     SEC_SGL_SGE_NR);
	if (IS_ERR(qp_ctx->c_in_pool)) {
		dev_err(dev, "fail to create sgl pool for input!\n");
		goto err_destroy_idr;
	}

	qp_ctx->c_out_pool = hisi_acc_create_sgl_pool(dev, QM_Q_DEPTH,
						      SEC_SGL_SGE_NR);
	if (IS_ERR(qp_ctx->c_out_pool)) {
		dev_err(dev, "fail to create sgl pool for output!\n");
		goto err_free_c_in_pool;
	}

	ret = sec_alg_resource_alloc(ctx, qp_ctx);
	if (ret)
		goto err_free_c_out_pool;

	ret = hisi_qm_start_qp(qp, 0);
	if (ret < 0)
		goto err_queue_free;

	return 0;

err_queue_free:
	sec_alg_resource_free(ctx, qp_ctx);
err_free_c_out_pool:
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_out_pool);
err_free_c_in_pool:
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_in_pool);
err_destroy_idr:
	idr_destroy(&qp_ctx->req_idr);
	hisi_qm_release_qp(qp);

	return ret;
}

static void sec_release_qp_ctx(struct sec_ctx *ctx,
			       struct sec_qp_ctx *qp_ctx)
{
	struct device *dev = SEC_CTX_DEV(ctx);

	hisi_qm_stop_qp(qp_ctx->qp);
	sec_alg_resource_free(ctx, qp_ctx);

	hisi_acc_free_sgl_pool(dev, qp_ctx->c_out_pool);
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_in_pool);

	idr_destroy(&qp_ctx->req_idr);
	hisi_qm_release_qp(qp_ctx->qp);
}

static int sec_ctx_base_init(struct sec_ctx *ctx)
{
	struct sec_dev *sec;
	int i, ret;

	sec = sec_find_device(cpu_to_node(smp_processor_id()));
	if (!sec) {
		pr_err("Can not find proper Hisilicon SEC device!\n");
		return -ENODEV;
	}
	ctx->sec = sec;
	ctx->hlf_q_num = sec->ctx_q_num >> 1;

	/* Half of queue depth is taken as fake requests limit in the queue. */
	ctx->fake_req_limit = QM_Q_DEPTH >> 1;
	ctx->qp_ctx = kcalloc(sec->ctx_q_num, sizeof(struct sec_qp_ctx),
			      GFP_KERNEL);
	if (!ctx->qp_ctx)
		return -ENOMEM;

	for (i = 0; i < sec->ctx_q_num; i++) {
		ret = sec_create_qp_ctx(&sec->qm, ctx, i, 0);
		if (ret)
			goto err_sec_release_qp_ctx;
	}

	return 0;
err_sec_release_qp_ctx:
	for (i = i - 1; i >= 0; i--)
		sec_release_qp_ctx(ctx, &ctx->qp_ctx[i]);

	kfree(ctx->qp_ctx);
	return ret;
}

static void sec_ctx_base_uninit(struct sec_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->sec->ctx_q_num; i++)
		sec_release_qp_ctx(ctx, &ctx->qp_ctx[i]);

	kfree(ctx->qp_ctx);
}

static int sec_cipher_init(struct sec_ctx *ctx)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;

	c_ctx->c_key = dma_alloc_coherent(SEC_CTX_DEV(ctx), SEC_MAX_KEY_SIZE,
					  &c_ctx->c_key_dma, GFP_KERNEL);
	if (!c_ctx->c_key)
		return -ENOMEM;

	return 0;
}

static void sec_cipher_uninit(struct sec_ctx *ctx)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;

	memzero_explicit(c_ctx->c_key, SEC_MAX_KEY_SIZE);
	dma_free_coherent(SEC_CTX_DEV(ctx), SEC_MAX_KEY_SIZE,
			  c_ctx->c_key, c_ctx->c_key_dma);
}

static int sec_auth_init(struct sec_ctx *ctx)
{
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;

	a_ctx->a_key = dma_alloc_coherent(SEC_CTX_DEV(ctx), SEC_MAX_KEY_SIZE,
					  &a_ctx->a_key_dma, GFP_KERNEL);
	if (!a_ctx->a_key)
		return -ENOMEM;

	return 0;
}

static void sec_auth_uninit(struct sec_ctx *ctx)
{
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;

	memzero_explicit(a_ctx->a_key, SEC_MAX_KEY_SIZE);
	dma_free_coherent(SEC_CTX_DEV(ctx), SEC_MAX_KEY_SIZE,
			  a_ctx->a_key, a_ctx->a_key_dma);
}

static int sec_skcipher_init(struct crypto_skcipher *tfm)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ctx = crypto_skcipher_ctx(tfm);
	ctx->alg_type = SEC_SKCIPHER;
	crypto_skcipher_set_reqsize(tfm, sizeof(struct sec_req));
	ctx->c_ctx.ivsize = crypto_skcipher_ivsize(tfm);
	if (ctx->c_ctx.ivsize > SEC_IV_SIZE) {
		dev_err(SEC_CTX_DEV(ctx), "get error skcipher iv size!\n");
		return -EINVAL;
	}

	ret = sec_ctx_base_init(ctx);
	if (ret)
		return ret;

	ret = sec_cipher_init(ctx);
	if (ret)
		goto err_cipher_init;

	return 0;
err_cipher_init:
	sec_ctx_base_uninit(ctx);

	return ret;
}

static void sec_skcipher_uninit(struct crypto_skcipher *tfm)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	sec_cipher_uninit(ctx);
	sec_ctx_base_uninit(ctx);
}

static int sec_skcipher_3des_setkey(struct sec_cipher_ctx *c_ctx,
				    const u32 keylen,
				    const enum sec_cmode c_mode)
{
	switch (keylen) {
	case SEC_DES3_2KEY_SIZE:
		c_ctx->c_key_len = SEC_CKEY_3DES_2KEY;
		break;
	case SEC_DES3_3KEY_SIZE:
		c_ctx->c_key_len = SEC_CKEY_3DES_3KEY;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sec_skcipher_aes_sm4_setkey(struct sec_cipher_ctx *c_ctx,
				       const u32 keylen,
				       const enum sec_cmode c_mode)
{
	if (c_mode == SEC_CMODE_XTS) {
		switch (keylen) {
		case SEC_XTS_MIN_KEY_SIZE:
			c_ctx->c_key_len = SEC_CKEY_128BIT;
			break;
		case SEC_XTS_MAX_KEY_SIZE:
			c_ctx->c_key_len = SEC_CKEY_256BIT;
			break;
		default:
			pr_err("hisi_sec2: xts mode key error!\n");
			return -EINVAL;
		}
	} else {
		switch (keylen) {
		case AES_KEYSIZE_128:
			c_ctx->c_key_len = SEC_CKEY_128BIT;
			break;
		case AES_KEYSIZE_192:
			c_ctx->c_key_len = SEC_CKEY_192BIT;
			break;
		case AES_KEYSIZE_256:
			c_ctx->c_key_len = SEC_CKEY_256BIT;
			break;
		default:
			pr_err("hisi_sec2: aes key error!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int sec_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			       const u32 keylen, const enum sec_calg c_alg,
			       const enum sec_cmode c_mode)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	int ret;

	if (c_mode == SEC_CMODE_XTS) {
		ret = xts_verify_key(tfm, key, keylen);
		if (ret) {
			dev_err(SEC_CTX_DEV(ctx), "xts mode key err!\n");
			return ret;
		}
	}

	c_ctx->c_alg  = c_alg;
	c_ctx->c_mode = c_mode;

	switch (c_alg) {
	case SEC_CALG_3DES:
		ret = sec_skcipher_3des_setkey(c_ctx, keylen, c_mode);
		break;
	case SEC_CALG_AES:
	case SEC_CALG_SM4:
		ret = sec_skcipher_aes_sm4_setkey(c_ctx, keylen, c_mode);
		break;
	default:
		return -EINVAL;
	}

	if (ret) {
		dev_err(SEC_CTX_DEV(ctx), "set sec key err!\n");
		return ret;
	}

	memcpy(c_ctx->c_key, key, keylen);

	return 0;
}

#define GEN_SEC_SETKEY_FUNC(name, c_alg, c_mode)			\
static int sec_setkey_##name(struct crypto_skcipher *tfm, const u8 *key,\
	u32 keylen)							\
{									\
	return sec_skcipher_setkey(tfm, key, keylen, c_alg, c_mode);	\
}

GEN_SEC_SETKEY_FUNC(aes_ecb, SEC_CALG_AES, SEC_CMODE_ECB)
GEN_SEC_SETKEY_FUNC(aes_cbc, SEC_CALG_AES, SEC_CMODE_CBC)
GEN_SEC_SETKEY_FUNC(aes_xts, SEC_CALG_AES, SEC_CMODE_XTS)

GEN_SEC_SETKEY_FUNC(3des_ecb, SEC_CALG_3DES, SEC_CMODE_ECB)
GEN_SEC_SETKEY_FUNC(3des_cbc, SEC_CALG_3DES, SEC_CMODE_CBC)

GEN_SEC_SETKEY_FUNC(sm4_xts, SEC_CALG_SM4, SEC_CMODE_XTS)
GEN_SEC_SETKEY_FUNC(sm4_cbc, SEC_CALG_SM4, SEC_CMODE_CBC)

static int sec_cipher_map(struct device *dev, struct sec_req *req,
			  struct scatterlist *src, struct scatterlist *dst)
{
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;

	c_req->c_in = hisi_acc_sg_buf_map_to_hw_sgl(dev, src,
						    qp_ctx->c_in_pool,
						    req->req_id,
						    &c_req->c_in_dma);

	if (IS_ERR(c_req->c_in)) {
		dev_err(dev, "fail to dma map input sgl buffers!\n");
		return PTR_ERR(c_req->c_in);
	}

	if (dst == src) {
		c_req->c_out = c_req->c_in;
		c_req->c_out_dma = c_req->c_in_dma;
	} else {
		c_req->c_out = hisi_acc_sg_buf_map_to_hw_sgl(dev, dst,
							     qp_ctx->c_out_pool,
							     req->req_id,
							     &c_req->c_out_dma);

		if (IS_ERR(c_req->c_out)) {
			dev_err(dev, "fail to dma map output sgl buffers!\n");
			hisi_acc_sg_buf_unmap(dev, src, c_req->c_in);
			return PTR_ERR(c_req->c_out);
		}
	}

	return 0;
}

static void sec_cipher_unmap(struct device *dev, struct sec_cipher_req *req,
			     struct scatterlist *src, struct scatterlist *dst)
{
	if (dst != src)
		hisi_acc_sg_buf_unmap(dev, src, req->c_in);

	hisi_acc_sg_buf_unmap(dev, dst, req->c_out);
}

static int sec_skcipher_sgl_map(struct sec_ctx *ctx, struct sec_req *req)
{
	struct skcipher_request *sq = req->c_req.sk_req;

	return sec_cipher_map(SEC_CTX_DEV(ctx), req, sq->src, sq->dst);
}

static void sec_skcipher_sgl_unmap(struct sec_ctx *ctx, struct sec_req *req)
{
	struct device *dev = SEC_CTX_DEV(ctx);
	struct sec_cipher_req *c_req = &req->c_req;
	struct skcipher_request *sk_req = c_req->sk_req;

	sec_cipher_unmap(dev, c_req, sk_req->src, sk_req->dst);
}

static int sec_aead_aes_set_key(struct sec_cipher_ctx *c_ctx,
				struct crypto_authenc_keys *keys)
{
	switch (keys->enckeylen) {
	case AES_KEYSIZE_128:
		c_ctx->c_key_len = SEC_CKEY_128BIT;
		break;
	case AES_KEYSIZE_192:
		c_ctx->c_key_len = SEC_CKEY_192BIT;
		break;
	case AES_KEYSIZE_256:
		c_ctx->c_key_len = SEC_CKEY_256BIT;
		break;
	default:
		pr_err("hisi_sec2: aead aes key error!\n");
		return -EINVAL;
	}
	memcpy(c_ctx->c_key, keys->enckey, keys->enckeylen);

	return 0;
}

static int sec_aead_auth_set_key(struct sec_auth_ctx *ctx,
				 struct crypto_authenc_keys *keys)
{
	struct crypto_shash *hash_tfm = ctx->hash_tfm;
	SHASH_DESC_ON_STACK(shash, hash_tfm);
	int blocksize, ret;

	if (!keys->authkeylen) {
		pr_err("hisi_sec2: aead auth key error!\n");
		return -EINVAL;
	}

	blocksize = crypto_shash_blocksize(hash_tfm);
	if (keys->authkeylen > blocksize) {
		ret = crypto_shash_digest(shash, keys->authkey,
					  keys->authkeylen, ctx->a_key);
		if (ret) {
			pr_err("hisi_sec2: aead auth digest error!\n");
			return -EINVAL;
		}
		ctx->a_key_len = blocksize;
	} else {
		memcpy(ctx->a_key, keys->authkey, keys->authkeylen);
		ctx->a_key_len = keys->authkeylen;
	}

	return 0;
}

static int sec_aead_setkey(struct crypto_aead *tfm, const u8 *key,
			   const u32 keylen, const enum sec_hash_alg a_alg,
			   const enum sec_calg c_alg,
			   const enum sec_mac_len mac_len,
			   const enum sec_cmode c_mode)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	struct crypto_authenc_keys keys;
	int ret;

	ctx->a_ctx.a_alg = a_alg;
	ctx->c_ctx.c_alg = c_alg;
	ctx->a_ctx.mac_len = mac_len;
	c_ctx->c_mode = c_mode;

	if (crypto_authenc_extractkeys(&keys, key, keylen))
		goto bad_key;

	ret = sec_aead_aes_set_key(c_ctx, &keys);
	if (ret) {
		dev_err(SEC_CTX_DEV(ctx), "set sec cipher key err!\n");
		goto bad_key;
	}

	ret = sec_aead_auth_set_key(&ctx->a_ctx, &keys);
	if (ret) {
		dev_err(SEC_CTX_DEV(ctx), "set sec auth key err!\n");
		goto bad_key;
	}

	return 0;
bad_key:
	memzero_explicit(&keys, sizeof(struct crypto_authenc_keys));

	return -EINVAL;
}


#define GEN_SEC_AEAD_SETKEY_FUNC(name, aalg, calg, maclen, cmode)	\
static int sec_setkey_##name(struct crypto_aead *tfm, const u8 *key,	\
	u32 keylen)							\
{									\
	return sec_aead_setkey(tfm, key, keylen, aalg, calg, maclen, cmode);\
}

GEN_SEC_AEAD_SETKEY_FUNC(aes_cbc_sha1, SEC_A_HMAC_SHA1,
			 SEC_CALG_AES, SEC_HMAC_SHA1_MAC, SEC_CMODE_CBC)
GEN_SEC_AEAD_SETKEY_FUNC(aes_cbc_sha256, SEC_A_HMAC_SHA256,
			 SEC_CALG_AES, SEC_HMAC_SHA256_MAC, SEC_CMODE_CBC)
GEN_SEC_AEAD_SETKEY_FUNC(aes_cbc_sha512, SEC_A_HMAC_SHA512,
			 SEC_CALG_AES, SEC_HMAC_SHA512_MAC, SEC_CMODE_CBC)

static int sec_aead_sgl_map(struct sec_ctx *ctx, struct sec_req *req)
{
	struct aead_request *aq = req->aead_req.aead_req;

	return sec_cipher_map(SEC_CTX_DEV(ctx), req, aq->src, aq->dst);
}

static void sec_aead_sgl_unmap(struct sec_ctx *ctx, struct sec_req *req)
{
	struct device *dev = SEC_CTX_DEV(ctx);
	struct sec_cipher_req *cq = &req->c_req;
	struct aead_request *aq = req->aead_req.aead_req;

	sec_cipher_unmap(dev, cq, aq->src, aq->dst);
}

static int sec_request_transfer(struct sec_ctx *ctx, struct sec_req *req)
{
	int ret;

	ret = ctx->req_op->buf_map(ctx, req);
	if (unlikely(ret))
		return ret;

	ctx->req_op->do_transfer(ctx, req);

	ret = ctx->req_op->bd_fill(ctx, req);
	if (unlikely(ret))
		goto unmap_req_buf;

	return ret;

unmap_req_buf:
	ctx->req_op->buf_unmap(ctx, req);

	return ret;
}

static void sec_request_untransfer(struct sec_ctx *ctx, struct sec_req *req)
{
	ctx->req_op->buf_unmap(ctx, req);
}

static void sec_skcipher_copy_iv(struct sec_ctx *ctx, struct sec_req *req)
{
	struct skcipher_request *sk_req = req->c_req.sk_req;
	u8 *c_ivin = req->qp_ctx->res[req->req_id].c_ivin;

	memcpy(c_ivin, sk_req->iv, ctx->c_ctx.ivsize);
}

static int sec_skcipher_bd_fill(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_sqe *sec_sqe = &req->sec_sqe;
	u8 scene, sa_type, da_type;
	u8 bd_type, cipher;
	u8 de = 0;

	memset(sec_sqe, 0, sizeof(struct sec_sqe));

	sec_sqe->type2.c_key_addr = cpu_to_le64(c_ctx->c_key_dma);
	sec_sqe->type2.c_ivin_addr =
		cpu_to_le64(req->qp_ctx->res[req->req_id].c_ivin_dma);
	sec_sqe->type2.data_src_addr = cpu_to_le64(c_req->c_in_dma);
	sec_sqe->type2.data_dst_addr = cpu_to_le64(c_req->c_out_dma);

	sec_sqe->type2.icvw_kmode |= cpu_to_le16(((u16)c_ctx->c_mode) <<
						SEC_CMODE_OFFSET);
	sec_sqe->type2.c_alg = c_ctx->c_alg;
	sec_sqe->type2.icvw_kmode |= cpu_to_le16(((u16)c_ctx->c_key_len) <<
						SEC_CKEY_OFFSET);

	bd_type = SEC_BD_TYPE2;
	if (c_req->encrypt)
		cipher = SEC_CIPHER_ENC << SEC_CIPHER_OFFSET;
	else
		cipher = SEC_CIPHER_DEC << SEC_CIPHER_OFFSET;
	sec_sqe->type_cipher_auth = bd_type | cipher;

	sa_type = SEC_SGL << SEC_SRC_SGL_OFFSET;
	scene = SEC_COMM_SCENE << SEC_SCENE_OFFSET;
	if (c_req->c_in_dma != c_req->c_out_dma)
		de = 0x1 << SEC_DE_OFFSET;

	sec_sqe->sds_sa_type = (de | scene | sa_type);

	/* Just set DST address type */
	da_type = SEC_SGL << SEC_DST_SGL_OFFSET;
	sec_sqe->sdm_addr_type |= da_type;

	sec_sqe->type2.clen_ivhlen |= cpu_to_le32(c_req->c_len);
	sec_sqe->type2.tag = cpu_to_le16((u16)req->req_id);

	return 0;
}

static void sec_update_iv(struct sec_req *req, enum sec_alg_type alg_type)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	struct skcipher_request *sk_req = req->c_req.sk_req;
	u32 iv_size = req->ctx->c_ctx.ivsize;
	struct scatterlist *sgl;
	unsigned int cryptlen;
	size_t sz;
	u8 *iv;

	if (req->c_req.encrypt)
		sgl = alg_type == SEC_SKCIPHER ? sk_req->dst : aead_req->dst;
	else
		sgl = alg_type == SEC_SKCIPHER ? sk_req->src : aead_req->src;

	if (alg_type == SEC_SKCIPHER) {
		iv = sk_req->iv;
		cryptlen = sk_req->cryptlen;
	} else {
		iv = aead_req->iv;
		cryptlen = aead_req->cryptlen;
	}

	sz = sg_pcopy_to_buffer(sgl, sg_nents(sgl), iv, iv_size,
				cryptlen - iv_size);
	if (unlikely(sz != iv_size))
		dev_err(SEC_CTX_DEV(req->ctx), "copy output iv error!\n");
}

static void sec_skcipher_callback(struct sec_ctx *ctx, struct sec_req *req,
				  int err)
{
	struct skcipher_request *sk_req = req->c_req.sk_req;
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;

	atomic_dec(&qp_ctx->pending_reqs);
	sec_free_req_id(req);

	/* IV output at encrypto of CBC mode */
	if (!err && ctx->c_ctx.c_mode == SEC_CMODE_CBC && req->c_req.encrypt)
		sec_update_iv(req, SEC_SKCIPHER);

	if (req->fake_busy)
		sk_req->base.complete(&sk_req->base, -EINPROGRESS);

	sk_req->base.complete(&sk_req->base, err);
}

static void sec_aead_copy_iv(struct sec_ctx *ctx, struct sec_req *req)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	u8 *c_ivin = req->qp_ctx->res[req->req_id].c_ivin;

	memcpy(c_ivin, aead_req->iv, ctx->c_ctx.ivsize);
}

static void sec_auth_bd_fill_ex(struct sec_auth_ctx *ctx, int dir,
			       struct sec_req *req, struct sec_sqe *sec_sqe)
{
	struct sec_aead_req *a_req = &req->aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	struct aead_request *aq = a_req->aead_req;

	sec_sqe->type2.a_key_addr = cpu_to_le64(ctx->a_key_dma);

	sec_sqe->type2.mac_key_alg =
			cpu_to_le32(ctx->mac_len / SEC_SQE_LEN_RATE);

	sec_sqe->type2.mac_key_alg |=
			cpu_to_le32((u32)((ctx->a_key_len) /
			SEC_SQE_LEN_RATE) << SEC_AKEY_OFFSET);

	sec_sqe->type2.mac_key_alg |=
			cpu_to_le32((u32)(ctx->a_alg) << SEC_AEAD_ALG_OFFSET);

	sec_sqe->type_cipher_auth |= SEC_AUTH_TYPE1 << SEC_AUTH_OFFSET;

	if (dir)
		sec_sqe->sds_sa_type &= SEC_CIPHER_AUTH;
	else
		sec_sqe->sds_sa_type |= SEC_AUTH_CIPHER;

	sec_sqe->type2.alen_ivllen = cpu_to_le32(c_req->c_len + aq->assoclen);

	sec_sqe->type2.cipher_src_offset = cpu_to_le16((u16)aq->assoclen);

	sec_sqe->type2.mac_addr =
		cpu_to_le64(req->qp_ctx->res[req->req_id].out_mac_dma);
}

static int sec_aead_bd_fill(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_auth_ctx *auth_ctx = &ctx->a_ctx;
	struct sec_sqe *sec_sqe = &req->sec_sqe;
	int ret;

	ret = sec_skcipher_bd_fill(ctx, req);
	if (unlikely(ret)) {
		dev_err(SEC_CTX_DEV(ctx), "skcipher bd fill is error!\n");
		return ret;
	}

	sec_auth_bd_fill_ex(auth_ctx, req->c_req.encrypt, req, sec_sqe);

	return 0;
}

static void sec_aead_callback(struct sec_ctx *c, struct sec_req *req, int err)
{
	struct aead_request *a_req = req->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(a_req);
	struct sec_cipher_req *c_req = &req->c_req;
	size_t authsize = crypto_aead_authsize(tfm);
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	size_t sz;

	atomic_dec(&qp_ctx->pending_reqs);

	if (!err && c->c_ctx.c_mode == SEC_CMODE_CBC && c_req->encrypt)
		sec_update_iv(req, SEC_AEAD);

	/* Copy output mac */
	if (!err && c_req->encrypt) {
		struct scatterlist *sgl = a_req->dst;

		sz = sg_pcopy_from_buffer(sgl, sg_nents(sgl),
					  qp_ctx->res[req->req_id].out_mac,
					  authsize, a_req->cryptlen +
					  a_req->assoclen);

		if (unlikely(sz != authsize)) {
			dev_err(SEC_CTX_DEV(req->ctx), "copy out mac err!\n");
			err = -EINVAL;
		}
	}

	sec_free_req_id(req);

	if (req->fake_busy)
		a_req->base.complete(&a_req->base, -EINPROGRESS);

	a_req->base.complete(&a_req->base, err);
}

static void sec_request_uninit(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;

	atomic_dec(&qp_ctx->pending_reqs);
	sec_free_req_id(req);
	sec_free_queue_id(ctx, req);
}

static int sec_request_init(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx;
	int queue_id;

	/* To load balance */
	queue_id = sec_alloc_queue_id(ctx, req);
	qp_ctx = &ctx->qp_ctx[queue_id];

	req->req_id = sec_alloc_req_id(req, qp_ctx);
	if (unlikely(req->req_id < 0)) {
		sec_free_queue_id(ctx, req);
		return req->req_id;
	}

	if (ctx->fake_req_limit <= atomic_inc_return(&qp_ctx->pending_reqs))
		req->fake_busy = true;
	else
		req->fake_busy = false;

	return 0;
}

static int sec_process(struct sec_ctx *ctx, struct sec_req *req)
{
	int ret;

	ret = sec_request_init(ctx, req);
	if (unlikely(ret))
		return ret;

	ret = sec_request_transfer(ctx, req);
	if (unlikely(ret))
		goto err_uninit_req;

	/* Output IV as decrypto */
	if (ctx->c_ctx.c_mode == SEC_CMODE_CBC && !req->c_req.encrypt)
		sec_update_iv(req, ctx->alg_type);

	ret = ctx->req_op->bd_send(ctx, req);
	if (unlikely(ret != -EBUSY && ret != -EINPROGRESS)) {
		dev_err_ratelimited(SEC_CTX_DEV(ctx), "send sec request failed!\n");
		goto err_send_req;
	}

	return ret;

err_send_req:
	/* As failing, restore the IV from user */
	if (ctx->c_ctx.c_mode == SEC_CMODE_CBC && !req->c_req.encrypt) {
		if (ctx->alg_type == SEC_SKCIPHER)
			memcpy(req->c_req.sk_req->iv,
			       req->qp_ctx->res[req->req_id].c_ivin,
			       ctx->c_ctx.ivsize);
		else
			memcpy(req->aead_req.aead_req->iv,
			       req->qp_ctx->res[req->req_id].c_ivin,
			       ctx->c_ctx.ivsize);
	}

	sec_request_untransfer(ctx, req);
err_uninit_req:
	sec_request_uninit(ctx, req);

	return ret;
}

static const struct sec_req_op sec_skcipher_req_ops = {
	.buf_map	= sec_skcipher_sgl_map,
	.buf_unmap	= sec_skcipher_sgl_unmap,
	.do_transfer	= sec_skcipher_copy_iv,
	.bd_fill	= sec_skcipher_bd_fill,
	.bd_send	= sec_bd_send,
	.callback	= sec_skcipher_callback,
	.process	= sec_process,
};

static const struct sec_req_op sec_aead_req_ops = {
	.buf_map	= sec_aead_sgl_map,
	.buf_unmap	= sec_aead_sgl_unmap,
	.do_transfer	= sec_aead_copy_iv,
	.bd_fill	= sec_aead_bd_fill,
	.bd_send	= sec_bd_send,
	.callback	= sec_aead_callback,
	.process	= sec_process,
};

static int sec_skcipher_ctx_init(struct crypto_skcipher *tfm)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->req_op = &sec_skcipher_req_ops;

	return sec_skcipher_init(tfm);
}

static void sec_skcipher_ctx_exit(struct crypto_skcipher *tfm)
{
	sec_skcipher_uninit(tfm);
}

static int sec_aead_init(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	crypto_aead_set_reqsize(tfm, sizeof(struct sec_req));
	ctx->alg_type = SEC_AEAD;
	ctx->c_ctx.ivsize = crypto_aead_ivsize(tfm);
	if (ctx->c_ctx.ivsize > SEC_IV_SIZE) {
		dev_err(SEC_CTX_DEV(ctx), "get error aead iv size!\n");
		return -EINVAL;
	}

	ctx->req_op = &sec_aead_req_ops;
	ret = sec_ctx_base_init(ctx);
	if (ret)
		return ret;

	ret = sec_auth_init(ctx);
	if (ret)
		goto err_auth_init;

	ret = sec_cipher_init(ctx);
	if (ret)
		goto err_cipher_init;

	return ret;

err_cipher_init:
	sec_auth_uninit(ctx);
err_auth_init:
	sec_ctx_base_uninit(ctx);

	return ret;
}

static void sec_aead_exit(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);

	sec_cipher_uninit(ctx);
	sec_auth_uninit(ctx);
	sec_ctx_base_uninit(ctx);
}

static int sec_aead_ctx_init(struct crypto_aead *tfm, const char *hash_name)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	struct sec_auth_ctx *auth_ctx = &ctx->a_ctx;
	int ret;

	ret = sec_aead_init(tfm);
	if (ret) {
		pr_err("hisi_sec2: aead init error!\n");
		return ret;
	}

	auth_ctx->hash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(auth_ctx->hash_tfm)) {
		dev_err(SEC_CTX_DEV(ctx), "aead alloc shash error!\n");
		sec_aead_exit(tfm);
		return PTR_ERR(auth_ctx->hash_tfm);
	}

	return 0;
}

static void sec_aead_ctx_exit(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_shash(ctx->a_ctx.hash_tfm);
	sec_aead_exit(tfm);
}

static int sec_aead_sha1_ctx_init(struct crypto_aead *tfm)
{
	return sec_aead_ctx_init(tfm, "sha1");
}

static int sec_aead_sha256_ctx_init(struct crypto_aead *tfm)
{
	return sec_aead_ctx_init(tfm, "sha256");
}

static int sec_aead_sha512_ctx_init(struct crypto_aead *tfm)
{
	return sec_aead_ctx_init(tfm, "sha512");
}

static int sec_skcipher_param_check(struct sec_ctx *ctx, struct sec_req *sreq)
{
	struct skcipher_request *sk_req = sreq->c_req.sk_req;
	struct device *dev = SEC_CTX_DEV(ctx);
	u8 c_alg = ctx->c_ctx.c_alg;

	if (unlikely(!sk_req->src || !sk_req->dst)) {
		dev_err(dev, "skcipher input param error!\n");
		return -EINVAL;
	}
	sreq->c_req.c_len = sk_req->cryptlen;
	if (c_alg == SEC_CALG_3DES) {
		if (unlikely(sk_req->cryptlen & (DES3_EDE_BLOCK_SIZE - 1))) {
			dev_err(dev, "skcipher 3des input length error!\n");
			return -EINVAL;
		}
		return 0;
	} else if (c_alg == SEC_CALG_AES || c_alg == SEC_CALG_SM4) {
		if (unlikely(sk_req->cryptlen & (AES_BLOCK_SIZE - 1))) {
			dev_err(dev, "skcipher aes input length error!\n");
			return -EINVAL;
		}
		return 0;
	}

	dev_err(dev, "skcipher algorithm error!\n");
	return -EINVAL;
}

static int sec_skcipher_crypto(struct skcipher_request *sk_req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(sk_req);
	struct sec_req *req = skcipher_request_ctx(sk_req);
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	if (!sk_req->cryptlen)
		return 0;

	req->c_req.sk_req = sk_req;
	req->c_req.encrypt = encrypt;
	req->ctx = ctx;

	ret = sec_skcipher_param_check(ctx, req);
	if (unlikely(ret))
		return -EINVAL;

	return ctx->req_op->process(ctx, req);
}

static int sec_skcipher_encrypt(struct skcipher_request *sk_req)
{
	return sec_skcipher_crypto(sk_req, true);
}

static int sec_skcipher_decrypt(struct skcipher_request *sk_req)
{
	return sec_skcipher_crypto(sk_req, false);
}

#define SEC_SKCIPHER_GEN_ALG(sec_cra_name, sec_set_key, sec_min_key_size, \
	sec_max_key_size, ctx_init, ctx_exit, blk_size, iv_size)\
{\
	.base = {\
		.cra_name = sec_cra_name,\
		.cra_driver_name = "hisi_sec_"sec_cra_name,\
		.cra_priority = SEC_PRIORITY,\
		.cra_flags = CRYPTO_ALG_ASYNC,\
		.cra_blocksize = blk_size,\
		.cra_ctxsize = sizeof(struct sec_ctx),\
		.cra_module = THIS_MODULE,\
	},\
	.init = ctx_init,\
	.exit = ctx_exit,\
	.setkey = sec_set_key,\
	.decrypt = sec_skcipher_decrypt,\
	.encrypt = sec_skcipher_encrypt,\
	.min_keysize = sec_min_key_size,\
	.max_keysize = sec_max_key_size,\
	.ivsize = iv_size,\
},

#define SEC_SKCIPHER_ALG(name, key_func, min_key_size, \
	max_key_size, blk_size, iv_size) \
	SEC_SKCIPHER_GEN_ALG(name, key_func, min_key_size, max_key_size, \
	sec_skcipher_ctx_init, sec_skcipher_ctx_exit, blk_size, iv_size)

static struct skcipher_alg sec_skciphers[] = {
	SEC_SKCIPHER_ALG("ecb(aes)", sec_setkey_aes_ecb,
			 AES_MIN_KEY_SIZE, AES_MAX_KEY_SIZE,
			 AES_BLOCK_SIZE, 0)

	SEC_SKCIPHER_ALG("cbc(aes)", sec_setkey_aes_cbc,
			 AES_MIN_KEY_SIZE, AES_MAX_KEY_SIZE,
			 AES_BLOCK_SIZE, AES_BLOCK_SIZE)

	SEC_SKCIPHER_ALG("xts(aes)", sec_setkey_aes_xts,
			 SEC_XTS_MIN_KEY_SIZE, SEC_XTS_MAX_KEY_SIZE,
			 AES_BLOCK_SIZE, AES_BLOCK_SIZE)

	SEC_SKCIPHER_ALG("ecb(des3_ede)", sec_setkey_3des_ecb,
			 SEC_DES3_2KEY_SIZE, SEC_DES3_3KEY_SIZE,
			 DES3_EDE_BLOCK_SIZE, 0)

	SEC_SKCIPHER_ALG("cbc(des3_ede)", sec_setkey_3des_cbc,
			 SEC_DES3_2KEY_SIZE, SEC_DES3_3KEY_SIZE,
			 DES3_EDE_BLOCK_SIZE, DES3_EDE_BLOCK_SIZE)

	SEC_SKCIPHER_ALG("xts(sm4)", sec_setkey_sm4_xts,
			 SEC_XTS_MIN_KEY_SIZE, SEC_XTS_MIN_KEY_SIZE,
			 AES_BLOCK_SIZE, AES_BLOCK_SIZE)

	SEC_SKCIPHER_ALG("cbc(sm4)", sec_setkey_sm4_cbc,
			 AES_MIN_KEY_SIZE, AES_MIN_KEY_SIZE,
			 AES_BLOCK_SIZE, AES_BLOCK_SIZE)
};

static int sec_aead_param_check(struct sec_ctx *ctx, struct sec_req *sreq)
{
	u8 c_alg = ctx->c_ctx.c_alg;
	struct aead_request *req = sreq->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	size_t authsize = crypto_aead_authsize(tfm);

	if (unlikely(!req->src || !req->dst || !req->cryptlen)) {
		dev_err(SEC_CTX_DEV(ctx), "aead input param error!\n");
		return -EINVAL;
	}

	/* Support AES only */
	if (unlikely(c_alg != SEC_CALG_AES)) {
		dev_err(SEC_CTX_DEV(ctx), "aead crypto alg error!\n");
		return -EINVAL;

	}
	if (sreq->c_req.encrypt)
		sreq->c_req.c_len = req->cryptlen;
	else
		sreq->c_req.c_len = req->cryptlen - authsize;

	if (unlikely(sreq->c_req.c_len & (AES_BLOCK_SIZE - 1))) {
		dev_err(SEC_CTX_DEV(ctx), "aead crypto length error!\n");
		return -EINVAL;
	}

	return 0;
}

static int sec_aead_crypto(struct aead_request *a_req, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(a_req);
	struct sec_req *req = aead_request_ctx(a_req);
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	req->aead_req.aead_req = a_req;
	req->c_req.encrypt = encrypt;
	req->ctx = ctx;

	ret = sec_aead_param_check(ctx, req);
	if (unlikely(ret))
		return -EINVAL;

	return ctx->req_op->process(ctx, req);
}

static int sec_aead_encrypt(struct aead_request *a_req)
{
	return sec_aead_crypto(a_req, true);
}

static int sec_aead_decrypt(struct aead_request *a_req)
{
	return sec_aead_crypto(a_req, false);
}

#define SEC_AEAD_GEN_ALG(sec_cra_name, sec_set_key, ctx_init,\
			 ctx_exit, blk_size, iv_size, max_authsize)\
{\
	.base = {\
		.cra_name = sec_cra_name,\
		.cra_driver_name = "hisi_sec_"sec_cra_name,\
		.cra_priority = SEC_PRIORITY,\
		.cra_flags = CRYPTO_ALG_ASYNC,\
		.cra_blocksize = blk_size,\
		.cra_ctxsize = sizeof(struct sec_ctx),\
		.cra_module = THIS_MODULE,\
	},\
	.init = ctx_init,\
	.exit = ctx_exit,\
	.setkey = sec_set_key,\
	.decrypt = sec_aead_decrypt,\
	.encrypt = sec_aead_encrypt,\
	.ivsize = iv_size,\
	.maxauthsize = max_authsize,\
}

#define SEC_AEAD_ALG(algname, keyfunc, aead_init, blksize, ivsize, authsize)\
	SEC_AEAD_GEN_ALG(algname, keyfunc, aead_init,\
			sec_aead_ctx_exit, blksize, ivsize, authsize)

static struct aead_alg sec_aeads[] = {
	SEC_AEAD_ALG("authenc(hmac(sha1),cbc(aes))",
		     sec_setkey_aes_cbc_sha1, sec_aead_sha1_ctx_init,
		     AES_BLOCK_SIZE, AES_BLOCK_SIZE, SHA1_DIGEST_SIZE),

	SEC_AEAD_ALG("authenc(hmac(sha256),cbc(aes))",
		     sec_setkey_aes_cbc_sha256, sec_aead_sha256_ctx_init,
		     AES_BLOCK_SIZE, AES_BLOCK_SIZE, SHA256_DIGEST_SIZE),

	SEC_AEAD_ALG("authenc(hmac(sha512),cbc(aes))",
		     sec_setkey_aes_cbc_sha512, sec_aead_sha512_ctx_init,
		     AES_BLOCK_SIZE, AES_BLOCK_SIZE, SHA512_DIGEST_SIZE),
};

int sec_register_to_crypto(void)
{
	int ret = 0;

	/* To avoid repeat register */
	if (atomic_add_return(1, &sec_active_devs) == 1) {
		ret = crypto_register_skciphers(sec_skciphers,
						ARRAY_SIZE(sec_skciphers));
		if (ret)
			return ret;

		ret = crypto_register_aeads(sec_aeads, ARRAY_SIZE(sec_aeads));
		if (ret)
			goto reg_aead_fail;
	}

	return ret;

reg_aead_fail:
	crypto_unregister_skciphers(sec_skciphers, ARRAY_SIZE(sec_skciphers));

	return ret;
}

void sec_unregister_from_crypto(void)
{
	if (atomic_sub_return(1, &sec_active_devs) == 0) {
		crypto_unregister_skciphers(sec_skciphers,
					    ARRAY_SIZE(sec_skciphers));
		crypto_unregister_aeads(sec_aeads, ARRAY_SIZE(sec_aeads));
	}
}
