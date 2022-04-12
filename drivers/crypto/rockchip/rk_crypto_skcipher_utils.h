/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_SKCIPHER_UTILS_H__
#define __RK_CRYPTO_SKCIPHER_UTILS_H__

#include <crypto/aead.h>
#include <crypto/skcipher.h>
#include <crypto/internal/skcipher.h>

#include "rk_crypto_core.h"
#include "rk_crypto_utils.h"

#define RK_MAX_TAG_SIZE		32

struct rk_crypto_algt *rk_cipher_get_algt(struct crypto_skcipher *tfm);

struct rk_crypto_algt *rk_aead_get_algt(struct crypto_aead *tfm);

struct rk_alg_ctx *rk_cipher_alg_ctx(struct rk_crypto_dev *rk_dev);

struct rk_cipher_ctx *rk_cipher_ctx_cast(struct rk_crypto_dev *rk_dev);

int rk_cipher_fallback(struct skcipher_request *req, struct rk_cipher_ctx *ctx, bool encrypt);

int rk_cipher_setkey(struct crypto_skcipher *cipher, const u8 *key, unsigned int keylen);

int rk_ablk_rx(struct rk_crypto_dev *rk_dev);

int rk_ablk_start(struct rk_crypto_dev *rk_dev);

int rk_skcipher_handle_req(struct rk_crypto_dev *rk_dev, struct skcipher_request *req);

int rk_aead_fallback(struct aead_request *req, struct rk_cipher_ctx *ctx, bool encrypt);

int rk_aead_setkey(struct crypto_aead *cipher, const u8 *key, unsigned int keylen);

int rk_aead_start(struct rk_crypto_dev *rk_dev);

int rk_aead_gcm_setauthsize(struct crypto_aead *tfm, unsigned int authsize);

int rk_aead_handle_req(struct rk_crypto_dev *rk_dev, struct aead_request *req);

#endif

