/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_V2_H__
#define __RK_CRYPTO_V2_H__

#include <linux/platform_device.h>

struct crypto_lli_desc {
	u32 src_addr;
	u32 src_len;
	u32 dst_addr;
	u32 dst_len;
	u32 user_define;
	u32 reserve;
	u32 dma_ctrl;
	u32 next_addr;
};

struct rk_hw_crypto_v2_info {
	struct crypto_lli_desc		*desc;
	dma_addr_t			desc_dma;
	int				clk_enable;
};

extern struct rk_crypto_tmp rk_v2_ecb_aes_alg;
extern struct rk_crypto_tmp rk_v2_cbc_aes_alg;
extern struct rk_crypto_tmp rk_v2_ecb_des_alg;
extern struct rk_crypto_tmp rk_v2_xts_aes_alg;
extern struct rk_crypto_tmp rk_v2_cbc_des_alg;
extern struct rk_crypto_tmp rk_v2_ecb_des3_ede_alg;
extern struct rk_crypto_tmp rk_v2_cbc_des3_ede_alg;

int rk_hw_crypto_v2_init(struct device *dev, void *hw_info);
void rk_hw_crypto_v2_deinit(struct device *dev, void *hw_info);
#endif

