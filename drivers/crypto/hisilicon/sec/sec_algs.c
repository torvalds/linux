// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2016-2017 HiSilicon Limited. */
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/internal/des.h>
#include <crypto/skcipher.h>
#include <crypto/xts.h>
#include <crypto/internal/skcipher.h>

#include "sec_drv.h"

#define SEC_MAX_CIPHER_KEY		64
#define SEC_REQ_LIMIT SZ_32M

struct sec_c_alg_cfg {
	unsigned c_alg		: 3;
	unsigned c_mode		: 3;
	unsigned key_len	: 2;
	unsigned c_width	: 2;
};

static const struct sec_c_alg_cfg sec_c_alg_cfgs[] =  {
	[SEC_C_DES_ECB_64] = {
		.c_alg = SEC_C_ALG_DES,
		.c_mode = SEC_C_MODE_ECB,
		.key_len = SEC_KEY_LEN_DES,
	},
	[SEC_C_DES_CBC_64] = {
		.c_alg = SEC_C_ALG_DES,
		.c_mode = SEC_C_MODE_CBC,
		.key_len = SEC_KEY_LEN_DES,
	},
	[SEC_C_3DES_ECB_192_3KEY] = {
		.c_alg = SEC_C_ALG_3DES,
		.c_mode = SEC_C_MODE_ECB,
		.key_len = SEC_KEY_LEN_3DES_3_KEY,
	},
	[SEC_C_3DES_ECB_192_2KEY] = {
		.c_alg = SEC_C_ALG_3DES,
		.c_mode = SEC_C_MODE_ECB,
		.key_len = SEC_KEY_LEN_3DES_2_KEY,
	},
	[SEC_C_3DES_CBC_192_3KEY] = {
		.c_alg = SEC_C_ALG_3DES,
		.c_mode = SEC_C_MODE_CBC,
		.key_len = SEC_KEY_LEN_3DES_3_KEY,
	},
	[SEC_C_3DES_CBC_192_2KEY] = {
		.c_alg = SEC_C_ALG_3DES,
		.c_mode = SEC_C_MODE_CBC,
		.key_len = SEC_KEY_LEN_3DES_2_KEY,
	},
	[SEC_C_AES_ECB_128] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_ECB,
		.key_len = SEC_KEY_LEN_AES_128,
	},
	[SEC_C_AES_ECB_192] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_ECB,
		.key_len = SEC_KEY_LEN_AES_192,
	},
	[SEC_C_AES_ECB_256] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_ECB,
		.key_len = SEC_KEY_LEN_AES_256,
	},
	[SEC_C_AES_CBC_128] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_CBC,
		.key_len = SEC_KEY_LEN_AES_128,
	},
	[SEC_C_AES_CBC_192] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_CBC,
		.key_len = SEC_KEY_LEN_AES_192,
	},
	[SEC_C_AES_CBC_256] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_CBC,
		.key_len = SEC_KEY_LEN_AES_256,
	},
	[SEC_C_AES_CTR_128] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_CTR,
		.key_len = SEC_KEY_LEN_AES_128,
	},
	[SEC_C_AES_CTR_192] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_CTR,
		.key_len = SEC_KEY_LEN_AES_192,
	},
	[SEC_C_AES_CTR_256] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_CTR,
		.key_len = SEC_KEY_LEN_AES_256,
	},
	[SEC_C_AES_XTS_128] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_XTS,
		.key_len = SEC_KEY_LEN_AES_128,
	},
	[SEC_C_AES_XTS_256] = {
		.c_alg = SEC_C_ALG_AES,
		.c_mode = SEC_C_MODE_XTS,
		.key_len = SEC_KEY_LEN_AES_256,
	},
	[SEC_C_NULL] = {
	},
};

/*
 * Mutex used to ensure safe operation of reference count of
 * alg providers
 */
static DEFINE_MUTEX(algs_lock);
static unsigned int active_devs;

static void sec_alg_skcipher_init_template(struct sec_alg_tfm_ctx *ctx,
					   struct sec_bd_info *req,
					   enum sec_cipher_alg alg)
{
	const struct sec_c_alg_cfg *cfg = &sec_c_alg_cfgs[alg];

	memset(req, 0, sizeof(*req));
	req->w0 |= cfg->c_mode << SEC_BD_W0_C_MODE_S;
	req->w1 |= cfg->c_alg << SEC_BD_W1_C_ALG_S;
	req->w3 |= cfg->key_len << SEC_BD_W3_C_KEY_LEN_S;
	req->w0 |= cfg->c_width << SEC_BD_W0_C_WIDTH_S;

