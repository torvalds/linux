/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

/* BFA common services */

#ifndef __BFA_CS_H__
#define __BFA_CS_H__

#include "cna.h"

/* BFA state machine interfaces */

typedef void (*bfa_sm_t)(void *sm, int event);

/* For converting from state machine function to state encoding. */
struct bfa_sm_table {
	bfa_sm_t	sm;	/*!< state machine function	*/
	int		state;	/*!< state machine encoding	*/
	char		*name;	/*!< state name for display	*/
};
#define BFA_SM(_sm)		((bfa_sm_t)(_sm))

/* State machine with entry actions. */
typedef void (*bfa_fsm_t)(void *fsm, int event);

/* oc - object class eg. bfa_ioc
 * st - state, eg. reset
 * otype - object type, eg. struct bfa_ioc
 * etype - object type, eg. enum ioc_event
 */
#define bfa_fsm_state_decl(oc, st, otype, etype)			\
	static void oc ## _sm_ ## st(otype * fsm, etype event);		\
	static void oc ## _sm_ ## st ## _entry(otype * fsm)

#define bfa_fsm_set_state(_fsm, _state) do {				\
	(_fsm)->fsm = (bfa_fsm_t)(_state);				\
	_state ## _entry(_fsm);						\
} while (0)

#define bfa_fsm_send_event(_fsm, _event)	((_fsm)->fsm((_fsm), (_event)))
#define bfa_fsm_cmp_state(_fsm, _state)					\
	((_fsm)->fsm == (bfa_fsm_t)(_state))

static inline int
bfa_sm_to_state(const struct bfa_sm_table *smt, bfa_sm_t sm)
{
	int	i = 0;

	while (smt[i].sm && smt[i].sm != sm)
		i++;
	return smt[i].state;
}

/* Generic wait counter. */

typedef void (*bfa_wc_resume_t) (void *cbarg);

struct bfa_wc {
	bfa_wc_resume_t wc_resume;
	void		*wc_cbarg;
	int		wc_count;
};

static inline void
bfa_wc_up(struct bfa_wc *wc)
{
	wc->wc_count++;
}

static inline void
bfa_wc_down(struct bfa_wc *wc)
{
	wc->wc_count--;
	if (wc->wc_count == 0)
		wc->wc_resume(wc->wc_cbarg);
}

/* Initialize a waiting counter. */
static inline void
bfa_wc_init(struct bfa_wc *wc, bfa_wc_resume_t wc_resume, void *wc_cbarg)
{
	wc->wc_resume = wc_resume;
	wc->wc_cbarg = wc_cbarg;
	wc->wc_count = 0;
	bfa_wc_up(wc);
}

/* Wait for counter to reach zero */
static inline void
bfa_wc_wait(struct bfa_wc *wc)
{
	bfa_wc_down(wc);
}

#endif /* __BFA_CS_H__ */
