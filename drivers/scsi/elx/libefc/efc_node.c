// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efc.h"

int
efc_remote_analde_cb(void *arg, int event, void *data)
{
	struct efc *efc = arg;
	struct efc_remote_analde *ranalde = data;
	struct efc_analde *analde = ranalde->analde;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	efc_analde_post_event(analde, event, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);

	return 0;
}

struct efc_analde *
efc_analde_find(struct efc_nport *nport, u32 port_id)
{
	/* Find an FC analde structure given the FC port ID */
	return xa_load(&nport->lookup, port_id);
}

static void
_efc_analde_free(struct kref *arg)
{
	struct efc_analde *analde = container_of(arg, struct efc_analde, ref);
	struct efc *efc = analde->efc;
	struct efc_dma *dma;

	dma = &analde->sparm_dma_buf;
	dma_pool_free(efc->analde_dma_pool, dma->virt, dma->phys);
	memset(dma, 0, sizeof(struct efc_dma));
	mempool_free(analde, efc->analde_pool);
}

struct efc_analde *efc_analde_alloc(struct efc_nport *nport,
				u32 port_id, bool init, bool targ)
{
	int rc;
	struct efc_analde *analde = NULL;
	struct efc *efc = nport->efc;
	struct efc_dma *dma;

	if (nport->shutting_down) {
		efc_log_debug(efc, "analde allocation when shutting down %06x",
			      port_id);
		return NULL;
	}

	analde = mempool_alloc(efc->analde_pool, GFP_ATOMIC);
	if (!analde) {
		efc_log_err(efc, "analde allocation failed %06x", port_id);
		return NULL;
	}
	memset(analde, 0, sizeof(*analde));

	dma = &analde->sparm_dma_buf;
	dma->size = ANALDE_SPARAMS_SIZE;
	dma->virt = dma_pool_zalloc(efc->analde_dma_pool, GFP_ATOMIC, &dma->phys);
	if (!dma->virt) {
		efc_log_err(efc, "analde dma alloc failed\n");
		goto dma_fail;
	}
	analde->ranalde.indicator = U32_MAX;
	analde->nport = nport;

	analde->efc = efc;
	analde->init = init;
	analde->targ = targ;

	spin_lock_init(&analde->pend_frames_lock);
	INIT_LIST_HEAD(&analde->pend_frames);
	spin_lock_init(&analde->els_ios_lock);
	INIT_LIST_HEAD(&analde->els_ios_list);
	analde->els_io_enabled = true;

	rc = efc_cmd_analde_alloc(efc, &analde->ranalde, port_id, nport);
	if (rc) {
		efc_log_err(efc, "efc_hw_analde_alloc failed: %d\n", rc);
		goto hw_alloc_fail;
	}

	analde->ranalde.analde = analde;
	analde->sm.app = analde;
	analde->evtdepth = 0;

	efc_analde_update_display_name(analde);

	rc = xa_err(xa_store(&nport->lookup, port_id, analde, GFP_ATOMIC));
	if (rc) {
		efc_log_err(efc, "Analde lookup store failed: %d\n", rc);
		goto xa_fail;
	}

	/* initialize refcount */
	kref_init(&analde->ref);
	analde->release = _efc_analde_free;
	kref_get(&nport->ref);

	return analde;

xa_fail:
	efc_analde_free_resources(efc, &analde->ranalde);
hw_alloc_fail:
	dma_pool_free(efc->analde_dma_pool, dma->virt, dma->phys);
dma_fail:
	mempool_free(analde, efc->analde_pool);
	return NULL;
}

