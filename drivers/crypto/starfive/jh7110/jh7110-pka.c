// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive Public Key Algo acceleration driver
 *
 * Copyright (c) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/mod_devicetable.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/crypto.h>
#include <linux/io.h>
#include <linux/module.h>

#include <linux/delay.h>
#include <linux/dma-direct.h>
#include <crypto/scatterwalk.h>

#include "jh7110-str.h"

#define JH7110_RSA_KEYSZ_LEN			(2048 >> 2)
#define JH7110_RSA_KEY_SIZE			(JH7110_RSA_KEYSZ_LEN * 3)
#define JH7110_RSA_MAX_KEYSZ			256
#define swap32(val) (						\
		     (((u32)(val) << 24) & (u32)0xFF000000) |	\
		     (((u32)(val) <<  8) & (u32)0x00FF0000) |	\
		     (((u32)(val) >>  8) & (u32)0x0000FF00) |	\
		     (((u32)(val) >> 24) & (u32)0x000000FF))

static inline int jh7110_pka_wait_pre(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_CRYPTO_CASR_OFFSET, status,
				   (status & JH7110_PKA_DONE_FLAGS), 10, 100000);
}

static int jh7110_pka_wait_done(struct jh7110_sec_dev *sdev)
{
	int ret = -1;

	wait_for_completion(&sdev->rsa_comp);
	reinit_completion(&sdev->rsa_comp);
	mutex_unlock(&sdev->doing);
	mutex_lock(&sdev->doing);
	if (sdev->done_flags & JH7110_PKA_DONE)
		ret = 0;
	mutex_unlock(&sdev->doing);

	return ret;
}

static void jh7110_rsa_free_key(struct jh7110_rsa_key *key)
{
	if (key->d)
		kfree_sensitive(key->d);
	if (key->e)
		kfree_sensitive(key->e);
	if (key->n)
		kfree_sensitive(key->n);
	memset(key, 0, sizeof(*key));
}

static unsigned int jh7110_rsa_get_nbit(u8 *pa, u32 snum, int key_sz)
{
	u32 i;
	u8 value;

	i = snum >> 3;

	value = pa[key_sz - i - 1];
	value >>= snum & 0x7;
	value &= 0x1;

	return value;
}

static int jh7110_rsa_domain_transfer(struct jh7110_sec_ctx *ctx, u32 *result, u32 *opa, u8 domain, u32 *mod, int bit_len)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	unsigned int *info;
	int loop;
	u8 opsize;
	u32 temp;

	mutex_lock(&sdev->doing);

	opsize = (bit_len - 1) >> 5;
	rctx->csr.pka_csr.v = 0;
	jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

	info = (unsigned int *)mod;
	for (loop = 0; loop <= opsize; loop++)
		jh7110_sec_write(sdev, JH7110_CRYPTO_CANR_OFFSET + loop * 4, info[opsize - loop]);


	if (domain != 0) {
		rctx->csr.pka_csr.v = 0;
		rctx->csr.pka_csr.cln_done = 1;
		rctx->csr.pka_csr.opsize = opsize;
		rctx->csr.pka_csr.exposize = opsize;
		rctx->csr.pka_csr.cmd = CRYPTO_CMD_PRE;
		rctx->csr.pka_csr.ie = 1;
		rctx->csr.pka_csr.start = 0x1;
		rctx->csr.pka_csr.not_r2 = 0x1;
		jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

		jh7110_pka_wait_done(sdev);

		mutex_lock(&sdev->doing);

		info = (unsigned int *)opa;
		for (loop = 0; loop <= opsize; loop++)
			jh7110_sec_write(sdev, JH7110_CRYPTO_CAAR_OFFSET + loop * 4, info[opsize - loop]);

		for (loop = 0; loop <= opsize; loop++) {
			if (loop == 0)
				jh7110_sec_write(sdev, JH7110_CRYPTO_CAER_OFFSET + loop * 4, 0x1000000);
			else
				jh7110_sec_write(sdev, JH7110_CRYPTO_CAER_OFFSET + loop * 4, 0);
		}

		rctx->csr.pka_csr.v = 0;
		rctx->csr.pka_csr.cln_done = 1;
		rctx->csr.pka_csr.ie = 1;
		rctx->csr.pka_csr.opsize = opsize;
		rctx->csr.pka_csr.exposize = opsize;
		rctx->csr.pka_csr.cmd = CRYPTO_CMD_AERN;
		rctx->csr.pka_csr.start = 0x1;
		jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

		jh7110_pka_wait_done(sdev);
	} else {
		rctx->csr.pka_csr.v = 0;
		rctx->csr.pka_csr.cln_done = 1;
		rctx->csr.pka_csr.opsize = opsize;
		rctx->csr.pka_csr.exposize = opsize;
		rctx->csr.pka_csr.cmd = CRYPTO_CMD_PRE;
		rctx->csr.pka_csr.ie = 1;
		rctx->csr.pka_csr.start = 0x1;
		rctx->csr.pka_csr.pre_expf = 0x1;
		jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

		jh7110_pka_wait_done(sdev);

		mutex_lock(&sdev->doing);

		info = (unsigned int *)opa;
		for (loop = 0; loop <= opsize; loop++)
			jh7110_sec_write(sdev, JH7110_CRYPTO_CAER_OFFSET + loop * 4, info[opsize - loop]);

		rctx->csr.pka_csr.v = 0;
		rctx->csr.pka_csr.cln_done = 1;
		rctx->csr.pka_csr.opsize = opsize;
		rctx->csr.pka_csr.exposize = opsize;
		rctx->csr.pka_csr.cmd = CRYPTO_CMD_ARN;
		rctx->csr.pka_csr.ie = 1;
		rctx->csr.pka_csr.start = 0x1;
		jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

		jh7110_pka_wait_done(sdev);
	}

	mutex_lock(&sdev->doing);
	for (loop = 0; loop <= opsize; loop++) {
		temp = jh7110_sec_read(sdev, JH7110_CRYPTO_CAAR_OFFSET + 0x4 * loop);
		result[opsize - loop] = temp;
	}
	mutex_unlock(&sdev->doing);

	return 0;
}

