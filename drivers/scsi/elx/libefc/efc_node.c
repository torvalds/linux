// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efc.h"

int
efc_remote_node_cb(void *arg, int event, void *data)
{
	struct efc *efc = arg;
	struct efc_remote_node *rnode = data;
	struct efc_node *node = rnode->node;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	efc_node_post_event(node, event, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);

	return 0;
}

struct efc_node *
efc_node_find(struct efc_nport *nport, u32 port_id)
{
	/* Find an FC node structure given the FC port ID */
	return xa_load(&nport->lookup, port_id);
}

static void
_efc_node_free(struct kref *arg)
{
	struct efc_node *node = container_of(arg, struct efc_node, ref);
	struct efc *efc = node->efc;
	struct efc_dma *dma;

	dma = &node->sparm_dma_buf;
	dma_pool_free(efc->node_dma_pool, dma->virt, dma->phys);
	memset(dma, 0, sizeof(struct efc_dma));
	mempool_free(node, efc->node_pool);
}

struct efc_node *efc_node_alloc(struct efc_nport *nport,
				u32 port_id, bool init, bool targ)
{
	int rc;
	struct efc_node *node = NULL;
	struct efc *efc = nport->efc;
	struct efc_dma *dma;

	if (nport->shutting_down) {
		efc_log_debug(efc, "node allocation when shutting down %06x",
			      port_id);
		return NULL;
	}

	node = mempool_alloc(efc->node_pool, GFP_ATOMIC);
	if (!node) {
		efc_log_err(efc, "node allocation failed %06x", port_id);
		return NULL;
	}
	memset(node, 0, sizeof(*node));

	dma = &node->sparm_dma_buf;
	dma->size = NODE_SPARAMS_SIZE;
	dma->virt = dma_pool_zalloc(efc->node_dma_pool, GFP_ATOMIC, &dma->phys);
	if (!dma->virt) {
		efc_log_err(efc, "node dma alloc failed\n");
		goto dma_fail;
	}
	node->rnode.indicator = U32_MAX;
	node->nport = nport;

	node->efc = efc;
	node->init = init;
	node->targ = targ;

	spin_lock_init(&node->pend_frames_lock);
	INIT_LIST_HEAD(&node->pend_frames);
	spin_lock_init(&node->els_ios_lock);
	INIT_LIST_HEAD(&node->els_ios_list);
	node->els_io_enabled = true;

	rc = efc_cmd_node_alloc(efc, &node->rnode, port_id, nport);
	if (rc) {
		efc_log_err(efc, "efc_hw_node_alloc failed: %d\n", rc);
		goto hw_alloc_fail;
	}

	node->rnode.node = node;
	node->sm.app = node;
	node->evtdepth = 0;

	efc_node_update_display_name(node);

	rc = xa_err(xa_store(&nport->lookup, port_id, node, GFP_ATOMIC));
	if (rc) {
		efc_log_err(efc, "Node lookup store failed: %d\n", rc);
		goto xa_fail;
	}

	/* initialize refcount */
	kref_init(&node->ref);
	node->release = _efc_node_free;
	kref_get(&nport->ref);

	return node;

xa_fail:
	efc_node_free_resources(efc, &node->rnode);
hw_alloc_fail:
	dma_pool_free(efc->node_dma_pool, dma->virt, dma->phys);
dma_fail:
	mempool_free(node, efc->node_pool);
	return NULL;
}

