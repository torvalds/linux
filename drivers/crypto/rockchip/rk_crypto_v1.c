// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip Crypto V1
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */
#include "rk_crypto_core.h"
#include "rk_crypto_v1.h"

static const char * const crypto_v1_rsts[] = {
	"crypto-rst",
};

static struct rk_crypto_algt *crypto_v1_algs[] = {
	&rk_v1_ecb_aes_alg,		/* ecb(aes) */
	&rk_v1_cbc_aes_alg,		/* cbc(aes) */

	&rk_v1_ecb_des_alg,		/* ecb(des) */
	&rk_v1_cbc_des_alg,		/* cbc(des) */

	&rk_v1_ecb_des3_ede_alg,	/* ecb(des3_ede) */
	&rk_v1_cbc_des3_ede_alg,	/* cbc(des3_ede) */

	&rk_v1_ahash_sha1,		/* sha1 */
	&rk_v1_ahash_sha256,		/* sha256 */
	&rk_v1_ahash_md5,		/* md5 */
};

int rk_hw_crypto_v1_init(struct device *dev, void *hw_info)
{
	return 0;
}

void rk_hw_crypto_v1_deinit(struct device *dev, void *hw_info)
{

}

const char * const *rk_hw_crypto_v1_get_rsts(uint32_t *num)
{
	*num = ARRAY_SIZE(crypto_v1_rsts);

	return crypto_v1_rsts;
}

struct rk_crypto_algt **rk_hw_crypto_v1_get_algts(uint32_t *num)
{
	*num = ARRAY_SIZE(crypto_v1_algs);

	return crypto_v1_algs;
}

bool rk_hw_crypto_v1_algo_valid(struct rk_crypto_dev *rk_dev, struct rk_crypto_algt *aglt)
{
	return true;
}

