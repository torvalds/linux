// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (c)  2021     Marvell
 */
#include "qla_def.h"
#include "qla_edif.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <scsi/scsi_tcq.h>

static void
qla_edif_sa_ctl_init(scsi_qla_host_t *vha, struct fc_port  *fcport)
{
	ql_dbg(ql_dbg_edif, vha, 0x2058,
		"Init SA_CTL List for fcport - nn %8phN pn %8phN portid=%02x%02x%02x.\n",
		fcport->node_name, fcport->port_name,
		fcport->d_id.b.domain, fcport->d_id.b.area,
		fcport->d_id.b.al_pa);

	fcport->edif.tx_rekey_cnt = 0;
	fcport->edif.rx_rekey_cnt = 0;

	fcport->edif.tx_bytes = 0;
	fcport->edif.rx_bytes = 0;
}

/**
 * qla_edif_app_check(): check for valid application id.
 * @vha: host adapter pointer
 * @appid: application id
 * Return: false = fail, true = pass
 */
static bool
qla_edif_app_check(scsi_qla_host_t *vha, struct app_id appid)
{
	/* check that the app is allow/known to the driver */

	if (appid.app_vid == EDIF_APP_ID) {
		ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x911d, "%s app id ok\n", __func__);
		return true;
	}
	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app id not ok (%x)",
	    __func__, appid.app_vid);

	return false;
}

static void qla_edif_reset_auth_wait(struct fc_port *fcport, int state,
		int waitonly)
{
	int cnt, max_cnt = 200;
	bool traced = false;

	fcport->keep_nport_handle = 1;

	if (!waitonly) {
		qla2x00_set_fcport_disc_state(fcport, state);
		qlt_schedule_sess_for_deletion(fcport);
	} else {
		qla2x00_set_fcport_disc_state(fcport, state);
	}

	ql_dbg(ql_dbg_edif, fcport->vha, 0xf086,
		"%s: waiting for session, max_cnt=%u\n",
		__func__, max_cnt);

	cnt = 0;

	if (waitonly) {
		/* Marker wait min 10 msecs. */
		msleep(50);
		cnt += 50;
	}
	while (1) {
		if (!traced) {
			ql_dbg(ql_dbg_edif, fcport->vha, 0xf086,
			    "%s: session sleep.\n",
			    __func__);
			traced = true;
		}
		msleep(20);
		cnt++;
		if (waitonly && (fcport->disc_state == state ||
			fcport->disc_state == DSC_LOGIN_COMPLETE))
			break;
		if (fcport->disc_state == DSC_LOGIN_AUTH_PEND)
			break;
		if (cnt > max_cnt)
			break;
	}

	if (!waitonly) {
		ql_dbg(ql_dbg_edif, fcport->vha, 0xf086,
		    "%s: waited for session - %8phC, loopid=%x portid=%06x fcport=%p state=%u, cnt=%u\n",
		    __func__, fcport->port_name, fcport->loop_id,
		    fcport->d_id.b24, fcport, fcport->disc_state, cnt);
	} else {
		ql_dbg(ql_dbg_edif, fcport->vha, 0xf086,
		    "%s: waited ONLY for session - %8phC, loopid=%x portid=%06x fcport=%p state=%u, cnt=%u\n",
		    __func__, fcport->port_name, fcport->loop_id,
		    fcport->d_id.b24, fcport, fcport->disc_state, cnt);
	}
}

/**
 * qla_edif_app_start:  application has announce its present
 * @vha: host adapter pointer
 * @bsg_job: user request
 *
 * Set/activate doorbell.  Reset current sessions and re-login with
 * secure flag.
 */