void
efc_node_free(struct efc_node *node)
{
	struct efc_nport *nport;
	struct efc *efc;
	int rc = 0;
	struct efc_node *ns = NULL;

	nport = node->nport;
	efc = node->efc;

	node_printf(node, "Free'd\n");

	if (node->refound) {
		/*
		 * Save the name server node. We will send fake RSCN event at
		 * the end to handle ignored RSCN event during node deletion
		 */
		ns = efc_node_find(node->nport, FC_FID_DIR_SERV);
	}

	if (!node->nport) {
		efc_log_err(efc, "Node already Freed\n");
		return;
	}

	/* Free HW resources */
	rc = efc_node_free_resources(efc, &node->rnode);
	if (rc < 0)
		efc_log_err(efc, "efc_hw_node_free failed: %d\n", rc);

	/* if the gidpt_delay_timer is still running, then delete it */
	if (timer_pending(&node->gidpt_delay_timer))
		del_timer(&node->gidpt_delay_timer);

	xa_erase(&nport->lookup, node->rnode.fc_id);

	/*
	 * If the node_list is empty,
	 * then post a ALL_CHILD_NODES_FREE event to the nport,
	 * after the lock is released.
	 * The nport may be free'd as a result of the event.
	 */
	if (xa_empty(&nport->lookup))
		efc_sm_post_event(&nport->sm, EFC_EVT_ALL_CHILD_NODES_FREE,
				  NULL);

	node->nport = NULL;
	node->sm.current_state = NULL;

	kref_put(&nport->ref, nport->release);
	kref_put(&node->ref, node->release);

	if (ns) {
		/* sending fake RSCN event to name server node */
		efc_node_post_event(ns, EFC_EVT_RSCN_RCVD, NULL);
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
efc_node_attach(struct efc_node *node)
{
	int rc = 0;
	struct efc_nport *nport = node->nport;
	struct efc_domain *domain = nport->domain;
	struct efc *efc = node->efc;

	if (!domain->attached) {
		efc_log_err(efc, "Warning: unattached domain\n");
		return -EIO;
	}
	/* Update node->wwpn/wwnn */

	efc_node_build_eui_name(node->wwpn, sizeof(node->wwpn),
				efc_node_get_wwpn(node));
	efc_node_build_eui_name(node->wwnn, sizeof(node->wwnn),
				efc_node_get_wwnn(node));

	efc_dma_copy_in(&node->sparm_dma_buf, node->service_params + 4,
			sizeof(node->service_params) - 4);

	/* take lock to protect node->rnode.attached */
	rc = efc_cmd_node_attach(efc, &node->rnode, &node->sparm_dma_buf);
	if (rc < 0)
		efc_log_debug(efc, "efc_hw_node_attach failed: %d\n", rc);

	return rc;
}

void
efc_node_fcid_display(u32 fc_id, char *buffer, u32 buffer_length)
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
efc_node_update_display_name(struct efc_node *node)
{
	u32 port_id = node->rnode.fc_id;
	struct efc_nport *nport = node->nport;
	char portid_display[16];

	efc_node_fcid_display(port_id, portid_display, sizeof(portid_display));

	snprintf(node->display_name, sizeof(node->display_name), "%s.%s",
		 nport->display_name, portid_display);
}

void
efc_node_send_ls_io_cleanup(struct efc_node *node)
{
	if (node->send_ls_acc != EFC_NODE_SEND_LS_ACC_NONE) {
		efc_log_debug(node->efc, "[%s] cleaning up LS_ACC oxid=0x%x\n",
			      node->display_name, node->ls_acc_oxid);

		node->send_ls_acc = EFC_NODE_SEND_LS_ACC_NONE;
		node->ls_acc_io = NULL;
	}
}

static void efc_node_handle_implicit_logo(struct efc_node *node)
{
	int rc;

	/*
	 * currently, only case for implicit logo is PLOGI
	 * recvd. Thus, node's ELS IO pending list won't be
	 * empty (PLOGI will be on it)
	 */
	WARN_ON(node->send_ls_acc != EFC_NODE_SEND_LS_ACC_PLOGI);
	node_printf(node, "Reason: implicit logout, re-authenticate\n");

	/* Re-attach node with the same HW node resources */
	node->req_free = false;
	rc = efc_node_attach(node);
	efc_node_transition(node, __efc_d_wait_node_attach, NULL);
	node->els_io_enabled = true;

	if (rc < 0)
		efc_node_post_event(node, EFC_EVT_NODE_ATTACH_FAIL, NULL);
}

static void efc_node_handle_explicit_logo(struct efc_node *node)
{
	s8 pend_frames_empty;
	unsigned long flags = 0;

	/* cleanup any pending LS_ACC ELSs */
	efc_node_send_ls_io_cleanup(node);

	spin_lock_irqsave(&node->pend_frames_lock, flags);
	pend_frames_empty = list_empty(&node->pend_frames);
	spin_unlock_irqrestore(&node->pend_frames_lock, flags);

	/*
	 * there are two scenarios where we want to keep
	 * this node alive:
	 * 1. there are pending frames that need to be
	 *    processed or
	 * 2. we're an initiator and the remote node is
	 *    a target and we need to re-authenticate
	 */
	node_printf(node, "Shutdown: explicit logo pend=%d ", !pend_frames_empty);
	node_printf(node, "nport.ini=%d node.tgt=%d\n",
		    node->nport->enable_ini, node->targ);
	if (!pend_frames_empty || (node->nport->enable_ini && node->targ)) {
		u8 send_plogi = false;

		if (node->nport->enable_ini && node->targ) {
			/*
			 * we're an initiator and
			 * node shutting down is a target;
			 * we'll need to re-authenticate in
			 * initial state
			 */
			send_plogi = true;
		}

		/*
		 * transition to __efc_d_init
		 * (will retain HW node resources)
		 */
		node->els_io_enabled = true;
		node->req_free = false;

		/*
		 * either pending frames exist or we are re-authenticating
		 * with PLOGI (or both); in either case, return to initial
		 * state
		 */
		efc_node_init_device(node, send_plogi);
	}
	/* else: let node shutdown occur */
}

static void
efc_node_purge_pending(struct efc_node *node)
{
	struct efc *efc = node->efc;
	struct efc_hw_sequence *frame, *next;
	unsigned long flags = 0;

	spin_lock_irqsave(&node->pend_frames_lock, flags);

	list_for_each_entry_safe(frame, next, &node->pend_frames, list_entry) {
		list_del(&frame->list_entry);
		efc->tt.hw_seq_free(efc, frame);
	}

	spin_unlock_irqrestore(&node->pend_frames_lock, flags);
}

void
__efc_node_shutdown(struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER: {
		efc_node_hold_frames(node);
		WARN_ON(!efc_els_io_list_empty(node, &node->els_ios_list));
		/* by default, we will be freeing node after we unwind */
		node->req_free = true;

		switch (node->shutdown_reason) {
		case EFC_NODE_SHUTDOWN_IMPLICIT_LOGO:
			/* Node shutdown b/c of PLOGI received when node
			 * already logged in. We have PLOGI service
			 * parameters, so submit node attach; we won't be
			 * freeing this node
			 */

			efc_node_handle_implicit_logo(node);
			break;

		case EFC_NODE_SHUTDOWN_EXPLICIT_LOGO:
			efc_node_handle_explicit_logo(node);
			break;

		case EFC_NODE_SHUTDOWN_DEFAULT:
		default: {
			/*
			 * shutdown due to link down,
			 * node going away (xport event) or
			 * nport shutdown, purge pending and
			 * proceed to cleanup node
			 */

			/* cleanup any pending LS_ACC ELSs */
			efc_node_send_ls_io_cleanup(node);

			node_printf(node,
				    "Shutdown reason: default, purge pending\n");
			efc_node_purge_pending(node);
			break;
		}
		}

		break;
	}
	case EFC_EVT_EXIT:
		efc_node_accept_frames(node);
		break;

	default:
		__efc_node_common(__func__, ctx, evt, arg);
	}
}

static bool
efc_node_check_els_quiesced(struct efc_node *node)
{
	/* check to see if ELS requests, completions are quiesced */
	if (node->els_req_cnt == 0 && node->els_cmpl_cnt == 0 &&
	    efc_els_io_list_empty(node, &node->els_ios_list)) {
		if (!node->attached) {
			/* hw node detach already completed, proceed */
			node_printf(node, "HW node not attached\n");
			efc_node_transition(node,
					    __efc_node_wait_ios_shutdown,
					     NULL);
		} else {
			/*
			 * hw node detach hasn't completed,
			 * transition and wait
			 */
			node_printf(node, "HW node still attached\n");
			efc_node_transition(node, __efc_node_wait_node_free,
					    NULL);
		}
		return true;
	}
	return false;
}

void
efc_node_initiate_cleanup(struct efc_node *node)
{
	/*
	 * if ELS's have already been quiesced, will move to next state
	 * if ELS's have not been quiesced, abort them
	 */
	if (!efc_node_check_els_quiesced(node)) {
		efc_node_hold_frames(node);
		efc_node_transition(node, __efc_node_wait_els_shutdown, NULL);
	}
}

void
__efc_node_wait_els_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	bool check_quiesce = false;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();
	/* Node state machine: Wait for all ELSs to complete */
	switch (evt) {
	case EFC_EVT_ENTER:
		efc_node_hold_frames(node);
		if (efc_els_io_list_empty(node, &node->els_ios_list)) {
			node_printf(node, "All ELS IOs complete\n");
			check_quiesce = true;
		}
		break;
	case EFC_EVT_EXIT:
		efc_node_accept_frames(node);
		break;

	case EFC_EVT_SRRS_ELS_REQ_OK:
	case EFC_EVT_SRRS_ELS_REQ_FAIL:
	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_ELS_REQ_ABORTED:
		if (WARN_ON(!node->els_req_cnt))
			break;
		node->els_req_cnt--;
		check_quiesce = true;
		break;

	case EFC_EVT_SRRS_ELS_CMPL_OK:
	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		if (WARN_ON(!node->els_cmpl_cnt))
			break;
		node->els_cmpl_cnt--;
		check_quiesce = true;
		break;

	case EFC_EVT_ALL_CHILD_NODES_FREE:
		/* all ELS IO's complete */
		node_printf(node, "All ELS IOs complete\n");
		WARN_ON(!efc_els_io_list_empty(node, &node->els_ios_list));
		check_quiesce = true;
		break;

	case EFC_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
		check_quiesce = true;
		break;

	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		fallthrough;

	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", efc_sm_event_name(evt));
		break;

	default:
		__efc_node_common(__func__, ctx, evt, arg);
	}

	if (check_quiesce)
		efc_node_check_els_quiesced(node);
}

