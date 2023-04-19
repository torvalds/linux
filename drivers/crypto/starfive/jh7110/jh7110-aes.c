// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive AES acceleration driver
 *
 * Copyright (c) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/amba/pl080.h>
#include <crypto/hash.h>
#include <crypto/gcm.h>

#include <linux/dma-direct.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "jh7110-str.h"

/* Mode mask = bits [3..0] */
#define FLG_MODE_MASK				GENMASK(2, 0)

/* Bit [4] encrypt / decrypt */
#define FLG_ENCRYPT				BIT(4)

/* Bit [31..16] status  */
#define FLG_CCM_PADDED_WA			BIT(5)

#define SR_BUSY					0x00000010
#define SR_OFNE					0x00000004

#define IMSCR_IN				BIT(0)
#define IMSCR_OUT				BIT(1)

#define MISR_IN					BIT(0)
#define MISR_OUT				BIT(1)

/* Misc */
#define AES_BLOCK_32				(AES_BLOCK_SIZE / sizeof(u32))
#define GCM_CTR_INIT				1
#define CRYP_AUTOSUSPEND_DELAY			50

static inline int jh7110_aes_wait_busy(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_AES_CSR, status,
				   !(status & JH7110_AES_BUSY), 10, 100000);
}

static inline int jh7110_aes_wait_keydone(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_AES_CSR, status,
				   (status & JH7110_AES_KEY_DONE), 10, 100000);
}

static inline int jh7110_aes_wait_gcmdone(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_AES_CSR, status,
				   (status & JH7110_AES_GCM_DONE), 10, 100000);
}

static int jh7110_cryp_check_aligned(struct scatterlist *sg, size_t total,
		size_t align)
{
	int len = 0;

	if (!total)
		return 0;

	if (!IS_ALIGNED(total, align))
		return -EINVAL;

	while (sg) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32)))
			return -EINVAL;

		if (!IS_ALIGNED(sg->length, align))
			return -EINVAL;

		len += sg->length;
		sg = sg_next(sg);
	}

	if (len != total)
		return -EINVAL;

	return 0;
}

static int jh7110_cryp_check_io_aligned(struct jh7110_sec_request_ctx *rctx)
{
	int ret;

	ret = jh7110_cryp_check_aligned(rctx->in_sg, rctx->total_in,
			rctx->hw_blocksize);

	if (ret)
		return ret;

	ret = jh7110_cryp_check_aligned(rctx->out_sg, rctx->total_out,
			rctx->hw_blocksize);

	return ret;
}

static void sg_copy_buf(void *buf, struct scatterlist *sg,
		unsigned int start, unsigned int nbytes, int out)
{
	struct scatter_walk walk;

	if (!nbytes)
		return;

	scatterwalk_start(&walk, sg);
	scatterwalk_advance(&walk, start);
	scatterwalk_copychunks(buf, &walk, nbytes, out);
	scatterwalk_done(&walk, out, 0);
}

static int jh7110_cryp_copy_sgs(struct jh7110_sec_request_ctx *rctx)
{
	void *buf_in, *buf_out;
	int pages, total_in, total_out;

	if (!jh7110_cryp_check_io_aligned(rctx)) {
		rctx->sgs_copied = 0;
		return 0;
	}

	total_in = ALIGN(rctx->total_in, rctx->hw_blocksize);
	pages = total_in ? get_order(total_in) : 1;
	buf_in = (void *)__get_free_pages(GFP_ATOMIC, pages);

	total_out = ALIGN(rctx->total_out, rctx->hw_blocksize);
	pages = total_out ? get_order(total_out) : 1;
	buf_out = (void *)__get_free_pages(GFP_ATOMIC, pages);

	if (!buf_in || !buf_out) {
		dev_err(rctx->sdev->dev, "Can't allocate pages when unaligned\n");
		rctx->sgs_copied = 0;
		return -EFAULT;
	}

	sg_copy_buf(buf_in, rctx->in_sg, 0, rctx->total_in, 0);

	sg_init_one(&rctx->in_sgl, buf_in, total_in);
	rctx->in_sg = &rctx->in_sgl;
	rctx->in_sg_len = 1;

	sg_init_one(&rctx->out_sgl, buf_out, total_out);
	rctx->out_sg_save = rctx->out_sg;
	rctx->out_sg = &rctx->out_sgl;
	rctx->out_sg_len = 1;

	rctx->sgs_copied = 1;

	return 0;
}

static inline int is_ecb(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_ECB;
}

static inline int is_cbc(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_CBC;
}

static inline int is_ofb(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_OFB;
}

static inline int is_cfb(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_CFB;
}

static inline int is_ctr(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_CTR;
}

static inline int is_gcm(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_GCM;
}

static inline int is_ccm(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_MODE_MASK) == JH7110_AES_MODE_CCM;
}

static inline int get_aes_mode(struct jh7110_sec_request_ctx *rctx)
{
	return rctx->flags & FLG_MODE_MASK;
}

static inline int is_encrypt(struct jh7110_sec_request_ctx *rctx)
{
	return !!(rctx->flags & FLG_ENCRYPT);
}

static inline int is_decrypt(struct jh7110_sec_request_ctx *rctx)
{
	return !is_encrypt(rctx);
}

static int jh7110_cryp_read_auth_tag(struct jh7110_sec_ctx *ctx);

static inline void jh7110_aes_reset(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	rctx->csr.aes_csr.v = 0;
	rctx->csr.aes_csr.aesrst = 1;
	jh7110_sec_write(ctx->sdev, JH7110_AES_CSR, rctx->csr.aes_csr.v);

}

static inline void jh7110_aes_xcm_start(struct jh7110_sec_ctx *ctx, u32 hw_mode)
{
	unsigned int value;

	switch (hw_mode) {
	case JH7110_AES_MODE_GCM:
		value = jh7110_sec_read(ctx->sdev, JH7110_AES_CSR);
		value |= JH7110_AES_GCM_START;
		jh7110_sec_write(ctx->sdev, JH7110_AES_CSR, value);
		break;
	case JH7110_AES_MODE_CCM:
		value = jh7110_sec_read(ctx->sdev, JH7110_AES_CSR);
		value |= JH7110_AES_CCM_START;
		jh7110_sec_write(ctx->sdev, JH7110_AES_CSR, value);
		break;
	}
}

static inline void jh7110_aes_csr_setup(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	rctx->csr.aes_csr.v = 0;
	switch (ctx->keylen) {
	case AES_KEYSIZE_128:
		rctx->csr.aes_csr.keymode = JH7110_AES_KEYMODE_128;
		break;
	case AES_KEYSIZE_192:
		rctx->csr.aes_csr.keymode = JH7110_AES_KEYMODE_192;
		break;
	case AES_KEYSIZE_256:
		rctx->csr.aes_csr.keymode = JH7110_AES_KEYMODE_256;
		break;
	default:
		return;
	}
	rctx->csr.aes_csr.mode  = rctx->flags & FLG_MODE_MASK;
	rctx->csr.aes_csr.cmode = is_decrypt(rctx);
	rctx->csr.aes_csr.stream_mode = rctx->stmode;

	if (ctx->sdev->use_side_channel_mitigation) {
		rctx->csr.aes_csr.delay_aes = 1;
		rctx->csr.aes_csr.vaes_start = 1;
	}

	if (jh7110_aes_wait_busy(ctx)) {
		dev_err(ctx->sdev->dev, "reset error\n");
		return;
	}

	jh7110_sec_write(ctx->sdev, JH7110_AES_CSR, rctx->csr.aes_csr.v);
}

