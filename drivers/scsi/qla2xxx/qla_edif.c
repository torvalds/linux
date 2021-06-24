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

static int qla_pur_get_pending(scsi_qla_host_t *, fc_port_t *, struct bsg_job *);

static struct els_sub_cmd {
	uint16_t cmd;
	const char *str;
} sc_str[] = {
	{SEND_ELS, "send ELS"},
	{SEND_ELS_REPLY, "send ELS Reply"},
	{PULL_ELS, "retrieve ELS"},
};

const char *sc_to_str(uint16_t cmd)
{
	int i;
	struct els_sub_cmd *e;

	for (i = 0; i < ARRAY_SIZE(sc_str); i++) {
		e = sc_str + i;
		if (cmd == e->cmd)
			return e->str;
	}
	return "unknown";
}

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

static int qla_bsg_check(scsi_qla_host_t *vha, struct bsg_job *bsg_job,
fc_port_t *fcport)
{
	struct extra_auth_els *p;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct qla_bsg_auth_els_request *req =
	    (struct qla_bsg_auth_els_request *)bsg_job->request;

	if (!vha->hw->flags.edif_enabled) {
		/* edif support not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x9105,
		    "%s edif not enabled\n", __func__);
		goto done;
	}
	if (vha->e_dbell.db_flags != EDB_ACTIVE) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s doorbell not enabled\n", __func__);
		goto done;
	}

	p = &req->e;

	/* Get response */
	if (p->sub_cmd == PULL_ELS) {
		struct qla_bsg_auth_els_reply *rpl =
			(struct qla_bsg_auth_els_reply *)bsg_job->reply;

		qla_pur_get_pending(vha, fcport, bsg_job);

		ql_dbg(ql_dbg_edif, vha, 0x911d,
			"%s %s %8phN sid=%x. xchg %x, nb=%xh bsg ptr %p\n",
			__func__, sc_to_str(p->sub_cmd), fcport->port_name,
			fcport->d_id.b24, rpl->rx_xchg_address,
			rpl->r.reply_payload_rcv_len, bsg_job);

		goto done;
	}
	return 0;

done:

	bsg_job_done(bsg_job, bsg_reply->result,
			bsg_reply->reply_payload_rcv_len);
	return -EIO;
}

fc_port_t *
qla2x00_find_fcport_by_pid(scsi_qla_host_t *vha, port_id_t *id)
{
	fc_port_t *f, *tf;

	f = NULL;
	list_for_each_entry_safe(f, tf, &vha->vp_fcports, list) {
		if ((f->flags & FCF_FCSP_DEVICE)) {
			ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x2058,
			    "Found secure fcport - nn %8phN pn %8phN portid=0x%x, 0x%x.\n",
			    f->node_name, f->port_name,
			    f->d_id.b24, id->b24);
			if (f->d_id.b24 == id->b24)
				return f;
		}
	}
	return NULL;
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

static void
qla_enode_free(scsi_qla_host_t *vha, struct enode *node)
{
	node->ntype = N_UNDEF;
	kfree(node);
}

/**
 * qla_enode_init - initialize enode structs & lock
 * @vha: host adapter pointer
 *
 * should only be called when driver attaching
 */
void
qla_enode_init(scsi_qla_host_t *vha)
{
	struct	qla_hw_data *ha = vha->hw;
	char	name[32];

	if (vha->pur_cinfo.enode_flags == ENODE_ACTIVE) {
		/* list still active - error */
		ql_dbg(ql_dbg_edif, vha, 0x09102, "%s enode still active\n",
		    __func__);
		return;
	}

	/* initialize lock which protects pur_core & init list */
	spin_lock_init(&vha->pur_cinfo.pur_lock);
	INIT_LIST_HEAD(&vha->pur_cinfo.head);

	snprintf(name, sizeof(name), "%s_%d_purex", QLA2XXX_DRIVER_NAME,
	    ha->pdev->device);
}

/**
 * qla_enode_stop - stop and clear and enode data
 * @vha: host adapter pointer
 *
 * called when app notified it is exiting
 */
void
qla_enode_stop(scsi_qla_host_t *vha)
{
	unsigned long flags;
	struct enode *node, *q;

	if (vha->pur_cinfo.enode_flags != ENODE_ACTIVE) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s enode not active\n", __func__);
		return;
	}

	/* grab lock so list doesn't move */
	spin_lock_irqsave(&vha->pur_cinfo.pur_lock, flags);

	vha->pur_cinfo.enode_flags &= ~ENODE_ACTIVE; /* mark it not active */

	/* hopefully this is a null list at this point */
	list_for_each_entry_safe(node, q, &vha->pur_cinfo.head, list) {
		ql_dbg(ql_dbg_edif, vha, 0x910f,
		    "%s freeing enode type=%x, cnt=%x\n", __func__, node->ntype,
		    node->dinfo.nodecnt);
		list_del_init(&node->list);
		qla_enode_free(vha, node);
	}
	spin_unlock_irqrestore(&vha->pur_cinfo.pur_lock, flags);
}

