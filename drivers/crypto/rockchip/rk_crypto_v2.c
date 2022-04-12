// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip Crypto V2
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include "rk_crypto_core.h"
#include "rk_crypto_v2.h"

static const char * const crypto_v2_rsts[] = {
	"crypto-rst",
};

static struct rk_crypto_algt *crypto_v2_algs[] = {
	&rk_v2_ecb_sm4_alg,		/* ecb(sm4) */
	&rk_v2_cbc_sm4_alg,		/* cbc(sm4) */
	&rk_v2_xts_sm4_alg,		/* xts(sm4) */
	&rk_v2_cfb_sm4_alg,		/* cfb(sm4) */
	&rk_v2_ofb_sm4_alg,		/* ofb(sm4) */
	&rk_v2_ctr_sm4_alg,		/* ctr(sm4) */
	&rk_v2_gcm_sm4_alg,		/* gcm(sm4) */

	&rk_v2_ecb_aes_alg,		/* ecb(aes) */
	&rk_v2_cbc_aes_alg,		/* cbc(aes) */
	&rk_v2_xts_aes_alg,		/* xts(aes) */
	&rk_v2_cfb_aes_alg,		/* cfb(aes) */
	&rk_v2_ofb_aes_alg,		/* ofb(aes) */
	&rk_v2_ctr_aes_alg,		/* ctr(aes) */
	&rk_v2_gcm_aes_alg,		/* gcm(aes) */

	&rk_v2_ecb_des_alg,		/* ecb(des) */
	&rk_v2_cbc_des_alg,		/* cbc(des) */
	&rk_v2_cfb_des_alg,		/* cfb(des) */
	&rk_v2_ofb_des_alg,		/* ofb(des) */

	&rk_v2_ecb_des3_ede_alg,	/* ecb(des3_ede) */
	&rk_v2_cbc_des3_ede_alg,	/* cbc(des3_ede) */
	&rk_v2_cfb_des3_ede_alg,	/* cfb(des3_ede) */
	&rk_v2_ofb_des3_ede_alg,	/* ofb(des3_ede) */

	&rk_v2_ahash_sha1,		/* sha1 */
	&rk_v2_ahash_sha224,		/* sha224 */
	&rk_v2_ahash_sha256,		/* sha256 */
	&rk_v2_ahash_sha384,		/* sha384 */
	&rk_v2_ahash_sha512,		/* sha512 */
	&rk_v2_ahash_md5,		/* md5 */
	&rk_v2_ahash_sm3,		/* sm3 */

	&rk_v2_hmac_sha1,		/* hmac(sha1) */
	&rk_v2_hmac_sha256,		/* hmac(sha256) */
	&rk_v2_hmac_sha512,		/* hmac(sha512) */
	&rk_v2_hmac_md5,		/* hmac(md5) */
	&rk_v2_hmac_sm3,		/* hmac(sm3) */

	&rk_v2_asym_rsa,		/* rsa */
};

int rk_hw_crypto_v2_init(struct device *dev, void *hw_info)
{
	struct rk_hw_crypto_v2_info *info =
		(struct rk_hw_crypto_v2_info *)hw_info;

	if (!dev || !hw_info)
		return -EINVAL;

	memset(info, 0x00, sizeof(*info));

	return rk_crypto_hw_desc_alloc(dev, &info->hw_desc);
}

void rk_hw_crypto_v2_deinit(struct device *dev, void *hw_info)
{
	struct rk_hw_crypto_v2_info *info =
		(struct rk_hw_crypto_v2_info *)hw_info;

	if (!dev || !hw_info)
		return;

	rk_crypto_hw_desc_free(&info->hw_desc);
}

const char * const *rk_hw_crypto_v2_get_rsts(uint32_t *num)
{
	*num = ARRAY_SIZE(crypto_v2_rsts);

	return crypto_v2_rsts;
}

struct rk_crypto_algt **rk_hw_crypto_v2_get_algts(uint32_t *num)
{
	*num = ARRAY_SIZE(crypto_v2_algs);

	return crypto_v2_algs;
}

bool rk_hw_crypto_v2_algo_valid(struct rk_crypto_dev *rk_dev, struct rk_crypto_algt *aglt)
{
	return true;
}