static inline void jh7110_aes_set_ivlen(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	if (is_gcm(rctx))
		jh7110_sec_write(sdev, JH7110_AES_IVLEN, GCM_AES_IV_SIZE);
	else
		jh7110_sec_write(sdev, JH7110_AES_IVLEN, AES_BLOCK_SIZE);
}

static inline void jh7110_aes_set_alen(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;

	jh7110_sec_write(sdev, JH7110_AES_ALEN0, (ctx->rctx->assoclen >> 32) & 0xffffffff);
	jh7110_sec_write(sdev, JH7110_AES_ALEN1, ctx->rctx->assoclen & 0xffffffff);
}

static unsigned int jh7110_cryp_get_input_text_len(struct jh7110_sec_ctx *ctx);

static inline void jh7110_aes_set_mlen(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	size_t data_len;

	data_len = jh7110_cryp_get_input_text_len(ctx);

	jh7110_sec_write(sdev, JH7110_AES_MLEN0, (data_len >> 32) & 0xffffffff);
	jh7110_sec_write(sdev, JH7110_AES_MLEN1, data_len & 0xffffffff);
}

static inline int crypto_ccm_check_iv(const u8 *iv)
{
	/* 2 <= L <= 8, so 1 <= L' <= 7. */
	if (iv[0] < 1 || iv[0] > 7)
		return -EINVAL;

	return 0;
}


static int jh7110_cryp_hw_write_iv(struct jh7110_sec_ctx *ctx, u32 *iv)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	if (!iv)
		return -EINVAL;

	jh7110_sec_write(sdev, JH7110_AES_IV0, iv[0]);
	jh7110_sec_write(sdev, JH7110_AES_IV1, iv[1]);
	jh7110_sec_write(sdev, JH7110_AES_IV2, iv[2]);

	if (!is_gcm(rctx))
		jh7110_sec_write(sdev, JH7110_AES_IV3, iv[3]);
	else
		if (jh7110_aes_wait_gcmdone(ctx))
			return -ETIMEDOUT;

	return 0;
}

static void jh7110_cryp_hw_get_iv(struct jh7110_sec_ctx *ctx, u32 *iv)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;

	if (!iv)
		return;

	iv[0] = jh7110_sec_read(sdev, JH7110_AES_IV0);
	iv[1] = jh7110_sec_read(sdev, JH7110_AES_IV1);
	iv[2] = jh7110_sec_read(sdev, JH7110_AES_IV2);
	iv[3] = jh7110_sec_read(sdev, JH7110_AES_IV3);
}

static void jh7110_cryp_hw_write_ctr(struct jh7110_sec_ctx *ctx, u32 *ctr)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;

	jh7110_sec_write(sdev, JH7110_AES_NONCE0, ctr[0]);
	jh7110_sec_write(sdev, JH7110_AES_NONCE1, ctr[1]);
	jh7110_sec_write(sdev, JH7110_AES_NONCE2, ctr[2]);
	jh7110_sec_write(sdev, JH7110_AES_NONCE3, ctr[3]);
}

static int jh7110_cryp_hw_write_key(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 *key = (u32 *)ctx->key;

	switch (ctx->keylen) {
	case AES_KEYSIZE_256:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_128:
		break;
	default:
		return -EINVAL;
	}

	if (ctx->keylen >= AES_KEYSIZE_128) {
		jh7110_sec_write(sdev, JH7110_AES_KEY0, key[0]);
		jh7110_sec_write(sdev, JH7110_AES_KEY1, key[1]);
		jh7110_sec_write(sdev, JH7110_AES_KEY2, key[2]);
		jh7110_sec_write(sdev, JH7110_AES_KEY3, key[3]);
	}

	if (ctx->keylen >= AES_KEYSIZE_192) {
		jh7110_sec_write(sdev, JH7110_AES_KEY4, key[4]);
		jh7110_sec_write(sdev, JH7110_AES_KEY5, key[5]);
	}

	if (ctx->keylen >= AES_KEYSIZE_256) {
		jh7110_sec_write(sdev, JH7110_AES_KEY6, key[6]);
		jh7110_sec_write(sdev, JH7110_AES_KEY7, key[7]);
	}

	if (jh7110_aes_wait_keydone(ctx))
		return -ETIMEDOUT;

	return 0;
}

static unsigned int jh7110_cryp_get_input_text_len(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	return is_encrypt(rctx) ? rctx->req.areq->cryptlen :
				  rctx->req.areq->cryptlen - rctx->authsize;
}

static int jh7110_cryp_gcm_init(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	int ret;
	/* Phase 1 : init */
	memcpy(rctx->last_ctr, rctx->req.areq->iv, 12);

	ret = jh7110_cryp_hw_write_iv(ctx, (u32 *)rctx->last_ctr);

	if (ret)
		return ret;

	return 0;
}

static int jh7110_cryp_ccm_init(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	u8 iv[AES_BLOCK_SIZE], *b0;
	unsigned int textlen;

	/* Phase 1 : init. Firstly set the CTR value to 1 (not 0) */
	memcpy(iv, rctx->req.areq->iv, AES_BLOCK_SIZE);
	memset(iv + AES_BLOCK_SIZE - 1 - iv[0], 0, iv[0] + 1);

	/* Build B0 */
	b0 = (u8 *)sdev->aes_data;
	memcpy(b0, iv, AES_BLOCK_SIZE);

	b0[0] |= (8 * ((rctx->authsize - 2) / 2));

	if (rctx->req.areq->assoclen)
		b0[0] |= 0x40;

	textlen = jh7110_cryp_get_input_text_len(ctx);

	b0[AES_BLOCK_SIZE - 2] = textlen >> 8;
	b0[AES_BLOCK_SIZE - 1] = textlen & 0xFF;

	memcpy((void *)rctx->last_ctr, sdev->aes_data, AES_BLOCK_SIZE);
	jh7110_cryp_hw_write_ctr(ctx, (u32 *)b0);

	return 0;
}