void
__efc_node_wait_node_free(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_node_hold_frames(node);
		break;

	case EFC_EVT_EXIT:
		efc_node_accept_frames(node);
		break;

	case EFC_EVT_NODE_FREE_OK:
		/* node is officially no longer attached */
		node->attached = false;
		efc_node_transition(node, __efc_node_wait_ios_shutdown, NULL);
		break;

	case EFC_EVT_ALL_CHILD_NODES_FREE:
	case EFC_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
		/* As IOs and ELS IO's complete we expect to get these events */
		break;

	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		fallthrough;

	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", efc_sm_event_name(evt));
		break;
	default:
		__efc_node_common(__func__, ctx, evt, arg);
	}
}

void
__efc_node_wait_ios_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;
	struct efc *efc = node->efc;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_node_hold_frames(node);

		/* first check to see if no ELS IOs are outstanding */
		if (efc_els_io_list_empty(node, &node->els_ios_list))
			/* If there are any active IOS, Free them. */
			efc_node_transition(node, __efc_node_shutdown, NULL);
		break;

	case EFC_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
	case EFC_EVT_ALL_CHILD_NODES_FREE:
		if (efc_els_io_list_empty(node, &node->els_ios_list))
			efc_node_transition(node, __efc_node_shutdown, NULL);
		break;

	case EFC_EVT_EXIT:
		efc_node_accept_frames(node);
		break;

	case EFC_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		if (WARN_ON(!node->els_req_cnt))
			break;
		node->els_req_cnt--;
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		fallthrough;

	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		efc_log_debug(efc, "[%s] %-20s\n", node->display_name,
			      efc_sm_event_name(evt));
		break;
	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__efc_node_common(__func__, ctx, evt, arg);
	}
}

