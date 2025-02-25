// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * NPORT
 *
 * Port object for physical port and NPIV ports.
 */

/*
 * NPORT REFERENCE COUNTING
 *
 * A nport reference should be taken when:
 * - an nport is allocated
 * - a vport populates associated nport
 * - a remote node is allocated
 * - a unsolicited frame is processed
 * The reference should be dropped when:
 * - the unsolicited frame processesing is done
 * - the remote node is removed
 * - the vport is removed
 * - the nport is removed
 */

#include "efc.h"

void
efc_nport_cb(void *arg, int event, void *data)
{
	struct efc *efc = arg;
	struct efc_nport *nport = data;
	unsigned long flags = 0;

	efc_log_debug(efc, "nport event: %s\n", efc_sm_event_name(event));

	spin_lock_irqsave(&efc->lock, flags);
	efc_sm_post_event(&nport->sm, event, NULL);
	spin_unlock_irqrestore(&efc->lock, flags);
}

static struct efc_nport *
efc_nport_find_wwn(struct efc_domain *domain, uint64_t wwnn, uint64_t wwpn)
{
	struct efc_nport *nport = NULL;

	/* Find a nport, given the WWNN and WWPN */
	list_for_each_entry(nport, &domain->nport_list, list_entry) {
		if (nport->wwnn == wwnn && nport->wwpn == wwpn)
			return nport;
	}
	return NULL;
}

static void
_efc_nport_free(struct kref *arg)
{
	struct efc_nport *nport = container_of(arg, struct efc_nport, ref);

	kfree(nport);
}

struct efc_nport *
efc_nport_alloc(struct efc_domain *domain, uint64_t wwpn, uint64_t wwnn,
		u32 fc_id, bool enable_ini, bool enable_tgt)
{
	struct efc_nport *nport;

	if (domain->efc->enable_ini)
		enable_ini = 0;

	/* Return a failure if this nport has already been allocated */
	if ((wwpn != 0) || (wwnn != 0)) {
		nport = efc_nport_find_wwn(domain, wwnn, wwpn);
		if (nport) {
			efc_log_err(domain->efc,
				    "NPORT %016llX %016llX already allocated\n",
				    wwnn, wwpn);
			return NULL;
		}
	}

	nport = kzalloc(sizeof(*nport), GFP_ATOMIC);
	if (!nport)
		return nport;

	/* initialize refcount */
	kref_init(&nport->ref);
	nport->release = _efc_nport_free;

	nport->efc = domain->efc;
	snprintf(nport->display_name, sizeof(nport->display_name), "------");
	nport->domain = domain;
	xa_init(&nport->lookup);
	nport->instance_index = domain->nport_count++;
	nport->sm.app = nport;
	nport->enable_ini = enable_ini;
	nport->enable_tgt = enable_tgt;
	nport->enable_rscn = (nport->enable_ini ||
			(nport->enable_tgt && enable_target_rscn(nport->efc)));

	/* Copy service parameters from domain */
	memcpy(nport->service_params, domain->service_params,
	       sizeof(struct fc_els_flogi));

	/* Update requested fc_id */
	nport->fc_id = fc_id;

	/* Update the nport's service parameters for the new wwn's */
	nport->wwpn = wwpn;
	nport->wwnn = wwnn;
	snprintf(nport->wwnn_str, sizeof(nport->wwnn_str), "%016llX",
		 (unsigned long long)wwnn);

	/*
	 * if this is the "first" nport of the domain,
	 * then make it the "phys" nport
	 */
	if (list_empty(&domain->nport_list))
		domain->nport = nport;

	INIT_LIST_HEAD(&nport->list_entry);
	list_add_tail(&nport->list_entry, &domain->nport_list);

	kref_get(&domain->ref);

	efc_log_debug(domain->efc, "New Nport [%s]\n", nport->display_name);

	return nport;
}

void
efc_nport_free(struct efc_nport *nport)
{
	struct efc_domain *domain;

	if (!nport)
		return;

	domain = nport->domain;
	efc_log_debug(domain->efc, "[%s] free nport\n", nport->display_name);
	list_del(&nport->list_entry);
	/*
	 * if this is the physical nport,
	 * then clear it out of the domain
	 */
	if (nport == domain->nport)
		domain->nport = NULL;

	xa_destroy(&nport->lookup);
	xa_erase(&domain->lookup, nport->fc_id);

	if (list_empty(&domain->nport_list))
		efc_domain_post_event(domain, EFC_EVT_ALL_CHILD_NODES_FREE,
				      NULL);

	kref_put(&domain->ref, domain->release);
	kref_put(&nport->ref, nport->release);
}

