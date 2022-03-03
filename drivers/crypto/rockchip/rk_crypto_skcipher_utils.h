/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_SKCIPHER_UTILS_H__
#define __RK_CRYPTO_SKCIPHER_UTILS_H__

#include <crypto/skcipher.h>
#include <crypto/internal/skcipher.h>

#include "rk_crypto_core.h"
#include "rk_crypto_utils.h"

struct rk_crypto_algt *rk_cipher_get_algt(struct crypto_skcipher *tfm);

struct rk_alg_ctx *rk_cipher_alg_ctx(struct rk_crypto_dev *rk_dev);

int rk_cipher_fallback(struct skcipher_request *req, struct rk_cipher_ctx *ctx, bool encrypt);

int rk_cipher_setkey(struct crypto_skcipher *cipher, const u8 *key, unsigned int keylen);

int rk_ablk_rx(struct rk_crypto_dev *rk_dev);

int rk_ablk_start(struct rk_crypto_dev *rk_dev);

int rk_skcipher_handle_req(struct rk_crypto_dev *rk_dev, struct skcipher_request *req);

#endif

