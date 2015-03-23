/*
 * Crypto wrapper for internal crypto implementation - modexp
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "tls/bignum.h"
#include "crypto.h"


int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	struct bignum *bn_base, *bn_exp, *bn_modulus, *bn_result;
	int ret = -1;

	bn_base = bignum_init();
	bn_exp = bignum_init();
	bn_modulus = bignum_init();
	bn_result = bignum_init();

	if (bn_base == NULL || bn_exp == NULL || bn_modulus == NULL ||
	    bn_result == NULL)
		goto error;

	if (bignum_set_unsigned_bin(bn_base, base, base_len) < 0 ||
	    bignum_set_unsigned_bin(bn_exp, power, power_len) < 0 ||
	    bignum_set_unsigned_bin(bn_modulus, modulus, modulus_len) < 0)
		goto error;

	if (bignum_exptmod(bn_base, bn_exp, bn_modulus, bn_result) < 0)
		goto error;

	ret = bignum_get_unsigned_bin(bn_result, result, result_len);

error:
	bignum_deinit(bn_base);
	bignum_deinit(bn_exp);
	bignum_deinit(bn_modulus);
	bignum_deinit(bn_result);
	return ret;
}
