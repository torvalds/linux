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

#include "includes.h"

#include "common.h"
#include "crypto/sha1.h"
#include "eap_peap_common.h"

void peap_prfplus(int version, const u8 *key, size_t key_len,
		  const char *label, const u8 *seed, size_t seed_len,
		  u8 *buf, size_t buf_len)
{
	unsigned char counter = 0;
	size_t pos, plen;
	u8 hash[SHA1_MAC_LEN];
	size_t label_len = os_strlen(label);
	u8 extra[2];
	const unsigned char *addr[5];
	size_t len[5];

	addr[0] = hash;
	len[0] = 0;
	addr[1] = (unsigned char *) label;
	len[1] = label_len;
	addr[2] = seed;
	len[2] = seed_len;

	if (version == 0) {
		/*
		 * PRF+(K, S, LEN) = T1 | T2 | ... | Tn
		 * T1 = HMAC-SHA1(K, S | 0x01 | 0x00 | 0x00)
		 * T2 = HMAC-SHA1(K, T1 | S | 0x02 | 0x00 | 0x00)
		 * ...
		 * Tn = HMAC-SHA1(K, Tn-1 | S | n | 0x00 | 0x00)
		 */

		extra[0] = 0;
		extra[1] = 0;

		addr[3] = &counter;
		len[3] = 1;
		addr[4] = extra;
		len[4] = 2;
	} else {
		/*
		 * PRF (K,S,LEN) = T1 | T2 | T3 | T4 | ... where:
		 * T1 = HMAC-SHA1(K, S | LEN | 0x01)
		 * T2 = HMAC-SHA1 (K, T1 | S | LEN | 0x02)
		 * T3 = HMAC-SHA1 (K, T2 | S | LEN | 0x03)
		 * T4 = HMAC-SHA1 (K, T3 | S | LEN | 0x04)
		 *   ...
		 */

		extra[0] = buf_len & 0xff;

		addr[3] = extra;
		len[3] = 1;
		addr[4] = &counter;
		len[4] = 1;
	}

	pos = 0;
	while (pos < buf_len) {
		counter++;
		plen = buf_len - pos;
		hmac_sha1_vector(key, key_len, 5, addr, len, hash);
		if (plen >= SHA1_MAC_LEN) {
			os_memcpy(&buf[pos], hash, SHA1_MAC_LEN);
			pos += SHA1_MAC_LEN;
		} else {
			os_memcpy(&buf[pos], hash, plen);
			break;
		}
		len[0] = SHA1_MAC_LEN;
	}
}
