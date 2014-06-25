/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha.h>

#include "cipher.h"
#include "common.h"
#include "core.h"
#include "regs-v5.h"
#include "sha.h"

#define QCE_SECTOR_SIZE		512

static inline u32 qce_read(struct qce_device *qce, u32 offset)
{
	return readl(qce->base + offset);
}

static inline void qce_write(struct qce_device *qce, u32 offset, u32 val)
{
	writel(val, qce->base + offset);
}

static inline void qce_write_array(struct qce_device *qce, u32 offset,
				   const u32 *val, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++)
		qce_write(qce, offset + i * sizeof(u32), val[i]);
}

static inline void
qce_clear_array(struct qce_device *qce, u32 offset, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++)
		qce_write(qce, offset + i * sizeof(u32), 0);
}

static u32 qce_encr_cfg(unsigned long flags, u32 aes_key_size)
{
	u32 cfg = 0;

	if (IS_AES(flags)) {
		if (aes_key_size == AES_KEYSIZE_128)
			cfg |= ENCR_KEY_SZ_AES128 << ENCR_KEY_SZ_SHIFT;
		else if (aes_key_size == AES_KEYSIZE_256)
			cfg |= ENCR_KEY_SZ_AES256 << ENCR_KEY_SZ_SHIFT;
	}

	if (IS_AES(flags))
		cfg |= ENCR_ALG_AES << ENCR_ALG_SHIFT;
	else if (IS_DES(flags) || IS_3DES(flags))
		cfg |= ENCR_ALG_DES << ENCR_ALG_SHIFT;

	if (IS_DES(flags))
		cfg |= ENCR_KEY_SZ_DES << ENCR_KEY_SZ_SHIFT;

	if (IS_3DES(flags))
		cfg |= ENCR_KEY_SZ_3DES << ENCR_KEY_SZ_SHIFT;

	switch (flags & QCE_MODE_MASK) {
	case QCE_MODE_ECB:
		cfg |= ENCR_MODE_ECB << ENCR_MODE_SHIFT;
		break;
	case QCE_MODE_CBC:
		cfg |= ENCR_MODE_CBC << ENCR_MODE_SHIFT;
		break;
	case QCE_MODE_CTR:
		cfg |= ENCR_MODE_CTR << ENCR_MODE_SHIFT;
		break;
	case QCE_MODE_XTS:
		cfg |= ENCR_MODE_XTS << ENCR_MODE_SHIFT;
		break;
	case QCE_MODE_CCM:
		cfg |= ENCR_MODE_CCM << ENCR_MODE_SHIFT;
		cfg |= LAST_CCM_XFR << LAST_CCM_SHIFT;
		break;
	default:
		return ~0;
	}

	return cfg;
}

static u32 qce_auth_cfg(unsigned long flags, u32 key_size)
{
	u32 cfg = 0;

	if (IS_AES(flags) && (IS_CCM(flags) || IS_CMAC(flags)))
		cfg |= AUTH_ALG_AES << AUTH_ALG_SHIFT;
	else
		cfg |= AUTH_ALG_SHA << AUTH_ALG_SHIFT;

	if (IS_CCM(flags) || IS_CMAC(flags)) {
		if (key_size == AES_KEYSIZE_128)
			cfg |= AUTH_KEY_SZ_AES128 << AUTH_KEY_SIZE_SHIFT;
		else if (key_size == AES_KEYSIZE_256)
			cfg |= AUTH_KEY_SZ_AES256 << AUTH_KEY_SIZE_SHIFT;
	}

	if (IS_SHA1(flags) || IS_SHA1_HMAC(flags))
		cfg |= AUTH_SIZE_SHA1 << AUTH_SIZE_SHIFT;
	else if (IS_SHA256(flags) || IS_SHA256_HMAC(flags))
		cfg |= AUTH_SIZE_SHA256 << AUTH_SIZE_SHIFT;
	else if (IS_CMAC(flags))
		cfg |= AUTH_SIZE_ENUM_16_BYTES << AUTH_SIZE_SHIFT;

	if (IS_SHA1(flags) || IS_SHA256(flags))
		cfg |= AUTH_MODE_HASH << AUTH_MODE_SHIFT;
	else if (IS_SHA1_HMAC(flags) || IS_SHA256_HMAC(flags) ||
		 IS_CBC(flags) || IS_CTR(flags))
		cfg |= AUTH_MODE_HMAC << AUTH_MODE_SHIFT;
	else if (IS_AES(flags) && IS_CCM(flags))
		cfg |= AUTH_MODE_CCM << AUTH_MODE_SHIFT;
	else if (IS_AES(flags) && IS_CMAC(flags))
		cfg |= AUTH_MODE_CMAC << AUTH_MODE_SHIFT;

	if (IS_SHA(flags) || IS_SHA_HMAC(flags))
		cfg |= AUTH_POS_BEFORE << AUTH_POS_SHIFT;

	if (IS_CCM(flags))
		cfg |= QCE_MAX_NONCE_WORDS << AUTH_NONCE_NUM_WORDS_SHIFT;

	if (IS_CBC(flags) || IS_CTR(flags) || IS_CCM(flags) ||
	    IS_CMAC(flags))
		cfg |= BIT(AUTH_LAST_SHIFT) | BIT(AUTH_FIRST_SHIFT);

	return cfg;
}