static int jh7110_cryp_hw_init(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	int ret;
	u32 hw_mode;

	pm_runtime_resume_and_get(ctx->sdev->dev);

	jh7110_aes_reset(ctx);

	hw_mode = get_aes_mode(ctx->rctx);
	if (hw_mode == JH7110_AES_MODE_CFB ||
	   hw_mode == JH7110_AES_MODE_OFB)
		rctx->stmode = JH7110_AES_MODE_XFB_128;
	else
		rctx->stmode = JH7110_AES_MODE_XFB_1;

	jh7110_aes_csr_setup(ctx);

	ret = jh7110_cryp_hw_write_key(ctx);

	switch (hw_mode) {
	case JH7110_AES_MODE_GCM:
		memset(ctx->sdev->aes_data, 0, JH7110_MSG_BUFFER_SIZE);
		jh7110_aes_set_alen(ctx);
		jh7110_aes_set_mlen(ctx);
		jh7110_aes_set_ivlen(ctx);
		jh7110_aes_xcm_start(ctx, hw_mode);

		if (jh7110_aes_wait_gcmdone(ctx))
			return -ETIMEDOUT;

		memset((void *)rctx->last_ctr, 0, sizeof(rctx->last_ctr));
		jh7110_cryp_gcm_init(ctx);

		break;
	case JH7110_AES_MODE_CCM:
		memset(ctx->sdev->aes_data, 0, JH7110_MSG_BUFFER_SIZE);
		memset((void *)rctx->last_ctr, 0, sizeof(rctx->last_ctr));
		jh7110_aes_set_alen(ctx);
		jh7110_aes_set_mlen(ctx);

		/* Phase 1 : init */
		jh7110_cryp_ccm_init(ctx);

		jh7110_aes_xcm_start(ctx, hw_mode);

		break;
	case JH7110_AES_MODE_OFB:
	case JH7110_AES_MODE_CFB:
	case JH7110_AES_MODE_CBC:
	case JH7110_AES_MODE_CTR:
		ret = jh7110_cryp_hw_write_iv(ctx, (void *)rctx->req.sreq->iv);
		break;
	default:
		break;
	}

	return ret;
}

static int jh7110_cryp_get_from_sg(struct jh7110_sec_request_ctx *rctx, size_t offset,
			     size_t count, size_t data_offset)
{
	size_t of, ct, index;
	struct scatterlist	*sg = rctx->in_sg;

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
			scatterwalk_map_and_copy(rctx->sdev->aes_data + index, sg,
					of, ct, 0);
			index = index + ct;
			return index - data_offset;
		}
		scatterwalk_map_and_copy(rctx->sdev->aes_data + index, sg,
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

static int jh7110_cryp_read_auth_tag(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	int loop, total_len, start_addr;

	total_len = AES_BLOCK_SIZE / sizeof(u32);
	start_addr = JH7110_AES_NONCE0;

	if (jh7110_aes_wait_busy(ctx))
		return -EBUSY;

	if (is_gcm(rctx))
		for (loop = 0; loop < total_len; loop++, start_addr += 4)
			rctx->tag_out[loop] = jh7110_sec_read(sdev, start_addr);
	else
		for (loop = 0; loop < total_len; loop++)
			rctx->tag_out[loop] = jh7110_sec_read(sdev, JH7110_AES_AESDIO0R);

	if (is_encrypt(rctx))
		sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg), rctx->tag_out,
				rctx->authsize, rctx->offset, 0);
	else {
		scatterwalk_map_and_copy(rctx->tag_in, rctx->in_sg,
				rctx->total_in_save - rctx->authsize,
				rctx->authsize, 0);

		if (crypto_memneq(rctx->tag_in, rctx->tag_out, rctx->authsize))
			return -EBADMSG;
	}

	return 0;
}

static int jh7110_gcm_zero_message_data(struct jh7110_sec_ctx *ctx);

static void jh7110_cryp_finish_req(struct jh7110_sec_ctx *ctx, int err)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	if (!err && (is_gcm(rctx) || is_ccm(rctx))) {
		/* Phase 4 : output tag */
		err = jh7110_cryp_read_auth_tag(ctx);
	}

	if (!err && (is_cbc(rctx) || is_ctr(rctx)))
		jh7110_cryp_hw_get_iv(ctx, (void *)rctx->req.sreq->iv);

	if (rctx->sgs_copied) {
		void *buf_in, *buf_out;
		int pages, len;

		buf_in = sg_virt(&rctx->in_sgl);
		buf_out = sg_virt(&rctx->out_sgl);

		sg_copy_buf(buf_out, rctx->out_sg_save, 0,
				rctx->total_out_save, 1);

		len = ALIGN(rctx->total_in_save, rctx->hw_blocksize);
		pages = len ? get_order(len) : 1;
		free_pages((unsigned long)buf_in, pages);

		len = ALIGN(rctx->total_out_save, rctx->hw_blocksize);
		pages = len ? get_order(len) : 1;
		free_pages((unsigned long)buf_out, pages);
	}

	pm_runtime_mark_last_busy(ctx->sdev->dev);
	pm_runtime_put_autosuspend(ctx->sdev->dev);

	if (is_gcm(rctx) || is_ccm(rctx))
		crypto_finalize_aead_request(ctx->sdev->engine, rctx->req.areq, err);
	else
		crypto_finalize_skcipher_request(ctx->sdev->engine, rctx->req.sreq,
				err);
}

static bool jh7110_check_counter_overflow(struct jh7110_sec_ctx *ctx, size_t count)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	bool ret = false;
	u32 start, end, ctr, blocks;

	if (count) {
		blocks = DIV_ROUND_UP(count, AES_BLOCK_SIZE);
		rctx->last_ctr[3] = cpu_to_be32(be32_to_cpu(rctx->last_ctr[3]) + blocks);

		if (rctx->last_ctr[3] == 0) {
			rctx->last_ctr[2] = cpu_to_be32(be32_to_cpu(rctx->last_ctr[2]) + 1);
			if (rctx->last_ctr[2] == 0) {
				rctx->last_ctr[1] = cpu_to_be32(be32_to_cpu(rctx->last_ctr[1]) + 1);
				if (rctx->last_ctr[1] == 0) {
					rctx->last_ctr[0] = cpu_to_be32(be32_to_cpu(rctx->last_ctr[0]) + 1);
					if (rctx->last_ctr[1] == 0)
						jh7110_cryp_hw_write_ctr(ctx, (u32 *)rctx->last_ctr);
				}
			}
		}
	}

	/* ctr counter overflow. */
	ctr = rctx->total_in - rctx->assoclen - rctx->authsize;
	blocks = DIV_ROUND_UP(ctr, AES_BLOCK_SIZE);
	start = be32_to_cpu(rctx->last_ctr[3]);

	end = start + blocks - 1;
	if (end < start) {
		rctx->ctr_over_count = AES_BLOCK_SIZE * -start;
		ret = true;
	}

	return ret;
}

static void jh7110_aes_dma_callback(void *param)
{
	struct jh7110_sec_dev *sdev = param;

	complete(&sdev->sec_comp_p);
}