void
__efc_node_common(const char *funcname, struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = NULL;
	struct efc *efc = NULL;
	struct efc_node_cb *cbdata = arg;

	node = ctx->app;
	efc = node->efc;

	switch (evt) {
	case EFC_EVT_ENTER:
	case EFC_EVT_REENTER:
	case EFC_EVT_EXIT:
	case EFC_EVT_NPORT_TOPOLOGY_NOTIFY:
	case EFC_EVT_NODE_MISSING:
	case EFC_EVT_FCP_CMD_RCVD:
		break;

	case EFC_EVT_NODE_REFOUND:
		node->refound = true;
		break;

	/*
	 * node->attached must be set appropriately
	 * for all node attach/detach events
	 */
	case EFC_EVT_NODE_ATTACH_OK:
		node->attached = true;
		break;

	case EFC_EVT_NODE_FREE_OK:
	case EFC_EVT_NODE_ATTACH_FAIL:
		node->attached = false;
		break;

	/*
	 * handle any ELS completions that
	 * other states either didn't care about
	 * or forgot about
	 */
	case EFC_EVT_SRRS_ELS_CMPL_OK:
	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		if (WARN_ON(!node->els_cmpl_cnt))
			break;
		node->els_cmpl_cnt--;
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
		if (WARN_ON(!node->els_req_cnt))
			break;
		node->els_req_cnt--;
		break;

	case EFC_EVT_ELS_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		/*
		 * Unsupported ELS was received,
		 * send LS_RJT, command not supported
		 */
		efc_log_debug(efc,
			      "[%s] (%s) ELS x%02x, LS_RJT not supported\n",
			      node->display_name, funcname,
			      ((u8 *)cbdata->payload->dma.virt)[0]);

		efc_send_ls_rjt(node, be16_to_cpu(hdr->fh_ox_id),
				ELS_RJT_UNSUP, ELS_EXPL_NONE, 0);
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
			      node->display_name, funcname,
			      efc_sm_event_name(evt));
		/* if we didn't catch this in a state, send generic LS_RJT */
		efc_send_ls_rjt(node, be16_to_cpu(hdr->fh_ox_id),
				ELS_RJT_UNAB, ELS_EXPL_NONE, 0);
		break;
	}
	case EFC_EVT_ABTS_RCVD: {
		efc_log_debug(efc, "[%s] (%s) %s sending BA_ACC\n",
			      node->display_name, funcname,
			      efc_sm_event_name(evt));

		/* sm: / send BA_ACC */
		efc_send_bls_acc(node, cbdata->header->dma.virt);
		break;
	}

	default:
		efc_log_debug(node->efc, "[%s] %-20s %-20s not handled\n",
			      node->display_name, funcname,
			      efc_sm_event_name(evt));
	}
}