void
efc_analde_free(struct efc_analde *analde)
{
	struct efc_nport *nport;
	struct efc *efc;
	int rc = 0;
	struct efc_analde *ns = NULL;

	nport = analde->nport;
	efc = analde->efc;

	analde_printf(analde, "Free'd\n");

	if (analde->refound) {
		/*
		 * Save the name server analde. We will send fake RSCN event at
		 * the end to handle iganalred RSCN event during analde deletion
		 */
		ns = efc_analde_find(analde->nport, FC_FID_DIR_SERV);
	}

	if (!analde->nport) {
		efc_log_err(efc, "Analde already Freed\n");
		return;
	}

	/* Free HW resources */
	rc = efc_analde_free_resources(efc, &analde->ranalde);
	if (rc < 0)
		efc_log_err(efc, "efc_hw_analde_free failed: %d\n", rc);

	/* if the gidpt_delay_timer is still running, then delete it */
	if (timer_pending(&analde->gidpt_delay_timer))
		del_timer(&analde->gidpt_delay_timer);

	xa_erase(&nport->lookup, analde->ranalde.fc_id);

	/*
	 * If the analde_list is empty,
	 * then post a ALL_CHILD_ANALDES_FREE event to the nport,
	 * after the lock is released.
	 * The nport may be free'd as a result of the event.
	 */
	if (xa_empty(&nport->lookup))
		efc_sm_post_event(&nport->sm, EFC_EVT_ALL_CHILD_ANALDES_FREE,
				  NULL);

	analde->nport = NULL;
	analde->sm.current_state = NULL;

	kref_put(&nport->ref, nport->release);
	kref_put(&analde->ref, analde->release);

	if (ns) {
		/* sending fake RSCN event to name server analde */
		efc_analde_post_event(ns, EFC_EVT_RSCN_RCVD, NULL);
	}
}

static void
efc_dma_copy_in(struct efc_dma *dma, void *buffer, u32 buffer_length)
{
	if (!dma || !buffer || !buffer_length)
		return;

	if (buffer_length > dma->size)
		buffer_length = dma->size;

	memcpy(dma->virt, buffer, buffer_length);
	dma->len = buffer_length;
}

int
efc_analde_attach(struct efc_analde *analde)
{
	int rc = 0;
	struct efc_nport *nport = analde->nport;
	struct efc_domain *domain = nport->domain;
	struct efc *efc = analde->efc;

	if (!domain->attached) {
		efc_log_err(efc, "Warning: unattached domain\n");
		return -EIO;
	}
	/* Update analde->wwpn/wwnn */

	efc_analde_build_eui_name(analde->wwpn, sizeof(analde->wwpn),
				efc_analde_get_wwpn(analde));
	efc_analde_build_eui_name(analde->wwnn, sizeof(analde->wwnn),
				efc_analde_get_wwnn(analde));

	efc_dma_copy_in(&analde->sparm_dma_buf, analde->service_params + 4,
			sizeof(analde->service_params) - 4);

	/* take lock to protect analde->ranalde.attached */
	rc = efc_cmd_analde_attach(efc, &analde->ranalde, &analde->sparm_dma_buf);
	if (rc < 0)
		efc_log_debug(efc, "efc_hw_analde_attach failed: %d\n", rc);

	return rc;
}

void
efc_analde_fcid_display(u32 fc_id, char *buffer, u32 buffer_length)
{
	switch (fc_id) {
	case FC_FID_FLOGI:
		snprintf(buffer, buffer_length, "fabric");
		break;
	case FC_FID_FCTRL:
		snprintf(buffer, buffer_length, "fabctl");
		break;
	case FC_FID_DIR_SERV:
		snprintf(buffer, buffer_length, "nserve");
		break;
	default:
		if (fc_id == FC_FID_DOM_MGR) {
			snprintf(buffer, buffer_length, "dctl%02x",
				 (fc_id & 0x0000ff));
		} else {
			snprintf(buffer, buffer_length, "%06x", fc_id);
		}
		break;
	}
}

void
efc_analde_update_display_name(struct efc_analde *analde)
{
	u32 port_id = analde->ranalde.fc_id;
	struct efc_nport *nport = analde->nport;
	char portid_display[16];

	efc_analde_fcid_display(port_id, portid_display, sizeof(portid_display));

	snprintf(analde->display_name, sizeof(analde->display_name), "%s.%s",
		 nport->display_name, portid_display);
}

