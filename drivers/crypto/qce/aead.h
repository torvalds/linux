/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Linaro Limited. All rights reserved.
 */

#ifndef _AEAD_H_
#define _AEAD_H_

#include "common.h"
#include "core.h"

#define QCE_MAX_KEY_SIZE		64
#define QCE_CCM4309_SALT_SIZE		3

struct qce_aead_ctx {
	u8 enc_key[QCE_MAX_KEY_SIZE];
	u8 auth_key[QCE_MAX_KEY_SIZE];
	u8 ccm4309_salt[QCE_CCM4309_SALT_SIZE];
	unsigned int enc_keylen;
	unsigned int auth_keylen;
	unsigned int authsize;
	bool need_fallback;
	struct crypto_aead *fallback;
};

struct qce_aead_reqctx {
	unsigned long flags;
	u8 *iv;
	unsigned int ivsize;
	int src_nents;
	int dst_nents;
	struct scatterlist result_sg;
	struct scatterlist adata_sg;
	struct sg_table dst_tbl;
	struct sg_table src_tbl;
	struct scatterlist *dst_sg;
	struct scatterlist *src_sg;
	unsigned int cryptlen;
	unsigned int assoclen;
	unsigned char *adata;
	u8 ccm_nonce[QCE_MAX_NONCE];
	u8 ccmresult_buf[QCE_BAM_BURST_SIZE];
	u8 ccm_rfc4309_iv[QCE_MAX_IV_SIZE];
	struct aead_request fallback_req;
};

static inline struct qce_alg_template *to_aead_tmpl(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);

	return container_of(alg, struct qce_alg_template, alg.aead);
}

extern const struct qce_algo_ops aead_ops;

#endif /* _AEAD_H_ */
