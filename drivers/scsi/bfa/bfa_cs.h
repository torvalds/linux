/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

/*
 *  bfa_cs.h BFA common services
 */

#ifndef __BFA_CS_H__
#define __BFA_CS_H__

#include "bfa_os_inc.h"

/*
 * BFA TRC
 */

#ifndef BFA_TRC_MAX
#define BFA_TRC_MAX	(4 * 1024)
#endif

#ifndef BFA_TRC_TS
#define BFA_TRC_TS(_trcm)	((_trcm)->ticks++)
#endif

struct bfa_trc_s {
#ifdef __BIGENDIAN
	u16	fileno;
	u16	line;
#else
	u16	line;
	u16	fileno;
#endif
	u32	timestamp;
	union {
		struct {
			u32	rsvd;
			u32	u32;
		} u32;
		u64	u64;
	} data;
};

struct bfa_trc_mod_s {
	u32	head;
	u32	tail;
	u32	ntrc;
	u32	stopped;
	u32	ticks;
	u32	rsvd[3];
	struct bfa_trc_s trc[BFA_TRC_MAX];
};

enum {
	BFA_TRC_HAL  = 1,	/*  BFA modules */
	BFA_TRC_FCS  = 2,	/*  BFA FCS modules */
	BFA_TRC_LDRV = 3,	/*  Linux driver modules */
	BFA_TRC_CNA  = 4,	/*  Common modules */
};
#define BFA_TRC_MOD_SH	10
#define BFA_TRC_MOD(__mod)	((BFA_TRC_ ## __mod) << BFA_TRC_MOD_SH)

/*
 * Define a new tracing file (module). Module should match one defined above.
 */
#define BFA_TRC_FILE(__mod, __submod)					\
	static int __trc_fileno = ((BFA_TRC_ ## __mod ## _ ## __submod) | \
						 BFA_TRC_MOD(__mod))


#define bfa_trc32(_trcp, _data)	\
	__bfa_trc((_trcp)->trcmod, __trc_fileno, __LINE__, (u32)_data)
#define bfa_trc(_trcp, _data)	\
	__bfa_trc((_trcp)->trcmod, __trc_fileno, __LINE__, (u64)_data)

static inline void
bfa_trc_init(struct bfa_trc_mod_s *trcm)
{
	trcm->head = trcm->tail = trcm->stopped = 0;
	trcm->ntrc = BFA_TRC_MAX;
}

static inline void
bfa_trc_stop(struct bfa_trc_mod_s *trcm)
{
	trcm->stopped = 1;
}

#ifdef FWTRC
extern void dc_flush(void *data);
#else
#define dc_flush(data)
#endif


static inline void
__bfa_trc(struct bfa_trc_mod_s *trcm, int fileno, int line, u64 data)
{
	int		tail = trcm->tail;
	struct bfa_trc_s	*trc = &trcm->trc[tail];

	if (trcm->stopped)
		return;

	trc->fileno = (u16) fileno;
	trc->line = (u16) line;
	trc->data.u64 = data;
	trc->timestamp = BFA_TRC_TS(trcm);
	dc_flush(trc);

	trcm->tail = (trcm->tail + 1) & (BFA_TRC_MAX - 1);
	if (trcm->tail == trcm->head)
		trcm->head = (trcm->head + 1) & (BFA_TRC_MAX - 1);
	dc_flush(trcm);
}


static inline void
__bfa_trc32(struct bfa_trc_mod_s *trcm, int fileno, int line, u32 data)
{
	int		tail = trcm->tail;
	struct bfa_trc_s *trc = &trcm->trc[tail];

	if (trcm->stopped)
		return;

	trc->fileno = (u16) fileno;
	trc->line = (u16) line;
	trc->data.u32.u32 = data;
	trc->timestamp = BFA_TRC_TS(trcm);
	dc_flush(trc);

	trcm->tail = (trcm->tail + 1) & (BFA_TRC_MAX - 1);
	if (trcm->tail == trcm->head)
		trcm->head = (trcm->head + 1) & (BFA_TRC_MAX - 1);
	dc_flush(trcm);
}

#ifndef BFA_PERF_BUILD
#define bfa_trc_fp(_trcp, _data)	bfa_trc(_trcp, _data)
#else
#define bfa_trc_fp(_trcp, _data)
#endif

/*
 * @ BFA LOG interfaces
 */
#define bfa_assert(__cond)	do {					\
	if (!(__cond)) {						\
		printk(KERN_ERR "assert(%s) failed at %s:%d\\n",         \
		#__cond, __FILE__, __LINE__);				\
	}								\
} while (0)

#define bfa_sm_fault(__mod, __event)	do {				\
	bfa_trc(__mod, (((u32)0xDEAD << 16) | __event));		\
	printk(KERN_ERR	"Assertion failure: %s:%d: %d",			\
		__FILE__, __LINE__, (__event));				\
} while (0)

#ifndef BFA_PERF_BUILD
#define bfa_assert_fp(__cond)	bfa_assert(__cond)
#else
#define bfa_assert_fp(__cond)
#endif

/* BFA queue definitions */
#define bfa_q_first(_q) ((void *)(((struct list_head *) (_q))->next))
#define bfa_q_next(_qe) (((struct list_head *) (_qe))->next)
#define bfa_q_prev(_qe) (((struct list_head *) (_qe))->prev)

