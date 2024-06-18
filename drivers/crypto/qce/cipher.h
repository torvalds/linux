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
	struct crypto_skcipher *fallback;
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
	struct scatterlist *src_sg;
	unsigned int cryptlen;
	struct skcipher_request fallback_req;	// keep at the end
};

static inline struct qce_alg_template *to_cipher_tmpl(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	return container_of(alg, struct qce_alg_template, alg.skcipher);
}

extern const struct qce_algo_ops skcipher_ops;

#endif /* _CIPHER_H_ */