static void vic_debug_dma_info(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int loop;

	for (loop = 0; loop <= 0x34; loop += 4)
		dev_dbg(sdev->dev, "dma[%x] = %x\n", loop, readl_relaxed(sdev->dma_base + loop));
	for (loop = 0x100; loop <= 0x110; loop += 4)
		dev_dbg(sdev->dev, "dma[%x] = %x\n", loop, readl_relaxed(sdev->dma_base + loop));
	for (loop = 0x120; loop <= 0x130; loop += 4)
		dev_dbg(sdev->dev, "dma[%x] = %x\n", loop, readl_relaxed(sdev->dma_base + loop));
}

static int jh7110_cryp_write_out_dma(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct dma_async_tx_descriptor	*in_desc, *out_desc;
	union  jh7110_alg_cr		alg_cr;
	dma_cookie_t cookie;
	unsigned int  *out;
	int total_len;
	int err;
	int loop;

	total_len = rctx->bufcnt;

	alg_cr.v = 0;
	alg_cr.start = 1;
	alg_cr.aes_dma_en = 1;
	jh7110_sec_write(sdev, JH7110_ALG_CR_OFFSET, alg_cr.v);

	total_len = (total_len & 0xf) ? (((total_len >> 4) + 1) << 4) : total_len;

	jh7110_sec_write(sdev, JH7110_DMA_IN_LEN_OFFSET, total_len);
	jh7110_sec_write(sdev, JH7110_DMA_OUT_LEN_OFFSET, total_len);

	sg_init_table(&ctx->sg[0], 1);
	sg_set_buf(&ctx->sg[0], sdev->aes_data, total_len);
	sg_dma_address(&ctx->sg[0]) = phys_to_dma(sdev->dev, (unsigned long long)(sdev->aes_data));
	sg_dma_len(&ctx->sg[0]) = total_len;

	sg_init_table(&ctx->sg[1], 1);
	sg_set_buf(&ctx->sg[1], sdev->aes_data + (JH7110_MSG_BUFFER_SIZE >> 1), total_len);
	sg_dma_address(&ctx->sg[1]) = phys_to_dma(sdev->dev, (unsigned long long)(sdev->aes_data + (JH7110_MSG_BUFFER_SIZE >> 1)));
	sg_dma_len(&ctx->sg[1]) = total_len;

	err = dma_map_sg(sdev->dev, &ctx->sg[0], 1, DMA_TO_DEVICE);
	if (!err) {
		dev_err(sdev->dev, "dma_map_sg() error\n");
		return -EINVAL;
	}

	err = dma_map_sg(sdev->dev, &ctx->sg[1], 1, DMA_FROM_DEVICE);
	if (!err) {
		dev_err(sdev->dev, "dma_map_sg() error\n");
		return -EINVAL;
	}

	sdev->cfg_in.direction = DMA_MEM_TO_DEV;
	sdev->cfg_in.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sdev->cfg_in.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sdev->cfg_in.src_maxburst = sdev->dma_maxburst;
	sdev->cfg_in.dst_maxburst = sdev->dma_maxburst;
	sdev->cfg_in.dst_addr = sdev->io_phys_base + JH7110_ALG_FIFO_OFFSET;

	dmaengine_slave_config(sdev->sec_xm_m, &sdev->cfg_in);

	sdev->cfg_out.direction = DMA_DEV_TO_MEM;
	sdev->cfg_out.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sdev->cfg_out.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sdev->cfg_out.src_maxburst = 4;
	sdev->cfg_out.dst_maxburst = 4;
	sdev->cfg_out.src_addr = sdev->io_phys_base + JH7110_ALG_FIFO_OFFSET;

	dmaengine_slave_config(sdev->sec_xm_p, &sdev->cfg_out);

	in_desc = dmaengine_prep_slave_sg(sdev->sec_xm_m, &ctx->sg[0],
				1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT  |  DMA_CTRL_ACK);
	if (!in_desc)
		return -EINVAL;

	cookie = dmaengine_submit(in_desc);
	dma_async_issue_pending(sdev->sec_xm_m);

	out_desc = dmaengine_prep_slave_sg(sdev->sec_xm_p, &ctx->sg[1],
				1, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!out_desc)
		return -EINVAL;

	reinit_completion(&sdev->sec_comp_p);

	out_desc->callback = jh7110_aes_dma_callback;
	out_desc->callback_param = sdev;

	dmaengine_submit(out_desc);
	dma_async_issue_pending(sdev->sec_xm_p);

	err = 0;

	if (!wait_for_completion_timeout(&sdev->sec_comp_p,
				msecs_to_jiffies(10000))) {
		alg_cr.v = 0;
		alg_cr.clear = 1;

		jh7110_sec_write(sdev, JH7110_ALG_CR_OFFSET, alg_cr.v);

		vic_debug_dma_info(ctx);
		dev_err(sdev->dev, "wait_for_completion_timeout out error cookie = %x\n",
			dma_async_is_tx_complete(sdev->sec_xm_p, cookie,
				     NULL, NULL));
	}

	out = (unsigned int *)(sdev->aes_data + (JH7110_MSG_BUFFER_SIZE >> 1));

	for (loop = 0; loop < total_len / 4; loop++)
		dev_dbg(sdev->dev, "this is debug [%d] = %x\n", loop, out[loop]);

	sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg), out,
			total_len, rctx->offset, 0);

	dma_unmap_sg(sdev->dev, &ctx->sg[0], 1, DMA_TO_DEVICE);
	dma_unmap_sg(sdev->dev, &ctx->sg[1], 1, DMA_FROM_DEVICE);

	alg_cr.v = 0;
	alg_cr.clear = 1;

	jh7110_sec_write(sdev, JH7110_ALG_CR_OFFSET, alg_cr.v);

	return 0;
}

static int jh7110_cryp_write_out_cpu(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	unsigned int  *buffer, *out;
	int total_len, loop, count;

	total_len = rctx->bufcnt;
	buffer = (unsigned int *)sdev->aes_data;
	out = (unsigned int *)(sdev->aes_data + (JH7110_MSG_BUFFER_SIZE >> 1));

	while (total_len > 0) {
		for (loop = 0; loop < 4; loop++, buffer++)
			jh7110_sec_write(sdev, JH7110_AES_AESDIO0R, *buffer);

		if (jh7110_aes_wait_busy(ctx)) {
			dev_err(sdev->dev, "jh7110_aes_wait_busy error\n");
			return -ETIMEDOUT;
		}

		for (loop = 0; loop < 4; loop++, out++)
			*out = jh7110_sec_read(sdev, JH7110_AES_AESDIO0R);

		total_len -= 16;
	}

	if (rctx->bufcnt >= rctx->total_out)
		count = rctx->total_out;
	else
		count = rctx->bufcnt;

	out = (unsigned int *)(sdev->aes_data + (JH7110_MSG_BUFFER_SIZE >> 1));

	sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg), out,
			count, rctx->offset, 0);

	return 0;
}

