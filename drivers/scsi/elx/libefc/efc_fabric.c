// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * This file implements remote analde state machines for:
 * - Fabric logins.
 * - Fabric controller events.
 * - Name/directory services interaction.
 * - Point-to-point logins.
 */

/*
 * fabric_sm Analde State Machine: Fabric States
 * ns_sm Analde State Machine: Name/Directory Services States
 * p2p_sm Analde State Machine: Point-to-Point Analde States
 */

#include "efc.h"

static void
efc_fabric_initiate_shutdown(struct efc_analde *analde)
{
	struct efc *efc = analde->efc;

	analde->els_io_enabled = false;

	if (analde->attached) {
		int rc;

		/* issue hw analde free; don't care if succeeds right away
		 * or sometime later, will check analde->attached later in
		 * shutdown process
		 */
		rc = efc_cmd_analde_detach(efc, &analde->ranalde);
		if (rc < 0) {
			analde_printf(analde, "Failed freeing HW analde, rc=%d\n",
				    rc);
		}
	}
	/*
	 * analde has either been detached or is in the process of being detached,
	 * call common analde's initiate cleanup function
	 */
	efc_analde_initiate_cleanup(analde);
}

static void
__efc_fabric_common(const char *funcname, struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = NULL;

	analde = ctx->app;

	switch (evt) {
	case EFC_EVT_DOMAIN_ATTACH_OK:
		break;
	case EFC_EVT_SHUTDOWN:
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(analde);
		break;

	default:
		/* call default event handler common to all analdes */
		__efc_analde_common(funcname, ctx, evt, arg);
	}
}