struct efc_nport *
efc_nport_find(struct efc_domain *domain, u32 d_id)
{
	struct efc_nport *nport;

	/* Find a nport object, given an FC_ID */
	nport = xa_load(&domain->lookup, d_id);
	if (!nport || !kref_get_unless_zero(&nport->ref))
		return NULL;

	return nport;
}

int
efc_nport_attach(struct efc_nport *nport, u32 fc_id)
{
	int rc;
	struct efc_node *node;
	struct efc *efc = nport->efc;
	unsigned long index;

	/* Set our lookup */
	rc = xa_err(xa_store(&nport->domain->lookup, fc_id, nport, GFP_ATOMIC));
	if (rc) {
		efc_log_err(efc, "Sport lookup store failed: %d\n", rc);
		return rc;
	}

	/* Update our display_name */
	efc_node_fcid_display(fc_id, nport->display_name,
			      sizeof(nport->display_name));

	xa_for_each(&nport->lookup, index, node) {
		efc_node_update_display_name(node);
	}

	efc_log_debug(nport->efc, "[%s] attach nport: fc_id x%06x\n",
		      nport->display_name, fc_id);

	/* Register a nport, given an FC_ID */
	rc = efc_cmd_nport_attach(efc, nport, fc_id);
	if (rc < 0) {
		efc_log_err(nport->efc,
			    "efc_hw_port_attach failed: %d\n", rc);
		return -EIO;
	}
	return 0;
}

