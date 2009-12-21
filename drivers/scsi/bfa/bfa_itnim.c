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

#include <bfa.h>
#include <bfa_fcpim.h>
#include "bfa_fcpim_priv.h"

BFA_TRC_FILE(HAL, ITNIM);

#define BFA_ITNIM_FROM_TAG(_fcpim, _tag)				\
	((_fcpim)->itnim_arr + ((_tag) & ((_fcpim)->num_itnims - 1)))

#define bfa_fcpim_additn(__itnim)					\
	list_add_tail(&(__itnim)->qe, &(__itnim)->fcpim->itnim_q)
#define bfa_fcpim_delitn(__itnim)	do {				\
	bfa_assert(bfa_q_is_on_q(&(__itnim)->fcpim->itnim_q, __itnim));      \
	list_del(&(__itnim)->qe);      \
	bfa_assert(list_empty(&(__itnim)->io_q));      \
	bfa_assert(list_empty(&(__itnim)->io_cleanup_q));      \
	bfa_assert(list_empty(&(__itnim)->pending_q));      \
} while (0)

#define bfa_itnim_online_cb(__itnim) do {				\
	if ((__itnim)->bfa->fcs)					\
		bfa_cb_itnim_online((__itnim)->ditn);      \
	else {								\
		bfa_cb_queue((__itnim)->bfa, &(__itnim)->hcb_qe,	\
		__bfa_cb_itnim_online, (__itnim));      \
	}								\
} while (0)

#define bfa_itnim_offline_cb(__itnim) do {				\
	if ((__itnim)->bfa->fcs)					\
		bfa_cb_itnim_offline((__itnim)->ditn);      \
	else {								\
		bfa_cb_queue((__itnim)->bfa, &(__itnim)->hcb_qe,	\
		__bfa_cb_itnim_offline, (__itnim));      \
	}								\
} while (0)

#define bfa_itnim_sler_cb(__itnim) do {					\
	if ((__itnim)->bfa->fcs)					\
		bfa_cb_itnim_sler((__itnim)->ditn);      \
	else {								\
		bfa_cb_queue((__itnim)->bfa, &(__itnim)->hcb_qe,	\
		__bfa_cb_itnim_sler, (__itnim));      \
	}								\
} while (0)

/*
 * forward declarations
 */