static struct enode *
qla_enode_find(scsi_qla_host_t *vha, uint32_t ntype, uint32_t p1, uint32_t p2)
{
	struct enode		*node_rtn = NULL;
	struct enode		*list_node = NULL;
	unsigned long		flags;
	struct list_head	*pos, *q;
	uint32_t		sid;
	uint32_t		rw_flag;
	struct purexevent	*purex;

	/* secure the list from moving under us */
	spin_lock_irqsave(&vha->pur_cinfo.pur_lock, flags);

	list_for_each_safe(pos, q, &vha->pur_cinfo.head) {
		list_node = list_entry(pos, struct enode, list);

		/* node type determines what p1 and p2 are */
		purex = &list_node->u.purexinfo;
		sid = p1;
		rw_flag = p2;

		if (purex->pur_info.pur_sid.b24 == sid) {
			if (purex->pur_info.pur_pend == 1 &&
			    rw_flag == PUR_GET) {
				/*
				 * if the receive is in progress
				 * and its a read/get then can't
				 * transfer yet
				 */
				ql_dbg(ql_dbg_edif, vha, 0x9106,
				    "%s purex xfer in progress for sid=%x\n",
				    __func__, sid);
			} else {
				/* found it and its complete */
				node_rtn = list_node;
				list_del(pos);
				break;
			}
		}
	}

	spin_unlock_irqrestore(&vha->pur_cinfo.pur_lock, flags);

	return node_rtn;
}

/**
 * qla_pur_get_pending - read/return authentication message sent
 *  from remote port
 * @vha: host adapter pointer
 * @fcport: session pointer
 * @bsg_job: user request where the message is copy to.
 */
static int
qla_pur_get_pending(scsi_qla_host_t *vha, fc_port_t *fcport,
	struct bsg_job *bsg_job)
{
	struct enode		*ptr;
	struct purexevent	*purex;
	struct qla_bsg_auth_els_reply *rpl =
	    (struct qla_bsg_auth_els_reply *)bsg_job->reply;

	bsg_job->reply_len = sizeof(*rpl);

	ptr = qla_enode_find(vha, N_PUREX, fcport->d_id.b24, PUR_GET);
	if (!ptr) {
		ql_dbg(ql_dbg_edif, vha, 0x9111,
		    "%s no enode data found for %8phN sid=%06x\n",
		    __func__, fcport->port_name, fcport->d_id.b24);
		SET_DID_STATUS(rpl->r.result, DID_IMM_RETRY);
		return -EIO;
	}

	/*
	 * enode is now off the linked list and is ours to deal with
	 */
	purex = &ptr->u.purexinfo;

	/* Copy info back to caller */
	rpl->rx_xchg_address = purex->pur_info.pur_rx_xchg_address;

	SET_DID_STATUS(rpl->r.result, DID_OK);
	rpl->r.reply_payload_rcv_len =
	    sg_pcopy_from_buffer(bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, purex->msgp,
		purex->pur_info.pur_bytes_rcvd, 0);

	/* data copy / passback completed - destroy enode */
	qla_enode_free(vha, ptr);