static int
qla_edif_app_start(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct app_start	appstart;
	struct app_start_reply	appreply;
	struct fc_port  *fcport, *tf;

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app start\n", __func__);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appstart,
	    sizeof(struct app_start));

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app_vid=%x app_start_flags %x\n",
	     __func__, appstart.app_info.app_vid, appstart.app_start_flags);

	if (vha->e_dbell.db_flags != EDB_ACTIVE) {
		/* mark doorbell as active since an app is now present */
		vha->e_dbell.db_flags = EDB_ACTIVE;
	} else {
		ql_dbg(ql_dbg_edif, vha, 0x911e, "%s doorbell already active\n",
		     __func__);
	}

	list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
		if ((fcport->flags & FCF_FCSP_DEVICE)) {
			ql_dbg(ql_dbg_edif, vha, 0xf084,
			    "%s: sess %p %8phC lid %#04x s_id %06x logout %d\n",
			    __func__, fcport, fcport->port_name,
			    fcport->loop_id, fcport->d_id.b24,
			    fcport->logout_on_delete);

			if (atomic_read(&vha->loop_state) == LOOP_DOWN)
				break;

			if (!fcport->edif.secured_login)
				continue;

			fcport->edif.app_started = 1;
			if (fcport->edif.app_stop ||
			    (fcport->disc_state != DSC_LOGIN_COMPLETE &&
			     fcport->disc_state != DSC_LOGIN_PEND &&
			     fcport->disc_state != DSC_DELETED)) {
				/* no activity */
				fcport->edif.app_stop = 0;

				ql_dbg(ql_dbg_edif, vha, 0x911e,
				    "%s wwpn %8phC calling qla_edif_reset_auth_wait\n",
				    __func__, fcport->port_name);
				fcport->edif.app_sess_online = 1;
				qla_edif_reset_auth_wait(fcport, DSC_LOGIN_PEND, 0);
			}
			qla_edif_sa_ctl_init(vha, fcport);
		}
	}

	if (vha->pur_cinfo.enode_flags != ENODE_ACTIVE) {
		/* mark as active since an app is now present */
		vha->pur_cinfo.enode_flags = ENODE_ACTIVE;
	} else {
		ql_dbg(ql_dbg_edif, vha, 0x911f, "%s enode already active\n",
		     __func__);
	}

	appreply.host_support_edif = vha->hw->flags.edif_enabled;
	appreply.edif_enode_active = vha->pur_cinfo.enode_flags;
	appreply.edif_edb_active = vha->e_dbell.db_flags;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply) +
	    sizeof(struct app_start_reply);

	SET_DID_STATUS(bsg_reply->result, DID_OK);

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, &appreply,
	    sizeof(struct app_start_reply));

	ql_dbg(ql_dbg_edif, vha, 0x911d,
	    "%s app start completed with 0x%x\n",
	    __func__, rval);

	return rval;
}

/**
 * qla_edif_app_stop - app has announced it's exiting.
 * @vha: host adapter pointer
 * @bsg_job: user space command pointer
 *
 * Free any in flight messages, clear all doorbell events
 * to application. Reject any message relate to security.
 */
static int
qla_edif_app_stop(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t                 rval = 0;
	struct app_stop         appstop;
	struct fc_bsg_reply     *bsg_reply = bsg_job->reply;
	struct fc_port  *fcport, *tf;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appstop,
	    sizeof(struct app_stop));

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s Stopping APP: app_vid=%x\n",
	    __func__, appstop.app_info.app_vid);

	/* Call db stop and enode stop functions */

	/* if we leave this running short waits are operational < 16 secs */
	qla_enode_stop(vha);        /* stop enode */
	qla_edb_stop(vha);          /* stop db */

	list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
		if (fcport->edif.non_secured_login)
			continue;

		if (fcport->flags & FCF_FCSP_DEVICE) {
			ql_dbg(ql_dbg_edif, vha, 0xf084,
			    "%s: sess %p from port %8phC lid %#04x s_id %06x logout %d keep %d els_logo %d\n",
			    __func__, fcport,
			    fcport->port_name, fcport->loop_id, fcport->d_id.b24,
			    fcport->logout_on_delete, fcport->keep_nport_handle,
			    fcport->send_els_logo);

			if (atomic_read(&vha->loop_state) == LOOP_DOWN)
				break;

			fcport->edif.app_stop = 1;
			ql_dbg(ql_dbg_edif, vha, 0x911e,
				"%s wwpn %8phC calling qla_edif_reset_auth_wait\n",
				__func__, fcport->port_name);

			fcport->send_els_logo = 1;
			qlt_schedule_sess_for_deletion(fcport);

			/* qla_edif_flush_sa_ctl_lists(fcport); */
			fcport->edif.app_started = 0;
		}
	}

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	SET_DID_STATUS(bsg_reply->result, DID_OK);

	/* no return interface to app - it assumes we cleaned up ok */

	return rval;
}

