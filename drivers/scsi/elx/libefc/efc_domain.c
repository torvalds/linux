// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * domain_sm Domain State Machine: States
 */

#include "efc.h"

int
efc_domain_cb(void *arg, int event, void *data)
{
	struct efc *efc = arg;
	struct efc_domain *domain = NULL;
	int rc = 0;
	unsigned long flags = 0;

	if (event != EFC_HW_DOMAIN_FOUND)
		domain = data;

	/* Accept domain callback events from the user driver */
	spin_lock_irqsave(&efc->lock, flags);
	switch (event) {
	case EFC_HW_DOMAIN_FOUND: {
		u64 fcf_wwn = 0;
		struct efc_domain_record *drec = data;

		/* extract the fcf_wwn */
		fcf_wwn = be64_to_cpu(*((__be64 *)drec->wwn));

		efc_log_debug(efc, "Domain found: wwn %016llX\n", fcf_wwn);

		/* lookup domain, or allocate a new one */
		domain = efc->domain;
		if (!domain) {
			domain = efc_domain_alloc(efc, fcf_wwn);
			if (!domain) {
				efc_log_err(efc, "efc_domain_alloc() failed\n");
				rc = -1;
				break;
			}
			efc_sm_transition(&domain->drvsm, __efc_domain_init,
					  NULL);
		}
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_FOUND, drec);
		break;
	}

	case EFC_HW_DOMAIN_LOST:
		domain_trace(domain, "EFC_HW_DOMAIN_LOST:\n");
		efc->hold_frames = true;
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_LOST, NULL);
		break;

	case EFC_HW_DOMAIN_ALLOC_OK:
		domain_trace(domain, "EFC_HW_DOMAIN_ALLOC_OK:\n");
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_ALLOC_OK, NULL);
		break;

	case EFC_HW_DOMAIN_ALLOC_FAIL:
		domain_trace(domain, "EFC_HW_DOMAIN_ALLOC_FAIL:\n");
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_ALLOC_FAIL,
				      NULL);
		break;

	case EFC_HW_DOMAIN_ATTACH_OK:
		domain_trace(domain, "EFC_HW_DOMAIN_ATTACH_OK:\n");
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_ATTACH_OK, NULL);
		break;

	case EFC_HW_DOMAIN_ATTACH_FAIL:
		domain_trace(domain, "EFC_HW_DOMAIN_ATTACH_FAIL:\n");
		efc_domain_post_event(domain,
				      EFC_EVT_DOMAIN_ATTACH_FAIL, NULL);
		break;

	case EFC_HW_DOMAIN_FREE_OK:
		domain_trace(domain, "EFC_HW_DOMAIN_FREE_OK:\n");
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_FREE_OK, NULL);
		break;

	case EFC_HW_DOMAIN_FREE_FAIL:
		domain_trace(domain, "EFC_HW_DOMAIN_FREE_FAIL:\n");
		efc_domain_post_event(domain, EFC_EVT_DOMAIN_FREE_FAIL, NULL);
		break;

	default:
		efc_log_warn(efc, "unsupported event %#x\n", event);
	}
	spin_unlock_irqrestore(&efc->lock, flags);

	if (efc->domain && domain->req_accept_frames) {
		domain->req_accept_frames = false;
		efc->hold_frames = false;
	}

	return rc;
}

static void
_efc_domain_free(struct kref *arg)
{
	struct efc_domain *domain = container_of(arg, struct efc_domain, ref);
	struct efc *efc = domain->efc;

	if (efc->domain_free_cb)
		(*efc->domain_free_cb)(efc, efc->domain_free_cb_arg);

	kfree(domain);
}

void
efc_domain_free(struct efc_domain *domain)
{
	struct efc *efc;

	efc = domain->efc;

	/* Hold frames to clear the domain pointer from the xport lookup */
	efc->hold_frames = false;

	efc_log_debug(efc, "Domain free: wwn %016llX\n", domain->fcf_wwn);

	xa_destroy(&domain->lookup);
	efc->domain = NULL;
	kref_put(&domain->ref, domain->release);
}