static int jh7110_cryp_write_data(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	size_t data_len, total, count, data_buf_len, data_offset;
	int ret;
	bool fragmented = false;

	sdev->cry_type = JH7110_AES_TYPE;

	/* ctr counter overflow. */
	fragmented = jh7110_check_counter_overflow(ctx, 0);

	total = 0;
	rctx->offset = 0;
	rctx->data_offset = 0;

	data_offset = rctx->data_offset;
	while (total < rctx->total_in) {
		data_buf_len = sdev->data_buf_len - (sdev->data_buf_len % ctx->keylen) - data_offset;
		count = min(rctx->total_in - rctx->offset, data_buf_len);

		/* ctr counter overflow. */
		if (fragmented && rctx->ctr_over_count != 0) {
			if (count >= rctx->ctr_over_count)
				count = rctx->ctr_over_count;
		}
		data_len = jh7110_cryp_get_from_sg(rctx, rctx->offset, count, data_offset);

		if (data_len < 0)
			return data_len;
		if (data_len != count)
			return -EINVAL;

		rctx->bufcnt = data_len + data_offset;
		total += data_len;

		if (!is_encrypt(rctx) && (total + rctx->assoclen >= rctx->total_in))
			rctx->bufcnt = rctx->bufcnt - rctx->authsize;

		if (rctx->bufcnt) {
			if (sdev->use_dma)
				ret = jh7110_cryp_write_out_dma(ctx);
			else
				ret = jh7110_cryp_write_out_cpu(ctx);

			if (ret)
				return ret;
		}
		data_offset = 0;
		rctx->offset += data_len;
		fragmented = jh7110_check_counter_overflow(ctx, data_len);
	}

	return ret;
}

static int jh7110_cryp_gcm_write_aad(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	unsigned int *buffer;
	int total_len, loop;

	if (rctx->assoclen) {
		total_len = rctx->assoclen;
		total_len = (total_len & 0xf) ? (((total_len >> 4) + 1) << 2) : (total_len >> 2);
	}

	buffer = (unsigned int *)sdev->aes_data;

	for (loop = 0; loop < total_len; loop += 4) {
		jh7110_sec_write(sdev, JH7110_AES_NONCE0, *buffer);
		buffer++;
		jh7110_sec_write(sdev, JH7110_AES_NONCE1, *buffer);
		buffer++;
		jh7110_sec_write(sdev, JH7110_AES_NONCE2, *buffer);
		buffer++;
		jh7110_sec_write(sdev, JH7110_AES_NONCE3, *buffer);
		buffer++;
		udelay(2);
	}

	if (jh7110_aes_wait_gcmdone(ctx))
		return -ETIMEDOUT;

	return 0;
}