static u32 qce_config_reg(struct qce_device *qce, int little)
{
	u32 beats = (qce->burst_size >> 3) - 1;
	u32 pipe_pair = qce->pipe_pair_id;
	u32 config;

	config = (beats << REQ_SIZE_SHIFT) & REQ_SIZE_MASK;
	config |= BIT(MASK_DOUT_INTR_SHIFT) | BIT(MASK_DIN_INTR_SHIFT) |
		  BIT(MASK_OP_DONE_INTR_SHIFT) | BIT(MASK_ERR_INTR_SHIFT);
	config |= (pipe_pair << PIPE_SET_SELECT_SHIFT) & PIPE_SET_SELECT_MASK;
	config &= ~HIGH_SPD_EN_N_SHIFT;

	if (little)
		config |= BIT(LITTLE_ENDIAN_MODE_SHIFT);

	return config;
}

void qce_cpu_to_be32p_array(__be32 *dst, const u8 *src, unsigned int len)
{
	__be32 *d = dst;
	const u8 *s = src;
	unsigned int n;

	n = len / sizeof(u32);
	for (; n > 0; n--) {
		*d = cpu_to_be32p((const __u32 *) s);
		s += sizeof(__u32);
		d++;
	}
}

static void qce_xts_swapiv(__be32 *dst, const u8 *src, unsigned int ivsize)
{
	u8 swap[QCE_AES_IV_LENGTH];
	u32 i, j;

	if (ivsize > QCE_AES_IV_LENGTH)
		return;

	memset(swap, 0, QCE_AES_IV_LENGTH);

	for (i = (QCE_AES_IV_LENGTH - ivsize), j = ivsize - 1;
	     i < QCE_AES_IV_LENGTH; i++, j--)
		swap[i] = src[j];

	qce_cpu_to_be32p_array(dst, swap, QCE_AES_IV_LENGTH);
}

static void qce_xtskey(struct qce_device *qce, const u8 *enckey,
		       unsigned int enckeylen, unsigned int cryptlen)
{
	u32 xtskey[QCE_MAX_CIPHER_KEY_SIZE / sizeof(u32)] = {0};
	unsigned int xtsklen = enckeylen / (2 * sizeof(u32));
	unsigned int xtsdusize;

	qce_cpu_to_be32p_array(xtskey, enckey + enckeylen / 2, enckeylen / 2);
	qce_write_array(qce, REG_ENCR_XTS_KEY0, xtskey, xtsklen);

	/* xts du size 512B */
	xtsdusize = min_t(u32, QCE_SECTOR_SIZE, cryptlen);
	qce_write(qce, REG_ENCR_XTS_DU_SIZE, xtsdusize);
}

static void qce_setup_config(struct qce_device *qce)
{
	u32 config;

	/* get big endianness */
	config = qce_config_reg(qce, 0);

	/* clear status */
	qce_write(qce, REG_STATUS, 0);
	qce_write(qce, REG_CONFIG, config);
}

static inline void qce_crypto_go(struct qce_device *qce)
{
	qce_write(qce, REG_GOPROC, BIT(GO_SHIFT) | BIT(RESULTS_DUMP_SHIFT));
}

