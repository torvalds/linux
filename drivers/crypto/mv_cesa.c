/*
 * Support for Marvell's crypto engine which can be found on some Orion5X
 * boards.
 *
 * Author: Sebastian Andrzej Siewior < sebastian at breakpoint dot cc >
 * License: GPLv2
 *
 */
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <crypto/internal/hash.h>
#include <crypto/sha.h>

#include "mv_cesa.h"

#define MV_CESA	"MV-CESA:"
#define MAX_HW_HASH_SIZE	0xFFFF

/*
 * STM:
 *   /---------------------------------------\
 *   |					     | request complete
 *  \./					     |
 * IDLE -> new request -> BUSY -> done -> DEQUEUE
 *                         /°\               |
 *			    |		     | more scatter entries
 *			    \________________/
 */
enum engine_status {
	ENGINE_IDLE,
	ENGINE_BUSY,
	ENGINE_W_DEQUEUE,
};

/**
 * struct req_progress - used for every crypt request
 * @src_sg_it:		sg iterator for src
 * @dst_sg_it:		sg iterator for dst
 * @sg_src_left:	bytes left in src to process (scatter list)
 * @src_start:		offset to add to src start position (scatter list)
 * @crypt_len:		length of current hw crypt/hash process
 * @hw_nbytes:		total bytes to process in hw for this request
 * @copy_back:		whether to copy data back (crypt) or not (hash)
 * @sg_dst_left:	bytes left dst to process in this scatter list
 * @dst_start:		offset to add to dst start position (scatter list)
 * @hw_processed_bytes:	number of bytes processed by hw (request).
 *
 * sg helper are used to iterate over the scatterlist. Since the size of the
 * SRAM may be less than the scatter size, this struct struct is used to keep
 * track of progress within current scatterlist.
 */
struct req_progress {
	struct sg_mapping_iter src_sg_it;
	struct sg_mapping_iter dst_sg_it;
	void (*complete) (void);
	void (*process) (int is_first);

	/* src mostly */
	int sg_src_left;
	int src_start;
	int crypt_len;
	int hw_nbytes;
	/* dst mostly */
	int copy_back;
	int sg_dst_left;
	int dst_start;
	int hw_processed_bytes;
};

struct crypto_priv {
	void __iomem *reg;
	void __iomem *sram;
	int irq;
	struct clk *clk;
	struct task_struct *queue_th;

	/* the lock protects queue and eng_st */
	spinlock_t lock;
	struct crypto_queue queue;
	enum engine_status eng_st;
	struct crypto_async_request *cur_req;
	struct req_progress p;
	int max_req_size;
	int sram_size;
	int has_sha1;
	int has_hmac_sha1;
};

static struct crypto_priv *cpg;

struct mv_ctx {
	u8 aes_enc_key[AES_KEY_LEN];
	u32 aes_dec_key[8];
	int key_len;
	u32 need_calc_aes_dkey;
};

enum crypto_op {
	COP_AES_ECB,
	COP_AES_CBC,
};

struct mv_req_ctx {
	enum crypto_op op;
	int decrypt;
};

enum hash_op {
	COP_SHA1,
	COP_HMAC_SHA1
};

struct mv_tfm_hash_ctx {
	struct crypto_shash *fallback;
	struct crypto_shash *base_hash;
	u32 ivs[2 * SHA1_DIGEST_SIZE / 4];
	int count_add;
	enum hash_op op;
};

struct mv_req_hash_ctx {
	u64 count;
	u32 state[SHA1_DIGEST_SIZE / 4];
	u8 buffer[SHA1_BLOCK_SIZE];
	int first_hash;		/* marks that we don't have previous state */
	int last_chunk;		/* marks that this is the 'final' request */
	int extra_bytes;	/* unprocessed bytes in buffer */
	enum hash_op op;
	int count_add;
};

static void compute_aes_dec_key(struct mv_ctx *ctx)
{
	struct crypto_aes_ctx gen_aes_key;
	int key_pos;

	if (!ctx->need_calc_aes_dkey)
		return;

	crypto_aes_expand_key(&gen_aes_key, ctx->aes_enc_key, ctx->key_len);

	key_pos = ctx->key_len + 24;
	memcpy(ctx->aes_dec_key, &gen_aes_key.key_enc[key_pos], 4 * 4);
	switch (ctx->key_len) {
	case AES_KEYSIZE_256:
		key_pos -= 2;
		/* fall */
	case AES_KEYSIZE_192:
		key_pos -= 2;
		memcpy(&ctx->aes_dec_key[4], &gen_aes_key.key_enc[key_pos],
				4 * 4);
		break;
	}
	ctx->need_calc_aes_dkey = 0;
}