static int jh7110_cryp_ccm_write_aad(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	unsigned int  *buffer, *out;
	unsigned char *ci;
	int total_len, mlen, loop;

	total_len = rctx->bufcnt;
	buffer = (unsigned int *)sdev->aes_data;
	out = (unsigned int *)(sdev->aes_data + (JH7110_MSG_BUFFER_SIZE >> 1));

	ci = (unsigned char *)buffer;
	jh7110_sec_writeb(sdev, JH7110_AES_AESDIO0R, *ci);
	ci++;
	jh7110_sec_writeb(sdev, JH7110_AES_AESDIO0R, *ci);
	ci++;
	total_len -= 2;
	buffer = (unsigned int *)ci;
	for (loop = 0; loop < 3; loop++, buffer++)
		jh7110_sec_write(sdev, JH7110_AES_AESDIO0R, *buffer);

	if (jh7110_aes_wait_busy(ctx)) {
		dev_err(sdev->dev, "jh7110_aes_wait_busy error\n");
		return -ETIMEDOUT;
	}
	total_len -= 12;

	while (total_len >= 16) {
		for (loop = 0; loop < 4; loop++, buffer++)
			jh7110_sec_write(sdev, JH7110_AES_AESDIO0R, *buffer);

		if (jh7110_aes_wait_busy(ctx)) {
			dev_err(sdev->dev, "jh7110_aes_wait_busy error\n");
			return -ETIMEDOUT;
		}
		total_len -= 16;
	}

	if (total_len > 0) {
		mlen = total_len;
		for (; total_len >= 4; total_len -= 4, buffer++)
			jh7110_sec_write(sdev, JH7110_AES_AESDIO0R, *buffer);

		ci = (unsigned char *)buffer;
		for (; total_len > 0; total_len--, ci++)
			jh7110_sec_writeb(sdev, JH7110_AES_AESDIO0R, *ci);

		if (jh7110_aes_wait_busy(ctx)) {
			dev_err(sdev->dev, "jh7110_aes_wait_busy error\n");
			return -ETIMEDOUT;
		}
	}

	if (jh7110_aes_wait_busy(ctx)) {
		dev_err(sdev->dev, "jh7110_aes_wait_busy error\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int jh7110_cryp_xcm_write_data(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	size_t data_len, total, count, data_buf_len, offset, auths;
	int ret;
	bool fragmented = false;

	sdev->cry_type = JH7110_AES_TYPE;

	/* ctr counter overflow. */
	fragmented = jh7110_check_counter_overflow(ctx, 0);

	total = 0;
	rctx->offset = 0;
	rctx->data_offset = 0;
	offset = 0;

	while (total < rctx->assoclen) {
		data_buf_len = sdev->data_buf_len - (sdev->data_buf_len % ctx->keylen);
		count = min(rctx->assoclen - offset, data_buf_len);
		count = min(count, rctx->assoclen - total);
		data_len = jh7110_cryp_get_from_sg(rctx, offset, count, 0);
		if (data_len < 0)
			return data_len;
		if (data_len != count)
			return -EINVAL;

		offset += data_len;
		rctx->offset += data_len;
		if ((data_len + 2) & 0xF) {
			memset(sdev->aes_data + data_len, 0, 16 - ((data_len + 2) & 0xf));
			data_len += 16 - ((data_len + 2) & 0xf);
		}

		rctx->bufcnt = data_len;
		total += data_len;

		if (is_ccm(rctx))
			ret = jh7110_cryp_ccm_write_aad(ctx);
		else
			ret = jh7110_cryp_gcm_write_aad(ctx);

		if (ret)
			return ret;
	}

	total = 0;
	auths = 0;

	while (total + auths < rctx->total_in - rctx->assoclen) {
		data_buf_len = sdev->data_buf_len - (sdev->data_buf_len % ctx->keylen);
		count = min(rctx->total_in - rctx->offset, data_buf_len);

		if (is_encrypt(rctx))
			count = min(count, rctx->total_in - rctx->assoclen - total);
		else {
			count = min(count, rctx->total_in - rctx->assoclen - total - rctx->authsize);
			auths = rctx->authsize;
		}

		/* ctr counter overflow. */
		if (fragmented && rctx->ctr_over_count != 0) {
			if (count >= rctx->ctr_over_count)
				count = rctx->ctr_over_count;
		}

		data_len = jh7110_cryp_get_from_sg(rctx, rctx->offset, count, 0);

		if (data_len < 0)
			return data_len;
		if (data_len != count)
			return -EINVAL;

		if (data_len % JH7110_AES_IV_LEN) {
			memset(sdev->aes_data + data_len, 0, JH7110_AES_IV_LEN - (data_len % JH7110_AES_IV_LEN));
			data_len = data_len + (JH7110_AES_IV_LEN - (data_len % JH7110_AES_IV_LEN));
		}

		rctx->bufcnt = data_len;
		total += data_len;

		if (rctx->bufcnt) {
			if (sdev->use_dma)
				ret = jh7110_cryp_write_out_dma(ctx);
			else
				ret = jh7110_cryp_write_out_cpu(ctx);
		}

		rctx->offset += count;
		offset += count;

		fragmented = jh7110_check_counter_overflow(ctx, data_len);
	}

	return ret;
}

static int jh7110_gcm_zero_message_data(struct jh7110_sec_ctx *ctx)
{
	int ret;

	ctx->rctx->bufcnt = 0;
	ctx->rctx->offset = 0;
	if (ctx->sdev->use_dma)
		ret = jh7110_cryp_write_out_dma(ctx);
	else
		ret = jh7110_cryp_write_out_cpu(ctx);

	return ret;
}

static int jh7110_cryp_cpu_start(struct jh7110_sec_ctx *ctx, struct jh7110_sec_request_ctx *rctx)
{
	int ret;

	ret = jh7110_cryp_write_data(ctx);
	if (ret)
		return ret;

	jh7110_cryp_finish_req(ctx, ret);

	return 0;
}

static int jh7110_cryp_xcm_start(struct jh7110_sec_ctx *ctx, struct jh7110_sec_request_ctx *rctx)
{
	int ret;

	ret = jh7110_cryp_xcm_write_data(ctx);
	if (ret)
		return ret;

	jh7110_cryp_finish_req(ctx, ret);

	mutex_unlock(&ctx->sdev->lock);

	return 0;
}

static int jh7110_cryp_cipher_one_req(struct crypto_engine *engine, void *areq);
static int jh7110_cryp_prepare_cipher_req(struct crypto_engine *engine,
					 void *areq);

static int jh7110_cryp_cra_init(struct crypto_skcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->sdev = jh7110_sec_find_dev(ctx);
	if (!ctx->sdev)
		return -ENODEV;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct jh7110_sec_request_ctx));

	ctx->enginectx.op.do_one_request = jh7110_cryp_cipher_one_req;
	ctx->enginectx.op.prepare_request = jh7110_cryp_prepare_cipher_req;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void jh7110_cryp_cra_exit(struct crypto_skcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->enginectx.op.do_one_request = NULL;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;
}

static int jh7110_cryp_aead_one_req(struct crypto_engine *engine, void *areq);
static int jh7110_cryp_prepare_aead_req(struct crypto_engine *engine,
				       void *areq);

static int jh7110_cryp_aes_aead_init(struct crypto_aead *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_tfm *aead = crypto_aead_tfm(tfm);
	struct crypto_alg *alg = aead->__crt_alg;

	ctx->sdev = jh7110_sec_find_dev(ctx);

	if (!ctx->sdev)
		return -ENODEV;

	crypto_aead_set_reqsize(tfm, sizeof(struct jh7110_sec_request_ctx));

	if (alg->cra_flags & CRYPTO_ALG_NEED_FALLBACK) {
		ctx->fallback.aead =
			crypto_alloc_aead(alg->cra_name, 0,
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(ctx->fallback.aead)) {
			pr_err("%s() failed to allocate fallback for %s\n",
					__func__, alg->cra_name);
			return PTR_ERR(ctx->fallback.aead);
		}
	}

	ctx->enginectx.op.do_one_request = jh7110_cryp_aead_one_req;
	ctx->enginectx.op.prepare_request = jh7110_cryp_prepare_aead_req;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void jh7110_cryp_aes_aead_exit(struct crypto_aead *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(tfm);

	if (ctx->fallback.aead) {
		crypto_free_aead(ctx->fallback.aead);
		ctx->fallback.aead = NULL;
	}

	ctx->enginectx.op.do_one_request = NULL;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;
}

static int jh7110_cryp_crypt(struct skcipher_request *req, unsigned long flags)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct jh7110_sec_request_ctx *rctx = skcipher_request_ctx(req);
	struct jh7110_sec_dev *sdev = ctx->sdev;
	unsigned int blocksize_align = crypto_skcipher_blocksize(tfm) - 1;

	if (!sdev)
		return -ENODEV;

	rctx->flags = flags;
	rctx->req_type = JH7110_ABLK_REQ;

	if (is_ecb(rctx) || is_cbc(rctx))
		if (req->cryptlen & (blocksize_align))
			return -EINVAL;

	return crypto_transfer_skcipher_request_to_engine(sdev->engine, req);
}

static int aead_do_fallback(struct aead_request *req)
{
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(aead);

	aead_request_set_tfm(subreq, ctx->fallback.aead);
	aead_request_set_callback(subreq, req->base.flags,
			req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src,
			req->dst, req->cryptlen, req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	return crypto_aead_decrypt(subreq);
}

static int jh7110_cryp_aead_crypt(struct aead_request *req, unsigned long flags)
{
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct jh7110_sec_request_ctx *rctx = aead_request_ctx(req);
	struct jh7110_sec_dev *sdev = ctx->sdev;

	if (!sdev)
		return -ENODEV;

	rctx->flags = flags;
	rctx->req_type = JH7110_AEAD_REQ;

	/* HW engine could not perform tag verification on
	 * non-blocksize aligned ciphertext, use fallback algo instead
	 */
	if (ctx->fallback.aead && is_decrypt(rctx))
		return aead_do_fallback(req);

	return crypto_transfer_aead_request_to_engine(sdev->engine, req);
}

static int jh7110_cryp_setkey(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	{
		int loop;

		for (loop = 0; loop < keylen; loop++)
			pr_debug("key[%d] = %x ctx->key[%d] = %x\n", loop, key[loop], loop, ctx->key[loop]);
	}
	return 0;
}

static int jh7110_cryp_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	if (!key || !keylen)
		return -EINVAL;

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
		keylen != AES_KEYSIZE_256)
		return -EINVAL;
	else
		return jh7110_cryp_setkey(tfm, key, keylen);
}

static int jh7110_cryp_aes_aead_setkey(struct crypto_aead *tfm, const u8 *key,
				      unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(tfm);
	int ret = 0;

	if (!key || !keylen)
		return -EINVAL;

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	if (ctx->fallback.aead)
		ret = crypto_aead_setkey(ctx->fallback.aead, key, keylen);

	return ret;
}

static int jh7110_cryp_aes_gcm_setauthsize(struct crypto_aead *tfm,
					  unsigned int authsize)
{
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(tfm);

	tfm->authsize = authsize;

	if (ctx->fallback.aead)
		ctx->fallback.aead->authsize = authsize;

	return crypto_gcm_check_authsize(authsize);
}

static int jh7110_cryp_aes_ccm_setauthsize(struct crypto_aead *tfm,
					  unsigned int authsize)
{
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(tfm);
	int ret = 0;

	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	tfm->authsize = authsize;

	if (ctx->fallback.aead)
		ctx->fallback.aead->authsize = authsize;

	return ret;
}