static void
efc_nport_shutdown(struct efc_nport *nport)
{
	struct efc *efc = nport->efc;
	struct efc_node *node;
	unsigned long index;

	xa_for_each(&nport->lookup, index, node) {
		if (!(node->rnode.fc_id == FC_FID_FLOGI && nport->is_vport)) {
			efc_node_post_event(node, EFC_EVT_SHUTDOWN, NULL);
			continue;
		}

		/*
		 * If this is a vport, logout of the fabric
		 * controller so that it deletes the vport
		 * on the switch.
		 */
		/* if link is down, don't send logo */
		if (efc->link_status == EFC_LINK_STATUS_DOWN) {
			efc_node_post_event(node, EFC_EVT_SHUTDOWN, NULL);
			continue;
		}

		efc_log_debug(efc, "[%s] nport shutdown vport, send logo\n",
			      node->display_name);

		if (!efc_send_logo(node)) {
			/* sent LOGO, wait for response */
			efc_node_transition(node, __efc_d_wait_logo_rsp, NULL);
			continue;
		}

		/*
		 * failed to send LOGO,
		 * go ahead and cleanup node anyways
		 */
		node_printf(node, "Failed to send LOGO\n");
		efc_node_post_event(node, EFC_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
	}
}

static void
efc_vport_link_down(struct efc_nport *nport)
{
	struct efc *efc = nport->efc;
	struct efc_vport *vport;

	/* Clear the nport reference in the vport specification */
	list_for_each_entry(vport, &efc->vport_list, list_entry) {
		if (vport->nport == nport) {
			kref_put(&nport->ref, nport->release);
			vport->nport = NULL;
			break;
		}
	}
}

static void
__efc_nport_common(const char *funcname, struct efc_sm_ctx *ctx,
		   enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc_domain *domain = nport->domain;
	struct efc *efc = nport->efc;

	switch (evt) {
	case EFC_EVT_ENTER:
	case EFC_EVT_REENTER:
	case EFC_EVT_EXIT:
	case EFC_EVT_ALL_CHILD_NODES_FREE:
		break;
	case EFC_EVT_NPORT_ATTACH_OK:
			efc_sm_transition(ctx, __efc_nport_attached, NULL);
		break;
	case EFC_EVT_SHUTDOWN:
		/* Flag this nport as shutting down */
		nport->shutting_down = true;

		if (nport->is_vport)
			efc_vport_link_down(nport);

		if (xa_empty(&nport->lookup)) {
			/* Remove the nport from the domain's lookup table */
			xa_erase(&domain->lookup, nport->fc_id);
			efc_sm_transition(ctx, __efc_nport_wait_port_free,
					  NULL);
			if (efc_cmd_nport_free(efc, nport)) {
				efc_log_debug(nport->efc,
					      "efc_hw_port_free failed\n");
				/* Not much we can do, free the nport anyways */
				efc_nport_free(nport);
			}
		} else {
			/* sm: node list is not empty / shutdown nodes */
			efc_sm_transition(ctx,
					  __efc_nport_wait_shutdown, NULL);
			efc_nport_shutdown(nport);
		}
		break;
	default:
		efc_log_debug(nport->efc, "[%s] %-20s %-20s not handled\n",
			      nport->display_name, funcname,
			      efc_sm_event_name(evt));
	}
}

void
__efc_nport_allocated(struct efc_sm_ctx *ctx,
		      enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc_domain *domain = nport->domain;

	nport_sm_trace(nport);

	switch (evt) {
	/* the physical nport is attached */
	case EFC_EVT_NPORT_ATTACH_OK:
		WARN_ON(nport != domain->nport);
		efc_sm_transition(ctx, __efc_nport_attached, NULL);
		break;

	case EFC_EVT_NPORT_ALLOC_OK:
		/* ignore */
		break;
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

void
__efc_nport_vport_init(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc *efc = nport->efc;

	nport_sm_trace(nport);

	switch (evt) {
	case EFC_EVT_ENTER: {
		__be64 be_wwpn = cpu_to_be64(nport->wwpn);

		if (nport->wwpn == 0)
			efc_log_debug(efc, "vport: letting f/w select WWN\n");

		if (nport->fc_id != U32_MAX) {
			efc_log_debug(efc, "vport: hard coding port id: %x\n",
				      nport->fc_id);
		}

		efc_sm_transition(ctx, __efc_nport_vport_wait_alloc, NULL);
		/* If wwpn is zero, then we'll let the f/w assign wwpn*/
		if (efc_cmd_nport_alloc(efc, nport, nport->domain,
					nport->wwpn == 0 ? NULL :
					(uint8_t *)&be_wwpn)) {
			efc_log_err(efc, "Can't allocate port\n");
			break;
		}

		break;
	}
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

void
__efc_nport_vport_wait_alloc(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc *efc = nport->efc;

	nport_sm_trace(nport);

	switch (evt) {
	case EFC_EVT_NPORT_ALLOC_OK: {
		struct fc_els_flogi *sp;

		sp = (struct fc_els_flogi *)nport->service_params;

		if (nport->wwnn == 0) {
			nport->wwnn = be64_to_cpu(nport->sli_wwnn);
			nport->wwpn = be64_to_cpu(nport->sli_wwpn);
			snprintf(nport->wwnn_str, sizeof(nport->wwnn_str),
				 "%016llX", nport->wwpn);
		}

		/* Update the nport's service parameters */
		sp->fl_wwpn = cpu_to_be64(nport->wwpn);
		sp->fl_wwnn = cpu_to_be64(nport->wwnn);

		/*
		 * if nport->fc_id is uninitialized,
		 * then request that the fabric node use FDISC
		 * to find an fc_id.
		 * Otherwise we're restoring vports, or we're in
		 * fabric emulation mode, so attach the fc_id
		 */
		if (nport->fc_id == U32_MAX) {
			struct efc_node *fabric;

			fabric = efc_node_alloc(nport, FC_FID_FLOGI, false,
						false);
			if (!fabric) {
				efc_log_err(efc, "efc_node_alloc() failed\n");
				return;
			}
			efc_node_transition(fabric, __efc_vport_fabric_init,
					    NULL);
		} else {
			snprintf(nport->wwnn_str, sizeof(nport->wwnn_str),
				 "%016llX", nport->wwpn);
			efc_nport_attach(nport, nport->fc_id);
		}
		efc_sm_transition(ctx, __efc_nport_vport_allocated, NULL);
		break;
	}
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

void
__efc_nport_vport_allocated(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc *efc = nport->efc;

	nport_sm_trace(nport);

	/*
	 * This state is entered after the nport is allocated;
	 * it then waits for a fabric node
	 * FDISC to complete, which requests a nport attach.
	 * The nport attach complete is handled in this state.
	 */
	switch (evt) {
	case EFC_EVT_NPORT_ATTACH_OK: {
		struct efc_node *node;

		/* Find our fabric node, and forward this event */
		node = efc_node_find(nport, FC_FID_FLOGI);
		if (!node) {
			efc_log_debug(efc, "can't find node %06x\n", FC_FID_FLOGI);
			break;
		}
		/* sm: / forward nport attach to fabric node */
		efc_node_post_event(node, evt, NULL);
		efc_sm_transition(ctx, __efc_nport_attached, NULL);
		break;
	}
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

static void
efc_vport_update_spec(struct efc_nport *nport)
{
	struct efc *efc = nport->efc;
	struct efc_vport *vport;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->vport_lock, flags);
	list_for_each_entry(vport, &efc->vport_list, list_entry) {
		if (vport->nport == nport) {
			vport->wwnn = nport->wwnn;
			vport->wwpn = nport->wwpn;
			vport->tgt_data = nport->tgt_data;
			vport->ini_data = nport->ini_data;
			break;
		}
	}
	spin_unlock_irqrestore(&efc->vport_lock, flags);
}

void
__efc_nport_attached(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc *efc = nport->efc;

	nport_sm_trace(nport);

	switch (evt) {
	case EFC_EVT_ENTER: {
		struct efc_node *node;
		unsigned long index;

		efc_log_debug(efc,
			      "[%s] NPORT attached WWPN %016llX WWNN %016llX\n",
			      nport->display_name,
			      nport->wwpn, nport->wwnn);

		xa_for_each(&nport->lookup, index, node)
			efc_node_update_display_name(node);

		efc->tt.new_nport(efc, nport);

		/*
		 * Update the vport (if its not the physical nport)
		 * parameters
		 */
		if (nport->is_vport)
			efc_vport_update_spec(nport);
		break;
	}

	case EFC_EVT_EXIT:
		efc_log_debug(efc,
			      "[%s] NPORT deattached WWPN %016llX WWNN %016llX\n",
			      nport->display_name,
			      nport->wwpn, nport->wwnn);

		efc->tt.del_nport(efc, nport);
		break;
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

void
__efc_nport_wait_shutdown(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;
	struct efc_domain *domain = nport->domain;
	struct efc *efc = nport->efc;

	nport_sm_trace(nport);

	switch (evt) {
	case EFC_EVT_NPORT_ALLOC_OK:
	case EFC_EVT_NPORT_ALLOC_FAIL:
	case EFC_EVT_NPORT_ATTACH_OK:
	case EFC_EVT_NPORT_ATTACH_FAIL:
		/* ignore these events - just wait for the all free event */
		break;

	case EFC_EVT_ALL_CHILD_NODES_FREE: {
		/*
		 * Remove the nport from the domain's
		 * sparse vector lookup table
		 */
		xa_erase(&domain->lookup, nport->fc_id);
		efc_sm_transition(ctx, __efc_nport_wait_port_free, NULL);
		if (efc_cmd_nport_free(efc, nport)) {
			efc_log_err(nport->efc, "efc_hw_port_free failed\n");
			/* Not much we can do, free the nport anyways */
			efc_nport_free(nport);
		}
		break;
	}
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

void
__efc_nport_wait_port_free(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg)
{
	struct efc_nport *nport = ctx->app;

	nport_sm_trace(nport);

	switch (evt) {
	case EFC_EVT_NPORT_ATTACH_OK:
		/* Ignore as we are waiting for the free CB */
		break;
	case EFC_EVT_NPORT_FREE_OK: {
		/* All done, free myself */
		efc_nport_free(nport);
		break;
	}
	default:
		__efc_nport_common(__func__, ctx, evt, arg);
	}
}

static int
efc_vport_nport_alloc(struct efc_domain *domain, struct efc_vport *vport)
{
	struct efc_nport *nport;

	lockdep_assert_held(&domain->efc->lock);

	nport = efc_nport_alloc(domain, vport->wwpn, vport->wwnn, vport->fc_id,
				vport->enable_ini, vport->enable_tgt);
	vport->nport = nport;
	if (!nport)
		return -EIO;

	kref_get(&nport->ref);
	nport->is_vport = true;
	nport->tgt_data = vport->tgt_data;
	nport->ini_data = vport->ini_data;

	efc_sm_transition(&nport->sm, __efc_nport_vport_init, NULL);

	return 0;
}

int
efc_vport_start(struct efc_domain *domain)
{
	struct efc *efc = domain->efc;
	struct efc_vport *vport;
	struct efc_vport *next;
	int rc = 0;
	unsigned long flags = 0;

	/* Use the vport spec to find the associated vports and start them */
	spin_lock_irqsave(&efc->vport_lock, flags);
	list_for_each_entry_safe(vport, next, &efc->vport_list, list_entry) {
		if (!vport->nport) {
			if (efc_vport_nport_alloc(domain, vport))
				rc = -EIO;
		}
	}
	spin_unlock_irqrestore(&efc->vport_lock, flags);

	return rc;
}

int
efc_nport_vport_new(struct efc_domain *domain, uint64_t wwpn, uint64_t wwnn,
		    u32 fc_id, bool ini, bool tgt, void *tgt_data,
		    void *ini_data)
{
	struct efc *efc = domain->efc;
	struct efc_vport *vport;
	int rc = 0;
	unsigned long flags = 0;

	if (ini && domain->efc->enable_ini == 0) {
		efc_log_debug(efc, "driver initiator mode not enabled\n");
		return -EIO;
	}

	if (tgt && domain->efc->enable_tgt == 0) {
		efc_log_debug(efc, "driver target mode not enabled\n");
		return -EIO;
	}

	/*
	 * Create a vport spec if we need to recreate
	 * this vport after a link up event
	 */
	vport = efc_vport_create_spec(domain->efc, wwnn, wwpn, fc_id, ini, tgt,
				      tgt_data, ini_data);
	if (!vport) {
		efc_log_err(efc, "failed to create vport object entry\n");
		return -EIO;
	}

	spin_lock_irqsave(&efc->lock, flags);
	rc = efc_vport_nport_alloc(domain, vport);
	spin_unlock_irqrestore(&efc->lock, flags);

	return rc;
}

int
efc_nport_vport_del(struct efc *efc, struct efc_domain *domain,
		    u64 wwpn, uint64_t wwnn)
{
	struct efc_nport *nport;
	struct efc_vport *vport;
	struct efc_vport *next;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->vport_lock, flags);
	/* walk the efc_vport_list and remove from there */
	list_for_each_entry_safe(vport, next, &efc->vport_list, list_entry) {
		if (vport->wwpn == wwpn && vport->wwnn == wwnn) {
			list_del(&vport->list_entry);
			kfree(vport);
			break;
		}
	}
	spin_unlock_irqrestore(&efc->vport_lock, flags);

	if (!domain) {
		/* No domain means no nport to look for */
		return 0;
	}

	spin_lock_irqsave(&efc->lock, flags);
	list_for_each_entry(nport, &domain->nport_list, list_entry) {
		if (nport->wwpn == wwpn && nport->wwnn == wwnn) {
			/* Shutdown this NPORT */
			efc_sm_post_event(&nport->sm, EFC_EVT_SHUTDOWN, NULL);
			kref_put(&nport->ref, nport->release);
			break;
		}
	}

	spin_unlock_irqrestore(&efc->lock, flags);
	return 0;
}

void
efc_vport_del_all(struct efc *efc)
{
	struct efc_vport *vport;
	struct efc_vport *next;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->vport_lock, flags);
	list_for_each_entry_safe(vport, next, &efc->vport_list, list_entry) {
		list_del(&vport->list_entry);
		kfree(vport);
	}
	spin_unlock_irqrestore(&efc->vport_lock, flags);
}

struct efc_vport *
efc_vport_create_spec(struct efc *efc, uint64_t wwnn, uint64_t wwpn,
		      u32 fc_id, bool enable_ini,
		      bool enable_tgt, void *tgt_data, void *ini_data)
{
	struct efc_vport *vport;
	unsigned long flags = 0;

	/*
	 * walk the efc_vport_list and return failure
	 * if a valid(vport with non zero WWPN and WWNN) vport entry
	 * is already created
	 */
	spin_lock_irqsave(&efc->vport_lock, flags);
	list_for_each_entry(vport, &efc->vport_list, list_entry) {
		if ((wwpn && vport->wwpn == wwpn) &&
		    (wwnn && vport->wwnn == wwnn)) {
			efc_log_err(efc,
				    "VPORT %016llX %016llX already allocated\n",
				    wwnn, wwpn);
			spin_unlock_irqrestore(&efc->vport_lock, flags);
			return NULL;
		}
	}

	vport = kzalloc(sizeof(*vport), GFP_ATOMIC);
	if (!vport) {
		spin_unlock_irqrestore(&efc->vport_lock, flags);
		return NULL;
	}

	vport->wwnn = wwnn;
	vport->wwpn = wwpn;
	vport->fc_id = fc_id;
	vport->enable_tgt = enable_tgt;
	vport->enable_ini = enable_ini;
	vport->tgt_data = tgt_data;
	vport->ini_data = ini_data;

	INIT_LIST_HEAD(&vport->list_entry);
	list_add_tail(&vport->list_entry, &efc->vport_list);
	spin_unlock_irqrestore(&efc->vport_lock, flags);
	return vport;
}
