/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#ifndef _CIPHER_H_
#define _CIPHER_H_

#include "common.h"
#include "core.h"

#define QCE_MAX_KEY_SIZE	64

struct qce_cipher_ctx {
	u8 enc_key[QCE_MAX_KEY_SIZE];
	unsigned int enc_keylen;
	struct crypto_sync_skcipher *fallback;
};

/**
 * struct qce_cipher_reqctx - holds private cipher objects per request
 * @flags: operation flags
 * @iv: pointer to the IV
 * @ivsize: IV size
 * @src_nents: source entries
 * @dst_nents: destination entries
 * @result_sg: scatterlist used for result buffer
 * @dst_tbl: destination sg table
 * @dst_sg: destination sg pointer table beginning
 * @src_tbl: source sg table
 * @src_sg: source sg pointer table beginning;
 * @cryptlen: crypto length
 */
struct qce_cipher_reqctx {
	unsigned long flags;
	u8 *iv;
	unsigned int ivsize;
	int src_nents;
	int dst_nents;
	struct scatterlist result_sg;
	struct sg_table dst_tbl;
	struct scatterlist *dst_sg;
	struct sg_table src_tbl;
	struct scatterlist *src_sg;
	unsigned int cryptlen;
};

static inline struct qce_alg_template *to_cipher_tmpl(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	return container_of(alg, struct qce_alg_template, alg.crypto);
}

extern const struct qce_algo_ops ablkcipher_ops;

#endif /* _CIPHER_H_ */