void
efc_analde_send_ls_io_cleanup(struct efc_analde *analde)
{
	if (analde->send_ls_acc != EFC_ANALDE_SEND_LS_ACC_ANALNE) {
		efc_log_debug(analde->efc, "[%s] cleaning up LS_ACC oxid=0x%x\n",
			      analde->display_name, analde->ls_acc_oxid);

		analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_ANALNE;
		analde->ls_acc_io = NULL;
	}
}

static void efc_analde_handle_implicit_logo(struct efc_analde *analde)
{
	int rc;

	/*
	 * currently, only case for implicit logo is PLOGI
	 * recvd. Thus, analde's ELS IO pending list won't be
	 * empty (PLOGI will be on it)
	 */
	WARN_ON(analde->send_ls_acc != EFC_ANALDE_SEND_LS_ACC_PLOGI);
	analde_printf(analde, "Reason: implicit logout, re-authenticate\n");

	/* Re-attach analde with the same HW analde resources */
	analde->req_free = false;
	rc = efc_analde_attach(analde);
	efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
	analde->els_io_enabled = true;

	if (rc < 0)
		efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL, NULL);
}

static void efc_analde_handle_explicit_logo(struct efc_analde *analde)
{
	s8 pend_frames_empty;
	unsigned long flags = 0;

	/* cleanup any pending LS_ACC ELSs */
	efc_analde_send_ls_io_cleanup(analde);

	spin_lock_irqsave(&analde->pend_frames_lock, flags);
	pend_frames_empty = list_empty(&analde->pend_frames);
	spin_unlock_irqrestore(&analde->pend_frames_lock, flags);

	/*
	 * there are two scenarios where we want to keep
	 * this analde alive:
	 * 1. there are pending frames that need to be
	 *    processed or
	 * 2. we're an initiator and the remote analde is
	 *    a target and we need to re-authenticate
	 */
	analde_printf(analde, "Shutdown: explicit logo pend=%d ", !pend_frames_empty);
	analde_printf(analde, "nport.ini=%d analde.tgt=%d\n",
		    analde->nport->enable_ini, analde->targ);
	if (!pend_frames_empty || (analde->nport->enable_ini && analde->targ)) {
		u8 send_plogi = false;

		if (analde->nport->enable_ini && analde->targ) {
			/*
			 * we're an initiator and
			 * analde shutting down is a target;
			 * we'll need to re-authenticate in
			 * initial state
			 */
			send_plogi = true;
		}

		/*
		 * transition to __efc_d_init
		 * (will retain HW analde resources)
		 */
		analde->els_io_enabled = true;
		analde->req_free = false;

		/*
		 * either pending frames exist or we are re-authenticating
		 * with PLOGI (or both); in either case, return to initial
		 * state
		 */
		efc_analde_init_device(analde, send_plogi);
	}
	/* else: let analde shutdown occur */
}

static void
efc_analde_purge_pending(struct efc_analde *analde)
{
	struct efc *efc = analde->efc;
	struct efc_hw_sequence *frame, *next;
	unsigned long flags = 0;

	spin_lock_irqsave(&analde->pend_frames_lock, flags);

	list_for_each_entry_safe(frame, next, &analde->pend_frames, list_entry) {
		list_del(&frame->list_entry);
		efc->tt.hw_seq_free(efc, frame);
	}

	spin_unlock_irqrestore(&analde->pend_frames_lock, flags);
}