	req->cipher_key_addr_lo = lower_32_bits(ctx->pkey);
	req->cipher_key_addr_hi = upper_32_bits(ctx->pkey);
}

static void sec_alg_skcipher_init_context(struct crypto_skcipher *atfm,
					  const u8 *key,
					  unsigned int keylen,
					  enum sec_cipher_alg alg)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(atfm);
	struct sec_alg_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->cipher_alg = alg;
	memcpy(ctx->key, key, keylen);
	sec_alg_skcipher_init_template(ctx, &ctx->req_template,
				       ctx->cipher_alg);
}

static void sec_free_hw_sgl(struct sec_hw_sgl *hw_sgl,
			    dma_addr_t psec_sgl, struct sec_dev_info *info)
{
	struct sec_hw_sgl *sgl_current, *sgl_next;
	dma_addr_t sgl_next_dma;

	sgl_current = hw_sgl;
	while (sgl_current) {
		sgl_next = sgl_current->next;
		sgl_next_dma = sgl_current->next_sgl;

		dma_pool_free(info->hw_sgl_pool, sgl_current, psec_sgl);

		sgl_current = sgl_next;
		psec_sgl = sgl_next_dma;
	}
}

static int sec_alloc_and_fill_hw_sgl(struct sec_hw_sgl **sec_sgl,
				     dma_addr_t *psec_sgl,
				     struct scatterlist *sgl,
				     int count,
				     struct sec_dev_info *info,
				     gfp_t gfp)
{
	struct sec_hw_sgl *sgl_current = NULL;
	struct sec_hw_sgl *sgl_next;
	dma_addr_t sgl_next_dma;
	struct scatterlist *sg;
	int ret, sge_index, i;

	if (!count)
		return -EINVAL;

	for_each_sg(sgl, sg, count, i) {
		sge_index = i % SEC_MAX_SGE_NUM;
		if (sge_index == 0) {
			sgl_next = dma_pool_zalloc(info->hw_sgl_pool,
						   gfp, &sgl_next_dma);
			if (!sgl_next) {
				ret = -ENOMEM;
				goto err_free_hw_sgls;
			}

			if (!sgl_current) { /* First one */
				*psec_sgl = sgl_next_dma;
				*sec_sgl = sgl_next;
			} else { /* Chained */
				sgl_current->entry_sum_in_sgl = SEC_MAX_SGE_NUM;
				sgl_current->next_sgl = sgl_next_dma;
				sgl_current->next = sgl_next;
			}
			sgl_current = sgl_next;
		}
		sgl_current->sge_entries[sge_index].buf = sg_dma_address(sg);
		sgl_current->sge_entries[sge_index].len = sg_dma_len(sg);
		sgl_current->data_bytes_in_sgl += sg_dma_len(sg);
	}
	sgl_current->entry_sum_in_sgl = count % SEC_MAX_SGE_NUM;
	sgl_current->next_sgl = 0;
	(*sec_sgl)->entry_sum_in_chain = count;

	return 0;

err_free_hw_sgls:
	sec_free_hw_sgl(*sec_sgl, *psec_sgl, info);
	*psec_sgl = 0;

	return ret;
}

static int sec_alg_skcipher_setkey(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen,
				   enum sec_cipher_alg alg)
{
	struct sec_alg_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct device *dev = ctx->queue->dev_info->dev;

	mutex_lock(&ctx->lock);
	if (ctx->key) {
		/* rekeying */
		memset(ctx->key, 0, SEC_MAX_CIPHER_KEY);
	} else {
		/* new key */
		ctx->key = dma_alloc_coherent(dev, SEC_MAX_CIPHER_KEY,
					      &ctx->pkey, GFP_KERNEL);
		if (!ctx->key) {
			mutex_unlock(&ctx->lock);
			return -ENOMEM;
		}
	}
	mutex_unlock(&ctx->lock);
	sec_alg_skcipher_init_context(tfm, key, keylen, alg);

	return 0;
}

static int sec_alg_skcipher_setkey_aes_ecb(struct crypto_skcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	enum sec_cipher_alg alg;

	switch (keylen) {
	case AES_KEYSIZE_128:
		alg = SEC_C_AES_ECB_128;
		break;
	case AES_KEYSIZE_192:
		alg = SEC_C_AES_ECB_192;
		break;
	case AES_KEYSIZE_256:
		alg = SEC_C_AES_ECB_256;
		break;
	default:
		return -EINVAL;
	}

	return sec_alg_skcipher_setkey(tfm, key, keylen, alg);
}

