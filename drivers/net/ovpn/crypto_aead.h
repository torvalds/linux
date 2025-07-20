/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_OVPNAEAD_H_
#define _NET_OVPN_OVPNAEAD_H_

#include "crypto.h"

#include <asm/types.h>
#include <linux/skbuff.h>

int ovpn_aead_encrypt(struct ovpn_peer *peer, struct ovpn_crypto_key_slot *ks,
		      struct sk_buff *skb);
int ovpn_aead_decrypt(struct ovpn_peer *peer, struct ovpn_crypto_key_slot *ks,
		      struct sk_buff *skb);

struct ovpn_crypto_key_slot *
ovpn_aead_crypto_key_slot_new(const struct ovpn_key_config *kc);
void ovpn_aead_crypto_key_slot_destroy(struct ovpn_crypto_key_slot *ks);

enum ovpn_cipher_alg ovpn_aead_crypto_alg(struct ovpn_crypto_key_slot *ks);

#endif /* _NET_OVPN_OVPNAEAD_H_ */
