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

/**
 *  bfa_wc.h Generic wait counter.
 */

#ifndef __BFA_WC_H__
#define __BFA_WC_H__

typedef void (*bfa_wc_resume_t) (void *cbarg);

struct bfa_wc_s {
	bfa_wc_resume_t wc_resume;
	void		*wc_cbarg;
	int		wc_count;
};

static inline void
bfa_wc_up(struct bfa_wc_s *wc)
{
	wc->wc_count++;
}

static inline void
bfa_wc_down(struct bfa_wc_s *wc)
{
	wc->wc_count--;
	if (wc->wc_count == 0)
		wc->wc_resume(wc->wc_cbarg);
}

/**
 * Initialize a waiting counter.
 */
static inline void
bfa_wc_init(struct bfa_wc_s *wc, bfa_wc_resume_t wc_resume, void *wc_cbarg)
{
	wc->wc_resume = wc_resume;
	wc->wc_cbarg = wc_cbarg;
	wc->wc_count = 0;
	bfa_wc_up(wc);
}

/**
 * Wait for counter to reach zero
 */
static inline void
bfa_wc_wait(struct bfa_wc_s *wc)
{
	bfa_wc_down(wc);
}

#endif