static int sec_alg_skcipher_setkey_aes_cbc(struct crypto_skcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	enum sec_cipher_alg alg;

	switch (keylen) {
	case AES_KEYSIZE_128:
		alg = SEC_C_AES_CBC_128;
		break;
	case AES_KEYSIZE_192:
		alg = SEC_C_AES_CBC_192;
		break;
	case AES_KEYSIZE_256:
		alg = SEC_C_AES_CBC_256;
		break;
	default:
		return -EINVAL;
	}

	return sec_alg_skcipher_setkey(tfm, key, keylen, alg);
}

static int sec_alg_skcipher_setkey_aes_ctr(struct crypto_skcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	enum sec_cipher_alg alg;

	switch (keylen) {
	case AES_KEYSIZE_128:
		alg = SEC_C_AES_CTR_128;
		break;
	case AES_KEYSIZE_192:
		alg = SEC_C_AES_CTR_192;
		break;
	case AES_KEYSIZE_256:
		alg = SEC_C_AES_CTR_256;
		break;
	default:
		return -EINVAL;
	}

	return sec_alg_skcipher_setkey(tfm, key, keylen, alg);
}

static int sec_alg_skcipher_setkey_aes_xts(struct crypto_skcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	enum sec_cipher_alg alg;
	int ret;

	ret = xts_verify_key(tfm, key, keylen);
	if (ret)
		return ret;

	switch (keylen) {
	case AES_KEYSIZE_128 * 2:
		alg = SEC_C_AES_XTS_128;
		break;
	case AES_KEYSIZE_256 * 2:
		alg = SEC_C_AES_XTS_256;
		break;
	default:
		return -EINVAL;
	}

	return sec_alg_skcipher_setkey(tfm, key, keylen, alg);
}

static int sec_alg_skcipher_setkey_des_ecb(struct crypto_skcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des_key(tfm, key) ?:
	       sec_alg_skcipher_setkey(tfm, key, keylen, SEC_C_DES_ECB_64);
}

static int sec_alg_skcipher_setkey_des_cbc(struct crypto_skcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des_key(tfm, key) ?:
	       sec_alg_skcipher_setkey(tfm, key, keylen, SEC_C_DES_CBC_64);
}

static int sec_alg_skcipher_setkey_3des_ecb(struct crypto_skcipher *tfm,
					    const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des3_key(tfm, key) ?:
	       sec_alg_skcipher_setkey(tfm, key, keylen,
				       SEC_C_3DES_ECB_192_3KEY);
}

static int sec_alg_skcipher_setkey_3des_cbc(struct crypto_skcipher *tfm,
					    const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des3_key(tfm, key) ?:
	       sec_alg_skcipher_setkey(tfm, key, keylen,
				       SEC_C_3DES_CBC_192_3KEY);
}

static void sec_alg_free_el(struct sec_request_el *el,
			    struct sec_dev_info *info)
{
	sec_free_hw_sgl(el->out, el->dma_out, info);
	sec_free_hw_sgl(el->in, el->dma_in, info);
	kfree(el->sgl_in);
	kfree(el->sgl_out);
	kfree(el);
}

/* queuelock must be held */
static int sec_send_request(struct sec_request *sec_req, struct sec_queue *queue)
{
	struct sec_request_el *el, *temp;
	int ret = 0;

	mutex_lock(&sec_req->lock);
	list_for_each_entry_safe(el, temp, &sec_req->elements, head) {
		/*
		 * Add to hardware queue only under following circumstances
		 * 1) Software and hardware queue empty so no chain dependencies
		 * 2) No dependencies as new IV - (check software queue empty
		 *    to maintain order)
		 * 3) No dependencies because the mode does no chaining.
		 *
		 * In other cases first insert onto the software queue which
		 * is then emptied as requests complete
		 */
		if (!queue->havesoftqueue ||
		    (kfifo_is_empty(&queue->softqueue) &&
		     sec_queue_empty(queue))) {
			ret = sec_queue_send(queue, &el->req, sec_req);
			if (ret == -EAGAIN) {
				/* Wait unti we can send then try again */
				/* DEAD if here - should not happen */
				ret = -EBUSY;
				goto err_unlock;
			}
		} else {
			kfifo_put(&queue->softqueue, el);
		}
	}
err_unlock:
	mutex_unlock(&sec_req->lock);

	return ret;
}

