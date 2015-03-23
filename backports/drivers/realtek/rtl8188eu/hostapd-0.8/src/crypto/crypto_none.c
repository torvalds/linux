/*
 * WPA Supplicant / Empty template functions for crypto wrapper
 * Copyright (c) 2005, Jouni Malinen <j@w1.fi>
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


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return 0;
}


void des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
}
