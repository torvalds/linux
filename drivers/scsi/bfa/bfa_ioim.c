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
#include <cs/bfa_debug.h>
#include <bfa_cb_ioim_macros.h>

BFA_TRC_FILE(HAL, IOIM);

/*
 * forward declarations.
 */
static bfa_boolean_t	bfa_ioim_send_ioreq(struct bfa_ioim_s *ioim);
static bfa_boolean_t	bfa_ioim_sge_setup(struct bfa_ioim_s *ioim);
static void		bfa_ioim_sgpg_setup(struct bfa_ioim_s *ioim);
static bfa_boolean_t	bfa_ioim_send_abort(struct bfa_ioim_s *ioim);
static void		bfa_ioim_notify_cleanup(struct bfa_ioim_s *ioim);
static void __bfa_cb_ioim_good_comp(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_comp(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_abort(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_failed(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_pathtov(void *cbarg, bfa_boolean_t complete);

/**
 *  bfa_ioim_sm
 */

/**
 * IO state machine events
 */
enum bfa_ioim_event {
	BFA_IOIM_SM_START = 1,		/*  io start request from host */
	BFA_IOIM_SM_COMP_GOOD = 2,	/*  io good comp, resource free */
	BFA_IOIM_SM_COMP = 3,		/*  io comp, resource is free */
	BFA_IOIM_SM_COMP_UTAG = 4,	/*  io comp, resource is free */
	BFA_IOIM_SM_DONE = 5,		/*  io comp, resource not free */
	BFA_IOIM_SM_FREE = 6,		/*  io resource is freed */
	BFA_IOIM_SM_ABORT = 7,		/*  abort request from scsi stack */
	BFA_IOIM_SM_ABORT_COMP = 8,	/*  abort from f/w */
	BFA_IOIM_SM_ABORT_DONE = 9,	/*  abort completion from f/w */
	BFA_IOIM_SM_QRESUME = 10,	/*  CQ space available to queue IO */
	BFA_IOIM_SM_SGALLOCED = 11,	/*  SG page allocation successful */
	BFA_IOIM_SM_SQRETRY = 12,	/*  sequence recovery retry */
	BFA_IOIM_SM_HCB	= 13,		/*  bfa callback complete */
	BFA_IOIM_SM_CLEANUP = 14,	/*  IO cleanup from itnim */
	BFA_IOIM_SM_TMSTART = 15,	/*  IO cleanup from tskim */
	BFA_IOIM_SM_TMDONE = 16,	/*  IO cleanup from tskim */
	BFA_IOIM_SM_HWFAIL = 17,	/*  IOC h/w failure event */
	BFA_IOIM_SM_IOTOV = 18,		/*  ITN offline TOV       */
};

/*
 * forward declaration of IO state machine
 */
static void     bfa_ioim_sm_uninit(struct bfa_ioim_s *ioim,
				       enum bfa_ioim_event event);
static void     bfa_ioim_sm_sgalloc(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_active(struct bfa_ioim_s *ioim,
				       enum bfa_ioim_event event);
static void     bfa_ioim_sm_abort(struct bfa_ioim_s *ioim,
				      enum bfa_ioim_event event);
static void     bfa_ioim_sm_cleanup(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_qfull(struct bfa_ioim_s *ioim,
				      enum bfa_ioim_event event);
static void     bfa_ioim_sm_abort_qfull(struct bfa_ioim_s *ioim,
					    enum bfa_ioim_event event);
static void     bfa_ioim_sm_cleanup_qfull(struct bfa_ioim_s *ioim,
					      enum bfa_ioim_event event);
static void     bfa_ioim_sm_hcb(struct bfa_ioim_s *ioim,
				    enum bfa_ioim_event event);
static void     bfa_ioim_sm_hcb_free(struct bfa_ioim_s *ioim,
					 enum bfa_ioim_event event);
static void     bfa_ioim_sm_resfree(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);

/**
 * 		IO is not started (unallocated).
 */
static void
bfa_ioim_sm_uninit(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc_fp(ioim->bfa, ioim->iotag);
	bfa_trc_fp(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_START:
		if (!bfa_itnim_is_online(ioim->itnim)) {
			if (!bfa_itnim_hold_io(ioim->itnim)) {
				bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
				list_del(&ioim->qe);
				list_add_tail(&ioim->qe,
						&ioim->fcpim->ioim_comp_q);
				bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
						__bfa_cb_ioim_pathtov, ioim);
			} else {
				list_del(&ioim->qe);
				list_add_tail(&ioim->qe,
						&ioim->itnim->pending_q);
			}
			break;
		}

		if (ioim->nsges > BFI_SGE_INLINE) {
			if (!bfa_ioim_sge_setup(ioim)) {
				bfa_sm_set_state(ioim, bfa_ioim_sm_sgalloc);
				return;
			}
		}

		if (!bfa_ioim_send_ioreq(ioim)) {
			bfa_sm_set_state(ioim, bfa_ioim_sm_qfull);
			break;
		}

		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
		break;

	case BFA_IOIM_SM_IOTOV:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
				__bfa_cb_ioim_pathtov, ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		/**
		 * IO in pending queue can get abort requests. Complete abort
		 * requests immediately.
		 */
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_assert(bfa_q_is_on_q(&ioim->itnim->pending_q, ioim));
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
				ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * 		IO is waiting for SG pages.
 */
static void
bfa_ioim_sm_sgalloc(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_SGALLOCED:
		if (!bfa_ioim_send_ioreq(ioim)) {
			bfa_sm_set_state(ioim, bfa_ioim_sm_qfull);
			break;
		}
		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_sgpg_wcancel(ioim->bfa, &ioim->iosp->sgpg_wqe);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_sgpg_wcancel(ioim->bfa, &ioim->iosp->sgpg_wqe);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_sgpg_wcancel(ioim->bfa, &ioim->iosp->sgpg_wqe);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * 		IO is active.
 */
static void
bfa_ioim_sm_active(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc_fp(ioim->bfa, ioim->iotag);
	bfa_trc_fp(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_COMP_GOOD:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
			      __bfa_cb_ioim_good_comp, ioim);
		break;

	case BFA_IOIM_SM_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_comp,
			      ioim);
		break;

	case BFA_IOIM_SM_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_comp,
			      ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		ioim->iosp->abort_explicit = BFA_TRUE;
		ioim->io_cbfn = __bfa_cb_ioim_abort;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_abort);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_abort_qfull);
			bfa_reqq_wait(ioim->bfa, ioim->itnim->reqq,
					  &ioim->iosp->reqq_wait);
		}
		break;

	case BFA_IOIM_SM_CLEANUP:
		ioim->iosp->abort_explicit = BFA_FALSE;
		ioim->io_cbfn = __bfa_cb_ioim_failed;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
			bfa_reqq_wait(ioim->bfa, ioim->itnim->reqq,
					  &ioim->iosp->reqq_wait);
		}
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * 		IO is being aborted, waiting for completion from firmware.
 */
static void
bfa_ioim_sm_abort(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
	case BFA_IOIM_SM_DONE:
	case BFA_IOIM_SM_FREE:
		break;

	case BFA_IOIM_SM_ABORT_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_ABORT_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_COMP_UTAG:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_assert(ioim->iosp->abort_explicit == BFA_TRUE);
		ioim->iosp->abort_explicit = BFA_FALSE;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
			bfa_reqq_wait(ioim->bfa, ioim->itnim->reqq,
					  &ioim->iosp->reqq_wait);
		}
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * IO is being cleaned up (implicit abort), waiting for completion from
 * firmware.
 */
static void
bfa_ioim_sm_cleanup(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
	case BFA_IOIM_SM_DONE:
	case BFA_IOIM_SM_FREE:
		break;

	case BFA_IOIM_SM_ABORT:
		/**
		 * IO is already being aborted implicitly
		 */
		ioim->io_cbfn = __bfa_cb_ioim_abort;
		break;

	case BFA_IOIM_SM_ABORT_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_ABORT_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_COMP_UTAG:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		/**
		 * IO can be in cleanup state already due to TM command. 2nd cleanup
		 * request comes from ITN offline event.
		 */
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * 		IO is waiting for room in request CQ
 */
static void
bfa_ioim_sm_qfull(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_QRESUME:
		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
		bfa_ioim_send_ioreq(ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * 		Active IO is being aborted, waiting for room in request CQ.
 */
static void
bfa_ioim_sm_abort_qfull(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_QRESUME:
		bfa_sm_set_state(ioim, bfa_ioim_sm_abort);
		bfa_ioim_send_abort(ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_assert(ioim->iosp->abort_explicit == BFA_TRUE);
		ioim->iosp->abort_explicit = BFA_FALSE;
		bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
		break;

	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * 		Active IO is being cleaned up, waiting for room in request CQ.
 */
static void
bfa_ioim_sm_cleanup_qfull(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_QRESUME:
		bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		bfa_ioim_send_abort(ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		/**
		 * IO is alraedy being cleaned up implicitly
		 */
		ioim->io_cbfn = __bfa_cb_ioim_abort;
		break;

	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * IO bfa callback is pending.
 */
static void
bfa_ioim_sm_hcb(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc_fp(ioim->bfa, ioim->iotag);
	bfa_trc_fp(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_HCB:
		bfa_sm_set_state(ioim, bfa_ioim_sm_uninit);
		bfa_ioim_free(ioim);
		bfa_cb_ioim_resfree(ioim->bfa->bfad);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * IO bfa callback is pending. IO resource cannot be freed.
 */
static void
bfa_ioim_sm_hcb_free(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_HCB:
		bfa_sm_set_state(ioim, bfa_ioim_sm_resfree);
		list_del(&ioim->qe);
		list_add_tail(&ioim->qe, &ioim->fcpim->ioim_resfree_q);
		break;

	case BFA_IOIM_SM_FREE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/**
 * IO is completed, waiting resource free from firmware.
 */
static void
bfa_ioim_sm_resfree(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_FREE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_uninit);
		bfa_ioim_free(ioim);
		bfa_cb_ioim_resfree(ioim->bfa->bfad);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}



/**
 *  bfa_ioim_private
 */

static void
__bfa_cb_ioim_good_comp(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_good_comp(ioim->bfa->bfad, ioim->dio);
}

static void
__bfa_cb_ioim_comp(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s	*ioim = cbarg;
	struct bfi_ioim_rsp_s *m;
	u8		*snsinfo = NULL;
	u8         sns_len = 0;
	s32         residue = 0;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	m = (struct bfi_ioim_rsp_s *) &ioim->iosp->comp_rspmsg;
	if (m->io_status == BFI_IOIM_STS_OK) {
		/**
		 * setup sense information, if present
		 */
		if (m->scsi_status == SCSI_STATUS_CHECK_CONDITION
					&& m->sns_len) {
			sns_len = m->sns_len;
			snsinfo = ioim->iosp->snsinfo;
		}

		/**
		 * setup residue value correctly for normal completions
		 */
		if (m->resid_flags == FCP_RESID_UNDER)
			residue = bfa_os_ntohl(m->residue);
		if (m->resid_flags == FCP_RESID_OVER) {
			residue = bfa_os_ntohl(m->residue);
			residue = -residue;
		}
	}

	bfa_cb_ioim_done(ioim->bfa->bfad, ioim->dio, m->io_status,
			  m->scsi_status, sns_len, snsinfo, residue);
}

static void
__bfa_cb_ioim_failed(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_done(ioim->bfa->bfad, ioim->dio, BFI_IOIM_STS_ABORTED,
			  0, 0, NULL, 0);
}

static void
__bfa_cb_ioim_pathtov(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_done(ioim->bfa->bfad, ioim->dio, BFI_IOIM_STS_PATHTOV,
			  0, 0, NULL, 0);
}

static void
__bfa_cb_ioim_abort(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_abort(ioim->bfa->bfad, ioim->dio);
}

static void
bfa_ioim_sgpg_alloced(void *cbarg)
{
	struct bfa_ioim_s *ioim = cbarg;

	ioim->nsgpgs = BFA_SGPG_NPAGE(ioim->nsges);
	list_splice_tail_init(&ioim->iosp->sgpg_wqe.sgpg_q, &ioim->sgpg_q);
	bfa_ioim_sgpg_setup(ioim);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_SGALLOCED);
}

/**
 * Send I/O request to firmware.
 */
static          bfa_boolean_t
bfa_ioim_send_ioreq(struct bfa_ioim_s *ioim)
{
	struct bfa_itnim_s *itnim = ioim->itnim;
	struct bfi_ioim_req_s *m;
	static struct fcp_cmnd_s cmnd_z0 = { 0 };
	struct bfi_sge_s      *sge;
	u32        pgdlen = 0;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(ioim->bfa, itnim->reqq);
	if (!m) {
		bfa_reqq_wait(ioim->bfa, ioim->itnim->reqq,
				  &ioim->iosp->reqq_wait);
		return BFA_FALSE;
	}

	/**
	 * build i/o request message next
	 */
	m->io_tag = bfa_os_htons(ioim->iotag);
	m->rport_hdl = ioim->itnim->rport->fw_handle;
	m->io_timeout = bfa_cb_ioim_get_timeout(ioim->dio);

	/**
	 * build inline IO SG element here
	 */
	sge = &m->sges[0];
	if (ioim->nsges) {
		sge->sga = bfa_cb_ioim_get_sgaddr(ioim->dio, 0);
		pgdlen = bfa_cb_ioim_get_sglen(ioim->dio, 0);
		sge->sg_len = pgdlen;
		sge->flags = (ioim->nsges > BFI_SGE_INLINE) ?
					BFI_SGE_DATA_CPL : BFI_SGE_DATA_LAST;
		bfa_sge_to_be(sge);
		sge++;
	}

	if (ioim->nsges > BFI_SGE_INLINE) {
		sge->sga = ioim->sgpg->sgpg_pa;
	} else {
		sge->sga.a32.addr_lo = 0;
		sge->sga.a32.addr_hi = 0;
	}
	sge->sg_len = pgdlen;
	sge->flags = BFI_SGE_PGDLEN;
	bfa_sge_to_be(sge);

	/**
	 * set up I/O command parameters
	 */
	bfa_os_assign(m->cmnd, cmnd_z0);
	m->cmnd.lun = bfa_cb_ioim_get_lun(ioim->dio);
	m->cmnd.iodir = bfa_cb_ioim_get_iodir(ioim->dio);
	bfa_os_assign(m->cmnd.cdb,
			*(struct scsi_cdb_s *)bfa_cb_ioim_get_cdb(ioim->dio));
	m->cmnd.fcp_dl = bfa_os_htonl(bfa_cb_ioim_get_size(ioim->dio));

	/**
	 * set up I/O message header
	 */
	switch (m->cmnd.iodir) {
	case FCP_IODIR_READ:
		bfi_h2i_set(m->mh, BFI_MC_IOIM_READ, 0, bfa_lpuid(ioim->bfa));
		bfa_stats(itnim, input_reqs);
		break;
	case FCP_IODIR_WRITE:
		bfi_h2i_set(m->mh, BFI_MC_IOIM_WRITE, 0, bfa_lpuid(ioim->bfa));
		bfa_stats(itnim, output_reqs);
		break;
	case FCP_IODIR_RW:
		bfa_stats(itnim, input_reqs);
		bfa_stats(itnim, output_reqs);
	default:
		bfi_h2i_set(m->mh, BFI_MC_IOIM_IO, 0, bfa_lpuid(ioim->bfa));
	}
	if (itnim->seq_rec ||
	    (bfa_cb_ioim_get_size(ioim->dio) & (sizeof(u32) - 1)))
		bfi_h2i_set(m->mh, BFI_MC_IOIM_IO, 0, bfa_lpuid(ioim->bfa));

#ifdef IOIM_ADVANCED
	m->cmnd.crn = bfa_cb_ioim_get_crn(ioim->dio);
	m->cmnd.priority = bfa_cb_ioim_get_priority(ioim->dio);
	m->cmnd.taskattr = bfa_cb_ioim_get_taskattr(ioim->dio);

	/**
	 * Handle large CDB (>16 bytes).
	 */
	m->cmnd.addl_cdb_len = (bfa_cb_ioim_get_cdblen(ioim->dio) -
					FCP_CMND_CDB_LEN) / sizeof(u32);
	if (m->cmnd.addl_cdb_len) {
		bfa_os_memcpy(&m->cmnd.cdb + 1, (struct scsi_cdb_s *)
				bfa_cb_ioim_get_cdb(ioim->dio) + 1,
				m->cmnd.addl_cdb_len * sizeof(u32));
		fcp_cmnd_fcpdl(&m->cmnd) =
				bfa_os_htonl(bfa_cb_ioim_get_size(ioim->dio));
	}
#endif

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(ioim->bfa, itnim->reqq);
	return BFA_TRUE;
}

/**
 * Setup any additional SG pages needed.Inline SG element is setup
 * at queuing time.
 */
static bfa_boolean_t
bfa_ioim_sge_setup(struct bfa_ioim_s *ioim)
{
	u16        nsgpgs;

	bfa_assert(ioim->nsges > BFI_SGE_INLINE);

	/**
	 * allocate SG pages needed
	 */
	nsgpgs = BFA_SGPG_NPAGE(ioim->nsges);
	if (!nsgpgs)
		return BFA_TRUE;

	if (bfa_sgpg_malloc(ioim->bfa, &ioim->sgpg_q, nsgpgs)
	    != BFA_STATUS_OK) {
		bfa_sgpg_wait(ioim->bfa, &ioim->iosp->sgpg_wqe, nsgpgs);
		return BFA_FALSE;
	}

	ioim->nsgpgs = nsgpgs;
	bfa_ioim_sgpg_setup(ioim);

	return BFA_TRUE;
}

static void
bfa_ioim_sgpg_setup(struct bfa_ioim_s *ioim)
{
	int             sgeid, nsges, i;
	struct bfi_sge_s      *sge;
	struct bfa_sgpg_s *sgpg;
	u32        pgcumsz;

	sgeid = BFI_SGE_INLINE;
	ioim->sgpg = sgpg = bfa_q_first(&ioim->sgpg_q);

	do {
		sge = sgpg->sgpg->sges;
		nsges = ioim->nsges - sgeid;
		if (nsges > BFI_SGPG_DATA_SGES)
			nsges = BFI_SGPG_DATA_SGES;

		pgcumsz = 0;
		for (i = 0; i < nsges; i++, sge++, sgeid++) {
			sge->sga = bfa_cb_ioim_get_sgaddr(ioim->dio, sgeid);
			sge->sg_len = bfa_cb_ioim_get_sglen(ioim->dio, sgeid);
			pgcumsz += sge->sg_len;

			/**
			 * set flags
			 */
			if (i < (nsges - 1))
				sge->flags = BFI_SGE_DATA;
			else if (sgeid < (ioim->nsges - 1))
				sge->flags = BFI_SGE_DATA_CPL;
			else
				sge->flags = BFI_SGE_DATA_LAST;
		}

		sgpg = (struct bfa_sgpg_s *) bfa_q_next(sgpg);

		/**
		 * set the link element of each page
		 */
		if (sgeid == ioim->nsges) {
			sge->flags = BFI_SGE_PGDLEN;
			sge->sga.a32.addr_lo = 0;
			sge->sga.a32.addr_hi = 0;
		} else {
			sge->flags = BFI_SGE_LINK;
			sge->sga = sgpg->sgpg_pa;
		}
		sge->sg_len = pgcumsz;
	} while (sgeid < ioim->nsges);
}

/**
 * Send I/O abort request to firmware.
 */
static          bfa_boolean_t
bfa_ioim_send_abort(struct bfa_ioim_s *ioim)
{
	struct bfa_itnim_s          *itnim = ioim->itnim;
	struct bfi_ioim_abort_req_s *m;
	enum bfi_ioim_h2i       msgop;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(ioim->bfa, itnim->reqq);
	if (!m)
		return BFA_FALSE;

	/**
	 * build i/o request message next
	 */
	if (ioim->iosp->abort_explicit)
		msgop = BFI_IOIM_H2I_IOABORT_REQ;
	else
		msgop = BFI_IOIM_H2I_IOCLEANUP_REQ;

	bfi_h2i_set(m->mh, BFI_MC_IOIM, msgop, bfa_lpuid(ioim->bfa));
	m->io_tag    = bfa_os_htons(ioim->iotag);
	m->abort_tag = ++ioim->abort_tag;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(ioim->bfa, itnim->reqq);
	return BFA_TRUE;
}

/**
 * Call to resume any I/O requests waiting for room in request queue.
 */
static void
bfa_ioim_qresume(void *cbarg)
{
	struct bfa_ioim_s *ioim = cbarg;

	bfa_fcpim_stats(ioim->fcpim, qresumes);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_QRESUME);
}


static void
bfa_ioim_notify_cleanup(struct bfa_ioim_s *ioim)
{
	/**
	 * Move IO from itnim queue to fcpim global queue since itnim will be
	 * freed.
	 */
	list_del(&ioim->qe);
	list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);

	if (!ioim->iosp->tskim) {
		if (ioim->fcpim->delay_comp && ioim->itnim->iotov_active) {
			bfa_cb_dequeue(&ioim->hcb_qe);
			list_del(&ioim->qe);
			list_add_tail(&ioim->qe, &ioim->itnim->delay_comp_q);
		}
		bfa_itnim_iodone(ioim->itnim);
	} else
		bfa_tskim_iodone(ioim->iosp->tskim);
}

/**
 * 		  or after the link comes back.
 */
void
bfa_ioim_delayed_comp(struct bfa_ioim_s *ioim, bfa_boolean_t iotov)
{
	/**
	 * If path tov timer expired, failback with PATHTOV status - these
	 * IO requests are not normally retried by IO stack.
	 *
	 * Otherwise device cameback online and fail it with normal failed
	 * status so that IO stack retries these failed IO requests.
	 */
	if (iotov)
		ioim->io_cbfn = __bfa_cb_ioim_pathtov;
	else
		ioim->io_cbfn = __bfa_cb_ioim_failed;

	bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);

    /**
     * Move IO to fcpim global queue since itnim will be
     * freed.
     */
    list_del(&ioim->qe);
    list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);
}



/**
 *  bfa_ioim_friend
 */

/**
 * Memory allocation and initialization.
 */
void
bfa_ioim_attach(struct bfa_fcpim_mod_s *fcpim, struct bfa_meminfo_s *minfo)
{
	struct bfa_ioim_s		*ioim;
	struct bfa_ioim_sp_s	*iosp;
	u16		i;
	u8			*snsinfo;
	u32		snsbufsz;

	/**
	 * claim memory first
	 */
	ioim = (struct bfa_ioim_s *) bfa_meminfo_kva(minfo);
	fcpim->ioim_arr = ioim;
	bfa_meminfo_kva(minfo) = (u8 *) (ioim + fcpim->num_ioim_reqs);

	iosp = (struct bfa_ioim_sp_s *) bfa_meminfo_kva(minfo);
	fcpim->ioim_sp_arr = iosp;
	bfa_meminfo_kva(minfo) = (u8 *) (iosp + fcpim->num_ioim_reqs);

	/**
	 * Claim DMA memory for per IO sense data.
	 */
	snsbufsz = fcpim->num_ioim_reqs * BFI_IOIM_SNSLEN;
	fcpim->snsbase.pa  = bfa_meminfo_dma_phys(minfo);
	bfa_meminfo_dma_phys(minfo) += snsbufsz;

	fcpim->snsbase.kva = bfa_meminfo_dma_virt(minfo);
	bfa_meminfo_dma_virt(minfo) += snsbufsz;
	snsinfo = fcpim->snsbase.kva;
	bfa_iocfc_set_snsbase(fcpim->bfa, fcpim->snsbase.pa);

	/**
	 * Initialize ioim free queues
	 */
	INIT_LIST_HEAD(&fcpim->ioim_free_q);
	INIT_LIST_HEAD(&fcpim->ioim_resfree_q);
	INIT_LIST_HEAD(&fcpim->ioim_comp_q);

	for (i = 0; i < fcpim->num_ioim_reqs;
	     i++, ioim++, iosp++, snsinfo += BFI_IOIM_SNSLEN) {
		/*
		 * initialize IOIM
		 */
		bfa_os_memset(ioim, 0, sizeof(struct bfa_ioim_s));
		ioim->iotag   = i;
		ioim->bfa     = fcpim->bfa;
		ioim->fcpim   = fcpim;
		ioim->iosp    = iosp;
		iosp->snsinfo = snsinfo;
		INIT_LIST_HEAD(&ioim->sgpg_q);
		bfa_reqq_winit(&ioim->iosp->reqq_wait,
				   bfa_ioim_qresume, ioim);
		bfa_sgpg_winit(&ioim->iosp->sgpg_wqe,
				   bfa_ioim_sgpg_alloced, ioim);
		bfa_sm_set_state(ioim, bfa_ioim_sm_uninit);

		list_add_tail(&ioim->qe, &fcpim->ioim_free_q);
	}
}

/**
 * Driver detach time call.
 */
void
bfa_ioim_detach(struct bfa_fcpim_mod_s *fcpim)
{
}

void
bfa_ioim_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);
	struct bfi_ioim_rsp_s *rsp = (struct bfi_ioim_rsp_s *) m;
	struct bfa_ioim_s *ioim;
	u16        iotag;
	enum bfa_ioim_event evt = BFA_IOIM_SM_COMP;

	iotag = bfa_os_ntohs(rsp->io_tag);

	ioim = BFA_IOIM_FROM_TAG(fcpim, iotag);
	bfa_assert(ioim->iotag == iotag);

	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, rsp->io_status);
	bfa_trc(ioim->bfa, rsp->reuse_io_tag);

	if (bfa_sm_cmp_state(ioim, bfa_ioim_sm_active))
		bfa_os_assign(ioim->iosp->comp_rspmsg, *m);

	switch (rsp->io_status) {
	case BFI_IOIM_STS_OK:
		bfa_fcpim_stats(fcpim, iocomp_ok);
		if (rsp->reuse_io_tag == 0)
			evt = BFA_IOIM_SM_DONE;
		else
			evt = BFA_IOIM_SM_COMP;
		break;

	case BFI_IOIM_STS_TIMEDOUT:
	case BFI_IOIM_STS_ABORTED:
		rsp->io_status = BFI_IOIM_STS_ABORTED;
		bfa_fcpim_stats(fcpim, iocomp_aborted);
		if (rsp->reuse_io_tag == 0)
			evt = BFA_IOIM_SM_DONE;
		else
			evt = BFA_IOIM_SM_COMP;
		break;

	case BFI_IOIM_STS_PROTO_ERR:
		bfa_fcpim_stats(fcpim, iocom_proto_err);
		bfa_assert(rsp->reuse_io_tag);
		evt = BFA_IOIM_SM_COMP;
		break;

	case BFI_IOIM_STS_SQER_NEEDED:
		bfa_fcpim_stats(fcpim, iocom_sqer_needed);
		bfa_assert(rsp->reuse_io_tag == 0);
		evt = BFA_IOIM_SM_SQRETRY;
		break;

	case BFI_IOIM_STS_RES_FREE:
		bfa_fcpim_stats(fcpim, iocom_res_free);
		evt = BFA_IOIM_SM_FREE;
		break;

	case BFI_IOIM_STS_HOST_ABORTED:
		bfa_fcpim_stats(fcpim, iocom_hostabrts);
		if (rsp->abort_tag != ioim->abort_tag) {
			bfa_trc(ioim->bfa, rsp->abort_tag);
			bfa_trc(ioim->bfa, ioim->abort_tag);
			return;
		}

		if (rsp->reuse_io_tag)
			evt = BFA_IOIM_SM_ABORT_COMP;
		else
			evt = BFA_IOIM_SM_ABORT_DONE;
		break;

	case BFI_IOIM_STS_UTAG:
		bfa_fcpim_stats(fcpim, iocom_utags);
		evt = BFA_IOIM_SM_COMP_UTAG;
		break;

	default:
		bfa_assert(0);
	}

	bfa_sm_send_event(ioim, evt);
}

void
bfa_ioim_good_comp_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);
	struct bfi_ioim_rsp_s *rsp = (struct bfi_ioim_rsp_s *) m;
	struct bfa_ioim_s *ioim;
	u16        iotag;

	iotag = bfa_os_ntohs(rsp->io_tag);

	ioim = BFA_IOIM_FROM_TAG(fcpim, iotag);
	bfa_assert(ioim->iotag == iotag);

	bfa_trc_fp(ioim->bfa, ioim->iotag);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_COMP_GOOD);
}

/**
 * Called by itnim to clean up IO while going offline.
 */
void
bfa_ioim_cleanup(struct bfa_ioim_s *ioim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_fcpim_stats(ioim->fcpim, io_cleanups);

	ioim->iosp->tskim = NULL;
	bfa_sm_send_event(ioim, BFA_IOIM_SM_CLEANUP);
}

void
bfa_ioim_cleanup_tm(struct bfa_ioim_s *ioim, struct bfa_tskim_s *tskim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_fcpim_stats(ioim->fcpim, io_tmaborts);

	ioim->iosp->tskim = tskim;
	bfa_sm_send_event(ioim, BFA_IOIM_SM_CLEANUP);
}

/**
 * IOC failure handling.
 */
void
bfa_ioim_iocdisable(struct bfa_ioim_s *ioim)
{
	bfa_sm_send_event(ioim, BFA_IOIM_SM_HWFAIL);
}

/**
 * IO offline TOV popped. Fail the pending IO.
 */
void
bfa_ioim_tov(struct bfa_ioim_s *ioim)
{
	bfa_sm_send_event(ioim, BFA_IOIM_SM_IOTOV);
}



/**
 *  bfa_ioim_api
 */

/**
 * Allocate IOIM resource for initiator mode I/O request.
 */
struct bfa_ioim_s *
bfa_ioim_alloc(struct bfa_s *bfa, struct bfad_ioim_s *dio,
		struct bfa_itnim_s *itnim, u16 nsges)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);
	struct bfa_ioim_s *ioim;

	/**
	 * alocate IOIM resource
	 */
	bfa_q_deq(&fcpim->ioim_free_q, &ioim);
	if (!ioim) {
		bfa_fcpim_stats(fcpim, no_iotags);
		return NULL;
	}

	ioim->dio = dio;
	ioim->itnim = itnim;
	ioim->nsges = nsges;
	ioim->nsgpgs = 0;

	bfa_stats(fcpim, total_ios);
	bfa_stats(itnim, ios);
	fcpim->ios_active++;

	list_add_tail(&ioim->qe, &itnim->io_q);
	bfa_trc_fp(ioim->bfa, ioim->iotag);

	return ioim;
}

void
bfa_ioim_free(struct bfa_ioim_s *ioim)
{
	struct bfa_fcpim_mod_s *fcpim = ioim->fcpim;

	bfa_trc_fp(ioim->bfa, ioim->iotag);
	bfa_assert_fp(bfa_sm_cmp_state(ioim, bfa_ioim_sm_uninit));

	bfa_assert_fp(list_empty(&ioim->sgpg_q)
		   || (ioim->nsges > BFI_SGE_INLINE));

	if (ioim->nsgpgs > 0)
		bfa_sgpg_mfree(ioim->bfa, &ioim->sgpg_q, ioim->nsgpgs);

	bfa_stats(ioim->itnim, io_comps);
	fcpim->ios_active--;

	list_del(&ioim->qe);
	list_add_tail(&ioim->qe, &fcpim->ioim_free_q);
}

void
bfa_ioim_start(struct bfa_ioim_s *ioim)
{
	bfa_trc_fp(ioim->bfa, ioim->iotag);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_START);
}

/**
 * Driver I/O abort request.
 */
void
bfa_ioim_abort(struct bfa_ioim_s *ioim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_fcpim_stats(ioim->fcpim, io_aborts);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_ABORT);
}