static int mv_setkey_aes(struct crypto_ablkcipher *cipher, const u8 *key,
		unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct mv_ctx *ctx = crypto_tfm_ctx(tfm);

	switch (len) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_256:
		break;
	default:
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	ctx->key_len = len;
	ctx->need_calc_aes_dkey = 1;

	memcpy(ctx->aes_enc_key, key, AES_KEY_LEN);
	return 0;
}

static void copy_src_to_buf(struct req_progress *p, char *dbuf, int len)
{
	int ret;
	void *sbuf;
	int copy_len;

	while (len) {
		if (!p->sg_src_left) {
			ret = sg_miter_next(&p->src_sg_it);
			BUG_ON(!ret);
			p->sg_src_left = p->src_sg_it.length;
			p->src_start = 0;
		}

		sbuf = p->src_sg_it.addr + p->src_start;

		copy_len = min(p->sg_src_left, len);
		memcpy(dbuf, sbuf, copy_len);

		p->src_start += copy_len;
		p->sg_src_left -= copy_len;

		len -= copy_len;
		dbuf += copy_len;
	}
}

static void setup_data_in(void)
{
	struct req_progress *p = &cpg->p;
	int data_in_sram =
	    min(p->hw_nbytes - p->hw_processed_bytes, cpg->max_req_size);
	copy_src_to_buf(p, cpg->sram + SRAM_DATA_IN_START + p->crypt_len,
			data_in_sram - p->crypt_len);
	p->crypt_len = data_in_sram;
}

static void mv_process_current_q(int first_block)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(cpg->cur_req);
	struct mv_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	struct sec_accel_config op;

	switch (req_ctx->op) {
	case COP_AES_ECB:
		op.config = CFG_OP_CRYPT_ONLY | CFG_ENCM_AES | CFG_ENC_MODE_ECB;
		break;
	case COP_AES_CBC:
	default:
		op.config = CFG_OP_CRYPT_ONLY | CFG_ENCM_AES | CFG_ENC_MODE_CBC;
		op.enc_iv = ENC_IV_POINT(SRAM_DATA_IV) |
			ENC_IV_BUF_POINT(SRAM_DATA_IV_BUF);
		if (first_block)
			memcpy(cpg->sram + SRAM_DATA_IV, req->info, 16);
		break;
	}
	if (req_ctx->decrypt) {
		op.config |= CFG_DIR_DEC;
		memcpy(cpg->sram + SRAM_DATA_KEY_P, ctx->aes_dec_key,
				AES_KEY_LEN);
	} else {
		op.config |= CFG_DIR_ENC;
		memcpy(cpg->sram + SRAM_DATA_KEY_P, ctx->aes_enc_key,
				AES_KEY_LEN);
	}

	switch (ctx->key_len) {
	case AES_KEYSIZE_128:
		op.config |= CFG_AES_LEN_128;
		break;
	case AES_KEYSIZE_192:
		op.config |= CFG_AES_LEN_192;
		break;
	case AES_KEYSIZE_256:
		op.config |= CFG_AES_LEN_256;
		break;
	}
	op.enc_p = ENC_P_SRC(SRAM_DATA_IN_START) |
		ENC_P_DST(SRAM_DATA_OUT_START);
	op.enc_key_p = SRAM_DATA_KEY_P;

	setup_data_in();
	op.enc_len = cpg->p.crypt_len;
	memcpy(cpg->sram + SRAM_CONFIG, &op,
			sizeof(struct sec_accel_config));

	/* GO */
	writel(SEC_CMD_EN_SEC_ACCL0, cpg->reg + SEC_ACCEL_CMD);

	/*
	 * XXX: add timer if the interrupt does not occur for some mystery
	 * reason
	 */
}

static void mv_crypto_algo_completion(void)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(cpg->cur_req);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	sg_miter_stop(&cpg->p.src_sg_it);
	sg_miter_stop(&cpg->p.dst_sg_it);

	if (req_ctx->op != COP_AES_CBC)
		return ;

	memcpy(req->info, cpg->sram + SRAM_DATA_IV_BUF, 16);
}

