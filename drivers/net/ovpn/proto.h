/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#ifndef _NET_OVPN_PROTO_H_
#define _NET_OVPN_PROTO_H_

#include "main.h"

#include <linux/bitfield.h>
#include <linux/skbuff.h>

/* When the OpenVPN protocol is ran in AEAD mode, use
 * the OpenVPN packet ID as the AEAD nonce:
 *
 *    00000005 521c3b01 4308c041
 *    [seq # ] [  nonce_tail   ]
 *    [     12-byte full IV    ] -> OVPN_NONCE_SIZE
 *    [4-bytes                   -> OVPN_NONCE_WIRE_SIZE
 *    on wire]
 */

/* nonce size (96bits) as required by AEAD ciphers */
#define OVPN_NONCE_SIZE			12
/* last 8 bytes of AEAD nonce: provided by userspace and usually derived
 * from key material generated during TLS handshake
 */
#define OVPN_NONCE_TAIL_SIZE		8

/* OpenVPN nonce size reduced by 8-byte nonce tail -- this is the
 * size of the AEAD Associated Data (AD) sent over the wire
 * and is normally the head of the IV
 */
#define OVPN_NONCE_WIRE_SIZE (OVPN_NONCE_SIZE - OVPN_NONCE_TAIL_SIZE)

#define OVPN_OPCODE_SIZE		4 /* DATA_V2 opcode size */
#define OVPN_OPCODE_KEYID_MASK		0x07000000
#define OVPN_OPCODE_PKTTYPE_MASK	0xF8000000
#define OVPN_OPCODE_PEERID_MASK		0x00FFFFFF

/* packet opcodes of interest to us */
#define OVPN_DATA_V1			6 /* data channel v1 packet */
#define OVPN_DATA_V2			9 /* data channel v2 packet */

#define OVPN_PEER_ID_UNDEF		0x00FFFFFF

/**
 * ovpn_opcode_from_skb - extract OP code from skb at specified offset
 * @skb: the packet to extract the OP code from
 * @offset: the offset in the data buffer where the OP code is located
 *
 * Note: this function assumes that the skb head was pulled enough
 * to access the first 4 bytes.
 *
 * Return: the OP code
 */
static inline u8 ovpn_opcode_from_skb(const struct sk_buff *skb, u16 offset)
{
	u32 opcode = be32_to_cpu(*(__be32 *)(skb->data + offset));

	return FIELD_GET(OVPN_OPCODE_PKTTYPE_MASK, opcode);
}

/**
 * ovpn_peer_id_from_skb - extract peer ID from skb at specified offset
 * @skb: the packet to extract the OP code from
 * @offset: the offset in the data buffer where the OP code is located
 *
 * Note: this function assumes that the skb head was pulled enough
 * to access the first 4 bytes.
 *
 * Return: the peer ID
 */
static inline u32 ovpn_peer_id_from_skb(const struct sk_buff *skb, u16 offset)
{
	u32 opcode = be32_to_cpu(*(__be32 *)(skb->data + offset));

	return FIELD_GET(OVPN_OPCODE_PEERID_MASK, opcode);
}

/**
 * ovpn_key_id_from_skb - extract key ID from the skb head
 * @skb: the packet to extract the key ID code from
 *
 * Note: this function assumes that the skb head was pulled enough
 * to access the first 4 bytes.
 *
 * Return: the key ID
 */
static inline u8 ovpn_key_id_from_skb(const struct sk_buff *skb)
{
	u32 opcode = be32_to_cpu(*(__be32 *)skb->data);

	return FIELD_GET(OVPN_OPCODE_KEYID_MASK, opcode);
}

/**
 * ovpn_opcode_compose - combine OP code, key ID and peer ID to wire format
 * @opcode: the OP code
 * @key_id: the key ID
 * @peer_id: the peer ID
 *
 * Return: a 4 bytes integer obtained combining all input values following the
 * OpenVPN wire format. This integer can then be written to the packet header.
 */
static inline u32 ovpn_opcode_compose(u8 opcode, u8 key_id, u32 peer_id)
{
	return FIELD_PREP(OVPN_OPCODE_PKTTYPE_MASK, opcode) |
	       FIELD_PREP(OVPN_OPCODE_KEYID_MASK, key_id) |
	       FIELD_PREP(OVPN_OPCODE_PEERID_MASK, peer_id);
}

#endif /* _NET_OVPN_OVPNPROTO_H_ */
