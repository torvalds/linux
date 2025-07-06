// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/types.h>

#include "ovpnpriv.h"
#include "main.h"
#include "pktid.h"

void ovpn_pktid_xmit_init(struct ovpn_pktid_xmit *pid)
{
	atomic_set(&pid->seq_num, 1);
}

void ovpn_pktid_recv_init(struct ovpn_pktid_recv *pr)
{
	memset(pr, 0, sizeof(*pr));
	spin_lock_init(&pr->lock);
}

/* Packet replay detection.
 * Allows ID backtrack of up to REPLAY_WINDOW_SIZE - 1.
 */
int ovpn_pktid_recv(struct ovpn_pktid_recv *pr, u32 pkt_id, u32 pkt_time)
{
	const unsigned long now = jiffies;
	int ret;

	/* ID must not be zero */
	if (unlikely(pkt_id == 0))
		return -EINVAL;

	spin_lock_bh(&pr->lock);

	/* expire backtracks at or below pr->id after PKTID_RECV_EXPIRE time */
	if (unlikely(time_after_eq(now, pr->expire)))
		pr->id_floor = pr->id;

	/* time changed? */
	if (unlikely(pkt_time != pr->time)) {
		if (pkt_time > pr->time) {
			/* time moved forward, accept */
			pr->base = 0;
			pr->extent = 0;
			pr->id = 0;
			pr->time = pkt_time;
			pr->id_floor = 0;
		} else {
			/* time moved backward, reject */
			ret = -ETIME;
			goto out;
		}
	}

	if (likely(pkt_id == pr->id + 1)) {
		/* well-formed ID sequence (incremented by 1) */
		pr->base = REPLAY_INDEX(pr->base, -1);
		pr->history[pr->base / 8] |= (1 << (pr->base % 8));
		if (pr->extent < REPLAY_WINDOW_SIZE)
			++pr->extent;
		pr->id = pkt_id;
	} else if (pkt_id > pr->id) {
		/* ID jumped forward by more than one */
		const unsigned int delta = pkt_id - pr->id;

		if (delta < REPLAY_WINDOW_SIZE) {
			unsigned int i;

			pr->base = REPLAY_INDEX(pr->base, -delta);
			pr->history[pr->base / 8] |= (1 << (pr->base % 8));
			pr->extent += delta;
			if (pr->extent > REPLAY_WINDOW_SIZE)
				pr->extent = REPLAY_WINDOW_SIZE;
			for (i = 1; i < delta; ++i) {
				unsigned int newb = REPLAY_INDEX(pr->base, i);

				pr->history[newb / 8] &= ~BIT(newb % 8);
			}
		} else {
			pr->base = 0;
			pr->extent = REPLAY_WINDOW_SIZE;
			memset(pr->history, 0, sizeof(pr->history));
			pr->history[0] = 1;
		}
		pr->id = pkt_id;
	} else {
		/* ID backtrack */
		const unsigned int delta = pr->id - pkt_id;

		if (delta > pr->max_backtrack)
			pr->max_backtrack = delta;
		if (delta < pr->extent) {
			if (pkt_id > pr->id_floor) {
				const unsigned int ri = REPLAY_INDEX(pr->base,
								     delta);
				u8 *p = &pr->history[ri / 8];
				const u8 mask = (1 << (ri % 8));

				if (*p & mask) {
					ret = -EINVAL;
					goto out;
				}
				*p |= mask;
			} else {
				ret = -EINVAL;
				goto out;
			}
		} else {
			ret = -EINVAL;
			goto out;
		}
	}

	pr->expire = now + PKTID_RECV_EXPIRE;
	ret = 0;
out:
	spin_unlock_bh(&pr->lock);
	return ret;
}
