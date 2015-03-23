/*
 * Big number math
 * Copyright (c) 2006, Jouni Malinen <j@w1.fi>
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

#ifndef BIGNUM_H
#define BIGNUM_H

struct bignum;

struct bignum * bignum_init(void);
void bignum_deinit(struct bignum *n);
size_t bignum_get_unsigned_bin_len(struct bignum *n);
int bignum_get_unsigned_bin(const struct bignum *n, u8 *buf, size_t *len);
int bignum_set_unsigned_bin(struct bignum *n, const u8 *buf, size_t len);
int bignum_cmp(const struct bignum *a, const struct bignum *b);
int bignum_cmp_d(const struct bignum *a, unsigned long b);
int bignum_add(const struct bignum *a, const struct bignum *b,
	       struct bignum *c);
int bignum_sub(const struct bignum *a, const struct bignum *b,
	       struct bignum *c);
int bignum_mul(const struct bignum *a, const struct bignum *b,
	       struct bignum *c);
int bignum_mulmod(const struct bignum *a, const struct bignum *b,
		  const struct bignum *c, struct bignum *d);
int bignum_exptmod(const struct bignum *a, const struct bignum *b,
		   const struct bignum *c, struct bignum *d);

#endif /* BIGNUM_H */
