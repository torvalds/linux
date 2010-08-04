/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __BFA_TIMER_H__
#define __BFA_TIMER_H__

#include <bfa_os_inc.h>
#include <cs/bfa_q.h>

struct bfa_s;

typedef void (*bfa_timer_cbfn_t)(void *);

/**
 * BFA timer data structure
 */
struct bfa_timer_s {
	struct list_head	qe;
	bfa_timer_cbfn_t timercb;
	void            *arg;
	int             timeout;	/**< in millisecs. */
};

/**
 * Timer module structure
 */
struct bfa_timer_mod_s {
	struct list_head timer_q;
};

#define BFA_TIMER_FREQ 200 /**< specified in millisecs */

void bfa_timer_beat(struct bfa_timer_mod_s *mod);
void bfa_timer_init(struct bfa_timer_mod_s *mod);
void bfa_timer_begin(struct bfa_timer_mod_s *mod, struct bfa_timer_s *timer,
			bfa_timer_cbfn_t timercb, void *arg,
			unsigned int timeout);
void bfa_timer_stop(struct bfa_timer_s *timer);

#endif /* __BFA_TIMER_H__ */
