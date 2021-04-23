/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_V1_H__
#define __RK_CRYPTO_V1_H__

#include <linux/platform_device.h>

struct rk_hw_crypto_v1_info {
	int	reserved;
};

extern struct rk_crypto_algt rk_v1_ecb_aes_alg;
extern struct rk_crypto_algt rk_v1_cbc_aes_alg;

extern struct rk_crypto_algt rk_v1_ecb_des_alg;
extern struct rk_crypto_algt rk_v1_cbc_des_alg;

extern struct rk_crypto_algt rk_v1_ecb_des3_ede_alg;
extern struct rk_crypto_algt rk_v1_cbc_des3_ede_alg;

extern struct rk_crypto_algt rk_v1_ahash_sha1;
extern struct rk_crypto_algt rk_v1_ahash_sha256;
extern struct rk_crypto_algt rk_v1_ahash_md5;

int rk_hw_crypto_v1_init(struct device *dev, void *hw_info);
void rk_hw_crypto_v1_deinit(struct device *dev, void *hw_info);
#endif