static int jh7110_cryp_aes_ecb_encrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_ECB | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_ecb_decrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_ECB);
}

static int jh7110_cryp_aes_cbc_encrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_CBC | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_cbc_decrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_CBC);
}

static int jh7110_cryp_aes_cfb_encrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_CFB | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_cfb_decrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_CFB);
}

static int jh7110_cryp_aes_ofb_encrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_OFB | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_ofb_decrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_OFB);
}

static int jh7110_cryp_aes_ctr_encrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_CTR | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_ctr_decrypt(struct skcipher_request *req)
{
	return jh7110_cryp_crypt(req,  JH7110_AES_MODE_CTR);
}

static int jh7110_cryp_aes_gcm_encrypt(struct aead_request *req)
{
	return jh7110_cryp_aead_crypt(req,  JH7110_AES_MODE_GCM | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_gcm_decrypt(struct aead_request *req)
{
	return jh7110_cryp_aead_crypt(req,  JH7110_AES_MODE_GCM);
}

static int jh7110_cryp_aes_ccm_encrypt(struct aead_request *req)
{
	int ret;

	ret = crypto_ccm_check_iv(req->iv);
	if (ret)
		return ret;

	return jh7110_cryp_aead_crypt(req,  JH7110_AES_MODE_CCM | FLG_ENCRYPT);
}

static int jh7110_cryp_aes_ccm_decrypt(struct aead_request *req)
{
	int ret;

	ret = crypto_ccm_check_iv(req->iv);
	if (ret)
		return ret;

	return jh7110_cryp_aead_crypt(req, JH7110_AES_MODE_CCM);
}

static int jh7110_cryp_prepare_req(struct skcipher_request *req,
				  struct aead_request *areq)
{
	struct jh7110_sec_ctx *ctx;
	struct jh7110_sec_dev *sdev;
	struct jh7110_sec_request_ctx *rctx;
	int ret;

	if (!req && !areq)
		return -EINVAL;

	ctx = req ? crypto_skcipher_ctx(crypto_skcipher_reqtfm(req)) :
		    crypto_aead_ctx(crypto_aead_reqtfm(areq));

	sdev = ctx->sdev;

	if (!sdev)
		return -ENODEV;

	rctx = req ? skcipher_request_ctx(req) : aead_request_ctx(areq);

	mutex_lock(&ctx->sdev->lock);

	rctx->sdev = sdev;
	ctx->rctx = rctx;
	rctx->hw_blocksize = AES_BLOCK_SIZE;

	if (req) {
		rctx->req.sreq = req;
		rctx->req_type = JH7110_ABLK_REQ;
		rctx->total_in = req->cryptlen;
		rctx->total_out = rctx->total_in;
		rctx->authsize = 0;
		rctx->assoclen = 0;
	} else {
		/*
		 * Length of input and output data:
		 * Encryption case:
		 *  INPUT  =   AssocData  ||   PlainText
		 *          <- assoclen ->  <- cryptlen ->
		 *          <------- total_in ----------->
		 *
		 *  OUTPUT =   AssocData  ||  CipherText  ||   AuthTag
		 *          <- assoclen ->  <- cryptlen ->  <- authsize ->
		 *          <---------------- total_out ----------------->
		 *
		 * Decryption case:
		 *  INPUT  =   AssocData  ||  CipherText  ||  AuthTag
		 *          <- assoclen ->  <--------- cryptlen --------->
		 *                                          <- authsize ->
		 *          <---------------- total_in ------------------>
		 *
		 *  OUTPUT =   AssocData  ||   PlainText
		 *          <- assoclen ->  <- crypten - authsize ->
		 *          <---------- total_out ----------------->
		 */
		rctx->req.areq = areq;
		rctx->req_type = JH7110_AEAD_REQ;
		rctx->authsize = crypto_aead_authsize(crypto_aead_reqtfm(areq));
		rctx->total_in = areq->assoclen + areq->cryptlen;
		rctx->assoclen = areq->assoclen;
		if (is_encrypt(rctx))
			/* Append auth tag to output */
			rctx->total_out = rctx->total_in + rctx->authsize;
		else
			/* No auth tag in output */
			rctx->total_out = rctx->total_in - rctx->authsize;
	}

	rctx->total_in_save = rctx->total_in;
	rctx->total_out_save = rctx->total_out;

	rctx->in_sg = req ? req->src : areq->src;
	rctx->out_sg = req ? req->dst : areq->dst;
	rctx->out_sg_save = rctx->out_sg;

	rctx->in_sg_len = sg_nents_for_len(rctx->in_sg, rctx->total_in);
	if (rctx->in_sg_len < 0) {
		dev_err(sdev->dev, "Cannot get in_sg_len\n");
		ret = rctx->in_sg_len;
		goto out;
	}

	rctx->out_sg_len = sg_nents_for_len(rctx->out_sg, rctx->total_out);
	if (rctx->out_sg_len < 0) {
		dev_err(sdev->dev, "Cannot get out_sg_len\n");
		ret = rctx->out_sg_len;
		goto out;
	}

	ret = jh7110_cryp_copy_sgs(rctx);
	if (ret)
		return ret;

	ret = jh7110_cryp_hw_init(ctx);
	if (ret)
		goto out;

	return ret;
out:
	mutex_unlock(&sdev->doing);

	return ret;
}

static int jh7110_cryp_prepare_cipher_req(struct crypto_engine *engine,
					 void *areq)
{
	struct skcipher_request *req = container_of(areq,
						      struct skcipher_request,
						      base);
	return jh7110_cryp_prepare_req(req, NULL);
}

static int jh7110_cryp_cipher_one_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = container_of(areq,
			struct skcipher_request,
			base);
	struct jh7110_sec_request_ctx *rctx = skcipher_request_ctx(req);
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int ret;

	if (!sdev)
		return -ENODEV;

	ret = jh7110_cryp_cpu_start(ctx, rctx);

	mutex_unlock(&sdev->lock);

	return ret;
}

static int jh7110_cryp_prepare_aead_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request,
						base);
	return jh7110_cryp_prepare_req(NULL, req);
}

static int jh7110_cryp_aead_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request,
						base);
	struct jh7110_sec_request_ctx *rctx = aead_request_ctx(req);
	struct jh7110_sec_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct jh7110_sec_dev *sdev = ctx->sdev;

	if (!sdev)
		return -ENODEV;

	if (unlikely(!rctx->req.areq->assoclen &&
		     !jh7110_cryp_get_input_text_len(ctx))) {
		/* No input data to process: get tag and finish */
		jh7110_gcm_zero_message_data(ctx);
		jh7110_cryp_finish_req(ctx, 0);
		mutex_unlock(&ctx->sdev->lock);
		return 0;
	}

	return jh7110_cryp_xcm_start(ctx, rctx);
}

