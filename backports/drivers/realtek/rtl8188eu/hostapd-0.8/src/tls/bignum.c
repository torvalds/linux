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

#include "includes.h"

#include "common.h"
#include "bignum.h"

#ifdef CONFIG_INTERNAL_LIBTOMMATH
#include "libtommath.c"
#else /* CONFIG_INTERNAL_LIBTOMMATH */
#include <tommath.h>
#endif /* CONFIG_INTERNAL_LIBTOMMATH */


/*
 * The current version is just a wrapper for LibTomMath library, so
 * struct bignum is just typecast to mp_int.
 */

/**
 * bignum_init - Allocate memory for bignum
 * Returns: Pointer to allocated bignum or %NULL on failure
 */
struct bignum * bignum_init(void)
{
	struct bignum *n = os_zalloc(sizeof(mp_int));
	if (n == NULL)
		return NULL;
	if (mp_init((mp_int *) n) != MP_OKAY) {
		os_free(n);
		n = NULL;
	}
	return n;
}


/**
 * bignum_deinit - Free bignum
 * @n: Bignum from bignum_init()
 */
void bignum_deinit(struct bignum *n)
{
	if (n) {
		mp_clear((mp_int *) n);
		os_free(n);
	}
}


/**
 * bignum_get_unsigned_bin - Get length of bignum as an unsigned binary buffer
 * @n: Bignum from bignum_init()
 * Returns: Length of n if written to a binary buffer
 */
size_t bignum_get_unsigned_bin_len(struct bignum *n)
{
	return mp_unsigned_bin_size((mp_int *) n);
}


/**
 * bignum_get_unsigned_bin - Set binary buffer to unsigned bignum
 * @n: Bignum from bignum_init()
 * @buf: Buffer for the binary number
 * @len: Length of the buffer, can be %NULL if buffer is known to be long
 * enough. Set to used buffer length on success if not %NULL.
 * Returns: 0 on success, -1 on failure
 */
int bignum_get_unsigned_bin(const struct bignum *n, u8 *buf, size_t *len)
{
	size_t need = mp_unsigned_bin_size((mp_int *) n);
	if (len && need > *len) {
		*len = need;
		return -1;
	}
	if (mp_to_unsigned_bin((mp_int *) n, buf) != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	if (len)
		*len = need;
	return 0;
}


/**
 * bignum_set_unsigned_bin - Set bignum based on unsigned binary buffer
 * @n: Bignum from bignum_init(); to be set to the given value
 * @buf: Buffer with unsigned binary value
 * @len: Length of buf in octets
 * Returns: 0 on success, -1 on failure
 */
int bignum_set_unsigned_bin(struct bignum *n, const u8 *buf, size_t len)
{
	if (mp_read_unsigned_bin((mp_int *) n, (u8 *) buf, len) != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	return 0;
}


/**
 * bignum_cmp - Signed comparison
 * @a: Bignum from bignum_init()
 * @b: Bignum from bignum_init()
 * Returns: 0 on success, -1 on failure
 */
int bignum_cmp(const struct bignum *a, const struct bignum *b)
{
	return mp_cmp((mp_int *) a, (mp_int *) b);
}


/**
 * bignum_cmd_d - Compare bignum to standard integer
 * @a: Bignum from bignum_init()
 * @b: Small integer
 * Returns: 0 on success, -1 on failure
 */
int bignum_cmp_d(const struct bignum *a, unsigned long b)
{
	return mp_cmp_d((mp_int *) a, b);
}


/**
 * bignum_add - c = a + b
 * @a: Bignum from bignum_init()
 * @b: Bignum from bignum_init()
 * @c: Bignum from bignum_init(); used to store the result of a + b
 * Returns: 0 on success, -1 on failure
 */
int bignum_add(const struct bignum *a, const struct bignum *b,
	       struct bignum *c)
{
	if (mp_add((mp_int *) a, (mp_int *) b, (mp_int *) c) != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	return 0;
}


/**
 * bignum_sub - c = a - b
 * @a: Bignum from bignum_init()
 * @b: Bignum from bignum_init()
 * @c: Bignum from bignum_init(); used to store the result of a - b
 * Returns: 0 on success, -1 on failure
 */
int bignum_sub(const struct bignum *a, const struct bignum *b,
	       struct bignum *c)
{
	if (mp_sub((mp_int *) a, (mp_int *) b, (mp_int *) c) != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	return 0;
}


/**
 * bignum_mul - c = a * b
 * @a: Bignum from bignum_init()
 * @b: Bignum from bignum_init()
 * @c: Bignum from bignum_init(); used to store the result of a * b
 * Returns: 0 on success, -1 on failure
 */
int bignum_mul(const struct bignum *a, const struct bignum *b,
	       struct bignum *c)
{
	if (mp_mul((mp_int *) a, (mp_int *) b, (mp_int *) c) != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	return 0;
}


/**
 * bignum_mulmod - d = a * b (mod c)
 * @a: Bignum from bignum_init()
 * @b: Bignum from bignum_init()
 * @c: Bignum from bignum_init(); modulus
 * @d: Bignum from bignum_init(); used to store the result of a * b (mod c)
 * Returns: 0 on success, -1 on failure
 */
int bignum_mulmod(const struct bignum *a, const struct bignum *b,
		  const struct bignum *c, struct bignum *d)
{
	if (mp_mulmod((mp_int *) a, (mp_int *) b, (mp_int *) c, (mp_int *) d)
	    != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	return 0;
}


/**
 * bignum_exptmod - Modular exponentiation: d = a^b (mod c)
 * @a: Bignum from bignum_init(); base
 * @b: Bignum from bignum_init(); exponent
 * @c: Bignum from bignum_init(); modulus
 * @d: Bignum from bignum_init(); used to store the result of a^b (mod c)
 * Returns: 0 on success, -1 on failure
 */
int bignum_exptmod(const struct bignum *a, const struct bignum *b,
		   const struct bignum *c, struct bignum *d)
{
	if (mp_exptmod((mp_int *) a, (mp_int *) b, (mp_int *) c, (mp_int *) d)
	    != MP_OKAY) {
		wpa_printf(MSG_DEBUG, "BIGNUM: %s failed", __func__);
		return -1;
	}
	return 0;
}