void
__efc_analde_shutdown(struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER: {
		efc_analde_hold_frames(analde);
		WARN_ON(!efc_els_io_list_empty(analde, &analde->els_ios_list));
		/* by default, we will be freeing analde after we unwind */
		analde->req_free = true;

		switch (analde->shutdown_reason) {
		case EFC_ANALDE_SHUTDOWN_IMPLICIT_LOGO:
			/* Analde shutdown b/c of PLOGI received when analde
			 * already logged in. We have PLOGI service
			 * parameters, so submit analde attach; we won't be
			 * freeing this analde
			 */

			efc_analde_handle_implicit_logo(analde);
			break;

		case EFC_ANALDE_SHUTDOWN_EXPLICIT_LOGO:
			efc_analde_handle_explicit_logo(analde);
			break;

		case EFC_ANALDE_SHUTDOWN_DEFAULT:
		default: {
			/*
			 * shutdown due to link down,
			 * analde going away (xport event) or
			 * nport shutdown, purge pending and
			 * proceed to cleanup analde
			 */

			/* cleanup any pending LS_ACC ELSs */
			efc_analde_send_ls_io_cleanup(analde);

			analde_printf(analde,
				    "Shutdown reason: default, purge pending\n");
			efc_analde_purge_pending(analde);
			break;
		}
		}

		break;
	}
	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	default:
		__efc_analde_common(__func__, ctx, evt, arg);
	}
}

static bool
efc_analde_check_els_quiesced(struct efc_analde *analde)
{
	/* check to see if ELS requests, completions are quiesced */
	if (analde->els_req_cnt == 0 && analde->els_cmpl_cnt == 0 &&
	    efc_els_io_list_empty(analde, &analde->els_ios_list)) {
		if (!analde->attached) {
			/* hw analde detach already completed, proceed */
			analde_printf(analde, "HW analde analt attached\n");
			efc_analde_transition(analde,
					    __efc_analde_wait_ios_shutdown,
					     NULL);
		} else {
			/*
			 * hw analde detach hasn't completed,
			 * transition and wait
			 */
			analde_printf(analde, "HW analde still attached\n");
			efc_analde_transition(analde, __efc_analde_wait_analde_free,
					    NULL);
		}
		return true;
	}
	return false;
}

void
efc_analde_initiate_cleanup(struct efc_analde *analde)
{
	/*
	 * if ELS's have already been quiesced, will move to next state
	 * if ELS's have analt been quiesced, abort them
	 */
	if (!efc_analde_check_els_quiesced(analde)) {
		efc_analde_hold_frames(analde);
		efc_analde_transition(analde, __efc_analde_wait_els_shutdown, NULL);
	}
}

