/*
 * EAP-IKEv2 definitions
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
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

#ifndef EAP_IKEV2_COMMON_H
#define EAP_IKEV2_COMMON_H

#ifdef CCNS_PL
/* incorrect bit order */
#define IKEV2_FLAGS_LENGTH_INCLUDED 0x01
#define IKEV2_FLAGS_MORE_FRAGMENTS 0x02
#define IKEV2_FLAGS_ICV_INCLUDED 0x04
#else /* CCNS_PL */
#define IKEV2_FLAGS_LENGTH_INCLUDED 0x80
#define IKEV2_FLAGS_MORE_FRAGMENTS 0x40
#define IKEV2_FLAGS_ICV_INCLUDED 0x20
#endif /* CCNS_PL */

#define IKEV2_FRAGMENT_SIZE 1400

struct ikev2_keys;

int eap_ikev2_derive_keymat(int prf, struct ikev2_keys *keys,
			    const u8 *i_nonce, size_t i_nonce_len,
			    const u8 *r_nonce, size_t r_nonce_len,
			    u8 *keymat);
struct wpabuf * eap_ikev2_build_frag_ack(u8 id, u8 code);
int eap_ikev2_validate_icv(int integ_alg, struct ikev2_keys *keys,
			   int initiator, const struct wpabuf *msg,
			   const u8 *pos, const u8 *end);

#endif /* EAP_IKEV2_COMMON_H */