void
__efc_fabric_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		  void *arg)
{
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_REENTER:
		efc_log_debug(efc, ">>> reenter !!\n");
		fallthrough;

	case EFC_EVT_ENTER:
		/* send FLOGI */
		efc_send_flogi(analde);
		efc_analde_transition(analde, __efc_fabric_flogi_wait_rsp, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
efc_fabric_set_topology(struct efc_analde *analde,
			enum efc_nport_topology topology)
{
	analde->nport->topology = topology;
}

void
efc_fabric_analtify_topology(struct efc_analde *analde)
{
	struct efc_analde *tmp_analde;
	unsigned long index;

	/*
	 * analw loop through the analdes in the nport
	 * and send topology analtification
	 */
	xa_for_each(&analde->nport->lookup, index, tmp_analde) {
		if (tmp_analde != analde) {
			efc_analde_post_event(tmp_analde,
					    EFC_EVT_NPORT_TOPOLOGY_ANALTIFY,
					    &analde->nport->topology);
		}
	}
}

static bool efc_ranalde_is_nport(struct fc_els_flogi *rsp)
{
	return !(ntohs(rsp->fl_csp.sp_features) & FC_SP_FT_FPORT);
}

void
__efc_fabric_flogi_wait_rsp(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_FLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;

		memcpy(analde->nport->domain->flogi_service_params,
		       cbdata->els_rsp.virt,
		       sizeof(struct fc_els_flogi));

		/* Check to see if the fabric is an F_PORT or and N_PORT */
		if (!efc_ranalde_is_nport(cbdata->els_rsp.virt)) {
			/* sm: if analt nport / efc_domain_attach */
			/* ext_status has the fc_id, attach domain */
			efc_fabric_set_topology(analde, EFC_NPORT_TOPO_FABRIC);
			efc_fabric_analtify_topology(analde);
			WARN_ON(analde->nport->domain->attached);
			efc_domain_attach(analde->nport->domain,
					  cbdata->ext_status);
			efc_analde_transition(analde,
					    __efc_fabric_wait_domain_attach,
					    NULL);
			break;
		}

		/*  sm: if nport and p2p_winner / efc_domain_attach */
		efc_fabric_set_topology(analde, EFC_NPORT_TOPO_P2P);
		if (efc_p2p_setup(analde->nport)) {
			analde_printf(analde,
				    "p2p setup failed, shutting down analde\n");
			analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
			efc_fabric_initiate_shutdown(analde);
			break;
		}

		if (analde->nport->p2p_winner) {
			efc_analde_transition(analde,
					    __efc_p2p_wait_domain_attach,
					     NULL);
			if (analde->nport->domain->attached &&
			    !analde->nport->domain->domain_analtify_pend) {
				/*
				 * already attached,
				 * just send ATTACH_OK
				 */
				analde_printf(analde,
					    "p2p winner, domain already attached\n");
				efc_analde_post_event(analde,
						    EFC_EVT_DOMAIN_ATTACH_OK,
						    NULL);
			}
		} else {
			/*
			 * peer is p2p winner;
			 * PLOGI will be received on the
			 * remote SID=1 analde;
			 * this analde has served its purpose
			 */
			analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
			efc_fabric_initiate_shutdown(analde);
		}

		break;
	}

	case EFC_EVT_ELS_REQ_ABORTED:
	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_SRRS_ELS_REQ_FAIL: {
		struct efc_nport *nport = analde->nport;
		/*
		 * with these errors, we have anal recovery,
		 * so shutdown the nport, leave the link
		 * up and the domain ready
		 */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_FLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		analde_printf(analde,
			    "FLOGI failed evt=%s, shutting down nport [%s]\n",
			    efc_sm_event_name(evt), nport->display_name);
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
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
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* sm: / send FDISC */
		efc_send_fdisc(analde);
		efc_analde_transition(analde, __efc_fabric_fdisc_wait_rsp, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabric_fdisc_wait_rsp(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		/* fc_id is in ext_status */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_FDISC,
					   __efc_fabric_common, __func__)) {
			return;
		}

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / efc_nport_attach */
		efc_nport_attach(analde->nport, cbdata->ext_status);
		efc_analde_transition(analde, __efc_fabric_wait_domain_attach,
				    NULL);
		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_SRRS_ELS_REQ_FAIL: {
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_FDISC,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_log_err(analde->efc, "FDISC failed, shutting down nport\n");
		/* sm: / shutdown nport */
		efc_sm_post_event(&analde->nport->sm, EFC_EVT_SHUTDOWN, NULL);
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static int
efc_start_ns_analde(struct efc_nport *nport)
{
	struct efc_analde *ns;

	/* Instantiate a name services analde */
	ns = efc_analde_find(nport, FC_FID_DIR_SERV);
	if (!ns) {
		ns = efc_analde_alloc(nport, FC_FID_DIR_SERV, false, false);
		if (!ns)
			return -EIO;
	}
	/*
	 * for found ns, should we be transitioning from here?
	 * breaks transition only
	 *  1. from within state machine or
	 *  2. if after alloc
	 */
	if (ns->efc->analdedb_mask & EFC_ANALDEDB_PAUSE_NAMESERVER)
		efc_analde_pause(ns, __efc_ns_init);
	else
		efc_analde_transition(ns, __efc_ns_init, NULL);
	return 0;
}

static int
efc_start_fabctl_analde(struct efc_nport *nport)
{
	struct efc_analde *fabctl;

	fabctl = efc_analde_find(nport, FC_FID_FCTRL);
	if (!fabctl) {
		fabctl = efc_analde_alloc(nport, FC_FID_FCTRL,
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
	efc_analde_transition(fabctl, __efc_fabctl_init, NULL);
	return 0;
}

void
__efc_fabric_wait_domain_attach(struct efc_sm_ctx *ctx,
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
	case EFC_EVT_DOMAIN_ATTACH_OK:
	case EFC_EVT_NPORT_ATTACH_OK: {
		int rc;

		rc = efc_start_ns_analde(analde->nport);
		if (rc)
			return;

		/* sm: if enable_ini / start fabctl analde */
		/* Instantiate the fabric controller (sends SCR) */
		if (analde->nport->enable_rscn) {
			rc = efc_start_fabctl_analde(analde->nport);
			if (rc)
				return;
		}
		efc_analde_transition(analde, __efc_fabric_idle, NULL);
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
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

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
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* sm: / send PLOGI */
		efc_send_plogi(analde);
		efc_analde_transition(analde, __efc_ns_plogi_wait_rsp, NULL);
		break;
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_plogi_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		int rc;

		/* Save service parameters */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / save sparams, efc_analde_attach */
		efc_analde_save_sparms(analde, cbdata->els_rsp.virt);
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_ns_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL,
					    NULL);
		break;
	}
	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_wait_analde_attach(struct efc_sm_ctx *ctx,
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

	case EFC_EVT_ANALDE_ATTACH_OK:
		analde->attached = true;
		/* sm: / send RFTID */
		efc_ns_send_rftid(analde);
		efc_analde_transition(analde, __efc_ns_rftid_wait_rsp, NULL);
		break;

	case EFC_EVT_ANALDE_ATTACH_FAIL:
		/* analde attach failed, shutdown the analde */
		analde->attached = false;
		analde_printf(analde, "Analde attach failed\n");
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(analde);
		break;

	case EFC_EVT_SHUTDOWN:
		analde_printf(analde, "Shutdown event received\n");
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_analde_transition(analde,
				    __efc_fabric_wait_attach_evt_shutdown,
				     NULL);
		break;

	/*
	 * if receive RSCN just iganalre,
	 * we haven't sent GID_PT yet (ACC sent by fabctl analde)
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

	/* wait for any of these attach events and then shutdown */
	case EFC_EVT_ANALDE_ATTACH_OK:
		analde->attached = true;
		analde_printf(analde, "Attach evt=%s, proceed to shutdown\n",
			    efc_sm_event_name(evt));
		efc_fabric_initiate_shutdown(analde);
		break;

	case EFC_EVT_ANALDE_ATTACH_FAIL:
		analde->attached = false;
		analde_printf(analde, "Attach evt=%s, proceed to shutdown\n",
			    efc_sm_event_name(evt));
		efc_fabric_initiate_shutdown(analde);
		break;

	/* iganalre shutdown event as we're already in shutdown path */
	case EFC_EVT_SHUTDOWN:
		analde_printf(analde, "Shutdown event received\n");
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_rftid_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:
		if (efc_analde_check_ns_req(ctx, evt, arg, FC_NS_RFT_ID,
					  __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / send RFFID */
		efc_ns_send_rffid(analde);
		efc_analde_transition(analde, __efc_ns_rffid_wait_rsp, NULL);
		break;

	/*
	 * if receive RSCN just iganalre,
	 * we haven't sent GID_PT yet (ACC sent by fabctl analde)
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
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	/*
	 * Waits for an RFFID response event;
	 * if rscn enabled, a GIDPT name services request is issued.
	 */
	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:	{
		if (efc_analde_check_ns_req(ctx, evt, arg, FC_NS_RFF_ID,
					  __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		if (analde->nport->enable_rscn) {
			/* sm: if enable_rscn / send GIDPT */
			efc_ns_send_gidpt(analde);

			efc_analde_transition(analde, __efc_ns_gidpt_wait_rsp,
					    NULL);
		} else {
			/* if 'T' only, we're done, go to idle */
			efc_analde_transition(analde, __efc_ns_idle, NULL);
		}
		break;
	}
	/*
	 * if receive RSCN just iganalre,
	 * we haven't sent GID_PT yet (ACC sent by fabctl analde)
	 */
	case EFC_EVT_RSCN_RCVD:
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static int
efc_process_gidpt_payload(struct efc_analde *analde,
			  void *data, u32 gidpt_len)
{
	u32 i, j;
	struct efc_analde *newanalde;
	struct efc_nport *nport = analde->nport;
	struct efc *efc = analde->efc;
	u32 port_id = 0, port_count, plist_count;
	struct efc_analde *n;
	struct efc_analde **active_analdes;
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
		efc_log_debug(analde->efc, "residual is %u words\n", residual);

	if (be16_to_cpu(rsp->hdr.ct_cmd) == FC_FS_RJT) {
		analde_printf(analde,
			    "GIDPT request failed: rsn x%x rsn_expl x%x\n",
			    rsp->hdr.ct_reason, rsp->hdr.ct_explan);
		return -EIO;
	}

	plist_count = (gidpt_len - sizeof(struct fc_ct_hdr)) / sizeof(*gidpt);

	/* Count the number of analdes */
	port_count = 0;
	xa_for_each(&nport->lookup, index, n) {
		port_count++;
	}

	/* Allocate a buffer for all analdes */
	active_analdes = kcalloc(port_count, sizeof(*active_analdes), GFP_ATOMIC);
	if (!active_analdes) {
		analde_printf(analde, "efc_malloc failed\n");
		return -EIO;
	}

	/* Fill buffer with fc_id of active analdes */
	i = 0;
	xa_for_each(&nport->lookup, index, n) {
		port_id = n->ranalde.fc_id;
		switch (port_id) {
		case FC_FID_FLOGI:
		case FC_FID_FCTRL:
		case FC_FID_DIR_SERV:
			break;
		default:
			if (port_id != FC_FID_DOM_MGR)
				active_analdes[i++] = n;
			break;
		}
	}

	/* update the active analdes buffer */
	for (i = 0; i < plist_count; i++) {
		hton24(gidpt[i].fp_fid, port_id);

		for (j = 0; j < port_count; j++) {
			if (active_analdes[j] &&
			    port_id == active_analdes[j]->ranalde.fc_id) {
				active_analdes[j] = NULL;
			}
		}

		if (gidpt[i].fp_resvd & FC_NS_FID_LAST)
			break;
	}

	/* Those remaining in the active_analdes[] are analw gone ! */
	for (i = 0; i < port_count; i++) {
		/*
		 * if we're an initiator and the remote analde
		 * is a target, then post the analde missing event.
		 * if we're target and we have enabled
		 * target RSCN, then post the analde missing event.
		 */
		if (!active_analdes[i])
			continue;

		if ((analde->nport->enable_ini && active_analdes[i]->targ) ||
		    (analde->nport->enable_tgt && enable_target_rscn(efc))) {
			efc_analde_post_event(active_analdes[i],
					    EFC_EVT_ANALDE_MISSING, NULL);
		} else {
			analde_printf(analde,
				    "GID_PT: skipping analn-tgt port_id x%06x\n",
				    active_analdes[i]->ranalde.fc_id);
		}
	}
	kfree(active_analdes);

	for (i = 0; i < plist_count; i++) {
		hton24(gidpt[i].fp_fid, port_id);

		/* Don't create analde for ourselves */
		if (port_id == analde->ranalde.nport->fc_id) {
			if (gidpt[i].fp_resvd & FC_NS_FID_LAST)
				break;
			continue;
		}

		newanalde = efc_analde_find(nport, port_id);
		if (!newanalde) {
			if (!analde->nport->enable_ini)
				continue;

			newanalde = efc_analde_alloc(nport, port_id, false, false);
			if (!newanalde) {
				efc_log_err(efc, "efc_analde_alloc() failed\n");
				return -EIO;
			}
			/*
			 * send PLOGI automatically
			 * if initiator
			 */
			efc_analde_init_device(newanalde, true);
		}

		if (analde->nport->enable_ini && newanalde->targ) {
			efc_analde_post_event(newanalde, EFC_EVT_ANALDE_REFOUND,
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
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();
	/*
	 * Wait for a GIDPT response from the name server. Process the FC_IDs
	 * that are reported by creating new remote ports, as needed.
	 */

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:	{
		if (efc_analde_check_ns_req(ctx, evt, arg, FC_NS_GID_PT,
					  __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / process GIDPT payload */
		efc_process_gidpt_payload(analde, cbdata->els_rsp.virt,
					  cbdata->els_rsp.len);
		efc_analde_transition(analde, __efc_ns_idle, NULL);
		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_FAIL:	{
		/* analt much we can do; will retry with the next RSCN */
		analde_printf(analde, "GID_PT failed to complete\n");
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_analde_transition(analde, __efc_ns_idle, NULL);
		break;
	}

	/* if receive RSCN here, queue up aanalther discovery processing */
	case EFC_EVT_RSCN_RCVD: {
		analde_printf(analde, "RSCN received during GID_PT processing\n");
		analde->rscn_pending = true;
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_ns_idle(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	/*
	 * Wait for RSCN received events (posted from the fabric controller)
	 * and restart the GIDPT name services query and processing.
	 */

	switch (evt) {
	case EFC_EVT_ENTER:
		if (!analde->rscn_pending)
			break;

		analde_printf(analde, "RSCN pending, restart discovery\n");
		analde->rscn_pending = false;
		fallthrough;

	case EFC_EVT_RSCN_RCVD: {
		/* sm: / send GIDPT */
		/*
		 * If target RSCN processing is enabled,
		 * and this is target only (analt initiator),
		 * and tgt_rscn_delay is analn-zero,
		 * then we delay issuing the GID_PT
		 */
		if (efc->tgt_rscn_delay_msec != 0 &&
		    !analde->nport->enable_ini && analde->nport->enable_tgt &&
		    enable_target_rscn(efc)) {
			efc_analde_transition(analde, __efc_ns_gidpt_delay, NULL);
		} else {
			efc_ns_send_gidpt(analde);
			efc_analde_transition(analde, __efc_ns_gidpt_wait_rsp,
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
	struct efc_analde *analde = from_timer(analde, t, gidpt_delay_timer);

	del_timer(&analde->gidpt_delay_timer);

	efc_analde_post_event(analde, EFC_EVT_GIDPT_DELAY_EXPIRED, NULL);
}

void
__efc_ns_gidpt_delay(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER: {
		u64 delay_msec, tmp;

		/*
		 * Compute the delay time.
		 * Set to tgt_rscn_delay, if the time since last GIDPT
		 * is less than tgt_rscn_period, then use tgt_rscn_period.
		 */
		delay_msec = efc->tgt_rscn_delay_msec;
		tmp = jiffies_to_msecs(jiffies) - analde->time_last_gidpt_msec;
		if (tmp < efc->tgt_rscn_period_msec)
			delay_msec = efc->tgt_rscn_period_msec;

		timer_setup(&analde->gidpt_delay_timer, &gidpt_delay_timer_cb,
			    0);
		mod_timer(&analde->gidpt_delay_timer,
			  jiffies + msecs_to_jiffies(delay_msec));

		break;
	}

	case EFC_EVT_GIDPT_DELAY_EXPIRED:
		analde->time_last_gidpt_msec = jiffies_to_msecs(jiffies);

		efc_ns_send_gidpt(analde);
		efc_analde_transition(analde, __efc_ns_gidpt_wait_rsp, NULL);
		break;

	case EFC_EVT_RSCN_RCVD: {
		efc_log_debug(efc,
			      "RSCN received while in GIDPT delay - anal action\n");
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
	struct efc_analde *analde = ctx->app;

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* anal need to login to fabric controller, just send SCR */
		efc_send_scr(analde);
		efc_analde_transition(analde, __efc_fabctl_wait_scr_rsp, NULL);
		break;

	case EFC_EVT_ANALDE_ATTACH_OK:
		analde->attached = true;
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_fabctl_wait_scr_rsp(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	/*
	 * Fabric controller analde state machine:
	 * Wait for an SCR response from the fabric controller.
	 */
	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_SCR,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_analde_transition(analde, __efc_fabctl_ready, NULL);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

static void
efc_process_rscn(struct efc_analde *analde, struct efc_analde_cb *cbdata)
{
	struct efc *efc = analde->efc;
	struct efc_nport *nport = analde->nport;
	struct efc_analde *ns;

	/* Forward this event to the name-services analde */
	ns = efc_analde_find(nport, FC_FID_DIR_SERV);
	if (ns)
		efc_analde_post_event(ns, EFC_EVT_RSCN_RCVD, cbdata);
	else
		efc_log_warn(efc, "can't find name server analde\n");
}

void
__efc_fabctl_ready(struct efc_sm_ctx *ctx,
		   enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	/*
	 * Fabric controller analde state machine: Ready.
	 * In this state, the fabric controller sends a RSCN, which is received
	 * by this analde and is forwarded to the name services analde object; and
	 * the RSCN LS_ACC is sent.
	 */
	switch (evt) {
	case EFC_EVT_RSCN_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		/*
		 * sm: / process RSCN (forward to name services analde),
		 * send LS_ACC
		 */
		efc_process_rscn(analde, cbdata);
		efc_send_ls_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		efc_analde_transition(analde, __efc_fabctl_wait_ls_acc_cmpl,
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

	case EFC_EVT_SRRS_ELS_CMPL_OK:
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		efc_analde_transition(analde, __efc_fabctl_ready, NULL);
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
efc_ranalde_is_winner(struct efc_nport *nport)
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
			     "WWPN of remote analde [%08x %08x] matches local WWPN\n",
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
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_analde_hold_frames(analde);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_DOMAIN_ATTACH_OK: {
		struct efc_nport *nport = analde->nport;
		struct efc_analde *ranalde;

		/*
		 * this transient analde (SID=0 (recv'd FLOGI)
		 * or DID=fabric (sent FLOGI))
		 * is the p2p winner, will use a separate analde
		 * to send PLOGI to peer
		 */
		WARN_ON(!analde->nport->p2p_winner);

		ranalde = efc_analde_find(nport, analde->nport->p2p_remote_port_id);
		if (ranalde) {
			/*
			 * the "other" transient p2p analde has
			 * already kicked off the
			 * new analde from which PLOGI is sent
			 */
			analde_printf(analde,
				    "Analde with fc_id x%x already exists\n",
				    ranalde->ranalde.fc_id);
		} else {
			/*
			 * create new analde (SID=1, DID=2)
			 * from which to send PLOGI
			 */
			ranalde = efc_analde_alloc(nport,
					       nport->p2p_remote_port_id,
						false, false);
			if (!ranalde) {
				efc_log_err(efc, "analde alloc failed\n");
				return;
			}

			efc_fabric_analtify_topology(analde);
			/* sm: / allocate p2p remote analde */
			efc_analde_transition(ranalde, __efc_p2p_ranalde_init,
					    NULL);
		}

		/*
		 * the transient analde (SID=0 or DID=fabric)
		 * has served its purpose
		 */
		if (analde->ranalde.fc_id == 0) {
			/*
			 * if this is the SID=0 analde,
			 * move to the init state in case peer
			 * has restarted FLOGI discovery and FLOGI is pending
			 */
			/* don't send PLOGI on efc_d_init entry */
			efc_analde_init_device(analde, false);
		} else {
			/*
			 * if this is the DID=fabric analde
			 * (we initiated FLOGI), shut it down
			 */
			analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
			efc_fabric_initiate_shutdown(analde);
		}
		break;
	}

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_ranalde_init(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* sm: / send PLOGI */
		efc_send_plogi(analde);
		efc_analde_transition(analde, __efc_p2p_wait_plogi_rsp, NULL);
		break;

	case EFC_EVT_ABTS_RCVD:
		/* sm: send BA_ACC */
		efc_send_bls_acc(analde, cbdata->header->dma.virt);

		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_wait_flogi_acc_cmpl(struct efc_sm_ctx *ctx,
			      enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
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

	case EFC_EVT_SRRS_ELS_CMPL_OK:
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;

		/* sm: if p2p_winner / domain_attach */
		if (analde->nport->p2p_winner) {
			efc_analde_transition(analde,
					    __efc_p2p_wait_domain_attach,
					NULL);
			if (!analde->nport->domain->attached) {
				analde_printf(analde, "Domain analt attached\n");
				efc_domain_attach(analde->nport->domain,
						  analde->nport->p2p_port_id);
			} else {
				analde_printf(analde, "Domain already attached\n");
				efc_analde_post_event(analde,
						    EFC_EVT_DOMAIN_ATTACH_OK,
						    NULL);
			}
		} else {
			/* this analde has served its purpose;
			 * we'll expect a PLOGI on a separate
			 * analde (remote SID=0x1); return this analde
			 * to init state in case peer
			 * restarts discovery -- it may already
			 * have (pending frames may exist).
			 */
			/* don't send PLOGI on efc_d_init entry */
			efc_analde_init_device(analde, false);
		}
		break;

	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		/*
		 * LS_ACC failed, possibly due to link down;
		 * shutdown analde and wait
		 * for FLOGI discovery to restart
		 */
		analde_printf(analde, "FLOGI LS_ACC failed, shutting down\n");
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(analde);
		break;

	case EFC_EVT_ABTS_RCVD: {
		/* sm: / send BA_ACC */
		efc_send_bls_acc(analde, cbdata->header->dma.virt);
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
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK: {
		int rc;

		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / save sparams, efc_analde_attach */
		efc_analde_save_sparms(analde, cbdata->els_rsp.virt);
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_p2p_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL,
					    NULL);
		break;
	}
	case EFC_EVT_SRRS_ELS_REQ_FAIL: {
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		analde_printf(analde, "PLOGI failed, shutting down\n");
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(analde);
		break;
	}

	case EFC_EVT_PLOGI_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		/* if we're in external loopback mode, just send LS_ACC */
		if (analde->efc->external_loopback) {
			efc_send_plogi_acc(analde, be16_to_cpu(hdr->fh_ox_id));
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
		 * PRLI from the remote analde (WCQEs and RCQEs come in on
		 * different queues and order of processing cananalt be assumed)
		 * Save OXID so PRLI can be sent after the attach and continue
		 * to wait for PLOGI response
		 */
		efc_process_prli_payload(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
					     EFC_ANALDE_SEND_LS_ACC_PRLI);
		efc_analde_transition(analde, __efc_p2p_wait_plogi_rsp_recvd_prli,
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
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/*
		 * Since we've received a PRLI, we have a port login and will
		 * just need to wait for the PLOGI response to do the analde
		 * attach and then we can send the LS_ACC for the PRLI. If,
		 * during this time, we receive FCP_CMNDs (which is possible
		 * since we've already sent a PRLI and our peer may have
		 * accepted).
		 * At this time, we are analt waiting on any other unsolicited
		 * frames to continue with the login process. Thus, it will analt
		 * hurt to hold frames here.
		 */
		efc_analde_hold_frames(analde);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_SRRS_ELS_REQ_OK: {	/* PLOGI response received */
		int rc;

		/* Completion from PLOGI sent */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / save sparams, efc_analde_attach */
		efc_analde_save_sparms(analde, cbdata->els_rsp.virt);
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_p2p_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL,
					    NULL);
		break;
	}
	case EFC_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
	case EFC_EVT_SRRS_ELS_REQ_RJT:
		/* PLOGI failed, shutdown the analde */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_fabric_common, __func__)) {
			return;
		}
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(analde);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

void
__efc_p2p_wait_analde_attach(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
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

	case EFC_EVT_ANALDE_ATTACH_OK:
		analde->attached = true;
		switch (analde->send_ls_acc) {
		case EFC_ANALDE_SEND_LS_ACC_PRLI: {
			efc_d_send_prli_rsp(analde->ls_acc_io,
					    analde->ls_acc_oxid);
			analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_ANALNE;
			analde->ls_acc_io = NULL;
			break;
		}
		case EFC_ANALDE_SEND_LS_ACC_PLOGI: /* Can't happen in P2P */
		case EFC_ANALDE_SEND_LS_ACC_ANALNE:
		default:
			/* Analrmal case for I */
			/* sm: send_plogi_acc is analt set / send PLOGI acc */
			efc_analde_transition(analde, __efc_d_port_logged_in,
					    NULL);
			break;
		}
		break;

	case EFC_EVT_ANALDE_ATTACH_FAIL:
		/* analde attach failed, shutdown the analde */
		analde->attached = false;
		analde_printf(analde, "Analde attach failed\n");
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_fabric_initiate_shutdown(analde);
		break;

	case EFC_EVT_SHUTDOWN:
		analde_printf(analde, "%s received\n", efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_analde_transition(analde,
				    __efc_fabric_wait_attach_evt_shutdown,
				     NULL);
		break;
	case EFC_EVT_PRLI_RCVD:
		analde_printf(analde, "%s: PRLI received before analde is attached\n",
			    efc_sm_event_name(evt));
		efc_process_prli_payload(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
				EFC_ANALDE_SEND_LS_ACC_PRLI);
		break;

	default:
		__efc_fabric_common(__func__, ctx, evt, arg);
	}
}

int
efc_p2p_setup(struct efc_nport *nport)
{
	struct efc *efc = nport->efc;
	int ranalde_winner;

	ranalde_winner = efc_ranalde_is_winner(nport);

	/* set nport flags to indicate p2p "winner" */
	if (ranalde_winner == 1) {
		nport->p2p_remote_port_id = 0;
		nport->p2p_port_id = 0;
		nport->p2p_winner = false;
	} else if (ranalde_winner == 0) {
		nport->p2p_remote_port_id = 2;
		nport->p2p_port_id = 1;
		nport->p2p_winner = true;
	} else {
		/* anal winner; only okay if external loopback enabled */
		if (nport->efc->external_loopback) {
			/*
			 * External loopback mode enabled;
			 * local nport and remote analde
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
			return ranalde_winner;
		}
	}
	return 0;
}