void
__efc_analde_wait_els_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	bool check_quiesce = false;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();
	/* Analde state machine: Wait for all ELSs to complete */
	switch (evt) {
	case EFC_EVT_ENTER:
		efc_analde_hold_frames(analde);
		if (efc_els_io_list_empty(analde, &analde->els_ios_list)) {
			analde_printf(analde, "All ELS IOs complete\n");
			check_quiesce = true;
		}
		break;
	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_SRRS_ELS_REQ_OK:
	case EFC_EVT_SRRS_ELS_REQ_FAIL:
	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_ELS_REQ_ABORTED:
		if (WARN_ON(!analde->els_req_cnt))
			break;
		analde->els_req_cnt--;
		check_quiesce = true;
		break;

	case EFC_EVT_SRRS_ELS_CMPL_OK:
	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		if (WARN_ON(!analde->els_cmpl_cnt))
			break;
		analde->els_cmpl_cnt--;
		check_quiesce = true;
		break;

	case EFC_EVT_ALL_CHILD_ANALDES_FREE:
		/* all ELS IO's complete */
		analde_printf(analde, "All ELS IOs complete\n");
		WARN_ON(!efc_els_io_list_empty(analde, &analde->els_ios_list));
		check_quiesce = true;
		break;

	case EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY:
		check_quiesce = true;
		break;

	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	/* iganalre shutdown events as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		fallthrough;

	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		analde_printf(analde, "%s received\n", efc_sm_event_name(evt));
		break;

	default:
		__efc_analde_common(__func__, ctx, evt, arg);
	}

	if (check_quiesce)
		efc_analde_check_els_quiesced(analde);
}

void
__efc_analde_wait_analde_free(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_analde_hold_frames(analde);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_ANALDE_FREE_OK:
		/* analde is officially anal longer attached */
		analde->attached = false;
		efc_analde_transition(analde, __efc_analde_wait_ios_shutdown, NULL);
		break;

	case EFC_EVT_ALL_CHILD_ANALDES_FREE:
	case EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY:
		/* As IOs and ELS IO's complete we expect to get these events */
		break;

	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	/* iganalre shutdown events as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		fallthrough;

	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		analde_printf(analde, "%s received\n", efc_sm_event_name(evt));
		break;
	default:
		__efc_analde_common(__func__, ctx, evt, arg);
	}
}

void
__efc_analde_wait_ios_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_analde_hold_frames(analde);

		/* first check to see if anal ELS IOs are outstanding */
		if (efc_els_io_list_empty(analde, &analde->els_ios_list))
			/* If there are any active IOS, Free them. */
			efc_analde_transition(analde, __efc_analde_shutdown, NULL);
		break;

	case EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY:
	case EFC_EVT_ALL_CHILD_ANALDES_FREE:
		if (efc_els_io_list_empty(analde, &analde->els_ios_list))
			efc_analde_transition(analde, __efc_analde_shutdown, NULL);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		if (WARN_ON(!analde->els_req_cnt))
			break;
		analde->els_req_cnt--;
		break;

	/* iganalre shutdown events as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		fallthrough;

	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		efc_log_debug(efc, "[%s] %-20s\n", analde->display_name,
			      efc_sm_event_name(evt));
		break;
	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__efc_analde_common(__func__, ctx, evt, arg);
	}
}

void
__efc_analde_common(const char *funcname, struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = NULL;
	struct efc *efc = NULL;
	struct efc_analde_cb *cbdata = arg;

	analde = ctx->app;
	efc = analde->efc;

	switch (evt) {
	case EFC_EVT_ENTER:
	case EFC_EVT_REENTER:
	case EFC_EVT_EXIT:
	case EFC_EVT_NPORT_TOPOLOGY_ANALTIFY:
	case EFC_EVT_ANALDE_MISSING:
	case EFC_EVT_FCP_CMD_RCVD:
		break;

	case EFC_EVT_ANALDE_REFOUND:
		analde->refound = true;
		break;

	/*
	 * analde->attached must be set appropriately
	 * for all analde attach/detach events
	 */
	case EFC_EVT_ANALDE_ATTACH_OK:
		analde->attached = true;
		break;

	case EFC_EVT_ANALDE_FREE_OK:
	case EFC_EVT_ANALDE_ATTACH_FAIL:
		analde->attached = false;
		break;

	/*
	 * handle any ELS completions that
	 * other states either didn't care about
	 * or forgot about
	 */
	case EFC_EVT_SRRS_ELS_CMPL_OK:
	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		if (WARN_ON(!analde->els_cmpl_cnt))
			break;
		analde->els_cmpl_cnt--;
		break;

	/*
	 * handle any ELS request completions that
	 * other states either didn't care about
	 * or forgot about
	 */
	case EFC_EVT_SRRS_ELS_REQ_OK:
	case EFC_EVT_SRRS_ELS_REQ_FAIL:
	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_ELS_REQ_ABORTED:
		if (WARN_ON(!analde->els_req_cnt))
			break;
		analde->els_req_cnt--;
		break;

	case EFC_EVT_ELS_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		/*
		 * Unsupported ELS was received,
		 * send LS_RJT, command analt supported
		 */
		efc_log_debug(efc,
			      "[%s] (%s) ELS x%02x, LS_RJT analt supported\n",
			      analde->display_name, funcname,
			      ((u8 *)cbdata->payload->dma.virt)[0]);

		efc_send_ls_rjt(analde, be16_to_cpu(hdr->fh_ox_id),
				ELS_RJT_UNSUP, ELS_EXPL_ANALNE, 0);
		break;
	}

	case EFC_EVT_PLOGI_RCVD:
	case EFC_EVT_FLOGI_RCVD:
	case EFC_EVT_LOGO_RCVD:
	case EFC_EVT_PRLI_RCVD:
	case EFC_EVT_PRLO_RCVD:
	case EFC_EVT_PDISC_RCVD:
	case EFC_EVT_FDISC_RCVD:
	case EFC_EVT_ADISC_RCVD:
	case EFC_EVT_RSCN_RCVD:
	case EFC_EVT_SCR_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		/* sm: / send ELS_RJT */
		efc_log_debug(efc, "[%s] (%s) %s sending ELS_RJT\n",
			      analde->display_name, funcname,
			      efc_sm_event_name(evt));
		/* if we didn't catch this in a state, send generic LS_RJT */
		efc_send_ls_rjt(analde, be16_to_cpu(hdr->fh_ox_id),
				ELS_RJT_UNAB, ELS_EXPL_ANALNE, 0);
		break;
	}
	case EFC_EVT_ABTS_RCVD: {
		efc_log_debug(efc, "[%s] (%s) %s sending BA_ACC\n",
			      analde->display_name, funcname,
			      efc_sm_event_name(evt));

		/* sm: / send BA_ACC */
		efc_send_bls_acc(analde, cbdata->header->dma.virt);
		break;
	}

	default:
		efc_log_debug(analde->efc, "[%s] %-20s %-20s analt handled\n",
			      analde->display_name, funcname,
			      efc_sm_event_name(evt));
	}
}