/**
 * qla_edif_app_getfcinfo - app would like to read session info (wwpn, nportid,
 *   [initiator|target] mode.  It can specific session with specific nport id or
 *   all sessions.
 * @vha: host adapter pointer
 * @bsg_job: user request pointer
 */
static int
qla_edif_app_getfcinfo(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	int32_t			num_cnt = 1;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct app_pinfo_req	app_req;
	struct app_pinfo_reply	*app_reply;
	port_id_t		tdid;

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app get fcinfo\n", __func__);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &app_req,
	    sizeof(struct app_pinfo_req));

	num_cnt = app_req.num_ports;	/* num of ports alloc'd by app */

	app_reply = kzalloc((sizeof(struct app_pinfo_reply) +
	    sizeof(struct app_pinfo) * num_cnt), GFP_KERNEL);
	if (!app_reply) {
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
	} else {
		struct fc_port	*fcport = NULL, *tf;
		uint32_t	pcnt = 0;

		list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
			if (!(fcport->flags & FCF_FCSP_DEVICE))
				continue;

			tdid = app_req.remote_pid;

			ql_dbg(ql_dbg_edif, vha, 0x2058,
			    "APP request entry - portid=%06x.\n",
			    tdid.b24);

			/* Ran out of space */
			if (pcnt > app_req.num_ports)
				break;

			if (tdid.b24 != 0 && tdid.b24 != fcport->d_id.b24)
				continue;

			app_reply->ports[pcnt].remote_type =
				VND_CMD_RTYPE_UNKNOWN;
			if (fcport->port_type & (FCT_NVME_TARGET | FCT_TARGET))
				app_reply->ports[pcnt].remote_type |=
					VND_CMD_RTYPE_TARGET;
			if (fcport->port_type & (FCT_NVME_INITIATOR | FCT_INITIATOR))
				app_reply->ports[pcnt].remote_type |=
					VND_CMD_RTYPE_INITIATOR;

			app_reply->ports[pcnt].remote_pid = fcport->d_id;

			ql_dbg(ql_dbg_edif, vha, 0x2058,
			    "Found FC_SP fcport - nn %8phN pn %8phN pcnt %d portid=%02x%02x%02x.\n",
			    fcport->node_name, fcport->port_name, pcnt,
			    fcport->d_id.b.domain, fcport->d_id.b.area,
			    fcport->d_id.b.al_pa);

			switch (fcport->edif.auth_state) {
			case VND_CMD_AUTH_STATE_ELS_RCVD:
				if (fcport->disc_state == DSC_LOGIN_AUTH_PEND) {
					fcport->edif.auth_state = VND_CMD_AUTH_STATE_NEEDED;
					app_reply->ports[pcnt].auth_state =
						VND_CMD_AUTH_STATE_NEEDED;
				} else {
					app_reply->ports[pcnt].auth_state =
						VND_CMD_AUTH_STATE_ELS_RCVD;
				}
				break;
			default:
				app_reply->ports[pcnt].auth_state = fcport->edif.auth_state;
				break;
			}

			memcpy(app_reply->ports[pcnt].remote_wwpn,
			    fcport->port_name, 8);

			app_reply->ports[pcnt].remote_state =
				(atomic_read(&fcport->state) ==
				    FCS_ONLINE ? 1 : 0);

			pcnt++;

			if (tdid.b24 != 0)
				break;
		}
		app_reply->port_count = pcnt;
		SET_DID_STATUS(bsg_reply->result, DID_OK);
	}

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, app_reply,
	    sizeof(struct app_pinfo_reply) + sizeof(struct app_pinfo) * num_cnt);

	kfree(app_reply);

	return rval;
}

/**
 * qla_edif_app_getstats - app would like to read various statistics info
 * @vha: host adapter pointer
 * @bsg_job: user request
 */