/*
 * bfa_q_qe_init - to initialize a queue element
 */
#define bfa_q_qe_init(_qe) {				\
	bfa_q_next(_qe) = (struct list_head *) NULL;	\
	bfa_q_prev(_qe) = (struct list_head *) NULL;	\
}

/*
 * bfa_q_deq - dequeue an element from head of the queue
 */
#define bfa_q_deq(_q, _qe) {						\
	if (!list_empty(_q)) {						\
		(*((struct list_head **) (_qe))) = bfa_q_next(_q);	\
		bfa_q_prev(bfa_q_next(*((struct list_head **) _qe))) =	\
				(struct list_head *) (_q);		\
		bfa_q_next(_q) = bfa_q_next(*((struct list_head **) _qe));\
		BFA_Q_DBG_INIT(*((struct list_head **) _qe));		\
	} else {							\
		*((struct list_head **) (_qe)) = (struct list_head *) NULL;\
	}								\
}

/*
 * bfa_q_deq_tail - dequeue an element from tail of the queue
 */
#define bfa_q_deq_tail(_q, _qe) {					\
	if (!list_empty(_q)) {						\
		*((struct list_head **) (_qe)) = bfa_q_prev(_q);	\
		bfa_q_next(bfa_q_prev(*((struct list_head **) _qe))) =	\
			(struct list_head *) (_q);			\
		bfa_q_prev(_q) = bfa_q_prev(*(struct list_head **) _qe);\
		BFA_Q_DBG_INIT(*((struct list_head **) _qe));		\
	} else {							\
		*((struct list_head **) (_qe)) = (struct list_head *) NULL;\
	}								\
}

static inline int
bfa_q_is_on_q_func(struct list_head *q, struct list_head *qe)
{
	struct list_head        *tqe;

	tqe = bfa_q_next(q);
	while (tqe != q) {
		if (tqe == qe)
			return 1;
		tqe = bfa_q_next(tqe);
		if (tqe == NULL)
			break;
	}
	return 0;
}

/*
 * #ifdef BFA_DEBUG (Using bfa_assert to check for debug_build is not
 * consistent across modules)
 */
#ifndef BFA_PERF_BUILD
#define BFA_Q_DBG_INIT(_qe) bfa_q_qe_init(_qe)
#else
#define BFA_Q_DBG_INIT(_qe)
#endif

#define bfa_q_is_on_q(_q, _qe)      \
	bfa_q_is_on_q_func(_q, (struct list_head *)(_qe))

/*
 * @ BFA state machine interfaces
 */

typedef void (*bfa_sm_t)(void *sm, int event);

/*
 * oc - object class eg. bfa_ioc
 * st - state, eg. reset
 * otype - object type, eg. struct bfa_ioc_s
 * etype - object type, eg. enum ioc_event
 */
#define bfa_sm_state_decl(oc, st, otype, etype)		\
	static void oc ## _sm_ ## st(otype * fsm, etype event)

#define bfa_sm_set_state(_sm, _state)	((_sm)->sm = (bfa_sm_t)(_state))
#define bfa_sm_send_event(_sm, _event)	((_sm)->sm((_sm), (_event)))
#define bfa_sm_get_state(_sm)		((_sm)->sm)
#define bfa_sm_cmp_state(_sm, _state)	((_sm)->sm == (bfa_sm_t)(_state))

/*
 * For converting from state machine function to state encoding.
 */
struct bfa_sm_table_s {
	bfa_sm_t	sm;	/*  state machine function	*/
	int		state;	/*  state machine encoding	*/
	char		*name;	/*  state name for display	*/
};
#define BFA_SM(_sm)	((bfa_sm_t)(_sm))

/*
 * State machine with entry actions.
 */
typedef void (*bfa_fsm_t)(void *fsm, int event);

/*
 * oc - object class eg. bfa_ioc
 * st - state, eg. reset
 * otype - object type, eg. struct bfa_ioc_s
 * etype - object type, eg. enum ioc_event
 */
#define bfa_fsm_state_decl(oc, st, otype, etype)		\
	static void oc ## _sm_ ## st(otype * fsm, etype event);      \
	static void oc ## _sm_ ## st ## _entry(otype * fsm)

#define bfa_fsm_set_state(_fsm, _state) do {	\
	(_fsm)->fsm = (bfa_fsm_t)(_state);      \
	_state ## _entry(_fsm);      \
} while (0)

#define bfa_fsm_send_event(_fsm, _event)	((_fsm)->fsm((_fsm), (_event)))
#define bfa_fsm_get_state(_fsm)			((_fsm)->fsm)
#define bfa_fsm_cmp_state(_fsm, _state)		\
	((_fsm)->fsm == (bfa_fsm_t)(_state))

static inline int
bfa_sm_to_state(struct bfa_sm_table_s *smt, bfa_sm_t sm)
{
	int	i = 0;

	while (smt[i].sm && smt[i].sm != sm)
		i++;
	return smt[i].state;
}

/*
 * @ Generic wait counter.
 */

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

/*
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

/*
 * Wait for counter to reach zero
 */
static inline void
bfa_wc_wait(struct bfa_wc_s *wc)
{
	bfa_wc_down(wc);
}

#endif /* __BFA_CS_H__ */