static void sec_skcipher_alg_callback(struct sec_bd_info *sec_resp,
				      struct crypto_async_request *req_base)
{
	struct skcipher_request *skreq = container_of(req_base,
						      struct skcipher_request,
						      base);
	struct sec_request *sec_req = skcipher_request_ctx(skreq);
	struct sec_request *backlog_req;
	struct sec_request_el *sec_req_el, *nextrequest;
	struct sec_alg_tfm_ctx *ctx = sec_req->tfm_ctx;
	struct crypto_skcipher *atfm = crypto_skcipher_reqtfm(skreq);
	struct device *dev = ctx->queue->dev_info->dev;
	int icv_or_skey_en, ret;
	bool done;

	sec_req_el = list_first_entry(&sec_req->elements, struct sec_request_el,
				      head);
	icv_or_skey_en = (sec_resp->w0 & SEC_BD_W0_ICV_OR_SKEY_EN_M) >>
		SEC_BD_W0_ICV_OR_SKEY_EN_S;
	if (sec_resp->w1 & SEC_BD_W1_BD_INVALID || icv_or_skey_en == 3) {
		dev_err(dev, "Got an invalid answer %lu %d\n",
			sec_resp->w1 & SEC_BD_W1_BD_INVALID,
			icv_or_skey_en);
		sec_req->err = -EINVAL;
		/*
		 * We need to muddle on to avoid getting stuck with elements
		 * on the queue. Error will be reported so requester so
		 * it should be able to handle appropriately.
		 */
	}

	mutex_lock(&ctx->queue->queuelock);
	/* Put the IV in place for chained cases */
	switch (ctx->cipher_alg) {
	case SEC_C_AES_CBC_128:
	case SEC_C_AES_CBC_192:
	case SEC_C_AES_CBC_256:
		if (sec_req_el->req.w0 & SEC_BD_W0_DE)
			sg_pcopy_to_buffer(sec_req_el->sgl_out,
					   sg_nents(sec_req_el->sgl_out),
					   skreq->iv,
					   crypto_skcipher_ivsize(atfm),
					   sec_req_el->el_length -
					   crypto_skcipher_ivsize(atfm));
		else
			sg_pcopy_to_buffer(sec_req_el->sgl_in,
					   sg_nents(sec_req_el->sgl_in),
					   skreq->iv,
					   crypto_skcipher_ivsize(atfm),
					   sec_req_el->el_length -
					   crypto_skcipher_ivsize(atfm));
		/* No need to sync to the device as coherent DMA */
		break;
	case SEC_C_AES_CTR_128:
	case SEC_C_AES_CTR_192:
	case SEC_C_AES_CTR_256:
		crypto_inc(skreq->iv, 16);
		break;
	default:
		/* Do not update */
		break;
	}

	if (ctx->queue->havesoftqueue &&
	    !kfifo_is_empty(&ctx->queue->softqueue) &&
	    sec_queue_empty(ctx->queue)) {
		ret = kfifo_get(&ctx->queue->softqueue, &nextrequest);
		if (ret <= 0)
			dev_err(dev,
				"Error getting next element from kfifo %d\n",
				ret);
		else
			/* We know there is space so this cannot fail */
			sec_queue_send(ctx->queue, &nextrequest->req,
				       nextrequest->sec_req);
	} else if (!list_empty(&ctx->backlog)) {
		/* Need to verify there is room first */
		backlog_req = list_first_entry(&ctx->backlog,
					       typeof(*backlog_req),
					       backlog_head);
		if (sec_queue_can_enqueue(ctx->queue,
		    backlog_req->num_elements) ||
		    (ctx->queue->havesoftqueue &&
		     kfifo_avail(&ctx->queue->softqueue) >
		     backlog_req->num_elements)) {
			sec_send_request(backlog_req, ctx->queue);
			backlog_req->req_base->complete(backlog_req->req_base,
							-EINPROGRESS);
			list_del(&backlog_req->backlog_head);
		}
	}
	mutex_unlock(&ctx->queue->queuelock);

	mutex_lock(&sec_req->lock);
	list_del(&sec_req_el->head);
	mutex_unlock(&sec_req->lock);
	sec_alg_free_el(sec_req_el, ctx->queue->dev_info);

