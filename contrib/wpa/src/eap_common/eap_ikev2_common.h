/*
 * EAP-IKEv2 definitions
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_IKEV2_COMMON_H
#define EAP_IKEV2_COMMON_H

#define IKEV2_FLAGS_LENGTH_INCLUDED 0x80
#define IKEV2_FLAGS_MORE_FRAGMENTS 0x40
#define IKEV2_FLAGS_ICV_INCLUDED 0x20

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