static void mv_process_hash_current(int first_block)
{
	struct ahash_request *req = ahash_request_cast(cpg->cur_req);
	const struct mv_tfm_hash_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_hash_ctx *req_ctx = ahash_request_ctx(req);
	struct req_progress *p = &cpg->p;
	struct sec_accel_config op = { 0 };
	int is_last;

	switch (req_ctx->op) {
	case COP_SHA1:
	default:
		op.config = CFG_OP_MAC_ONLY | CFG_MACM_SHA1;
		break;
	case COP_HMAC_SHA1:
		op.config = CFG_OP_MAC_ONLY | CFG_MACM_HMAC_SHA1;
		memcpy(cpg->sram + SRAM_HMAC_IV_IN,
				tfm_ctx->ivs, sizeof(tfm_ctx->ivs));
		break;
	}

	op.mac_src_p =
		MAC_SRC_DATA_P(SRAM_DATA_IN_START) | MAC_SRC_TOTAL_LEN((u32)
		req_ctx->
		count);

	setup_data_in();

	op.mac_digest =
		MAC_DIGEST_P(SRAM_DIGEST_BUF) | MAC_FRAG_LEN(p->crypt_len);
	op.mac_iv =
		MAC_INNER_IV_P(SRAM_HMAC_IV_IN) |
		MAC_OUTER_IV_P(SRAM_HMAC_IV_OUT);

	is_last = req_ctx->last_chunk
		&& (p->hw_processed_bytes + p->crypt_len >= p->hw_nbytes)
		&& (req_ctx->count <= MAX_HW_HASH_SIZE);
	if (req_ctx->first_hash) {
		if (is_last)
			op.config |= CFG_NOT_FRAG;
		else
			op.config |= CFG_FIRST_FRAG;

		req_ctx->first_hash = 0;
	} else {
		if (is_last)
			op.config |= CFG_LAST_FRAG;
		else
			op.config |= CFG_MID_FRAG;

		if (first_block) {
			writel(req_ctx->state[0], cpg->reg + DIGEST_INITIAL_VAL_A);
			writel(req_ctx->state[1], cpg->reg + DIGEST_INITIAL_VAL_B);
			writel(req_ctx->state[2], cpg->reg + DIGEST_INITIAL_VAL_C);
			writel(req_ctx->state[3], cpg->reg + DIGEST_INITIAL_VAL_D);
			writel(req_ctx->state[4], cpg->reg + DIGEST_INITIAL_VAL_E);
		}
	}

	memcpy(cpg->sram + SRAM_CONFIG, &op, sizeof(struct sec_accel_config));

	/* GO */
	writel(SEC_CMD_EN_SEC_ACCL0, cpg->reg + SEC_ACCEL_CMD);

	/*
	* XXX: add timer if the interrupt does not occur for some mystery
	* reason
	*/
}

static inline int mv_hash_import_sha1_ctx(const struct mv_req_hash_ctx *ctx,
					  struct shash_desc *desc)
{
	int i;
	struct sha1_state shash_state;

	shash_state.count = ctx->count + ctx->count_add;
	for (i = 0; i < 5; i++)
		shash_state.state[i] = ctx->state[i];
	memcpy(shash_state.buffer, ctx->buffer, sizeof(shash_state.buffer));
	return crypto_shash_import(desc, &shash_state);
}

static int mv_hash_final_fallback(struct ahash_request *req)
{
	const struct mv_tfm_hash_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_hash_ctx *req_ctx = ahash_request_ctx(req);
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(tfm_ctx->fallback)];
	} desc;
	int rc;

	desc.shash.tfm = tfm_ctx->fallback;
	desc.shash.flags = CRYPTO_TFM_REQ_MAY_SLEEP;
	if (unlikely(req_ctx->first_hash)) {
		crypto_shash_init(&desc.shash);
		crypto_shash_update(&desc.shash, req_ctx->buffer,
				    req_ctx->extra_bytes);
	} else {
		/* only SHA1 for now....
		 */
		rc = mv_hash_import_sha1_ctx(req_ctx, &desc.shash);
		if (rc)
			goto out;
	}
	rc = crypto_shash_final(&desc.shash, req->result);
out:
	return rc;
}

static void mv_hash_algo_completion(void)
{
	struct ahash_request *req = ahash_request_cast(cpg->cur_req);
	struct mv_req_hash_ctx *ctx = ahash_request_ctx(req);

	if (ctx->extra_bytes)
		copy_src_to_buf(&cpg->p, ctx->buffer, ctx->extra_bytes);
	sg_miter_stop(&cpg->p.src_sg_it);

	if (likely(ctx->last_chunk)) {
		if (likely(ctx->count <= MAX_HW_HASH_SIZE)) {
			memcpy(req->result, cpg->sram + SRAM_DIGEST_BUF,
			       crypto_ahash_digestsize(crypto_ahash_reqtfm
						       (req)));
		} else
			mv_hash_final_fallback(req);
	} else {
		ctx->state[0] = readl(cpg->reg + DIGEST_INITIAL_VAL_A);
		ctx->state[1] = readl(cpg->reg + DIGEST_INITIAL_VAL_B);
		ctx->state[2] = readl(cpg->reg + DIGEST_INITIAL_VAL_C);
		ctx->state[3] = readl(cpg->reg + DIGEST_INITIAL_VAL_D);
		ctx->state[4] = readl(cpg->reg + DIGEST_INITIAL_VAL_E);
	}
}

