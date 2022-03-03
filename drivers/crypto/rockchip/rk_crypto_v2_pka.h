/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_V2_PKA_H__
#define __RK_CRYPTO_V2_PKA_H__

#include "rk_crypto_bignum.h"

void rk_pka_set_crypto_base(void __iomem *base);

int rk_pka_expt_mod(struct rk_bignum *in,
		    struct rk_bignum *e,
		    struct rk_bignum *n,
		    struct rk_bignum *out);

#endif
