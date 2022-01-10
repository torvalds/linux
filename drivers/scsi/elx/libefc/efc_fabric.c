// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * This file implements remote node state machines for:
 * - Fabric logins.
 * - Fabric controller events.
 * - Name/directory services interaction.
 * - Point-to-point logins.
 */

/*
 * fabric_sm Node State Machine: Fabric States
 * ns_sm Node State Machine: Name/Directory Services States
 * p2p_sm Node State Machine: Point-to-Point Node States
 */

#include "efc.h"

static void
efc_fabric_initiate_shutdown(struct efc_node *node)
{
	struct efc *efc = node->efc;

	node->els_io_enabled = false;

	if (node->attached) {
		int rc;

		/* issue hw node free; don't care if succeeds right away
		 * or sometime later, will check node->attached later in
		 * shutdown process
		 */
		rc = efc_cmd_node_detach(efc, &node->rnode);
		if (rc < 0) {
			node_printf(node, "Failed freeing HW node, rc=%d\n",
				    rc);
		}
	}
	/*
	 * node has either been detached or is in the process of being detached,
	 * call common node's initiate cleanup function
	 */
	efc_node_initiate_cleanup(node);
}

static void
__efc_fabric_common(const char *funcname, struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = NULL;

	node = ctx->app;

	switch (evt) {
	case EFC_EVT_DOMAIN_ATTACH_OK:
		break;
	case EFC_EVT_SHUTDOWN:
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(node);
		break;

	default:
		/* call default event handler common to all nodes */
		__efc_node_common(funcname, ctx, evt, arg);
	}
}