static void dequeue_complete_req(void)
{
	struct crypto_async_request *req = cpg->cur_req;
	void *buf;
	int ret;
	cpg->p.hw_processed_bytes += cpg->p.crypt_len;
	if (cpg->p.copy_back) {
		int need_copy_len = cpg->p.crypt_len;
		int sram_offset = 0;
		do {
			int dst_copy;

			if (!cpg->p.sg_dst_left) {
				ret = sg_miter_next(&cpg->p.dst_sg_it);
				BUG_ON(!ret);
				cpg->p.sg_dst_left = cpg->p.dst_sg_it.length;
				cpg->p.dst_start = 0;
			}

			buf = cpg->p.dst_sg_it.addr;
			buf += cpg->p.dst_start;

			dst_copy = min(need_copy_len, cpg->p.sg_dst_left);

			memcpy(buf,
			       cpg->sram + SRAM_DATA_OUT_START + sram_offset,
			       dst_copy);
			sram_offset += dst_copy;
			cpg->p.sg_dst_left -= dst_copy;
			need_copy_len -= dst_copy;
			cpg->p.dst_start += dst_copy;
		} while (need_copy_len > 0);
	}

	cpg->p.crypt_len = 0;

	BUG_ON(cpg->eng_st != ENGINE_W_DEQUEUE);
	if (cpg->p.hw_processed_bytes < cpg->p.hw_nbytes) {
		/* process next scatter list entry */
		cpg->eng_st = ENGINE_BUSY;
		cpg->p.process(0);
	} else {
		cpg->p.complete();
		cpg->eng_st = ENGINE_IDLE;
		local_bh_disable();
		req->complete(req, 0);
		local_bh_enable();
	}
}

static int count_sgs(struct scatterlist *sl, unsigned int total_bytes)
{
	int i = 0;
	size_t cur_len;

	while (sl) {
		cur_len = sl[i].length;
		++i;
		if (total_bytes > cur_len)
			total_bytes -= cur_len;
		else
			break;
	}

	return i;
}

static void mv_start_new_crypt_req(struct ablkcipher_request *req)
{
	struct req_progress *p = &cpg->p;
	int num_sgs;

	cpg->cur_req = &req->base;
	memset(p, 0, sizeof(struct req_progress));
	p->hw_nbytes = req->nbytes;
	p->complete = mv_crypto_algo_completion;
	p->process = mv_process_current_q;
	p->copy_back = 1;

	num_sgs = count_sgs(req->src, req->nbytes);
	sg_miter_start(&p->src_sg_it, req->src, num_sgs, SG_MITER_FROM_SG);

	num_sgs = count_sgs(req->dst, req->nbytes);
	sg_miter_start(&p->dst_sg_it, req->dst, num_sgs, SG_MITER_TO_SG);

	mv_process_current_q(1);
}

static void mv_start_new_hash_req(struct ahash_request *req)
{
	struct req_progress *p = &cpg->p;
	struct mv_req_hash_ctx *ctx = ahash_request_ctx(req);
	int num_sgs, hw_bytes, old_extra_bytes, rc;
	cpg->cur_req = &req->base;
	memset(p, 0, sizeof(struct req_progress));
	hw_bytes = req->nbytes + ctx->extra_bytes;
	old_extra_bytes = ctx->extra_bytes;

	ctx->extra_bytes = hw_bytes % SHA1_BLOCK_SIZE;
	if (ctx->extra_bytes != 0
	    && (!ctx->last_chunk || ctx->count > MAX_HW_HASH_SIZE))
		hw_bytes -= ctx->extra_bytes;
	else
		ctx->extra_bytes = 0;

	num_sgs = count_sgs(req->src, req->nbytes);
	sg_miter_start(&p->src_sg_it, req->src, num_sgs, SG_MITER_FROM_SG);

	if (hw_bytes) {
		p->hw_nbytes = hw_bytes;
		p->complete = mv_hash_algo_completion;
		p->process = mv_process_hash_current;

		if (unlikely(old_extra_bytes)) {
			memcpy(cpg->sram + SRAM_DATA_IN_START, ctx->buffer,
			       old_extra_bytes);
			p->crypt_len = old_extra_bytes;
		}

		mv_process_hash_current(1);
	} else {
		copy_src_to_buf(p, ctx->buffer + old_extra_bytes,
				ctx->extra_bytes - old_extra_bytes);
		sg_miter_stop(&p->src_sg_it);
		if (ctx->last_chunk)
			rc = mv_hash_final_fallback(req);
		else
			rc = 0;
		cpg->eng_st = ENGINE_IDLE;
		local_bh_disable();
		req->base.complete(&req->base, rc);
		local_bh_enable();
	}
}

