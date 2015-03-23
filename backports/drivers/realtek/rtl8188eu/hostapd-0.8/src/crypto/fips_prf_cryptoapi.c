/*
 * FIPS 186-2 PRF for Microsoft CryptoAPI
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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
#include "crypto.h"


int fips186_2_prf(const u8 *seed, size_t seed_len, u8 *x, size_t xlen)
{
	/* FIX: how to do this with CryptoAPI? */
	return -1;
}