void
__efc_fabric_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		  void *arg)
{
	struct efc_node *node = ctx->app;
	struct efc *efc = node->efc;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_REENTER:
		efc_log_debug(efc, ">>> reenter !!\n");
		fallthrough;

	case EFC_EVT_ENTER:
		/* send FLOGI */
		efc_send_flogi(node);
		efc_node_transition(node, __efc_fabric_flogi_wait_rsp, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
efc_fabric_set_topology(struct efc_node *node,
			enum efc_nport_topology topology)
{
	node->nport->topology = topology;
}

void
efc_fabric_notify_topology(struct efc_node *node)
{
	struct efc_node *tmp_node;
	unsigned long index;

	/*
	 * now loop through the nodes in the nport
	 * and send topology notification
	 */
	xa_for_each(&node->nport->lookup, index, tmp_node) {
		if (tmp_node != node) {
			efc_node_post_event(tmp_node,
					    EFC_EVT_NPORT_TOPOLOGY_NOTIFY,
					    &node->nport->topology);
		}
	}
}

static bool efc_rnode_is_nport(struct fc_els_flogi *rsp)
{
	return !(ntohs(rsp->fl_csp.sp_features) & FC_SP_FT_FPORT);
}

void
__efc_fabric_flogi_wait_rsp(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		if (efc_node_check_els_req(ctx, evt, arg, ELS_FLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;

		memcpy(node->nport->domain->flogi_service_params,
		       cbdata->els_rsp.virt,
		       sizeof(struct fc_els_flogi));

		/* Check to see if the fabric is an F_PORT or and N_PORT */
		if (!efc_rnode_is_nport(cbdata->els_rsp.virt)) {
			/* sm: if not nport / efc_domain_attach */
			/* ext_status has the fc_id, attach domain */
			efc_fabric_set_topology(node, EFC_NPORT_TOPO_FABRIC);
			efc_fabric_notify_topology(node);
			WARN_ON(node->nport->domain->attached);
			efc_domain_attach(node->nport->domain,
					  cbdata->ext_status);
			efc_node_transition(node,
					    __efc_fabric_wait_domain_attach,
					    NULL);
			break;
		}

		/*  sm: if nport and p2p_winner / efc_domain_attach */
		efc_fabric_set_topology(node, EFC_NPORT_TOPO_P2P);
		if (efc_p2p_setup(node->nport)) {
			node_printf(node,
				    "p2p setup failed, shutting down node\n");
			node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
			efc_fabric_initiate_shutdown(node);
			break;
		}

		if (node->nport->p2p_winner) {
			efc_node_transition(node,
					    __efc_p2p_wait_domain_attach,
					     NULL);
			if (node->nport->domain->attached &&
			    !node->nport->domain->domain_notify_pend) {
				/*
				 * already attached,
				 * just send ATTACH_OK
				 */
				node_printf(node,
					    "p2p winner, domain already attached\n");
				efc_node_post_event(node,
						    EFC_EVT_DOMAIN_ATTACH_OK,
						    NULL);
			}
		} else {
			/*
			 * peer is p2p winner;
			 * PLOGI will be received on the
			 * remote SID=1 node;
			 * this node has served its purpose
			 */
			node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
			efc_fabric_initiate_shutdown(node);
		}

		break;
	}

	case EFC_EVT_ELS_REQ_ABORTED:
	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_SRRS_ELS_REQ_FAIL: {
		struct efc_nport *nport = node->nport;
		/*
		 * with these errors, we have no recovery,
		 * so shutdown the nport, leave the link
		 * up and the domain ready
		 */
		if (efc_node_check_els_req(ctx, evt, arg, ELS_FLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		node_printf(node,
			    "FLOGI failed evt=%s, shutting down nport [%s]\n",
			    efc_sm_event_name(evt), nport->display_name);
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		efc_sm_post_event(&nport->sm, EFC_EVT_SHUTDOWN, NULL);
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_vport_fabric_init(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* sm: / send FDISC */
		efc_send_fdisc(node);
		efc_node_transition(node, __efc_fabric_fdisc_wait_rsp, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabric_fdisc_wait_rsp(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		/* fc_id is in ext_status */
		if (efc_node_check_els_req(ctx, evt, arg, ELS_FDISC,
					   __efc_fabric_common, __func__)) {
			return;
		}

		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		/* sm: / efc_nport_attach */
		efc_nport_attach(node->nport, cbdata->ext_status);
		efc_node_transition(node, __efc_fabric_wait_domain_attach,
				    NULL);
		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_SRRS_ELS_REQ_FAIL: {
		if (efc_node_check_els_req(ctx, evt, arg, ELS_FDISC,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		efc_log_err(node->efc, "FDISC failed, shutting down nport\n");
		/* sm: / shutdown nport */
		efc_sm_post_event(&node->nport->sm, EFC_EVT_SHUTDOWN, NULL);
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static int
efc_start_ns_node(struct efc_nport *nport)
{
	struct efc_node *ns;

	/* Instantiate a name services node */
	ns = efc_node_find(nport, FC_FID_DIR_SERV);
	if (!ns) {
		ns = efc_node_alloc(nport, FC_FID_DIR_SERV, false, false);
		if (!ns)
			return -EIO;
	}
	/*
	 * for found ns, should we be transitioning from here?
	 * breaks transition only
	 *  1. from within state machine or
	 *  2. if after alloc
	 */
	if (ns->efc->nodedb_mask & EFC_NODEDB_PAUSE_NAMESERVER)
		efc_node_pause(ns, __efc_ns_init);
	else
		efc_node_transition(ns, __efc_ns_init, NULL);
	return 0;
}

static int
efc_start_fabctl_node(struct efc_nport *nport)
{
	struct efc_node *fabctl;

	fabctl = efc_node_find(nport, FC_FID_FCTRL);
	if (!fabctl) {
		fabctl = efc_node_alloc(nport, FC_FID_FCTRL,
					false, false);
		if (!fabctl)
			return -EIO;
	}
	/*
	 * for found ns, should we be transitioning from here?
	 * breaks transition only
	 *  1. from within state machine or
	 *  2. if after alloc
	 */
	efc_node_transition(fabctl, __efc_fabctl_init, NULL);
	return 0;
}

void
__efc_fabric_wait_domain_attach(struct efc_sm_ctx *ctx,
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
	case EFC_EVT_DOMAIN_ATTACH_OK:
	case EFC_EVT_NPORT_ATTACH_OK: {
		int rc;

		rc = efc_start_ns_node(node->nport);
		if (rc)
			return;

		/* sm: if enable_ini / start fabctl node */
		/* Instantiate the fabric controller (sends SCR) */
		if (node->nport->enable_rscn) {
			rc = efc_start_fabctl_node(node->nport);
			if (rc)
				return;
		}
		efc_node_transition(node, __efc_fabric_idle, NULL);
		break;
	}
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabric_idle(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		  void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_DOMAIN_ATTACH_OK:
		break;
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* sm: / send PLOGI */
		efc_send_plogi(node);
		efc_node_transition(node, __efc_ns_plogi_wait_rsp, NULL);
		break;
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_plogi_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		int rc;

		/* Save service parameters */
		if (efc_node_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		/* sm: / save sparams, efc_node_attach */
		efc_node_save_sparms(node, cbdata->els_rsp.virt);
		rc = efc_node_attach(node);
		efc_node_transition(node, __efc_ns_wait_node_attach, NULL);
		if (rc < 0)
			efc_node_post_event(node, EFC_EVT_NODE_ATTACH_FAIL,
					    NULL);
		break;
	}
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_wait_node_attach(struct efc_sm_ctx *ctx,
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

	case EFC_EVT_NODE_ATTACH_OK:
		node->attached = true;
		/* sm: / send RFTID */
		efc_ns_send_rftid(node);
		efc_node_transition(node, __efc_ns_rftid_wait_rsp, NULL);
		break;

	case EFC_EVT_NODE_ATTACH_FAIL:
		/* node attach failed, shutdown the node */
		node->attached = false;
		node_printf(node, "Node attach failed\n");
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(node);
		break;

	case EFC_EVT_SHUTDOWN:
		node_printf(node, "Shutdown event received\n");
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_node_transition(node,
				    __efc_fabric_wait_attach_evt_shutdown,
				     NULL);
		break;

	/*
	 * if receive RSCN just ignore,
	 * we haven't sent GID_PT yet (ACC sent by fabctl node)
	 */
	case EFC_EVT_RSCN_RCVD:
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabric_wait_attach_evt_shutdown(struct efc_sm_ctx *ctx,
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

	/* wait for any of these attach events and then shutdown */
	case EFC_EVT_NODE_ATTACH_OK:
		node->attached = true;
		node_printf(node, "Attach evt=%s, proceed to shutdown\n",
			    efc_sm_event_name(evt));
		efc_fabric_initiate_shutdown(node);
		break;

	case EFC_EVT_NODE_ATTACH_FAIL:
		node->attached = false;
		node_printf(node, "Attach evt=%s, proceed to shutdown\n",
			    efc_sm_event_name(evt));
		efc_fabric_initiate_shutdown(node);
		break;

	/* ignore shutdown event as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		node_printf(node, "Shutdown event received\n");
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_rftid_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:
		if (efc_node_check_ns_req(ctx, evt, arg, FC_NS_RFT_ID,
					  __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		/* sm: / send RFFID */
		efc_ns_send_rffid(node);
		efc_node_transition(node, __efc_ns_rffid_wait_rsp, NULL);
		break;

	/*
	 * if receive RSCN just ignore,
	 * we haven't sent GID_PT yet (ACC sent by fabctl node)
	 */
	case EFC_EVT_RSCN_RCVD:
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_rffid_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	/*
	 * Waits for an RFFID response event;
	 * if rscn enabled, a GIDPT name services request is issued.
	 */
	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:	{
		if (efc_node_check_ns_req(ctx, evt, arg, FC_NS_RFF_ID,
					  __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		if (node->nport->enable_rscn) {
			/* sm: if enable_rscn / send GIDPT */
			efc_ns_send_gidpt(node);

			efc_node_transition(node, __efc_ns_gidpt_wait_rsp,
					    NULL);
		} else {
			/* if 'T' only, we're done, go to idle */
			efc_node_transition(node, __efc_ns_idle, NULL);
		}
		break;
	}
	/*
	 * if receive RSCN just ignore,
	 * we haven't sent GID_PT yet (ACC sent by fabctl node)
	 */
	case EFC_EVT_RSCN_RCVD:
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static int
efc_process_gidpt_payload(struct efc_node *node,
			  void *data, u32 gidpt_len)
{
	u32 i, j;
	struct efc_node *newnode;
	struct efc_nport *nport = node->nport;
	struct efc *efc = node->efc;
	u32 port_id = 0, port_count, plist_count;
	struct efc_node *n;
	struct efc_node **active_nodes;
	int residual;
	struct {
		struct fc_ct_hdr hdr;
		struct fc_gid_pn_resp pn_rsp;
	} *rsp;
	struct fc_gid_pn_resp *gidpt;
	unsigned long index;

	rsp = data;
	gidpt = &rsp->pn_rsp;
	residual = be16_to_cpu(rsp->hdr.ct_mr_size);

	if (residual != 0)
		efc_log_debug(node->efc, "residual is %u words\n", residual);

	if (be16_to_cpu(rsp->hdr.ct_cmd) == FC_FS_RJT) {
		node_printf(node,
			    "GIDPT request failed: rsn x%x rsn_expl x%x\n",
			    rsp->hdr.ct_reason, rsp->hdr.ct_explan);
		return -EIO;
	}

	plist_count = (gidpt_len - sizeof(struct fc_ct_hdr)) / sizeof(*gidpt);

	/* Count the number of nodes */
	port_count = 0;
	xa_for_each(&nport->lookup, index, n) {
		port_count++;
	}

	/* Allocate a buffer for all nodes */
	active_nodes = kcalloc(port_count, sizeof(*active_nodes), GFP_ATOMIC);
	if (!active_nodes) {
		node_printf(node, "efc_malloc failed\n");
		return -EIO;
	}

	/* Fill buffer with fc_id of active nodes */
	i = 0;
	xa_for_each(&nport->lookup, index, n) {
		port_id = n->rnode.fc_id;
		switch (port_id) {
		case FC_FID_FLOGI:
		case FC_FID_FCTRL:
		case FC_FID_DIR_SERV:
			break;
		default:
			if (port_id != FC_FID_DOM_MGR)
				active_nodes[i++] = n;
			break;
		}
	}

	/* update the active nodes buffer */
	for (i = 0; i < plist_count; i++) {
		hton24(gidpt[i].fp_fid, port_id);

		for (j = 0; j < port_count; j++) {
			if (active_nodes[j] &&
			    port_id == active_nodes[j]->rnode.fc_id) {
				active_nodes[j] = NULL;
			}
		}

		if (gidpt[i].fp_resvd & FC_NS_FID_LAST)
			break;
	}

	/* Those remaining in the active_nodes[] are now gone ! */
	for (i = 0; i < port_count; i++) {
		/*
		 * if we're an initiator and the remote node
		 * is a target, then post the node missing event.
		 * if we're target and we have enabled
		 * target RSCN, then post the node missing event.
		 */
		if (!active_nodes[i])
			continue;

		if ((node->nport->enable_ini && active_nodes[i]->targ) ||
		    (node->nport->enable_tgt && enable_target_rscn(efc))) {
			efc_node_post_event(active_nodes[i],
					    EFC_EVT_NODE_MISSING, NULL);
		} else {
			node_printf(node,
				    "GID_PT: skipping non-tgt port_id x%06x\n",
				    active_nodes[i]->rnode.fc_id);
		}
	}
	kfree(active_nodes);

	for (i = 0; i < plist_count; i++) {
		hton24(gidpt[i].fp_fid, port_id);

		/* Don't create node for ourselves */
		if (port_id == node->rnode.nport->fc_id) {
			if (gidpt[i].fp_resvd & FC_NS_FID_LAST)
				break;
			continue;
		}

		newnode = efc_node_find(nport, port_id);
		if (!newnode) {
			if (!node->nport->enable_ini)
				continue;

			newnode = efc_node_alloc(nport, port_id, false, false);
			if (!newnode) {
				efc_log_err(efc, "efc_node_alloc() failed\n");
				return -EIO;
			}
			/*
			 * send PLOGI automatically
			 * if initiator
			 */
			efc_node_init_device(newnode, true);
		}

		if (node->nport->enable_ini && newnode->targ) {
			efc_node_post_event(newnode, EFC_EVT_NODE_REFOUND,
					    NULL);
		}

		if (gidpt[i].fp_resvd & FC_NS_FID_LAST)
			break;
	}
	return 0;
}

void
__efc_ns_gidpt_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();
	/*
	 * Wait for a GIDPT response from the name server. Process the FC_IDs
	 * that are reported by creating new remote ports, as needed.
	 */

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:	{
		if (efc_node_check_ns_req(ctx, evt, arg, FC_NS_GID_PT,
					  __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		/* sm: / process GIDPT payload */
		efc_process_gidpt_payload(node, cbdata->els_rsp.virt,
					  cbdata->els_rsp.len);
		efc_node_transition(node, __efc_ns_idle, NULL);
		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_FAIL:	{
		/* not much we can do; will retry with the next RSCN */
		node_printf(node, "GID_PT failed to complete\n");
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		efc_node_transition(node, __efc_ns_idle, NULL);
		break;
	}

	/* if receive RSCN here, queue up another discovery processing */
	case EFC_EVT_RSCN_RCVD: {
		node_printf(node, "RSCN received during GID_PT processing\n");
		node->rscn_pending = true;
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_idle(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;
	struct efc *efc = node->efc;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	/*
	 * Wait for RSCN received events (posted from the fabric controller)
	 * and restart the GIDPT name services query and processing.
	 */

	switch (evt) {
	case EFC_EVT_ENTER:
		if (!node->rscn_pending)
			break;

		node_printf(node, "RSCN pending, restart discovery\n");
		node->rscn_pending = false;
		fallthrough;

	case EFC_EVT_RSCN_RCVD: {
		/* sm: / send GIDPT */
		/*
		 * If target RSCN processing is enabled,
		 * and this is target only (not initiator),
		 * and tgt_rscn_delay is non-zero,
		 * then we delay issuing the GID_PT
		 */
		if (efc->tgt_rscn_delay_msec != 0 &&
		    !node->nport->enable_ini && node->nport->enable_tgt &&
		    enable_target_rscn(efc)) {
			efc_node_transition(node, __efc_ns_gidpt_delay, NULL);
		} else {
			efc_ns_send_gidpt(node);
			efc_node_transition(node, __efc_ns_gidpt_wait_rsp,
					    NULL);
		}
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static void
gidpt_delay_timer_cb(struct timer_list *t)
{
	struct efc_node *node = from_timer(node, t, gidpt_delay_timer);

	del_timer(&node->gidpt_delay_timer);

	efc_node_post_event(node, EFC_EVT_GIDPT_DELAY_EXPIRED, NULL);
}

void
__efc_ns_gidpt_delay(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;
	struct efc *efc = node->efc;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER: {
		u64 delay_msec, tmp;

		/*
		 * Compute the delay time.
		 * Set to tgt_rscn_delay, if the time since last GIDPT
		 * is less than tgt_rscn_period, then use tgt_rscn_period.
		 */
		delay_msec = efc->tgt_rscn_delay_msec;
		tmp = jiffies_to_msecs(jiffies) - node->time_last_gidpt_msec;
		if (tmp < efc->tgt_rscn_period_msec)
			delay_msec = efc->tgt_rscn_period_msec;

		timer_setup(&node->gidpt_delay_timer, &gidpt_delay_timer_cb,
			    0);
		mod_timer(&node->gidpt_delay_timer,
			  jiffies + msecs_to_jiffies(delay_msec));

		break;
	}

	case EFC_EVT_GIDPT_DELAY_EXPIRED:
		node->time_last_gidpt_msec = jiffies_to_msecs(jiffies);

		efc_ns_send_gidpt(node);
		efc_node_transition(node, __efc_ns_gidpt_wait_rsp, NULL);
		break;

	case EFC_EVT_RSCN_RCVD: {
		efc_log_debug(efc,
			      "RSCN received while in GIDPT delay - no action\n");
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabctl_init(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* no need to login to fabric controller, just send SCR */
		efc_send_scr(node);
		efc_node_transition(node, __efc_fabctl_wait_scr_rsp, NULL);
		break;

	case EFC_EVT_NODE_ATTACH_OK:
		node->attached = true;
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabctl_wait_scr_rsp(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	/*
	 * Fabric controller node state machine:
	 * Wait for an SCR response from the fabric controller.
	 */
	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:
		if (efc_node_check_els_req(ctx, evt, arg, ELS_SCR,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		efc_node_transition(node, __efc_fabctl_ready, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static void
efc_process_rscn(struct efc_node *node, struct efc_node_cb *cbdata)
{
	struct efc *efc = node->efc;
	struct efc_nport *nport = node->nport;
	struct efc_node *ns;

	/* Forward this event to the name-services node */
	ns = efc_node_find(nport, FC_FID_DIR_SERV);
	if (ns)
		efc_node_post_event(ns, EFC_EVT_RSCN_RCVD, cbdata);
	else
		efc_log_warn(efc, "can't find name server node\n");
}

void
__efc_fabctl_ready(struct efc_sm_ctx *ctx,
		   enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	/*
	 * Fabric controller node state machine: Ready.
	 * In this state, the fabric controller sends a RSCN, which is received
	 * by this node and is forwarded to the name services node object; and
	 * the RSCN LS_ACC is sent.
	 */
	switch (evt) {
	case EFC_EVT_RSCN_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		/*
		 * sm: / process RSCN (forward to name services node),
		 * send LS_ACC
		 */
		efc_process_rscn(node, cbdata);
		efc_send_ls_acc(node, be16_to_cpu(hdr->fh_ox_id));
		efc_node_transition(node, __efc_fabctl_wait_ls_acc_cmpl,
				    NULL);
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabctl_wait_ls_acc_cmpl(struct efc_sm_ctx *ctx,
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

	case EFC_EVT_SRRS_ELS_CMPL_OK:
		WARN_ON(!node->els_cmpl_cnt);
		node->els_cmpl_cnt--;
		efc_node_transition(node, __efc_fabctl_ready, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static uint64_t
efc_get_wwpn(struct fc_els_flogi *sp)
{
	return be64_to_cpu(sp->fl_wwnn);
}

static int
efc_rnode_is_winner(struct efc_nport *nport)
{
	struct fc_els_flogi *remote_sp;
	u64 remote_wwpn;
	u64 local_wwpn = nport->wwpn;
	u64 wwn_bump = 0;

	remote_sp = (struct fc_els_flogi *)nport->domain->flogi_service_params;
	remote_wwpn = efc_get_wwpn(remote_sp);

	local_wwpn ^= wwn_bump;

	efc_log_debug(nport->efc, "r: %llx\n",
		      be64_to_cpu(remote_sp->fl_wwpn));
	efc_log_debug(nport->efc, "l: %llx\n", local_wwpn);

	if (remote_wwpn == local_wwpn) {
		efc_log_warn(nport->efc,
			     "WWPN of remote node [%08x %08x] matches local WWPN\n",
			     (u32)(local_wwpn >> 32ll),
			     (u32)local_wwpn);
		return -1;
	}

	return (remote_wwpn > local_wwpn);
}

void
__efc_p2p_wait_domain_attach(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	struct efc_node *node = ctx->app;
	struct efc *efc = node->efc;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_node_hold_frames(node);
		break;

	case EFC_EVT_EXIT:
		efc_node_accept_frames(node);
		break;

	case EFC_EVT_DOMAIN_ATTACH_OK: {
		struct efc_nport *nport = node->nport;
		struct efc_node *rnode;

		/*
		 * this transient node (SID=0 (recv'd FLOGI)
		 * or DID=fabric (sent FLOGI))
		 * is the p2p winner, will use a separate node
		 * to send PLOGI to peer
		 */
		WARN_ON(!node->nport->p2p_winner);

		rnode = efc_node_find(nport, node->nport->p2p_remote_port_id);
		if (rnode) {
			/*
			 * the "other" transient p2p node has
			 * already kicked off the
			 * new node from which PLOGI is sent
			 */
			node_printf(node,
				    "Node with fc_id x%x already exists\n",
				    rnode->rnode.fc_id);
		} else {
			/*
			 * create new node (SID=1, DID=2)
			 * from which to send PLOGI
			 */
			rnode = efc_node_alloc(nport,
					       nport->p2p_remote_port_id,
						false, false);
			if (!rnode) {
				efc_log_err(efc, "node alloc failed\n");
				return;
			}

			efc_fabric_notify_topology(node);
			/* sm: / allocate p2p remote node */
			efc_node_transition(rnode, __efc_p2p_rnode_init,
					    NULL);
		}

		/*
		 * the transient node (SID=0 or DID=fabric)
		 * has served its purpose
		 */
		if (node->rnode.fc_id == 0) {
			/*
			 * if this is the SID=0 node,
			 * move to the init state in case peer
			 * has restarted FLOGI discovery and FLOGI is pending
			 */
			/* don't send PLOGI on efc_d_init entry */
			efc_node_init_device(node, false);
		} else {
			/*
			 * if this is the DID=fabric node
			 * (we initiated FLOGI), shut it down
			 */
			node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
			efc_fabric_initiate_shutdown(node);
		}
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_rnode_init(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* sm: / send PLOGI */
		efc_send_plogi(node);
		efc_node_transition(node, __efc_p2p_wait_plogi_rsp, NULL);
		break;

	case EFC_EVT_ABTS_RCVD:
		/* sm: send BA_ACC */
		efc_send_bls_acc(node, cbdata->header->dma.virt);

		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_wait_flogi_acc_cmpl(struct efc_sm_ctx *ctx,
			      enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
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

	case EFC_EVT_SRRS_ELS_CMPL_OK:
		WARN_ON(!node->els_cmpl_cnt);
		node->els_cmpl_cnt--;

		/* sm: if p2p_winner / domain_attach */
		if (node->nport->p2p_winner) {
			efc_node_transition(node,
					    __efc_p2p_wait_domain_attach,
					NULL);
			if (!node->nport->domain->attached) {
				node_printf(node, "Domain not attached\n");
				efc_domain_attach(node->nport->domain,
						  node->nport->p2p_port_id);
			} else {
				node_printf(node, "Domain already attached\n");
				efc_node_post_event(node,
						    EFC_EVT_DOMAIN_ATTACH_OK,
						    NULL);
			}
		} else {
			/* this node has served its purpose;
			 * we'll expect a PLOGI on a separate
			 * node (remote SID=0x1); return this node
			 * to init state in case peer
			 * restarts discovery -- it may already
			 * have (pending frames may exist).
			 */
			/* don't send PLOGI on efc_d_init entry */
			efc_node_init_device(node, false);
		}
		break;

	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		/*
		 * LS_ACC failed, possibly due to link down;
		 * shutdown node and wait
		 * for FLOGI discovery to restart
		 */
		node_printf(node, "FLOGI LS_ACC failed, shutting down\n");
		WARN_ON(!node->els_cmpl_cnt);
		node->els_cmpl_cnt--;
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(node);
		break;

	case EFC_EVT_ABTS_RCVD: {
		/* sm: / send BA_ACC */
		efc_send_bls_acc(node, cbdata->header->dma.virt);
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_wait_plogi_rsp(struct efc_sm_ctx *ctx,
			 enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		int rc;

		if (efc_node_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		/* sm: / save sparams, efc_node_attach */
		efc_node_save_sparms(node, cbdata->els_rsp.virt);
		rc = efc_node_attach(node);
		efc_node_transition(node, __efc_p2p_wait_node_attach, NULL);
		if (rc < 0)
			efc_node_post_event(node, EFC_EVT_NODE_ATTACH_FAIL,
					    NULL);
		break;
	}
	case EFC_EVT_SRRS_ELS_REQ_FAIL: {
		if (efc_node_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		node_printf(node, "PLOGI failed, shutting down\n");
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(node);
		break;
	}

	case EFC_EVT_PLOGI_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		/* if we're in external loopback mode, just send LS_ACC */
		if (node->efc->external_loopback) {
			efc_send_plogi_acc(node, be16_to_cpu(hdr->fh_ox_id));
		} else {
			/*
			 * if this isn't external loopback,
			 * pass to default handler
			 */
			__efc_fabric_common(__func__, ctx, evt, arg);
		}
		break;
	}
	case EFC_EVT_PRLI_RCVD:
		/* I, or I+T */
		/* sent PLOGI and before completion was seen, received the
		 * PRLI from the remote node (WCQEs and RCQEs come in on
		 * different queues and order of processing cannot be assumed)
		 * Save OXID so PRLI can be sent after the attach and continue
		 * to wait for PLOGI response
		 */
		efc_process_prli_payload(node, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(node,
					     cbdata->header->dma.virt,
					     EFC_NODE_SEND_LS_ACC_PRLI);
		efc_node_transition(node, __efc_p2p_wait_plogi_rsp_recvd_prli,
				    NULL);
		break;
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_wait_plogi_rsp_recvd_prli(struct efc_sm_ctx *ctx,
				    enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
	struct efc_node *node = ctx->app;

	efc_node_evt_set(ctx, evt, __func__);

	node_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/*
		 * Since we've received a PRLI, we have a port login and will
		 * just need to wait for the PLOGI response to do the node
		 * attach and then we can send the LS_ACC for the PRLI. If,
		 * during this time, we receive FCP_CMNDs (which is possible
		 * since we've already sent a PRLI and our peer may have
		 * accepted).
		 * At this time, we are not waiting on any other unsolicited
		 * frames to continue with the login process. Thus, it will not
		 * hurt to hold frames here.
		 */
		efc_node_hold_frames(node);
		break;

	case EFC_EVT_EXIT:
		efc_node_accept_frames(node);
		break;

	case EFC_EVT_SRRS_ELS_REQ_OK: {	/* PLOGI response received */
		int rc;

		/* Completion from PLOGI sent */
		if (efc_node_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		/* sm: / save sparams, efc_node_attach */
		efc_node_save_sparms(node, cbdata->els_rsp.virt);
		rc = efc_node_attach(node);
		efc_node_transition(node, __efc_p2p_wait_node_attach, NULL);
		if (rc < 0)
			efc_node_post_event(node, EFC_EVT_NODE_ATTACH_FAIL,
					    NULL);
		break;
	}
	case EFC_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
	case EFC_EVT_SRRS_ELS_REQ_RJT:
		/* PLOGI failed, shutdown the node */
		if (efc_node_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!node->els_req_cnt);
		node->els_req_cnt--;
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(node);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_wait_node_attach(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg)
{
	struct efc_node_cb *cbdata = arg;
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

	case EFC_EVT_NODE_ATTACH_OK:
		node->attached = true;
		switch (node->send_ls_acc) {
		case EFC_NODE_SEND_LS_ACC_PRLI: {
			efc_d_send_prli_rsp(node->ls_acc_io,
					    node->ls_acc_oxid);
			node->send_ls_acc = EFC_NODE_SEND_LS_ACC_NONE;
			node->ls_acc_io = NULL;
			break;
		}
		case EFC_NODE_SEND_LS_ACC_PLOGI: /* Can't happen in P2P */
		case EFC_NODE_SEND_LS_ACC_NONE:
		default:
			/* Normal case for I */
			/* sm: send_plogi_acc is not set / send PLOGI acc */
			efc_node_transition(node, __efc_d_port_logged_in,
					    NULL);
			break;
		}
		break;

	case EFC_EVT_NODE_ATTACH_FAIL:
		/* node attach failed, shutdown the node */
		node->attached = false;
		node_printf(node, "Node attach failed\n");
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(node);
		break;

	case EFC_EVT_SHUTDOWN:
		node_printf(node, "%s received\n", efc_sm_event_name(evt));
		node->shutdown_reason = EFC_NODE_SHUTDOWN_DEFAULT;
		efc_node_transition(node,
				    __efc_fabric_wait_attach_evt_shutdown,
				     NULL);
		break;
	case EFC_EVT_PRLI_RCVD:
		node_printf(node, "%s: PRLI received before node is attached\n",
			    efc_sm_event_name(evt));
		efc_process_prli_payload(node, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(node,
					     cbdata->header->dma.virt,
				EFC_NODE_SEND_LS_ACC_PRLI);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

int
efc_p2p_setup(struct efc_nport *nport)
{
	struct efc *efc = nport->efc;
	int rnode_winner;

	rnode_winner = efc_rnode_is_winner(nport);

	/* set nport flags to indicate p2p "winner" */
	if (rnode_winner == 1) {
		nport->p2p_remote_port_id = 0;
		nport->p2p_port_id = 0;
		nport->p2p_winner = false;
	} else if (rnode_winner == 0) {
		nport->p2p_remote_port_id = 2;
		nport->p2p_port_id = 1;
		nport->p2p_winner = true;
	} else {
		/* no winner; only okay if external loopback enabled */
		if (nport->efc->external_loopback) {
			/*
			 * External loopback mode enabled;
			 * local nport and remote node
			 * will be registered with an NPortID = 1;
			 */
			efc_log_debug(efc,
				      "External loopback mode enabled\n");
			nport->p2p_remote_port_id = 1;
			nport->p2p_port_id = 1;
			nport->p2p_winner = true;
		} else {
			efc_log_warn(efc,
				     "failed to determine p2p winner\n");
			return rnode_winner;
		}
	}
	return 0;
}