static int queue_manag(void *data)
{
	cpg->eng_st = ENGINE_IDLE;
	do {
		struct crypto_async_request *async_req = NULL;
		struct crypto_async_request *backlog;

		__set_current_state(TASK_INTERRUPTIBLE);

		if (cpg->eng_st == ENGINE_W_DEQUEUE)
			dequeue_complete_req();

		spin_lock_irq(&cpg->lock);
		if (cpg->eng_st == ENGINE_IDLE) {
			backlog = crypto_get_backlog(&cpg->queue);
			async_req = crypto_dequeue_request(&cpg->queue);
			if (async_req) {
				BUG_ON(cpg->eng_st != ENGINE_IDLE);
				cpg->eng_st = ENGINE_BUSY;
			}
		}
		spin_unlock_irq(&cpg->lock);

		if (backlog) {
			backlog->complete(backlog, -EINPROGRESS);
			backlog = NULL;
		}

		if (async_req) {
			if (async_req->tfm->__crt_alg->cra_type !=
			    &crypto_ahash_type) {
				struct ablkcipher_request *req =
				    ablkcipher_request_cast(async_req);
				mv_start_new_crypt_req(req);
			} else {
				struct ahash_request *req =
				    ahash_request_cast(async_req);
				mv_start_new_hash_req(req);
			}
			async_req = NULL;
		}

		schedule();

	} while (!kthread_should_stop());
	return 0;
}

static int mv_handle_req(struct crypto_async_request *req)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cpg->lock, flags);
	ret = crypto_enqueue_request(&cpg->queue, req);
	spin_unlock_irqrestore(&cpg->lock, flags);
	wake_up_process(cpg->queue_th);
	return ret;
}

static int mv_enc_aes_ecb(struct ablkcipher_request *req)
{
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_ECB;
	req_ctx->decrypt = 0;

	return mv_handle_req(&req->base);
}

static int mv_dec_aes_ecb(struct ablkcipher_request *req)
{
	struct mv_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_ECB;
	req_ctx->decrypt = 1;

	compute_aes_dec_key(ctx);
	return mv_handle_req(&req->base);
}

static int mv_enc_aes_cbc(struct ablkcipher_request *req)
{
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_CBC;
	req_ctx->decrypt = 0;

	return mv_handle_req(&req->base);
}

static int mv_dec_aes_cbc(struct ablkcipher_request *req)
{
	struct mv_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_CBC;
	req_ctx->decrypt = 1;

	compute_aes_dec_key(ctx);
	return mv_handle_req(&req->base);
}

static int mv_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct mv_req_ctx);
	return 0;
}

static void mv_init_hash_req_ctx(struct mv_req_hash_ctx *ctx, int op,
				 int is_last, unsigned int req_len,
				 int count_add)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->op = op;
	ctx->count = req_len;
	ctx->first_hash = 1;
	ctx->last_chunk = is_last;
	ctx->count_add = count_add;
}

static void mv_update_hash_req_ctx(struct mv_req_hash_ctx *ctx, int is_last,
				   unsigned req_len)
{
	ctx->last_chunk = is_last;
	ctx->count += req_len;
}

static int mv_hash_init(struct ahash_request *req)
{
	const struct mv_tfm_hash_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);
	mv_init_hash_req_ctx(ahash_request_ctx(req), tfm_ctx->op, 0, 0,
			     tfm_ctx->count_add);
	return 0;
}

static int mv_hash_update(struct ahash_request *req)
{
	if (!req->nbytes)
		return 0;

	mv_update_hash_req_ctx(ahash_request_ctx(req), 0, req->nbytes);
	return mv_handle_req(&req->base);
}

static int mv_hash_final(struct ahash_request *req)
{
	struct mv_req_hash_ctx *ctx = ahash_request_ctx(req);

	ahash_request_set_crypt(req, NULL, req->result, 0);
	mv_update_hash_req_ctx(ctx, 1, 0);
	return mv_handle_req(&req->base);
}

static int mv_hash_finup(struct ahash_request *req)
{
	mv_update_hash_req_ctx(ahash_request_ctx(req), 1, req->nbytes);
	return mv_handle_req(&req->base);
}