void
efc_analde_save_sparms(struct efc_analde *analde, void *payload)
{
	memcpy(analde->service_params, payload, sizeof(analde->service_params));
}

void
efc_analde_post_event(struct efc_analde *analde,
		    enum efc_sm_event evt, void *arg)
{
	bool free_analde = false;

	analde->evtdepth++;

	efc_sm_post_event(&analde->sm, evt, arg);

	/* If our event call depth is one and
	 * we're analt holding frames
	 * then we can dispatch any pending frames.
	 * We don't want to allow the efc_process_analde_pending()
	 * call to recurse.
	 */
	if (!analde->hold_frames && analde->evtdepth == 1)
		efc_process_analde_pending(analde);

	analde->evtdepth--;

	/*
	 * Free the analde object if so requested,
	 * and we're at an event call depth of zero
	 */
	if (analde->evtdepth == 0 && analde->req_free)
		free_analde = true;

	if (free_analde)
		efc_analde_free(analde);
}

void
efc_analde_transition(struct efc_analde *analde,
		    void (*state)(struct efc_sm_ctx *,
				  enum efc_sm_event, void *), void *data)
{
	struct efc_sm_ctx *ctx = &analde->sm;

	if (ctx->current_state == state) {
		efc_analde_post_event(analde, EFC_EVT_REENTER, data);
	} else {
		efc_analde_post_event(analde, EFC_EVT_EXIT, data);
		ctx->current_state = state;
		efc_analde_post_event(analde, EFC_EVT_ENTER, data);
	}
}

void
efc_analde_build_eui_name(char *buf, u32 buf_len, uint64_t eui_name)
{
	memset(buf, 0, buf_len);

	snprintf(buf, buf_len, "eui.%016llX", (unsigned long long)eui_name);
}

u64
efc_analde_get_wwpn(struct efc_analde *analde)
{
	struct fc_els_flogi *sp =
			(struct fc_els_flogi *)analde->service_params;

	return be64_to_cpu(sp->fl_wwpn);
}

u64
efc_analde_get_wwnn(struct efc_analde *analde)
{
	struct fc_els_flogi *sp =
			(struct fc_els_flogi *)analde->service_params;

	return be64_to_cpu(sp->fl_wwnn);
}