	/*
	 * Request is done.
	 * The dance is needed as the lock is freed in the completion
	 */
	mutex_lock(&sec_req->lock);
	done = list_empty(&sec_req->elements);
	mutex_unlock(&sec_req->lock);
	if (done) {
		if (crypto_skcipher_ivsize(atfm)) {
			dma_unmap_single(dev, sec_req->dma_iv,
					 crypto_skcipher_ivsize(atfm),
					 DMA_TO_DEVICE);
		}
		dma_unmap_sg(dev, skreq->src, sec_req->len_in,
			     DMA_BIDIRECTIONAL);
		if (skreq->src != skreq->dst)
			dma_unmap_sg(dev, skreq->dst, sec_req->len_out,
				     DMA_BIDIRECTIONAL);
		skreq->base.complete(&skreq->base, sec_req->err);
	}
}

void sec_alg_callback(struct sec_bd_info *resp, void *shadow)
{
	struct sec_request *sec_req = shadow;

	sec_req->cb(resp, sec_req->req_base);
}

static int sec_alg_alloc_and_calc_split_sizes(int length, size_t **split_sizes,
					      int *steps, gfp_t gfp)
{
	size_t *sizes;
	int i;

	/* Split into suitable sized blocks */
	*steps = roundup(length, SEC_REQ_LIMIT) / SEC_REQ_LIMIT;
	sizes = kcalloc(*steps, sizeof(*sizes), gfp);
	if (!sizes)
		return -ENOMEM;

	for (i = 0; i < *steps - 1; i++)
		sizes[i] = SEC_REQ_LIMIT;
	sizes[*steps - 1] = length - SEC_REQ_LIMIT * (*steps - 1);
	*split_sizes = sizes;

	return 0;
}

static int sec_map_and_split_sg(struct scatterlist *sgl, size_t *split_sizes,
				int steps, struct scatterlist ***splits,
				int **splits_nents,
				int sgl_len_in,
				struct device *dev, gfp_t gfp)
{
	int ret, count;

	count = dma_map_sg(dev, sgl, sgl_len_in, DMA_BIDIRECTIONAL);
	if (!count)
		return -EINVAL;

	*splits = kcalloc(steps, sizeof(struct scatterlist *), gfp);
	if (!*splits) {
		ret = -ENOMEM;
		goto err_unmap_sg;
	}
	*splits_nents = kcalloc(steps, sizeof(int), gfp);
	if (!*splits_nents) {
		ret = -ENOMEM;
		goto err_free_splits;
	}

	/* output the scatter list before and after this */
	ret = sg_split(sgl, count, 0, steps, split_sizes,
		       *splits, *splits_nents, gfp);
	if (ret) {
		ret = -ENOMEM;
		goto err_free_splits_nents;
	}

	return 0;

err_free_splits_nents:
	kfree(*splits_nents);
err_free_splits:
	kfree(*splits);
err_unmap_sg:
	dma_unmap_sg(dev, sgl, sgl_len_in, DMA_BIDIRECTIONAL);

	return ret;
}

/*
 * Reverses the sec_map_and_split_sg call for messages not yet added to
 * the queues.
 */
static void sec_unmap_sg_on_err(struct scatterlist *sgl, int steps,
				struct scatterlist **splits, int *splits_nents,
				int sgl_len_in, struct device *dev)
{
	int i;

	for (i = 0; i < steps; i++)
		kfree(splits[i]);
	kfree(splits_nents);
	kfree(splits);

	dma_unmap_sg(dev, sgl, sgl_len_in, DMA_BIDIRECTIONAL);
}

static struct sec_request_el
*sec_alg_alloc_and_fill_el(struct sec_bd_info *template, int encrypt,
			   int el_size, bool different_dest,
			   struct scatterlist *sgl_in, int n_ents_in,
			   struct scatterlist *sgl_out, int n_ents_out,
			   struct sec_dev_info *info, gfp_t gfp)
{
	struct sec_request_el *el;
	struct sec_bd_info *req;
	int ret;

	el = kzalloc(sizeof(*el), gfp);
	if (!el)
		return ERR_PTR(-ENOMEM);
	el->el_length = el_size;
	req = &el->req;
	memcpy(req, template, sizeof(*req));

	req->w0 &= ~SEC_BD_W0_CIPHER_M;
	if (encrypt)
		req->w0 |= SEC_CIPHER_ENCRYPT << SEC_BD_W0_CIPHER_S;
	else
		req->w0 |= SEC_CIPHER_DECRYPT << SEC_BD_W0_CIPHER_S;

