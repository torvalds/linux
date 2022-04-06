/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_V1_H__
#define __RK_CRYPTO_V1_H__

#include <linux/platform_device.h>

struct rk_hw_crypto_v1_info {
	int	reserved;
};

#define RK_CRYPTO_V1_SOC_DATA_INIT(names) {\
	.crypto_ver		= "CRYPTO V1.0.0.0",\
	.use_soft_aes192	= false,\
	.valid_algs_name	= (names),\
	.valid_algs_num		= ARRAY_SIZE(names),\
	.hw_init		= rk_hw_crypto_v1_init,\
	.hw_deinit		= rk_hw_crypto_v1_deinit,\
	.hw_get_rsts		= rk_hw_crypto_v1_get_rsts,\
	.hw_get_algts		= rk_hw_crypto_v1_get_algts,\
	.hw_is_algo_valid	= rk_hw_crypto_v1_algo_valid,\
	.hw_info_size		= sizeof(struct rk_hw_crypto_v1_info),\
	.default_pka_offset	= 0,\
	.use_lli_chain          = false,\
}

#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP_V1)

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
const char * const *rk_hw_crypto_v1_get_rsts(uint32_t *num);
struct rk_crypto_algt **rk_hw_crypto_v1_get_algts(uint32_t *num);
bool rk_hw_crypto_v1_algo_valid(struct rk_crypto_dev *rk_dev, struct rk_crypto_algt *aglt);

#else

static inline int rk_hw_crypto_v1_init(struct device *dev, void *hw_info) { return -EINVAL; }
static inline void rk_hw_crypto_v1_deinit(struct device *dev, void *hw_info) {}
static inline const char * const *rk_hw_crypto_v1_get_rsts(uint32_t *num) { return NULL; }
static inline struct rk_crypto_algt **rk_hw_crypto_v1_get_algts(uint32_t *num) { return NULL; }
static inline bool rk_hw_crypto_v1_algo_valid(struct rk_crypto_dev *rk_dev,
					      struct rk_crypto_algt *aglt)
{
	return false;
}

#endif /* end of IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP_V1) */

#endif /* end of __RK_CRYPTO_V1_H__ */

