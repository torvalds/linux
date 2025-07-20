/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#ifndef _NET_OVPN_OVPNPKTID_H_
#define _NET_OVPN_OVPNPKTID_H_

#include "proto.h"

/* If no packets received for this length of time, set a backtrack floor
 * at highest received packet ID thus far.
 */
#define PKTID_RECV_EXPIRE (30 * HZ)

/* Packet-ID state for transmitter */
struct ovpn_pktid_xmit {
	atomic_t seq_num;
};

/* replay window sizing in bytes = 2^REPLAY_WINDOW_ORDER */
#define REPLAY_WINDOW_ORDER 8

#define REPLAY_WINDOW_BYTES BIT(REPLAY_WINDOW_ORDER)
#define REPLAY_WINDOW_SIZE  (REPLAY_WINDOW_BYTES * 8)
#define REPLAY_INDEX(base, i) (((base) + (i)) & (REPLAY_WINDOW_SIZE - 1))

/* Packet-ID state for receiver.
 * Other than lock member, can be zeroed to initialize.
 */
struct ovpn_pktid_recv {
	/* "sliding window" bitmask of recent packet IDs received */
	u8 history[REPLAY_WINDOW_BYTES];
	/* bit position of deque base in history */
	unsigned int base;
	/* extent (in bits) of deque in history */
	unsigned int extent;
	/* expiration of history in jiffies */
	unsigned long expire;
	/* highest sequence number received */
	u32 id;
	/* highest time stamp received */
	u32 time;
	/* we will only accept backtrack IDs > id_floor */
	u32 id_floor;
	unsigned int max_backtrack;
	/* protects entire pktd ID state */
	spinlock_t lock;
};

/* Get the next packet ID for xmit */
static inline int ovpn_pktid_xmit_next(struct ovpn_pktid_xmit *pid, u32 *pktid)
{
	const u32 seq_num = atomic_fetch_add_unless(&pid->seq_num, 1, 0);
	/* when the 32bit space is over, we return an error because the packet
	 * ID is used to create the cipher IV and we do not want to reuse the
	 * same value more than once
	 */
	if (unlikely(!seq_num))
		return -ERANGE;

	*pktid = seq_num;

	return 0;
}

/* Write 12-byte AEAD IV to dest */
static inline void ovpn_pktid_aead_write(const u32 pktid,
					 const u8 nt[],
					 unsigned char *dest)
{
	*(__force __be32 *)(dest) = htonl(pktid);
	BUILD_BUG_ON(4 + OVPN_NONCE_TAIL_SIZE != OVPN_NONCE_SIZE);
	memcpy(dest + 4, nt, OVPN_NONCE_TAIL_SIZE);
}

void ovpn_pktid_xmit_init(struct ovpn_pktid_xmit *pid);
void ovpn_pktid_recv_init(struct ovpn_pktid_recv *pr);

int ovpn_pktid_recv(struct ovpn_pktid_recv *pr, u32 pkt_id, u32 pkt_time);

#endif /* _NET_OVPN_OVPNPKTID_H_ */