static int mv_hash_digest(struct ahash_request *req)
{
	const struct mv_tfm_hash_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);
	mv_init_hash_req_ctx(ahash_request_ctx(req), tfm_ctx->op, 1,
			     req->nbytes, tfm_ctx->count_add);
	return mv_handle_req(&req->base);
}

static void mv_hash_init_ivs(struct mv_tfm_hash_ctx *ctx, const void *istate,
			     const void *ostate)
{
	const struct sha1_state *isha1_state = istate, *osha1_state = ostate;
	int i;
	for (i = 0; i < 5; i++) {
		ctx->ivs[i] = cpu_to_be32(isha1_state->state[i]);
		ctx->ivs[i + 5] = cpu_to_be32(osha1_state->state[i]);
	}
}

static int mv_hash_setkey(struct crypto_ahash *tfm, const u8 * key,
			  unsigned int keylen)
{
	int rc;
	struct mv_tfm_hash_ctx *ctx = crypto_tfm_ctx(&tfm->base);
	int bs, ds, ss;

	if (!ctx->base_hash)
		return 0;

	rc = crypto_shash_setkey(ctx->fallback, key, keylen);
	if (rc)
		return rc;

	/* Can't see a way to extract the ipad/opad from the fallback tfm
	   so I'm basically copying code from the hmac module */
	bs = crypto_shash_blocksize(ctx->base_hash);
	ds = crypto_shash_digestsize(ctx->base_hash);
	ss = crypto_shash_statesize(ctx->base_hash);

	{
		struct {
			struct shash_desc shash;
			char ctx[crypto_shash_descsize(ctx->base_hash)];
		} desc;
		unsigned int i;
		char ipad[ss];
		char opad[ss];

		desc.shash.tfm = ctx->base_hash;
		desc.shash.flags = crypto_shash_get_flags(ctx->base_hash) &
		    CRYPTO_TFM_REQ_MAY_SLEEP;

		if (keylen > bs) {
			int err;

			err =
			    crypto_shash_digest(&desc.shash, key, keylen, ipad);
			if (err)
				return err;

			keylen = ds;
		} else
			memcpy(ipad, key, keylen);

		memset(ipad + keylen, 0, bs - keylen);
		memcpy(opad, ipad, bs);

		for (i = 0; i < bs; i++) {
			ipad[i] ^= 0x36;
			opad[i] ^= 0x5c;
		}

		rc = crypto_shash_init(&desc.shash) ? :
		    crypto_shash_update(&desc.shash, ipad, bs) ? :
		    crypto_shash_export(&desc.shash, ipad) ? :
		    crypto_shash_init(&desc.shash) ? :
		    crypto_shash_update(&desc.shash, opad, bs) ? :
		    crypto_shash_export(&desc.shash, opad);

		if (rc == 0)
			mv_hash_init_ivs(ctx, ipad, opad);

		return rc;
	}
}

static int mv_cra_hash_init(struct crypto_tfm *tfm, const char *base_hash_name,
			    enum hash_op op, int count_add)
{
	const char *fallback_driver_name = tfm->__crt_alg->cra_name;
	struct mv_tfm_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *fallback_tfm = NULL;
	struct crypto_shash *base_hash = NULL;
	int err = -ENOMEM;

	ctx->op = op;
	ctx->count_add = count_add;

	/* Allocate a fallback and abort if it failed. */
	fallback_tfm = crypto_alloc_shash(fallback_driver_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		printk(KERN_WARNING MV_CESA
		       "Fallback driver '%s' could not be loaded!\n",
		       fallback_driver_name);
		err = PTR_ERR(fallback_tfm);
		goto out;
	}
	ctx->fallback = fallback_tfm;

	if (base_hash_name) {
		/* Allocate a hash to compute the ipad/opad of hmac. */
		base_hash = crypto_alloc_shash(base_hash_name, 0,
					       CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(base_hash)) {
			printk(KERN_WARNING MV_CESA
			       "Base driver '%s' could not be loaded!\n",
			       base_hash_name);
			err = PTR_ERR(base_hash);
			goto err_bad_base;
		}
	}
	ctx->base_hash = base_hash;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct mv_req_hash_ctx) +
				 crypto_shash_descsize(ctx->fallback));
	return 0;
err_bad_base:
	crypto_free_shash(fallback_tfm);
out:
	return err;
}

static void mv_cra_hash_exit(struct crypto_tfm *tfm)
{
	struct mv_tfm_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(ctx->fallback);
	if (ctx->base_hash)
		crypto_free_shash(ctx->base_hash);
}

