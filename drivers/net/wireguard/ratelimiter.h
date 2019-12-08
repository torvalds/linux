/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _WG_RATELIMITER_H
#define _WG_RATELIMITER_H

#include <linux/skbuff.h>

int wg_ratelimiter_init(void);
void wg_ratelimiter_uninit(void);
bool wg_ratelimiter_allow(struct sk_buff *skb, struct net *net);

#ifdef DEBUG
bool wg_ratelimiter_selftest(void);
#endif

#endif /* _WG_RATELIMITER_H */