void
efc_node_save_sparms(struct efc_node *node, void *payload)
{
	memcpy(node->service_params, payload, sizeof(node->service_params));
}

void
efc_node_post_event(struct efc_node *node,
		    enum efc_sm_event evt, void *arg)
{
	bool free_node = false;

	node->evtdepth++;

	efc_sm_post_event(&node->sm, evt, arg);

	/* If our event call depth is one and
	 * we're not holding frames
	 * then we can dispatch any pending frames.
	 * We don't want to allow the efc_process_node_pending()
	 * call to recurse.
	 */
	if (!node->hold_frames && node->evtdepth == 1)
		efc_process_node_pending(node);

	node->evtdepth--;

	/*
	 * Free the node object if so requested,
	 * and we're at an event call depth of zero
	 */
	if (node->evtdepth == 0 && node->req_free)
		free_node = true;

	if (free_node)
		efc_node_free(node);
}

void
efc_node_transition(struct efc_node *node,
		    void (*state)(struct efc_sm_ctx *,
				  enum efc_sm_event, void *), void *data)
{
	struct efc_sm_ctx *ctx = &node->sm;

	if (ctx->current_state == state) {
		efc_node_post_event(node, EFC_EVT_REENTER, data);
	} else {
		efc_node_post_event(node, EFC_EVT_EXIT, data);
		ctx->current_state = state;
		efc_node_post_event(node, EFC_EVT_ENTER, data);
	}
}

void
efc_node_build_eui_name(char *buf, u32 buf_len, uint64_t eui_name)
{
	memset(buf, 0, buf_len);

	snprintf(buf, buf_len, "eui.%016llX", (unsigned long long)eui_name);
}

u64
efc_node_get_wwpn(struct efc_node *node)
{
	struct fc_els_flogi *sp =
			(struct fc_els_flogi *)node->service_params;

	return be64_to_cpu(sp->fl_wwpn);
}

u64
efc_node_get_wwnn(struct efc_node *node)
{
	struct fc_els_flogi *sp =
			(struct fc_els_flogi *)node->service_params;

	return be64_to_cpu(sp->fl_wwnn);
}

int
efc_node_check_els_req(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg,
		u8 cmd, void (*efc_node_common_func)(const char *,
				struct efc_sm_ctx *, enum efc_sm_event, void *),
		const char *funcname)
{
	return 0;
}

int
efc_node_check_ns_req(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg,
		u16 cmd, void (*efc_node_common_func)(const char *,
				struct efc_sm_ctx *, enum efc_sm_event, void *),
		const char *funcname)
{
	return 0;
}

int
efc_els_io_list_empty(struct efc_node *node, struct list_head *list)
{
	int empty;
	unsigned long flags = 0;

	spin_lock_irqsave(&node->els_ios_lock, flags);
	empty = list_empty(list);
	spin_unlock_irqrestore(&node->els_ios_lock, flags);
	return empty;
}

void
efc_node_pause(struct efc_node *node,
	       void (*state)(struct efc_sm_ctx *,
			     enum efc_sm_event, void *))

{
	node->nodedb_state = state;
	efc_node_transition(node, __efc_node_paused, NULL);
}

void
__efc_node_paused(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	/*
	 * This state is entered when a state is "paused". When resumed, the
	 * node is transitioned to a previously saved state (node->ndoedb_state)
	 */
	switch (evt) {
	case EFC_EVT_ENTER:
		node_printf(node, "Paused\n");
		break;

	case EFC_EVT_RESUME: {
		void (*pf)(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg);

		pf = node->nodedb_state;

		node->nodedb_state = NULL;
		efc_node_transition(node, pf, NULL);
		break;
	}

	case EFC_EVT_DOMAIN_ATTACH_OK:
		break;

	case EFC_EVT_SHUTDOWN:
		node->req_free = true;
		break;

	default:
		__efc_node_common(__func__, ctx, evt, arg);
	}
}