static int mv_cra_hash_sha1_init(struct crypto_tfm *tfm)
{
	return mv_cra_hash_init(tfm, NULL, COP_SHA1, 0);
}

static int mv_cra_hash_hmac_sha1_init(struct crypto_tfm *tfm)
{
	return mv_cra_hash_init(tfm, "sha1", COP_HMAC_SHA1, SHA1_BLOCK_SIZE);
}

irqreturn_t crypto_int(int irq, void *priv)
{
	u32 val;

	val = readl(cpg->reg + SEC_ACCEL_INT_STATUS);
	if (!(val & SEC_INT_ACCEL0_DONE))
		return IRQ_NONE;

	val &= ~SEC_INT_ACCEL0_DONE;
	writel(val, cpg->reg + FPGA_INT_STATUS);
	writel(val, cpg->reg + SEC_ACCEL_INT_STATUS);
	BUG_ON(cpg->eng_st != ENGINE_BUSY);
	cpg->eng_st = ENGINE_W_DEQUEUE;
	wake_up_process(cpg->queue_th);
	return IRQ_HANDLED;
}

struct crypto_alg mv_aes_alg_ecb = {
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "mv-ecb-aes",
	.cra_priority	= 300,
	.cra_flags	= CRYPTO_ALG_TYPE_ABLKCIPHER |
			  CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
	.cra_blocksize	= 16,
	.cra_ctxsize	= sizeof(struct mv_ctx),
	.cra_alignmask	= 0,
	.cra_type	= &crypto_ablkcipher_type,
	.cra_module	= THIS_MODULE,
	.cra_init	= mv_cra_init,
	.cra_u		= {
		.ablkcipher = {
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	mv_setkey_aes,
			.encrypt	=	mv_enc_aes_ecb,
			.decrypt	=	mv_dec_aes_ecb,
		},
	},
};

struct crypto_alg mv_aes_alg_cbc = {
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "mv-cbc-aes",
	.cra_priority	= 300,
	.cra_flags	= CRYPTO_ALG_TYPE_ABLKCIPHER |
			  CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
	.cra_blocksize	= AES_BLOCK_SIZE,
	.cra_ctxsize	= sizeof(struct mv_ctx),
	.cra_alignmask	= 0,
	.cra_type	= &crypto_ablkcipher_type,
	.cra_module	= THIS_MODULE,
	.cra_init	= mv_cra_init,
	.cra_u		= {
		.ablkcipher = {
			.ivsize		=	AES_BLOCK_SIZE,
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	mv_setkey_aes,
			.encrypt	=	mv_enc_aes_cbc,
			.decrypt	=	mv_dec_aes_cbc,
		},
	},
};

struct ahash_alg mv_sha1_alg = {
	.init = mv_hash_init,
	.update = mv_hash_update,
	.final = mv_hash_final,
	.finup = mv_hash_finup,
	.digest = mv_hash_digest,
	.halg = {
		 .digestsize = SHA1_DIGEST_SIZE,
		 .base = {
			  .cra_name = "sha1",
			  .cra_driver_name = "mv-sha1",
			  .cra_priority = 300,
			  .cra_flags =
			  CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
			  CRYPTO_ALG_NEED_FALLBACK,
			  .cra_blocksize = SHA1_BLOCK_SIZE,
			  .cra_ctxsize = sizeof(struct mv_tfm_hash_ctx),
			  .cra_init = mv_cra_hash_sha1_init,
			  .cra_exit = mv_cra_hash_exit,
			  .cra_module = THIS_MODULE,
			  }
		 }
};

struct ahash_alg mv_hmac_sha1_alg = {
	.init = mv_hash_init,
	.update = mv_hash_update,
	.final = mv_hash_final,
	.finup = mv_hash_finup,
	.digest = mv_hash_digest,
	.setkey = mv_hash_setkey,
	.halg = {
		 .digestsize = SHA1_DIGEST_SIZE,
		 .base = {
			  .cra_name = "hmac(sha1)",
			  .cra_driver_name = "mv-hmac-sha1",
			  .cra_priority = 300,
			  .cra_flags =
			  CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
			  CRYPTO_ALG_NEED_FALLBACK,
			  .cra_blocksize = SHA1_BLOCK_SIZE,
			  .cra_ctxsize = sizeof(struct mv_tfm_hash_ctx),
			  .cra_init = mv_cra_hash_hmac_sha1_init,
			  .cra_exit = mv_cra_hash_exit,
			  .cra_module = THIS_MODULE,
			  }
		 }
};

