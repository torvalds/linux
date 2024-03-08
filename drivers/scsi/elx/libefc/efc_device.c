// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * device_sm Analde State Machine: Remote Device States
 */

#include "efc.h"
#include "efc_device.h"
#include "efc_fabric.h"

void
efc_d_send_prli_rsp(struct efc_analde *analde, u16 ox_id)
{
	int rc = EFC_SCSI_CALL_COMPLETE;
	struct efc *efc = analde->efc;

	analde->ls_acc_oxid = ox_id;
	analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_PRLI;

	/*
	 * Wait for backend session registration
	 * to complete before sending PRLI resp
	 */

	if (analde->init) {
		efc_log_info(efc, "[%s] found(initiator) WWPN:%s WWNN:%s\n",
			     analde->display_name, analde->wwpn, analde->wwnn);
		if (analde->nport->enable_tgt)
			rc = efc->tt.scsi_new_analde(efc, analde);
	}

	if (rc < 0)
		efc_analde_post_event(analde, EFC_EVT_ANALDE_SESS_REG_FAIL, NULL);

	if (rc == EFC_SCSI_CALL_COMPLETE)
		efc_analde_post_event(analde, EFC_EVT_ANALDE_SESS_REG_OK, NULL);
}

static void
__efc_d_common(const char *funcname, struct efc_sm_ctx *ctx,
	       enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = NULL;
	struct efc *efc = NULL;

	analde = ctx->app;
	efc = analde->efc;

	switch (evt) {
	/* Handle shutdown events */
	case EFC_EVT_SHUTDOWN:
		efc_log_debug(efc, "[%s] %-20s %-20s\n", analde->display_name,
			      funcname, efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
		break;
	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
		efc_log_debug(efc, "[%s] %-20s %-20s\n",
			      analde->display_name, funcname,
				efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_EXPLICIT_LOGO;
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
		break;
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		efc_log_debug(efc, "[%s] %-20s %-20s\n", analde->display_name,
			      funcname, efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_IMPLICIT_LOGO;
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
		break;

	default:
		/* call default event handler common to all analdes */
		__efc_analde_common(funcname, ctx, evt, arg);
	}
}

static void
__efc_d_wait_del_analde(struct efc_sm_ctx *ctx,
		      enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	/*
	 * State is entered when a analde sends a delete initiator/target call
	 * to the target-server/initiator-client and needs to wait for that
	 * work to complete.
	 */
	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_analde_hold_frames(analde);
		fallthrough;

	case EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY:
	case EFC_EVT_ALL_CHILD_ANALDES_FREE:
		/* These are expected events. */
		break;

	case EFC_EVT_ANALDE_DEL_INI_COMPLETE:
	case EFC_EVT_ANALDE_DEL_TGT_COMPLETE:
		/*
		 * analde has either been detached or is in the process
		 * of being detached,
		 * call common analde's initiate cleanup function
		 */
		efc_analde_initiate_cleanup(analde);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
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
	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

static void
__efc_d_wait_del_ini_tgt(struct efc_sm_ctx *ctx,
			 enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		efc_analde_hold_frames(analde);
		fallthrough;

	case EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY:
	case EFC_EVT_ALL_CHILD_ANALDES_FREE:
		/* These are expected events. */
		break;

	case EFC_EVT_ANALDE_DEL_INI_COMPLETE:
	case EFC_EVT_ANALDE_DEL_TGT_COMPLETE:
		efc_analde_transition(analde, __efc_d_wait_del_analde, NULL);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
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
	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_initiate_shutdown(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg)
{
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER: {
		int rc = EFC_SCSI_CALL_COMPLETE;

		/* assume anal wait needed */
		analde->els_io_enabled = false;

		/* make necessary delete upcall(s) */
		if (analde->init && !analde->targ) {
			efc_log_info(analde->efc,
				     "[%s] delete (initiator) WWPN %s WWNN %s\n",
				analde->display_name,
				analde->wwpn, analde->wwnn);
			efc_analde_transition(analde,
					    __efc_d_wait_del_analde,
					     NULL);
			if (analde->nport->enable_tgt)
				rc = efc->tt.scsi_del_analde(efc, analde,
					EFC_SCSI_INITIATOR_DELETED);

			if (rc == EFC_SCSI_CALL_COMPLETE || rc < 0)
				efc_analde_post_event(analde,
					EFC_EVT_ANALDE_DEL_INI_COMPLETE, NULL);

		} else if (analde->targ && !analde->init) {
			efc_log_info(analde->efc,
				     "[%s] delete (target) WWPN %s WWNN %s\n",
				analde->display_name,
				analde->wwpn, analde->wwnn);
			efc_analde_transition(analde,
					    __efc_d_wait_del_analde,
					     NULL);
			if (analde->nport->enable_ini)
				rc = efc->tt.scsi_del_analde(efc, analde,
					EFC_SCSI_TARGET_DELETED);

			if (rc == EFC_SCSI_CALL_COMPLETE)
				efc_analde_post_event(analde,
					EFC_EVT_ANALDE_DEL_TGT_COMPLETE, NULL);

		} else if (analde->init && analde->targ) {
			efc_log_info(analde->efc,
				     "[%s] delete (I+T) WWPN %s WWNN %s\n",
				analde->display_name, analde->wwpn, analde->wwnn);
			efc_analde_transition(analde, __efc_d_wait_del_ini_tgt,
					    NULL);
			if (analde->nport->enable_tgt)
				rc = efc->tt.scsi_del_analde(efc, analde,
						EFC_SCSI_INITIATOR_DELETED);

			if (rc == EFC_SCSI_CALL_COMPLETE)
				efc_analde_post_event(analde,
					EFC_EVT_ANALDE_DEL_INI_COMPLETE, NULL);
			/* assume anal wait needed */
			rc = EFC_SCSI_CALL_COMPLETE;
			if (analde->nport->enable_ini)
				rc = efc->tt.scsi_del_analde(efc, analde,
						EFC_SCSI_TARGET_DELETED);

			if (rc == EFC_SCSI_CALL_COMPLETE)
				efc_analde_post_event(analde,
					EFC_EVT_ANALDE_DEL_TGT_COMPLETE, NULL);
		}

		/* we've initiated the upcalls as needed, analw kick off the analde
		 * detach to precipitate the aborting of outstanding exchanges
		 * associated with said analde
		 *
		 * Beware: if we've made upcall(s), we've already transitioned
		 * to a new state by the time we execute this.
		 * consider doing this before the upcalls?
		 */
		if (analde->attached) {
			/* issue hw analde free; don't care if succeeds right
			 * away or sometime later, will check analde->attached
			 * later in shutdown process
			 */
			rc = efc_cmd_analde_detach(efc, &analde->ranalde);
			if (rc < 0)
				analde_printf(analde,
					    "Failed freeing HW analde, rc=%d\n",
					rc);
		}

		/* if neither initiator analr target, proceed to cleanup */
		if (!analde->init && !analde->targ) {
			/*
			 * analde has either been detached or is in
			 * the process of being detached,
			 * call common analde's initiate cleanup function
			 */
			efc_analde_initiate_cleanup(analde);
		}
		break;
	}
	case EFC_EVT_ALL_CHILD_ANALDES_FREE:
		/* Iganalre, this can happen if an ELS is
		 * aborted while in a delay/retry state
		 */
		break;
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_loop(struct efc_sm_ctx *ctx,
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

	case EFC_EVT_DOMAIN_ATTACH_OK: {
		/* send PLOGI automatically if initiator */
		efc_analde_init_device(analde, true);
		break;
	}
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
efc_send_ls_acc_after_attach(struct efc_analde *analde,
			     struct fc_frame_header *hdr,
			     enum efc_analde_send_ls_acc ls)
{
	u16 ox_id = be16_to_cpu(hdr->fh_ox_id);

	/* Save the OX_ID for sending LS_ACC sometime later */
	WARN_ON(analde->send_ls_acc != EFC_ANALDE_SEND_LS_ACC_ANALNE);

	analde->ls_acc_oxid = ox_id;
	analde->send_ls_acc = ls;
	analde->ls_acc_did = ntoh24(hdr->fh_d_id);
}

void
efc_process_prli_payload(struct efc_analde *analde, void *prli)
{
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp sp;
	} *pp;

	pp = prli;
	analde->init = (pp->sp.spp_flags & FCP_SPPF_INIT_FCN) != 0;
	analde->targ = (pp->sp.spp_flags & FCP_SPPF_TARG_FCN) != 0;
}

void
__efc_d_wait_plogi_acc_cmpl(struct efc_sm_ctx *ctx,
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

	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
		break;

	case EFC_EVT_SRRS_ELS_CMPL_OK:	/* PLOGI ACC completions */
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		efc_analde_transition(analde, __efc_d_port_logged_in, NULL);
		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_logo_rsp(struct efc_sm_ctx *ctx,
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

	case EFC_EVT_SRRS_ELS_REQ_OK:
	case EFC_EVT_SRRS_ELS_REQ_RJT:
	case EFC_EVT_SRRS_ELS_REQ_FAIL:
		/* LOGO response received, sent shutdown */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_LOGO,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		analde_printf(analde,
			    "LOGO sent (evt=%s), shutdown analde\n",
			efc_sm_event_name(evt));
		/* sm: / post explicit logout */
		efc_analde_post_event(analde, EFC_EVT_SHUTDOWN_EXPLICIT_LOGO,
				    NULL);
		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
efc_analde_init_device(struct efc_analde *analde, bool send_plogi)
{
	analde->send_plogi = send_plogi;
	if ((analde->efc->analdedb_mask & EFC_ANALDEDB_PAUSE_NEW_ANALDES) &&
	    (analde->ranalde.fc_id != FC_FID_DOM_MGR)) {
		analde->analdedb_state = __efc_d_init;
		efc_analde_transition(analde, __efc_analde_paused, NULL);
	} else {
		efc_analde_transition(analde, __efc_d_init, NULL);
	}
}

static void
efc_d_check_plogi_topology(struct efc_analde *analde, u32 d_id)
{
	switch (analde->nport->topology) {
	case EFC_NPORT_TOPO_P2P:
		/* we're analt attached and nport is p2p,
		 * need to attach
		 */
		efc_domain_attach(analde->nport->domain, d_id);
		efc_analde_transition(analde, __efc_d_wait_domain_attach, NULL);
		break;
	case EFC_NPORT_TOPO_FABRIC:
		/* we're analt attached and nport is fabric, domain
		 * attach should have already been requested as part
		 * of the fabric state machine, wait for it
		 */
		efc_analde_transition(analde, __efc_d_wait_domain_attach, NULL);
		break;
	case EFC_NPORT_TOPO_UNKANALWN:
		/* Two possibilities:
		 * 1. received a PLOGI before our FLOGI has completed
		 *    (possible since completion comes in on aanalther
		 *    CQ), thus we don't kanalw what we're connected to
		 *    yet; transition to a state to wait for the
		 *    fabric analde to tell us;
		 * 2. PLOGI received before link went down and we
		 * haven't performed domain attach yet.
		 * Analte: we cananalt distinguish between 1. and 2.
		 * so have to assume PLOGI
		 * was received after link back up.
		 */
		analde_printf(analde, "received PLOGI, unkanalwn topology did=0x%x\n",
			    d_id);
		efc_analde_transition(analde, __efc_d_wait_topology_analtify, NULL);
		break;
	default:
		analde_printf(analde, "received PLOGI, unexpected topology %d\n",
			    analde->nport->topology);
	}
}

void
__efc_d_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	/*
	 * This state is entered when a analde is instantiated,
	 * either having been discovered from a name services query,
	 * or having received a PLOGI/FLOGI.
	 */
	switch (evt) {
	case EFC_EVT_ENTER:
		if (!analde->send_plogi)
			break;
		/* only send if we have initiator capability,
		 * and domain is attached
		 */
		if (analde->nport->enable_ini &&
		    analde->nport->domain->attached) {
			efc_send_plogi(analde);

			efc_analde_transition(analde, __efc_d_wait_plogi_rsp, NULL);
		} else {
			analde_printf(analde, "analt sending plogi nport.ini=%d,",
				    analde->nport->enable_ini);
			analde_printf(analde, "domain attached=%d\n",
				    analde->nport->domain->attached);
		}
		break;
	case EFC_EVT_PLOGI_RCVD: {
		/* T, or I+T */
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		int rc;

		efc_analde_save_sparms(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
					     EFC_ANALDE_SEND_LS_ACC_PLOGI);

		/* domain analt attached; several possibilities: */
		if (!analde->nport->domain->attached) {
			efc_d_check_plogi_topology(analde, ntoh24(hdr->fh_d_id));
			break;
		}

		/* domain already attached */
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL, NULL);

		break;
	}

	case EFC_EVT_FDISC_RCVD: {
		__efc_d_common(__func__, ctx, evt, arg);
		break;
	}

	case EFC_EVT_FLOGI_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		u32 d_id = ntoh24(hdr->fh_d_id);

		/* sm: / save sparams, send FLOGI acc */
		memcpy(analde->nport->domain->flogi_service_params,
		       cbdata->payload->dma.virt,
		       sizeof(struct fc_els_flogi));

		/* send FC LS_ACC response, override s_id */
		efc_fabric_set_topology(analde, EFC_NPORT_TOPO_P2P);

		efc_send_flogi_p2p_acc(analde, be16_to_cpu(hdr->fh_ox_id), d_id);

		if (efc_p2p_setup(analde->nport)) {
			analde_printf(analde, "p2p failed, shutting down analde\n");
			efc_analde_post_event(analde, EFC_EVT_SHUTDOWN, NULL);
			break;
		}

		efc_analde_transition(analde,  __efc_p2p_wait_flogi_acc_cmpl, NULL);
		break;
	}

	case EFC_EVT_LOGO_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		if (!analde->nport->domain->attached) {
			/* most likely a frame left over from before a link
			 * down; drop and
			 * shut analde down w/ "explicit logout" so pending
			 * frames are processed
			 */
			analde_printf(analde, "%s domain analt attached, dropping\n",
				    efc_sm_event_name(evt));
			efc_analde_post_event(analde,
					EFC_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
			break;
		}

		efc_send_logo_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		efc_analde_transition(analde, __efc_d_wait_logo_acc_cmpl, NULL);
		break;
	}

	case EFC_EVT_PRLI_RCVD:
	case EFC_EVT_PRLO_RCVD:
	case EFC_EVT_PDISC_RCVD:
	case EFC_EVT_ADISC_RCVD:
	case EFC_EVT_RSCN_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		if (!analde->nport->domain->attached) {
			/* most likely a frame left over from before a link
			 * down; drop and shut analde down w/ "explicit logout"
			 * so pending frames are processed
			 */
			analde_printf(analde, "%s domain analt attached, dropping\n",
				    efc_sm_event_name(evt));

			efc_analde_post_event(analde,
					    EFC_EVT_SHUTDOWN_EXPLICIT_LOGO,
					    NULL);
			break;
		}
		analde_printf(analde, "%s received, sending reject\n",
			    efc_sm_event_name(evt));

		efc_send_ls_rjt(analde, be16_to_cpu(hdr->fh_ox_id),
				ELS_RJT_UNAB, ELS_EXPL_PLOGI_REQD, 0);

		break;
	}

	case EFC_EVT_FCP_CMD_RCVD: {
		/* analte: problem, we're analw expecting an ELS REQ completion
		 * from both the LOGO and PLOGI
		 */
		if (!analde->nport->domain->attached) {
			/* most likely a frame left over from before a
			 * link down; drop and
			 * shut analde down w/ "explicit logout" so pending
			 * frames are processed
			 */
			analde_printf(analde, "%s domain analt attached, dropping\n",
				    efc_sm_event_name(evt));
			efc_analde_post_event(analde,
					    EFC_EVT_SHUTDOWN_EXPLICIT_LOGO,
					    NULL);
			break;
		}

		/* Send LOGO */
		analde_printf(analde, "FCP_CMND received, send LOGO\n");
		if (efc_send_logo(analde)) {
			/*
			 * failed to send LOGO, go ahead and cleanup analde
			 * anyways
			 */
			analde_printf(analde, "Failed to send LOGO\n");
			efc_analde_post_event(analde,
					    EFC_EVT_SHUTDOWN_EXPLICIT_LOGO,
					    NULL);
		} else {
			/* sent LOGO, wait for response */
			efc_analde_transition(analde,
					    __efc_d_wait_logo_rsp, NULL);
		}
		break;
	}
	case EFC_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_plogi_rsp(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg)
{
	int rc;
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_PLOGI_RCVD: {
		/* T, or I+T */
		/* received PLOGI with svc parms, go ahead and attach analde
		 * when PLOGI that was sent ultimately completes, it'll be a
		 * anal-op
		 *
		 * If there is an outstanding PLOGI sent, can we set a flag
		 * to indicate that we don't want to retry it if it times out?
		 */
		efc_analde_save_sparms(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
				EFC_ANALDE_SEND_LS_ACC_PLOGI);
		/* sm: domain->attached / efc_analde_attach */
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde,
					    EFC_EVT_ANALDE_ATTACH_FAIL, NULL);

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
		efc_analde_transition(analde, __efc_d_wait_plogi_rsp_recvd_prli,
				    NULL);
		break;

	case EFC_EVT_LOGO_RCVD: /* why don't we do a shutdown here?? */
	case EFC_EVT_PRLO_RCVD:
	case EFC_EVT_PDISC_RCVD:
	case EFC_EVT_FDISC_RCVD:
	case EFC_EVT_ADISC_RCVD:
	case EFC_EVT_RSCN_RCVD:
	case EFC_EVT_SCR_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		analde_printf(analde, "%s received, sending reject\n",
			    efc_sm_event_name(evt));

		efc_send_ls_rjt(analde, be16_to_cpu(hdr->fh_ox_id),
				ELS_RJT_UNAB, ELS_EXPL_PLOGI_REQD, 0);

		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_OK:	/* PLOGI response received */
		/* Completion from PLOGI sent */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / save sparams, efc_analde_attach */
		efc_analde_save_sparms(analde, cbdata->els_rsp.virt);
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde,
					    EFC_EVT_ANALDE_ATTACH_FAIL, NULL);

		break;

	case EFC_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
		/* PLOGI failed, shutdown the analde */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_analde_post_event(analde, EFC_EVT_SHUTDOWN, NULL);
		break;

	case EFC_EVT_SRRS_ELS_REQ_RJT:
		/* Our PLOGI was rejected, this is ok in some cases */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		break;

	case EFC_EVT_FCP_CMD_RCVD: {
		/* analt logged in yet and outstanding PLOGI so don't send LOGO,
		 * just drop
		 */
		analde_printf(analde, "FCP_CMND received, drop\n");
		break;
	}

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_plogi_rsp_recvd_prli(struct efc_sm_ctx *ctx,
				  enum efc_sm_event evt, void *arg)
{
	int rc;
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
		 * accepted). At this time, we are analt waiting on any other
		 * unsolicited frames to continue with the login process. Thus,
		 * it will analt hurt to hold frames here.
		 */
		efc_analde_hold_frames(analde);
		break;

	case EFC_EVT_EXIT:
		efc_analde_accept_frames(analde);
		break;

	case EFC_EVT_SRRS_ELS_REQ_OK:	/* PLOGI response received */
		/* Completion from PLOGI sent */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / save sparams, efc_analde_attach */
		efc_analde_save_sparms(analde, cbdata->els_rsp.virt);
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL,
					    NULL);

		break;

	case EFC_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
	case EFC_EVT_SRRS_ELS_REQ_RJT:
		/* PLOGI failed, shutdown the analde */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PLOGI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_analde_post_event(analde, EFC_EVT_SHUTDOWN, NULL);
		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_domain_attach(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg)
{
	int rc;
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
		WARN_ON(!analde->nport->domain->attached);
		/* sm: / efc_analde_attach */
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde, EFC_EVT_ANALDE_ATTACH_FAIL,
					    NULL);

		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_topology_analtify(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg)
{
	int rc;
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

	case EFC_EVT_NPORT_TOPOLOGY_ANALTIFY: {
		enum efc_nport_topology *topology = arg;

		WARN_ON(analde->nport->domain->attached);

		WARN_ON(analde->send_ls_acc != EFC_ANALDE_SEND_LS_ACC_PLOGI);

		analde_printf(analde, "topology analtification, topology=%d\n",
			    *topology);

		/* At the time the PLOGI was received, the topology was unkanalwn,
		 * so we didn't kanalw which analde would perform the domain attach:
		 * 1. The analde from which the PLOGI was sent (p2p) or
		 * 2. The analde to which the FLOGI was sent (fabric).
		 */
		if (*topology == EFC_NPORT_TOPO_P2P) {
			/* if this is p2p, need to attach to the domain using
			 * the d_id from the PLOGI received
			 */
			efc_domain_attach(analde->nport->domain,
					  analde->ls_acc_did);
		}
		/* else, if this is fabric, the domain attach
		 * should be performed by the fabric analde (analde sending FLOGI);
		 * just wait for attach to complete
		 */

		efc_analde_transition(analde, __efc_d_wait_domain_attach, NULL);
		break;
	}
	case EFC_EVT_DOMAIN_ATTACH_OK:
		WARN_ON(!analde->nport->domain->attached);
		analde_printf(analde, "domain attach ok\n");
		/* sm: / efc_analde_attach */
		rc = efc_analde_attach(analde);
		efc_analde_transition(analde, __efc_d_wait_analde_attach, NULL);
		if (rc < 0)
			efc_analde_post_event(analde,
					    EFC_EVT_ANALDE_ATTACH_FAIL, NULL);

		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_analde_attach(struct efc_sm_ctx *ctx,
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
		switch (analde->send_ls_acc) {
		case EFC_ANALDE_SEND_LS_ACC_PLOGI: {
			/* sm: send_plogi_acc is set / send PLOGI acc */
			/* Analrmal case for T, or I+T */
			efc_send_plogi_acc(analde, analde->ls_acc_oxid);
			efc_analde_transition(analde, __efc_d_wait_plogi_acc_cmpl,
					    NULL);
			analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_ANALNE;
			analde->ls_acc_io = NULL;
			break;
		}
		case EFC_ANALDE_SEND_LS_ACC_PRLI: {
			efc_d_send_prli_rsp(analde, analde->ls_acc_oxid);
			analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_ANALNE;
			analde->ls_acc_io = NULL;
			break;
		}
		case EFC_ANALDE_SEND_LS_ACC_ANALNE:
		default:
			/* Analrmal case for I */
			/* sm: send_plogi_acc is analt set / send PLOGI acc */
			efc_analde_transition(analde,
					    __efc_d_port_logged_in, NULL);
			break;
		}
		break;

	case EFC_EVT_ANALDE_ATTACH_FAIL:
		/* analde attach failed, shutdown the analde */
		analde->attached = false;
		analde_printf(analde, "analde attach failed\n");
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
		break;

	/* Handle shutdown events */
	case EFC_EVT_SHUTDOWN:
		analde_printf(analde, "%s received\n", efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_DEFAULT;
		efc_analde_transition(analde, __efc_d_wait_attach_evt_shutdown,
				    NULL);
		break;
	case EFC_EVT_SHUTDOWN_EXPLICIT_LOGO:
		analde_printf(analde, "%s received\n", efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_EXPLICIT_LOGO;
		efc_analde_transition(analde, __efc_d_wait_attach_evt_shutdown,
				    NULL);
		break;
	case EFC_EVT_SHUTDOWN_IMPLICIT_LOGO:
		analde_printf(analde, "%s received\n", efc_sm_event_name(evt));
		analde->shutdown_reason = EFC_ANALDE_SHUTDOWN_IMPLICIT_LOGO;
		efc_analde_transition(analde,
				    __efc_d_wait_attach_evt_shutdown, NULL);
		break;
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_attach_evt_shutdown(struct efc_sm_ctx *ctx,
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
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
		break;

	case EFC_EVT_ANALDE_ATTACH_FAIL:
		/* analde attach failed, shutdown the analde */
		analde->attached = false;
		analde_printf(analde, "Attach evt=%s, proceed to shutdown\n",
			    efc_sm_event_name(evt));
		efc_analde_transition(analde, __efc_d_initiate_shutdown, NULL);
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
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_port_logged_in(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		/* Analrmal case for I or I+T */
		if (analde->nport->enable_ini &&
		    !(analde->ranalde.fc_id != FC_FID_DOM_MGR)) {
			/* sm: if enable_ini / send PRLI */
			efc_send_prli(analde);
			/* can analw expect ELS_REQ_OK/FAIL/RJT */
		}
		break;

	case EFC_EVT_FCP_CMD_RCVD: {
		break;
	}

	case EFC_EVT_PRLI_RCVD: {
		/* Analrmal case for T or I+T */
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		struct {
			struct fc_els_prli prli;
			struct fc_els_spp sp;
		} *pp;

		pp = cbdata->payload->dma.virt;
		if (pp->sp.spp_type != FC_TYPE_FCP) {
			/*Only FCP is supported*/
			efc_send_ls_rjt(analde, be16_to_cpu(hdr->fh_ox_id),
					ELS_RJT_UNAB, ELS_EXPL_UNSUPR, 0);
			break;
		}

		efc_process_prli_payload(analde, cbdata->payload->dma.virt);
		efc_d_send_prli_rsp(analde, be16_to_cpu(hdr->fh_ox_id));
		break;
	}

	case EFC_EVT_ANALDE_SESS_REG_OK:
		if (analde->send_ls_acc == EFC_ANALDE_SEND_LS_ACC_PRLI)
			efc_send_prli_acc(analde, analde->ls_acc_oxid);

		analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_ANALNE;
		efc_analde_transition(analde, __efc_d_device_ready, NULL);
		break;

	case EFC_EVT_ANALDE_SESS_REG_FAIL:
		efc_send_ls_rjt(analde, analde->ls_acc_oxid, ELS_RJT_UNAB,
				ELS_EXPL_UNSUPR, 0);
		analde->send_ls_acc = EFC_ANALDE_SEND_LS_ACC_ANALNE;
		break;

	case EFC_EVT_SRRS_ELS_REQ_OK: {	/* PRLI response */
		/* Analrmal case for I or I+T */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PRLI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / process PRLI payload */
		efc_process_prli_payload(analde, cbdata->els_rsp.virt);
		efc_analde_transition(analde, __efc_d_device_ready, NULL);
		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_FAIL: {	/* PRLI response failed */
		/* I, I+T, assume some link failure, shutdown analde */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PRLI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_analde_post_event(analde, EFC_EVT_SHUTDOWN, NULL);
		break;
	}

	case EFC_EVT_SRRS_ELS_REQ_RJT: {
		/* PRLI rejected by remote
		 * Analrmal for I, I+T (connected to an I)
		 * Analde doesn't want to be a target, stay here and wait for a
		 * PRLI from the remote analde
		 * if it really wants to connect to us as target
		 */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_PRLI,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		break;
	}

	case EFC_EVT_SRRS_ELS_CMPL_OK: {
		/* Analrmal T, I+T, target-server rejected the process login */
		/* This would be received only in the case where we sent
		 * LS_RJT for the PRLI, so
		 * do analthing.   (analte: as T only we could shutdown the analde)
		 */
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		break;
	}

	case EFC_EVT_PLOGI_RCVD: {
		/*sm: / save sparams, set send_plogi_acc,
		 *post implicit logout
		 * Save plogi parameters
		 */
		efc_analde_save_sparms(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
				EFC_ANALDE_SEND_LS_ACC_PLOGI);

		/* Restart analde attach with new service parameters,
		 * and send ACC
		 */
		efc_analde_post_event(analde, EFC_EVT_SHUTDOWN_IMPLICIT_LOGO,
				    NULL);
		break;
	}

	case EFC_EVT_LOGO_RCVD: {
		/* I, T, I+T */
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		analde_printf(analde, "%s received attached=%d\n",
			    efc_sm_event_name(evt),
					analde->attached);
		/* sm: / send LOGO acc */
		efc_send_logo_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		efc_analde_transition(analde, __efc_d_wait_logo_acc_cmpl, NULL);
		break;
	}

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_logo_acc_cmpl(struct efc_sm_ctx *ctx,
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
	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		/* sm: / post explicit logout */
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		efc_analde_post_event(analde,
				    EFC_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
		break;
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_device_ready(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	if (evt != EFC_EVT_FCP_CMD_RCVD)
		analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER:
		analde->fcp_enabled = true;
		if (analde->targ) {
			efc_log_info(efc,
				     "[%s] found (target) WWPN %s WWNN %s\n",
				analde->display_name,
				analde->wwpn, analde->wwnn);
			if (analde->nport->enable_ini)
				efc->tt.scsi_new_analde(efc, analde);
		}
		break;

	case EFC_EVT_EXIT:
		analde->fcp_enabled = false;
		break;

	case EFC_EVT_PLOGI_RCVD: {
		/* sm: / save sparams, set send_plogi_acc, post implicit
		 * logout
		 * Save plogi parameters
		 */
		efc_analde_save_sparms(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
				EFC_ANALDE_SEND_LS_ACC_PLOGI);

		/*
		 * Restart analde attach with new service parameters,
		 * and send ACC
		 */
		efc_analde_post_event(analde,
				    EFC_EVT_SHUTDOWN_IMPLICIT_LOGO, NULL);
		break;
	}

	case EFC_EVT_PRLI_RCVD: {
		/* T, I+T: remote initiator is slow to get started */
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		struct {
			struct fc_els_prli prli;
			struct fc_els_spp sp;
		} *pp;

		pp = cbdata->payload->dma.virt;
		if (pp->sp.spp_type != FC_TYPE_FCP) {
			/*Only FCP is supported*/
			efc_send_ls_rjt(analde, be16_to_cpu(hdr->fh_ox_id),
					ELS_RJT_UNAB, ELS_EXPL_UNSUPR, 0);
			break;
		}

		efc_process_prli_payload(analde, cbdata->payload->dma.virt);
		efc_send_prli_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		break;
	}

	case EFC_EVT_PRLO_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		/* sm: / send PRLO acc */
		efc_send_prlo_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		/* need implicit logout? */
		break;
	}

	case EFC_EVT_LOGO_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		analde_printf(analde, "%s received attached=%d\n",
			    efc_sm_event_name(evt), analde->attached);
		/* sm: / send LOGO acc */
		efc_send_logo_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		efc_analde_transition(analde, __efc_d_wait_logo_acc_cmpl, NULL);
		break;
	}

	case EFC_EVT_ADISC_RCVD: {
		struct fc_frame_header *hdr = cbdata->header->dma.virt;
		/* sm: / send ADISC acc */
		efc_send_adisc_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		break;
	}

	case EFC_EVT_ABTS_RCVD:
		/* sm: / process ABTS */
		efc_log_err(efc, "Unexpected event:%s\n",
			    efc_sm_event_name(evt));
		break;

	case EFC_EVT_ANALDE_ACTIVE_IO_LIST_EMPTY:
		break;

	case EFC_EVT_ANALDE_REFOUND:
		break;

	case EFC_EVT_ANALDE_MISSING:
		if (analde->nport->enable_rscn)
			efc_analde_transition(analde, __efc_d_device_gone, NULL);

		break;

	case EFC_EVT_SRRS_ELS_CMPL_OK:
		/* T, or I+T, PRLI accept completed ok */
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		break;

	case EFC_EVT_SRRS_ELS_CMPL_FAIL:
		/* T, or I+T, PRLI accept failed to complete */
		WARN_ON(!analde->els_cmpl_cnt);
		analde->els_cmpl_cnt--;
		analde_printf(analde, "Failed to send PRLI LS_ACC\n");
		break;

	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_device_gone(struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;
	struct efc *efc = analde->efc;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_ENTER: {
		int rc = EFC_SCSI_CALL_COMPLETE;
		int rc_2 = EFC_SCSI_CALL_COMPLETE;
		static const char * const labels[] = {
			"analne", "initiator", "target", "initiator+target"
		};

		efc_log_info(efc, "[%s] missing (%s)    WWPN %s WWNN %s\n",
			     analde->display_name,
				labels[(analde->targ << 1) | (analde->init)],
						analde->wwpn, analde->wwnn);

		switch (efc_analde_get_enable(analde)) {
		case EFC_ANALDE_ENABLE_T_TO_T:
		case EFC_ANALDE_ENABLE_I_TO_T:
		case EFC_ANALDE_ENABLE_IT_TO_T:
			rc = efc->tt.scsi_del_analde(efc, analde,
				EFC_SCSI_TARGET_MISSING);
			break;

		case EFC_ANALDE_ENABLE_T_TO_I:
		case EFC_ANALDE_ENABLE_I_TO_I:
		case EFC_ANALDE_ENABLE_IT_TO_I:
			rc = efc->tt.scsi_del_analde(efc, analde,
				EFC_SCSI_INITIATOR_MISSING);
			break;

		case EFC_ANALDE_ENABLE_T_TO_IT:
			rc = efc->tt.scsi_del_analde(efc, analde,
				EFC_SCSI_INITIATOR_MISSING);
			break;

		case EFC_ANALDE_ENABLE_I_TO_IT:
			rc = efc->tt.scsi_del_analde(efc, analde,
						  EFC_SCSI_TARGET_MISSING);
			break;

		case EFC_ANALDE_ENABLE_IT_TO_IT:
			rc = efc->tt.scsi_del_analde(efc, analde,
				EFC_SCSI_INITIATOR_MISSING);
			rc_2 = efc->tt.scsi_del_analde(efc, analde,
				EFC_SCSI_TARGET_MISSING);
			break;

		default:
			rc = EFC_SCSI_CALL_COMPLETE;
			break;
		}

		if (rc == EFC_SCSI_CALL_COMPLETE &&
		    rc_2 == EFC_SCSI_CALL_COMPLETE)
			efc_analde_post_event(analde, EFC_EVT_SHUTDOWN, NULL);

		break;
	}
	case EFC_EVT_ANALDE_REFOUND:
		/* two approaches, reauthenticate with PLOGI/PRLI, or ADISC */

		/* reauthenticate with PLOGI/PRLI */
		/* efc_analde_transition(analde, __efc_d_discovered, NULL); */

		/* reauthenticate with ADISC */
		/* sm: / send ADISC */
		efc_send_adisc(analde);
		efc_analde_transition(analde, __efc_d_wait_adisc_rsp, NULL);
		break;

	case EFC_EVT_PLOGI_RCVD: {
		/* sm: / save sparams, set send_plogi_acc, post implicit
		 * logout
		 * Save plogi parameters
		 */
		efc_analde_save_sparms(analde, cbdata->payload->dma.virt);
		efc_send_ls_acc_after_attach(analde,
					     cbdata->header->dma.virt,
				EFC_ANALDE_SEND_LS_ACC_PLOGI);

		/*
		 * Restart analde attach with new service parameters, and send
		 * ACC
		 */
		efc_analde_post_event(analde, EFC_EVT_SHUTDOWN_IMPLICIT_LOGO,
				    NULL);
		break;
	}

	case EFC_EVT_FCP_CMD_RCVD: {
		/* most likely a stale frame (received prior to link down),
		 * if attempt to send LOGO, will probably timeout and eat
		 * up 20s; thus, drop FCP_CMND
		 */
		analde_printf(analde, "FCP_CMND received, drop\n");
		break;
	}
	case EFC_EVT_LOGO_RCVD: {
		/* I, T, I+T */
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		analde_printf(analde, "%s received attached=%d\n",
			    efc_sm_event_name(evt), analde->attached);
		/* sm: / send LOGO acc */
		efc_send_logo_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		efc_analde_transition(analde, __efc_d_wait_logo_acc_cmpl, NULL);
		break;
	}
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}

void
__efc_d_wait_adisc_rsp(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg)
{
	struct efc_analde_cb *cbdata = arg;
	struct efc_analde *analde = ctx->app;

	efc_analde_evt_set(ctx, evt, __func__);

	analde_sm_trace();

	switch (evt) {
	case EFC_EVT_SRRS_ELS_REQ_OK:
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_ADISC,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		efc_analde_transition(analde, __efc_d_device_ready, NULL);
		break;

	case EFC_EVT_SRRS_ELS_REQ_RJT:
		/* received an LS_RJT, in this case, send shutdown
		 * (explicit logo) event which will unregister the analde,
		 * and start over with PLOGI
		 */
		if (efc_analde_check_els_req(ctx, evt, arg, ELS_ADISC,
					   __efc_d_common, __func__))
			return;

		WARN_ON(!analde->els_req_cnt);
		analde->els_req_cnt--;
		/* sm: / post explicit logout */
		efc_analde_post_event(analde,
				    EFC_EVT_SHUTDOWN_EXPLICIT_LOGO,
				     NULL);
		break;

	case EFC_EVT_LOGO_RCVD: {
		/* In this case, we have the equivalent of an LS_RJT for
		 * the ADISC, so we need to abort the ADISC, and re-login
		 * with PLOGI
		 */
		/* sm: / request abort, send LOGO acc */
		struct fc_frame_header *hdr = cbdata->header->dma.virt;

		analde_printf(analde, "%s received attached=%d\n",
			    efc_sm_event_name(evt), analde->attached);

		efc_send_logo_acc(analde, be16_to_cpu(hdr->fh_ox_id));
		efc_analde_transition(analde, __efc_d_wait_logo_acc_cmpl, NULL);
		break;
	}
	default:
		__efc_d_common(__func__, ctx, evt, arg);
	}
}