static int32_t
qla_edif_app_getstats(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	uint32_t ret_size, size;

	struct app_sinfo_req	app_req;
	struct app_stats_reply	*app_reply;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &app_req,
	    sizeof(struct app_sinfo_req));
	if (app_req.num_ports == 0) {
		ql_dbg(ql_dbg_async, vha, 0x911d,
		   "%s app did not indicate number of ports to return\n",
		    __func__);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
	}

	size = sizeof(struct app_stats_reply) +
	    (sizeof(struct app_sinfo) * app_req.num_ports);

	if (size > bsg_job->reply_payload.payload_len)
		ret_size = bsg_job->reply_payload.payload_len;
	else
		ret_size = size;

	app_reply = kzalloc(size, GFP_KERNEL);
	if (!app_reply) {
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
	} else {
		struct fc_port	*fcport = NULL, *tf;
		uint32_t	pcnt = 0;

		list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
			if (fcport->edif.enable) {
				if (pcnt > app_req.num_ports)
					break;

				app_reply->elem[pcnt].rekey_count =
				    fcport->edif.rekey_cnt;
				app_reply->elem[pcnt].tx_bytes =
				    fcport->edif.tx_bytes;
				app_reply->elem[pcnt].rx_bytes =
				    fcport->edif.rx_bytes;

				memcpy(app_reply->elem[pcnt].remote_wwpn,
				    fcport->port_name, 8);

				pcnt++;
			}
		}
		app_reply->elem_count = pcnt;
		SET_DID_STATUS(bsg_reply->result, DID_OK);
	}

	bsg_reply->reply_payload_rcv_len =
	    sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	       bsg_job->reply_payload.sg_cnt, app_reply, ret_size);

	kfree(app_reply);

	return rval;
}

int32_t
qla_edif_app_mgmt(struct bsg_job *bsg_job)
{
	struct fc_bsg_request	*bsg_request = bsg_job->request;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t		*vha = shost_priv(host);
	struct app_id		appcheck;
	bool done = true;
	int32_t         rval = 0;
	uint32_t	vnd_sc = bsg_request->rqst_data.h_vendor.vendor_cmd[1];

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s vnd subcmd=%x\n",
	    __func__, vnd_sc);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appcheck,
	    sizeof(struct app_id));

	if (!vha->hw->flags.edif_enabled ||
		test_bit(VPORT_DELETE, &vha->dpc_flags)) {
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s edif not enabled or vp delete. bsg ptr done %p\n",
		    __func__, bsg_job);

		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;
	}

	if (!qla_edif_app_check(vha, appcheck)) {
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s app checked failed.\n",
		    __func__);

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;
	}

	switch (vnd_sc) {
	case QL_VND_SC_APP_START:
		rval = qla_edif_app_start(vha, bsg_job);
		break;
	case QL_VND_SC_APP_STOP:
		rval = qla_edif_app_stop(vha, bsg_job);
		break;
	case QL_VND_SC_GET_FCINFO:
		rval = qla_edif_app_getfcinfo(vha, bsg_job);
		break;
	case QL_VND_SC_GET_STATS:
		rval = qla_edif_app_getstats(vha, bsg_job);
		break;
	default:
		ql_dbg(ql_dbg_edif, vha, 0x911d, "%s unknown cmd=%x\n",
		    __func__,
		    bsg_request->rqst_data.h_vendor.vendor_cmd[1]);
		rval = EXT_STATUS_INVALID_PARAM;
		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		break;
	}

done:
	if (done) {
		ql_dbg(ql_dbg_user, vha, 0x7009,
		    "%s: %d  bsg ptr done %p\n", __func__, __LINE__, bsg_job);
		bsg_job_done(bsg_job, bsg_reply->result,
		    bsg_reply->reply_payload_rcv_len);
	}

	return rval;
}

void
qla_enode_stop(scsi_qla_host_t *vha)
{
	if (vha->pur_cinfo.enode_flags != ENODE_ACTIVE) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s enode not active\n", __func__);
		return;
	}
}

/* function called when app is stopping */

void
qla_edb_stop(scsi_qla_host_t *vha)
{
	if (vha->e_dbell.db_flags != EDB_ACTIVE) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s doorbell not enabled\n", __func__);
		return;
	}
}