	req->w0 &= ~SEC_BD_W0_C_GRAN_SIZE_19_16_M;
	req->w0 |= ((el_size >> 16) << SEC_BD_W0_C_GRAN_SIZE_19_16_S) &
		SEC_BD_W0_C_GRAN_SIZE_19_16_M;

	req->w0 &= ~SEC_BD_W0_C_GRAN_SIZE_21_20_M;
	req->w0 |= ((el_size >> 20) << SEC_BD_W0_C_GRAN_SIZE_21_20_S) &
		SEC_BD_W0_C_GRAN_SIZE_21_20_M;

	/* Writing whole u32 so no need to take care of masking */
	req->w2 = ((1 << SEC_BD_W2_GRAN_NUM_S) & SEC_BD_W2_GRAN_NUM_M) |
		((el_size << SEC_BD_W2_C_GRAN_SIZE_15_0_S) &
		 SEC_BD_W2_C_GRAN_SIZE_15_0_M);

	req->w3 &= ~SEC_BD_W3_CIPHER_LEN_OFFSET_M;
	req->w1 |= SEC_BD_W1_ADDR_TYPE;

	el->sgl_in = sgl_in;

	ret = sec_alloc_and_fill_hw_sgl(&el->in, &el->dma_in, el->sgl_in,
					n_ents_in, info, gfp);
	if (ret)
		goto err_free_el;

	req->data_addr_lo = lower_32_bits(el->dma_in);
	req->data_addr_hi = upper_32_bits(el->dma_in);

	if (different_dest) {
		el->sgl_out = sgl_out;
		ret = sec_alloc_and_fill_hw_sgl(&el->out, &el->dma_out,
						el->sgl_out,
						n_ents_out, info, gfp);
		if (ret)
			goto err_free_hw_sgl_in;

		req->w0 |= SEC_BD_W0_DE;
		req->cipher_destin_addr_lo = lower_32_bits(el->dma_out);
		req->cipher_destin_addr_hi = upper_32_bits(el->dma_out);

	} else {
		req->w0 &= ~SEC_BD_W0_DE;
		req->cipher_destin_addr_lo = lower_32_bits(el->dma_in);
		req->cipher_destin_addr_hi = upper_32_bits(el->dma_in);
	}

	return el;

err_free_hw_sgl_in:
	sec_free_hw_sgl(el->in, el->dma_in, info);
err_free_el:
	kfree(el);

	return ERR_PTR(ret);
}

