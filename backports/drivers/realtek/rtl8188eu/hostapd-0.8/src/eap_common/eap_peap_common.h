/*
 * EAP-PEAP common routines
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
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

#ifndef EAP_PEAP_COMMON_H
#define EAP_PEAP_COMMON_H

void peap_prfplus(int version, const u8 *key, size_t key_len,
		  const char *label, const u8 *seed, size_t seed_len,
		  u8 *buf, size_t buf_len);

#endif /* EAP_PEAP_COMMON_H */
