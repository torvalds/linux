/*
 * EAP-PEAP common routines
 * Copyright (c) 2008-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_PEAP_COMMON_H
#define EAP_PEAP_COMMON_H

int peap_prfplus(int version, const u8 *key, size_t key_len,
		 const char *label, const u8 *seed, size_t seed_len,
		 u8 *buf, size_t buf_len);

#endif /* EAP_PEAP_COMMON_H */