static int jh7110_rsa_cpu_powm(struct jh7110_sec_ctx *ctx, u32 *result, u8 *de, u32 *n, int key_sz)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_rsa_key *key = &ctx->rsa_key;
	u32 initial;
	int opsize, mlen, bs, loop;
	unsigned int *mta;

	opsize = (key_sz - 1) >> 2;
	initial = 1;

	mta = kmalloc(key_sz, GFP_KERNEL);
	if (!mta)
		return -ENOMEM;

	jh7110_rsa_domain_transfer(ctx, mta, sdev->pka_data, 0, n, key_sz << 3);

	for (loop = 0; loop <= opsize; loop++)
		jh7110_sec_write(sdev, JH7110_CRYPTO_CANR_OFFSET + loop * 4, n[opsize - loop]);

	mutex_lock(&sdev->doing);

	rctx->csr.pka_csr.v = 0;
	rctx->csr.pka_csr.cln_done = 1;
	rctx->csr.pka_csr.opsize = opsize;
	rctx->csr.pka_csr.exposize = opsize;
	rctx->csr.pka_csr.cmd = CRYPTO_CMD_PRE;
	rctx->csr.pka_csr.ie = 1;
	rctx->csr.pka_csr.not_r2 = 0x1;
	rctx->csr.pka_csr.start = 0x1;

	jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

	jh7110_pka_wait_done(sdev);

	for (loop = 0; loop <= opsize; loop++)
		jh7110_sec_write(sdev, JH7110_CRYPTO_CAER_OFFSET + loop * 4, mta[opsize - loop]);

	for (loop = key->bitlen; loop > 0; loop--) {
		if (initial) {
			for (bs = 0; bs <= opsize; bs++)
				result[bs] = mta[bs];

			initial = 0;
		} else {
			mlen = jh7110_rsa_get_nbit(de, loop - 1, key_sz);

			mutex_lock(&sdev->doing);

			rctx->csr.pka_csr.v = 0;
			rctx->csr.pka_csr.cln_done = 1;
			rctx->csr.pka_csr.opsize = opsize;
			rctx->csr.pka_csr.exposize = opsize;
			rctx->csr.pka_csr.cmd = CRYPTO_CMD_AARN;
			rctx->csr.pka_csr.ie = 1;
			rctx->csr.pka_csr.start = 0x1;

			jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

			jh7110_pka_wait_done(sdev);

			if (mlen) {
				mutex_lock(&sdev->doing);

				rctx->csr.pka_csr.v = 0;
				rctx->csr.pka_csr.cln_done = 1;
				rctx->csr.pka_csr.opsize = opsize;
				rctx->csr.pka_csr.exposize = opsize;
				rctx->csr.pka_csr.cmd = CRYPTO_CMD_AERN;
				rctx->csr.pka_csr.ie = 1;
				rctx->csr.pka_csr.start = 0x1;

				jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

				jh7110_pka_wait_done(sdev);
			}
		}
	}

	mutex_lock(&sdev->doing);
	for (loop = 0; loop <= opsize; loop++) {
		unsigned int temp;

		temp = jh7110_sec_read(sdev, JH7110_CRYPTO_CAAR_OFFSET + 0x4 * loop);
		result[opsize - loop] = temp;
	}
	mutex_unlock(&sdev->doing);

	jh7110_rsa_domain_transfer(ctx, result, result, 1, n, key_sz << 3);

	return 0;
}