static int mv_probe(struct platform_device *pdev)
{
	struct crypto_priv *cp;
	struct resource *res;
	int irq;
	int ret;

	if (cpg) {
		printk(KERN_ERR MV_CESA "Second crypto dev?\n");
		return -EEXIST;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res)
		return -ENXIO;

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	spin_lock_init(&cp->lock);
	crypto_init_queue(&cp->queue, 50);
	cp->reg = ioremap(res->start, resource_size(res));
	if (!cp->reg) {
		ret = -ENOMEM;
		goto err;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	if (!res) {
		ret = -ENXIO;
		goto err_unmap_reg;
	}
	cp->sram_size = resource_size(res);
	cp->max_req_size = cp->sram_size - SRAM_CFG_SPACE;
	cp->sram = ioremap(res->start, cp->sram_size);
	if (!cp->sram) {
		ret = -ENOMEM;
		goto err_unmap_reg;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0 || irq == NO_IRQ) {
		ret = irq;
		goto err_unmap_sram;
	}
	cp->irq = irq;

	platform_set_drvdata(pdev, cp);
	cpg = cp;

	cp->queue_th = kthread_run(queue_manag, cp, "mv_crypto");
	if (IS_ERR(cp->queue_th)) {
		ret = PTR_ERR(cp->queue_th);
		goto err_unmap_sram;
	}

	ret = request_irq(irq, crypto_int, IRQF_DISABLED, dev_name(&pdev->dev),
			cp);
	if (ret)
		goto err_thread;

	/* Not all platforms can gate the clock, so it is not
	   an error if the clock does not exists. */
	cp->clk = clk_get(&pdev->dev, NULL);
	if (!IS_ERR(cp->clk))
		clk_prepare_enable(cp->clk);

	writel(SEC_INT_ACCEL0_DONE, cpg->reg + SEC_ACCEL_INT_MASK);
	writel(SEC_CFG_STOP_DIG_ERR, cpg->reg + SEC_ACCEL_CFG);
	writel(SRAM_CONFIG, cpg->reg + SEC_ACCEL_DESC_P0);

	ret = crypto_register_alg(&mv_aes_alg_ecb);
	if (ret) {
		printk(KERN_WARNING MV_CESA
		       "Could not register aes-ecb driver\n");
		goto err_irq;
	}

	ret = crypto_register_alg(&mv_aes_alg_cbc);
	if (ret) {
		printk(KERN_WARNING MV_CESA
		       "Could not register aes-cbc driver\n");
		goto err_unreg_ecb;
	}

	ret = crypto_register_ahash(&mv_sha1_alg);
	if (ret == 0)
		cpg->has_sha1 = 1;
	else
		printk(KERN_WARNING MV_CESA "Could not register sha1 driver\n");

	ret = crypto_register_ahash(&mv_hmac_sha1_alg);
	if (ret == 0) {
		cpg->has_hmac_sha1 = 1;
	} else {
		printk(KERN_WARNING MV_CESA
		       "Could not register hmac-sha1 driver\n");
	}

	return 0;
err_unreg_ecb:
	crypto_unregister_alg(&mv_aes_alg_ecb);
err_irq:
	free_irq(irq, cp);
err_thread:
	kthread_stop(cp->queue_th);
err_unmap_sram:
	iounmap(cp->sram);
err_unmap_reg:
	iounmap(cp->reg);
err:
	kfree(cp);
	cpg = NULL;
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int mv_remove(struct platform_device *pdev)
{
	struct crypto_priv *cp = platform_get_drvdata(pdev);

	crypto_unregister_alg(&mv_aes_alg_ecb);
	crypto_unregister_alg(&mv_aes_alg_cbc);
	if (cp->has_sha1)
		crypto_unregister_ahash(&mv_sha1_alg);
	if (cp->has_hmac_sha1)
		crypto_unregister_ahash(&mv_hmac_sha1_alg);
	kthread_stop(cp->queue_th);
	free_irq(cp->irq, cp);
	memset(cp->sram, 0, cp->sram_size);
	iounmap(cp->sram);
	iounmap(cp->reg);

	if (!IS_ERR(cp->clk)) {
		clk_disable_unprepare(cp->clk);
		clk_put(cp->clk);
	}

	kfree(cp);
	cpg = NULL;
	return 0;
}

static struct platform_driver marvell_crypto = {
	.probe		= mv_probe,
	.remove		= mv_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "mv_crypto",
	},
};
MODULE_ALIAS("platform:mv_crypto");

module_platform_driver(marvell_crypto);

MODULE_AUTHOR("Sebastian Andrzej Siewior <sebastian@breakpoint.cc>");
MODULE_DESCRIPTION("Support for Marvell's cryptographic engine");
MODULE_LICENSE("GPL");