static void     bfa_itnim_iocdisable_cleanup(struct bfa_itnim_s *itnim);
static bfa_boolean_t bfa_itnim_send_fwcreate(struct bfa_itnim_s *itnim);
static bfa_boolean_t bfa_itnim_send_fwdelete(struct bfa_itnim_s *itnim);
static void     bfa_itnim_cleanp_comp(void *itnim_cbarg);
static void     bfa_itnim_cleanup(struct bfa_itnim_s *itnim);
static void     __bfa_cb_itnim_online(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_itnim_offline(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_itnim_sler(void *cbarg, bfa_boolean_t complete);
static void     bfa_itnim_iotov_online(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov_cleanup(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov(void *itnim_arg);
static void     bfa_itnim_iotov_start(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov_stop(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov_delete(struct bfa_itnim_s *itnim);

/**
 *  bfa_itnim_sm BFA itnim state machine
 */


enum bfa_itnim_event {
	BFA_ITNIM_SM_CREATE = 1,	/*  itnim is created */
	BFA_ITNIM_SM_ONLINE = 2,	/*  itnim is online */
	BFA_ITNIM_SM_OFFLINE = 3,	/*  itnim is offline */
	BFA_ITNIM_SM_FWRSP = 4,		/*  firmware response */
	BFA_ITNIM_SM_DELETE = 5,	/*  deleting an existing itnim */
	BFA_ITNIM_SM_CLEANUP = 6,	/*  IO cleanup completion */
	BFA_ITNIM_SM_SLER = 7,		/*  second level error recovery */
	BFA_ITNIM_SM_HWFAIL = 8,	/*  IOC h/w failure event */
	BFA_ITNIM_SM_QRESUME = 9,	/*  queue space available */
};

static void     bfa_itnim_sm_uninit(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_created(struct bfa_itnim_s *itnim,
					 enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwcreate(struct bfa_itnim_s *itnim,
					  enum bfa_itnim_event event);
static void	bfa_itnim_sm_delete_pending(struct bfa_itnim_s *itnim,
				enum bfa_itnim_event event);
static void     bfa_itnim_sm_online(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_sler(struct bfa_itnim_s *itnim,
				      enum bfa_itnim_event event);
static void     bfa_itnim_sm_cleanup_offline(struct bfa_itnim_s *itnim,
						 enum bfa_itnim_event event);
static void     bfa_itnim_sm_cleanup_delete(struct bfa_itnim_s *itnim,
						enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwdelete(struct bfa_itnim_s *itnim,
					  enum bfa_itnim_event event);
static void     bfa_itnim_sm_offline(struct bfa_itnim_s *itnim,
					 enum bfa_itnim_event event);
static void     bfa_itnim_sm_iocdisable(struct bfa_itnim_s *itnim,
					    enum bfa_itnim_event event);
static void     bfa_itnim_sm_deleting(struct bfa_itnim_s *itnim,
					  enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwcreate_qfull(struct bfa_itnim_s *itnim,
					  enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwdelete_qfull(struct bfa_itnim_s *itnim,
					  enum bfa_itnim_event event);
static void     bfa_itnim_sm_deleting_qfull(struct bfa_itnim_s *itnim,
					  enum bfa_itnim_event event);

/**
 * 		Beginning/unallocated state - no events expected.
 */
static void
bfa_itnim_sm_uninit(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_CREATE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_created);
		itnim->is_online = BFA_FALSE;
		bfa_fcpim_additn(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Beginning state, only online event expected.
 */
static void
bfa_itnim_sm_created(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_ONLINE:
		if (bfa_itnim_send_fwcreate(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate_qfull);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Waiting for itnim create response from firmware.
 */
static void
bfa_itnim_sm_fwcreate(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
		bfa_sm_set_state(itnim, bfa_itnim_sm_online);
		itnim->is_online = BFA_TRUE;
		bfa_itnim_iotov_online(itnim);
		bfa_itnim_online_cb(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_delete_pending);
		break;

	case BFA_ITNIM_SM_OFFLINE:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_itnim_sm_fwcreate_qfull(struct bfa_itnim_s *itnim,
			enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_QRESUME:
		bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		bfa_itnim_send_fwcreate(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_offline);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 	Waiting for itnim create response from firmware, a delete is pending.
 */
static void
bfa_itnim_sm_delete_pending(struct bfa_itnim_s *itnim,
				enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_fcpim_delitn(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Online state - normal parking state.
 */
static void
bfa_itnim_sm_online(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_offline);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_iotov_start(itnim);
		bfa_itnim_cleanup(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_delete);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_cleanup(itnim);
		break;

	case BFA_ITNIM_SM_SLER:
		bfa_sm_set_state(itnim, bfa_itnim_sm_sler);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_iotov_start(itnim);
		bfa_itnim_sler_cb(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_iotov_start(itnim);
		bfa_itnim_iocdisable_cleanup(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Second level error recovery need.
 */
static void
bfa_itnim_sm_sler(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_offline);
		bfa_itnim_cleanup(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_delete);
		bfa_itnim_cleanup(itnim);
		bfa_itnim_iotov_delete(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_iocdisable_cleanup(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Going offline. Waiting for active IO cleanup.
 */
static void
bfa_itnim_sm_cleanup_offline(struct bfa_itnim_s *itnim,
				 enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_CLEANUP:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete_qfull);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_delete);
		bfa_itnim_iotov_delete(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_iocdisable_cleanup(itnim);
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_SLER:
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Deleting itnim. Waiting for active IO cleanup.
 */
static void
bfa_itnim_sm_cleanup_delete(struct bfa_itnim_s *itnim,
				enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_CLEANUP:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_iocdisable_cleanup(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * Rport offline. Fimrware itnim is being deleted - awaiting f/w response.
 */
static void
bfa_itnim_sm_fwdelete(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
		bfa_sm_set_state(itnim, bfa_itnim_sm_offline);
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_offline_cb(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_itnim_sm_fwdelete_qfull(struct bfa_itnim_s *itnim,
			enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_QRESUME:
		bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete);
		bfa_itnim_send_fwdelete(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_deleting_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_itnim_offline_cb(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Offline state.
 */
static void
bfa_itnim_sm_offline(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_itnim_iotov_delete(itnim);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_ONLINE:
		if (bfa_itnim_send_fwcreate(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		IOC h/w failed state.
 */
static void
bfa_itnim_sm_iocdisable(struct bfa_itnim_s *itnim,
			    enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_itnim_iotov_delete(itnim);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_OFFLINE:
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_ONLINE:
		if (bfa_itnim_send_fwcreate(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Itnim is deleted, waiting for firmware response to delete.
 */
static void
bfa_itnim_sm_deleting(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_fcpim_delitn(itnim);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_itnim_sm_deleting_qfull(struct bfa_itnim_s *itnim,
			enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_QRESUME:
		bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		bfa_itnim_send_fwdelete(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_fcpim_delitn(itnim);
		break;

	default:
		bfa_assert(0);
	}
}



/**
 *  bfa_itnim_private
 */

/**
 * 		Initiate cleanup of all IOs on an IOC failure.
 */
static void
bfa_itnim_iocdisable_cleanup(struct bfa_itnim_s *itnim)
{
	struct bfa_tskim_s *tskim;
	struct bfa_ioim_s *ioim;
	struct list_head        *qe, *qen;

	list_for_each_safe(qe, qen, &itnim->tsk_q) {
		tskim = (struct bfa_tskim_s *) qe;
		bfa_tskim_iocdisable(tskim);
	}

	list_for_each_safe(qe, qen, &itnim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_iocdisable(ioim);
	}

	/**
	 * For IO request in pending queue, we pretend an early timeout.
	 */
	list_for_each_safe(qe, qen, &itnim->pending_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_tov(ioim);
	}

	list_for_each_safe(qe, qen, &itnim->io_cleanup_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_iocdisable(ioim);
	}
}

/**
 * 		IO cleanup completion
 */
static void
bfa_itnim_cleanp_comp(void *itnim_cbarg)
{
	struct bfa_itnim_s *itnim = itnim_cbarg;

	bfa_stats(itnim, cleanup_comps);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_CLEANUP);
}

/**
 * 		Initiate cleanup of all IOs.
 */
static void
bfa_itnim_cleanup(struct bfa_itnim_s *itnim)
{
	struct bfa_ioim_s  *ioim;
	struct bfa_tskim_s *tskim;
	struct list_head         *qe, *qen;

	bfa_wc_init(&itnim->wc, bfa_itnim_cleanp_comp, itnim);

	list_for_each_safe(qe, qen, &itnim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;

		/**
		 * Move IO to a cleanup queue from active queue so that a later
		 * TM will not pickup this IO.
		 */
		list_del(&ioim->qe);
		list_add_tail(&ioim->qe, &itnim->io_cleanup_q);

		bfa_wc_up(&itnim->wc);
		bfa_ioim_cleanup(ioim);
	}

	list_for_each_safe(qe, qen, &itnim->tsk_q) {
		tskim = (struct bfa_tskim_s *) qe;
		bfa_wc_up(&itnim->wc);
		bfa_tskim_cleanup(tskim);
	}

	bfa_wc_wait(&itnim->wc);
}

static void
__bfa_cb_itnim_online(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_itnim_s *itnim = cbarg;

	if (complete)
		bfa_cb_itnim_online(itnim->ditn);
}

static void
__bfa_cb_itnim_offline(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_itnim_s *itnim = cbarg;

	if (complete)
		bfa_cb_itnim_offline(itnim->ditn);
}

static void
__bfa_cb_itnim_sler(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_itnim_s *itnim = cbarg;

	if (complete)
		bfa_cb_itnim_sler(itnim->ditn);
}

/**
 * Call to resume any I/O requests waiting for room in request queue.
 */
static void
bfa_itnim_qresume(void *cbarg)
{
	struct bfa_itnim_s *itnim = cbarg;

	bfa_sm_send_event(itnim, BFA_ITNIM_SM_QRESUME);
}




/**
 *  bfa_itnim_public
 */

void
bfa_itnim_iodone(struct bfa_itnim_s *itnim)
{
	bfa_wc_down(&itnim->wc);
}

void
bfa_itnim_tskdone(struct bfa_itnim_s *itnim)
{
	bfa_wc_down(&itnim->wc);
}

void
bfa_itnim_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
		u32 *dm_len)
{
	/**
	 * ITN memory
	 */
	*km_len += cfg->fwcfg.num_rports * sizeof(struct bfa_itnim_s);
}

void
bfa_itnim_attach(struct bfa_fcpim_mod_s *fcpim, struct bfa_meminfo_s *minfo)
{
	struct bfa_s      *bfa = fcpim->bfa;
	struct bfa_itnim_s *itnim;
	int             i;

	INIT_LIST_HEAD(&fcpim->itnim_q);

	itnim = (struct bfa_itnim_s *) bfa_meminfo_kva(minfo);
	fcpim->itnim_arr = itnim;

	for (i = 0; i < fcpim->num_itnims; i++, itnim++) {
		bfa_os_memset(itnim, 0, sizeof(struct bfa_itnim_s));
		itnim->bfa = bfa;
		itnim->fcpim = fcpim;
		itnim->reqq = BFA_REQQ_QOS_LO;
		itnim->rport = BFA_RPORT_FROM_TAG(bfa, i);
		itnim->iotov_active = BFA_FALSE;
		bfa_reqq_winit(&itnim->reqq_wait, bfa_itnim_qresume, itnim);

		INIT_LIST_HEAD(&itnim->io_q);
		INIT_LIST_HEAD(&itnim->io_cleanup_q);
		INIT_LIST_HEAD(&itnim->pending_q);
		INIT_LIST_HEAD(&itnim->tsk_q);
		INIT_LIST_HEAD(&itnim->delay_comp_q);
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
	}

	bfa_meminfo_kva(minfo) = (u8 *) itnim;
}

void
bfa_itnim_iocdisable(struct bfa_itnim_s *itnim)
{
	bfa_stats(itnim, ioc_disabled);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_HWFAIL);
}

static bfa_boolean_t
bfa_itnim_send_fwcreate(struct bfa_itnim_s *itnim)
{
	struct bfi_itnim_create_req_s *m;

	itnim->msg_no++;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(itnim->bfa, itnim->reqq);
	if (!m) {
		bfa_reqq_wait(itnim->bfa, itnim->reqq, &itnim->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_ITNIM, BFI_ITNIM_H2I_CREATE_REQ,
			bfa_lpuid(itnim->bfa));
	m->fw_handle = itnim->rport->fw_handle;
	m->class = FC_CLASS_3;
	m->seq_rec = itnim->seq_rec;
	m->msg_no = itnim->msg_no;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(itnim->bfa, itnim->reqq);
	return BFA_TRUE;
}

static bfa_boolean_t
bfa_itnim_send_fwdelete(struct bfa_itnim_s *itnim)
{
	struct bfi_itnim_delete_req_s *m;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(itnim->bfa, itnim->reqq);
	if (!m) {
		bfa_reqq_wait(itnim->bfa, itnim->reqq, &itnim->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_ITNIM, BFI_ITNIM_H2I_DELETE_REQ,
			bfa_lpuid(itnim->bfa));
	m->fw_handle = itnim->rport->fw_handle;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(itnim->bfa, itnim->reqq);
	return BFA_TRUE;
}

/**
 * Cleanup all pending failed inflight requests.
 */
static void
bfa_itnim_delayed_comp(struct bfa_itnim_s *itnim, bfa_boolean_t iotov)
{
	struct bfa_ioim_s *ioim;
	struct list_head *qe, *qen;

	list_for_each_safe(qe, qen, &itnim->delay_comp_q) {
		ioim = (struct bfa_ioim_s *)qe;
		bfa_ioim_delayed_comp(ioim, iotov);
	}
}

/**
 * Start all pending IO requests.
 */
static void
bfa_itnim_iotov_online(struct bfa_itnim_s *itnim)
{
	struct bfa_ioim_s *ioim;

	bfa_itnim_iotov_stop(itnim);

	/**
	 * Abort all inflight IO requests in the queue
	 */
	bfa_itnim_delayed_comp(itnim, BFA_FALSE);

	/**
	 * Start all pending IO requests.
	 */
	while (!list_empty(&itnim->pending_q)) {
		bfa_q_deq(&itnim->pending_q, &ioim);
		list_add_tail(&ioim->qe, &itnim->io_q);
		bfa_ioim_start(ioim);
	}
}

/**
 * Fail all pending IO requests
 */
static void
bfa_itnim_iotov_cleanup(struct bfa_itnim_s *itnim)
{
	struct bfa_ioim_s *ioim;

	/**
	 * Fail all inflight IO requests in the queue
	 */
	bfa_itnim_delayed_comp(itnim, BFA_TRUE);

	/**
	 * Fail any pending IO requests.
	 */
	while (!list_empty(&itnim->pending_q)) {
		bfa_q_deq(&itnim->pending_q, &ioim);
		list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);
		bfa_ioim_tov(ioim);
	}
}

/**
 * IO TOV timer callback. Fail any pending IO requests.
 */
static void
bfa_itnim_iotov(void *itnim_arg)
{
	struct bfa_itnim_s *itnim = itnim_arg;

	itnim->iotov_active = BFA_FALSE;

	bfa_cb_itnim_tov_begin(itnim->ditn);
	bfa_itnim_iotov_cleanup(itnim);
	bfa_cb_itnim_tov(itnim->ditn);
}

/**
 * Start IO TOV timer for failing back pending IO requests in offline state.
 */
static void
bfa_itnim_iotov_start(struct bfa_itnim_s *itnim)
{
	if (itnim->fcpim->path_tov > 0) {

		itnim->iotov_active = BFA_TRUE;
		bfa_assert(bfa_itnim_hold_io(itnim));
		bfa_timer_start(itnim->bfa, &itnim->timer,
			bfa_itnim_iotov, itnim, itnim->fcpim->path_tov);
	}
}

/**
 * Stop IO TOV timer.
 */
static void
bfa_itnim_iotov_stop(struct bfa_itnim_s *itnim)
{
	if (itnim->iotov_active) {
		itnim->iotov_active = BFA_FALSE;
		bfa_timer_stop(&itnim->timer);
	}
}

/**
 * Stop IO TOV timer.
 */
static void
bfa_itnim_iotov_delete(struct bfa_itnim_s *itnim)
{
    bfa_boolean_t pathtov_active = BFA_FALSE;

    if (itnim->iotov_active)
		pathtov_active = BFA_TRUE;

	bfa_itnim_iotov_stop(itnim);
	if (pathtov_active)
		bfa_cb_itnim_tov_begin(itnim->ditn);
	bfa_itnim_iotov_cleanup(itnim);
	if (pathtov_active)
		bfa_cb_itnim_tov(itnim->ditn);
}



/**
 *  bfa_itnim_public
 */

/**
 * 		Itnim interrupt processing.
 */
void
bfa_itnim_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);
	union bfi_itnim_i2h_msg_u msg;
	struct bfa_itnim_s *itnim;

	bfa_trc(bfa, m->mhdr.msg_id);

	msg.msg = m;

	switch (m->mhdr.msg_id) {
	case BFI_ITNIM_I2H_CREATE_RSP:
		itnim = BFA_ITNIM_FROM_TAG(fcpim,
					       msg.create_rsp->bfa_handle);
		bfa_assert(msg.create_rsp->status == BFA_STATUS_OK);
		bfa_stats(itnim, create_comps);
		bfa_sm_send_event(itnim, BFA_ITNIM_SM_FWRSP);
		break;

	case BFI_ITNIM_I2H_DELETE_RSP:
		itnim = BFA_ITNIM_FROM_TAG(fcpim,
					       msg.delete_rsp->bfa_handle);
		bfa_assert(msg.delete_rsp->status == BFA_STATUS_OK);
		bfa_stats(itnim, delete_comps);
		bfa_sm_send_event(itnim, BFA_ITNIM_SM_FWRSP);
		break;

	case BFI_ITNIM_I2H_SLER_EVENT:
		itnim = BFA_ITNIM_FROM_TAG(fcpim,
					       msg.sler_event->bfa_handle);
		bfa_stats(itnim, sler_events);
		bfa_sm_send_event(itnim, BFA_ITNIM_SM_SLER);
		break;

	default:
		bfa_trc(bfa, m->mhdr.msg_id);
		bfa_assert(0);
	}
}



/**
 *  bfa_itnim_api
 */

struct bfa_itnim_s *
bfa_itnim_create(struct bfa_s *bfa, struct bfa_rport_s *rport, void *ditn)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);
	struct bfa_itnim_s *itnim;

	itnim = BFA_ITNIM_FROM_TAG(fcpim, rport->rport_tag);
	bfa_assert(itnim->rport == rport);

	itnim->ditn = ditn;

	bfa_stats(itnim, creates);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_CREATE);

	return itnim;
}

void
bfa_itnim_delete(struct bfa_itnim_s *itnim)
{
	bfa_stats(itnim, deletes);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_DELETE);
}

void
bfa_itnim_online(struct bfa_itnim_s *itnim, bfa_boolean_t seq_rec)
{
	itnim->seq_rec = seq_rec;
	bfa_stats(itnim, onlines);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_ONLINE);
}

void
bfa_itnim_offline(struct bfa_itnim_s *itnim)
{
	bfa_stats(itnim, offlines);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_OFFLINE);
}

/**
 * Return true if itnim is considered offline for holding off IO request.
 * IO is not held if itnim is being deleted.
 */
bfa_boolean_t
bfa_itnim_hold_io(struct bfa_itnim_s *itnim)
{
	return
		itnim->fcpim->path_tov && itnim->iotov_active &&
		(bfa_sm_cmp_state(itnim, bfa_itnim_sm_fwcreate) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_sler) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_cleanup_offline) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_fwdelete) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_offline) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_iocdisable))
	;
}

void
bfa_itnim_get_stats(struct bfa_itnim_s *itnim,
	struct bfa_itnim_hal_stats_s *stats)
{
	*stats = itnim->stats;
}

void
bfa_itnim_clear_stats(struct bfa_itnim_s *itnim)
{
	bfa_os_memset(&itnim->stats, 0, sizeof(itnim->stats));
}