static int jh7110_rsa_powm(struct jh7110_sec_ctx *ctx, u8 *result, u8 *de, u8 *n, int key_sz)
{
	return jh7110_rsa_cpu_powm(ctx, (u32 *)result, de, (u32 *)n, key_sz);
}

static int jh7110_rsa_get_from_sg(struct jh7110_sec_request_ctx *rctx, size_t offset,
				size_t count, size_t data_offset)
{
	size_t of, ct, index;
	struct scatterlist	*sg = rctx->sg;

	of = offset;
	ct = count;

	while (sg->length <= of) {
		of -= sg->length;

		if (!sg_is_last(sg)) {
			sg = sg_next(sg);
			continue;
		} else {
			return -EBADE;
		}
	}

	index = data_offset;
	while (ct > 0) {
		if (sg->length - of >= ct) {
			scatterwalk_map_and_copy(rctx->sdev->pka_data + index, sg,
					of, ct, 0);
			index = index + ct;
			return index - data_offset;
		}
		scatterwalk_map_and_copy(rctx->sdev->pka_data + index, sg,
									of, sg->length - of, 0);
		index += sg->length - of;
		ct = ct - (sg->length - of);

		of = 0;

		if (!sg_is_last(sg))
			sg = sg_next(sg);
		else
			return -EBADE;
	}
	return index - data_offset;
}

static int jh7110_rsa_enc_core(struct jh7110_sec_ctx *ctx, int enc)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_rsa_key *key = &ctx->rsa_key;
	size_t data_len, total, count, data_offset;
	int ret = 0;
	unsigned int *info;
	int loop;

	sdev->cry_type = JH7110_PKA_TYPE;

	rctx->csr.pka_csr.v = 0;
	rctx->csr.pka_csr.reset = 1;
	jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, rctx->csr.pka_csr.v);

	if (jh7110_pka_wait_pre(ctx))
		dev_dbg(sdev->dev, "this is debug for lophyel pka_casr = %x %s %s %d\n",
			   jh7110_sec_read(sdev, JH7110_CRYPTO_CASR_OFFSET), __FILE__, __func__, __LINE__);

	rctx->offset = 0;
	total = 0;

	while (total < rctx->total_in) {
		count = min(sdev->data_buf_len, rctx->total_in);
		count = min(count, key->key_sz);
		memset(sdev->pka_data, 0, key->key_sz);
		data_offset = key->key_sz - count;

		data_len = jh7110_rsa_get_from_sg(rctx, rctx->offset, count, data_offset);
		if (data_len < 0)
			return data_len;
		if (data_len != count)
			return -EINVAL;

		if (enc) {
			key->bitlen = key->e_bitlen;
			ret = jh7110_rsa_powm(ctx, sdev->pka_data + JH7110_RSA_KEYSZ_LEN, key->e, key->n, key->key_sz);
		} else {
			key->bitlen = key->d_bitlen;
			ret = jh7110_rsa_powm(ctx, sdev->pka_data + JH7110_RSA_KEYSZ_LEN, key->d, key->n, key->key_sz);
		}


		if (ret)
			return ret;

		info = (unsigned int *)(sdev->pka_data + JH7110_RSA_KEYSZ_LEN);
		for (loop = 0; loop < key->key_sz >> 2; loop++)
			dev_dbg(sdev->dev, "result[%d] = %x\n", loop, info[loop]);

		sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg), sdev->pka_data + JH7110_RSA_KEYSZ_LEN,
			       key->key_sz, rctx->offset, 0);

		rctx->offset += data_len;
		total += data_len;
	}

	return ret;
}

static int jh7110_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct jh7110_rsa_key *key = &ctx->rsa_key;
	struct jh7110_sec_request_ctx *rctx = akcipher_request_ctx(req);
	int ret = 0;

	if (key->key_sz > JH7110_RSA_MAX_KEYSZ) {
		akcipher_request_set_tfm(req, ctx->fallback.akcipher);
		ret = crypto_akcipher_encrypt(req);
		akcipher_request_set_tfm(req, tfm);
		return ret;
	}

	if (unlikely(!key->n || !key->e))
		return -EINVAL;

	if (req->dst_len < key->key_sz) {
		req->dst_len = key->key_sz;
		dev_err(ctx->sdev->dev, "Output buffer length less than parameter n\n");
		return -EOVERFLOW;
	}

	mutex_lock(&ctx->sdev->rsa_lock);

	rctx->sg = req->src;
	rctx->out_sg = req->dst;
	rctx->sdev = ctx->sdev;
	ctx->rctx = rctx;
	rctx->total_in = req->src_len;
	rctx->total_out = req->dst_len;

	ret = jh7110_rsa_enc_core(ctx, 1);

	mutex_unlock(&ctx->sdev->rsa_lock);

	return ret;
}