struct efc_domain *
efc_domain_alloc(struct efc *efc, uint64_t fcf_wwn)
{
	struct efc_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_ATOMIC);
	if (!domain)
		return NULL;

	domain->efc = efc;
	domain->drvsm.app = domain;

	/* initialize refcount */
	kref_init(&domain->ref);
	domain->release = _efc_domain_free;

	xa_init(&domain->lookup);

	INIT_LIST_HEAD(&domain->nport_list);
	efc->domain = domain;
	domain->fcf_wwn = fcf_wwn;
	efc_log_debug(efc, "Domain allocated: wwn %016llX\n", domain->fcf_wwn);

	return domain;
}

void
efc_register_domain_free_cb(struct efc *efc,
			    void (*callback)(struct efc *efc, void *arg),
			    void *arg)
{
	/* Register a callback to be called when the domain is freed */
	efc->domain_free_cb = callback;
	efc->domain_free_cb_arg = arg;
	if (!efc->domain && callback)
		(*callback)(efc, arg);
}

static void
__efc_domain_common(const char *funcname, struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg)
{
	struct efc_domain *domain = ctx->app;

	switch (evt) {
	case EFC_EVT_ENTER:
	case EFC_EVT_REENTER:
	case EFC_EVT_EXIT:
	case EFC_EVT_ALL_CHILD_NODES_FREE:
		/*
		 * this can arise if an FLOGI fails on the NPORT,
		 * and the NPORT is shutdown
		 */
		break;
	default:
		efc_log_warn(domain->efc, "%-20s %-20s not handled\n",
			     funcname, efc_sm_event_name(evt));
	}
}

static void
__efc_domain_common_shutdown(const char *funcname, struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	struct efc_domain *domain = ctx->app;

	switch (evt) {
	case EFC_EVT_ENTER:
	case EFC_EVT_REENTER:
	case EFC_EVT_EXIT:
		break;
	case EFC_EVT_DOMAIN_FOUND:
		/* save drec, mark domain_found_pending */
		memcpy(&domain->pending_drec, arg,
		       sizeof(domain->pending_drec));
		domain->domain_found_pending = true;
		break;
	case EFC_EVT_DOMAIN_LOST:
		/* unmark domain_found_pending */
		domain->domain_found_pending = false;
		break;

	default:
		efc_log_warn(domain->efc, "%-20s %-20s not handled\n",
			     funcname, efc_sm_event_name(evt));
	}
}

#define std_domain_state_decl(...)\
	struct efc_domain *domain = NULL;\
	struct efc *efc = NULL;\
	\
	WARN_ON(!ctx || !ctx->app);\
	domain = ctx->app;\
	WARN_ON(!domain->efc);\
	efc = domain->efc