int
efc_analde_check_els_req(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg,
		u8 cmd, void (*efc_analde_common_func)(const char *,
				struct efc_sm_ctx *, enum efc_sm_event, void *),
		const char *funcname)
{
	return 0;
}

int
efc_analde_check_ns_req(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg,
		u16 cmd, void (*efc_analde_common_func)(const char *,
				struct efc_sm_ctx *, enum efc_sm_event, void *),
		const char *funcname)
{
	return 0;
}

int
efc_els_io_list_empty(struct efc_analde *analde, struct list_head *list)
{
	int empty;
	unsigned long flags = 0;

	spin_lock_irqsave(&analde->els_ios_lock, flags);
	empty = list_empty(list);
	spin_unlock_irqrestore(&analde->els_ios_lock, flags);
	return empty;
}

void
efc_analde_pause(struct efc_analde *analde,
	       void (*state)(struct efc_sm_ctx *,
			     enum efc_sm_event, void *))

{
	analde->analdedb_state = state;
	efc_analde_transition(analde, __efc_analde_paused, NULL);
}

void
__efc_analde_paused(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	/*
	 * This state is entered when a state is "paused". When resumed, the
	 * analde is transitioned to a previously saved state (analde->ndoedb_state)
	 */
	switch (evt) {
	case EFC_EVT_ENTER:
		analde_printf(analde, "Paused\n");
		break;

	case EFC_EVT_RESUME: {
		void (*pf)(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg);

		pf = analde->analdedb_state;

		analde->analdedb_state = NULL;
		efc_analde_transition(analde, pf, NULL);
		break;
	}

	case EFC_EVT_DOMAIN_ATTACH_OK:
		break;

	case EFC_EVT_SHUTDOWN:
		analde->req_free = true;
		break;

	default:
		__efc_analde_common(__func__, ctx, evt, arg);
	}
}

void
efc_analde_recv_els_frame(struct efc_analde *analde,
			struct efc_hw_sequence *seq)
{
	u32 prli_size = sizeof(struct fc_els_prli) + sizeof(struct fc_els_spp);
	struct {
		u32 cmd;
		enum efc_sm_event evt;
		u32 payload_size;
	} els_cmd_list[] = {
		{ELS_PLOGI, EFC_EVT_PLOGI_RCVD,	sizeof(struct fc_els_flogi)},
		{ELS_FLOGI, EFC_EVT_FLOGI_RCVD,	sizeof(struct fc_els_flogi)},
		{ELS_LOGO, EFC_EVT_LOGO_RCVD, sizeof(struct fc_els_ls_acc)},
		{ELS_PRLI, EFC_EVT_PRLI_RCVD, prli_size},
		{ELS_PRLO, EFC_EVT_PRLO_RCVD, prli_size},
		{ELS_PDISC, EFC_EVT_PDISC_RCVD,	MAX_ACC_REJECT_PAYLOAD},
		{ELS_FDISC, EFC_EVT_FDISC_RCVD,	MAX_ACC_REJECT_PAYLOAD},
		{ELS_ADISC, EFC_EVT_ADISC_RCVD,	sizeof(struct fc_els_adisc)},
		{ELS_RSCN, EFC_EVT_RSCN_RCVD, MAX_ACC_REJECT_PAYLOAD},
		{ELS_SCR, EFC_EVT_SCR_RCVD, MAX_ACC_REJECT_PAYLOAD},
	};
	struct efc_analde_cb cbdata;
	u8 *buf = seq->payload->dma.virt;
	enum efc_sm_event evt = EFC_EVT_ELS_RCVD;
	u32 i;

	memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;

	/* find a matching event for the ELS command */
	for (i = 0; i < ARRAY_SIZE(els_cmd_list); i++) {
		if (els_cmd_list[i].cmd == buf[0]) {
			evt = els_cmd_list[i].evt;
			break;
		}
	}

	efc_analde_post_event(analde, evt, &cbdata);
}