static int jh7110_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct jh7110_rsa_key *key = &ctx->rsa_key;
	struct jh7110_sec_request_ctx *rctx = akcipher_request_ctx(req);
	int ret = 0;

	if (key->key_sz > JH7110_RSA_MAX_KEYSZ) {
		akcipher_request_set_tfm(req, ctx->fallback.akcipher);
		ret = crypto_akcipher_decrypt(req);
		akcipher_request_set_tfm(req, tfm);
		return ret;
	}

	if (unlikely(!key->n || !key->d))
		return -EINVAL;

	if (req->dst_len < key->key_sz) {
		req->dst_len = key->key_sz;
		dev_err(ctx->sdev->dev, "Output buffer length less than parameter n\n");
		return -EOVERFLOW;
	}

	mutex_lock(&ctx->sdev->rsa_lock);

	rctx->sg = req->src;
	rctx->out_sg = req->dst;
	rctx->sdev = ctx->sdev;
	ctx->rctx = rctx;
	rctx->total_in = req->src_len;
	rctx->total_out = req->dst_len;

	ret = jh7110_rsa_enc_core(ctx, 0);

	mutex_unlock(&ctx->sdev->rsa_lock);

	return ret;
}

static unsigned long jh7110_rsa_enc_fn_id(unsigned int len)
{
	unsigned int bitslen = len << 3;

	if (bitslen & 0x1f)
		return -EINVAL;

	if (bitslen > 2048)
		return false;

	return true;
}

static int jh7110_rsa_set_n(struct jh7110_rsa_key *rsa_key, const char *value,
			 size_t vlen)
{
	const char *ptr = value;
	int ret;

	while (!*ptr && vlen) {
		ptr++;
		vlen--;
	}
	rsa_key->key_sz = vlen;
	ret = -EINVAL;
	/* invalid key size provided */
	if (!jh7110_rsa_enc_fn_id(rsa_key->key_sz))
		return 0;

	ret = -ENOMEM;
	rsa_key->n = kmemdup(ptr, rsa_key->key_sz, GFP_KERNEL);
	if (!rsa_key->n)
		goto err;

	return 1;
 err:
	rsa_key->key_sz = 0;
	rsa_key->n = NULL;
	return ret;
}

static int jh7110_rsa_set_e(struct jh7110_rsa_key *rsa_key, const char *value,
			 size_t vlen)
{
	const char *ptr = value;
	unsigned char pt;
	int loop;

	while (!*ptr && vlen) {
		ptr++;
		vlen--;
	}
	pt = *ptr;

	if (!rsa_key->key_sz || !vlen || vlen > rsa_key->key_sz) {
		rsa_key->e = NULL;
		return -EINVAL;
	}

	rsa_key->e = kzalloc(rsa_key->key_sz, GFP_KERNEL);
	if (!rsa_key->e)
		return -ENOMEM;

	for (loop = 8; loop > 0; loop--) {
		if (pt >> (loop - 1))
			break;
	}

	rsa_key->e_bitlen = (vlen - 1) * 8 + loop;

	memcpy(rsa_key->e + (rsa_key->key_sz - vlen), ptr, vlen);

	return 0;
}

static int jh7110_rsa_set_d(struct jh7110_rsa_key *rsa_key, const char *value,
			 size_t vlen)
{
	const char *ptr = value;
	unsigned char pt;
	int loop;
	int ret;

	while (!*ptr && vlen) {
		ptr++;
		vlen--;
	}
	pt = *ptr;

	ret = -EINVAL;
	if (!rsa_key->key_sz || !vlen || vlen > rsa_key->key_sz)
		goto err;

	ret = -ENOMEM;
	rsa_key->d = kzalloc(rsa_key->key_sz, GFP_KERNEL);
	if (!rsa_key->d)
		goto err;

	for (loop = 8; loop > 0; loop--) {
		pr_debug("this is debug for lophyel loop = %d pt >> (loop - 1) = %x value[%d] = %x %s %s %d\n",
			   loop, pt >> (loop - 1), loop, value[loop], __FILE__, __func__, __LINE__);
		if (pt >> (loop - 1))
			break;
	}

	rsa_key->d_bitlen = (vlen - 1) * 8 + loop;

	memcpy(rsa_key->d + (rsa_key->key_sz - vlen), ptr, vlen);

	return 0;
 err:
	rsa_key->d = NULL;
	return ret;
}