void
efc_node_recv_els_frame(struct efc_node *node,
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
	struct efc_node_cb cbdata;
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

	efc_node_post_event(node, evt, &cbdata);
}

void
efc_node_recv_ct_frame(struct efc_node *node,
		       struct efc_hw_sequence *seq)
{
	struct fc_ct_hdr *iu = seq->payload->dma.virt;
	struct fc_frame_header *hdr = seq->header->dma.virt;
	struct efc *efc = node->efc;
	u16 gscmd = be16_to_cpu(iu->ct_cmd);

	efc_log_err(efc, "[%s] Received cmd :%x sending CT_REJECT\n",
		    node->display_name, gscmd);
	efc_send_ct_rsp(efc, node, be16_to_cpu(hdr->fh_ox_id), iu,
			FC_FS_RJT, FC_FS_RJT_UNSUP, 0);
}

void
efc_node_recv_fcp_cmd(struct efc_node *node, struct efc_hw_sequence *seq)
{
	struct efc_node_cb cbdata;

	memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;

	efc_node_post_event(node, EFC_EVT_FCP_CMD_RCVD, &cbdata);
}

void
efc_process_node_pending(struct efc_node *node)
{
	struct efc *efc = node->efc;
	struct efc_hw_sequence *seq = NULL;
	u32 pend_frames_processed = 0;
	unsigned long flags = 0;

	for (;;) {
		/* need to check for hold frames condition after each frame
		 * processed because any given frame could cause a transition
		 * to a state that holds frames
		 */
		if (node->hold_frames)
			break;

		seq = NULL;
		/* Get next frame/sequence */
		spin_lock_irqsave(&node->pend_frames_lock, flags);

		if (!list_empty(&node->pend_frames)) {
			seq = list_first_entry(&node->pend_frames,
					struct efc_hw_sequence, list_entry);
			list_del(&seq->list_entry);
		}
		spin_unlock_irqrestore(&node->pend_frames_lock, flags);

		if (!seq) {
			pend_frames_processed =	node->pend_frames_processed;
			node->pend_frames_processed = 0;
			break;
		}
		node->pend_frames_processed++;

		/* now dispatch frame(s) to dispatch function */
		efc_node_dispatch_frame(node, seq);
		efc->tt.hw_seq_free(efc, seq);
	}

	if (pend_frames_processed != 0)
		efc_log_debug(efc, "%u node frames held and processed\n",
			      pend_frames_processed);
}

void
efc_scsi_sess_reg_complete(struct efc_node *node, u32 status)
{
	unsigned long flags = 0;
	enum efc_sm_event evt = EFC_EVT_NODE_SESS_REG_OK;
	struct efc *efc = node->efc;

	if (status)
		evt = EFC_EVT_NODE_SESS_REG_FAIL;

	spin_lock_irqsave(&efc->lock, flags);
	/* Notify the node to resume */
	efc_node_post_event(node, evt, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void
efc_scsi_del_initiator_complete(struct efc *efc, struct efc_node *node)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	/* Notify the node to resume */
	efc_node_post_event(node, EFC_EVT_NODE_DEL_INI_COMPLETE, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void
efc_scsi_del_target_complete(struct efc *efc, struct efc_node *node)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	/* Notify the node to resume */
	efc_node_post_event(node, EFC_EVT_NODE_DEL_TGT_COMPLETE, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void
efc_scsi_io_list_empty(struct efc *efc, struct efc_node *node)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	efc_node_post_event(node, EFC_EVT_NODE_ACTIVE_IO_LIST_EMPTY, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void efc_node_post_els_resp(struct efc_node *node, u32 evt, void *arg)
{
	struct efc *efc = node->efc;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->lock, flags);
	efc_node_post_event(node, evt, arg);
	spin_unlock_irqrestore(&efc->lock, flags);
}

void efc_node_post_shutdown(struct efc_node *node, void *arg)
{
	unsigned long flags = 0;
	struct efc *efc = node->efc;

	spin_lock_irqsave(&efc->lock, flags);
	efc_node_post_event(node, EFC_EVT_SHUTDOWN, arg);
	spin_unlock_irqrestore(&efc->lock, flags);
}