	return 0;
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

static void qla_parse_auth_els_ctl(struct srb *sp)
{
	struct qla_els_pt_arg *a = &sp->u.bsg_cmd.u.els_arg;
	struct bsg_job *bsg_job = sp->u.bsg_cmd.bsg_job;
	struct fc_bsg_request *request = bsg_job->request;
	struct qla_bsg_auth_els_request *p =
	    (struct qla_bsg_auth_els_request *)bsg_job->request;

	a->tx_len = a->tx_byte_count = sp->remap.req.len;
	a->tx_addr = sp->remap.req.dma;
	a->rx_len = a->rx_byte_count = sp->remap.rsp.len;
	a->rx_addr = sp->remap.rsp.dma;

	if (p->e.sub_cmd == SEND_ELS_REPLY) {
		a->control_flags = p->e.extra_control_flags << 13;
		a->rx_xchg_address = cpu_to_le32(p->e.extra_rx_xchg_address);
		if (p->e.extra_control_flags == BSG_CTL_FLAG_LS_ACC)
			a->els_opcode = ELS_LS_ACC;
		else if (p->e.extra_control_flags == BSG_CTL_FLAG_LS_RJT)
			a->els_opcode = ELS_LS_RJT;
	}
	a->did = sp->fcport->d_id;
	a->els_opcode =  request->rqst_data.h_els.command_code;
	a->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	a->vp_idx = sp->vha->vp_idx;
}

int qla_edif_process_els(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	fc_port_t *fcport = NULL;
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	int rval =  (DID_ERROR << 16);
	port_id_t d_id;
	struct qla_bsg_auth_els_request *p =
	    (struct qla_bsg_auth_els_request *)bsg_job->request;

	d_id.b.al_pa = bsg_request->rqst_data.h_els.port_id[2];
	d_id.b.area = bsg_request->rqst_data.h_els.port_id[1];
	d_id.b.domain = bsg_request->rqst_data.h_els.port_id[0];

	/* find matching d_id in fcport list */
	fcport = qla2x00_find_fcport_by_pid(vha, &d_id);
	if (!fcport) {
		ql_dbg(ql_dbg_edif, vha, 0x911a,
		    "%s fcport not find online portid=%06x.\n",
		    __func__, d_id.b24);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		return -EIO;
	}

	if (qla_bsg_check(vha, bsg_job, fcport))
		return 0;

	if (fcport->loop_id == FC_NO_LOOP_ID) {
		ql_dbg(ql_dbg_edif, vha, 0x910d,
		    "%s ELS code %x, no loop id.\n", __func__,
		    bsg_request->rqst_data.r_els.els_code);
		SET_DID_STATUS(bsg_reply->result, DID_BAD_TARGET);
		return -ENXIO;
	}

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x7005, "Host not online.\n");
		SET_DID_STATUS(bsg_reply->result, DID_BAD_TARGET);
		rval = -EIO;
		goto done;
	}

	/* pass through is supported only for ISP 4Gb or higher */
	if (!IS_FWI2_CAPABLE(ha)) {
		ql_dbg(ql_dbg_user, vha, 0x7001,
		    "ELS passthru not supported for ISP23xx based adapters.\n");
		SET_DID_STATUS(bsg_reply->result, DID_BAD_TARGET);
		rval = -EPERM;
		goto done;
	}

	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		ql_dbg(ql_dbg_user, vha, 0x7004,
		    "Failed get sp pid=%06x\n", fcport->d_id.b24);
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done;
	}

	sp->remap.req.len = bsg_job->request_payload.payload_len;
	sp->remap.req.buf = dma_pool_alloc(ha->purex_dma_pool,
	    GFP_KERNEL, &sp->remap.req.dma);
	if (!sp->remap.req.buf) {
		ql_dbg(ql_dbg_user, vha, 0x7005,
		    "Failed allocate request dma len=%x\n",
		    bsg_job->request_payload.payload_len);
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done_free_sp;
	}

	sp->remap.rsp.len = bsg_job->reply_payload.payload_len;
	sp->remap.rsp.buf = dma_pool_alloc(ha->purex_dma_pool,
	    GFP_KERNEL, &sp->remap.rsp.dma);
	if (!sp->remap.rsp.buf) {
		ql_dbg(ql_dbg_user, vha, 0x7006,
		    "Failed allocate response dma len=%x\n",
		    bsg_job->reply_payload.payload_len);
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done_free_remap_req;
	}
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, sp->remap.req.buf,
	    sp->remap.req.len);
	sp->remap.remapped = true;

	sp->type = SRB_ELS_CMD_HST_NOLOGIN;
	sp->name = "SPCN_BSG_HST_NOLOGIN";
	sp->u.bsg_cmd.bsg_job = bsg_job;
	qla_parse_auth_els_ctl(sp);

	sp->free = qla2x00_bsg_sp_free;
	sp->done = qla2x00_bsg_job_done;

	rval = qla2x00_start_sp(sp);

	ql_dbg(ql_dbg_edif, vha, 0x700a,
	    "%s %s %8phN xchg %x ctlflag %x hdl %x reqlen %xh bsg ptr %p\n",
	    __func__, sc_to_str(p->e.sub_cmd), fcport->port_name,
	    p->e.extra_rx_xchg_address, p->e.extra_control_flags,
	    sp->handle, sp->remap.req.len, bsg_job);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x700e,
		    "qla2x00_start_sp failed = %d\n", rval);
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		rval = -EIO;
		goto done_free_remap_rsp;
	}
	return rval;

done_free_remap_rsp:
	dma_pool_free(ha->purex_dma_pool, sp->remap.rsp.buf,
	    sp->remap.rsp.dma);
done_free_remap_req:
	dma_pool_free(ha->purex_dma_pool, sp->remap.req.buf,
	    sp->remap.req.dma);
done_free_sp:
	qla2x00_rel_sp(sp);

done:
	return rval;
}