static int sec_alg_skcipher_crypto(struct skcipher_request *skreq,
				   bool encrypt)
{
	struct crypto_skcipher *atfm = crypto_skcipher_reqtfm(skreq);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(atfm);
	struct sec_alg_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct sec_queue *queue = ctx->queue;
	struct sec_request *sec_req = skcipher_request_ctx(skreq);
	struct sec_dev_info *info = queue->dev_info;
	int i, ret, steps;
	size_t *split_sizes;
	struct scatterlist **splits_in;
	struct scatterlist **splits_out = NULL;
	int *splits_in_nents;
	int *splits_out_nents = NULL;
	struct sec_request_el *el, *temp;
	bool split = skreq->src != skreq->dst;
	gfp_t gfp = skreq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL : GFP_ATOMIC;

	mutex_init(&sec_req->lock);
	sec_req->req_base = &skreq->base;
	sec_req->err = 0;
	/* SGL mapping out here to allow us to break it up as necessary */
	sec_req->len_in = sg_nents(skreq->src);

	ret = sec_alg_alloc_and_calc_split_sizes(skreq->cryptlen, &split_sizes,
						 &steps, gfp);
	if (ret)
		return ret;
	sec_req->num_elements = steps;
	ret = sec_map_and_split_sg(skreq->src, split_sizes, steps, &splits_in,
				   &splits_in_nents, sec_req->len_in,
				   info->dev, gfp);
	if (ret)
		goto err_free_split_sizes;

	if (split) {
		sec_req->len_out = sg_nents(skreq->dst);
		ret = sec_map_and_split_sg(skreq->dst, split_sizes, steps,
					   &splits_out, &splits_out_nents,
					   sec_req->len_out, info->dev, gfp);
		if (ret)
			goto err_unmap_in_sg;
	}
	/* Shared info stored in seq_req - applies to all BDs */
	sec_req->tfm_ctx = ctx;
	sec_req->cb = sec_skcipher_alg_callback;
	INIT_LIST_HEAD(&sec_req->elements);

	/*
	 * Future optimization.
	 * In the chaining case we can't use a dma pool bounce buffer
	 * but in the case where we know there is no chaining we can
	 */
	if (crypto_skcipher_ivsize(atfm)) {
		sec_req->dma_iv = dma_map_single(info->dev, skreq->iv,
						 crypto_skcipher_ivsize(atfm),
						 DMA_TO_DEVICE);
		if (dma_mapping_error(info->dev, sec_req->dma_iv)) {
			ret = -ENOMEM;
			goto err_unmap_out_sg;
		}
	}

	/* Set them all up then queue - cleaner error handling. */
	for (i = 0; i < steps; i++) {
		el = sec_alg_alloc_and_fill_el(&ctx->req_template,
					       encrypt ? 1 : 0,
					       split_sizes[i],
					       skreq->src != skreq->dst,
					       splits_in[i], splits_in_nents[i],
					       split ? splits_out[i] : NULL,
					       split ? splits_out_nents[i] : 0,
					       info, gfp);
		if (IS_ERR(el)) {
			ret = PTR_ERR(el);
			goto err_free_elements;
		}
		el->req.cipher_iv_addr_lo = lower_32_bits(sec_req->dma_iv);
		el->req.cipher_iv_addr_hi = upper_32_bits(sec_req->dma_iv);
		el->sec_req = sec_req;
		list_add_tail(&el->head, &sec_req->elements);
	}

	/*
	 * Only attempt to queue if the whole lot can fit in the queue -
	 * we can't successfully cleanup after a partial queing so this
	 * must succeed or fail atomically.
	 *
	 * Big hammer test of both software and hardware queues - could be
	 * more refined but this is unlikely to happen so no need.
	 */

	/* Grab a big lock for a long time to avoid concurrency issues */
	mutex_lock(&queue->queuelock);

	/*
	 * Can go on to queue if we have space in either:
	 * 1) The hardware queue and no software queue
	 * 2) The software queue
	 * AND there is nothing in the backlog.  If there is backlog we
	 * have to only queue to the backlog queue and return busy.
	 */
	if ((!sec_queue_can_enqueue(queue, steps) &&
	     (!queue->havesoftqueue ||
	      kfifo_avail(&queue->softqueue) > steps)) ||
	    !list_empty(&ctx->backlog)) {
		ret = -EBUSY;
		if ((skreq->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
			list_add_tail(&sec_req->backlog_head, &ctx->backlog);
			mutex_unlock(&queue->queuelock);
			goto out;
		}

		mutex_unlock(&queue->queuelock);
		goto err_free_elements;
	}
	ret = sec_send_request(sec_req, queue);
	mutex_unlock(&queue->queuelock);
	if (ret)
		goto err_free_elements;

	ret = -EINPROGRESS;
out:
	/* Cleanup - all elements in pointer arrays have been copied */
	kfree(splits_in_nents);
	kfree(splits_in);
	kfree(splits_out_nents);
	kfree(splits_out);
	kfree(split_sizes);
	return ret;

err_free_elements:
	list_for_each_entry_safe(el, temp, &sec_req->elements, head) {
		list_del(&el->head);
		sec_alg_free_el(el, info);
	}
	if (crypto_skcipher_ivsize(atfm))
		dma_unmap_single(info->dev, sec_req->dma_iv,
				 crypto_skcipher_ivsize(atfm),
				 DMA_BIDIRECTIONAL);
err_unmap_out_sg:
	if (split)
		sec_unmap_sg_on_err(skreq->dst, steps, splits_out,
				    splits_out_nents, sec_req->len_out,
				    info->dev);
err_unmap_in_sg:
	sec_unmap_sg_on_err(skreq->src, steps, splits_in, splits_in_nents,
			    sec_req->len_in, info->dev);
err_free_split_sizes:
	kfree(split_sizes);

	return ret;
}

static int sec_alg_skcipher_encrypt(struct skcipher_request *req)
{
	return sec_alg_skcipher_crypto(req, true);
}

static int sec_alg_skcipher_decrypt(struct skcipher_request *req)
{
	return sec_alg_skcipher_crypto(req, false);
}

static int sec_alg_skcipher_init(struct crypto_skcipher *tfm)
{
	struct sec_alg_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);

	mutex_init(&ctx->lock);
	INIT_LIST_HEAD(&ctx->backlog);
	crypto_skcipher_set_reqsize(tfm, sizeof(struct sec_request));

	ctx->queue = sec_queue_alloc_start_safe();
	if (IS_ERR(ctx->queue))
		return PTR_ERR(ctx->queue);

	mutex_init(&ctx->queue->queuelock);
	ctx->queue->havesoftqueue = false;

	return 0;
}

