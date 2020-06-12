/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef __RK_CRYPTO_BIGNUM_H__
#define __RK_CRYPTO_BIGNUM_H__

enum bignum_endian {
	RK_BG_BIG_ENDIAN,
	RK_BG_LITTILE_ENDIAN
};

/**
 * struct rk_bignum - crypto bignum struct.
 */
struct rk_bignum {
	u32 n_words;
	u32 *data;
};

struct rk_bignum *rk_bn_alloc(u32 max_size);
void rk_bn_free(struct rk_bignum *bn);
int rk_bn_set_data(struct rk_bignum *bn, const u8 *data, u32 size, enum bignum_endian endian);
int rk_bn_get_data(const struct rk_bignum *bn, u8 *data, u32 size, enum bignum_endian endian);
u32 rk_bn_get_size(const struct rk_bignum *bn);
int rk_bn_highest_bit(const struct rk_bignum *src);

#endif
