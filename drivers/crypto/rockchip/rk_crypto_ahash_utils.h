/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_AHASH_UTILS_H__
#define __RK_CRYPTO_AHASH_UTILS_H__

#include <crypto/internal/hash.h>

#include "rk_crypto_core.h"
#include "rk_crypto_utils.h"

struct rk_alg_ctx *rk_ahash_alg_ctx(struct rk_crypto_dev *rk_dev);

struct rk_crypto_algt *rk_ahash_get_algt(struct crypto_ahash *tfm);

struct rk_ahash_ctx *rk_ahash_ctx_cast(struct rk_crypto_dev *rk_dev);

int rk_ahash_hmac_setkey(struct crypto_ahash *tfm, const u8 *key, unsigned int keylen);

int rk_ahash_init(struct ahash_request *req);

int rk_ahash_update(struct ahash_request *req);

int rk_ahash_final(struct ahash_request *req);

int rk_ahash_finup(struct ahash_request *req);

int rk_ahash_digest(struct ahash_request *req);

int rk_ahash_crypto_rx(struct rk_crypto_dev *rk_dev);

int rk_ahash_start(struct rk_crypto_dev *rk_dev);

#endif