void
__efc_domain_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		  void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch (evt) {
	case EFC_EVT_ENTER:
		domain->attached = false;
		break;

	case EFC_EVT_DOMAIN_FOUND: {
		u32	i;
		struct efc_domain_record *drec = arg;
		struct efc_nport *nport;

		u64 my_wwnn = efc->req_wwnn;
		u64 my_wwpn = efc->req_wwpn;
		__be64 bewwpn;

		if (my_wwpn == 0 || my_wwnn == 0) {
			efc_log_debug(efc, "using default hardware WWN config\n");
			my_wwpn = efc->def_wwpn;
			my_wwnn = efc->def_wwnn;
		}

		efc_log_debug(efc, "Create nport WWPN %016llX WWNN %016llX\n",
			      my_wwpn, my_wwnn);

		/* Allocate a nport and transition to __efc_nport_allocated */
		nport = efc_nport_alloc(domain, my_wwpn, my_wwnn, U32_MAX,
					efc->enable_ini, efc->enable_tgt);

		if (!nport) {
			efc_log_err(efc, "efc_nport_alloc() failed\n");
			break;
		}
		efc_sm_transition(&nport->sm, __efc_nport_allocated, NULL);

		bewwpn = cpu_to_be64(nport->wwpn);

		/* allocate struct efc_nport object for local port
		 * Note: drec->fc_id is ALPA from read_topology only if loop
		 */
		if (efc_cmd_nport_alloc(efc, nport, NULL, (uint8_t *)&bewwpn)) {
			efc_log_err(efc, "Can't allocate port\n");
			efc_nport_free(nport);
			break;
		}

		domain->is_loop = drec->is_loop;

		/*
		 * If the loop position map includes ALPA == 0,
		 * then we are in a public loop (NL_PORT)
		 * Note that the first element of the loopmap[]
		 * contains the count of elements, and if
		 * ALPA == 0 is present, it will occupy the first
		 * location after the count.
		 */
		domain->is_nlport = drec->map.loop[1] == 0x00;

		if (!domain->is_loop) {
			/* Initiate HW domain alloc */
			if (efc_cmd_domain_alloc(efc, domain, drec->index)) {
				efc_log_err(efc,
					    "Failed to initiate HW domain allocation\n");
				break;
			}
			efc_sm_transition(ctx, __efc_domain_wait_alloc, arg);
			break;
		}

		efc_log_debug(efc, "%s fc_id=%#x speed=%d\n",
			      drec->is_loop ?
			      (domain->is_nlport ?
			      "public-loop" : "loop") : "other",
			      drec->fc_id, drec->speed);

		nport->fc_id = drec->fc_id;
		nport->topology = EFC_NPORT_TOPO_FC_AL;
		snprintf(nport->display_name, sizeof(nport->display_name),
			 "s%06x", drec->fc_id);

		if (efc->enable_ini) {
			u32 count = drec->map.loop[0];

			efc_log_debug(efc, "%d position map entries\n",
				      count);
			for (i = 1; i <= count; i++) {
				if (drec->map.loop[i] != drec->fc_id) {
					struct efc_node *node;

					efc_log_debug(efc, "%#x -> %#x\n",
						      drec->fc_id,
						      drec->map.loop[i]);
					node = efc_node_alloc(nport,
							      drec->map.loop[i],
							      false, true);
					if (!node) {
						efc_log_err(efc,
							    "efc_node_alloc() failed\n");
						break;
					}
					efc_node_transition(node,
							    __efc_d_wait_loop,
							    NULL);
				}
			}
		}

		/* Initiate HW domain alloc */
		if (efc_cmd_domain_alloc(efc, domain, drec->index)) {
			efc_log_err(efc,
				    "Failed to initiate HW domain allocation\n");
			break;
		}
		efc_sm_transition(ctx, __efc_domain_wait_alloc, arg);
		break;
	}
	default:
		__efc_domain_common(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_wait_alloc(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch (evt) {
	case EFC_EVT_DOMAIN_ALLOC_OK: {
		struct fc_els_flogi  *sp;
		struct efc_nport *nport;

		nport = domain->nport;
		if (WARN_ON(!nport))
			return;

		sp = (struct fc_els_flogi  *)nport->service_params;

		/* Save the domain service parameters */
		memcpy(domain->service_params + 4, domain->dma.virt,
		       sizeof(struct fc_els_flogi) - 4);
		memcpy(nport->service_params + 4, domain->dma.virt,
		       sizeof(struct fc_els_flogi) - 4);

		/*
		 * Update the nport's service parameters,
		 * user might have specified non-default names
		 */
		sp->fl_wwpn = cpu_to_be64(nport->wwpn);
		sp->fl_wwnn = cpu_to_be64(nport->wwnn);

		/*
		 * Take the loop topology path,
		 * unless we are an NL_PORT (public loop)
		 */
		if (domain->is_loop && !domain->is_nlport) {
			/*
			 * For loop, we already have our FC ID
			 * and don't need fabric login.
			 * Transition to the allocated state and
			 * post an event to attach to
			 * the domain. Note that this breaks the
			 * normal action/transition
			 * pattern here to avoid a race with the
			 * domain attach callback.
			 */
			/* sm: is_loop / domain_attach */
			efc_sm_transition(ctx, __efc_domain_allocated, NULL);
			__efc_domain_attach_internal(domain, nport->fc_id);
			break;
		}
		{
			struct efc_node *node;

			/* alloc fabric node, send FLOGI */
			node = efc_node_find(nport, FC_FID_FLOGI);
			if (node) {
				efc_log_err(efc,
					    "Fabric Controller node already exists\n");
				break;
			}
			node = efc_node_alloc(nport, FC_FID_FLOGI,
					      false, false);
			if (!node) {
				efc_log_err(efc,
					    "Error: efc_node_alloc() failed\n");
			} else {
				efc_node_transition(node,
						    __efc_fabric_init, NULL);
			}
			/* Accept frames */
			domain->req_accept_frames = true;
		}
		/* sm: / start fabric logins */
		efc_sm_transition(ctx, __efc_domain_allocated, NULL);
		break;
	}

	case EFC_EVT_DOMAIN_ALLOC_FAIL:
		efc_log_err(efc, "%s recv'd waiting for DOMAIN_ALLOC_OK;",
			    efc_sm_event_name(evt));
		efc_log_err(efc, "shutting down domain\n");
		domain->req_domain_free = true;
		break;

	case EFC_EVT_DOMAIN_FOUND:
		/* Should not happen */
		break;

	case EFC_EVT_DOMAIN_LOST:
		efc_log_debug(efc,
			      "%s received while waiting for hw_domain_alloc()\n",
			efc_sm_event_name(evt));
		efc_sm_transition(ctx, __efc_domain_wait_domain_lost, NULL);
		break;

	default:
		__efc_domain_common(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_allocated(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch (evt) {
	case EFC_EVT_DOMAIN_REQ_ATTACH: {
		int rc = 0;
		u32 fc_id;

		if (WARN_ON(!arg))
			return;

		fc_id = *((u32 *)arg);
		efc_log_debug(efc, "Requesting hw domain attach fc_id x%x\n",
			      fc_id);
		/* Update nport lookup */
		rc = xa_err(xa_store(&domain->lookup, fc_id, domain->nport,
				     GFP_ATOMIC));
		if (rc) {
			efc_log_err(efc, "Sport lookup store failed: %d\n", rc);
			return;
		}

		/* Update display name for the nport */
		efc_node_fcid_display(fc_id, domain->nport->display_name,
				      sizeof(domain->nport->display_name));

		/* Issue domain attach call */
		rc = efc_cmd_domain_attach(efc, domain, fc_id);
		if (rc) {
			efc_log_err(efc, "efc_hw_domain_attach failed: %d\n",
				    rc);
			return;
		}
		/* sm: / domain_attach */
		efc_sm_transition(ctx, __efc_domain_wait_attach, NULL);
		break;
	}

	case EFC_EVT_DOMAIN_FOUND:
		/* Should not happen */
		efc_log_err(efc, "%s: evt: %d should not happen\n",
			    __func__, evt);
		break;

	case EFC_EVT_DOMAIN_LOST: {
		efc_log_debug(efc,
			      "%s received while in EFC_EVT_DOMAIN_REQ_ATTACH\n",
			efc_sm_event_name(evt));
		if (!list_empty(&domain->nport_list)) {
			/*
			 * if there are nports, transition to
			 * wait state and send shutdown to each
			 * nport
			 */
			struct efc_nport *nport = NULL, *nport_next = NULL;

			efc_sm_transition(ctx, __efc_domain_wait_nports_free,
					  NULL);
			list_for_each_entry_safe(nport, nport_next,
						 &domain->nport_list,
						 list_entry) {
				efc_sm_post_event(&nport->sm,
						  EFC_EVT_SHUTDOWN, NULL);
			}
		} else {
			/* no nports exist, free domain */
			efc_sm_transition(ctx, __efc_domain_wait_shutdown,
					  NULL);
			if (efc_cmd_domain_free(efc, domain))
				efc_log_err(efc, "hw_domain_free failed\n");
		}

		break;
	}

	default:
		__efc_domain_common(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_wait_attach(struct efc_sm_ctx *ctx,
			 enum efc_sm_event evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch (evt) {
	case EFC_EVT_DOMAIN_ATTACH_OK: {
		struct efc_node *node = NULL;
		struct efc_nport *nport, *next_nport;
		unsigned long index;

		/*
		 * Set domain notify pending state to avoid
		 * duplicate domain event post
		 */
		domain->domain_notify_pend = true;

		/* Mark as attached */
		domain->attached = true;

		/* Transition to ready */
		/* sm: / forward event to all nports and nodes */
		efc_sm_transition(ctx, __efc_domain_ready, NULL);

		/* We have an FCFI, so we can accept frames */
		domain->req_accept_frames = true;

		/*
		 * Notify all nodes that the domain attach request
		 * has completed
		 * Note: nport will have already received notification
		 * of nport attached as a result of the HW's port attach.
		 */
		list_for_each_entry_safe(nport, next_nport,
					 &domain->nport_list, list_entry) {
			xa_for_each(&nport->lookup, index, node) {
				efc_node_post_event(node,
						    EFC_EVT_DOMAIN_ATTACH_OK,
						    NULL);
			}
		}
		domain->domain_notify_pend = false;
		break;
	}

	case EFC_EVT_DOMAIN_ATTACH_FAIL:
		efc_log_debug(efc,
			      "%s received while waiting for hw attach\n",
			      efc_sm_event_name(evt));
		break;

	case EFC_EVT_DOMAIN_FOUND:
		/* Should not happen */
		efc_log_err(efc, "%s: evt: %d should not happen\n",
			    __func__, evt);
		break;

	case EFC_EVT_DOMAIN_LOST:
		/*
		 * Domain lost while waiting for an attach to complete,
		 * go to a state that waits for  the domain attach to
		 * complete, then handle domain lost
		 */
		efc_sm_transition(ctx, __efc_domain_wait_domain_lost, NULL);
		break;

	case EFC_EVT_DOMAIN_REQ_ATTACH:
		/*
		 * In P2P we can get an attach request from
		 * the other FLOGI path, so drop this one
		 */
		break;

	default:
		__efc_domain_common(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_ready(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch (evt) {
	case EFC_EVT_ENTER: {
		/* start any pending vports */
		if (efc_vport_start(domain)) {
			efc_log_debug(domain->efc,
				      "efc_vport_start didn't start vports\n");
		}
		break;
	}
	case EFC_EVT_DOMAIN_LOST: {
		if (!list_empty(&domain->nport_list)) {
			/*
			 * if there are nports, transition to wait state
			 * and send shutdown to each nport
			 */
			struct efc_nport *nport = NULL, *nport_next = NULL;

			efc_sm_transition(ctx, __efc_domain_wait_nports_free,
					  NULL);
			list_for_each_entry_safe(nport, nport_next,
						 &domain->nport_list,
						 list_entry) {
				efc_sm_post_event(&nport->sm,
						  EFC_EVT_SHUTDOWN, NULL);
			}
		} else {
			/* no nports exist, free domain */
			efc_sm_transition(ctx, __efc_domain_wait_shutdown,
					  NULL);
			if (efc_cmd_domain_free(efc, domain))
				efc_log_err(efc, "hw_domain_free failed\n");
		}
		break;
	}

	case EFC_EVT_DOMAIN_FOUND:
		/* Should not happen */
		efc_log_err(efc, "%s: evt: %d should not happen\n",
			    __func__, evt);
		break;

	case EFC_EVT_DOMAIN_REQ_ATTACH: {
		/* can happen during p2p */
		u32 fc_id;

		fc_id = *((u32 *)arg);

		/* Assume that the domain is attached */
		WARN_ON(!domain->attached);

		/*
		 * Verify that the requested FC_ID
		 * is the same as the one we're working with
		 */
		WARN_ON(domain->nport->fc_id != fc_id);
		break;
	}

	default:
		__efc_domain_common(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_wait_nports_free(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
			      void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	/* Wait for nodes to free prior to the domain shutdown */
	switch (evt) {
	case EFC_EVT_ALL_CHILD_NODES_FREE: {
		int rc;

		/* sm: / efc_hw_domain_free */
		efc_sm_transition(ctx, __efc_domain_wait_shutdown, NULL);

		/* Request efc_hw_domain_free and wait for completion */
		rc = efc_cmd_domain_free(efc, domain);
		if (rc) {
			efc_log_err(efc, "efc_hw_domain_free() failed: %d\n",
				    rc);
		}
		break;
	}
	default:
		__efc_domain_common_shutdown(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_wait_shutdown(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch (evt) {
	case EFC_EVT_DOMAIN_FREE_OK:
		/* sm: / domain_free */
		if (domain->domain_found_pending) {
			/*
			 * save fcf_wwn and drec from this domain,
			 * free current domain and allocate
			 * a new one with the same fcf_wwn
			 * could use a SLI-4 "re-register VPI"
			 * operation here?
			 */
			u64 fcf_wwn = domain->fcf_wwn;
			struct efc_domain_record drec = domain->pending_drec;

			efc_log_debug(efc, "Reallocating domain\n");
			domain->req_domain_free = true;
			domain = efc_domain_alloc(efc, fcf_wwn);

			if (!domain) {
				efc_log_err(efc,
					    "efc_domain_alloc() failed\n");
				return;
			}
			/*
			 * got a new domain; at this point,
			 * there are at least two domains
			 * once the req_domain_free flag is processed,
			 * the associated domain will be removed.
			 */
			efc_sm_transition(&domain->drvsm, __efc_domain_init,
					  NULL);
			efc_sm_post_event(&domain->drvsm,
					  EFC_EVT_DOMAIN_FOUND, &drec);
		} else {
			domain->req_domain_free = true;
		}
		break;
	default:
		__efc_domain_common_shutdown(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_wait_domain_lost(struct efc_sm_ctx *ctx,
			      enum efc_sm_event evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	/*
	 * Wait for the domain alloc/attach completion
	 * after receiving a domain lost.
	 */
	switch (evt) {
	case EFC_EVT_DOMAIN_ALLOC_OK:
	case EFC_EVT_DOMAIN_ATTACH_OK: {
		if (!list_empty(&domain->nport_list)) {
			/*
			 * if there are nports, transition to
			 * wait state and send shutdown to each nport
			 */
			struct efc_nport *nport = NULL, *nport_next = NULL;

			efc_sm_transition(ctx, __efc_domain_wait_nports_free,
					  NULL);
			list_for_each_entry_safe(nport, nport_next,
						 &domain->nport_list,
						 list_entry) {
				efc_sm_post_event(&nport->sm,
						  EFC_EVT_SHUTDOWN, NULL);
			}
		} else {
			/* no nports exist, free domain */
			efc_sm_transition(ctx, __efc_domain_wait_shutdown,
					  NULL);
			if (efc_cmd_domain_free(efc, domain))
				efc_log_err(efc, "hw_domain_free() failed\n");
		}
		break;
	}
	case EFC_EVT_DOMAIN_ALLOC_FAIL:
	case EFC_EVT_DOMAIN_ATTACH_FAIL:
		efc_log_err(efc, "[domain] %-20s: failed\n",
			    efc_sm_event_name(evt));
		break;

	default:
		__efc_domain_common_shutdown(__func__, ctx, evt, arg);
	}
}

void
__efc_domain_attach_internal(struct efc_domain *domain, u32 s_id)
{
	memcpy(domain->dma.virt,
	       ((uint8_t *)domain->flogi_service_params) + 4,
		   sizeof(struct fc_els_flogi) - 4);
	(void)efc_sm_post_event(&domain->drvsm, EFC_EVT_DOMAIN_REQ_ATTACH,
				 &s_id);
}

void
efc_domain_attach(struct efc_domain *domain, u32 s_id)
{
	__efc_domain_attach_internal(domain, s_id);
}

int
efc_domain_post_event(struct efc_domain *domain,
		      enum efc_sm_event event, void *arg)
{
	int rc;
	bool req_domain_free;

	rc = efc_sm_post_event(&domain->drvsm, event, arg);

	req_domain_free = domain->req_domain_free;
	domain->req_domain_free = false;

	if (req_domain_free)
		efc_domain_free(domain);

	return rc;
}

static void
efct_domain_process_pending(struct efc_domain *domain)
{
	struct efc *efc = domain->efc;
	struct efc_hw_sequence *seq = NULL;
	u32 processed = 0;
	unsigned long flags = 0;

	for (;;) {
		/* need to check for hold frames condition after each frame
		 * processed because any given frame could cause a transition
		 * to a state that holds frames
		 */
		if (efc->hold_frames)
			break;

		/* Get next frame/sequence */
		spin_lock_irqsave(&efc->pend_frames_lock, flags);

		if (!list_empty(&efc->pend_frames)) {
			seq = list_first_entry(&efc->pend_frames,
					struct efc_hw_sequence, list_entry);
			list_del(&seq->list_entry);
		}

		if (!seq) {
			processed = efc->pend_frames_processed;
			efc->pend_frames_processed = 0;
			spin_unlock_irqrestore(&efc->pend_frames_lock, flags);
			break;
		}
		efc->pend_frames_processed++;

		spin_unlock_irqrestore(&efc->pend_frames_lock, flags);

		/* now dispatch frame(s) to dispatch function */
		if (efc_domain_dispatch_frame(domain, seq))
			efc->tt.hw_seq_free(efc, seq);

		seq = NULL;
	}

	if (processed != 0)
		efc_log_debug(efc, "%u domain frames held and processed\n",
			      processed);
}

void
efc_dispatch_frame(struct efc *efc, struct efc_hw_sequence *seq)
{
	struct efc_domain *domain = efc->domain;

	/*
	 * If we are holding frames or the domain is not yet registered or
	 * there's already frames on the pending list,
	 * then add the new frame to pending list
	 */
	if (!domain || efc->hold_frames || !list_empty(&efc->pend_frames)) {
		unsigned long flags = 0;

		spin_lock_irqsave(&efc->pend_frames_lock, flags);
		INIT_LIST_HEAD(&seq->list_entry);
		list_add_tail(&seq->list_entry, &efc->pend_frames);
		spin_unlock_irqrestore(&efc->pend_frames_lock, flags);

		if (domain) {
			/* immediately process pending frames */
			efct_domain_process_pending(domain);
		}
	} else {
		/*
		 * We are not holding frames and pending list is empty,
		 * just process frame. A non-zero return means the frame
		 * was not handled - so cleanup
		 */
		if (efc_domain_dispatch_frame(domain, seq))
			efc->tt.hw_seq_free(efc, seq);
	}
}

int
efc_domain_dispatch_frame(void *arg, struct efc_hw_sequence *seq)
{
	struct efc_domain *domain = (struct efc_domain *)arg;
	struct efc *efc = domain->efc;
	struct fc_frame_header *hdr;
	struct efc_node *node = NULL;
	struct efc_nport *nport = NULL;
	unsigned long flags = 0;
	u32 s_id, d_id, rc = EFC_HW_SEQ_FREE;

	if (!seq->header || !seq->header->dma.virt || !seq->payload->dma.virt) {
		efc_log_err(efc, "Sequence header or payload is null\n");
		return rc;
	}

	hdr = seq->header->dma.virt;

	/* extract the s_id and d_id */
	s_id = ntoh24(hdr->fh_s_id);
	d_id = ntoh24(hdr->fh_d_id);

	spin_lock_irqsave(&efc->lock, flags);

	nport = efc_nport_find(domain, d_id);
	if (!nport) {
		if (hdr->fh_type == FC_TYPE_FCP) {
			/* Drop frame */
			efc_log_warn(efc, "FCP frame with invalid d_id x%x\n",
				     d_id);
			goto out;
		}

		/* p2p will use this case */
		nport = domain->nport;
		if (!nport || !kref_get_unless_zero(&nport->ref)) {
			efc_log_err(efc, "Physical nport is NULL\n");
			goto out;
		}
	}

	/* Lookup the node given the remote s_id */
	node = efc_node_find(nport, s_id);

	/* If not found, then create a new node */
	if (!node) {
		/*
		 * If this is solicited data or control based on R_CTL and
		 * there is no node context, then we can drop the frame
		 */
		if ((hdr->fh_r_ctl == FC_RCTL_DD_SOL_DATA) ||
		    (hdr->fh_r_ctl == FC_RCTL_DD_SOL_CTL)) {
			efc_log_debug(efc, "sol data/ctrl frame without node\n");
			goto out_release;
		}

		node = efc_node_alloc(nport, s_id, false, false);
		if (!node) {
			efc_log_err(efc, "efc_node_alloc() failed\n");
			goto out_release;
		}
		/* don't send PLOGI on efc_d_init entry */
		efc_node_init_device(node, false);
	}

	if (node->hold_frames || !list_empty(&node->pend_frames)) {
		/* add frame to node's pending list */
		spin_lock(&node->pend_frames_lock);
		INIT_LIST_HEAD(&seq->list_entry);
		list_add_tail(&seq->list_entry, &node->pend_frames);
		spin_unlock(&node->pend_frames_lock);
		rc = EFC_HW_SEQ_HOLD;
		goto out_release;
	}

	/* now dispatch frame to the node frame handler */
	efc_node_dispatch_frame(node, seq);

out_release:
	kref_put(&nport->ref, nport->release);
out:
	spin_unlock_irqrestore(&efc->lock, flags);
	return rc;
}

void
efc_node_dispatch_frame(void *arg, struct efc_hw_sequence *seq)
{
	struct fc_frame_header *hdr = seq->header->dma.virt;
	u32 port_id;
	struct efc_node *node = (struct efc_node *)arg;
	struct efc *efc = node->efc;

	port_id = ntoh24(hdr->fh_s_id);

	if (WARN_ON(port_id != node->rnode.fc_id))
		return;

	if ((!(ntoh24(hdr->fh_f_ctl) & FC_FC_END_SEQ)) ||
	    !(ntoh24(hdr->fh_f_ctl) & FC_FC_SEQ_INIT)) {
		node_printf(node,
			    "Drop frame hdr = %08x %08x %08x %08x %08x %08x\n",
			    cpu_to_be32(((u32 *)hdr)[0]),
			    cpu_to_be32(((u32 *)hdr)[1]),
			    cpu_to_be32(((u32 *)hdr)[2]),
			    cpu_to_be32(((u32 *)hdr)[3]),
			    cpu_to_be32(((u32 *)hdr)[4]),
			    cpu_to_be32(((u32 *)hdr)[5]));
		return;
	}

	switch (hdr->fh_r_ctl) {
	case FC_RCTL_ELS_REQ:
	case FC_RCTL_ELS_REP:
		efc_node_recv_els_frame(node, seq);
		break;

	case FC_RCTL_BA_ABTS:
	case FC_RCTL_BA_ACC:
	case FC_RCTL_BA_RJT:
	case FC_RCTL_BA_NOP:
		efc_log_err(efc, "Received ABTS:\n");
		break;

	case FC_RCTL_DD_UNSOL_CMD:
	case FC_RCTL_DD_UNSOL_CTL:
		switch (hdr->fh_type) {
		case FC_TYPE_FCP:
			if ((hdr->fh_r_ctl & 0xf) == FC_RCTL_DD_UNSOL_CMD) {
				if (!node->fcp_enabled) {
					efc_node_recv_fcp_cmd(node, seq);
					break;
				}
				efc_log_err(efc, "Recvd FCP CMD. Drop IO\n");
			} else if ((hdr->fh_r_ctl & 0xf) ==
							FC_RCTL_DD_SOL_DATA) {
				node_printf(node,
					    "solicited data recvd. Drop IO\n");
			}
			break;

		case FC_TYPE_CT:
			efc_node_recv_ct_frame(node, seq);
			break;
		default:
			break;
		}
		break;
	default:
		efc_log_err(efc, "Unhandled frame rctl: %02x\n", hdr->fh_r_ctl);
	}
}