static int qce_setup_regs_ahash(struct crypto_async_request *async_req,
				u32 totallen, u32 offset)
{
	struct ahash_request *req = ahash_request_cast(async_req);
	struct crypto_ahash *ahash = __crypto_ahash_cast(async_req->tfm);
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_alg_template *tmpl = to_ahash_tmpl(async_req->tfm);
	struct qce_device *qce = tmpl->qce;
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	unsigned int blocksize = crypto_tfm_alg_blocksize(async_req->tfm);
	__be32 auth[SHA256_DIGEST_SIZE / sizeof(__be32)] = {0};
	__be32 mackey[QCE_SHA_HMAC_KEY_SIZE / sizeof(__be32)] = {0};
	u32 auth_cfg = 0, config;
	unsigned int iv_words;

	/* if not the last, the size has to be on the block boundary */
	if (!rctx->last_blk && req->nbytes % blocksize)
		return -EINVAL;

	qce_setup_config(qce);

	if (IS_CMAC(rctx->flags)) {
		qce_write(qce, REG_AUTH_SEG_CFG, 0);
		qce_write(qce, REG_ENCR_SEG_CFG, 0);
		qce_write(qce, REG_ENCR_SEG_SIZE, 0);
		qce_clear_array(qce, REG_AUTH_IV0, 16);
		qce_clear_array(qce, REG_AUTH_KEY0, 16);
		qce_clear_array(qce, REG_AUTH_BYTECNT0, 4);

		auth_cfg = qce_auth_cfg(rctx->flags, rctx->authklen);
	}

	if (IS_SHA_HMAC(rctx->flags) || IS_CMAC(rctx->flags)) {
		u32 authkey_words = rctx->authklen / sizeof(u32);

		qce_cpu_to_be32p_array(mackey, rctx->authkey, rctx->authklen);
		qce_write_array(qce, REG_AUTH_KEY0, mackey, authkey_words);
	}

	if (IS_CMAC(rctx->flags))
		goto go_proc;

	if (rctx->first_blk)
		memcpy(auth, rctx->digest, digestsize);
	else
		qce_cpu_to_be32p_array(auth, rctx->digest, digestsize);

	iv_words = (IS_SHA1(rctx->flags) || IS_SHA1_HMAC(rctx->flags)) ? 5 : 8;
	qce_write_array(qce, REG_AUTH_IV0, auth, iv_words);

	if (rctx->first_blk)
		qce_clear_array(qce, REG_AUTH_BYTECNT0, 4);
	else
		qce_write_array(qce, REG_AUTH_BYTECNT0, rctx->byte_count, 2);

	auth_cfg = qce_auth_cfg(rctx->flags, 0);

	if (rctx->last_blk)
		auth_cfg |= BIT(AUTH_LAST_SHIFT);
	else
		auth_cfg &= ~BIT(AUTH_LAST_SHIFT);

	if (rctx->first_blk)
		auth_cfg |= BIT(AUTH_FIRST_SHIFT);
	else
		auth_cfg &= ~BIT(AUTH_FIRST_SHIFT);

go_proc:
	qce_write(qce, REG_AUTH_SEG_CFG, auth_cfg);
	qce_write(qce, REG_AUTH_SEG_SIZE, req->nbytes);
	qce_write(qce, REG_AUTH_SEG_START, 0);
	qce_write(qce, REG_ENCR_SEG_CFG, 0);
	qce_write(qce, REG_SEG_SIZE, req->nbytes);

	/* get little endianness */
	config = qce_config_reg(qce, 1);
	qce_write(qce, REG_CONFIG, config);

	qce_crypto_go(qce);

	return 0;
}