static void sec_alg_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct sec_alg_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct device *dev = ctx->queue->dev_info->dev;

	if (ctx->key) {
		memzero_explicit(ctx->key, SEC_MAX_CIPHER_KEY);
		dma_free_coherent(dev, SEC_MAX_CIPHER_KEY, ctx->key,
				  ctx->pkey);
	}
	sec_queue_stop_release(ctx->queue);
}

static int sec_alg_skcipher_init_with_queue(struct crypto_skcipher *tfm)
{
	struct sec_alg_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = sec_alg_skcipher_init(tfm);
	if (ret)
		return ret;

	INIT_KFIFO(ctx->queue->softqueue);
	ret = kfifo_alloc(&ctx->queue->softqueue, 512, GFP_KERNEL);
	if (ret) {
		sec_alg_skcipher_exit(tfm);
		return ret;
	}
	ctx->queue->havesoftqueue = true;

	return 0;
}

static void sec_alg_skcipher_exit_with_queue(struct crypto_skcipher *tfm)
{
	struct sec_alg_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);

	kfifo_free(&ctx->queue->softqueue);
	sec_alg_skcipher_exit(tfm);
}

static struct skcipher_alg sec_algs[] = {
	{
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "hisi_sec_aes_ecb",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init,
		.exit = sec_alg_skcipher_exit,
		.setkey = sec_alg_skcipher_setkey_aes_ecb,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = 0,
	}, {
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "hisi_sec_aes_cbc",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init_with_queue,
		.exit = sec_alg_skcipher_exit_with_queue,
		.setkey = sec_alg_skcipher_setkey_aes_cbc,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
	}, {
		.base = {
			.cra_name = "ctr(aes)",
			.cra_driver_name = "hisi_sec_aes_ctr",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init_with_queue,
		.exit = sec_alg_skcipher_exit_with_queue,
		.setkey = sec_alg_skcipher_setkey_aes_ctr,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
	}, {
		.base = {
			.cra_name = "xts(aes)",
			.cra_driver_name = "hisi_sec_aes_xts",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init,
		.exit = sec_alg_skcipher_exit,
		.setkey = sec_alg_skcipher_setkey_aes_xts,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = 2 * AES_MIN_KEY_SIZE,
		.max_keysize = 2 * AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
	}, {
	/* Unable to find any test vectors so untested */
		.base = {
			.cra_name = "ecb(des)",
			.cra_driver_name = "hisi_sec_des_ecb",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init,
		.exit = sec_alg_skcipher_exit,
		.setkey = sec_alg_skcipher_setkey_des_ecb,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = DES_KEY_SIZE,
		.max_keysize = DES_KEY_SIZE,
		.ivsize = 0,
	}, {
		.base = {
			.cra_name = "cbc(des)",
			.cra_driver_name = "hisi_sec_des_cbc",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init_with_queue,
		.exit = sec_alg_skcipher_exit_with_queue,
		.setkey = sec_alg_skcipher_setkey_des_cbc,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = DES_KEY_SIZE,
		.max_keysize = DES_KEY_SIZE,
		.ivsize = DES_BLOCK_SIZE,
	}, {
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "hisi_sec_3des_cbc",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init_with_queue,
		.exit = sec_alg_skcipher_exit_with_queue,
		.setkey = sec_alg_skcipher_setkey_3des_cbc,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
		.ivsize = DES3_EDE_BLOCK_SIZE,
	}, {
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "hisi_sec_3des_ecb",
			.cra_priority = 4001,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct sec_alg_tfm_ctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = sec_alg_skcipher_init,
		.exit = sec_alg_skcipher_exit,
		.setkey = sec_alg_skcipher_setkey_3des_ecb,
		.decrypt = sec_alg_skcipher_decrypt,
		.encrypt = sec_alg_skcipher_encrypt,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
		.ivsize = 0,
	}
};

int sec_algs_register(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs != 1)
		goto unlock;

	ret = crypto_register_skciphers(sec_algs, ARRAY_SIZE(sec_algs));
	if (ret)
		--active_devs;
unlock:
	mutex_unlock(&algs_lock);

	return ret;
}

void sec_algs_unregister(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs != 0)
		goto unlock;
	crypto_unregister_skciphers(sec_algs, ARRAY_SIZE(sec_algs));

unlock:
	mutex_unlock(&algs_lock);
}
