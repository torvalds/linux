/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_V3_H__
#define __RK_CRYPTO_V3_H__

#include <linux/platform_device.h>

#include "rk_crypto_utils.h"

struct rk_hw_crypto_v3_info {
	struct rk_hw_desc		hw_desc;
};

#define RK_CRYPTO_V3_SOC_DATA_INIT(names) {\
	.crypto_ver		= "CRYPTO V3.0.0.0 multi",\
	.use_soft_aes192	= false,\
	.valid_algs_name	= (names),\
	.valid_algs_num		= ARRAY_SIZE(names),\
	.hw_init		= rk_hw_crypto_v3_init,\
	.hw_deinit		= rk_hw_crypto_v3_deinit,\
	.hw_get_rsts		= rk_hw_crypto_v3_get_rsts,\
	.hw_get_algts		= rk_hw_crypto_v3_get_algts,\
	.hw_is_algo_valid	= rk_hw_crypto_v3_algo_valid,\
	.hw_info_size		= sizeof(struct rk_hw_crypto_v3_info),\
	.default_pka_offset	= 0x0480,\
	.use_lli_chain          = true,\
}

#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP_V3)

extern struct rk_crypto_algt rk_v3_ecb_sm4_alg;
extern struct rk_crypto_algt rk_v3_cbc_sm4_alg;
extern struct rk_crypto_algt rk_v3_xts_sm4_alg;
extern struct rk_crypto_algt rk_v3_cfb_sm4_alg;
extern struct rk_crypto_algt rk_v3_ofb_sm4_alg;
extern struct rk_crypto_algt rk_v3_ctr_sm4_alg;
extern struct rk_crypto_algt rk_v3_gcm_sm4_alg;

extern struct rk_crypto_algt rk_v3_ecb_aes_alg;
extern struct rk_crypto_algt rk_v3_cbc_aes_alg;
extern struct rk_crypto_algt rk_v3_xts_aes_alg;
extern struct rk_crypto_algt rk_v3_cfb_aes_alg;
extern struct rk_crypto_algt rk_v3_ofb_aes_alg;
extern struct rk_crypto_algt rk_v3_ctr_aes_alg;
extern struct rk_crypto_algt rk_v3_gcm_aes_alg;

extern struct rk_crypto_algt rk_v3_ecb_des_alg;
extern struct rk_crypto_algt rk_v3_cbc_des_alg;
extern struct rk_crypto_algt rk_v3_cfb_des_alg;
extern struct rk_crypto_algt rk_v3_ofb_des_alg;

extern struct rk_crypto_algt rk_v3_ecb_des3_ede_alg;
extern struct rk_crypto_algt rk_v3_cbc_des3_ede_alg;
extern struct rk_crypto_algt rk_v3_cfb_des3_ede_alg;
extern struct rk_crypto_algt rk_v3_ofb_des3_ede_alg;

extern struct rk_crypto_algt rk_v3_ahash_sha1;
extern struct rk_crypto_algt rk_v3_ahash_sha224;
extern struct rk_crypto_algt rk_v3_ahash_sha256;
extern struct rk_crypto_algt rk_v3_ahash_sha384;
extern struct rk_crypto_algt rk_v3_ahash_sha512;
extern struct rk_crypto_algt rk_v3_ahash_md5;
extern struct rk_crypto_algt rk_v3_ahash_sm3;

extern struct rk_crypto_algt rk_v3_hmac_md5;
extern struct rk_crypto_algt rk_v3_hmac_sha1;
extern struct rk_crypto_algt rk_v3_hmac_sha256;
extern struct rk_crypto_algt rk_v3_hmac_sha512;
extern struct rk_crypto_algt rk_v3_hmac_sm3;

/* Shared v2 version implementation */
extern struct rk_crypto_algt rk_v2_asym_rsa;

int rk_hw_crypto_v3_init(struct device *dev, void *hw_info);
void rk_hw_crypto_v3_deinit(struct device *dev, void *hw_info);
const char * const *rk_hw_crypto_v3_get_rsts(uint32_t *num);
struct rk_crypto_algt **rk_hw_crypto_v3_get_algts(uint32_t *num);
bool rk_hw_crypto_v3_algo_valid(struct rk_crypto_dev *rk_dev, struct rk_crypto_algt *aglt);

#else

static inline int rk_hw_crypto_v3_init(struct device *dev, void *hw_info) { return -EINVAL; }
static inline void rk_hw_crypto_v3_deinit(struct device *dev, void *hw_info) {}
static inline const char * const *rk_hw_crypto_v3_get_rsts(uint32_t *num) { return NULL; }
static inline struct rk_crypto_algt **rk_hw_crypto_v3_get_algts(uint32_t *num) { return NULL; }
static inline bool rk_hw_crypto_v3_algo_valid(struct rk_crypto_dev *rk_dev,
					      struct rk_crypto_algt *aglt)
{
	return false;
}

#endif /* end of IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP_V3) */

#endif /* end of __RK_CRYPTO_V3_H__ */