static struct skcipher_alg crypto_algs[] = {
{
	.base.cra_name		        = "ecb(aes)",
	.base.cra_driver_name	        = "jh7110-ecb-aes",
	.base.cra_priority		= 200,
	.base.cra_flags		        = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize		= AES_BLOCK_SIZE,
	.base.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
	.base.cra_alignmask		= 0xf,
	.base.cra_module		= THIS_MODULE,
	.init		                = jh7110_cryp_cra_init,
	.exit                           = jh7110_cryp_cra_exit,
	.min_keysize	                = AES_MIN_KEY_SIZE,
	.max_keysize	                = AES_MAX_KEY_SIZE,
	.setkey		                = jh7110_cryp_aes_setkey,
	.encrypt	                = jh7110_cryp_aes_ecb_encrypt,
	.decrypt	                = jh7110_cryp_aes_ecb_decrypt,
}, {
	.base.cra_name		        = "cbc(aes)",
	.base.cra_driver_name	        = "jh7110-cbc-aes",
	.base.cra_priority		= 200,
	.base.cra_flags		        = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize		= AES_BLOCK_SIZE,
	.base.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
	.base.cra_alignmask		= 0xf,
	.base.cra_module		= THIS_MODULE,
	.init		                = jh7110_cryp_cra_init,
	.exit                           = jh7110_cryp_cra_exit,
	.min_keysize	                = AES_MIN_KEY_SIZE,
	.max_keysize	                = AES_MAX_KEY_SIZE,
	.ivsize		                = AES_BLOCK_SIZE,
	.setkey		                = jh7110_cryp_aes_setkey,
	.encrypt	                = jh7110_cryp_aes_cbc_encrypt,
	.decrypt	                = jh7110_cryp_aes_cbc_decrypt,
}, {
	.base.cra_name		        = "ctr(aes)",
	.base.cra_driver_name	        = "jh7110-ctr-aes",
	.base.cra_priority		= 200,
	.base.cra_flags		        = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize		= 1,
	.base.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
	.base.cra_alignmask		= 0xf,
	.base.cra_module		= THIS_MODULE,
	.init		                = jh7110_cryp_cra_init,
	.exit                           = jh7110_cryp_cra_exit,
	.min_keysize	                = AES_MIN_KEY_SIZE,
	.max_keysize	                = AES_MAX_KEY_SIZE,
	.ivsize		                = AES_BLOCK_SIZE,
	.setkey		                = jh7110_cryp_aes_setkey,
	.encrypt	                = jh7110_cryp_aes_ctr_encrypt,
	.decrypt	                = jh7110_cryp_aes_ctr_decrypt,
}, {
	.base.cra_name		        = "cfb(aes)",
	.base.cra_driver_name	        = "jh7110-cfb-aes",
	.base.cra_priority		= 200,
	.base.cra_flags		        = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize		= AES_BLOCK_SIZE,
	.base.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
	.base.cra_alignmask		= 0xf,
	.base.cra_module		= THIS_MODULE,
	.init		                = jh7110_cryp_cra_init,
	.exit                           = jh7110_cryp_cra_exit,
	.min_keysize	                = AES_MIN_KEY_SIZE,
	.max_keysize	                = AES_MAX_KEY_SIZE,
	.ivsize		                = AES_BLOCK_SIZE,
	.setkey		                = jh7110_cryp_aes_setkey,
	.encrypt	                = jh7110_cryp_aes_cfb_encrypt,
	.decrypt	                = jh7110_cryp_aes_cfb_decrypt,
}, {
	.base.cra_name		        = "ofb(aes)",
	.base.cra_driver_name	        = "jh7110-ofb-aes",
	.base.cra_priority		= 200,
	.base.cra_flags		        = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize		= AES_BLOCK_SIZE,
	.base.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
	.base.cra_alignmask		= 0xf,
	.base.cra_module		= THIS_MODULE,
	.init		                = jh7110_cryp_cra_init,
	.exit                           = jh7110_cryp_cra_exit,
	.min_keysize	                = AES_MIN_KEY_SIZE,
	.max_keysize	                = AES_MAX_KEY_SIZE,
	.ivsize		                = AES_BLOCK_SIZE,
	.setkey		                = jh7110_cryp_aes_setkey,
	.encrypt	                = jh7110_cryp_aes_ofb_encrypt,
	.decrypt	                = jh7110_cryp_aes_ofb_decrypt,
},
};

static struct aead_alg aead_algs[] = {
{
	.setkey                         = jh7110_cryp_aes_aead_setkey,
	.setauthsize                    = jh7110_cryp_aes_gcm_setauthsize,
	.encrypt                        = jh7110_cryp_aes_gcm_encrypt,
	.decrypt                        = jh7110_cryp_aes_gcm_decrypt,
	.init                           = jh7110_cryp_aes_aead_init,
	.exit                           = jh7110_cryp_aes_aead_exit,
	.ivsize                         = GCM_AES_IV_SIZE,
	.maxauthsize                    = AES_BLOCK_SIZE,

	.base = {
		.cra_name               = "gcm(aes)",
		.cra_driver_name        = "jh7110-gcm-aes",
		.cra_priority           = 200,
		.cra_flags              = CRYPTO_ALG_ASYNC,
		.cra_blocksize          = 1,
		.cra_ctxsize            = sizeof(struct jh7110_sec_ctx),
		.cra_alignmask          = 0xf,
		.cra_module             = THIS_MODULE,
	},
}, {
	.setkey		                = jh7110_cryp_aes_aead_setkey,
	.setauthsize	                = jh7110_cryp_aes_ccm_setauthsize,
	.encrypt	                = jh7110_cryp_aes_ccm_encrypt,
	.decrypt	                = jh7110_cryp_aes_ccm_decrypt,
	.init		                = jh7110_cryp_aes_aead_init,
	.exit		                = jh7110_cryp_aes_aead_exit,
	.ivsize		                = AES_BLOCK_SIZE,
	.maxauthsize	                = AES_BLOCK_SIZE,

	.base = {
		.cra_name		= "ccm(aes)",
		.cra_driver_name	= "jh7110-ccm-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
};

int jh7110_aes_register_algs(void)
{
	int ret;

	ret = crypto_register_skciphers(crypto_algs, ARRAY_SIZE(crypto_algs));

	if (ret) {
		pr_err("Could not register algs\n");
		goto err_algs;
	}

	ret = crypto_register_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	if (ret)
		goto err_aead_algs;

	return 0;

err_aead_algs:
	crypto_unregister_skciphers(crypto_algs, ARRAY_SIZE(crypto_algs));
err_algs:
	return ret;
}

void jh7110_aes_unregister_algs(void)
{
	crypto_unregister_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	crypto_unregister_skciphers(crypto_algs, ARRAY_SIZE(crypto_algs));
}

MODULE_DESCRIPTION("AES acceleration driver for StarFive JH7110 SoC");
MODULE_AUTHOR("Jia Jie Ho <jiajie.ho@starfivetech.com>");
MODULE_LICENSE("GPL");
