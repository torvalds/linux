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

#endif /* _NET_OVPN_PROTO_H_ */