static int qce_setup_regs_ablkcipher(struct crypto_async_request *async_req,
				     u32 totallen, u32 offset)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(async_req);
	struct qce_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct qce_cipher_ctx *ctx = crypto_tfm_ctx(async_req->tfm);
	struct qce_alg_template *tmpl = to_cipher_tmpl(async_req->tfm);
	struct qce_device *qce = tmpl->qce;
	__be32 enckey[QCE_MAX_CIPHER_KEY_SIZE / sizeof(__be32)] = {0};
	__be32 enciv[QCE_MAX_IV_SIZE / sizeof(__be32)] = {0};
	unsigned int enckey_words, enciv_words;
	unsigned int keylen;
	u32 encr_cfg = 0, auth_cfg = 0, config;
	unsigned int ivsize = rctx->ivsize;
	unsigned long flags = rctx->flags;

	qce_setup_config(qce);

	if (IS_XTS(flags))
		keylen = ctx->enc_keylen / 2;
	else
		keylen = ctx->enc_keylen;

	qce_cpu_to_be32p_array(enckey, ctx->enc_key, keylen);
	enckey_words = keylen / sizeof(u32);

	qce_write(qce, REG_AUTH_SEG_CFG, auth_cfg);

	encr_cfg = qce_encr_cfg(flags, keylen);

	if (IS_DES(flags)) {
		enciv_words = 2;
		enckey_words = 2;
	} else if (IS_3DES(flags)) {
		enciv_words = 2;
		enckey_words = 6;
	} else if (IS_AES(flags)) {
		if (IS_XTS(flags))
			qce_xtskey(qce, ctx->enc_key, ctx->enc_keylen,
				   rctx->cryptlen);
		enciv_words = 4;
	} else {
		return -EINVAL;
	}

	qce_write_array(qce, REG_ENCR_KEY0, enckey, enckey_words);

	if (!IS_ECB(flags)) {
		if (IS_XTS(flags))
			qce_xts_swapiv(enciv, rctx->iv, ivsize);
		else
			qce_cpu_to_be32p_array(enciv, rctx->iv, ivsize);

		qce_write_array(qce, REG_CNTR0_IV0, enciv, enciv_words);
	}

	if (IS_ENCRYPT(flags))
		encr_cfg |= BIT(ENCODE_SHIFT);

	qce_write(qce, REG_ENCR_SEG_CFG, encr_cfg);
	qce_write(qce, REG_ENCR_SEG_SIZE, rctx->cryptlen);
	qce_write(qce, REG_ENCR_SEG_START, offset & 0xffff);

	if (IS_CTR(flags)) {
		qce_write(qce, REG_CNTR_MASK, ~0);
		qce_write(qce, REG_CNTR_MASK0, ~0);
		qce_write(qce, REG_CNTR_MASK1, ~0);
		qce_write(qce, REG_CNTR_MASK2, ~0);
	}

	qce_write(qce, REG_SEG_SIZE, totallen);

	/* get little endianness */
	config = qce_config_reg(qce, 1);
	qce_write(qce, REG_CONFIG, config);

	qce_crypto_go(qce);

	return 0;
}

int qce_start(struct crypto_async_request *async_req, u32 type, u32 totallen,
	      u32 offset)
{
	switch (type) {
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		return qce_setup_regs_ablkcipher(async_req, totallen, offset);
	case CRYPTO_ALG_TYPE_AHASH:
		return qce_setup_regs_ahash(async_req, totallen, offset);
	default:
		return -EINVAL;
	}
}

#define STATUS_ERRORS	\
		(BIT(SW_ERR_SHIFT) | BIT(AXI_ERR_SHIFT) | BIT(HSD_ERR_SHIFT))

int qce_check_status(struct qce_device *qce, u32 *status)
{
	int ret = 0;

	*status = qce_read(qce, REG_STATUS);

	/*
	 * Don't use result dump status. The operation may not be complete.
	 * Instead, use the status we just read from device. In case, we need to
	 * use result_status from result dump the result_status needs to be byte
	 * swapped, since we set the device to little endian.
	 */
	if (*status & STATUS_ERRORS || !(*status & BIT(OPERATION_DONE_SHIFT)))
		ret = -ENXIO;

	return ret;
}

void qce_get_version(struct qce_device *qce, u32 *major, u32 *minor, u32 *step)
{
	u32 val;

	val = qce_read(qce, REG_VERSION);
	*major = (val & CORE_MAJOR_REV_MASK) >> CORE_MAJOR_REV_SHIFT;
	*minor = (val & CORE_MINOR_REV_MASK) >> CORE_MINOR_REV_SHIFT;
	*step = (val & CORE_STEP_REV_MASK) >> CORE_STEP_REV_SHIFT;
}