static int jh7110_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			unsigned int keylen, bool private)
{
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsa_key raw_key = {NULL};
	struct jh7110_rsa_key *rsa_key = &ctx->rsa_key;
	int ret;

	jh7110_rsa_free_key(rsa_key);

	if (private)
		ret = rsa_parse_priv_key(&raw_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&raw_key, key, keylen);
	if (ret < 0)
		goto err;

	ret = jh7110_rsa_set_n(rsa_key, raw_key.n, raw_key.n_sz);
	if (ret <= 0)
		return ret;

	ret = jh7110_rsa_set_e(rsa_key, raw_key.e, raw_key.e_sz);
	if (ret < 0)
		goto err;

	if (private) {
		ret = jh7110_rsa_set_d(rsa_key, raw_key.d, raw_key.d_sz);
		if (ret < 0)
			goto err;
	}

	if (!rsa_key->n || !rsa_key->e) {
		/* invalid key provided */
		ret = -EINVAL;
		goto err;
	}
	if (private && !rsa_key->d) {
		/* invalid private key provided */
		ret = -EINVAL;
		goto err;
	}

	return 0;
 err:
	jh7110_rsa_free_key(rsa_key);
	return ret;
}

static int jh7110_rsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
			       unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_pub_key(ctx->fallback.akcipher, key, keylen);
	if (ret)
		return ret;

	return jh7110_rsa_setkey(tfm, key, keylen, false);
}

static int jh7110_rsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
				unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_priv_key(ctx->fallback.akcipher, key, keylen);
	if (ret)
		return ret;

	return jh7110_rsa_setkey(tfm, key, keylen, true);
}

static unsigned int jh7110_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);

	/* For key sizes > 2Kb, use software tfm */
	if (ctx->rsa_key.key_sz > JH7110_RSA_MAX_KEYSZ)
		return crypto_akcipher_maxsize(ctx->fallback.akcipher);

	return ctx->rsa_key.key_sz;
}

/* Per session pkc's driver context creation function */
static int jh7110_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);

	ctx->fallback.akcipher = crypto_alloc_akcipher("rsa-generic", 0, 0);
	if (IS_ERR(ctx->fallback.akcipher)) {
		pr_err("Can not alloc_akcipher!\n");
		return PTR_ERR(ctx->fallback.akcipher);
	}

	ctx->sdev = jh7110_sec_find_dev(ctx);
	if (!ctx->sdev) {
		crypto_free_akcipher(ctx->fallback.akcipher);
		return -ENODEV;
	}

	akcipher_set_reqsize(tfm, sizeof(struct jh7110_sec_request_ctx));

	return 0;
}

/* Per session pkc's driver context cleanup function */
static void jh7110_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct jh7110_rsa_key *key = (struct jh7110_rsa_key *)&ctx->rsa_key;

	crypto_free_akcipher(ctx->fallback.akcipher);
	jh7110_rsa_free_key(key);
}

static struct akcipher_alg jh7110_rsa = {
	.encrypt = jh7110_rsa_enc,
	.decrypt = jh7110_rsa_dec,
	.sign = jh7110_rsa_dec,
	.verify = jh7110_rsa_enc,
	.set_pub_key = jh7110_rsa_set_pub_key,
	.set_priv_key = jh7110_rsa_set_priv_key,
	.max_size = jh7110_rsa_max_size,
	.init = jh7110_rsa_init_tfm,
	.exit = jh7110_rsa_exit_tfm,
	.reqsize = sizeof(struct jh7110_sec_request_ctx),
	.base = {
		.cra_name = "rsa",
		.cra_driver_name = "rsa-jh7110",
		.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
			     CRYPTO_ALG_ASYNC |
			     CRYPTO_ALG_NEED_FALLBACK,
		.cra_priority = 3000,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct jh7110_sec_ctx),
	},
};

int jh7110_pka_register_algs(void)
{
	int ret = 0;

	ret = crypto_register_akcipher(&jh7110_rsa);
	if (ret)
		pr_err("JH7110 RSA registration failed\n");

	return ret;
}

void jh7110_pka_unregister_algs(void)
{
	crypto_unregister_akcipher(&jh7110_rsa);
}

MODULE_DESCRIPTION("Public Key Algo acceleration driver for StarFive JH7110 SoC");
MODULE_AUTHOR("Jia Jie Ho <jiajie.ho@starfivetech.com>");
MODULE_LICENSE("GPL");