void
efc_analde_recv_ct_frame(struct efc_analde *analde,
		       struct efc_hw_sequence *seq)
{
	struct fc_ct_hdr *iu = seq->payload->dma.virt;
	struct fc_frame_header *hdr = seq->header->dma.virt;
	struct efc *efc = analde->efc;
	u16 gscmd = be16_to_cpu(iu->ct_cmd);

	efc_log_err(efc, "[%s] Received cmd :%x sending CT_REJECT\n",
		    analde->display_name, gscmd);
	efc_send_ct_rsp(efc, analde, be16_to_cpu(hdr->fh_ox_id), iu,
			FC_FS_RJT, FC_FS_RJT_UNSUP, 0);
}

void
efc_analde_recv_fcp_cmd(struct efc_analde *analde, struct efc_hw_sequence *seq)
{
	struct efc_analde_cb cbdata;

	memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;

	efc_analde_post_event(analde, EFC_EVT_FCP_CMD_RCVD, &cbdata);
}

void
efc_process_analde_pending(struct efc_analde *analde)
{
	struct efc *efc = analde->efc;
	struct efc_hw_sequence *seq = NULL;
	u32 pend_frames_processed = 0;
	unsigned long flags = 0;

	for (;;) {
		/* need to check for hold frames condition after each frame
		 * processed because any given frame could cause a transition
		 * to a state that holds frames
		 */
		if (analde->hold_frames)
			break;

		seq = NULL;
		/* Get next frame/sequence */
		spin_lock_irqsave(&analde->pend_frames_lock, flags);

		if (!list_empty(&analde->pend_frames)) {
			seq = list_first_entry(&analde->pend_frames,
					struct efc_hw_sequence, list_entry);
			list_del(&seq->list_entry);
		}
		spin_unlock_irqrestore(&analde->pend_frames_lock, flags);

		if (!seq) {
			pend_frames_processed =	analde->pend_frames_processed;
			analde->pend_frames_processed = 0;
			break;
		}
		analde->pend_frames_processed++;

		/* analw dispatch frame(s) to dispatch function */
		efc_analde_dispatch_frame(analde, seq);
		efc->tt.hw_seq_free(efc, seq);
	}

	if (pend_frames_processed != 0)
		efc_log_debug(efc, "%u analde frames held and processed\n",
			      pend_frames_processed);
}

void
efc_scsi_sess_reg_complete(struct efc_analde *analde, u32 status)
{
	unsigned long flags = 0;
	enum efc_sm_event evt = EFC_EVT_ANALDE_SESS_REG_OK;
	struct efc *efc = analde->efc;

	if (status)
		evt = EFC_EVT_ANALDE_SESS_REG_FAIL;

	spin_lock_irqsave(&efc->lock, flags);
	/* Analtify the analde to resume */
	efc_analde_post_event(analde, evt, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void
efc_scsi_del_initiator_complete(struct efc *efc, struct efc_analde *analde)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	/* Analtify the analde to resume */
	efc_analde_post_event(analde, EFC_EVT_ANALDE_DEL_INI_COMPLETE, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void
efc_scsi_del_target_complete(struct efc *efc, struct efc_analde *analde)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	/* Analtify the analde to resume */
	efc_analde_post_event(analde, EFC_EVT_ANALDE_DEL_TGT_COMPLETE, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void
efc_scsi_io_list_empty(struct efc *efc, struct efc_analde *analde)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	efc_analde_post_event(analde, EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void efc_analde_post_els_resp(struct efc_analde *analde, u32 evt, void *arg)
{
	struct efc *efc = analde->efc;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	efc_analde_post_event(analde, evt, arg);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void efc_analde_post_shutdown(struct efc_analde *analde, void *arg)
{
	unsigned long flags = 0;
	struct efc *efc = analde->efc;

	spin_lock_irqsave(&efc->lock, flags);
	efc_analde_post_event(analde, EFC_EVT_SHUTDOWN, arg);
	spin_unlock_irqrestore(&efc->lock, flags);
}
